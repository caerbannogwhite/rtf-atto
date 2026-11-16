#include "rtf/model.hpp"
#include "rtf/version.hpp"
#include "check.hpp"
#include <cstring>

static void defaults_match_the_rtf_specification() {
  rtf::RtfDocument doc;
  CHECK_EQ(doc.version, 1);
  CHECK_EQ(doc.codePage, 1252);
  CHECK_EQ(doc.defaultFont, 0);
  CHECK_EQ(doc.page.width, 12240);
  CHECK_EQ(doc.page.height, 15840);
  CHECK_EQ(doc.page.marginLeft, 1800);
  CHECK_EQ(doc.page.marginTop, 1440);
  CHECK_EQ(doc.page.headerDistance, 720);
  CHECK(!doc.page.landscape);
  rtf::Section section;
  CHECK(section.break_ == rtf::SectionBreak::Page);
  CHECK(!section.headers.default_.has_value());
}

static void blocks_and_inlines_nest() {
  rtf::Paragraph p;
  p.runs.push_back(rtf::Inline{rtf::TextRun{"Hello", {}}});
  rtf::Cell cell;
  cell.blocks.push_back(rtf::Block{p});
  rtf::Row row;
  row.cells.push_back(cell);
  rtf::Table table;
  table.rows.push_back(row);
  rtf::Block block{table};
  CHECK(std::holds_alternative<rtf::Table>(block.value));
  const auto& inner = std::get<rtf::Table>(block.value).rows[0].cells[0].blocks[0];
  CHECK(std::holds_alternative<rtf::Paragraph>(inner.value));
  const auto& run = std::get<rtf::TextRun>(std::get<rtf::Paragraph>(inner.value).runs[0].value);
  CHECK_EQ(run.text, std::string("Hello"));
  rtf::Footnote note;
  note.blocks.push_back(rtf::Block{rtf::Paragraph{}});
  p.runs.push_back(rtf::Inline{note});
  CHECK_EQ(p.runs.size(), std::size_t(2));
}

static void version_string() {
  CHECK(std::strcmp(rtf::version(), RTF_ATTO_VERSION) == 0);
  CHECK(std::strcmp(rtf::version(), "0.1.0") == 0);
}

int main() {
  defaults_match_the_rtf_specification();
  blocks_and_inlines_nest();
  version_string();
  return rtf_test::result();
}
