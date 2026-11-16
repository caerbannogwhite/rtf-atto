#include <cstddef>
#include <string>
#include <string_view>
#include <variant>

#include "check.hpp"
#include "helpers.hpp"
#include "rtf/reader.hpp"

static void sas_page_setup_and_sections() {
  auto doc = parse_ok(R"({\rtf1\paperw16837\paperh11905\margl1080\margr1080\margt1080\margb360
\sectd\linex0\endnhere\pgwsxn16837\pghsxn11905\lndscpsxn\headery1080\footery360\marglsxn1080\margrsxn1080\margtsxn1080\margbsxn360 one\par
\sect\sectd\sbknone\cols2\titlepg\pgnstarts5\pgnrestart\pgwsxn12240\pghsxn15840 two\par\sect\sectd three\par})");
  CHECK_EQ(doc.sections.size(), std::size_t(3));
  const auto& s0 = doc.sections[0];
  CHECK_EQ(s0.page.width, 16837); CHECK_EQ(s0.page.height, 11905); CHECK(s0.page.landscape);
  CHECK_EQ(s0.page.headerDistance, 1080); CHECK_EQ(s0.page.footerDistance, 360); CHECK_EQ(s0.page.marginBottom, 360);
  CHECK(s0.break_ == rtf::SectionBreak::Page); CHECK(!s0.columns.has_value());
  CHECK(!doc.page.landscape);   // \landscape was not written; the flag does not swap sizes
  const auto& s1 = doc.sections[1];
  CHECK(s1.break_ == rtf::SectionBreak::None); CHECK_EQ(*s1.columns, 2); CHECK(s1.titlePage); CHECK_EQ(*s1.pageNumberStart, 5);
  CHECK_EQ(s1.page.width, 12240); CHECK(!s1.page.landscape);
  CHECK_EQ(run_text(body_paragraph(doc, 2, 0)), std::string("three"));
  CHECK_EQ(doc.sections[2].page.width, 16837);   // \sectd resets to the document values
}
static void sectd_without_sect_is_one_section() {
  auto doc = parse_ok(R"({\rtf1\sectd\lndscpsxn{\headerr h\par}{\footerr f\par}\sectd\lndscpsxn{\*\ftnsep\chftnsep}\sectd\lndscpsxn body\par})");
  CHECK_EQ(doc.sections.size(), std::size_t(1));
  CHECK(doc.sections[0].headers.right.has_value()); CHECK(doc.sections[0].footers.right.has_value());
  CHECK(!doc.sections[0].headers.default_.has_value());
  CHECK(doc.sections[0].page.landscape);
}
static void headers_all_kinds() {
  auto doc = parse_ok(R"({\rtf1\sectd{\header d\par}{\headerf f\par}{\headerl l\par}{\headerr r\par}{\footer fd\par}{\footerf ff\par}{\footerl fl\par}{\footerr fr\par} body\par\sect\sectd{\headerr only\par} two\par})");
  const auto& h = doc.sections[0].headers;
  CHECK_EQ(run_text(std::get<rtf::Paragraph>((*h.default_)[0].value)), std::string("d"));
  CHECK_EQ(run_text(std::get<rtf::Paragraph>((*h.first)[0].value)), std::string("f"));
  CHECK_EQ(run_text(std::get<rtf::Paragraph>((*h.left)[0].value)), std::string("l"));
  CHECK_EQ(run_text(std::get<rtf::Paragraph>((*h.right)[0].value)), std::string("r"));
  CHECK_EQ(run_text(std::get<rtf::Paragraph>((*doc.sections[0].footers.first)[0].value)), std::string("ff"));
  CHECK(!doc.sections[1].headers.default_.has_value()); CHECK(doc.sections[1].headers.right.has_value());
  CHECK_EQ(doc.sections[0].body.size(), std::size_t(1));
}
static void fields_sas_and_word() {
  auto doc = parse_ok(R"({\rtf1 Page {\field{\*\fldinst { PAGE }}} of {\field{\*\fldinst { NUMPAGES }}}\par
{\field\flddirty{\*\fldinst {\rtlch\fcs1 HYPERLINK "https://example.test/x" {\*\datafield 00d0c9ea}}}{\fldrslt {\cf2\ul Example{\field{\*\fldinst PAGE}{\fldrslt 1}}}}}\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(p.runs.size(), std::size_t(4));
  const auto& f1 = std::get<rtf::Field>(p.runs[1].value);
  CHECK_EQ(f1.instruction, std::string("PAGE")); CHECK(f1.result.empty());
  CHECK_EQ(std::get<rtf::Field>(p.runs[3].value).instruction, std::string("NUMPAGES"));
  const auto& q = body_paragraph(doc, 0, 1);
  const auto& f2 = std::get<rtf::Field>(q.runs[0].value);
  CHECK_EQ(f2.instruction, std::string("HYPERLINK \"https://example.test/x\"")); CHECK(f2.dirty);
  CHECK_EQ(f2.result.size(), std::size_t(2));
  CHECK_EQ(std::get<rtf::TextRun>(f2.result[0].value).text, std::string("Example"));
  CHECK(*std::get<rtf::TextRun>(f2.result[0].value).props.underline == rtf::Underline::Single);
  CHECK_EQ(std::get<rtf::Field>(f2.result[1].value).instruction, std::string("PAGE"));
  CHECK_EQ(std::get<rtf::TextRun>(std::get<rtf::Field>(f2.result[1].value).result[0].value).text, std::string("1"));
}
static void bookmarks_and_upr() {
  auto doc = parse_ok(R"({\rtf1 {\upr{\*\bkmkstart IDX}{\*\ud{\*\bkmkstart IDX}}}x{\*\bkmkend IDX}\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(p.runs.size(), std::size_t(3));
  const auto& b = std::get<rtf::Bookmark>(p.runs[0].value); CHECK_EQ(b.name, std::string("IDX")); CHECK(!b.end);
  CHECK(std::get<rtf::Bookmark>(p.runs[2].value).end);
}
static void footnotes() {
  auto doc = parse_ok(R"({\rtf1 text{\super\chftn}{\footnote\pard\plain{\super\chftn} note one\par}more{\footnote\ftnalt\pard end note}\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(p.runs.size(), std::size_t(4));
  CHECK_EQ(std::get<rtf::TextRun>(p.runs[0].value).text, std::string("text"));
  const auto& n = std::get<rtf::Footnote>(p.runs[1].value);
  CHECK(!n.endnote); CHECK_EQ(n.blocks.size(), std::size_t(1));
  CHECK_EQ(run_text(std::get<rtf::Paragraph>(n.blocks[0].value)), std::string(" note one"));
  CHECK_EQ(std::get<rtf::TextRun>(p.runs[2].value).text, std::string("more"));
  CHECK(std::get<rtf::Footnote>(p.runs[3].value).endnote);
  CHECK_EQ(run_text(std::get<rtf::Paragraph>(std::get<rtf::Footnote>(p.runs[3].value).blocks[0].value)), std::string("end note"));
}
static void pictures_hex_shppict_and_bin() {
  auto doc = parse_ok("{\\rtf1 {\\pict\\pngblip\\picw1\\pich1\\picwgoal15\\pichgoal15\\picscalex50 89504e47\r\n0d0a1a0a}"
                      "{\\*\\shppict{\\pict\\jpegblip ffd8}}{\\nonshppict{\\pict\\wmetafile8 0102}}"
                      "{\\pict\\emfblip\\bin4 \x01\x02{\x7d}{\\pict\\dibitmap0 00}\\par}");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(p.runs.size(), std::size_t(4));
  const auto& a = std::get<rtf::Picture>(p.runs[0].value);
  CHECK(a.format == rtf::PictureFormat::Png); CHECK_EQ(a.data, std::string("\x89PNG\r\n\x1a\n"));
  CHECK_EQ(*a.width, 1); CHECK_EQ(*a.goalWidth, 15); CHECK_EQ(*a.scaleX, 50); CHECK(!a.scaleY.has_value());
  CHECK(std::get<rtf::Picture>(p.runs[1].value).format == rtf::PictureFormat::Jpeg);
  const auto& c = std::get<rtf::Picture>(p.runs[2].value);
  CHECK(c.format == rtf::PictureFormat::Emf); CHECK_EQ(c.data, std::string("\x01\x02{}"));
  CHECK(std::get<rtf::Picture>(p.runs[3].value).format == rtf::PictureFormat::Dib);
  CHECK_EQ(count_warnings(doc, "skipped_destination"), std::size_t(1));
}
static void picture_hex_with_line_breaks_and_odd_digit() {
  auto doc = parse_ok("{\\rtf1 {\\pict\\pngblip 8950\r\n4e4\r\n7}{\\pict\\pngblip 89zz50}\\par}");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(std::get<rtf::Picture>(p.runs[0].value).data, std::string("\x89PNG"));   // the digits 8 9 5 0 4 e 4 7 pair up across the line breaks
  CHECK_EQ(std::get<rtf::Picture>(p.runs[1].value).data, std::string("\x89"));
  CHECK_EQ(count_warnings(doc, "bad_picture_data"), std::size_t(1));
}
static void shapes_word_style() {
  auto doc = parse_ok(R"({\rtf1 {\shp{\*\shpinst\shpleft290\shptop219\shpwr2{\sp{\sn shapeType}{\sv 75}}{\sp{\sn pib}{\sv{\pict\pngblip\picw7093\pich6484 8950}}}{\sp{\sn fLine}{\sv 0}}}{\shprslt{\pict\wmetafile8 0102}}}x{\shp{\*\shpinst{\shptxt in a box\par}}}{\shp{\*\shpinst\shpz0}}\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(p.runs.size(), std::size_t(3));
  const auto& s = std::get<rtf::Shape>(p.runs[0].value);
  CHECK(s.blocks.empty()); CHECK(s.picture.has_value()); CHECK(s.picture->format == rtf::PictureFormat::Png); CHECK_EQ(*s.picture->width, 7093);
  CHECK_EQ(std::get<rtf::TextRun>(p.runs[1].value).text, std::string("x"));
  const auto& t = std::get<rtf::Shape>(p.runs[2].value);
  CHECK_EQ(run_text(std::get<rtf::Paragraph>(t.blocks[0].value)), std::string("in a box"));
  CHECK_EQ(count_warnings(doc, "shape_geometry_dropped"), std::size_t(1));
  CHECK_EQ(count_warnings(doc, "skipped_destination"), std::size_t(1));   // shprslt
}
static void objects() {
  auto doc = parse_ok(R"({\rtf1 {\object\objemb{\*\objclass Excel.Sheet.8}{\*\objdata 0102}{\result{\pard res\par}}}\par})");
  const auto& o = std::get<rtf::EmbeddedObject>(body_paragraph(doc, 0, 0).runs[0].value);
  CHECK_EQ(*o.className, std::string("Excel.Sheet.8"));
  CHECK_EQ(run_text(std::get<rtf::Paragraph>(o.result[0].value)), std::string("res"));
  CHECK(has_warning(doc, "object_data_dropped"));
}
static void lists_as_text() {
  auto doc = parse_ok(R"({\rtf1{\fonttbl{\f3\fnil\fcharset2 Symbol;}}{\*\listtable{\list\listid1{\listlevel\levelnfc23{\leveltext\'01\u-3913 ?;}}}}
{\listtext\pard\plain\f3 \'b7\tab}\pard\fi-360\li720\ls1\ilvl0 item\par{\*\pn\pnlvlblt\pnf3{\pntxtb\'b7}}{\pntext\f3\'b7\tab}\pard second\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  CHECK_EQ(p.listText.size(), std::size_t(2));
  CHECK_EQ(std::get<rtf::TextRun>(p.listText[0].value).text, std::string("\xEF\x82\xB7"));
  CHECK(std::holds_alternative<rtf::Tab>(p.listText[1].value));
  CHECK_EQ(run_text(p), std::string("item")); CHECK_EQ(p.props.list->id, 1);
  const auto& q = body_paragraph(doc, 0, 1);
  CHECK_EQ(q.listText.size(), std::size_t(2)); CHECK_EQ(run_text(q), std::string("second"));
  CHECK_EQ(count_warnings(doc, "skipped_destination"), std::size_t(2));   // listtable and pn, one warning each
}

int main() {
  sas_page_setup_and_sections();
  sectd_without_sect_is_one_section();
  headers_all_kinds();
  fields_sas_and_word();
  bookmarks_and_upr();
  footnotes();
  pictures_hex_shppict_and_bin();
  picture_hex_with_line_breaks_and_odd_digit();
  shapes_word_style();
  objects();
  lists_as_text();
  return rtf_test::result();
}
