#include <string>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "rtf/json.hpp"
#include "rtf/reader.hpp"
#include "rtf/tables.hpp"
#include "rtf/text.hpp"

// Replaces every occurrence of `from` in `s` with `to`.
static std::string replace(std::string s, const std::string& from, const std::string& to) {
  for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
  return s;
}

static void text_projection_rules() {
  auto doc = parse_ok(R"({\rtf1{\header h1\par}{\footer f1}\trowd\cellx100\cellx200\pard\intbl a\par\pard\intbl b\cell c\line d\cell\row \pard x\tab y{\footnote n\par}z\page\par{\field{\*\fldinst PAGE}{\fldrslt 7}}\par})");
  CHECK_EQ(rtf::to_text(doc), std::string("h1\nf1\na\nb\tc\nd\t\nx\ty" "n\n" "z\f\n7\n"));
}
static void stitches_parts_across_sections() {
  const char* page = R"(\trowd\trhdr\cellx1000\cellx2000\pard\intbl\keepn Name\cell\pard\intbl\keepn Value\cell\row
\trowd\cellx1000\cellx2000\pard\intbl %s\cell 1\cell\row)";
  std::string rtf = "{\\rtf1\\sectd" + replace(page, "%s", "a") + "\\sect\\sectd" + replace(page, "%s", "b") + "}";
  auto doc = parse_ok(rtf);
  auto tables = rtf::logical_tables(doc);
  CHECK_EQ(tables.size(), std::size_t(1));
  CHECK_EQ(tables[0].firstSection, 0); CHECK_EQ(tables[0].lastSection, 1); CHECK_EQ(tables[0].parts.size(), std::size_t(2));
  CHECK_EQ(tables[0].headerRows.size(), std::size_t(1));
  CHECK_EQ(tables[0].rows.size(), std::size_t(2));
  CHECK_EQ(tables[0].rows[1].cells[0].text, std::string("b"));
  CHECK_EQ(tables[0].columnNames.size(), std::size_t(2));
  CHECK_EQ(tables[0].columnNames[0], std::string("Name")); CHECK_EQ(tables[0].columnNames[1], std::string("Value"));
}
static void different_headers_start_a_new_table() {
  auto doc = parse_ok(R"({\rtf1\trowd\trhdr\cellx1000\pard\intbl A\cell\row\trowd\cellx1000\pard\intbl 1\cell\row \pard\par \trowd\trhdr\cellx1000\pard\intbl B\cell\row\trowd\cellx1000\pard\intbl 2\cell\row})");
  auto tables = rtf::logical_tables(doc);
  CHECK_EQ(tables.size(), std::size_t(2)); CHECK_EQ(tables[1].id, 2); CHECK_EQ(tables[1].columnNames[0], std::string("B"));
}
static void headerless_part_joins_when_edges_fit() {
  auto doc = parse_ok(R"({\rtf1\trowd\trhdr\cellx1000\cellx2000\pard\intbl A\cell B\cell\row\trowd\cellx1000\cellx2000\pard\intbl 1\cell 2\cell\row\sect\sectd\trowd\cellx2000\pard\intbl wide\cell\row})");
  auto tables = rtf::logical_tables(doc);
  CHECK_EQ(tables.size(), std::size_t(1)); CHECK_EQ(tables[0].rows.size(), std::size_t(2));
  CHECK_EQ(tables[0].rows[1].cells[0].span, 2); CHECK_EQ(tables[0].rows[1].cells[0].column, 0);
}
static void grid_spans_spacers_indent_and_names() {
  auto doc = parse_ok(R"({\rtf1\trowd\trhdr\cellx3456\cellx8670\pard\intbl\cell\pard\intbl Treatment\cell\row
\trowd\trhdr\cellx3456\cellx5194\cellx6932\cellx8670\pard\intbl\cell\pard\intbl Placebo\line (N=20)\cell\pard\intbl Drug A\cell\pard\intbl Drug A\cell\row
\trowd\cellx8670\pard\intbl\cell\row
\trowd\cellx3456\cellx5194\cellx6932\cellx8670\pard\intbl\li288 Completed\cell 15\cell 17\cell 32\cell\row})");
  auto tables = rtf::logical_tables(doc);
  CHECK_EQ(tables.size(), std::size_t(1));
  const auto& t = tables[0];
  CHECK_EQ(t.edges.size(), std::size_t(5));   // 0 3456 5194 6932 8670
  CHECK_EQ(t.edges[0], 0); CHECK_EQ(t.edges[4], 8670);
  CHECK_EQ(t.columnNames.size(), std::size_t(4));
  CHECK_EQ(t.columnNames[0], std::string("column_1"));
  CHECK_EQ(t.columnNames[1], std::string("Treatment Placebo (N=20)"));
  CHECK_EQ(t.columnNames[2], std::string("Treatment Drug A"));
  CHECK_EQ(t.columnNames[3], std::string("Treatment Drug A_2"));
  CHECK_EQ(t.rows.size(), std::size_t(2));
  CHECK(t.rows[0].spacer); CHECK_EQ(t.rows[0].cells[0].span, 4);
  CHECK(!t.rows[1].spacer); CHECK_EQ(t.rows[1].indent, 288); CHECK_EQ(t.rows[1].cells[3].text, std::string("32"));
}
static void logical_tables_json() {
  auto doc = parse_ok(R"({\rtf1\trowd\trhdr\cellx100\pard\intbl A\cell\row\trowd\cellx100\pard\intbl 1\cell\row})");
  std::string json = rtf::to_json(rtf::logical_tables(doc));
  CHECK(json.find("\"columnNames\": [\n      \"A\"\n    ]") != std::string::npos);
  CHECK(json.find("\"spacer\": false") != std::string::npos);
  CHECK(json.find("\"headerRows\": [") != std::string::npos);
  CHECK_EQ(rtf::to_json(std::vector<rtf::LogicalTable>{}), std::string("[]\n"));
}

int main() {
  text_projection_rules();
  stitches_parts_across_sections();
  different_headers_start_a_new_table();
  headerless_part_joins_when_edges_fit();
  grid_spans_spacers_indent_and_names();
  logical_tables_json();
  return rtf_test::result();
}
