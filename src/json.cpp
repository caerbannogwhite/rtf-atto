// Canonical JSON writer. The output equals Python's
// json.dumps(obj, ensure_ascii=False, indent=2, sort_keys=True) + "\n".
// Every write_* function emits its keys in sorted (byte) order by hand.
#include "rtf/json.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "base64.hpp"

namespace rtf::detail {

class JsonWriter {
 public:
  explicit JsonWriter(std::string& out) : out_(out) {}

  void begin_object() { open('{'); }
  void end_object() { close('}'); }
  void begin_array() { open('['); }
  void end_array() { close(']'); }

  // Writes a member key. The next call must write the member's value.
  void key(std::string_view k) {
    separator();
    quoted(k);
    out_ += ": ";
    after_key_ = true;
  }
  void string(std::string_view s) {
    before_value();
    quoted(s);
  }
  void integer(long long v) {
    before_value();
    out_ += std::to_string(v);
  }
  void boolean(bool v) {
    before_value();
    out_ += v ? "true" : "false";
  }
  void null() {
    before_value();
    out_ += "null";
  }

 private:
  // Starts a new member or element on its own line, after a comma if needed.
  void separator() {
    if (!empty_.empty()) {
      if (!empty_.back()) out_ += ',';
      empty_.back() = false;
    }
    out_ += '\n';
    out_.append(empty_.size() * 2, ' ');
  }
  void before_value() {
    if (after_key_) {
      after_key_ = false;
    } else if (!empty_.empty()) {
      separator();
    }
  }
  void open(char c) {
    before_value();
    out_ += c;
    empty_.push_back(true);
  }
  void close(char c) {
    bool was_empty = empty_.back();
    empty_.pop_back();
    if (!was_empty) {
      out_ += '\n';
      out_.append(empty_.size() * 2, ' ');
    }
    out_ += c;
  }
  void quoted(std::string_view s) {
    static const char hex[] = "0123456789abcdef";
    out_ += '"';
    for (char ch : s) {
      unsigned char c = static_cast<unsigned char>(ch);
      switch (c) {
        case '"': out_ += "\\\""; break;
        case '\\': out_ += "\\\\"; break;
        case '\n': out_ += "\\n"; break;
        case '\r': out_ += "\\r"; break;
        case '\t': out_ += "\\t"; break;
        case '\b': out_ += "\\b"; break;
        case '\f': out_ += "\\f"; break;
        default:
          if (c < 0x20) {
            out_ += "\\u00";
            out_ += hex[c >> 4];
            out_ += hex[c & 15];
          } else {
            out_ += ch;
          }
      }
    }
    out_ += '"';
  }

