// The reader: turns the token stream into the document model.
// The rules follow the design document, section "The reader".
#include "rtf/reader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "builder.hpp"
#include "encodings.hpp"
#include "properties.hpp"
#include "tokenizer.hpp"

namespace rtf {
namespace detail {
namespace {

// ---------------------------------------------------------------------------
// The control-word table
// ---------------------------------------------------------------------------

// Each known control word belongs to one family. The family decides which
// handler runs; the operation says what the handler does.
enum class Family : std::uint8_t {
  Destination, Document, Char, Para, Border, Special, Font, Color, Style, Info, Table, Row, CellDef,
  Section, Picture, Frame
};

// What a destination word does when it is the first word of a group.
// HeaderFooter opens a sink on the section's header or footer.
// NestTableProps holds the definition of a nested row; its content is read.
// Transparent groups (\*\shppict, \*\shpinst, \*\ud) are read as if their
// content stood in the parent. The other kinds open the structures of the
// design's "Destinations the reader understands".
enum class DestKind : std::uint8_t {
  FontTable, ColorTable, StyleSheet, Info, Generator, SkipWarn, SkipSilent, HeaderFooter, NestTableProps,
  Transparent, Upr, Field, FieldInstruction, FieldResult, Footnote, BookmarkStart, BookmarkEnd, ListText,
  Picture, Shape, ShapeText, ShapeName, ShapeValue, Object, ObjectClass, ObjectData, ObjectResult
};
// \sect, \sectd and the words that fill the current section.
enum class SectionOp : std::uint8_t {
  Sect, Sectd, PageW, PageH, MargL, MargR, MargT, MargB, Landscape, HeaderY, FooterY,
  BreakNone, BreakColumn, BreakPage, BreakEven, BreakOdd, Columns, TitlePage, PageNumberStart, PageNumberRestart
};
// The format and size words of \pict.
enum class PictOp : std::uint8_t {
  Png, Jpeg, Emf, Wmf, Dib, Bmp, Mac, Width, Height, GoalWidth, GoalHeight, ScaleX, ScaleY
};
// Flags of an open footnote or field.
enum class FrameOp : std::uint8_t { FootnoteAlt, FieldDirty, FieldLock };
enum class DocOp : std::uint8_t {
  Rtf, Ansi, Mac, Pc, Pca, AnsiCpg, Deff, DefLang, Uc,
  PaperW, PaperH, MargL, MargR, MargT, MargB, Landscape, FacingP
};
enum class CharOp : std::uint8_t {
  Font, Size, Bold, Italic, Underline, UlNone, UlDotted, UlDouble, UlWord, UlThick, UlWave, UlDash,
  Strike, DoubleStrike, Super, Sub, NoSuperSub, Up, Down, Color, Background, Highlight,
  Caps, SmallCaps, Hidden, Outline, Shadow, Lang, Deleted, Revised, Plain
};
enum class ParaOp : std::uint8_t {
  Pard, Ql, Qc, Qr, Qj, Li, Ri, Fi, Sb, Sa, Sl, SlMult, Keep, KeepN, PageBB, WidCtl, NoWidCtl,
  Style, Ls, Ilvl, RtlPar, LtrPar, InTbl, Itap,
  Tx, Tb, Tqc, Tqr, Tqdec, TlDot, TlHyph, TlUl, TlTh, TlEq,
  Shading, CbPat, CfPat
};
enum class BorderOp : std::uint8_t {
  OpenTop, OpenLeft, OpenBottom, OpenRight, OpenBetween, OpenBar, OpenBox,
  Single, Double, Dotted, Dashed, Thick, Hairline, Triple, None, Width, Color, Space
};
enum class SpecialOp : std::uint8_t {
  Par, Line, Tab, Page, Column, Chpgn, Chftn, Lbr,
  Emdash, Endash, Bullet, Lquote, Rquote, Ldblquote, Rdblquote, Emspace, Enspace, Qmspace, Zwj, Zwnj
};
enum class FontOp : std::uint8_t { Nil, Roman, Swiss, Modern, Script, Decor, Tech, Bidi, Charset, Pitch, CodePage };
enum class ColorOp : std::uint8_t { Red, Green, Blue };
enum class StyleOp : std::uint8_t { Character, Table, Section, BasedOn, Next };
enum class InfoOp : std::uint8_t { Year, Month, Day, Hour, Minute, Second };
// \trowd, \cellx and the words that end cells and rows.
enum class TableOp : std::uint8_t { Trowd, Cellx, Cell, NestCell, Row, NestRow };
// The \tr* row words and \rtlrow.
enum class RowOp : std::uint8_t {
  Header, Keep, AlignLeft, AlignCenter, AlignRight, Left, GapHalf, Height,
  PadLeft, PadTop, PadBottom, PadRight,
  BorderTop, BorderLeft, BorderBottom, BorderRight, BorderHorizontal, BorderVertical, Rtl
};
// The \cl* words of the cell definition under construction.
enum class CellOp : std::uint8_t {
  VAlignTop, VAlignCenter, VAlignBottom, TextLrtb, TextTbrl, TextBtlr, TextLrtbv, TextTbrlv, NoWrap,
  MergeFirst, MergeNext, VMergeFirst, VMergeNext, Background, Foreground, Percent,
  BorderTop, BorderLeft, BorderBottom, BorderRight, PadT, PadL, PadB, PadR, Width, WidthType
};

struct WordInfo {
  const char* name;
  Family family;
  std::uint8_t op;
};

constexpr WordInfo entry(const char* n, DestKind k) { return {n, Family::Destination, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, DocOp k) { return {n, Family::Document, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, CharOp k) { return {n, Family::Char, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, ParaOp k) { return {n, Family::Para, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, BorderOp k) { return {n, Family::Border, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, SpecialOp k) { return {n, Family::Special, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, FontOp k) { return {n, Family::Font, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, ColorOp k) { return {n, Family::Color, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, StyleOp k) { return {n, Family::Style, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, InfoOp k) { return {n, Family::Info, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, TableOp k) { return {n, Family::Table, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, RowOp k) { return {n, Family::Row, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, CellOp k) { return {n, Family::CellDef, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, SectionOp k) { return {n, Family::Section, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, PictOp k) { return {n, Family::Picture, static_cast<std::uint8_t>(k)}; }
constexpr WordInfo entry(const char* n, FrameOp k) { return {n, Family::Frame, static_cast<std::uint8_t>(k)}; }

// Sorted by byte value of the name, for the binary search in find_word.
// Words that only start a destination are listed with the destination kind.
constexpr WordInfo kWords[] = {
    entry("aftnsep", DestKind::SkipWarn),
    entry("aftnsepc", DestKind::SkipWarn),
    entry("annotation", DestKind::SkipWarn),
    entry("ansi", DocOp::Ansi),
    entry("ansicpg", DocOp::AnsiCpg),
    entry("atnauthor", DestKind::SkipWarn),
    entry("atndate", DestKind::SkipWarn),
    entry("atnid", DestKind::SkipWarn),
    entry("atnparent", DestKind::SkipWarn),
    entry("atnref", DestKind::SkipWarn),
    entry("b", CharOp::Bold),
    entry("background", DestKind::SkipWarn),
    entry("bkmkend", DestKind::BookmarkEnd),
    entry("bkmkstart", DestKind::BookmarkStart),
    entry("blipuid", DestKind::SkipWarn),
    entry("blue", ColorOp::Blue),
    entry("box", BorderOp::OpenBox),
    entry("brdrb", BorderOp::OpenBottom),
    entry("brdrbar", BorderOp::OpenBar),
    entry("brdrbtw", BorderOp::OpenBetween),
    entry("brdrcf", BorderOp::Color),
    entry("brdrdash", BorderOp::Dashed),
    entry("brdrdb", BorderOp::Double),
    entry("brdrdot", BorderOp::Dotted),
    entry("brdrhair", BorderOp::Hairline),
    entry("brdrl", BorderOp::OpenLeft),
    entry("brdrnone", BorderOp::None),
    entry("brdrr", BorderOp::OpenRight),
    entry("brdrs", BorderOp::Single),
    entry("brdrt", BorderOp::OpenTop),
    entry("brdrth", BorderOp::Thick),
    entry("brdrtriple", BorderOp::Triple),
    entry("brdrw", BorderOp::Width),
    entry("brsp", BorderOp::Space),
    entry("bullet", SpecialOp::Bullet),
    entry("caps", CharOp::Caps),
    entry("cb", CharOp::Background),
    entry("cbpat", ParaOp::CbPat),
    entry("cell", TableOp::Cell),
    entry("cellx", TableOp::Cellx),
    entry("cf", CharOp::Color),
    entry("cfpat", ParaOp::CfPat),
    entry("chcbpat", CharOp::Background),
    entry("chftn", SpecialOp::Chftn),
    entry("chpgn", SpecialOp::Chpgn),
    entry("clNoWrap", CellOp::NoWrap),
    entry("clbrdrb", CellOp::BorderBottom),
    entry("clbrdrl", CellOp::BorderLeft),
    entry("clbrdrr", CellOp::BorderRight),
    entry("clbrdrt", CellOp::BorderTop),
    entry("clcbpat", CellOp::Background),
    entry("clcfpat", CellOp::Foreground),
    entry("clftsWidth", CellOp::WidthType),
    entry("clmgf", CellOp::MergeFirst),
    entry("clmrg", CellOp::MergeNext),
    entry("clpadb", CellOp::PadB),
    entry("clpadl", CellOp::PadL),
    entry("clpadr", CellOp::PadR),
    entry("clpadt", CellOp::PadT),
    entry("clshdng", CellOp::Percent),
    entry("cltxbtlr", CellOp::TextBtlr),
    entry("cltxlrtb", CellOp::TextLrtb),
    entry("cltxlrtbv", CellOp::TextLrtbv),
    entry("cltxtbrl", CellOp::TextTbrl),
    entry("cltxtbrlv", CellOp::TextTbrlv),
    entry("clvertalb", CellOp::VAlignBottom),
    entry("clvertalc", CellOp::VAlignCenter),
    entry("clvertalt", CellOp::VAlignTop),
    entry("clvmgf", CellOp::VMergeFirst),
    entry("clvmrg", CellOp::VMergeNext),
    entry("clwWidth", CellOp::Width),
    entry("colorschememapping", DestKind::SkipWarn),
    entry("colortbl", DestKind::ColorTable),
    entry("cols", SectionOp::Columns),
    entry("column", SpecialOp::Column),
    entry("cpg", FontOp::CodePage),
    entry("cs", StyleOp::Character),
    entry("datafield", DestKind::SkipSilent),
    entry("datastore", DestKind::SkipWarn),
    entry("defchp", DestKind::SkipWarn),
    entry("deff", DocOp::Deff),
    entry("deflang", DocOp::DefLang),
    entry("defpap", DestKind::SkipWarn),
    entry("deleted", CharOp::Deleted),
    entry("dibitmap", PictOp::Dib),
    entry("dn", CharOp::Down),
    entry("do", DestKind::SkipWarn),
    entry("ds", StyleOp::Section),
    entry("dy", InfoOp::Day),
    entry("emdash", SpecialOp::Emdash),
    entry("emfblip", PictOp::Emf),
    entry("emspace", SpecialOp::Emspace),
    entry("endash", SpecialOp::Endash),
    entry("enspace", SpecialOp::Enspace),
    entry("f", CharOp::Font),
    entry("facingp", DocOp::FacingP),
    entry("falt", DestKind::SkipSilent),
    entry("fbidi", FontOp::Bidi),
    entry("fcharset", FontOp::Charset),
    entry("fdecor", FontOp::Decor),
    entry("fi", ParaOp::Fi),
    entry("field", DestKind::Field),
    entry("flddirty", FrameOp::FieldDirty),
    entry("fldinst", DestKind::FieldInstruction),
    entry("fldlock", FrameOp::FieldLock),
    entry("fldrslt", DestKind::FieldResult),
    entry("fmodern", FontOp::Modern),
    entry("fname", DestKind::SkipSilent),
    entry("fnil", FontOp::Nil),
    entry("fonttbl", DestKind::FontTable),
    entry("footer", DestKind::HeaderFooter),
    entry("footerf", DestKind::HeaderFooter),
    entry("footerl", DestKind::HeaderFooter),
    entry("footerr", DestKind::HeaderFooter),
    entry("footery", SectionOp::FooterY),
    entry("footnote", DestKind::Footnote),
    entry("fprq", FontOp::Pitch),
    entry("froman", FontOp::Roman),
    entry("fs", CharOp::Size),
    entry("fscript", FontOp::Script),
    entry("fswiss", FontOp::Swiss),
    entry("ftech", FontOp::Tech),
    entry("ftnalt", FrameOp::FootnoteAlt),
    entry("ftnsep", DestKind::SkipWarn),
    entry("ftnsepc", DestKind::SkipWarn),
    entry("generator", DestKind::Generator),
    entry("green", ColorOp::Green),
    entry("header", DestKind::HeaderFooter),
    entry("headerf", DestKind::HeaderFooter),
    entry("headerl", DestKind::HeaderFooter),
    entry("headerr", DestKind::HeaderFooter),
    entry("headery", SectionOp::HeaderY),
    entry("highlight", CharOp::Highlight),
    entry("hr", InfoOp::Hour),
    entry("htmltag", DestKind::SkipWarn),
    entry("i", CharOp::Italic),
    entry("ilvl", ParaOp::Ilvl),
    entry("info", DestKind::Info),
    entry("intbl", ParaOp::InTbl),
    entry("itap", ParaOp::Itap),
    entry("jpegblip", PictOp::Jpeg),
    entry("keep", ParaOp::Keep),
    entry("keepn", ParaOp::KeepN),
    entry("landscape", DocOp::Landscape),
    entry("lang", CharOp::Lang),
    entry("latentstyles", DestKind::SkipWarn),
    entry("lbr", SpecialOp::Lbr),
    entry("ldblquote", SpecialOp::Ldblquote),
    entry("li", ParaOp::Li),
    entry("line", SpecialOp::Line),
    entry("listoverridetable", DestKind::SkipWarn),
    entry("listtable", DestKind::SkipWarn),
    entry("listtext", DestKind::ListText),
    entry("lndscpsxn", SectionOp::Landscape),
    entry("lquote", SpecialOp::Lquote),
    entry("ls", ParaOp::Ls),
    entry("lsdlockedexcept", DestKind::SkipWarn),
    entry("ltrpar", ParaOp::LtrPar),
    entry("mac", DocOp::Mac),
    entry("macpict", PictOp::Mac),
    entry("margb", DocOp::MargB),
    entry("margbsxn", SectionOp::MargB),
    entry("margl", DocOp::MargL),
    entry("marglsxn", SectionOp::MargL),
    entry("margr", DocOp::MargR),
    entry("margrsxn", SectionOp::MargR),
    entry("margt", DocOp::MargT),
    entry("margtsxn", SectionOp::MargT),
    entry("mhtmltag", DestKind::SkipWarn),
    entry("min", InfoOp::Minute),
    entry("mmathPr", DestKind::SkipWarn),
    entry("mo", InfoOp::Month),
    entry("nestcell", TableOp::NestCell),
    entry("nestrow", TableOp::NestRow),
    entry("nesttableprops", DestKind::NestTableProps),
    entry("nonesttables", DestKind::SkipWarn),
    entry("nonshppict", DestKind::SkipWarn),
    entry("nosupersub", CharOp::NoSuperSub),
    entry("nowidctlpar", ParaOp::NoWidCtl),
    entry("objclass", DestKind::ObjectClass),
    entry("objdata", DestKind::ObjectData),
    entry("object", DestKind::Object),
    entry("outl", CharOp::Outline),
    entry("page", SpecialOp::Page),
    entry("pagebb", ParaOp::PageBB),
    entry("panose", DestKind::SkipSilent),
    entry("paperh", DocOp::PaperH),
    entry("paperw", DocOp::PaperW),
    entry("par", SpecialOp::Par),
    entry("pard", ParaOp::Pard),
    entry("pc", DocOp::Pc),
    entry("pca", DocOp::Pca),
    entry("pgdsctbl", DestKind::SkipWarn),
    entry("pghsxn", SectionOp::PageH),
    entry("pgnrestart", SectionOp::PageNumberRestart),
    entry("pgnstarts", SectionOp::PageNumberStart),
    entry("pgwsxn", SectionOp::PageW),
    entry("pich", PictOp::Height),
    entry("pichgoal", PictOp::GoalHeight),
    entry("picprop", DestKind::SkipWarn),
    entry("picscalex", PictOp::ScaleX),
    entry("picscaley", PictOp::ScaleY),
    entry("pict", DestKind::Picture),
    entry("picw", PictOp::Width),
    entry("picwgoal", PictOp::GoalWidth),
    entry("plain", CharOp::Plain),
    entry("pn", DestKind::SkipWarn),
    entry("pngblip", PictOp::Png),
    entry("pnseclvl", DestKind::SkipWarn),
    entry("pntext", DestKind::ListText),
    entry("protusertbl", DestKind::SkipWarn),
    entry("qc", ParaOp::Qc),
    entry("qj", ParaOp::Qj),
    entry("ql", ParaOp::Ql),
    entry("qmspace", SpecialOp::Qmspace),
    entry("qr", ParaOp::Qr),
    entry("rdblquote", SpecialOp::Rdblquote),
    entry("red", ColorOp::Red),
    entry("result", DestKind::ObjectResult),
    entry("revised", CharOp::Revised),
    entry("revtbl", DestKind::SkipWarn),
    entry("ri", ParaOp::Ri),
    entry("row", TableOp::Row),
    entry("rquote", SpecialOp::Rquote),
    entry("rsidtbl", DestKind::SkipWarn),
    entry("rtf", DocOp::Rtf),
    entry("rtlpar", ParaOp::RtlPar),
    entry("rtlrow", RowOp::Rtl),
    entry("s", ParaOp::Style),
    entry("sa", ParaOp::Sa),
    entry("sb", ParaOp::Sb),
    entry("sbasedon", StyleOp::BasedOn),
    entry("sbkcol", SectionOp::BreakColumn),
    entry("sbkeven", SectionOp::BreakEven),
    entry("sbknone", SectionOp::BreakNone),
    entry("sbkodd", SectionOp::BreakOdd),
    entry("sbkpage", SectionOp::BreakPage),
    entry("scaps", CharOp::SmallCaps),
    entry("sec", InfoOp::Second),
    entry("sect", SectionOp::Sect),
    entry("sectd", SectionOp::Sectd),
    entry("shad", CharOp::Shadow),
    entry("shading", ParaOp::Shading),
    entry("shp", DestKind::Shape),
    entry("shpinst", DestKind::Transparent),
    entry("shppict", DestKind::Transparent),
    entry("shprslt", DestKind::SkipWarn),
    entry("shptxt", DestKind::ShapeText),
    entry("sl", ParaOp::Sl),
    entry("slmult", ParaOp::SlMult),
    entry("sn", DestKind::ShapeName),
    entry("snext", StyleOp::Next),
    entry("strike", CharOp::Strike),
    entry("striked", CharOp::DoubleStrike),
    entry("stylesheet", DestKind::StyleSheet),
    entry("sub", CharOp::Sub),
    entry("super", CharOp::Super),
    entry("sv", DestKind::ShapeValue),
    entry("tab", SpecialOp::Tab),
    entry("tb", ParaOp::Tb),
    entry("tc", DestKind::SkipWarn),
    entry("themedata", DestKind::SkipWarn),
    entry("titlepg", SectionOp::TitlePage),
    entry("tldot", ParaOp::TlDot),
    entry("tleq", ParaOp::TlEq),
    entry("tlhyph", ParaOp::TlHyph),
    entry("tlth", ParaOp::TlTh),
    entry("tlul", ParaOp::TlUl),
    entry("tqc", ParaOp::Tqc),
    entry("tqdec", ParaOp::Tqdec),
    entry("tqr", ParaOp::Tqr),
    entry("trbrdrb", RowOp::BorderBottom),
    entry("trbrdrh", RowOp::BorderHorizontal),
    entry("trbrdrl", RowOp::BorderLeft),
    entry("trbrdrr", RowOp::BorderRight),
    entry("trbrdrt", RowOp::BorderTop),
    entry("trbrdrv", RowOp::BorderVertical),
    entry("trgaph", RowOp::GapHalf),
    entry("trhdr", RowOp::Header),
    entry("trkeep", RowOp::Keep),
    entry("trleft", RowOp::Left),
    entry("trowd", TableOp::Trowd),
    entry("trpaddb", RowOp::PadBottom),
    entry("trpaddl", RowOp::PadLeft),
    entry("trpaddr", RowOp::PadRight),
    entry("trpaddt", RowOp::PadTop),
    entry("trqc", RowOp::AlignCenter),
    entry("trql", RowOp::AlignLeft),
    entry("trqr", RowOp::AlignRight),
    entry("trrh", RowOp::Height),
    entry("ts", StyleOp::Table),
    entry("tx", ParaOp::Tx),
    entry("uc", DocOp::Uc),
    entry("ud", DestKind::Transparent),
    entry("ul", CharOp::Underline),
    entry("uld", CharOp::UlDotted),
    entry("uldash", CharOp::UlDash),
    entry("uldb", CharOp::UlDouble),
    entry("ulnone", CharOp::UlNone),
    entry("ulth", CharOp::UlThick),
    entry("ulw", CharOp::UlWord),
    entry("ulwave", CharOp::UlWave),
    entry("up", CharOp::Up),
    entry("upr", DestKind::Upr),
    entry("userprops", DestKind::SkipWarn),
    entry("v", CharOp::Hidden),
    entry("wbitmap", PictOp::Bmp),
    entry("wgrffmtfilter", DestKind::SkipWarn),
    entry("widctlpar", ParaOp::WidCtl),
    entry("wmetafile", PictOp::Wmf),
    entry("xe", DestKind::SkipWarn),
    entry("xmlnstbl", DestKind::SkipWarn),
    entry("yr", InfoOp::Year),
    entry("zwj", SpecialOp::Zwj),
    entry("zwnj", SpecialOp::Zwnj),
};

// Compares two names byte by byte, as unsigned chars, like std::string_view.
constexpr int compare_names(const char* a, const char* b) {
  while (*a != '\0' && *a == *b) {
    ++a;
    ++b;
  }
  return static_cast<int>(static_cast<unsigned char>(*a)) - static_cast<int>(static_cast<unsigned char>(*b));
}

constexpr bool words_sorted() {
  for (std::size_t i = 1; i < sizeof(kWords) / sizeof(kWords[0]); ++i) {
    if (compare_names(kWords[i - 1].name, kWords[i].name) >= 0) return false;
  }
  return true;
}
static_assert(words_sorted(), "kWords must be sorted by name, without duplicates");

const WordInfo* find_word(std::string_view name) {
  const WordInfo* begin = kWords;
  const WordInfo* end = kWords + sizeof(kWords) / sizeof(kWords[0]);
  const WordInfo* it = std::lower_bound(begin, end, name, [](const WordInfo& w, std::string_view n) {
    return std::string_view(w.name) < n;
  });
  if (it != end && std::string_view(it->name) == name) return it;
  return nullptr;
}

template <class Op>
Op op_of(const WordInfo& w) {
  return static_cast<Op>(w.op);
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

bool is_ascii_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

std::string trim(std::string_view s) {
  while (!s.empty() && is_ascii_space(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_ascii_space(s.back())) s.remove_suffix(1);
  return std::string(s);
}

// A font or style name: the entry's text up to its ';', trimmed.
std::string entry_name(std::string_view text) {
  const std::size_t semi = text.find(';');
  return trim(text.substr(0, semi));
}

// An info or generator value: the text trimmed, without a trailing ';'.
std::string entry_value(std::string_view text) {
  std::string value = trim(text);
  if (!value.empty() && value.back() == ';') value = trim(std::string_view(value).substr(0, value.size() - 1));
  return value;
}

int param_or(const Token& tok, int fallback) { return tok.hasParam ? tok.param : fallback; }

// A toggle word such as \b turns the property on; \b0 turns it off.
bool toggle(const Token& tok) { return !tok.hasParam || tok.param != 0; }

// The paragraph's table nesting level: \itapN when present, else 1 for \intbl.
int paragraph_level(const ParaState& p) { return p.itap > 0 ? p.itap : (p.inTable ? 1 : 0); }

// Where a group's text goes.
enum class Dest : std::uint8_t { Body, FontTable, ColorTable, StyleSheet, StyleEntry, Info, InfoEntry, Generator, Skip };
// Field, Shape and Object are the group of an open frame: their own text is
// dropped. FieldInstruction, Name and Picture collect text into a string or
// bytes. FieldResult and ListText collect runs and inlines into a list.
// For the frame targets, GroupState::frame names the frame.
enum class TextTarget : std::uint8_t {
  Paragraph, Buffer, ColorTable, Discard,
  Field, FieldInstruction, FieldResult, ListText, Name, Picture, Shape, Object
};

// What a group finishes when its '}' arrives, besides its sink.
enum class Owner : std::uint8_t {
  None, Field, Footnote, Picture, Shape, Object, BookmarkStart, BookmarkEnd, ShapeName, ObjectClass
};

// The targets that take breaks, tabs, fields and other inlines.
bool takes_inlines(TextTarget t) {
  return t == TextTarget::Paragraph || t == TextTarget::FieldResult || t == TextTarget::ListText;
}

// An open \pict. Hex digits pair up across text tokens, so the first
// digit of a pair waits in `half`.
struct PictureFrame {
  Picture picture;
  std::size_t depth = 0;  // stack size of the \pict group; text in child groups is not data
  int half = -1;          // the waiting digit, or -1
  bool stopped = false;   // a bad byte ended the data
  bool hasFormat = false;
};

// An open \shp, with the name of the property being read.
struct ShapeFrame {
  Shape shape;
  std::string property;
};

// The state each '{' copies and each '}' restores (design, "Group state").
struct GroupState {
  CharProps chars;
  ParaState para;
  Dest dest = Dest::Body;
  TextTarget target = TextTarget::Paragraph;
  int uc = 1;                  // \ucN: fallback units after each Unicode escape
  bool first = true;           // no token yet after '{' other than \*
  bool star = false;           // the group started with \*
  bool ownsEntry = false;      // this group is a style, info, colour table or generator entry
  bool ownsSink = false;       // this group opened the innermost BlockSink
  bool nestProps = false;      // inside {\*\nesttableprops ...}
  bool uprPending = false;     // a \upr group whose first child group is still to come
  bool pibValue = false;       // inside the \sv value of a shape's pib property
  Owner owner = Owner::None;   // the frame or name this group finishes
  BorderTarget border;
  std::size_t sink = 0;        // index of the BlockSink this group writes to
  std::size_t frame = 0;       // index of the field, picture, shape or object frame of the target
};

// ---------------------------------------------------------------------------
// The reader
// ---------------------------------------------------------------------------

class Reader {
 public:
  Reader(const unsigned char* data, std::size_t size, std::size_t base, const ParseOptions& options);
  RtfDocument run();

 private:
  GroupState& top() { return stack_.back(); }
  BlockSink& sink() { return sinks_[top().sink]; }

  // Token handlers.
  void on_group_open(const Token& tok);
  void on_group_close();
  void on_end(const Token& tok);
  void on_text(std::string_view text, std::size_t offset);
  void on_hex(const Token& tok);
  void on_unicode(const Token& tok);
  void on_symbol(const Token& tok);
  void on_word(const Token& tok);
  void skip_token(const Token& tok);
  void after_document(const Token& tok);
  void note_clamped(const Token& tok);

  // Groups and destinations.
  void close_group();
  bool decide_destination(const Token& tok, const WordInfo* info, bool star);
  bool open_destination(const Token& tok, DestKind kind);
  void skip_group();
  void finish_document(const GroupState& root);
  void close_sink(BlockSink& s, const ParaState& para);
  bool open_header_footer(std::string_view word);
  void open_sink(std::vector<Block>* blocks);

  // Sections, fields, footnotes, pictures, shapes, objects, bookmarks and
  // lists (design, "Destinations the reader understands").
  bool open_structure(const Token& tok, DestKind kind);
  void close_frame(Owner owner);
  void finish_picture(PictureFrame& frame);
  void section_word(const Token& tok, SectionOp op);
  void end_section();
  void commit_section(Section& s);
  void picture_word(const Token& tok, PictOp op);
  void picture_text(std::string_view text, std::size_t offset);
  void picture_binary(std::string_view bytes);
  void frame_word(FrameOp op);
  void flatten_break();

  // One handler per destination kind.
  void dispatch_word(const Token& tok, const WordInfo* info);
  void body_word(const Token& tok, const WordInfo* info);
  void font_table_word(const Token& tok, const WordInfo* info);
  void color_table_word(const Token& tok, const WordInfo* info);
  void style_word(const Token& tok, const WordInfo* info);
  void info_word(const Token& tok, const WordInfo* info);

  // One handler per property family.
  void document_word(const Token& tok, DocOp op);
  void char_word(const Token& tok, CharOp op);
  void para_word(const Token& tok, ParaOp op);
  void border_word(const Token& tok, BorderOp op);
  void open_border(BorderTarget::Kind kind, BorderTarget::Side side);
  void border_style(BorderStyle style);
  int border_slots(Border* out[4]);
  void special_word(SpecialOp op);

  // Tables.
  int table_level(const ParaState& p) const;
  int definition_level();
  int nested_level();
  void start_row_definition();
  void end_cell_definition(const Token& tok);
  void end_cell_or_row(TableOp op);
  void row_word(const Token& tok, RowOp op);
  void cell_word(const Token& tok, CellOp op);

  // The document tables.
  void begin_font_entry(int number);
  void finish_font_entry();
  void push_color();
  void begin_info_entry(const Token& tok);
  void commit_info();
  void commit_style();
  void page_changed();

  // Text.
  void emit(std::string_view utf8);
  void emit_code_point(std::uint32_t cp);
  void emit_inline(Inline item);
  std::uint32_t decode(unsigned char b, std::size_t offset);
  int current_code_page();
  void flush_pending_surrogate();

  // Warnings.
  void warn(const char* code, std::string message, std::size_t offset);
  void warn_once(std::set<std::string>& seen, std::string_view key, const char* code, std::string message,
                 std::size_t offset);

  Tokenizer tokenizer_;
  std::size_t base_;          // offset of the tokenizer's input in the caller's bytes
  std::size_t offset_ = 0;    // offset of the current token in the caller's bytes
  std::size_t max_depth_;
  RtfDocument doc_;
  std::vector<GroupState> stack_;
  // Sections are built in a deque. Adding a section does not move the
  // others, so BlockSink targets into bodies, headers and footers stay
  // valid. finish_document moves them into doc_.sections.
  std::deque<Section> sections_;
  std::vector<BlockSink> sinks_;
  SectionState section_;
  bool section_defined_ = false;  // a \sectd or section word has been read

  // Open frames, innermost last. A frame lives as long as the group that
  // opened it; GroupState holds only its index. Footnotes, shapes and
  // objects are in deques because a sink points into their blocks.
  std::vector<Field> fields_;
  std::vector<PictureFrame> pictures_;
  std::deque<ShapeFrame> shapes_;
  std::deque<EmbeddedObject> objects_;
  std::deque<Footnote> footnotes_;
  std::string name_;  // text of a bookmark name, a shape property name or an object class

  // Group skipping and depth.
  bool skipping_ = false;        // inside a skipped destination
  std::size_t skip_nesting_ = 0; // groups opened inside the skipped one
  std::size_t overflow_ = 0;     // groups opened beyond max_depth_
  bool finished_ = false;        // the root group has closed
  std::size_t trailing_ = 0;     // groups opened after the root group closed

  // Unicode escapes.
  std::size_t fallback_left_ = 0;  // fallback units still to skip
  std::uint32_t pending_high_ = 0; // a high surrogate waiting for its pair

  // The document tables.
  std::string buffer_;            // text of the font, style, info or generator entry
  std::optional<int> font_entry_; // font number being defined
  std::size_t font_entry_depth_ = 0;
  bool ansicpg_seen_ = false;
  Color color_;
  bool color_has_ = false;
  Style style_;
  std::string info_key_;
  std::optional<int> info_param_;
  bool info_time_ = false;
  bool info_has_year_ = false;
  int info_parts_[6] = {0, 0, 0, 0, 0, 0};

  // Warnings given once per key.
  std::set<std::string> unknown_seen_, skipped_seen_, param_seen_, code_page_seen_, once_seen_;
};

Reader::Reader(const unsigned char* data, std::size_t size, std::size_t base, const ParseOptions& options)
    : tokenizer_(data, size), base_(base), max_depth_(options.maxDepth > 0 ? options.maxDepth : 1) {
  sections_.emplace_back();
  sinks_.emplace_back(&sections_.back().body);
  section_.reset(doc_.page);
}

RtfDocument Reader::run() {
  for (;;) {
    Token tok = tokenizer_.next();
    offset_ = base_ + tok.offset;
    note_clamped(tok);
    if (tok.kind == TokenKind::End) {
      on_end(tok);
      break;
    }
    if (finished_) {
      after_document(tok);
      continue;
    }
    if (skipping_) {
      skip_token(tok);
      continue;
    }

    // After a Unicode escape, skip its fallback: one unit per text byte or
    // \'hh. Any other token ends the skip early (design, "Text decoding").
    if (fallback_left_ > 0) {
      if (tok.kind == TokenKind::Text) {
        const std::size_t n = std::min(fallback_left_, tok.text.size());
        tok.text.remove_prefix(n);
        tok.offset += n;
        fallback_left_ -= n;
        if (tok.text.empty()) continue;
      } else if (tok.kind == TokenKind::HexByte) {
        --fallback_left_;
        continue;
      } else {
        fallback_left_ = 0;
      }
    }
    if (tok.kind != TokenKind::Unicode) flush_pending_surrogate();

    switch (tok.kind) {
      case TokenKind::GroupOpen: on_group_open(tok); break;
      case TokenKind::GroupClose: on_group_close(); break;
      case TokenKind::ControlWord: on_word(tok); break;
      case TokenKind::ControlSymbol: on_symbol(tok); break;
      case TokenKind::HexByte: on_hex(tok); break;
      case TokenKind::Unicode: on_unicode(tok); break;
      case TokenKind::Text: on_text(tok.text, tok.offset); break;
      case TokenKind::Binary:
        // \bin in a picture holds the picture's bytes. Elsewhere its
        // bytes are not text.
        top().first = false;
        if (top().target == TextTarget::Picture) picture_binary(tok.text);
        break;
      case TokenKind::End: break;
    }
  }
  return std::move(doc_);
}

// ---------------------------------------------------------------------------
// Groups
// ---------------------------------------------------------------------------

void Reader::on_group_open(const Token& tok) {
  if (stack_.empty()) {
    stack_.emplace_back();  // the root group, checked by parse() to be {\rtf
    return;
  }
  if (stack_.size() >= max_depth_) {
    // Deeper groups are flattened into the current one (design, "Group state").
    ++overflow_;
    // The flattened group is transparent: its first word must not decide
    // the destination of the group it is flattened into.
    top().first = false;
    warn_once(once_seen_, "depth_limit", "depth_limit", "groups nested deeper than the limit are flattened",
              base_ + tok.offset);
    return;
  }
  top().first = false;
  // \upr holds an ANSI branch and a \*\ud branch with the same content.
  // Its first child group is the ANSI branch and is dropped.
  const bool upr_branch = top().uprPending;
  top().uprPending = false;
  GroupState child = top();
  child.first = true;
  child.star = false;
  child.ownsEntry = false;
  child.ownsSink = false;
  child.owner = Owner::None;
  // Every sub-group of the style sheet is a style; every sub-group of the
  // info group is one entry.
  if (child.dest == Dest::StyleSheet) {
    child.dest = Dest::StyleEntry;
    child.target = TextTarget::Buffer;
    child.ownsEntry = true;
    style_ = Style{};
    buffer_.clear();
  } else if (child.dest == Dest::Info) {
    child.dest = Dest::InfoEntry;
    child.target = TextTarget::Buffer;
    child.ownsEntry = true;
    info_key_.clear();
    buffer_.clear();
  }
  stack_.push_back(std::move(child));
  if (upr_branch) skip_group();
}

void Reader::on_group_close() {
  if (overflow_ > 0) {
    --overflow_;
    return;
  }
  close_group();
}

void Reader::close_group() {
  GroupState& g = top();
  // A font entry ends with the group it started in: its own group, or the
  // font table group for an entry written without braces.
  if (g.dest == Dest::FontTable && font_entry_ && stack_.size() <= font_entry_depth_) finish_font_entry();
  if (g.ownsEntry) {
    switch (g.dest) {
      case Dest::StyleEntry: commit_style(); break;
      case Dest::InfoEntry: commit_info(); break;
      case Dest::Generator: doc_.generator = entry_value(buffer_); break;
      case Dest::ColorTable:
        if (color_has_) push_color();  // a last entry without its ';'
        break;
      default: break;
    }
  }
  if (g.ownsSink && sinks_.size() > 1) {
    close_sink(sinks_.back(), g.para);
    sinks_.pop_back();
  }
  const Owner owner = g.owner;
  if (stack_.size() == 1) finish_document(g);
  stack_.pop_back();
  // The finished frame goes to the parent group's target.
  if (owner != Owner::None && !stack_.empty()) close_frame(owner);
}

void Reader::finish_document(const GroupState& root) {
  while (!sinks_.empty()) {
    close_sink(sinks_.back(), root.para);
    sinks_.pop_back();
  }
  // The section being read at the end of the input is always kept.
  commit_section(sections_.back());
  doc_.sections.assign(std::make_move_iterator(sections_.begin()), std::make_move_iterator(sections_.end()));
  sections_.clear();
  finished_ = true;
}

// The paragraph that a closing container ends carries the properties in
// effect at that moment. Text of a table paragraph goes into its cell, so
// that the row closed by row_not_closed keeps it.
void Reader::close_sink(BlockSink& s, const ParaState& para) {
  const int level = table_level(para);
  if (s.current && level > 0 && !(s.current->runs.empty() && s.current->listText.empty())) {
    s.end_paragraph(para.props, level, doc_.warnings, offset_);
  }
  if (s.current) s.current->props = para.props;
  s.close(doc_.warnings, offset_);
}

// \header, \footer and their f, l and r forms open a sink on the matching
// member of the current section. A second group of the same kind appends.
bool Reader::open_header_footer(std::string_view word) {
  Section& section = sections_.back();
  HeaderSet& set = word.substr(0, 6) == "header" ? section.headers : section.footers;
  const char kind = word.size() > 6 ? word[6] : '\0';
  std::optional<std::vector<Block>>& slot =
      kind == 'f' ? set.first : (kind == 'l' ? set.left : (kind == 'r' ? set.right : set.default_));
  if (!slot) slot.emplace();
  open_sink(&*slot);
  return true;
}

// A destination with its own blocks (header, footer, footnote, shape text,
// object result) starts a new sink with default paragraph and character
// state, as if \pard\plain had been written. Its text does not join the
// table of the paragraph around it. The closing brace restores the
// parent group's state.
void Reader::open_sink(std::vector<Block>* blocks) {
  sinks_.emplace_back(blocks);
  GroupState& g = top();
  g.sink = sinks_.size() - 1;
  g.ownsSink = true;
  g.para = ParaState{};
  g.border = BorderTarget{};
  g.chars = CharProps{};
  g.nestProps = false;
}

void Reader::on_end(const Token& tok) {
  if (tok.clamped) warn("truncated_control", "the input ends with a lone backslash", base_ + tok.offset);
  if (finished_) return;
  flush_pending_surrogate();
  // A missing '}' closes every open group, one warning each.
  std::size_t missing = overflow_ + (skipping_ ? skip_nesting_ : 0);
  overflow_ = 0;
  skip_nesting_ = 0;
  skipping_ = false;
  for (; missing > 0; --missing) warn("unbalanced_group", "missing } at the end of the input", base_ + tok.offset);
  while (!stack_.empty()) {
    warn("unbalanced_group", "missing } at the end of the input", base_ + tok.offset);
    close_group();
  }
}

void Reader::after_document(const Token& tok) {
  if (tok.kind == TokenKind::GroupOpen) {
    ++trailing_;
  } else if (tok.kind == TokenKind::GroupClose) {
    if (trailing_ > 0) {
      --trailing_;
    } else {
      warn("unbalanced_group", "extra } ignored", base_ + tok.offset);
    }
  }
}

// Inside a skipped destination every token is ignored until the '}' that
// matches the group's '{'. This is still one pass over the tokens.
void Reader::skip_token(const Token& tok) {
  if (tok.kind == TokenKind::GroupOpen) {
    ++skip_nesting_;
  } else if (tok.kind == TokenKind::GroupClose) {
    if (skip_nesting_ > 0) {
      --skip_nesting_;
    } else {
      skipping_ = false;
      close_group();
    }
  }
}

void Reader::skip_group() {
  top().dest = Dest::Skip;
  top().target = TextTarget::Discard;
  skipping_ = true;
  skip_nesting_ = 0;
}

void Reader::note_clamped(const Token& tok) {
  if (!tok.clamped) return;
  if (tok.kind == TokenKind::ControlWord || tok.kind == TokenKind::Unicode) {
    std::string name(tok.word);
    warn_once(param_seen_, name, "param_overflow", "parameter of \\" + name + " clamped to 32 bits",
              base_ + tok.offset);
  } else if (tok.kind == TokenKind::Binary) {
    warn_once(once_seen_, "bin_truncated", "bin_truncated", "\\bin is longer than the rest of the input",
              base_ + tok.offset);
  }
}

// ---------------------------------------------------------------------------
// Control words and destinations
// ---------------------------------------------------------------------------

void Reader::on_word(const Token& tok) {
  const WordInfo* info = find_word(tok.word);
  GroupState& g = top();
  if (g.first) {
    const bool star = g.star;
    g.first = false;
    g.star = false;
    if (decide_destination(tok, info, star)) return;
  }
  dispatch_word(tok, info);
}

// The first control word of a group, after an optional \*, decides its
// destination (design, "Group state" and "Destinations the reader
// understands"). Returns true when the word needs no further handling.
bool Reader::decide_destination(const Token& tok, const WordInfo* info, bool star) {
  const GroupState& g = top();
  const bool in_tables = g.dest == Dest::StyleSheet || g.dest == Dest::StyleEntry || g.dest == Dest::Info ||
                         g.dest == Dest::InfoEntry;
  const bool is_dest = info != nullptr && info->family == Family::Destination;
  if (star) {
    // In the style sheet, \*\cs and \*\ts start character and table styles.
    if (g.dest == Dest::StyleEntry && g.ownsEntry && info != nullptr && info->family == Family::Style &&
        (op_of<StyleOp>(*info) == StyleOp::Character || op_of<StyleOp>(*info) == StyleOp::Table)) {
      return false;
    }
    // Other \* groups in the style sheet and the info group are skipped silently.
    if (in_tables) {
      skip_group();
      return true;
    }
    if (is_dest) return open_destination(tok, op_of<DestKind>(*info));
    std::string name(tok.word);
    warn_once(unknown_seen_, name, "unknown_destination", "unknown destination \\" + name + " skipped",
              base_ + tok.offset);
    skip_group();
    return true;
  }
  if (g.dest == Dest::InfoEntry && g.ownsEntry) {
    begin_info_entry(tok);
    return true;
  }
  if (is_dest) return open_destination(tok, op_of<DestKind>(*info));
  // Any other word, known or not, starts a formatting group.
  return false;
}

bool Reader::open_destination(const Token& tok, DestKind kind) {
  GroupState& g = top();
  switch (kind) {
    case DestKind::FontTable:
      g.dest = Dest::FontTable;
      g.target = TextTarget::Buffer;
      buffer_.clear();
      return true;
    case DestKind::ColorTable:
      g.dest = Dest::ColorTable;
      g.target = TextTarget::ColorTable;
      g.ownsEntry = true;
      color_ = Color{};
      color_has_ = false;
      return true;
    case DestKind::StyleSheet:
      g.dest = Dest::StyleSheet;
      g.target = TextTarget::Discard;
      return true;
    case DestKind::Info:
      g.dest = Dest::Info;
      g.target = TextTarget::Discard;
      return true;
    case DestKind::Generator:
      g.dest = Dest::Generator;
      g.target = TextTarget::Buffer;
      g.ownsEntry = true;
      buffer_.clear();
      return true;
    case DestKind::SkipWarn: {
      std::string name(tok.word);
      warn_once(skipped_seen_, name, "skipped_destination", "destination \\" + name + " skipped",
                base_ + tok.offset);
      skip_group();
      return true;
    }
    case DestKind::SkipSilent:
      skip_group();
      return true;
    case DestKind::HeaderFooter: return open_header_footer(tok.word);
    case DestKind::NestTableProps:
      // The table words inside target the nested level (design, "Tables").
      g.nestProps = true;
      return true;
    default: return open_structure(tok, kind);
  }
  return false;
}

// ---------------------------------------------------------------------------
// Fields, footnotes, bookmarks, lists, pictures, shapes and objects
// ---------------------------------------------------------------------------

// Opens one of the text containers or pictures. They belong to the body
// text; inside the document tables they are skipped. A \fldrslt, \result
// or \shptxt found outside its container is read as a formatting group,
// so its text stays in the enclosing paragraph. Other parts found outside
// their container (\*\fldinst, \sn, \sv, \*\objclass) are skipped silently.
bool Reader::open_structure(const Token& tok, DestKind kind) {
  GroupState& g = top();
  if (g.dest != Dest::Body) {
    skip_group();
    return true;
  }
  switch (kind) {
    case DestKind::Transparent: return true;
    case DestKind::Upr:
      g.uprPending = true;
      return true;
    case DestKind::Field:
      fields_.emplace_back();
      g.owner = Owner::Field;
      g.frame = fields_.size() - 1;
      g.target = TextTarget::Field;
      return true;
    case DestKind::FieldInstruction:
      if (g.target != TextTarget::Field) break;
      g.target = TextTarget::FieldInstruction;
      return true;
    case DestKind::FieldResult:
      if (g.target != TextTarget::Field) return false;  // stray: a formatting group, so its text stays
      g.target = TextTarget::FieldResult;
      return true;
    case DestKind::Footnote:
      footnotes_.emplace_back();
      g.owner = Owner::Footnote;
      open_sink(&footnotes_.back().blocks);
      g.target = TextTarget::Paragraph;
      return true;
    case DestKind::BookmarkStart:
    case DestKind::BookmarkEnd:
      g.owner = kind == DestKind::BookmarkStart ? Owner::BookmarkStart : Owner::BookmarkEnd;
      g.target = TextTarget::Name;
      name_.clear();
      return true;
    case DestKind::ListText:
      // The text before a list item goes to the paragraph's listText.
      if (g.target != TextTarget::Paragraph) break;
      sink().begin_paragraph_if_needed();
      g.target = TextTarget::ListText;
      return true;
    case DestKind::Picture:
      pictures_.emplace_back();
      pictures_.back().depth = stack_.size();
      g.owner = Owner::Picture;
      g.frame = pictures_.size() - 1;
      g.target = TextTarget::Picture;
      return true;
    case DestKind::Shape:
      // Only the text and the picture of a shape are kept.
      warn_once(once_seen_, "shape_geometry_dropped", "shape_geometry_dropped",
                "shape position, size and drawing properties are dropped", base_ + tok.offset);
      shapes_.emplace_back();
      g.owner = Owner::Shape;
      g.frame = shapes_.size() - 1;
      g.target = TextTarget::Shape;
      return true;
    case DestKind::ShapeText:
      if (g.target != TextTarget::Shape || g.frame >= shapes_.size()) return false;  // stray: text stays
      open_sink(&shapes_[g.frame].shape.blocks);
      g.target = TextTarget::Paragraph;
      return true;
    case DestKind::ShapeName:
      if (g.target != TextTarget::Shape) break;
      g.owner = Owner::ShapeName;
      g.target = TextTarget::Name;
      name_.clear();
      return true;
    case DestKind::ShapeValue:
      // Only the value of the pib property, the shape's picture, is read.
      if (g.target != TextTarget::Shape || g.frame >= shapes_.size() || shapes_[g.frame].property != "pib") break;
      g.pibValue = true;
      return true;
    case DestKind::Object:
      objects_.emplace_back();
      g.owner = Owner::Object;
      g.frame = objects_.size() - 1;
      g.target = TextTarget::Object;
      return true;
    case DestKind::ObjectClass:
      if (g.target != TextTarget::Object) break;
      g.owner = Owner::ObjectClass;
      g.target = TextTarget::Name;
      name_.clear();
      return true;
    case DestKind::ObjectData:
      warn_once(once_seen_, "object_data_dropped", "object_data_dropped", "embedded object data dropped",
                base_ + tok.offset);
      break;
    case DestKind::ObjectResult:
      if (g.target != TextTarget::Object || g.frame >= objects_.size()) return false;  // stray: text stays
      open_sink(&objects_[g.frame].result);
      g.target = TextTarget::Paragraph;
      return true;
    default: break;
  }
  skip_group();
  return true;
}

// Finishes the frame of a group that has just closed. top() is now the
// parent group, so emit_inline sends the result to the enclosing
// paragraph, field result or list text.
void Reader::close_frame(Owner owner) {
  switch (owner) {
    case Owner::None: return;
    case Owner::Field: {
      if (fields_.empty()) return;
      Field field = std::move(fields_.back());
      fields_.pop_back();
      field.instruction = trim(field.instruction);
      emit_inline(Inline{std::move(field)});
      return;
    }
    case Owner::Footnote: {
      if (footnotes_.empty()) return;
      Footnote note = std::move(footnotes_.back());
      footnotes_.pop_back();
      emit_inline(Inline{std::move(note)});
      return;
    }
    case Owner::Picture: {
      if (pictures_.empty()) return;
      PictureFrame frame = std::move(pictures_.back());
      pictures_.pop_back();
      finish_picture(frame);
      const GroupState& g = top();
      // The value of a pib property is the picture of its shape.
      if (g.pibValue && g.target == TextTarget::Shape && g.frame < shapes_.size()) {
        shapes_[g.frame].shape.picture = std::move(frame.picture);
      } else {
        emit_inline(Inline{std::move(frame.picture)});
      }
      return;
    }
    case Owner::Shape: {
      if (shapes_.empty()) return;
      Shape shape = std::move(shapes_.back().shape);
      shapes_.pop_back();
      if (!shape.blocks.empty() || shape.picture) emit_inline(Inline{std::move(shape)});
      return;
    }
    case Owner::Object: {
      if (objects_.empty()) return;
      EmbeddedObject object = std::move(objects_.back());
      objects_.pop_back();
      emit_inline(Inline{std::move(object)});
      return;
    }
    case Owner::BookmarkStart:
    case Owner::BookmarkEnd:
      emit_inline(Inline{Bookmark{trim(name_), owner == Owner::BookmarkEnd}});
      name_.clear();
      return;
    case Owner::ShapeName: {
      const GroupState& g = top();
      if (g.target == TextTarget::Shape && g.frame < shapes_.size()) shapes_[g.frame].property = trim(name_);
      name_.clear();
      return;
    }
    case Owner::ObjectClass: {
      const GroupState& g = top();
      if (g.target == TextTarget::Object && g.frame < objects_.size()) objects_[g.frame].className = trim(name_);
      name_.clear();
      return;
    }
  }
}

// A picture without a known format word is Unknown. A hex digit left
// without its pair is bad data.
void Reader::finish_picture(PictureFrame& frame) {
  if (!frame.hasFormat) {
    frame.picture.format = PictureFormat::Unknown;
    warn_once(once_seen_, "unknown_picture_format", "unknown_picture_format", "picture of unknown format",
              offset_);
  }
  if (!frame.stopped && frame.half >= 0) {
    warn_once(once_seen_, "bad_picture_data", "bad_picture_data", "picture data ends with half a byte", offset_);
  }
}

void Reader::picture_word(const Token& tok, PictOp op) {
  const GroupState& g = top();
  if (g.target != TextTarget::Picture || g.frame >= pictures_.size()) return;
  PictureFrame& frame = pictures_[g.frame];
  Picture& p = frame.picture;
  auto format = [&](PictureFormat f) {
    p.format = f;
    frame.hasFormat = true;
  };
  auto size = [&](std::optional<int>& slot) {
    if (tok.hasParam) slot = tok.param;
  };
  switch (op) {
    case PictOp::Png: format(PictureFormat::Png); break;
    case PictOp::Jpeg: format(PictureFormat::Jpeg); break;
    case PictOp::Emf: format(PictureFormat::Emf); break;
    case PictOp::Wmf: format(PictureFormat::Wmf); break;
    case PictOp::Dib: format(PictureFormat::Dib); break;
    case PictOp::Bmp: format(PictureFormat::Bmp); break;
    case PictOp::Mac:
      // A Macintosh PICT has no format in the model.
      p.format = PictureFormat::Unknown;
      frame.hasFormat = false;
      break;
    case PictOp::Width: size(p.width); break;
    case PictOp::Height: size(p.height); break;
    case PictOp::GoalWidth: size(p.goalWidth); break;
    case PictOp::GoalHeight: size(p.goalHeight); break;
    case PictOp::ScaleX: size(p.scaleX); break;
    case PictOp::ScaleY: size(p.scaleY); break;
  }
}

// The picture's text is hex digits. Line breaks, spaces and tabs between
// them are ignored, and a pair may span two text tokens. A byte that is not
// a hex digit ends the data; the bytes before it are kept. Text in a child
// group of the picture is not data.
void Reader::picture_text(std::string_view text, std::size_t offset) {
  const GroupState& g = top();
  if (g.frame >= pictures_.size()) return;
  PictureFrame& frame = pictures_[g.frame];
  if (stack_.size() != frame.depth || frame.stopped) return;
  for (char c : text) {
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    int digit = -1;
    if (c >= '0' && c <= '9') digit = c - '0';
    if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    if (digit < 0) {
      frame.stopped = true;
      warn_once(once_seen_, "bad_picture_data", "bad_picture_data", "picture data holds a byte that is not hex",
                base_ + offset);
      return;
    }
    if (frame.half < 0) {
      frame.half = digit;
    } else {
      frame.picture.data.push_back(static_cast<char>((frame.half << 4) | digit));
      frame.half = -1;
    }
  }
}

// \binN in a picture gives N raw bytes of data.
void Reader::picture_binary(std::string_view bytes) {
  const GroupState& g = top();
  if (g.frame >= pictures_.size()) return;
  PictureFrame& frame = pictures_[g.frame];
  if (stack_.size() != frame.depth || frame.stopped) return;
  frame.picture.data.append(bytes.data(), bytes.size());
}

// \ftnalt marks the innermost footnote as an endnote. \flddirty and
// \fldlock set the flags of the field whose group they are in.
void Reader::frame_word(FrameOp op) {
  const GroupState& g = top();
  switch (op) {
    case FrameOp::FootnoteAlt:
      if (!footnotes_.empty()) footnotes_.back().endnote = true;
      return;
    case FrameOp::FieldDirty:
    case FrameOp::FieldLock:
      if (g.target != TextTarget::Field || g.frame >= fields_.size()) return;
      if (op == FrameOp::FieldDirty) {
        fields_[g.frame].dirty = true;
      } else {
        fields_[g.frame].locked = true;
      }
      return;
  }
}

// A field result and list text hold inlines, not paragraphs. A paragraph,
// cell or row break there becomes a line break, so the lines of a TOC
// result stay apart. Other targets drop the break.
void Reader::flatten_break() {
  const TextTarget t = top().target;
  if (t != TextTarget::FieldResult && t != TextTarget::ListText) return;
  emit_inline(Inline{LineBreak{}});
  warn_once(once_seen_, "field_result_flattened", "field_result_flattened",
            "paragraph or cell breaks inside a field result or list text became line breaks", offset_);
}

// ---------------------------------------------------------------------------
// Sections (design, "Sections and page setup")
// ---------------------------------------------------------------------------

void Reader::section_word(const Token& tok, SectionOp op) {
  PageSetup& page = section_.page;
  switch (op) {
    case SectionOp::Sect: end_section(); return;
    case SectionOp::Sectd:
      // \sectd resets the section's properties to the document's page
      // setup. It does not start a section and keeps the headers read.
      section_.reset(doc_.page);
      break;
    case SectionOp::PageW: page.width = param_or(tok, page.width); break;
    case SectionOp::PageH: page.height = param_or(tok, page.height); break;
    case SectionOp::MargL: page.marginLeft = param_or(tok, page.marginLeft); break;
    case SectionOp::MargR: page.marginRight = param_or(tok, page.marginRight); break;
    case SectionOp::MargT: page.marginTop = param_or(tok, page.marginTop); break;
    case SectionOp::MargB: page.marginBottom = param_or(tok, page.marginBottom); break;
    case SectionOp::Landscape: page.landscape = true; break;  // a flag only; the sizes stay as written
    case SectionOp::HeaderY: page.headerDistance = param_or(tok, page.headerDistance); break;
    case SectionOp::FooterY: page.footerDistance = param_or(tok, page.footerDistance); break;
    case SectionOp::BreakNone: section_.break_ = SectionBreak::None; break;
    case SectionOp::BreakColumn: section_.break_ = SectionBreak::Column; break;
    case SectionOp::BreakPage: section_.break_ = SectionBreak::Page; break;
    case SectionOp::BreakEven: section_.break_ = SectionBreak::Even; break;
    case SectionOp::BreakOdd: section_.break_ = SectionBreak::Odd; break;
    case SectionOp::Columns: section_.columns = param_or(tok, 1); break;
    case SectionOp::TitlePage: section_.titlePage = toggle(tok); break;
    case SectionOp::PageNumberStart: if (tok.hasParam) section_.pageNumberStart = tok.param; break;
    case SectionOp::PageNumberRestart: section_.pageNumberRestart = true; break;
  }
  // From now on a change of the document page does not reset the section.
  section_defined_ = true;
}

// \sect ends the section: the open paragraph and tables end, and the next
// section starts with the same page, break and columns and no headers.
// Inside a header, footer, footnote, shape text or object result it acts
// as \par.
void Reader::end_section() {
  if (top().sink != 0 || top().target != TextTarget::Paragraph) {
    special_word(SpecialOp::Par);
    return;
  }
  BlockSink& body = sinks_[0];
  close_sink(body, top().para);
  commit_section(sections_.back());
  sections_.emplace_back();
  body.target = &sections_.back().body;
  section_.titlePage = false;
  section_.pageNumberStart.reset();
  section_.pageNumberRestart = false;
}

void Reader::commit_section(Section& s) {
  s.page = section_.page;
  s.break_ = section_.break_;
  s.columns = section_.columns;
  s.titlePage = section_.titlePage;
  s.pageNumberStart.reset();
  if (section_.pageNumberRestart) s.pageNumberStart = section_.pageNumberStart;
}

void Reader::dispatch_word(const Token& tok, const WordInfo* info) {
  // \ansi, \deff, \paperw, \uc and the other document words apply anywhere.
  if (info != nullptr && info->family == Family::Document) {
    document_word(tok, op_of<DocOp>(*info));
    return;
  }
  switch (top().dest) {
    case Dest::Body: body_word(tok, info); break;
    case Dest::FontTable: font_table_word(tok, info); break;
    case Dest::ColorTable: color_table_word(tok, info); break;
    case Dest::StyleEntry: style_word(tok, info); break;
    case Dest::InfoEntry: info_word(tok, info); break;
    case Dest::Generator:
      if (info != nullptr && info->family == Family::Special) special_word(op_of<SpecialOp>(*info));
      break;
    case Dest::StyleSheet:
    case Dest::Info:
    case Dest::Skip: break;
  }
}

void Reader::body_word(const Token& tok, const WordInfo* info) {
  if (info == nullptr) {
    // Any \brdr word the reader does not name is a border of style Other.
    if (tok.word.size() > 4 && tok.word.substr(0, 4) == "brdr") border_style(BorderStyle::Other);
    return;  // unknown words are ignored, as the specification requires
  }
  switch (info->family) {
    case Family::Char: char_word(tok, op_of<CharOp>(*info)); break;
    case Family::Para: para_word(tok, op_of<ParaOp>(*info)); break;
    case Family::Border: border_word(tok, op_of<BorderOp>(*info)); break;
    case Family::Special: special_word(op_of<SpecialOp>(*info)); break;
    case Family::Section: section_word(tok, op_of<SectionOp>(*info)); break;
    case Family::Picture: picture_word(tok, op_of<PictOp>(*info)); break;
    case Family::Frame: frame_word(op_of<FrameOp>(*info)); break;
    case Family::Table: {
      // Cells and rows hold paragraphs. In a field result or list text a
      // cell or row end becomes a line break; elsewhere outside a
      // paragraph it is dropped.
      const auto op = op_of<TableOp>(*info);
      if (top().target != TextTarget::Paragraph) {
        if (op != TableOp::Trowd && op != TableOp::Cellx) flatten_break();
        break;
      }
      if (op == TableOp::Trowd) {
        start_row_definition();
      } else if (op == TableOp::Cellx) {
        end_cell_definition(tok);
      } else {
        end_cell_or_row(op);
      }
      break;
    }
    case Family::Row: row_word(tok, op_of<RowOp>(*info)); break;
    case Family::CellDef: cell_word(tok, op_of<CellOp>(*info)); break;
    default: break;  // destination words not at a group start
  }
}

void Reader::document_word(const Token& tok, DocOp op) {
  PageSetup& page = doc_.page;
  switch (op) {
    case DocOp::Rtf: return;  // the version is not checked
    case DocOp::Ansi: if (!ansicpg_seen_) doc_.codePage = 1252; return;
    case DocOp::Mac: if (!ansicpg_seen_) doc_.codePage = 10000; return;
    case DocOp::Pc: if (!ansicpg_seen_) doc_.codePage = 437; return;
    case DocOp::Pca: if (!ansicpg_seen_) doc_.codePage = 850; return;
    case DocOp::AnsiCpg:
      // \ansicpgN names the code page exactly and wins over \ansi and \mac.
      if (tok.hasParam) {
        doc_.codePage = tok.param;
        ansicpg_seen_ = true;
      }
      return;
    case DocOp::Deff: doc_.defaultFont = param_or(tok, 0); return;
    case DocOp::DefLang: if (tok.hasParam) doc_.defaultLanguage = tok.param; return;
    case DocOp::Uc: top().uc = std::max(0, param_or(tok, 1)); return;
    case DocOp::PaperW: page.width = param_or(tok, page.width); break;
    case DocOp::PaperH: page.height = param_or(tok, page.height); break;
    case DocOp::MargL: page.marginLeft = param_or(tok, page.marginLeft); break;
    case DocOp::MargR: page.marginRight = param_or(tok, page.marginRight); break;
    case DocOp::MargT: page.marginTop = param_or(tok, page.marginTop); break;
    case DocOp::MargB: page.marginBottom = param_or(tok, page.marginBottom); break;
    case DocOp::Landscape: page.landscape = true; break;
    case DocOp::FacingP: page.facingPages = true; break;
  }
  page_changed();
}

// Until a \sectd defines the section, its page setup follows the document's.
void Reader::page_changed() {
  if (!section_defined_) section_.reset(doc_.page);
}

void Reader::char_word(const Token& tok, CharOp op) {
  CharProps& c = top().chars;
  // \ulN with N not 0 is single; every underline word with 0 clears it.
  auto underline = [&](Underline kind) {
    if (tok.hasParam && tok.param == 0) {
      c.underline.reset();
    } else {
      c.underline = kind;
    }
  };
  switch (op) {
    case CharOp::Font: if (tok.hasParam) c.font = tok.param; break;
    case CharOp::Size: c.size = param_or(tok, 24); break;
    case CharOp::Bold: c.bold = toggle(tok); break;
    case CharOp::Italic: c.italic = toggle(tok); break;
    case CharOp::Underline: underline(Underline::Single); break;
    case CharOp::UlNone: c.underline.reset(); break;
    case CharOp::UlDotted: underline(Underline::Dotted); break;
    case CharOp::UlDouble: underline(Underline::Double); break;
    case CharOp::UlWord: underline(Underline::Word); break;
    case CharOp::UlThick: underline(Underline::Thick); break;
    case CharOp::UlWave: underline(Underline::Wave); break;
    case CharOp::UlDash: underline(Underline::Dash); break;
    case CharOp::Strike: c.strike = toggle(tok); break;
    case CharOp::DoubleStrike: c.doubleStrike = toggle(tok); break;
    case CharOp::Super: c.script = Script::Super; break;
    case CharOp::Sub: c.script = Script::Sub; break;
    case CharOp::NoSuperSub: c.script.reset(); break;
    case CharOp::Up:
    case CharOp::Down: {
      // \upN raises by N half-points, \dnN lowers; the default N is 6.
      const int n = param_or(tok, 6);
      if (n == 0) {
        c.offset.reset();
      } else {
        c.offset = op == CharOp::Up ? n : -n;
      }
      break;
    }
    case CharOp::Color: if (tok.hasParam) c.color = tok.param; break;
    case CharOp::Background: if (tok.hasParam) c.background = tok.param; break;
    case CharOp::Highlight: if (tok.hasParam) c.highlight = tok.param; break;
    case CharOp::Caps: c.caps = toggle(tok); break;
    case CharOp::SmallCaps: c.smallCaps = toggle(tok); break;
    case CharOp::Hidden: c.hidden = toggle(tok); break;
    case CharOp::Outline: c.outline = toggle(tok); break;
    case CharOp::Shadow: c.shadow = toggle(tok); break;
    case CharOp::Lang: if (tok.hasParam) c.language = tok.param; break;
    case CharOp::Deleted: c.deleted = toggle(tok); break;
    case CharOp::Revised: c.inserted = toggle(tok); break;
    case CharOp::Plain: c = CharProps{}; break;
  }
}

void Reader::para_word(const Token& tok, ParaOp op) {
  GroupState& g = top();
  ParagraphProps& p = g.para.props;
  // A tab stop: \tqc, \tqr, \tqdec and the leader words come before \txN.
  auto add_tab = [&](TabAlign align) {
    const TabStop stop{param_or(tok, 0), align, g.para.pendingTabLeader};
    g.para.pendingTabAlign = TabAlign::Left;
    g.para.pendingTabLeader = TabLeader::None;
    // A stop at a position already listed replaces it. Stops beyond
    // kMaxTabStops are dropped without a warning.
    for (TabStop& t : p.tabs) {
      if (t.position == stop.position) {
        t = stop;
        return;
      }
    }
    if (p.tabs.size() < kMaxTabStops) p.tabs.push_back(stop);
  };
  auto shading = [&]() -> Shading& {
    if (!p.shading) p.shading.emplace();
    return *p.shading;
  };
  switch (op) {
    case ParaOp::Pard:
      g.para = ParaState{};
      g.border = BorderTarget{};
      break;
    case ParaOp::Ql: p.align = Align::Left; break;
    case ParaOp::Qc: p.align = Align::Center; break;
    case ParaOp::Qr: p.align = Align::Right; break;
    case ParaOp::Qj: p.align = Align::Justify; break;
    case ParaOp::Li: p.indentLeft = param_or(tok, 0); break;
    case ParaOp::Ri: p.indentRight = param_or(tok, 0); break;
    case ParaOp::Fi: p.indentFirst = param_or(tok, 0); break;
    case ParaOp::Sb: p.spaceBefore = param_or(tok, 0); break;
    case ParaOp::Sa: p.spaceAfter = param_or(tok, 0); break;
    case ParaOp::Sl: p.lineSpacing = param_or(tok, 0); break;
    case ParaOp::SlMult: p.lineSpacingMultiple = toggle(tok); break;
    case ParaOp::Keep: p.keepTogether = toggle(tok); break;
    case ParaOp::KeepN: p.keepWithNext = toggle(tok); break;
    case ParaOp::PageBB: p.pageBreakBefore = toggle(tok); break;
    case ParaOp::WidCtl: p.widowControl = true; break;
    case ParaOp::NoWidCtl: p.widowControl = false; break;
    case ParaOp::Style: p.style = param_or(tok, 0); break;
    case ParaOp::Ls:
      if (!p.list) p.list.emplace();
      p.list->id = param_or(tok, 0);
      break;
    case ParaOp::Ilvl:
      if (!p.list) p.list.emplace();
      p.list->level = param_or(tok, 0);
      break;
    case ParaOp::RtlPar: p.rtl = true; break;
    case ParaOp::LtrPar: p.rtl = false; break;
    case ParaOp::InTbl: g.para.inTable = true; break;
    case ParaOp::Itap: g.para.itap = param_or(tok, 0); break;
    case ParaOp::Tx: add_tab(g.para.pendingTabAlign); break;
    case ParaOp::Tb:
      // \tbN is a bar tab at N; written without N it marks the next \tx.
      if (tok.hasParam) {
        add_tab(TabAlign::Bar);
      } else {
        g.para.pendingTabAlign = TabAlign::Bar;
      }
      break;
    case ParaOp::Tqc: g.para.pendingTabAlign = TabAlign::Center; break;
    case ParaOp::Tqr: g.para.pendingTabAlign = TabAlign::Right; break;
    case ParaOp::Tqdec: g.para.pendingTabAlign = TabAlign::Decimal; break;
    case ParaOp::TlDot: g.para.pendingTabLeader = TabLeader::Dots; break;
    case ParaOp::TlHyph: g.para.pendingTabLeader = TabLeader::Hyphens; break;
    case ParaOp::TlUl: g.para.pendingTabLeader = TabLeader::Underline; break;
    case ParaOp::TlTh: g.para.pendingTabLeader = TabLeader::Thick; break;
    case ParaOp::TlEq: g.para.pendingTabLeader = TabLeader::Equal; break;
    case ParaOp::Shading: shading().percent = param_or(tok, 0); break;
    case ParaOp::CbPat: shading().background = param_or(tok, 0); break;
    case ParaOp::CfPat: shading().foreground = param_or(tok, 0); break;
  }
}

void Reader::border_word(const Token& tok, BorderOp op) {
  auto open = [&](BorderTarget::Side side) { open_border(BorderTarget::Kind::Paragraph, side); };
  auto set = [&](auto&& apply) {
    Border* slots[4] = {nullptr, nullptr, nullptr, nullptr};
    const int n = border_slots(slots);
    for (int i = 0; i < n; ++i) apply(*slots[i]);
  };
  switch (op) {
    case BorderOp::OpenTop: open(BorderTarget::Side::Top); break;
    case BorderOp::OpenLeft: open(BorderTarget::Side::Left); break;
    case BorderOp::OpenBottom: open(BorderTarget::Side::Bottom); break;
    case BorderOp::OpenRight: open(BorderTarget::Side::Right); break;
    case BorderOp::OpenBetween: open(BorderTarget::Side::Between); break;
    case BorderOp::OpenBar: open(BorderTarget::Side::Bar); break;
    case BorderOp::OpenBox: open(BorderTarget::Side::Box); break;
    case BorderOp::Single: border_style(BorderStyle::Single); break;
    case BorderOp::Double: border_style(BorderStyle::Double); break;
    case BorderOp::Dotted: border_style(BorderStyle::Dotted); break;
    case BorderOp::Dashed: border_style(BorderStyle::Dashed); break;
    case BorderOp::Thick: border_style(BorderStyle::Thick); break;
    case BorderOp::Hairline: border_style(BorderStyle::Hairline); break;
    case BorderOp::Triple: border_style(BorderStyle::Triple); break;
    case BorderOp::None: border_style(BorderStyle::None); break;
    case BorderOp::Width: set([&](Border& b) { b.width = param_or(tok, 0); }); break;
    case BorderOp::Color: set([&](Border& b) { b.color = param_or(tok, 0); }); break;
    case BorderOp::Space: set([&](Border& b) { b.space = param_or(tok, 0); }); break;
  }
}

// A border-opening word makes its side the target of the following
// \brdr* words and starts that border as a single line of width 0.
void Reader::open_border(BorderTarget::Kind kind, BorderTarget::Side side) {
  GroupState& g = top();
  g.border = BorderTarget{kind, side};
  if (kind == BorderTarget::Kind::Paragraph && !g.para.props.borders) g.para.props.borders.emplace();
  Border* slots[4] = {nullptr, nullptr, nullptr, nullptr};
  const int n = border_slots(slots);
  for (int i = 0; i < n; ++i) *slots[i] = Border{};
}

void Reader::border_style(BorderStyle style) {
  Border* slots[4] = {nullptr, nullptr, nullptr, nullptr};
  const int n = border_slots(slots);
  for (int i = 0; i < n; ++i) slots[i]->style = style;
}

// Returns the borders the current target names. \box names all four sides.
// The border between paragraphs is stored as `horizontal`; the bar border
// has no place in the model and is dropped. A cell border belongs to the
// cell definition that the next \cellx closes; a row border to the row
// definition.
int Reader::border_slots(Border* out[4]) {
  GroupState& g = top();
  std::optional<Borders>* target = nullptr;
  switch (g.border.kind) {
    case BorderTarget::Kind::None: return 0;
    case BorderTarget::Kind::Paragraph:
      if (!g.para.props.borders) return 0;
      target = &g.para.props.borders;
      break;
    case BorderTarget::Kind::Cell: target = &sink().table(definition_level()).nextDef.borders; break;
    case BorderTarget::Kind::Row: target = &sink().table(definition_level()).rowProps.borders; break;
  }
  if (!*target) target->emplace();
  Borders& b = **target;
  auto slot = [](std::optional<Border>& o) -> Border* {
    if (!o) o.emplace();
    return &*o;
  };
  switch (g.border.side) {
    case BorderTarget::Side::Top: out[0] = slot(b.top); return 1;
    case BorderTarget::Side::Left: out[0] = slot(b.left); return 1;
    case BorderTarget::Side::Bottom: out[0] = slot(b.bottom); return 1;
    case BorderTarget::Side::Right: out[0] = slot(b.right); return 1;
    case BorderTarget::Side::Horizontal:
    case BorderTarget::Side::Between: out[0] = slot(b.horizontal); return 1;
    case BorderTarget::Side::Vertical: out[0] = slot(b.vertical); return 1;
    case BorderTarget::Side::Box:
      out[0] = slot(b.top);
      out[1] = slot(b.left);
      out[2] = slot(b.bottom);
      out[3] = slot(b.right);
      return 4;
    case BorderTarget::Side::Bar: return 0;
  }
  return 0;
}

// Breaks, tabs, \chpgn and the special characters of the design's table.
void Reader::special_word(SpecialOp op) {
  const bool in_paragraph = top().target == TextTarget::Paragraph;
  const bool inlines = takes_inlines(top().target);
  switch (op) {
    case SpecialOp::Par:
      if (in_paragraph) {
        sink().end_paragraph(top().para.props, table_level(top().para), doc_.warnings, offset_);
      } else {
        flatten_break();
      }
      return;
    case SpecialOp::Line: emit_inline(Inline{LineBreak{}}); return;
    case SpecialOp::Tab:
      if (inlines) {
        emit_inline(Inline{Tab{}});
      } else {
        emit("\t");
      }
      return;
    case SpecialOp::Page: emit_inline(Inline{PageBreak{}}); return;
    case SpecialOp::Column: emit_inline(Inline{ColumnBreak{}}); return;
    case SpecialOp::Chpgn: emit_inline(Inline{Field{"PAGE", {}, false, false}}); return;
    case SpecialOp::Chftn: return;  // the footnote mark; the footnote itself carries the content
    case SpecialOp::Lbr: return;    // the kind of line break; \line already made it
    case SpecialOp::Emdash: emit_code_point(0x2014); return;
    case SpecialOp::Endash: emit_code_point(0x2013); return;
    case SpecialOp::Bullet: emit_code_point(0x2022); return;
    case SpecialOp::Lquote: emit_code_point(0x2018); return;
    case SpecialOp::Rquote: emit_code_point(0x2019); return;
    case SpecialOp::Ldblquote: emit_code_point(0x201C); return;
    case SpecialOp::Rdblquote: emit_code_point(0x201D); return;
    case SpecialOp::Emspace: emit_code_point(0x2003); return;
    case SpecialOp::Enspace: emit_code_point(0x2002); return;
    case SpecialOp::Qmspace: emit_code_point(0x2005); return;
    case SpecialOp::Zwj: emit_code_point(0x200D); return;
    case SpecialOp::Zwnj: emit_code_point(0x200C); return;
  }
}

// ---------------------------------------------------------------------------
// Tables (design, "Tables")
// ---------------------------------------------------------------------------

// The paragraph's table level, capped at kMaxTableNesting and at the group
// depth limit, so that a huge \itapN cannot make the reader build a table
// per unit.
int Reader::table_level(const ParaState& p) const {
  const int cap = static_cast<int>(std::min<std::size_t>(max_depth_, static_cast<std::size_t>(kMaxTableNesting)));
  return std::min(paragraph_level(p), cap);
}

// \trowd, \cellx, \cl*, \tr* and \row work on level 1. Inside
// \*\nesttableprops they work on the paragraph's level, at least 2.
int Reader::definition_level() { return top().nestProps ? nested_level() : 1; }

// \nestcell and \nestrow close a cell or row of the paragraph's level, at least 2.
int Reader::nested_level() { return std::max(2, table_level(top().para)); }

// \trowd resets the row and cell definitions. Cells already closed keep
// their text; they take the definitions in effect at \row.
void Reader::start_row_definition() {
  top().border = BorderTarget{};
  sink().table(definition_level()).reset_definition();
}

// \cellxN closes one cell definition. A cell open at this level is not
// touched: this is Word's second copy of the row definition.
void Reader::end_cell_definition(const Token& tok) {
  top().border = BorderTarget{};
  sink().table(definition_level()).define_cell(param_or(tok, 0));
}

// \cell closes a level-1 cell; \nestcell a nested one. \row and \nestrow
// close the row the same way.
void Reader::end_cell_or_row(TableOp op) {
  GroupState& g = top();
  g.border = BorderTarget{};
  const ParagraphProps& props = g.para.props;
  switch (op) {
    case TableOp::Cell: sink().end_cell(props, 1, doc_.warnings, offset_); break;
    case TableOp::NestCell: sink().end_cell(props, nested_level(), doc_.warnings, offset_); break;
    case TableOp::Row: sink().end_row(props, definition_level(), doc_.warnings, offset_); break;
    case TableOp::NestRow: sink().end_row(props, nested_level(), doc_.warnings, offset_); break;
    case TableOp::Trowd:
    case TableOp::Cellx: break;  // handled by their own functions
  }
}

void Reader::row_word(const Token& tok, RowOp op) {
  RowProps& r = sink().table(definition_level()).rowProps;
  const int n = param_or(tok, 0);
  auto padding = [&]() -> Padding& {
    if (!r.padding) r.padding.emplace();
    return *r.padding;
  };
  using Side = BorderTarget::Side;
  switch (op) {
    case RowOp::Header: r.header = toggle(tok); break;
    case RowOp::Keep: r.keep = toggle(tok); break;
    case RowOp::AlignLeft: r.align = Align::Left; break;
    case RowOp::AlignCenter: r.align = Align::Center; break;
    case RowOp::AlignRight: r.align = Align::Right; break;
    case RowOp::Left: r.left = n; break;
    case RowOp::GapHalf: r.gapHalf = n; break;
    case RowOp::Height:
      // \trrhN: N above 0 is a minimum height, N below 0 an exact one.
      if (n > 0) {
        r.height = n;
        r.heightRule = HeightRule::AtLeast;
      } else if (n < 0) {
        r.height = n == std::numeric_limits<int>::min() ? std::numeric_limits<int>::max() : -n;
        r.heightRule = HeightRule::Exact;
      } else {
        r.height.reset();
        r.heightRule.reset();
      }
      break;
    case RowOp::PadLeft: padding().left = n; break;
    case RowOp::PadTop: padding().top = n; break;
    case RowOp::PadBottom: padding().bottom = n; break;
    case RowOp::PadRight: padding().right = n; break;
    case RowOp::BorderTop: open_border(BorderTarget::Kind::Row, Side::Top); break;
    case RowOp::BorderLeft: open_border(BorderTarget::Kind::Row, Side::Left); break;
    case RowOp::BorderBottom: open_border(BorderTarget::Kind::Row, Side::Bottom); break;
    case RowOp::BorderRight: open_border(BorderTarget::Kind::Row, Side::Right); break;
    case RowOp::BorderHorizontal: open_border(BorderTarget::Kind::Row, Side::Horizontal); break;
    case RowOp::BorderVertical: open_border(BorderTarget::Kind::Row, Side::Vertical); break;
    case RowOp::Rtl: r.rtl = true; break;
  }
}

// The \cl* words describe the cell definition that the next \cellx closes.
void Reader::cell_word(const Token& tok, CellOp op) {
  TableLevel& t = sink().table(definition_level());
  Cell& c = t.nextDef;
  const int n = param_or(tok, 0);
  auto padding = [&]() -> Padding& {
    if (!c.padding) c.padding.emplace();
    return *c.padding;
  };
  auto shading = [&]() -> Shading& {
    if (!c.shading) c.shading.emplace();
    return *c.shading;
  };
  using Side = BorderTarget::Side;
  switch (op) {
    case CellOp::VAlignTop: c.verticalAlign = VerticalAlign::Top; break;
    case CellOp::VAlignCenter: c.verticalAlign = VerticalAlign::Center; break;
    case CellOp::VAlignBottom: c.verticalAlign = VerticalAlign::Bottom; break;
    case CellOp::TextLrtb: c.textDirection = TextDirection::Lrtb; break;
    case CellOp::TextTbrl: c.textDirection = TextDirection::Tbrl; break;
    case CellOp::TextBtlr: c.textDirection = TextDirection::Btlr; break;
    case CellOp::TextLrtbv: c.textDirection = TextDirection::Lrtbv; break;
    case CellOp::TextTbrlv: c.textDirection = TextDirection::Tbrlv; break;
    case CellOp::NoWrap: c.noWrap = toggle(tok); break;
    case CellOp::MergeFirst: c.mergeHorizontal = Merge::First; break;
    case CellOp::MergeNext: c.mergeHorizontal = Merge::Continued; break;
    case CellOp::VMergeFirst: c.mergeVertical = Merge::First; break;
    case CellOp::VMergeNext: c.mergeVertical = Merge::Continued; break;
    case CellOp::Background: shading().background = n; break;
    case CellOp::Foreground: shading().foreground = n; break;
    case CellOp::Percent: shading().percent = n; break;
    // Word and SAS write \clpadt for the left padding and \clpadl for the
    // top one. The reader follows the producers, not the names.
    case CellOp::PadT: padding().left = n; break;
    case CellOp::PadL: padding().top = n; break;
    case CellOp::PadB: padding().bottom = n; break;
    case CellOp::PadR: padding().right = n; break;
    case CellOp::Width: t.nextWidth = n; break;
    case CellOp::WidthType: t.nextWidthType = n; break;
    case CellOp::BorderTop: open_border(BorderTarget::Kind::Cell, Side::Top); return;
    case CellOp::BorderLeft: open_border(BorderTarget::Kind::Cell, Side::Left); return;
    case CellOp::BorderBottom: open_border(BorderTarget::Kind::Cell, Side::Bottom); return;
    case CellOp::BorderRight: open_border(BorderTarget::Kind::Cell, Side::Right); return;
  }
  // Any other cell word ends the border definition.
  top().border = BorderTarget{};
}

void Reader::on_symbol(const Token& tok) {
  if (tok.symbol == '*') {
    top().star = true;  // only the group's first token can make it a destination
    return;
  }
  top().first = false;
  switch (tok.symbol) {
    case '{': emit("{"); break;
    case '}': emit("}"); break;
    case '\\': emit("\\"); break;
    case '~': emit_code_point(0x00A0); break;  // non-breaking space
    case '_': emit_code_point(0x2011); break;  // non-breaking hyphen
    case '-': emit_code_point(0x00AD); break;  // optional hyphen
    default: break;                            // \: \| and unknown symbols
  }
}

// ---------------------------------------------------------------------------
// The document tables
// ---------------------------------------------------------------------------

// \fonttbl: entries in sub-groups or written bare, each started by \fN.
void Reader::font_table_word(const Token& tok, const WordInfo* info) {
  if (info == nullptr) return;  // \flomajor and the other theme words
  if (info->family == Family::Char && op_of<CharOp>(*info) == CharOp::Font) {
    begin_font_entry(param_or(tok, 0));
    return;
  }
  if (info->family == Family::Special) {
    special_word(op_of<SpecialOp>(*info));
    return;
  }
  if (info->family != Family::Font || !font_entry_) return;
  Font& f = doc_.fonts[*font_entry_];
  switch (op_of<FontOp>(*info)) {
    case FontOp::Nil: f.family = FontFamily::Nil; break;
    case FontOp::Roman: f.family = FontFamily::Roman; break;
    case FontOp::Swiss: f.family = FontFamily::Swiss; break;
    case FontOp::Modern: f.family = FontFamily::Modern; break;
    case FontOp::Script: f.family = FontFamily::Script; break;
    case FontOp::Decor: f.family = FontFamily::Decor; break;
    case FontOp::Tech: f.family = FontFamily::Tech; break;
    case FontOp::Bidi: f.family = FontFamily::Bidi; break;
    case FontOp::Charset: f.charset = param_or(tok, 0); break;
    case FontOp::Pitch: {
      const int n = param_or(tok, 0);
      f.pitch = n == 1 ? FontPitch::Fixed : (n == 2 ? FontPitch::Variable : FontPitch::Default);
      break;
    }
    case FontOp::CodePage: if (tok.hasParam) f.codePage = tok.param; break;
  }
}

void Reader::begin_font_entry(int number) {
  finish_font_entry();
  doc_.fonts[number] = Font{};
  font_entry_ = number;
  font_entry_depth_ = stack_.size();
  buffer_.clear();
}

void Reader::finish_font_entry() {
  if (!font_entry_) return;
  doc_.fonts[*font_entry_].name = entry_name(buffer_);
  font_entry_.reset();
  buffer_.clear();
}

// \colortbl: entries end at ';'; an entry without \red \green \blue is
// the automatic colour. Theme words such as \ctint are ignored.
void Reader::color_table_word(const Token& tok, const WordInfo* info) {
  if (info == nullptr || info->family != Family::Color) return;
  const int v = param_or(tok, 0);
  switch (op_of<ColorOp>(*info)) {
    case ColorOp::Red: color_.r = v; break;
    case ColorOp::Green: color_.g = v; break;
    case ColorOp::Blue: color_.b = v; break;
  }
  color_has_ = true;
}

void Reader::push_color() {
  if (color_has_) {
    doc_.colors.emplace_back(color_);
  } else {
    doc_.colors.emplace_back(std::nullopt);
  }
  color_ = Color{};
  color_has_ = false;
}

// \stylesheet: \sN paragraph, \*\csN character, \*\tsN table, \dsN section;
// an entry with none of them is paragraph style 0.
void Reader::style_word(const Token& tok, const WordInfo* info) {
  if (info == nullptr) return;
  const int n = param_or(tok, 0);
  if (info->family == Family::Para && op_of<ParaOp>(*info) == ParaOp::Style) {
    style_.type = StyleType::Paragraph;
    style_.index = n;
    return;
  }
  if (info->family == Family::Special) {
    special_word(op_of<SpecialOp>(*info));
    return;
  }
  if (info->family != Family::Style) return;
  switch (op_of<StyleOp>(*info)) {
    case StyleOp::Character: style_.type = StyleType::Character; style_.index = n; break;
    case StyleOp::Table: style_.type = StyleType::Table; style_.index = n; break;
    case StyleOp::Section: style_.type = StyleType::Section; style_.index = n; break;
    case StyleOp::BasedOn: style_.basedOn = n; break;
    case StyleOp::Next: style_.next = n; break;
  }
}

void Reader::commit_style() {
  style_.name = entry_name(buffer_);
  doc_.styles.push_back(std::move(style_));
  style_ = Style{};
  buffer_.clear();
}

// \info: the first word of each sub-group names the entry.
void Reader::begin_info_entry(const Token& tok) {
  info_key_.assign(tok.word.data(), tok.word.size());
  info_param_.reset();
  if (tok.hasParam) info_param_ = tok.param;
  info_time_ = info_key_ == "creatim" || info_key_ == "revtim" || info_key_ == "printim" || info_key_ == "buptim";
  info_has_year_ = false;
  for (int& part : info_parts_) part = 0;
  buffer_.clear();
}

void Reader::info_word(const Token& tok, const WordInfo* info) {
  if (info == nullptr) return;
  if (info->family == Family::Special) {
    special_word(op_of<SpecialOp>(*info));
    return;
  }
  if (info->family != Family::Info || !info_time_) return;
  const auto op = op_of<InfoOp>(*info);
  info_parts_[static_cast<int>(op)] = param_or(tok, 0);
  if (op == InfoOp::Year) info_has_year_ = true;
}

void Reader::commit_info() {
  if (info_key_.empty()) return;
  if (info_time_) {
    // A time is written as YYYY-MM-DDTHH:MM:SS; a time without \yr is dropped.
    if (!info_has_year_) return;
    char text[96];
    std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02d", info_parts_[0], info_parts_[1],
                  info_parts_[2], info_parts_[3], info_parts_[4], info_parts_[5]);
    doc_.info[info_key_] = text;
    return;
  }
  std::string value = entry_value(buffer_);
  // A numeric entry such as {\nofpages1} has a parameter and no text.
  if (value.empty() && info_param_) value = std::to_string(*info_param_);
  doc_.info[info_key_] = std::move(value);
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

void Reader::on_text(std::string_view text, std::size_t offset) {
  GroupState& g = top();
  g.first = false;
  if (g.target == TextTarget::Picture) {
    picture_text(text, offset);
    return;
  }
  if (g.target == TextTarget::Discard) return;
  if (g.target == TextTarget::ColorTable) {
    for (char c : text) {
      if (c == ';') push_color();
    }
    return;
  }
  // Bytes below 0x80 are ASCII; the others decode through the current code page.
  std::string out;
  out.reserve(text.size());
  for (char ch : text) {
    const auto b = static_cast<unsigned char>(ch);
    if (b < 0x80) {
      out.push_back(ch);
    } else {
      append_utf8(out, decode(b, offset));
    }
  }
  emit(out);
}

void Reader::on_hex(const Token& tok) {
  top().first = false;
  emit_code_point(decode(tok.symbol, tok.offset));
}

// \uN: N below 0 is N + 65536; a high surrogate followed by a low one is one
// code point; an unpaired half is U+FFFD. Then skip \uc fallback units.
void Reader::on_unicode(const Token& tok) {
  top().first = false;
  std::int64_t v = tok.param;
  if (v < 0) v += 65536;
  const std::uint32_t cp = v < 0 ? 0xFFFDu : static_cast<std::uint32_t>(v);
  const bool high = cp >= 0xD800 && cp <= 0xDBFF;
  const bool low = cp >= 0xDC00 && cp <= 0xDFFF;
  if (pending_high_ != 0 && low) {
    const std::uint32_t pair = 0x10000u + ((pending_high_ - 0xD800u) << 10) + (cp - 0xDC00u);
    pending_high_ = 0;
    emit_code_point(pair);
  } else {
    flush_pending_surrogate();
    if (high) {
      pending_high_ = cp;
    } else {
      emit_code_point(cp);  // a lone low surrogate becomes U+FFFD in append_utf8
    }
  }
  fallback_left_ = static_cast<std::size_t>(top().uc);
}

void Reader::flush_pending_surrogate() {
  if (pending_high_ == 0) return;
  pending_high_ = 0;
  emit_code_point(0xFFFD);
}

// Sends UTF-8 text to the group's text target.
void Reader::emit(std::string_view utf8) {
  if (utf8.empty()) return;
  const GroupState& g = top();
  switch (g.target) {
    case TextTarget::Paragraph: sink().append_run(utf8, g.chars); break;
    case TextTarget::Buffer: buffer_.append(utf8.data(), utf8.size()); break;
    case TextTarget::Name: name_.append(utf8.data(), utf8.size()); break;
    case TextTarget::FieldInstruction:
      if (g.frame < fields_.size()) fields_[g.frame].instruction.append(utf8.data(), utf8.size());
      break;
    case TextTarget::FieldResult:
      if (g.frame < fields_.size()) append_text_run(fields_[g.frame].result, utf8, g.chars);
      break;
    case TextTarget::ListText: append_text_run(sink().begin_paragraph_if_needed().listText, utf8, g.chars); break;
    case TextTarget::ColorTable:
    case TextTarget::Discard:
    case TextTarget::Field:
    case TextTarget::Picture:
    case TextTarget::Shape:
    case TextTarget::Object: break;  // the text of a frame's own group is dropped
  }
}

void Reader::emit_code_point(std::uint32_t cp) {
  if (cp == 0) return;
  std::string s;
  append_utf8(s, cp);
  emit(s);
}

// Sends a break, a field or another inline item to the group's target.
// Targets that hold only text drop it.
void Reader::emit_inline(Inline item) {
  const GroupState& g = top();
  switch (g.target) {
    case TextTarget::Paragraph: sink().append_inline(std::move(item)); break;
    case TextTarget::FieldResult:
      if (g.frame < fields_.size()) fields_[g.frame].result.push_back(std::move(item));
      break;
    case TextTarget::ListText: sink().begin_paragraph_if_needed().listText.push_back(std::move(item)); break;
    default: break;
  }
}

// The code page of the current font: its \cpg, else its \fcharset mapped
// through the charset table, else the document's code page.
int Reader::current_code_page() {
  int font = top().chars.font.value_or(doc_.defaultFont);
  if (top().dest == Dest::FontTable && font_entry_) font = *font_entry_;
  const auto it = doc_.fonts.find(font);
  if (it == doc_.fonts.end()) return doc_.codePage;
  if (it->second.codePage) return *it->second.codePage;
  return charset_to_code_page(it->second.charset, doc_.codePage);
}

std::uint32_t Reader::decode(unsigned char b, std::size_t offset) {
  if (b < 0x80) return b;
  const int cp = current_code_page();
  if (cp == kSymbolCodePage) {
    warn_once(once_seen_, "symbol_font", "symbol_font", "Symbol font bytes mapped to U+F000 plus the byte",
              base_ + offset);
  } else if (!code_page_supported(cp)) {
    const std::string key = std::to_string(cp);
    warn_once(code_page_seen_, key, "unsupported_code_page", "code page " + key + " is not supported; bytes become U+FFFD",
              base_ + offset);
  }
  return decode_byte(cp, b);
}

// ---------------------------------------------------------------------------
// Warnings
// ---------------------------------------------------------------------------

void Reader::warn(const char* code, std::string message, std::size_t offset) {
  doc_.warnings.push_back(Warning{code, std::move(message), offset});
}

void Reader::warn_once(std::set<std::string>& seen, std::string_view key, const char* code, std::string message,
                       std::size_t offset) {
  if (!seen.insert(std::string(key)).second) return;
  warn(code, std::move(message), offset);
}

// Returns the offset of the '{' that starts {\rtf after an optional UTF-8
// BOM and ASCII whitespace, or size when the input is not RTF.
std::size_t rtf_start(const unsigned char* data, std::size_t size) {
  if (data == nullptr) return size;
  std::size_t i = 0;
  if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) i = 3;
  while (i < size && is_ascii_space(static_cast<char>(data[i]))) ++i;
  static constexpr char kPrefix[] = "{\\rtf";
  const std::size_t n = sizeof(kPrefix) - 1;
  if (size - i < n) return size;
  for (std::size_t k = 0; k < n; ++k) {
    if (data[i + k] != static_cast<unsigned char>(kPrefix[k])) return size;
  }
  // The control word must be exactly \rtf, followed by its parameter or a delimiter.
  if (i + n < size) {
    const unsigned char c = data[i + n];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) return size;
  }
  return i;
}

}  // namespace
}  // namespace detail

ParseResult parse(const unsigned char* data, std::size_t size, const ParseOptions& options) {
  ParseResult result;
  const std::size_t start = detail::rtf_start(data, size);
  if (start >= size) {
    result.error = "not_rtf";
    return result;
  }
  detail::Reader reader(data + start, size - start, start, options);
  result.document = reader.run();
  result.ok = true;
  return result;
}

ParseResult parse(std::string_view bytes, const ParseOptions& options) {
  return parse(reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), options);
}

}  // namespace rtf
