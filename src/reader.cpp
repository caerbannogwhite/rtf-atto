// The reader: turns the token stream into the document model.
// The rules follow the design document, section "The reader".
#include "rtf/reader.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
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
enum class Family : std::uint8_t { Destination, Document, Char, Para, Border, Special, Font, Color, Style, Info };

// What a destination word does when it is the first word of a group.
// Later marks destinations that Task 6 gives a meaning; until then they
// follow the generic rules (a formatting group, or unknown after \*).
enum class DestKind : std::uint8_t { FontTable, ColorTable, StyleSheet, Info, Generator, SkipWarn, SkipSilent, Later };
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
    entry("bkmkend", DestKind::Later),
    entry("bkmkstart", DestKind::Later),
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
    entry("cf", CharOp::Color),
    entry("cfpat", ParaOp::CfPat),
    entry("chcbpat", CharOp::Background),
    entry("chftn", SpecialOp::Chftn),
    entry("chpgn", SpecialOp::Chpgn),
    entry("colorschememapping", DestKind::SkipWarn),
    entry("colortbl", DestKind::ColorTable),
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
    entry("dn", CharOp::Down),
    entry("do", DestKind::SkipWarn),
    entry("ds", StyleOp::Section),
    entry("dy", InfoOp::Day),
    entry("emdash", SpecialOp::Emdash),
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
    entry("field", DestKind::Later),
    entry("fldinst", DestKind::Later),
    entry("fldrslt", DestKind::Later),
    entry("fmodern", FontOp::Modern),
    entry("fname", DestKind::SkipSilent),
    entry("fnil", FontOp::Nil),
    entry("fonttbl", DestKind::FontTable),
    entry("footer", DestKind::Later),
    entry("footerf", DestKind::Later),
    entry("footerl", DestKind::Later),
    entry("footerr", DestKind::Later),
    entry("footnote", DestKind::Later),
    entry("fprq", FontOp::Pitch),
    entry("froman", FontOp::Roman),
    entry("fs", CharOp::Size),
    entry("fscript", FontOp::Script),
    entry("fswiss", FontOp::Swiss),
    entry("ftech", FontOp::Tech),
    entry("ftnsep", DestKind::SkipWarn),
    entry("ftnsepc", DestKind::SkipWarn),
    entry("generator", DestKind::Generator),
    entry("green", ColorOp::Green),
    entry("header", DestKind::Later),
    entry("headerf", DestKind::Later),
    entry("headerl", DestKind::Later),
    entry("headerr", DestKind::Later),
    entry("highlight", CharOp::Highlight),
    entry("hr", InfoOp::Hour),
    entry("htmltag", DestKind::SkipWarn),
    entry("i", CharOp::Italic),
    entry("ilvl", ParaOp::Ilvl),
    entry("info", DestKind::Info),
    entry("intbl", ParaOp::InTbl),
    entry("itap", ParaOp::Itap),
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
    entry("listtext", DestKind::Later),
    entry("lquote", SpecialOp::Lquote),
    entry("ls", ParaOp::Ls),
    entry("lsdlockedexcept", DestKind::SkipWarn),
    entry("ltrpar", ParaOp::LtrPar),
    entry("mac", DocOp::Mac),
    entry("margb", DocOp::MargB),
    entry("margl", DocOp::MargL),
    entry("margr", DocOp::MargR),
    entry("margt", DocOp::MargT),
    entry("mhtmltag", DestKind::SkipWarn),
    entry("min", InfoOp::Minute),
    entry("mmathPr", DestKind::SkipWarn),
    entry("mo", InfoOp::Month),
    entry("nesttableprops", DestKind::Later),
    entry("nonesttables", DestKind::SkipWarn),
    entry("nonshppict", DestKind::SkipWarn),
    entry("nosupersub", CharOp::NoSuperSub),
    entry("nowidctlpar", ParaOp::NoWidCtl),
    entry("objclass", DestKind::Later),
    entry("objdata", DestKind::SkipWarn),
    entry("object", DestKind::Later),
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
    entry("picprop", DestKind::SkipWarn),
    entry("pict", DestKind::Later),
    entry("plain", CharOp::Plain),
    entry("pn", DestKind::SkipWarn),
    entry("pnseclvl", DestKind::SkipWarn),
    entry("pntext", DestKind::Later),
    entry("protusertbl", DestKind::SkipWarn),
    entry("qc", ParaOp::Qc),
    entry("qj", ParaOp::Qj),
    entry("ql", ParaOp::Ql),
    entry("qmspace", SpecialOp::Qmspace),
    entry("qr", ParaOp::Qr),
    entry("rdblquote", SpecialOp::Rdblquote),
    entry("red", ColorOp::Red),
    entry("result", DestKind::Later),
    entry("revised", CharOp::Revised),
    entry("revtbl", DestKind::SkipWarn),
    entry("ri", ParaOp::Ri),
    entry("rquote", SpecialOp::Rquote),
    entry("rsidtbl", DestKind::SkipWarn),
    entry("rtf", DocOp::Rtf),
    entry("rtlpar", ParaOp::RtlPar),
    entry("s", ParaOp::Style),
    entry("sa", ParaOp::Sa),
    entry("sb", ParaOp::Sb),
    entry("sbasedon", StyleOp::BasedOn),
    entry("scaps", CharOp::SmallCaps),
    entry("sec", InfoOp::Second),
    entry("shad", CharOp::Shadow),
    entry("shading", ParaOp::Shading),
    entry("shp", DestKind::Later),
    entry("shpinst", DestKind::Later),
    entry("shppict", DestKind::Later),
    entry("shprslt", DestKind::SkipWarn),
    entry("shptxt", DestKind::Later),
    entry("sl", ParaOp::Sl),
    entry("slmult", ParaOp::SlMult),
    entry("snext", StyleOp::Next),
    entry("strike", CharOp::Strike),
    entry("striked", CharOp::DoubleStrike),
    entry("stylesheet", DestKind::StyleSheet),
    entry("sub", CharOp::Sub),
    entry("super", CharOp::Super),
    entry("tab", SpecialOp::Tab),
    entry("tb", ParaOp::Tb),
    entry("tc", DestKind::SkipWarn),
    entry("themedata", DestKind::SkipWarn),
    entry("tldot", ParaOp::TlDot),
    entry("tleq", ParaOp::TlEq),
    entry("tlhyph", ParaOp::TlHyph),
    entry("tlth", ParaOp::TlTh),
    entry("tlul", ParaOp::TlUl),
    entry("tqc", ParaOp::Tqc),
    entry("tqdec", ParaOp::Tqdec),
    entry("tqr", ParaOp::Tqr),
    entry("ts", StyleOp::Table),
    entry("tx", ParaOp::Tx),
    entry("uc", DocOp::Uc),
    entry("ud", DestKind::Later),
    entry("ul", CharOp::Underline),
    entry("uld", CharOp::UlDotted),
    entry("uldash", CharOp::UlDash),
    entry("uldb", CharOp::UlDouble),
    entry("ulnone", CharOp::UlNone),
    entry("ulth", CharOp::UlThick),
    entry("ulw", CharOp::UlWord),
    entry("ulwave", CharOp::UlWave),
    entry("up", CharOp::Up),
    entry("upr", DestKind::Later),
    entry("userprops", DestKind::SkipWarn),
    entry("v", CharOp::Hidden),
    entry("wgrffmtfilter", DestKind::SkipWarn),
    entry("widctlpar", ParaOp::WidCtl),
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
enum class TextTarget : std::uint8_t { Paragraph, Buffer, ColorTable, Discard };

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
  BorderTarget border;
  std::size_t sink = 0;        // index of the BlockSink this group writes to
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
  void close_sink(BlockSink& s, const ParagraphProps& props);

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
  void border_style(BorderStyle style);
  int border_slots(Border* out[4]);
  void special_word(SpecialOp op);

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
  std::size_t max_depth_;
  RtfDocument doc_;
  std::vector<GroupState> stack_;
  std::vector<BlockSink> sinks_;
  SectionState section_;
  bool section_defined_ = false;  // Task 6 sets it on \sectd

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

  // Paragraph tab stops: the alignment and leader words come before \tx.
  TabAlign pending_tab_align_ = TabAlign::Left;
  TabLeader pending_tab_leader_ = TabLeader::None;

  // Warnings given once per key.
  std::set<std::string> unknown_seen_, skipped_seen_, param_seen_, code_page_seen_, once_seen_;
};

