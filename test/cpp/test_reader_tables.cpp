#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include "check.hpp"
#include "helpers.hpp"
#include "rtf/reader.hpp"

static void one_row() {
  auto doc = parse_ok(R"({\rtf1\trowd\cellx1000\cellx2500 \pard\intbl a\cell b\cell\row})");
  const auto& t = body_table(doc, 0, 0);
  CHECK_EQ(t.rows.size(), std::size_t(1)); CHECK_EQ(t.rows[0].cells.size(), std::size_t(2));
  CHECK_EQ(t.rows[0].cells[0].right, 1000); CHECK_EQ(t.rows[0].cells[0].width, 1000);
  CHECK_EQ(t.rows[0].cells[1].right, 2500); CHECK_EQ(t.rows[0].cells[1].width, 1500);
  CHECK_EQ(cell_plain(t.rows[0].cells[0]), std::string("a")); CHECK_EQ(cell_plain(t.rows[0].cells[1]), std::string("b"));
  CHECK(doc.warnings.empty());
}
static void word_second_definition_wins() {
  auto doc = parse_ok(R"({\rtf1\trowd\cellx1000\cellx2000 \pard\intbl a\cell b\cell{\trowd\trleft-108\cellx1100\cellx2200\row}})");
  const auto& r = body_table(doc, 0, 0).rows[0];
  CHECK_EQ(r.cells[0].right, 1100); CHECK_EQ(r.cells[0].width, 1208); CHECK_EQ(r.cells[1].right, 2200); CHECK_EQ(*r.props.left, -108);
}
static void sas_row_in_braces_and_header_tables() {
  auto doc = parse_ok(R"({\rtf1{\header\pard\plain\qc{\trowd\trkeep\trqc\cellx4862\cellx9802\pard\plain\intbl t\cell\pard\plain\intbl u\cell{\row}\pard{\par}}}\trowd\trhdr\cellx100\pard\intbl x\cell{\row}})");
  const auto& h = *doc.sections[0].headers.default_;
  CHECK_EQ(h.size(), std::size_t(2));
  CHECK(std::holds_alternative<rtf::Table>(h[0].value)); CHECK(std::holds_alternative<rtf::Paragraph>(h[1].value));
  const auto& hr = std::get<rtf::Table>(h[0].value).rows[0];
  CHECK(hr.props.keep); CHECK(*hr.props.align == rtf::Align::Center); CHECK_EQ(hr.cells.size(), std::size_t(2));
  CHECK(body_table(doc, 0, 0).rows[0].props.header);
}
static void cell_properties() {
  auto doc = parse_ok(R"({\rtf1\trowd\clbrdrt\brdrs\brdrw10\brdrcf1\clbrdrb\brdrnone\cltxtbrl\clvertalb\clcbpat8\clshdng5000\clpadt10\clpadft3\clpadr20\clpadfr3\clpadl5\clwWidth1424\clftsWidth3\clNoWrap\clmgf\clvmgf\cellx1000\clmrg\clvmrg\cellx2000 \pard\intbl a\cell b\cell\row})");
  const auto& c = body_table(doc, 0, 0).rows[0].cells[0];
  CHECK(c.borders->top->style == rtf::BorderStyle::Single); CHECK_EQ(c.borders->top->width, 10); CHECK_EQ(*c.borders->top->color, 1);
  CHECK(c.borders->bottom->style == rtf::BorderStyle::None);
  CHECK(*c.textDirection == rtf::TextDirection::Tbrl); CHECK(*c.verticalAlign == rtf::VerticalAlign::Bottom);
  CHECK_EQ(*c.shading->background, 8); CHECK_EQ(*c.shading->percent, 5000);
  CHECK_EQ(*c.padding->left, 10); CHECK_EQ(*c.padding->right, 20); CHECK_EQ(*c.padding->top, 5); CHECK(!c.padding->bottom.has_value());
  CHECK(c.preferredWidth->type == rtf::WidthType::Twips); CHECK_EQ(c.preferredWidth->value, 1424);
  CHECK(c.noWrap); CHECK(*c.mergeHorizontal == rtf::Merge::First); CHECK(*c.mergeVertical == rtf::Merge::First);
  const auto& d = body_table(doc, 0, 0).rows[0].cells[1];
  CHECK(*d.mergeHorizontal == rtf::Merge::Continued); CHECK(*d.mergeVertical == rtf::Merge::Continued);
}
static void row_properties() {
  auto doc = parse_ok(R"({\rtf1\trowd\trhdr\trkeep\trqr\trleft-108\trrh-300\trgaph108\trpaddl108\trpaddfl3\trpaddt5\trbrdrt\brdrdb\brdrw20\trbrdrh\brdrdot\cellx1000 \pard\intbl a\cell\row\trowd\trrh400\cellx1000\pard\intbl b\cell\row})");
  const auto& r = body_table(doc, 0, 0).rows[0].props;
  CHECK(r.header); CHECK(r.keep); CHECK(*r.align == rtf::Align::Right); CHECK_EQ(*r.left, -108);
  CHECK_EQ(*r.height, 300); CHECK(*r.heightRule == rtf::HeightRule::Exact); CHECK_EQ(*r.gapHalf, 108);
  CHECK_EQ(*r.padding->left, 108); CHECK_EQ(*r.padding->top, 5);
  CHECK(r.borders->top->style == rtf::BorderStyle::Double); CHECK_EQ(r.borders->top->width, 20);
  CHECK(r.borders->horizontal->style == rtf::BorderStyle::Dotted);
  const auto& s = body_table(doc, 0, 0).rows[1].props;
  CHECK_EQ(*s.height, 400); CHECK(*s.heightRule == rtf::HeightRule::AtLeast); CHECK(!s.header);
}
static void fewer_and_more_cells() {
  auto a = parse_ok(R"({\rtf1\trowd\cellx1000\cellx2000\cellx3000 \pard\intbl a\cell b\cell\row})");
  const auto& ra = body_table(a, 0, 0).rows[0];
  CHECK_EQ(ra.cells.size(), std::size_t(3)); CHECK_EQ(ra.cells[2].right, 3000); CHECK_EQ(ra.cells[2].blocks.size(), std::size_t(1));
  CHECK_EQ(count_warnings(a, "cell_count_mismatch"), std::size_t(1));
  auto b = parse_ok(R"({\rtf1\trowd\cellx1000 \pard\intbl a\cell b\cell c\cell\row})");
  const auto& rb = body_table(b, 0, 0).rows[0];
  CHECK_EQ(rb.cells.size(), std::size_t(3)); CHECK_EQ(rb.cells[1].right, 2000); CHECK_EQ(rb.cells[2].right, 3000); CHECK_EQ(rb.cells[2].width, 1000);
  auto c = parse_ok(R"({\rtf1\pard\intbl a\cell\row})");
  CHECK_EQ(body_table(c, 0, 0).rows[0].cells[0].right, 0); CHECK(has_warning(c, "cell_count_mismatch"));
}
static void tables_start_and_end() {
  auto doc = parse_ok(R"({\rtf1\trowd\cellx1000\pard\intbl a\cell\row \pard\intbl b\cell\row \pard plain\par \trowd\cellx1000\pard\intbl c\cell\row\trowd\cellx1000\trowd\cellx1500\pard\intbl d\cell\row})");
  const auto& body = doc.sections[0].body;
  CHECK_EQ(body.size(), std::size_t(3));
  CHECK_EQ(std::get<rtf::Table>(body[0].value).rows.size(), std::size_t(2));   // second row reuses the definition
  CHECK(std::holds_alternative<rtf::Paragraph>(body[1].value));
  CHECK_EQ(std::get<rtf::Table>(body[2].value).rows.size(), std::size_t(2));   // a row-less \trowd does not split
  CHECK_EQ(std::get<rtf::Table>(body[2].value).rows[1].cells[0].right, 1500);
}
static void multi_paragraph_cells_and_empty_cells() {
  auto doc = parse_ok(R"({\rtf1\trowd\cellx1000\cellx2000 \pard\intbl a\par\pard\intbl b\cell\pard\intbl\cell\row})");
  const auto& r = body_table(doc, 0, 0).rows[0];
  CHECK_EQ(r.cells[0].blocks.size(), std::size_t(2)); CHECK_EQ(cell_plain(r.cells[0]), std::string("a\nb"));
  CHECK_EQ(r.cells[1].blocks.size(), std::size_t(1)); CHECK(std::get<rtf::Paragraph>(r.cells[1].blocks[0].value).runs.empty());
}
static void nested_table_word_style() {
  auto doc = parse_ok(R"({\rtf1\trowd\cellx5000 \pard\intbl\itap1 outer\par
\pard\intbl\itap2 {n1\nestcell{\nonesttables\par}n2\nestcell{\nonesttables\par}}
\pard\intbl\itap2 {{\*\nesttableprops\trowd\cellx1429\cellx2854\nestrow}{\nonesttables\par}}
\pard\intbl\itap1 {after\cell}{\trowd\cellx5000\row}})");
  const auto& outer = body_table(doc, 0, 0);
  CHECK_EQ(outer.rows.size(), std::size_t(1)); CHECK_EQ(outer.rows[0].cells.size(), std::size_t(1));
  const auto& blocks = outer.rows[0].cells[0].blocks;
  CHECK_EQ(blocks.size(), std::size_t(3));
  CHECK(std::holds_alternative<rtf::Paragraph>(blocks[0].value));
  CHECK(std::holds_alternative<rtf::Table>(blocks[1].value));
  CHECK(std::holds_alternative<rtf::Paragraph>(blocks[2].value));
  const auto& inner = std::get<rtf::Table>(blocks[1].value);
  CHECK_EQ(inner.rows.size(), std::size_t(1)); CHECK_EQ(inner.rows[0].cells.size(), std::size_t(2));
  CHECK_EQ(inner.rows[0].cells[0].right, 1429); CHECK_EQ(cell_plain(inner.rows[0].cells[1]), std::string("n2"));
  CHECK_EQ(run_text(std::get<rtf::Paragraph>(blocks[2].value)), std::string("after"));
  CHECK_EQ(count_warnings(doc, "skipped_destination"), std::size_t(1));   // nonesttables, once
}
static void row_not_closed() {
  auto a = parse_ok(R"({\rtf1\trowd\cellx1000\pard\intbl a\cell b\cell})");
  CHECK_EQ(body_table(a, 0, 0).rows.size(), std::size_t(1)); CHECK_EQ(body_table(a, 0, 0).rows[0].cells.size(), std::size_t(2));
  CHECK(has_warning(a, "row_not_closed"));
  auto b = parse_ok(R"({\rtf1\trowd\cellx1000\pard\intbl a\cell\pard\par})");
  CHECK_EQ(b.sections[0].body.size(), std::size_t(2)); CHECK(has_warning(b, "row_not_closed"));
}
static void large_itap_is_bounded() {
  // A level far deeper than the open tables becomes one level below them,
  // so one paragraph cannot create millions of nested tables.
  auto doc = parse_ok(R"({\rtf1\pard\intbl\itap2000000000 x\par\pard y\par})");
  CHECK_EQ(doc.sections[0].body.size(), std::size_t(2));
  const auto& outer = body_table(doc, 0, 0);
  CHECK_EQ(outer.rows.size(), std::size_t(1));
  if (outer.rows.empty() || outer.rows[0].cells.empty() || outer.rows[0].cells[0].blocks.empty()) return;
  const auto* inner = std::get_if<rtf::Table>(&outer.rows[0].cells[0].blocks[0].value);
  CHECK(inner != nullptr);
  if (inner == nullptr || inner->rows.empty() || inner->rows[0].cells.empty()) return;
  CHECK_EQ(cell_plain(inner->rows[0].cells[0]), std::string("x"));
  CHECK_EQ(count_warnings(doc, "row_not_closed"), std::size_t(2));
}

int main() {
  one_row();
  word_second_definition_wins();
  sas_row_in_braces_and_header_tables();
  cell_properties();
  row_properties();
  fewer_and_more_cells();
  tables_start_and_end();
  multi_paragraph_cells_and_empty_cells();
  nested_table_word_style();
  row_not_closed();
  large_itap_is_bounded();
  return rtf_test::result();
}
