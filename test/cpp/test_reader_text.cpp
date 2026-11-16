#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include "check.hpp"
#include "helpers.hpp"
#include "rtf/reader.hpp"

static void parses_minimal() {
  auto doc = parse_ok(R"({\rtf1\ansi Hello\par})");
  CHECK_EQ(doc.sections.size(), std::size_t(1));
  CHECK_EQ(doc.sections[0].body.size(), std::size_t(1));
  CHECK_EQ(run_text(body_paragraph(doc, 0, 0)), std::string("Hello"));
  CHECK(doc.warnings.empty());
}
static void rejects_non_rtf() {
  CHECK(!rtf::parse(std::string_view("hello")).ok);
  CHECK_EQ(rtf::parse(std::string_view("hello")).error, std::string("not_rtf"));
  CHECK(!rtf::parse(std::string_view("")).ok);
  CHECK(rtf::parse(std::string_view("\xEF\xBB\xBF \r\n{\\rtf1 x}")).ok);
}
static void header_tables() {
  auto doc = parse_ok(R"({\rtf1\ansi\ansicpg1250\deff1\deflang2057
{\fonttbl{\f1\fmodern\fprq1\fcharset0 Courier New;}{\f3\fnil\fcharset2 Symbol;}
{\flomajor\f31500\fbidi\froman\fcharset0\fprq2{\*\panose 02020603050405020304}Times New Roman;}
\f5\fswiss\fcharset0\cpg1251 Arial;}
{\colortbl;\red0\green0\blue0;\red255\green255\blue255\ctint255\cshade255\caccentone;}
{\stylesheet{\ql\fs20\snext0 Normal;}{\s1\sbasedon0\snext1 heading 1;}{\*\cs10\additive Default Paragraph Font;}{\*\ts11\tsrowd Normal Table;}}
{\info{\title Synthetic;}{\author Nobody}{\creatim\yr2026\mo11\dy10\hr10\min19\sec28}{\nofpages1}{\*\unknowninfo x}}
{\*\generator rtf-atto test;}
\paperw16837\paperh11905\margl1080\margr1080\margt1080\margb360\landscape\facingp
Body\par})");
  CHECK_EQ(doc.codePage, 1250); CHECK_EQ(doc.defaultFont, 1); CHECK_EQ(*doc.defaultLanguage, 2057);
  CHECK_EQ(doc.fonts.size(), std::size_t(4));
  CHECK_EQ(doc.fonts.at(1).name, std::string("Courier New"));
  CHECK(doc.fonts.at(1).family == rtf::FontFamily::Modern); CHECK(doc.fonts.at(1).pitch == rtf::FontPitch::Fixed);
  CHECK_EQ(doc.fonts.at(3).charset, 2);
  CHECK_EQ(doc.fonts.at(31500).name, std::string("Times New Roman"));
  CHECK_EQ(*doc.fonts.at(5).codePage, 1251); CHECK_EQ(doc.fonts.at(5).name, std::string("Arial"));
  CHECK_EQ(doc.colors.size(), std::size_t(3)); CHECK(!doc.colors[0].has_value()); CHECK_EQ(doc.colors[2]->r, 255);
  CHECK_EQ(doc.styles.size(), std::size_t(4));
  CHECK_EQ(doc.styles[0].name, std::string("Normal")); CHECK_EQ(doc.styles[0].index, 0);
  CHECK_EQ(doc.styles[1].index, 1); CHECK_EQ(*doc.styles[1].basedOn, 0); CHECK_EQ(*doc.styles[1].next, 1);
  CHECK(doc.styles[2].type == rtf::StyleType::Character); CHECK_EQ(doc.styles[2].index, 10);
  CHECK(doc.styles[3].type == rtf::StyleType::Table); CHECK_EQ(doc.styles[3].name, std::string("Normal Table"));
  CHECK_EQ(doc.info.at("title"), std::string("Synthetic")); CHECK_EQ(doc.info.at("author"), std::string("Nobody"));
  CHECK_EQ(doc.info.at("creatim"), std::string("2026-11-10T10:19:28")); CHECK_EQ(doc.info.at("nofpages"), std::string("1"));
  CHECK_EQ(doc.info.count("unknowninfo"), std::size_t(0));
  CHECK_EQ(*doc.generator, std::string("rtf-atto test"));
  CHECK_EQ(doc.page.width, 16837); CHECK_EQ(doc.page.marginBottom, 360); CHECK(doc.page.landscape); CHECK(doc.page.facingPages);
  CHECK_EQ(run_text(body_paragraph(doc, 0, 0)), std::string("Body"));
  CHECK(doc.warnings.empty());
}
static void runs_follow_character_properties() {
  auto doc = parse_ok(R"({\rtf1 a{\b b\i c}d{}e\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(p.runs.size(), std::size_t(4));
  CHECK_EQ(run(p, 0).text, std::string("a")); CHECK(!run(p, 0).props.bold);
  CHECK_EQ(run(p, 1).text, std::string("b")); CHECK(run(p, 1).props.bold); CHECK(!run(p, 1).props.italic);
  CHECK_EQ(run(p, 2).text, std::string("c")); CHECK(run(p, 2).props.bold); CHECK(run(p, 2).props.italic);
  CHECK_EQ(run(p, 3).text, std::string("de"));
}
static void char_props_and_plain() {
  auto doc = parse_ok(R"({\rtf1 {\f1\fs18\ul\strike\super\up6\cf1\highlight7\caps\v\lang1033 x}{\b\plain y}{\i\ul\ul0 z}\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  const auto& x = run(p, 0).props;
  CHECK_EQ(*x.font, 1); CHECK_EQ(*x.size, 18); CHECK(*x.underline == rtf::Underline::Single); CHECK(x.strike);
  CHECK(*x.script == rtf::Script::Super); CHECK_EQ(*x.offset, 6); CHECK_EQ(*x.color, 1); CHECK_EQ(*x.highlight, 7);
  CHECK(x.caps); CHECK(x.hidden); CHECK_EQ(*x.language, 1033);
  CHECK(!run(p, 1).props.bold);
  CHECK_EQ(p.runs.size(), std::size_t(3));   // y and z differ in italic, so they stay two runs
  CHECK(run(p, 2).props.italic);
  CHECK(!run(p, 2).props.underline.has_value());   // \ul then \ul0 clears it
}
static void paragraph_props_apply_at_par() {
  auto doc = parse_ok(R"({\rtf1\pard\qc\li288\fi-360\sb10\sa0\sl278\slmult1\keepn\s1\ls1\ilvl0\widctlpar one\par{\qr two}\par})");
  const auto& a = body_paragraph(doc, 0, 0).props;
  CHECK(*a.align == rtf::Align::Center); CHECK_EQ(*a.indentLeft, 288); CHECK_EQ(*a.indentFirst, -360);
  CHECK_EQ(*a.spaceBefore, 10); CHECK_EQ(*a.spaceAfter, 0); CHECK_EQ(*a.lineSpacing, 278); CHECK(a.lineSpacingMultiple);
  CHECK(a.keepWithNext); CHECK_EQ(*a.style, 1); CHECK_EQ(a.list->id, 1); CHECK_EQ(a.list->level, 0); CHECK(*a.widowControl);
  CHECK(*body_paragraph(doc, 0, 1).props.align == rtf::Align::Center);   // \qr was popped before \par
}
static void tabs_borders_and_shading() {
  auto doc = parse_ok(R"({\rtf1\pard\tqc\tx4513\tqr\tx9026\tldot\tx720\brdrt\brdrs\brdrw10\brsp20\brdrcf27\shading2500\cbpat8 x\par})");
  const auto& a = body_paragraph(doc, 0, 0).props;
  CHECK_EQ(a.tabs.size(), std::size_t(3));
  CHECK_EQ(a.tabs[0].position, 4513); CHECK(a.tabs[0].align == rtf::TabAlign::Center);
  CHECK(a.tabs[1].align == rtf::TabAlign::Right);
  CHECK_EQ(a.tabs[2].position, 720); CHECK(a.tabs[2].leader == rtf::TabLeader::Dots);
  CHECK(a.borders->top->style == rtf::BorderStyle::Single); CHECK_EQ(a.borders->top->width, 10);
  CHECK_EQ(*a.borders->top->space, 20); CHECK_EQ(*a.borders->top->color, 27);
  CHECK_EQ(*a.shading->percent, 2500); CHECK_EQ(*a.shading->background, 8);
}
static void special_characters() {
  auto doc = parse_ok(R"({\rtf1 a\~b\_c\-d\emdash\endash\bullet\lquote\rquote\ldblquote\rdblquote\emspace\enspace\qmspace\zwj\zwnj\{\}\\e\par})");
  CHECK_EQ(run_text(body_paragraph(doc, 0, 0)),
           std::string("a\xC2\xA0" "b\xE2\x80\x91" "c\xC2\xAD" "d\xE2\x80\x94\xE2\x80\x93\xE2\x80\xA2\xE2\x80\x98\xE2\x80\x99\xE2\x80\x9C\xE2\x80\x9D\xE2\x80\x83\xE2\x80\x82\xE2\x80\x85\xE2\x80\x8D\xE2\x80\x8C{}\\e"));
}
static void breaks_and_fields() {
  auto doc = parse_ok(R"({\rtf1 a\line b\tab c\page d\column e\chpgn\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(p.runs.size(), std::size_t(10));
  CHECK(std::holds_alternative<rtf::LineBreak>(p.runs[1].value));
  CHECK(std::holds_alternative<rtf::Tab>(p.runs[3].value));
  CHECK(std::holds_alternative<rtf::PageBreak>(p.runs[5].value));
  CHECK(std::holds_alternative<rtf::ColumnBreak>(p.runs[7].value));
  const auto& f = std::get<rtf::Field>(p.runs[9].value);
  CHECK_EQ(f.instruction, std::string("PAGE")); CHECK(f.result.empty());
}
static void hex_raw_and_font_code_pages() {
  auto doc = parse_ok("{\\rtf1\\ansi\\ansicpg1252{\\fonttbl{\\f0\\fnil\\fcharset0 Arial;}{\\f5\\fnil\\fcharset204 Arial;}{\\f3\\fnil\\fcharset2 Symbol;}{\\f7\\fnil\\fcharset128 MS Gothic;}}"
                      "\\f0\\'e9\xE9{\\f5\\'c0}{\\f3\\'b7}{\\f7\\'82\\'a0}\\par}");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(run(p, 0).text, std::string("\xC3\xA9\xC3\xA9"));
  CHECK_EQ(run(p, 1).text, std::string("\xD0\x90"));
  CHECK_EQ(run(p, 2).text, std::string("\xEF\x82\xB7"));
  CHECK_EQ(run(p, 3).text, std::string("\xEF\xBF\xBD\xEF\xBF\xBD"));
  CHECK_EQ(count_warnings(doc, "symbol_font"), std::size_t(1));
  CHECK_EQ(count_warnings(doc, "unsupported_code_page"), std::size_t(1));
}
static void unicode_escapes_and_fallback_skip() {
  auto doc = parse_ok(R"({\rtf1\uc1 \u8364?x \u963\'73y {\uc2\u20013ab}z \u-10179\'3f\u-8699\'3f w\par})");
  CHECK_EQ(run_text(body_paragraph(doc, 0, 0)),
           std::string("\xE2\x82\xACx \xCF\x83y \xE4\xB8\xADz \xF0\x9F\x98\x85 w"));
}
static void uc_skip_stops_at_brace() {
  auto doc = parse_ok(R"({\rtf1 {\u8364}x\par})");
  CHECK_EQ(run_text(body_paragraph(doc, 0, 0)), std::string("\xE2\x82\xACx"));
}
static void uc_skip_stops_at_control() {
  auto doc = parse_ok(R"({\rtf1 a\u8364\par b\par})");
  CHECK_EQ(doc.sections[0].body.size(), std::size_t(2));
  CHECK_EQ(run_text(body_paragraph(doc, 0, 0)), std::string("a\xE2\x82\xAC"));
  CHECK_EQ(run_text(body_paragraph(doc, 0, 1)), std::string("b"));
}
static void destinations_known_unknown_and_plain() {
  auto doc = parse_ok(R"({\rtf1 {\*\foo lost}{\*\foo lost again}{\*\themedata 504b0304}{\mmathPr\mmathFont34}{\bar kept}after\par})");
  CHECK_EQ(run_text(body_paragraph(doc, 0, 0)), std::string("keptafter"));
  CHECK_EQ(count_warnings(doc, "unknown_destination"), std::size_t(1));
  CHECK_EQ(count_warnings(doc, "skipped_destination"), std::size_t(2));
}
static void unbalanced_groups_and_depth() {
  auto a = parse_ok(R"({\rtf1 a)");
  CHECK_EQ(run_text(body_paragraph(a, 0, 0)), std::string("a")); CHECK(has_warning(a, "unbalanced_group"));
  auto b = parse_ok(R"({\rtf1 a}})");
  CHECK(has_warning(b, "unbalanced_group"));
  std::string deep = "{\\rtf1 ";
  for (int i = 0; i < 1500; ++i) deep += "{";
  deep += "x";
  for (int i = 0; i < 1500; ++i) deep += "}";
  deep += "\\par}";
  auto c = parse_ok(deep);
  CHECK_EQ(run_text(body_paragraph(c, 0, 0)), std::string("x")); CHECK_EQ(count_warnings(c, "depth_limit"), std::size_t(1));
  auto d = rtf::parse(std::string_view(deep), rtf::ParseOptions{2000});
  CHECK(d.ok); CHECK(!has_warning(d.document, "depth_limit"));
}
static void paragraph_ends_and_empty_paragraphs() {
  auto a = parse_ok("{\\rtf1 a\\par b\\\nc\r\nd}");
  CHECK_EQ(a.sections[0].body.size(), std::size_t(3));
  CHECK_EQ(run_text(body_paragraph(a, 0, 1)), std::string("b"));
  CHECK_EQ(run_text(body_paragraph(a, 0, 2)), std::string("cd"));
  auto b = parse_ok(R"({\rtf1 a\par})");
  CHECK_EQ(b.sections[0].body.size(), std::size_t(1));
  auto c = parse_ok(R"({\rtf1 })");
  CHECK_EQ(c.sections[0].body.size(), std::size_t(1));
  CHECK(body_paragraph(c, 0, 0).runs.empty());
  auto d = parse_ok(R"({\rtf1 \par\par})");
  CHECK_EQ(d.sections[0].body.size(), std::size_t(2));
}
static void tokenizer_warnings_reach_the_document() {
  auto a = parse_ok("{\\rtf1 \\fs99999999999 x\\par}");
  CHECK(has_warning(a, "param_overflow"));
  auto b = parse_ok("{\\rtf1 x\\");
  CHECK(has_warning(b, "truncated_control"));
}

int main() {
  parses_minimal();
  rejects_non_rtf();
  header_tables();
  runs_follow_character_properties();
  char_props_and_plain();
  paragraph_props_apply_at_par();
  tabs_borders_and_shading();
  special_characters();
  breaks_and_fields();
  hex_raw_and_font_code_pages();
  unicode_escapes_and_fallback_skip();
  uc_skip_stops_at_brace();
  uc_skip_stops_at_control();
  destinations_known_unknown_and_plain();
  unbalanced_groups_and_depth();
  paragraph_ends_and_empty_paragraphs();
  tokenizer_warnings_reach_the_document();
  return rtf_test::result();
}
