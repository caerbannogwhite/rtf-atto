// Compares every fixture with its committed goldens, byte for byte.
// Usage: test_goldens <fixtures_dir>
// For each <name>.rtf the directory must hold <name>.json (the document) and
// <name>.tables.json (the logical tables). test/tools/regen_goldens.py writes them.
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <system_error>
#include <variant>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "rtf/json.hpp"
#include "rtf/model.hpp"
#include "rtf/reader.hpp"
#include "rtf/tables.hpp"

namespace fs = std::filesystem;

static const char* const kNotRtf = "{\"error\": \"not_rtf\"}\n";

// Reads the whole file into `out`. Returns false when it cannot be opened.
static bool read_file(const fs::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

// Returns line number `n` (from 0) of `text`, without its newline.
static std::string line_at(const std::string& text, std::size_t n) {
  std::size_t start = 0;
  for (std::size_t i = 0; i < n; ++i) {
    std::size_t next = text.find('\n', start);
    if (next == std::string::npos) return std::string();
    start = next + 1;
  }
  std::size_t end = text.find('\n', start);
  return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Compares `actual` with the golden file and reports the first differing line.
static void compare_with_golden(const fs::path& golden, const std::string& actual) {
  std::string expected;
  if (!read_file(golden, expected)) {
    std::fprintf(stderr, "missing golden: %s\n", golden.string().c_str());
    CHECK(!"golden file exists");
    return;
  }
  if (expected == actual) return;
  std::size_t at = 0;
  std::size_t limit = std::min(expected.size(), actual.size());
  while (at < limit && expected[at] == actual[at]) ++at;
  std::size_t line = static_cast<std::size_t>(std::count(expected.begin(), expected.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
  std::fprintf(stderr, "%s differs at line %zu\n  golden: %s\n  actual: %s\n", golden.string().c_str(), line + 1,
               line_at(expected, line).c_str(), line_at(actual, line).c_str());
  CHECK(!"output equals golden");
}

// Lists the *.rtf files of the directory, sorted by name.
static std::vector<fs::path> list_fixtures(const fs::path& dir) {
  std::vector<fs::path> out;
  std::error_code ec;
  fs::directory_iterator it(dir, ec);
  if (ec) {
    std::fprintf(stderr, "cannot list %s\n", dir.string().c_str());
    CHECK(!"fixtures directory exists");
    return out;
  }
  for (; it != fs::directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it->path().extension() == ".rtf") out.push_back(it->path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

static std::string document_json(const rtf::ParseResult& result) {
  return result.ok ? rtf::to_json(result.document) : std::string(kNotRtf);
}
static std::string tables_json(const rtf::ParseResult& result) {
  return result.ok ? rtf::to_json(rtf::logical_tables(result.document)) : std::string(kNotRtf);
}

// First pass: every fixture against its two goldens, and parsing twice gives the same JSON.
static void goldens(const std::vector<fs::path>& fixtures) {
  for (const auto& path : fixtures) {
    std::string bytes;
    CHECK(read_file(path, bytes));
    rtf::ParseResult first = rtf::parse(bytes);
    std::string doc = document_json(first);
    fs::path base = path;
    base.replace_extension();
    compare_with_golden(fs::path(base.string() + ".json"), doc);
    compare_with_golden(fs::path(base.string() + ".tables.json"), tables_json(first));
    rtf::ParseResult second = rtf::parse(bytes);
    CHECK(document_json(second) == doc);
  }
  CHECK(!fixtures.empty());
}

// ---------------------------------------------------------------------------
// Second pass: the facts about named fixtures that were checked by eye in the
// goldens, written against the parsed model so that the goldens cannot drift.

// Every paragraph, inline, cell and table of a part of the document, nested ones included.
struct Inventory {
  std::vector<const rtf::Paragraph*> paragraphs;
  std::vector<const rtf::Inline*> inlines;
  std::vector<const rtf::Cell*> cells;
  std::vector<const rtf::Table*> tables;
  std::vector<const rtf::Picture*> pictures;  // picture inlines and the pictures of shapes
};

static void collect(const std::vector<rtf::Block>& blocks, Inventory& inv);

static void collect(const std::vector<rtf::Inline>& inlines, Inventory& inv) {
  for (const auto& item : inlines) {
    inv.inlines.push_back(&item);
    if (const auto* f = std::get_if<rtf::Field>(&item.value)) collect(f->result, inv);
    if (const auto* n = std::get_if<rtf::Footnote>(&item.value)) collect(n->blocks, inv);
    if (const auto* p = std::get_if<rtf::Picture>(&item.value)) inv.pictures.push_back(p);
    if (const auto* s = std::get_if<rtf::Shape>(&item.value)) {
      collect(s->blocks, inv);
      if (s->picture) inv.pictures.push_back(&*s->picture);
    }
    if (const auto* o = std::get_if<rtf::EmbeddedObject>(&item.value)) collect(o->result, inv);
  }
}

static void collect(const std::vector<rtf::Block>& blocks, Inventory& inv) {
  for (const auto& block : blocks) {
    if (const auto* p = std::get_if<rtf::Paragraph>(&block.value)) {
      inv.paragraphs.push_back(p);
      collect(p->listText, inv);
      collect(p->runs, inv);
    } else if (const auto* t = std::get_if<rtf::Table>(&block.value)) {
      inv.tables.push_back(t);
      for (const auto& row : t->rows) {
        for (const auto& cell : row.cells) {
          inv.cells.push_back(&cell);
          collect(cell.blocks, inv);
        }
      }
    }
  }
}

static void collect(const rtf::HeaderSet& set, Inventory& inv) {
  for (const auto* part : {&set.default_, &set.first, &set.left, &set.right}) {
    if (*part) collect(**part, inv);
  }
}

// The whole document: headers, footers and body of every section.
static Inventory inventory(const rtf::RtfDocument& doc) {
  Inventory inv;
  for (const auto& section : doc.sections) {
    collect(section.headers, inv);
    collect(section.footers, inv);
    collect(section.body, inv);
  }
  return inv;
}
static Inventory inventory(const std::vector<rtf::Block>& blocks) {
  Inventory inv;
  collect(blocks, inv);
  return inv;
}

template <class T>
static std::vector<const T*> inlines_of(const Inventory& inv) {
  std::vector<const T*> out;
  for (const auto* item : inv.inlines) {
    if (const auto* x = std::get_if<T>(&item->value)) out.push_back(x);
  }
  return out;
}

static std::string g_dir;

static rtf::ParseResult parse_fixture(const char* name) {
  std::string bytes;
  if (!read_file(fs::path(g_dir) / name, bytes)) {
    std::fprintf(stderr, "cannot read fixture %s\n", name);
    CHECK(!"fixture exists");
  }
  return rtf::parse(bytes);
}

static bool is_empty_paragraph(const rtf::Block& block) {
  const auto* p = std::get_if<rtf::Paragraph>(&block.value);
  return p != nullptr && p->runs.empty();
}
static const rtf::Table* as_table(const rtf::Block& block) { return std::get_if<rtf::Table>(&block.value); }

// The distinct warning codes, sorted.
static std::vector<std::string> codes(const rtf::RtfDocument& doc) {
  std::vector<std::string> out;
  for (const auto& w : doc.warnings) out.push_back(w.code);
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}
static std::size_t count_code(const rtf::RtfDocument& doc, const char* code) {
  return static_cast<std::size_t>(std::count_if(doc.warnings.begin(), doc.warnings.end(),
                                                [&](const rtf::Warning& w) { return w.code == code; }));
}
static bool skipped(const rtf::RtfDocument& doc, const char* word) {
  std::string message = std::string("destination \\") + word + " skipped";
  for (const auto& w : doc.warnings) {
    if (w.code == "skipped_destination" && w.message == message) return true;
  }
  return false;
}

// Headers {table of 3 rows, empty paragraph}, footers {empty paragraph, table},
// a PAGE and a NUMPAGES field without results, and two header rows bottom-aligned.
static void check_sas_page(const rtf::Section& section, std::size_t title_rows) {
  CHECK(section.headers.default_.has_value());
  CHECK(section.footers.default_.has_value());
  if (!section.headers.default_ || !section.footers.default_) return;
  const auto& header = *section.headers.default_;
  const auto& footer = *section.footers.default_;
  CHECK(header.size() == 2);
  CHECK(footer.size() == 2);
  if (header.size() != 2 || footer.size() != 2) return;
  CHECK(as_table(header[0]) != nullptr && as_table(header[0])->rows.size() == title_rows);
  CHECK(is_empty_paragraph(header[1]));
  CHECK(is_empty_paragraph(footer[0]));
  CHECK(as_table(footer[1]) != nullptr);
  auto fields = inlines_of<rtf::Field>(inventory(header));
  CHECK(fields.size() == 2);
  if (fields.size() == 2) {
    CHECK(fields[0]->instruction == "PAGE" && fields[0]->result.empty());
    CHECK(fields[1]->instruction == "NUMPAGES" && fields[1]->result.empty());
  }
  CHECK(!section.body.empty());
  const rtf::Table* body = section.body.empty() ? nullptr : as_table(section.body[0]);
  CHECK(body != nullptr && body->rows.size() > 2);
  if (body == nullptr || body->rows.size() <= 2) return;
  CHECK(body->rows[0].props.header && body->rows[1].props.header && !body->rows[2].props.header);
  for (std::size_t r = 0; r < 2; ++r) {
    for (const auto& cell : body->rows[r].cells) CHECK(cell.verticalAlign == rtf::VerticalAlign::Bottom);
  }
}

// One bookmark start and one end in the whole document.
static void check_one_bookmark(const rtf::RtfDocument& doc) {
  auto marks = inlines_of<rtf::Bookmark>(inventory(doc));
  CHECK(marks.size() == 2);
  if (marks.size() == 2) CHECK(!marks[0]->end && marks[1]->end);
}

// Spacer rows exist, and a row is a spacer exactly when it is one empty cell.
static void check_spacers(const rtf::LogicalTable& t) {
  std::size_t spacers = 0;
  for (const auto& row : t.rows) {
    bool empty_single = row.cells.size() == 1 && row.cells[0].text.empty();
    CHECK(row.spacer == empty_single);
    if (row.spacer) ++spacers;
  }
  CHECK(spacers > 0);
}

static bool any_cell_text_has(const rtf::LogicalTable& t, const std::string& needle) {
  for (const auto& row : t.rows) {
    for (const auto& cell : row.cells) {
      if (cell.text.find(needle) != std::string::npos) return true;
    }
  }
  return false;
}

// No section holds more than one part of the logical tables.
static void check_one_table_per_section(const std::vector<rtf::LogicalTable>& tables) {
  std::vector<int> sections;
  for (const auto& t : tables) {
    for (const auto& part : t.parts) sections.push_back(part.section);
  }
  std::sort(sections.begin(), sections.end());
  CHECK(std::adjacent_find(sections.begin(), sections.end()) == sections.end());
}

static void sas_table_3pages() {
  rtf::ParseResult r = parse_fixture("sas_table_3pages.rtf");
  CHECK(r.ok);
  const auto& doc = r.document;
  CHECK(doc.sections.size() == 3);
  for (const auto& section : doc.sections) check_sas_page(section, 3);
  check_one_bookmark(doc);
  CHECK(doc.warnings.empty());
  const std::string nbsp = "\xC2\xA0";
  bool model_has_nbsp = false;
  for (const auto* run : inlines_of<rtf::TextRun>(inventory(doc))) {
    if (run->text.find(nbsp) != std::string::npos) model_has_nbsp = true;
  }
  CHECK(model_has_nbsp);
  std::string json = rtf::to_json(doc);
  CHECK(json.find(nbsp) != std::string::npos);  // written raw, not escaped
  CHECK(json.find(std::string("\\") + "u00a0") == std::string::npos);

  auto tables = rtf::logical_tables(doc);
  CHECK(tables.size() == 1);
  if (tables.size() != 1) return;
  const auto& t = tables[0];
  CHECK(t.firstSection == 0 && t.lastSection == 2);
  CHECK(t.parts.size() == 3);
  CHECK(t.edges.size() == 5);
  std::vector<std::string> names = {"column_1", "Treatment group Placebo (N=20)", "Treatment group Drug A (N=21)",
                                    "Treatment group Total (N=41)"};
  CHECK(t.columnNames == names);
  check_spacers(t);
  bool indent288 = false, indent576 = false;
  for (const auto& row : t.rows) {
    if (row.indent == 288) indent288 = true;
    if (row.indent == 576) indent576 = true;
  }
  CHECK(indent288 && indent576);
  CHECK(any_cell_text_has(t, nbsp));
}

static void sas_listing_1page() {
  rtf::ParseResult r = parse_fixture("sas_listing_1page.rtf");
  CHECK(r.ok);
  auto tables = rtf::logical_tables(r.document);
  CHECK(tables.size() == 1);
  if (tables.size() != 1) return;
  std::size_t one_column = 0;
  for (const auto& row : tables[0].rows) {
    if (row.cells.size() != 1) continue;
    ++one_column;
    CHECK(row.cells[0].span == 3);
  }
  CHECK(one_column == 3);
}

static void word_minimal() {
  rtf::ParseResult r = parse_fixture("word_minimal.rtf");
  CHECK(r.ok);
  for (const char* word : {"themedata", "datastore", "latentstyles", "rsidtbl", "xmlnstbl", "colorschememapping",
                           "listtable", "listoverridetable", "mmathPr"}) {
    if (!skipped(r.document, word)) std::fprintf(stderr, "word_minimal: no skipped_destination for %s\n", word);
    CHECK(skipped(r.document, word));
  }
  CHECK(count_code(r.document, "unknown_destination") == 0);
}

static void word_tables() {
  rtf::ParseResult r = parse_fixture("word_tables.rtf");
  CHECK(r.ok);
  Inventory inv = inventory(r.document);
  bool vertical = false, horizontal = false;
  const rtf::Cell* nested = nullptr;
  for (const auto* cell : inv.cells) {
    if (cell->mergeVertical) vertical = true;
    if (cell->mergeHorizontal) horizontal = true;
    for (const auto& block : cell->blocks) {
      if (as_table(block) != nullptr) nested = cell;
    }
  }
  CHECK(vertical && horizontal);
  CHECK(count_code(r.document, "row_not_closed") == 1);
  CHECK(nested != nullptr);
  if (nested != nullptr) {
    // The cell holds the nested table and nothing from \nonesttables.
    CHECK(nested->blocks.size() == 1);
  }
}

static void word_text() {
  rtf::ParseResult r = parse_fixture("word_text.rtf");
  CHECK(r.ok);
  CHECK(rtf::to_json(r.document).find("\xEF\xBF\xBD") == std::string::npos);  // U+FFFD
  CHECK(count_code(r.document, "symbol_font") > 0);
}

static void pictures() {
  rtf::ParseResult r = parse_fixture("pictures.rtf");
  CHECK(r.ok);
  Inventory inv = inventory(r.document);
  CHECK(inv.pictures.size() == 5);
  std::size_t shapes_with_picture = 0;
  for (const auto* shape : inlines_of<rtf::Shape>(inv)) {
    if (shape->picture) ++shapes_with_picture;
  }
  CHECK(shapes_with_picture == 1);
  CHECK(count_code(r.document, "shape_geometry_dropped") > 0);
  CHECK(count_code(r.document, "object_data_dropped") > 0);
}

static void malformed() {
  CHECK(!parse_fixture("malformed_not_rtf.rtf").ok);
  CHECK(!parse_fixture("malformed_empty.rtf").ok);
  rtf::ParseResult deep = parse_fixture("malformed_deep_nesting.rtf");
  CHECK(deep.ok);
  CHECK(count_code(deep.document, "depth_limit") > 0);
  std::string text;
  for (const auto* run : inlines_of<rtf::TextRun>(inventory(deep.document))) text += run->text;
  CHECK(text == "x");
}

static void msword_disposition() {
  rtf::ParseResult r = parse_fixture("msword_disposition.rtf");
  CHECK(r.ok);
  const auto& doc = r.document;
  CHECK(doc.sections.size() == 1);
  if (doc.sections.size() != 1) return;
  const auto& s = doc.sections[0];
  CHECK(s.headers.right && !s.headers.default_);
  CHECK(s.footers.right && !s.footers.default_);
  const rtf::Table* body = nullptr;
  for (const auto& block : s.body) {
    if (body == nullptr) body = as_table(block);
  }
  CHECK(body != nullptr && !body->rows.empty() && body->rows[0].props.header);
  CHECK(codes(doc) == std::vector<std::string>{"skipped_destination"});
  check_one_table_per_section(rtf::logical_tables(doc));
}

static void msword_features() {
  rtf::ParseResult r = parse_fixture("msword_features.rtf");
  CHECK(r.ok);
  const auto& doc = r.document;
  CHECK(doc.sections.size() == 1);
  if (doc.sections.size() != 1) return;
  Inventory body = inventory(doc.sections[0].body);
  std::size_t vertical = 0, horizontal = 0;
  for (const auto* cell : body.cells) {
    if (cell->mergeVertical) ++vertical;
    if (cell->mergeHorizontal) ++horizontal;
  }
  CHECK(!body.tables.empty());
  CHECK(vertical == 2 && horizontal == 0);
  Inventory all = inventory(doc);
  CHECK(all.pictures.size() == 1);
  if (all.pictures.size() == 1) CHECK(all.pictures[0]->format == rtf::PictureFormat::Png);
  CHECK(inlines_of<rtf::Shape>(all).empty());
  std::size_t with_list_text = 0;
  for (const auto* p : all.paragraphs) {
    if (!p->listText.empty()) ++with_list_text;
  }
  CHECK(with_list_text == 7);
  bool hyperlink = false;
  for (const auto* f : inlines_of<rtf::Field>(all)) {
    if (f->instruction.compare(0, 9, "HYPERLINK") == 0) {
      hyperlink = true;
      CHECK(!f->result.empty());
    }
  }
  CHECK(hyperlink);
  CHECK(inlines_of<rtf::PageBreak>(all).size() == 1);
  // The emoji is one four-byte UTF-8 sequence, not two encoded surrogates.
  std::size_t four_byte_runs = 0;
  for (const auto* run : inlines_of<rtf::TextRun>(all)) {
    const std::string& t = run->text;
    bool four = false;
    for (std::size_t i = 0; i < t.size(); ++i) {
      unsigned char c = static_cast<unsigned char>(t[i]);
      if (c >= 0xF0 && c <= 0xF4) four = true;
      if (c == 0xED && i + 1 < t.size() && static_cast<unsigned char>(t[i + 1]) >= 0xA0) CHECK(!"no encoded surrogate");
    }
    if (four) ++four_byte_runs;
  }
  CHECK(four_byte_runs == 1);
  CHECK((codes(doc) == std::vector<std::string>{"skipped_destination", "symbol_font"}));
  check_one_table_per_section(rtf::logical_tables(doc));
}

static void msword_sections() {
  rtf::ParseResult r = parse_fixture("msword_sections.rtf");
  CHECK(r.ok);
  const auto& doc = r.document;
  CHECK(doc.sections.size() == 2);
  for (const auto& s : doc.sections) CHECK(s.page.landscape);
  if (doc.sections.empty()) return;
  const rtf::Table* first = nullptr;
  for (const auto& block : doc.sections[0].body) {
    if (first == nullptr) first = as_table(block);
  }
  CHECK(first != nullptr);
  if (first != nullptr) {
    bool nested = false;
    for (const auto& row : first->rows) {
      for (const auto& cell : row.cells) {
        for (const auto& block : cell.blocks) {
          if (as_table(block) != nullptr) nested = true;
        }
      }
    }
    CHECK(nested);
  }
  Inventory all = inventory(doc);
  auto notes = inlines_of<rtf::Footnote>(all);
  CHECK(notes.size() == 1);
  if (notes.size() == 1) {
    CHECK(notes[0]->blocks.size() == 1);
    if (notes[0]->blocks.size() == 1) CHECK(std::get_if<rtf::Paragraph>(&notes[0]->blocks[0].value) != nullptr);
  }
  // No footnote mark survives: no superscript run and no control character in any run.
  for (const auto* run : inlines_of<rtf::TextRun>(all)) {
    CHECK(run->props.script != rtf::Script::Super);
    for (char c : run->text) CHECK(static_cast<unsigned char>(c) >= 0x20);
  }
  CHECK(codes(doc) == std::vector<std::string>{"skipped_destination"});
  check_one_table_per_section(rtf::logical_tables(doc));
}

static void sas_disposition() {
  rtf::ParseResult r = parse_fixture("sas_disposition.rtf");
  CHECK(r.ok);
  const auto& doc = r.document;
  CHECK(doc.sections.size() == 1);
  if (doc.sections.size() != 1) return;
  check_sas_page(doc.sections[0], 3);
  CHECK(inlines_of<rtf::Field>(inventory(doc)).size() == 2);
  check_one_bookmark(doc);
  CHECK(doc.warnings.empty());
  auto tables = rtf::logical_tables(doc);
  CHECK(tables.size() == 1);
  if (tables.size() == 1) {
    // The names join the two header rows: the first column takes its text from both.
    const auto& t = tables[0];
    CHECK(t.headerRows.size() == 2);
    if (t.headerRows.size() == 2 && !t.columnNames.empty() && !t.headerRows[0].cells.empty() &&
        !t.headerRows[1].cells.empty()) {
      std::string top = cell_plain(t.headerRows[0].cells[0]);
      std::string bottom = cell_plain(t.headerRows[1].cells[0]);
      CHECK(!top.empty() && !bottom.empty());
      CHECK(t.columnNames[0] == top + " " + bottom);
    }
    check_spacers(t);
  }
  check_one_table_per_section(tables);
}

static void by_eye() {
  sas_table_3pages();
  sas_listing_1page();
  word_minimal();
  word_tables();
  word_text();
  pictures();
  malformed();
  msword_disposition();
  msword_features();
  msword_sections();
  sas_disposition();
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: test_goldens <fixtures_dir>\n");
    return 2;
  }
  g_dir = argv[1];
  std::vector<fs::path> fixtures = list_fixtures(fs::path(g_dir));
  goldens(fixtures);
  if (!fixtures.empty()) by_eye();
  return rtf_test::result();
}