Reader::Reader(const unsigned char* data, std::size_t size, std::size_t base, const ParseOptions& options)
    : tokenizer_(data, size), base_(base), max_depth_(options.maxDepth > 0 ? options.maxDepth : 1) {
  doc_.sections.emplace_back();
  sinks_.emplace_back(&doc_.sections.back().body);
  section_.reset(doc_.page);
}

RtfDocument Reader::run() {
  for (;;) {
    Token tok = tokenizer_.next();
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
        // \bin outside a picture or object: its bytes are not text.
        top().first = false;
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
    warn_once(once_seen_, "depth_limit", "depth_limit", "groups nested deeper than the limit are flattened",
              base_ + tok.offset);
    return;
  }
  top().first = false;
  GroupState child = top();
  child.first = true;
  child.star = false;
  child.ownsEntry = false;
  child.ownsSink = false;
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
    close_sink(sinks_.back(), g.para.props);
    sinks_.pop_back();
  }
  if (stack_.size() == 1) finish_document(g);
  stack_.pop_back();
}

void Reader::finish_document(const GroupState& root) {
  while (!sinks_.empty()) {
    close_sink(sinks_.back(), root.para.props);
    sinks_.pop_back();
  }
  Section& s = doc_.sections.back();
  s.page = section_.page;
  s.break_ = section_.break_;
  s.columns = section_.columns;
  s.titlePage = section_.titlePage;
  s.pageNumberStart = section_.pageNumberStart;
  finished_ = true;
}