  std::string& out_;
  std::vector<bool> empty_;  // one entry per open container: true while it has no members
  bool after_key_ = false;
};

namespace {

using W = JsonWriter;

void write_blocks(W& w, const std::vector<Block>& blocks);
void write_inlines(W& w, const std::vector<Inline>& items);

// ---- helpers for the omission rules ----

void opt_int(W& w, const char* name, const std::optional<int>& v) {
  if (!v) return;
  w.key(name);
  w.integer(*v);
}
void flag(W& w, const char* name, bool v) {
  if (!v) return;
  w.key(name);
  w.boolean(true);
}
void int_member(W& w, const char* name, int v) {
  w.key(name);
  w.integer(v);
}
void str_member(W& w, std::string_view name, std::string_view v) {
  w.key(name);
  w.string(v);
}

// ---- enum names ----

const char* name_of(FontFamily v) {
  switch (v) {
    case FontFamily::Nil: return "nil";
    case FontFamily::Roman: return "roman";
    case FontFamily::Swiss: return "swiss";
    case FontFamily::Modern: return "modern";
    case FontFamily::Script: return "script";
    case FontFamily::Decor: return "decor";
    case FontFamily::Tech: return "tech";
    case FontFamily::Bidi: return "bidi";
  }
  return "nil";
}
const char* name_of(FontPitch v) {
  switch (v) {
    case FontPitch::Default: return "default";
    case FontPitch::Fixed: return "fixed";
    case FontPitch::Variable: return "variable";
  }
  return "default";
}
const char* name_of(StyleType v) {
  switch (v) {
    case StyleType::Paragraph: return "paragraph";
    case StyleType::Character: return "character";
    case StyleType::Section: return "section";
    case StyleType::Table: return "table";
  }
  return "paragraph";
}
const char* name_of(Align v) {
  switch (v) {
    case Align::Left: return "left";
    case Align::Center: return "center";
    case Align::Right: return "right";
    case Align::Justify: return "justify";
  }
  return "left";
}
const char* name_of(BorderStyle v) {
  switch (v) {
    case BorderStyle::Single: return "single";
    case BorderStyle::Double: return "double";
    case BorderStyle::Dotted: return "dotted";
    case BorderStyle::Dashed: return "dashed";
    case BorderStyle::Thick: return "thick";
    case BorderStyle::Hairline: return "hairline";
    case BorderStyle::Triple: return "triple";
    case BorderStyle::None: return "none";
    case BorderStyle::Other: return "other";
  }
  return "other";
}
const char* name_of(TabAlign v) {
  switch (v) {
    case TabAlign::Left: return "left";
    case TabAlign::Center: return "center";
    case TabAlign::Right: return "right";
    case TabAlign::Decimal: return "decimal";
    case TabAlign::Bar: return "bar";
  }
  return "left";
}
const char* name_of(TabLeader v) {
  switch (v) {
    case TabLeader::None: return "none";
    case TabLeader::Dots: return "dots";
    case TabLeader::Hyphens: return "hyphens";
    case TabLeader::Underline: return "underline";
    case TabLeader::Thick: return "thick";
    case TabLeader::Equal: return "equal";
  }
  return "none";
}
const char* name_of(Underline v) {
  switch (v) {
    case Underline::None: return "none";
    case Underline::Single: return "single";
    case Underline::Double: return "double";
    case Underline::Word: return "word";
    case Underline::Dotted: return "dotted";
    case Underline::Dash: return "dash";
    case Underline::Wave: return "wave";
    case Underline::Thick: return "thick";
  }
  return "none";
}
const char* name_of(Script v) {
  switch (v) {
    case Script::Normal: return "normal";
    case Script::Super: return "super";
    case Script::Sub: return "sub";
  }
  return "normal";
}
const char* name_of(PictureFormat v) {
  switch (v) {
    case PictureFormat::Png: return "png";
    case PictureFormat::Jpeg: return "jpeg";
    case PictureFormat::Emf: return "emf";
    case PictureFormat::Wmf: return "wmf";
    case PictureFormat::Dib: return "dib";
    case PictureFormat::Bmp: return "bmp";
    case PictureFormat::Unknown: return "unknown";
  }
  return "unknown";
}
const char* name_of(WidthType v) {
  switch (v) {
    case WidthType::Auto: return "auto";
    case WidthType::Percent: return "percent";
    case WidthType::Twips: return "twips";
  }
  return "auto";
}
const char* name_of(VerticalAlign v) {
  switch (v) {
    case VerticalAlign::Top: return "top";
    case VerticalAlign::Center: return "center";
    case VerticalAlign::Bottom: return "bottom";
  }
  return "top";
}
const char* name_of(TextDirection v) {
  switch (v) {
    case TextDirection::Lrtb: return "lrtb";
    case TextDirection::Tbrl: return "tbrl";
    case TextDirection::Btlr: return "btlr";
    case TextDirection::Lrtbv: return "lrtbv";
    case TextDirection::Tbrlv: return "tbrlv";
  }
  return "lrtb";
}
const char* name_of(Merge v) {
  switch (v) {
    case Merge::First: return "first";
    case Merge::Continued: return "continued";
  }
  return "first";
}
const char* name_of(HeightRule v) {
  switch (v) {
    case HeightRule::Auto: return "auto";
    case HeightRule::AtLeast: return "atLeast";
    case HeightRule::Exact: return "exact";
  }
  return "auto";
}
const char* name_of(SectionBreak v) {
  switch (v) {
    case SectionBreak::Page: return "page";
    case SectionBreak::None: return "none";
    case SectionBreak::Column: return "column";
    case SectionBreak::Even: return "even";
    case SectionBreak::Odd: return "odd";
  }
  return "page";
}

template <class E>
void opt_enum(W& w, const char* name, const std::optional<E>& v) {
  if (!v) return;
  w.key(name);
  w.string(name_of(*v));
}

// ---- leaf structs ----

void write_warning(W& w, const Warning& v) {
  w.begin_object();
  str_member(w, "code", v.code);
  str_member(w, "message", v.message);
  w.key("offset");
  w.integer(static_cast<long long>(v.offset));
  w.end_object();
}

void write_font(W& w, const Font& v) {
  w.begin_object();
  int_member(w, "charset", v.charset);
  opt_int(w, "codePage", v.codePage);
  str_member(w, "family", name_of(v.family));
  str_member(w, "name", v.name);
  str_member(w, "pitch", name_of(v.pitch));
  w.end_object();
}

void write_color(W& w, const Color& v) {
  w.begin_object();
  int_member(w, "b", v.b);
  int_member(w, "g", v.g);
  int_member(w, "r", v.r);
  w.end_object();
}

void write_style(W& w, const Style& v) {
  w.begin_object();
  opt_int(w, "basedOn", v.basedOn);
  int_member(w, "index", v.index);
  str_member(w, "name", v.name);
  opt_int(w, "next", v.next);
  str_member(w, "type", name_of(v.type));
  w.end_object();
}

void write_page(W& w, const PageSetup& v) {
  w.begin_object();
  flag(w, "facingPages", v.facingPages);
  int_member(w, "footerDistance", v.footerDistance);
  int_member(w, "headerDistance", v.headerDistance);
  int_member(w, "height", v.height);
  w.key("landscape");
  w.boolean(v.landscape);
  int_member(w, "marginBottom", v.marginBottom);
  int_member(w, "marginLeft", v.marginLeft);
  int_member(w, "marginRight", v.marginRight);
  int_member(w, "marginTop", v.marginTop);
  int_member(w, "width", v.width);
  w.end_object();
}

void write_border(W& w, const Border& v) {
  w.begin_object();
  opt_int(w, "color", v.color);
  opt_int(w, "space", v.space);
  str_member(w, "style", name_of(v.style));
  int_member(w, "width", v.width);
  w.end_object();
}

void opt_border(W& w, const char* name, const std::optional<Border>& v) {
  if (!v) return;
  w.key(name);
  write_border(w, *v);
}

void write_borders(W& w, const Borders& v) {
  w.begin_object();
  opt_border(w, "bottom", v.bottom);
  opt_border(w, "horizontal", v.horizontal);
  opt_border(w, "left", v.left);
  opt_border(w, "right", v.right);
  opt_border(w, "top", v.top);
  opt_border(w, "vertical", v.vertical);
  w.end_object();
}

void write_shading(W& w, const Shading& v) {
  w.begin_object();
  opt_int(w, "background", v.background);
  opt_int(w, "foreground", v.foreground);
  if (v.pattern) str_member(w, "pattern", *v.pattern);
  opt_int(w, "percent", v.percent);
  w.end_object();
}

void write_padding(W& w, const Padding& v) {
  w.begin_object();
  opt_int(w, "bottom", v.bottom);
  opt_int(w, "left", v.left);
  opt_int(w, "right", v.right);
  opt_int(w, "top", v.top);
  w.end_object();
}

void write_tab_stop(W& w, const TabStop& v) {
  w.begin_object();
  str_member(w, "align", name_of(v.align));
  str_member(w, "leader", name_of(v.leader));
  int_member(w, "position", v.position);
  w.end_object();
}

void write_list_ref(W& w, const ListRef& v) {
  w.begin_object();
  int_member(w, "id", v.id);
  int_member(w, "level", v.level);
  w.end_object();
}

void write_paragraph_props(W& w, const ParagraphProps& v) {
  w.begin_object();
  opt_enum(w, "align", v.align);
  if (v.borders) {
    w.key("borders");
    write_borders(w, *v.borders);
  }
  opt_int(w, "indentFirst", v.indentFirst);
  opt_int(w, "indentLeft", v.indentLeft);
  opt_int(w, "indentRight", v.indentRight);
  flag(w, "keepTogether", v.keepTogether);
  flag(w, "keepWithNext", v.keepWithNext);
  opt_int(w, "lineSpacing", v.lineSpacing);
  flag(w, "lineSpacingMultiple", v.lineSpacingMultiple);
  if (v.list) {
    w.key("list");
    write_list_ref(w, *v.list);
  }
  flag(w, "pageBreakBefore", v.pageBreakBefore);
  flag(w, "rtl", v.rtl);
  if (v.shading) {
    w.key("shading");
    write_shading(w, *v.shading);
  }
  opt_int(w, "spaceAfter", v.spaceAfter);
  opt_int(w, "spaceBefore", v.spaceBefore);
  opt_int(w, "style", v.style);
  if (!v.tabs.empty()) {
    w.key("tabs");
    w.begin_array();
    for (const TabStop& t : v.tabs) write_tab_stop(w, t);
    w.end_array();
  }
  if (v.widowControl) {
    w.key("widowControl");
    w.boolean(*v.widowControl);
  }
  w.end_object();
}

void write_char_props(W& w, const CharProps& v) {
  w.begin_object();
  opt_int(w, "background", v.background);
  flag(w, "bold", v.bold);
  flag(w, "caps", v.caps);
  opt_int(w, "color", v.color);
  flag(w, "deleted", v.deleted);
  flag(w, "doubleStrike", v.doubleStrike);
  opt_int(w, "font", v.font);
  flag(w, "hidden", v.hidden);
  opt_int(w, "highlight", v.highlight);
  flag(w, "inserted", v.inserted);
  flag(w, "italic", v.italic);
  opt_int(w, "language", v.language);
  opt_int(w, "offset", v.offset);
  flag(w, "outline", v.outline);
  opt_enum(w, "script", v.script);
  flag(w, "shadow", v.shadow);
  opt_int(w, "size", v.size);
  flag(w, "smallCaps", v.smallCaps);
  flag(w, "strike", v.strike);
  opt_enum(w, "underline", v.underline);
  w.end_object();
}

// ---- inlines ----

void write_picture_body(W& w, const Picture& v) {
  w.begin_object();
  str_member(w, "data", base64(v.data));
  str_member(w, "format", name_of(v.format));
  opt_int(w, "goalHeight", v.goalHeight);
  opt_int(w, "goalWidth", v.goalWidth);
  opt_int(w, "height", v.height);
  str_member(w, "kind", "picture");
  opt_int(w, "scaleX", v.scaleX);
  opt_int(w, "scaleY", v.scaleY);
  opt_int(w, "width", v.width);
  w.end_object();
}

void write_kind_only(W& w, const char* kind) {
  w.begin_object();
  str_member(w, "kind", kind);
  w.end_object();
}

void write_inline(W& w, const Inline& item) {
  const auto& v = item.value;
  if (const auto* t = std::get_if<TextRun>(&v)) {
    w.begin_object();
    str_member(w, "kind", "text");
    w.key("props");
    write_char_props(w, t->props);
    str_member(w, "text", t->text);
    w.end_object();
  } else if (std::holds_alternative<LineBreak>(v)) {
    write_kind_only(w, "lineBreak");
  } else if (std::holds_alternative<Tab>(v)) {
    write_kind_only(w, "tab");
  } else if (std::holds_alternative<PageBreak>(v)) {
    write_kind_only(w, "pageBreak");
  } else if (std::holds_alternative<ColumnBreak>(v)) {
    write_kind_only(w, "columnBreak");
  } else if (const auto* f = std::get_if<Field>(&v)) {
    w.begin_object();
    flag(w, "dirty", f->dirty);
    str_member(w, "instruction", f->instruction);
    str_member(w, "kind", "field");
    flag(w, "locked", f->locked);
    w.key("result");
    write_inlines(w, f->result);
    w.end_object();
  } else if (const auto* b = std::get_if<Bookmark>(&v)) {
    w.begin_object();
    str_member(w, "kind", "bookmark");
    str_member(w, "name", b->name);
    str_member(w, "position", b->end ? "end" : "start");
    w.end_object();
  } else if (const auto* n = std::get_if<Footnote>(&v)) {
    w.begin_object();
    w.key("blocks");
    write_blocks(w, n->blocks);
    flag(w, "endnote", n->endnote);
    str_member(w, "kind", "footnote");
    w.end_object();
  } else if (const auto* p = std::get_if<Picture>(&v)) {
    write_picture_body(w, *p);
  } else if (const auto* s = std::get_if<Shape>(&v)) {
    w.begin_object();
    w.key("blocks");
    write_blocks(w, s->blocks);
    str_member(w, "kind", "shape");
    if (s->picture) {
      w.key("picture");
      write_picture_body(w, *s->picture);
    }
    w.end_object();
  } else if (const auto* o = std::get_if<EmbeddedObject>(&v)) {
    w.begin_object();
    if (o->className) str_member(w, "className", *o->className);
    str_member(w, "kind", "object");
    w.key("result");
    write_blocks(w, o->result);
    w.end_object();
  }
}

void write_inlines(W& w, const std::vector<Inline>& items) {
  w.begin_array();
  for (const Inline& item : items) write_inline(w, item);
  w.end_array();
}

// ---- blocks ----

void write_paragraph(W& w, const Paragraph& v) {
  w.begin_object();
  str_member(w, "kind", "paragraph");
  if (!v.listText.empty()) {
    w.key("listText");
    write_inlines(w, v.listText);
  }
  w.key("props");
  write_paragraph_props(w, v.props);
  w.key("runs");
  write_inlines(w, v.runs);
  w.end_object();
}

void write_cell(W& w, const Cell& v) {
  w.begin_object();
  w.key("blocks");
  write_blocks(w, v.blocks);
  if (v.borders) {
    w.key("borders");
    write_borders(w, *v.borders);
  }
  opt_enum(w, "mergeHorizontal", v.mergeHorizontal);
  opt_enum(w, "mergeVertical", v.mergeVertical);
  flag(w, "noWrap", v.noWrap);
  if (v.padding) {
    w.key("padding");
    write_padding(w, *v.padding);
  }
  if (v.preferredWidth) {
    w.key("preferredWidth");
    w.begin_object();
    str_member(w, "type", name_of(v.preferredWidth->type));
    int_member(w, "value", v.preferredWidth->value);
    w.end_object();
  }
  int_member(w, "right", v.right);
  if (v.shading) {
    w.key("shading");
    write_shading(w, *v.shading);
  }
  opt_enum(w, "textDirection", v.textDirection);
  opt_enum(w, "verticalAlign", v.verticalAlign);
  int_member(w, "width", v.width);
  w.end_object();
}

void write_row_props(W& w, const RowProps& v) {
  w.begin_object();
  opt_enum(w, "align", v.align);
  if (v.borders) {
    w.key("borders");
    write_borders(w, *v.borders);
  }
  opt_int(w, "gapHalf", v.gapHalf);
  flag(w, "header", v.header);
  opt_int(w, "height", v.height);
  opt_enum(w, "heightRule", v.heightRule);
  flag(w, "keep", v.keep);
  opt_int(w, "left", v.left);
  if (v.padding) {
    w.key("padding");
    write_padding(w, *v.padding);
  }
  flag(w, "rtl", v.rtl);
  w.end_object();
}

void write_table(W& w, const Table& v) {
  w.begin_object();
  str_member(w, "kind", "table");
  w.key("rows");
  w.begin_array();
  for (const Row& row : v.rows) {
    w.begin_object();
    w.key("cells");
    w.begin_array();
    for (const Cell& c : row.cells) write_cell(w, c);
    w.end_array();
    w.key("props");
    write_row_props(w, row.props);
    w.end_object();
  }
  w.end_array();
  w.end_object();
}

void write_blocks(W& w, const std::vector<Block>& blocks) {
  w.begin_array();
  for (const Block& b : blocks) {
    if (const auto* p = std::get_if<Paragraph>(&b.value)) {
      write_paragraph(w, *p);
    } else if (const auto* t = std::get_if<Table>(&b.value)) {
      write_table(w, *t);
    }
  }
  w.end_array();
}

// ---- sections and document ----

void opt_blocks(W& w, const char* name, const std::optional<std::vector<Block>>& v) {
  if (!v) return;
  w.key(name);
  write_blocks(w, *v);
}

void write_header_set(W& w, const HeaderSet& v) {
  w.begin_object();
  opt_blocks(w, "default", v.default_);
  opt_blocks(w, "first", v.first);
  opt_blocks(w, "left", v.left);
  opt_blocks(w, "right", v.right);
  w.end_object();
}

void write_section(W& w, const Section& v) {
  w.begin_object();
  w.key("body");
  write_blocks(w, v.body);
  str_member(w, "break", name_of(v.break_));
  opt_int(w, "columns", v.columns);
  w.key("footers");
  write_header_set(w, v.footers);
  w.key("headers");
  write_header_set(w, v.headers);
  w.key("page");
  write_page(w, v.page);
  opt_int(w, "pageNumberStart", v.pageNumberStart);
  flag(w, "titlePage", v.titlePage);
  w.end_object();
}

void write_document(W& w, const RtfDocument& d) {
  w.begin_object();
  int_member(w, "codePage", d.codePage);
  w.key("colors");
  w.begin_array();
  for (const std::optional<Color>& c : d.colors) {
    if (c) {
      write_color(w, *c);
    } else {
      w.null();
    }
  }
  w.end_array();
  int_member(w, "defaultFont", d.defaultFont);
  opt_int(w, "defaultLanguage", d.defaultLanguage);

  // Font numbers are keys, sorted as strings.
  std::vector<std::pair<std::string, const Font*>> fonts;
  fonts.reserve(d.fonts.size());
  for (const auto& entry : d.fonts) fonts.emplace_back(std::to_string(entry.first), &entry.second);
  std::sort(fonts.begin(), fonts.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  w.key("fonts");
  w.begin_object();
  for (const auto& entry : fonts) {
    w.key(entry.first);
    write_font(w, *entry.second);
  }
  w.end_object();

  if (d.generator) str_member(w, "generator", *d.generator);
  w.key("info");
  w.begin_object();
  for (const auto& entry : d.info) str_member(w, entry.first, entry.second);
  w.end_object();
  w.key("page");
  write_page(w, d.page);
  w.key("sections");
  w.begin_array();
  for (const Section& s : d.sections) write_section(w, s);
  w.end_array();
  w.key("styles");
  w.begin_array();
  for (const Style& s : d.styles) write_style(w, s);
  w.end_array();
  int_member(w, "version", d.version);
  w.key("warnings");
  w.begin_array();
  for (const Warning& x : d.warnings) write_warning(w, x);
  w.end_array();
  w.end_object();
}

}  // namespace
}  // namespace rtf::detail

namespace rtf {

std::string to_json(const RtfDocument& doc) {
  std::string out;
  detail::JsonWriter writer(out);
  detail::write_document(writer, doc);
  out += '\n';
  return out;
}

}  // namespace rtf
