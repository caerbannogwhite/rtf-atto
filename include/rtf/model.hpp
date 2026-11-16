#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rtf {

struct Warning {
  std::string code;        // see the plan's warning table
  std::string message;
  std::size_t offset = 0;  // byte offset in the input
};

enum class FontFamily { Nil, Roman, Swiss, Modern, Script, Decor, Tech, Bidi };
enum class FontPitch { Default, Fixed, Variable };
struct Font {
  std::string name;
  FontFamily family = FontFamily::Nil;
  int charset = 0;
  FontPitch pitch = FontPitch::Default;
  std::optional<int> codePage;  // \cpg
};

struct Color { int r = 0; int g = 0; int b = 0; };

enum class StyleType { Paragraph, Character, Section, Table };
struct Style {
  int index = 0;
  StyleType type = StyleType::Paragraph;
  std::string name;
  std::optional<int> basedOn;
  std::optional<int> next;
};

struct PageSetup {
  int width = 12240;
  int height = 15840;
  bool landscape = false;
  int marginTop = 1440;
  int marginBottom = 1440;
  int marginLeft = 1800;
  int marginRight = 1800;
  int headerDistance = 720;
  int footerDistance = 720;
  bool facingPages = false;
};

enum class Align { Left, Center, Right, Justify };
enum class BorderStyle { Single, Double, Dotted, Dashed, Thick, Hairline, Triple, None, Other };
struct Border {
  BorderStyle style = BorderStyle::Single;
  int width = 0;
  std::optional<int> color;
  std::optional<int> space;
};
struct Borders { std::optional<Border> top, left, bottom, right, horizontal, vertical; };
struct Shading {
  std::optional<int> background, foreground, percent;
  std::optional<std::string> pattern;
};
struct Padding { std::optional<int> top, left, bottom, right; };

enum class TabAlign { Left, Center, Right, Decimal, Bar };
enum class TabLeader { None, Dots, Hyphens, Underline, Thick, Equal };
struct TabStop { int position = 0; TabAlign align = TabAlign::Left; TabLeader leader = TabLeader::None; };
struct ListRef { int id = 0; int level = 0; };

struct ParagraphProps {
  std::optional<Align> align;
  std::optional<int> indentLeft, indentRight, indentFirst;
  std::optional<int> spaceBefore, spaceAfter, lineSpacing;
  bool lineSpacingMultiple = false;
  bool keepTogether = false;
  bool keepWithNext = false;
  bool pageBreakBefore = false;
  std::optional<bool> widowControl;
  std::optional<int> style;
  std::optional<ListRef> list;
  std::vector<TabStop> tabs;
  std::optional<Borders> borders;
  std::optional<Shading> shading;
  bool rtl = false;
};

enum class Underline { None, Single, Double, Word, Dotted, Dash, Wave, Thick };
enum class Script { Normal, Super, Sub };
struct CharProps {
  std::optional<int> font, size;
  bool bold = false, italic = false;
  std::optional<Underline> underline;
  bool strike = false, doubleStrike = false;
  std::optional<Script> script;
  std::optional<int> offset;
  std::optional<int> color, background, highlight;
  bool caps = false, smallCaps = false, hidden = false, outline = false, shadow = false;
  std::optional<int> language;
  bool deleted = false, inserted = false;
};

struct Block;
struct Inline;

struct TextRun { std::string text; CharProps props; };
struct LineBreak {};
struct Tab {};
struct PageBreak {};
struct ColumnBreak {};
struct Field { std::string instruction; std::vector<Inline> result; bool dirty = false; bool locked = false; };
struct Bookmark { std::string name; bool end = false; };
struct Footnote { bool endnote = false; std::vector<Block> blocks; };
enum class PictureFormat { Png, Jpeg, Emf, Wmf, Dib, Bmp, Unknown };
struct Picture {
  PictureFormat format = PictureFormat::Unknown;
  std::string data;  // raw bytes; the JSON carries base64
  std::optional<int> width, height, goalWidth, goalHeight, scaleX, scaleY;
};
struct Shape { std::vector<Block> blocks; std::optional<Picture> picture; };
struct EmbeddedObject { std::optional<std::string> className; std::vector<Block> result; };

struct Inline {
  std::variant<TextRun, LineBreak, Tab, PageBreak, ColumnBreak, Field, Bookmark,
               Footnote, Picture, Shape, EmbeddedObject> value;
};

struct Paragraph { ParagraphProps props; std::vector<Inline> listText; std::vector<Inline> runs; };

enum class WidthType { Auto, Percent, Twips };
struct PreferredWidth { WidthType type = WidthType::Auto; int value = 0; };
enum class VerticalAlign { Top, Center, Bottom };
enum class TextDirection { Lrtb, Tbrl, Btlr, Lrtbv, Tbrlv };
enum class Merge { First, Continued };
enum class HeightRule { Auto, AtLeast, Exact };

struct Cell {
  int right = 0;  // \cellx
  int width = 0;  // right minus the previous cell's right, or minus the row's left
  std::optional<PreferredWidth> preferredWidth;
  std::optional<Borders> borders;
  std::optional<Shading> shading;
  std::optional<VerticalAlign> verticalAlign;
  std::optional<TextDirection> textDirection;
  std::optional<Padding> padding;
  std::optional<Merge> mergeHorizontal, mergeVertical;
  bool noWrap = false;
  std::vector<Block> blocks;
};
struct RowProps {
  bool header = false, keep = false;
  std::optional<Align> align;
  std::optional<int> left, height;
  std::optional<HeightRule> heightRule;
  std::optional<int> gapHalf;
  std::optional<Padding> padding;
  std::optional<Borders> borders;
  bool rtl = false;
};
struct Row { RowProps props; std::vector<Cell> cells; };
struct Table { std::vector<Row> rows; };

struct Block { std::variant<Paragraph, Table> value; };

enum class SectionBreak { Page, None, Column, Even, Odd };
struct HeaderSet { std::optional<std::vector<Block>> default_, first, left, right; };
struct Section {
  PageSetup page;
  SectionBreak break_ = SectionBreak::Page;
  std::optional<int> columns;
  bool titlePage = false;
  std::optional<int> pageNumberStart;
  HeaderSet headers, footers;
  std::vector<Block> body;
};

struct RtfDocument {
  int version = 1;
  std::optional<std::string> generator;
  std::map<std::string, std::string> info;
  int codePage = 1252;
  int defaultFont = 0;
  std::optional<int> defaultLanguage;
  std::map<int, Font> fonts;  // key = RTF font number
  std::vector<std::optional<Color>> colors;  // nullopt = automatic
  std::vector<Style> styles;
  PageSetup page;
  std::vector<Section> sections;
  std::vector<Warning> warnings;
};

}  // namespace rtf