// The paragraph that a closing container ends carries the properties in
// effect at that moment.
void Reader::close_sink(BlockSink& s, const ParagraphProps& props) {
  if (s.current) s.current->props = props;
  s.close(doc_.warnings);
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
    if (is_dest && op_of<DestKind>(*info) != DestKind::Later) return open_destination(tok, op_of<DestKind>(*info));
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
    case DestKind::Later:
      // Task 6: \field, \header, \footnote, \pict, \shp, \object, \listtext,
      // \pntext, \upr and their parts. Until then a formatting group.
      return false;
  }
  return false;
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
    default: break;  // table words outside their table, destination words not at a group start
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
    p.tabs.push_back(TabStop{param_or(tok, 0), align, pending_tab_leader_});
    pending_tab_align_ = TabAlign::Left;
    pending_tab_leader_ = TabLeader::None;
  };
  auto shading = [&]() -> Shading& {
    if (!p.shading) p.shading.emplace();
    return *p.shading;
  };
  switch (op) {
    case ParaOp::Pard:
      g.para = ParaState{};
      g.border = BorderTarget{};
      pending_tab_align_ = TabAlign::Left;
      pending_tab_leader_ = TabLeader::None;
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
    case ParaOp::Tx: add_tab(pending_tab_align_); break;
    case ParaOp::Tb:
      // \tbN is a bar tab at N; written without N it marks the next \tx.
      if (tok.hasParam) {
        add_tab(TabAlign::Bar);
      } else {
        pending_tab_align_ = TabAlign::Bar;
      }
      break;
    case ParaOp::Tqc: pending_tab_align_ = TabAlign::Center; break;
    case ParaOp::Tqr: pending_tab_align_ = TabAlign::Right; break;
    case ParaOp::Tqdec: pending_tab_align_ = TabAlign::Decimal; break;
    case ParaOp::TlDot: pending_tab_leader_ = TabLeader::Dots; break;
    case ParaOp::TlHyph: pending_tab_leader_ = TabLeader::Hyphens; break;
    case ParaOp::TlUl: pending_tab_leader_ = TabLeader::Underline; break;
    case ParaOp::TlTh: pending_tab_leader_ = TabLeader::Thick; break;
    case ParaOp::TlEq: pending_tab_leader_ = TabLeader::Equal; break;
    case ParaOp::Shading: shading().percent = param_or(tok, 0); break;
    case ParaOp::CbPat: shading().background = param_or(tok, 0); break;
    case ParaOp::CfPat: shading().foreground = param_or(tok, 0); break;
  }
}

void Reader::border_word(const Token& tok, BorderOp op) {
  GroupState& g = top();
  // A border-opening word makes its side the target of the following
  // \brdr* words and starts that border as a single line of width 0.
  auto open = [&](BorderTarget::Side side) {
    g.border = BorderTarget{BorderTarget::Kind::Paragraph, side};
    if (!g.para.props.borders) g.para.props.borders.emplace();
    Border* slots[4] = {nullptr, nullptr, nullptr, nullptr};
    const int n = border_slots(slots);
    for (int i = 0; i < n; ++i) *slots[i] = Border{};
  };
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

void Reader::border_style(BorderStyle style) {
  Border* slots[4] = {nullptr, nullptr, nullptr, nullptr};
  const int n = border_slots(slots);
  for (int i = 0; i < n; ++i) slots[i]->style = style;
}

// Returns the borders the current target names. \box names all four sides.
// The border between paragraphs is stored as `horizontal`; the bar border
// has no place in the model and is dropped. Cell and row borders are Task 5.
int Reader::border_slots(Border* out[4]) {
  GroupState& g = top();
  if (g.border.kind != BorderTarget::Kind::Paragraph || !g.para.props.borders) return 0;
  Borders& b = *g.para.props.borders;
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
  switch (op) {
    case SpecialOp::Par:
      if (in_paragraph) sink().end_paragraph(top().para.props, paragraph_level(top().para));
      return;
    case SpecialOp::Line: emit_inline(Inline{LineBreak{}}); return;
    case SpecialOp::Tab:
      if (in_paragraph) {
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
  switch (top().target) {
    case TextTarget::Paragraph: sink().append_run(utf8, top().chars); break;
    case TextTarget::Buffer: buffer_.append(utf8.data(), utf8.size()); break;
    case TextTarget::ColorTable:
    case TextTarget::Discard: break;
  }
}

void Reader::emit_code_point(std::uint32_t cp) {
  if (cp == 0) return;
  std::string s;
  append_utf8(s, cp);
  emit(s);
}

void Reader::emit_inline(Inline item) {
  if (top().target == TextTarget::Paragraph) sink().append_inline(std::move(item));
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
