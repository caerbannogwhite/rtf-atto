# RTF reader implementation plan (rtf-atto, plan 1 of 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the reader of the C++ core in the `rtf-atto` repository: RTF bytes in, the document model out as C++ structs and as canonical JSON, with the logical-table helper, the plain-text projection, the `rtf2json` command, hand-written fixtures with goldens, the independent text extractor, the mutation test and the local samples check.

**Architecture:** A tokenizer turns bytes into tokens. The reader runs a group-state machine over them: each `{` pushes a copy of the formatting state and of the current destination; known destinations route text into the right container (section body, header, footnote, field, cell); unknown `\*` destinations are skipped with a warning; a table builder per nesting level assembles rows and cells from `\trowd`, `\cellx`, `\cell` and `\row`. The model is plain structs. Separate passes write JSON, project text and stitch logical tables. Standard library only, no exceptions, no RTTI, linear time, bounded depth.

**Tech Stack:** C++17, CMake 3.20+, CTest, Python 3 for the tools and for the independent extractor, GitHub Actions.

**Spec:** `docs/plans/2026-11-10-rtf-parser-design.md` in this repository (the-stats-duck keeps an identical copy at `notes/plans/2026-11-10-rtf-parser-design.md`). The spec is the authority; where this plan and the spec disagree, the spec is right and the plan gets corrected.

## Global constraints

- C++17 and the standard library only. No Eigen, no DuckDB, no JSON library, no test framework.
- No exceptions and no RTTI. GCC and Clang builds compile the library, the tools and the tests with `-fno-exceptions -fno-rtti`. MSVC builds with its default flags (the no-exceptions proof runs on GCC and Clang in CI).
- Warnings are errors: `-Wall -Wextra -Wpedantic -Werror` on GCC and Clang, `/W4 /WX /permissive-` on MSVC.
- Linear time in the input size; no re-parse per output chunk.
- Bounded memory and depth: group nesting is capped at 1024 (`ParseOptions::maxDepth`); `\binN` is clamped to the bytes remaining; a `\u` fallback skip never crosses a brace or a control word.
- No globals and no static mutable state in the library. A parse is a function call on a byte range.
- Licence Apache-2.0. The `LICENSE` file is the-stats-duck's `LICENSE` copied verbatim (same holder line, `Copyright 2026 Massimo Meneghello`).
- Plain English in code comments, commit messages and documents (`~/.claude/CLAUDE.md`).
- Model version 1. The JSON shape is the TypeScript shape in the spec section "The document model", as amended on 2026-11-16: `fonts` keyed by RTF font number, colour properties never `null`, `Shape.picture`.
- Lengths are twips, font sizes half-points, every number in the model an integer. Properties at their default value are omitted from the JSON.
- Canonical JSON is exactly what Python prints for `json.dumps(obj, ensure_ascii=False, indent=2, sort_keys=True) + "\n"`: two-space indentation, keys sorted by byte value, `[]` and `{}` for empty containers, raw UTF-8 for non-ASCII, the escapes `\"`, `\\`, `\n`, `\r`, `\t`, `\b`, `\f` and `\u00XX` (lower-case hex) for the other control characters.
- Eleven of the twelve SAS-made files in `C:\Users\massi\Downloads\RTF` are never copied into the repository, neither bytes nor text. Structure learned from them (which control words, in which order) may be used, and every hand-written fixture's text is invented. The three Word-saved files `W_Disposition.rtf`, `W_Features.rtf` and `W_Sections.rtf` and the SAS file `T_Disposition.RTF` are copied byte for byte as fixtures (Massimo's permission, 2026-11-16).
- Fixture files are binary for git: `.gitattributes` says `*.rtf -text`; goldens and scripts are `text eol=lf`.
- Commit messages: one imperative sentence in plain English, no type prefix, an optional body, and the line `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` at the end. Commits go to `main` during plan 1; nothing is pushed until Massimo says so.

## Review focus

1. **Word's `\headerr` as the only header, no `\facingp`.** Every Word sample has it. The model must carry it as `right` and the text projection must include it; a consumer picks `default`, else `right`, else `left`. Test `headers_all_kinds` in Task 6.
2. **The `\u` fallback skip eating a brace or a control word.** `{\u8364}x` must give "€x" and `\u8364\par` must still end the paragraph. Tests `uc_skip_stops_at_brace` and `uc_skip_stops_at_control` in Task 4.
3. **Cell count different from the definition count**, including Word's second row definition changing the count and a `\cell` with no `\trowd` at all: pad or truncate per the spec, warn, never index out of range. Test `fewer_and_more_cells` in Task 5.
4. **Picture hex with CRLF inside and an odd trailing digit.** Word breaks hex lines every 128 characters. Bytes up to the error are kept, one `bad_picture_data` warning, no crash. Test `picture_hex_with_line_breaks_and_odd_digit` in Task 6.
5. **`\sectd` repeated without `\sect`.** Word writes three `\sectd` for one section. One section, headers kept. Test `sectd_without_sect_is_one_section` in Task 6.

## Repository and conventions

- Local clone: `C:\Users\massi\source\repos\rtf-atto`, branch `main`, remote `origin` = https://github.com/caerbannogwhite/rtf-atto.git (public, empty until the first push).
- Namespace `rtf`; internal code in `rtf::detail`. Types PascalCase, functions and locals snake_case, struct members camelCase equal to the JSON keys (`marginTop`, `indentLeft`), with a trailing underscore where the key is a C++ keyword (`break_`, `default_`).
- Each test is a plain C++ executable over `test/cpp/check.hpp`. Its exit code is 0 when every check passed. Test functions are named after the behaviour they pin and each file ends with a `main` that calls them in order.
- RTF inputs in tests are raw string literals: `R"({\rtf1\ansi Hello\par})"`.
- Commands below are for Git Bash on Massimo's machine with forward slashes. Configure: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`. Build: `cmake --build build --config Release -j 8`. Test: `ctest --test-dir build -C Release --output-on-failure`. The default generator is Visual Studio 17 2022, so `-C Release` is required. The second local compiler check is `cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++` followed by the same build and test commands without `-C`.
- Massimo disabled his antivirus on 2026-11-10 because it blocked freshly compiled executables. If a test executable fails with "Access is denied" right after linking, that is the cause; retry once before debugging.

## File structure

```
rtf-atto/
  CMakeLists.txt                 project, options, library rtf_atto, tests, tools
  LICENSE                        Apache-2.0
  README.md                      what it is, how to build and test, status
  .gitignore                     build*/, mutant_failed.rtf
  .gitattributes                 *.rtf -text; *.json *.txt *.py *.sh text eol=lf
  .github/workflows/ci.yml       Linux GCC, macOS Clang, Windows MSVC, Linux sanitizers
  docs/plans/                    the design document and the plans
  include/rtf/version.hpp        RTF_ATTO_VERSION, version()
  include/rtf/model.hpp          the document model structs (Task 1)
  include/rtf/reader.hpp         ParseOptions, ParseResult, parse()
  include/rtf/json.hpp           to_json(document), to_json(logical tables)
  include/rtf/text.hpp           to_text(), inline_text(), cell_text()
  include/rtf/tables.hpp         LogicalTable, logical_tables()
  src/version.cpp                version()
  src/tokenizer.hpp, .cpp        bytes -> tokens (rtf::detail)
  src/encodings.hpp, .cpp        charset table, code pages, UTF-8 output
  src/codepages.inc              generated single-byte tables (do not edit)
  src/properties.hpp             CharState, ParaState, SectionState, BorderTarget
  src/builder.hpp, .cpp          BlockSink and TableLevel: paragraphs, cells, rows
  src/reader.cpp                 the group-state machine and the destinations
  src/json.cpp                   JsonWriter, base64, the model -> JSON mapping
  src/text.cpp                   the text projection
  src/tables.cpp                 logical tables
  tools/rtf2json.cpp             the command: json | text | tables | summary
  tools/gen_codepages.py         writes src/codepages.inc from Python's codecs
  test/cpp/check.hpp             CHECK, CHECK_EQ, rtf_test::result()
  test/cpp/helpers.hpp           parse_ok(), body_paragraph(), run_text(), ...
  test/cpp/test_*.cpp            one executable per area
  test/fixtures/*.rtf            hand-written or generated fixtures, plus three Word-saved files and one SAS file
  test/fixtures/*.json           golden document JSON per fixture
  test/fixtures/*.tables.json    golden logical tables per fixture
  test/tools/make_fixtures.py    writes the fixtures that hold non-text bytes
  test/tools/regen_goldens.py    rewrites the goldens and checks canonical form
  test/tools/rtf_text.py         the independent extractor (Task 10, separate author)
  test/tools/compare_text.py     normalises and compares two text projections
  test/tools/check_text_coverage.py   runs rtf2json --text against rtf_text.py
  test/local/samples.sh          the local-only check over RTF_SAMPLES_DIR
  test/fuzz/fuzz_parse.cpp       libFuzzer entry point, built with -DRTF_FUZZ=ON
```

## Warning codes

One warning per code and name per document unless stated. `offset` is the byte offset of the token that caused it.

| code | when | message content |
|---|---|---|
| `unknown_destination` | a `\*` destination the reader does not know is skipped | the control word |
| `skipped_destination` | a known destination that the model does not carry is skipped (`\*\themedata`, `\nonesttables`, `\shprslt`, ...) | the control word |
| `unbalanced_group` | a missing `}` at the end or an extra `}` (every occurrence) | which of the two |
| `depth_limit` | nesting above `maxDepth`; deeper groups are flattened | the depth |
| `truncated_control` | the input ends inside a control word or after a lone `\` | none |
| `param_overflow` | a control word parameter outside 32-bit signed range, clamped | the control word |
| `bin_truncated` | `\binN` with fewer than N bytes left | N and the bytes left |
| `unsupported_code_page` | a byte decoded under code page 932, 936, 949, 950, 1361 or an unknown one; U+FFFD emitted | the code page |
| `symbol_font` | text decoded under a Symbol-charset font (U+F000 + byte) | the font number |
| `row_not_closed` | cells pending without `\row` at the end of a table, section or input (every occurrence) | the level |
| `cell_count_mismatch` | cells and `\cellx` definitions differ in number in a row (every occurrence) | both counts |
| `bad_picture_data` | a non-hex character or an odd digit count in picture data | the character |
| `unknown_picture_format` | `\pict` without a known format word | none |
| `object_data_dropped` | `\*\objdata` skipped | none |
| `shape_geometry_dropped` | a `\shp` read; only its text and picture are kept | none |

## The text projection

Shared by `to_text` (Task 8) and `rtf_text.py` (Task 10). Both are compared after `compare_text.py` normalises them (split on "\n", strip each line of leading and trailing Unicode whitespace, drop empty lines), so only content, order, tabs between cells and line structure must agree.

From the model (`to_text`):

- A document is its sections in order. A section is: `headers.default_`, `headers.first`, `headers.left`, `headers.right`, then the same four footers, then `body`, each a block list.
- A block list is its blocks in order. A Paragraph is `listText` projected, then `runs` projected, then "\n". A Table is its rows; a row is its cells each followed by "\t", then "\n"; a cell is its block list projected, except that the last paragraph of a cell contributes no trailing "\n".
- Inlines: `TextRun` its text; `Tab` "\t"; `LineBreak` "\n"; `PageBreak` "\f"; `ColumnBreak` "\n"; `Field` its `result` projected (never the instruction); `Bookmark` nothing; `Footnote` its blocks projected (so the note's text appears in place); `Picture` nothing; `Shape` its blocks; `EmbeddedObject` its result blocks.

From the RTF stream (`rtf_text.py`), equivalently: decoded text in order; `\tab` and a raw tab "\t"; `\cell` and `\nestcell` "\t"; `\row` and `\nestrow` "\n"; `\par`, `\line` and `\sect` "\n"; `\page` "\f"; `\column` "\n"; the special-character words as the spec maps them; `\chpgn` and `\chftn` nothing; text inside `\*\fldinst` nothing, inside `\fldrslt` kept; `\listtext` and `\pntext` text kept where it stands; header and footer groups kept where they stand; `\footnote` text kept where it stands, followed by "\n" if the group emitted anything after its last "\n"; `\shptxt` and `\result` likewise; the skipped destinations of the spec (and `\nonesttables`, `\shprslt`, `\nonshppict`, `\mmathPr`, `\*\datafield`, `\*\objdata`, `\*\picprop`, `\*\blipuid`) nothing; `\pict` nothing; `\info`, `\fonttbl`, `\colortbl`, `\stylesheet`, `\*\generator` nothing; inside `\upr` only the `\*\ud` branch.

---

### Task 1: Repository scaffold and the document model

**Files:**
- Create: `CMakeLists.txt`, `LICENSE`, `README.md`, `.gitignore`, `.gitattributes`
- Create: `include/rtf/version.hpp`, `include/rtf/model.hpp`, `src/version.cpp`
- Create: `test/cpp/check.hpp`, `test/cpp/test_model.cpp`
- Already present: `docs/plans/2026-11-10-rtf-parser-design.md`, `docs/plans/2026-11-16-rtf-reader-implementation.md`

**Interfaces:**
- Produces: every type in `model.hpp` below, used by all later tasks unchanged. `RTF_ATTO_VERSION` is `"0.1.0"`; `const char* rtf::version()`.
- Produces: the CMake function `rtf_add_test(<name> [args...])`, which builds `test/cpp/test_<name>.cpp` into `test_<name>`, links `rtf_atto`, and registers CTest test `<name>` with the given arguments.
- Produces: `CHECK(cond)`, `CHECK_EQ(a, b)` (ints, `std::size_t`, bools, `std::string`, C strings; prints both values on failure) and `int rtf_test::result()` (0 when no check failed, else 1).

- [ ] **Step 1: Write the failing test `test/cpp/test_model.cpp`**

```cpp
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
```

- [ ] **Step 2: Write `CMakeLists.txt`, `check.hpp`, `.gitignore`, `.gitattributes`, `LICENSE`, `README.md`**

`CMakeLists.txt`: `cmake_minimum_required(VERSION 3.20)`, `project(rtf-atto VERSION 0.1.0 LANGUAGES CXX)`, `CMAKE_CXX_STANDARD 17` required, extensions off; options `RTF_BUILD_TESTS` (ON when top-level), `RTF_BUILD_TOOLS` (ON), `RTF_SANITIZE` (OFF: adds `-fsanitize=address,undefined -fno-sanitize-recover=all` to compile and link flags), `RTF_FUZZ` (OFF, Task 11); an interface target `rtf_atto_flags` carrying the warning flags and, on GCC and Clang, `-fno-exceptions -fno-rtti`; `add_library(rtf_atto STATIC src/version.cpp)` with `target_include_directories(rtf_atto PUBLIC include PRIVATE src)` linking `rtf_atto_flags`; `enable_testing()`; the `rtf_add_test` function; `rtf_add_test(model)`.

`check.hpp`: a failure counter behind `rtf_test::failures()`, `CHECK` printing `file:line: CHECK failed: <expr>` to stderr, `CHECK_EQ` printing `file:line: CHECK_EQ failed: <a> != <b>` with both values through overloads of a `to_string` helper, and `rtf_test::result()`.

`LICENSE`: copy `C:/Users/massi/source/repos/kolistat/the-stats-duck/LICENSE` byte for byte.

`README.md`: the name, one paragraph in plain English (an RTF reader and writer in C++17 for SAS-made clinical tables and general RTF, with a WebAssembly build and a TypeScript renderer to come), the build and test commands from "Repository and conventions", the sentence "Status: plan 1 (the reader) in progress; see docs/plans/", the licence line.

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release -j 8`
Expected: the build fails because `rtf/model.hpp` does not exist.

- [ ] **Step 4: Write `include/rtf/version.hpp`, `src/version.cpp` and `include/rtf/model.hpp`**

`version.hpp`: `#define RTF_ATTO_VERSION "0.1.0"` and `namespace rtf { const char* version(); }`; `version.cpp` returns the macro.

`model.hpp`, complete; later tasks add nothing to it except where a task says so:

```cpp
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
```

`std::vector` of an incomplete type (`Block` inside `Footnote`, `Cell`, `Shape`, `EmbeddedObject`; `Inline` inside `Field`) is allowed in C++17 and compiles on GCC 10, Clang and MSVC 2022. If a compiler rejects it, wrap the recursive vectors in a small `struct Blocks { std::vector<Block> items; }` defined after `Block`; do not switch to pointers.

- [ ] **Step 5: Build and run the test**

Run: `cmake --build build --config Release -j 8 && ctest --test-dir build -C Release --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 6: Build with TDM-GCC too**

Run: `cmake -S . -B build-gcc -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ && cmake --build build-gcc -j 8 && ctest --test-dir build-gcc --output-on-failure`
Expected: the same, compiled with `-fno-exceptions -fno-rtti -Werror`.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt LICENSE README.md .gitignore .gitattributes include/rtf/version.hpp include/rtf/model.hpp src/version.cpp test/cpp/check.hpp test/cpp/test_model.cpp docs/plans/2026-11-10-rtf-parser-design.md docs/plans/2026-11-16-rtf-reader-implementation.md
git commit -m "Add the document model, the build and the design documents"
```

(If the two documents were committed before this task started, leave them out of the `git add`.)

---

### Task 2: Tokenizer

**Files:**
- Create: `src/tokenizer.hpp`, `src/tokenizer.cpp`, `test/cpp/test_tokenizer.cpp`
- Modify: `CMakeLists.txt` (add the source, `rtf_add_test(tokenizer)`)

**Interfaces:**
- Produces, in `rtf::detail`:

```cpp
enum class TokenKind { GroupOpen, GroupClose, ControlWord, ControlSymbol, HexByte, Unicode, Binary, Text, End };
struct Token {
  TokenKind kind = TokenKind::End;
  std::string_view word;        // ControlWord: the letters, without the backslash
  bool hasParam = false;
  std::int32_t param = 0;       // ControlWord and Unicode
  unsigned char symbol = 0;     // ControlSymbol: the byte; HexByte: the value
  std::string_view text;        // Text and Binary: the bytes
  std::size_t offset = 0;       // byte offset of the token's first byte
  bool clamped = false;         // param_overflow, bin_truncated, truncated_control
};
class Tokenizer {
 public:
  Tokenizer(const unsigned char* data, std::size_t size);
  Token next();                 // End forever once the input is consumed
  std::size_t position() const;
};
```

Rules (from the spec's "Tokenizer"), pinned by the tests: a control word is `\` and one or more ASCII letters, then an optional `-` and digits; the digits are parsed as a 32-bit signed integer and clamped to its range with `clamped = true` when they overflow; one space after the control word or its parameter is consumed, any other delimiter is not. `\'hh` is `HexByte` (a non-hex digit after `\'` gives `ControlSymbol '\''` and the following bytes are text). `\uN` is `Unicode` with the signed parameter. `\binN` consumes the one space and then N raw bytes (negative N reads as 0; fewer bytes left clamps with `clamped = true`). Any other `\` plus a non-letter byte is `ControlSymbol`, except `\` followed by CR or LF, which is `ControlWord "par"`. A raw tab byte is `ControlWord "tab"`. CR and LF outside `\bin` end a `Text` token and are dropped. A `Text` token is a maximal run of bytes that are not `\`, `{`, `}`, CR, LF or tab. A `\` as the last byte gives `End` with `clamped = true`.

- [ ] **Step 1: Write the failing tests** in `test/cpp/test_tokenizer.cpp`: a helper `std::vector<Token> all(std::string_view)` that collects tokens until `End`, then:

```cpp
static void words_symbols_and_groups() {
  auto t = all(R"({\rtf1\ansi\b0 x\par})");
  CHECK_EQ(t.size(), std::size_t(7));
  CHECK(t[0].kind == TokenKind::GroupOpen);
  CHECK(t[1].kind == TokenKind::ControlWord); CHECK_EQ(std::string(t[1].word), std::string("rtf"));
  CHECK(t[1].hasParam); CHECK_EQ(t[1].param, 1);
  CHECK(!t[2].hasParam);
  CHECK_EQ(t[3].param, 0); CHECK(t[3].hasParam);
  CHECK(t[4].kind == TokenKind::Text); CHECK_EQ(std::string(t[4].text), std::string("x"));  // the space after \b0 is consumed
  CHECK_EQ(std::string(t[5].word), std::string("par"));
  CHECK(t[6].kind == TokenKind::GroupClose);
}
static void hex_unicode_and_symbols() {
  auto t = all(R"(\'e9\u8364?\u-10179\*\{\}\\\~\'zz)");
  CHECK(t[0].kind == TokenKind::HexByte); CHECK_EQ(int(t[0].symbol), 0xE9);
  CHECK(t[1].kind == TokenKind::Unicode); CHECK_EQ(t[1].param, 8364);
  CHECK(t[2].kind == TokenKind::Text); CHECK_EQ(std::string(t[2].text), std::string("?"));
  CHECK_EQ(t[3].param, -10179);
  CHECK(t[4].kind == TokenKind::ControlSymbol); CHECK_EQ(int(t[4].symbol), int('*'));
  CHECK_EQ(int(t[5].symbol), int('{')); CHECK_EQ(int(t[6].symbol), int('}')); CHECK_EQ(int(t[7].symbol), int('\\'));
  CHECK_EQ(int(t[8].symbol), int('~'));
  CHECK(t[9].kind == TokenKind::ControlSymbol); CHECK_EQ(int(t[9].symbol), int('\''));
  CHECK_EQ(std::string(t[10].text), std::string("zz"));
}
static void line_ends_tabs_and_backslash_newline() {
  auto t = all("a\r\nb\tc\\\nd");
  CHECK_EQ(std::string(t[0].text), std::string("a"));
  CHECK_EQ(std::string(t[1].text), std::string("b"));
  CHECK_EQ(std::string(t[2].word), std::string("tab"));
  CHECK_EQ(std::string(t[3].text), std::string("c"));
  CHECK_EQ(std::string(t[4].word), std::string("par"));
  CHECK_EQ(std::string(t[5].text), std::string("d"));
}
static void binary_and_clamping() {
  auto t = all("\\bin3 ab{d");          // 3 bytes: 'a' 'b' '{'
  CHECK(t[0].kind == TokenKind::Binary); CHECK_EQ(std::string(t[0].text), std::string("ab{"));
  CHECK(!t[0].clamped);
  CHECK_EQ(std::string(t[1].text), std::string("d"));
  auto u = all("\\bin10 abc");
  CHECK_EQ(std::string(u[0].text), std::string("abc")); CHECK(u[0].clamped);
  auto v = all("\\bin-5 abc");
  CHECK(v[0].kind == TokenKind::Binary); CHECK(v[0].text.empty()); CHECK_EQ(std::string(v[1].text), std::string("abc"));
  auto w = all("\\fs99999999999 x");
  CHECK_EQ(w[0].param, 2147483647); CHECK(w[0].clamped);
  auto x = all("\\fs-99999999999 x");
  CHECK_EQ(x[0].param, -2147483647 - 1); CHECK(x[0].clamped);
}
static void letters_only_and_offsets() {
  auto t = all("\\f1a \\par");
  CHECK_EQ(std::string(t[0].word), std::string("f")); CHECK_EQ(t[0].param, 1);
  CHECK_EQ(std::string(t[1].text), std::string("a "));
  CHECK_EQ(t[2].offset, std::size_t(5));
}
static void trailing_backslash() {
  Tokenizer tk(reinterpret_cast<const unsigned char*>("ab\\"), 3);
  Token a = tk.next(); CHECK(a.kind == TokenKind::Text);
  Token b = tk.next(); CHECK(b.kind == TokenKind::End); CHECK(b.clamped);
  CHECK(tk.next().kind == TokenKind::End);
}
```

- [ ] **Step 2: Run to verify the build fails** (`rtf_add_test(tokenizer)` added, `src/tokenizer.hpp` missing).

- [ ] **Step 3: Implement `Tokenizer` in `src/tokenizer.hpp` and `src/tokenizer.cpp`**

A single forward pass with an index; no allocation; `std::string_view` into the input. Parameter parsing accumulates in `std::int64_t` and stops accumulating after 11 digits to avoid overflow of the accumulator itself.

- [ ] **Step 4: Build and run**: `ctest --test-dir build -C Release --output-on-failure -R tokenizer`. Expected: pass.

- [ ] **Step 5: Commit**: `git add src/tokenizer.hpp src/tokenizer.cpp test/cpp/test_tokenizer.cpp CMakeLists.txt && git commit -m "Add the tokenizer"`.

---

### Task 3: Encodings

**Files:**
- Create: `tools/gen_codepages.py`, `src/codepages.inc`, `src/encodings.hpp`, `src/encodings.cpp`, `test/cpp/test_encodings.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces, in `rtf::detail`:

```cpp
constexpr int kSymbolCodePage = 42;                       // Word's value for the Symbol charset
int charset_to_code_page(int charset, int documentCodePage);   // the spec's table; unknown charsets -> documentCodePage
bool code_page_supported(int codePage);                   // 1250-1258, 874, 437, 850, 852, 866, 10000, kSymbolCodePage
std::uint32_t decode_byte(int codePage, unsigned char b);  // b < 0x80 -> b; Symbol -> 0xF000 + b; undefined or unsupported -> 0xFFFD
void append_utf8(std::string& out, std::uint32_t codePoint);  // surrogates and > 0x10FFFF become U+FFFD
```

- [ ] **Step 1: Write the failing tests** in `test/cpp/test_encodings.cpp`:

```cpp
static void charset_table() {
  CHECK_EQ(charset_to_code_page(0, 1252), 1252);
  CHECK_EQ(charset_to_code_page(1, 1250), 1250);
  CHECK_EQ(charset_to_code_page(2, 1252), kSymbolCodePage);
  CHECK_EQ(charset_to_code_page(77, 1252), 10000);
  CHECK_EQ(charset_to_code_page(128, 1252), 932);
  CHECK_EQ(charset_to_code_page(129, 1252), 949);
  CHECK_EQ(charset_to_code_page(130, 1252), 1361);
  CHECK_EQ(charset_to_code_page(134, 1252), 936);
  CHECK_EQ(charset_to_code_page(136, 1252), 950);
  CHECK_EQ(charset_to_code_page(161, 1252), 1253);
  CHECK_EQ(charset_to_code_page(162, 1252), 1254);
  CHECK_EQ(charset_to_code_page(163, 1252), 1258);
  CHECK_EQ(charset_to_code_page(177, 1252), 1255);
  CHECK_EQ(charset_to_code_page(178, 1252), 1256);
  CHECK_EQ(charset_to_code_page(186, 1252), 1257);
  CHECK_EQ(charset_to_code_page(204, 1252), 1251);
  CHECK_EQ(charset_to_code_page(222, 1252), 874);
  CHECK_EQ(charset_to_code_page(238, 1252), 1250);
  CHECK_EQ(charset_to_code_page(254, 1252), 437);
  CHECK_EQ(charset_to_code_page(255, 1252), 850);
  CHECK_EQ(charset_to_code_page(99, 1252), 1252);
}
static void single_byte_tables() {
  CHECK_EQ(decode_byte(1252, 0x41), 0x41u);
  CHECK_EQ(decode_byte(1252, 0x80), 0x20ACu);
  CHECK_EQ(decode_byte(1252, 0x81), 0xFFFDu);
  CHECK_EQ(decode_byte(1252, 0x9F), 0x0178u);
  CHECK_EQ(decode_byte(1252, 0xE9), 0x00E9u);
  CHECK_EQ(decode_byte(1250, 0xA5), 0x0104u);
  CHECK_EQ(decode_byte(1251, 0xC0), 0x0410u);
  CHECK_EQ(decode_byte(1253, 0xC1), 0x0391u);
  CHECK_EQ(decode_byte(1254, 0xDD), 0x0130u);
  CHECK_EQ(decode_byte(1255, 0xE0), 0x05D0u);
  CHECK_EQ(decode_byte(1256, 0xC7), 0x0627u);
  CHECK_EQ(decode_byte(1257, 0xC0), 0x0104u);
  CHECK_EQ(decode_byte(1258, 0xD5), 0x01A0u);
  CHECK_EQ(decode_byte(874, 0xA1), 0x0E01u);
  CHECK_EQ(decode_byte(437, 0x80), 0x00C7u);
  CHECK_EQ(decode_byte(437, 0xE1), 0x00DFu);
  CHECK_EQ(decode_byte(850, 0x9E), 0x00D7u);
  CHECK_EQ(decode_byte(852, 0x85), 0x016Fu);
  CHECK_EQ(decode_byte(866, 0x80), 0x0410u);
  CHECK_EQ(decode_byte(10000, 0xA5), 0x2022u);
  CHECK_EQ(decode_byte(10000, 0xDB), 0x20ACu);
  CHECK_EQ(decode_byte(kSymbolCodePage, 0xB7), 0xF0B7u);
  CHECK_EQ(decode_byte(932, 0x82), 0xFFFDu);
  CHECK(code_page_supported(1252)); CHECK(!code_page_supported(932)); CHECK(!code_page_supported(65001));
}
static void utf8_output() {
  std::string s;
  append_utf8(s, 0x41); append_utf8(s, 0xE9); append_utf8(s, 0x20AC); append_utf8(s, 0x1F605);
  CHECK_EQ(s, std::string("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x85"));
  s.clear(); append_utf8(s, 0xD800); append_utf8(s, 0x110000);
  CHECK_EQ(s, std::string("\xEF\xBF\xBD\xEF\xBF\xBD"));
}
```

- [ ] **Step 2: Run to verify the build fails.**

- [ ] **Step 3: Write `tools/gen_codepages.py` and generate `src/codepages.inc`**

The script maps each code page to Python's codec name (`cp1250` ... `cp1258`, `cp874`, `cp437`, `cp850`, `cp852`, `cp866`, `mac_roman`), decodes each byte 0x80 to 0xFF alone, writes 0 for a byte the codec rejects, and emits `static const std::uint16_t kCp<N>[128] = { ... };` arrays plus a `static const std::uint16_t* code_page_table(int codePage)` returning `nullptr` for the rest. Header comment: "Generated by tools/gen_codepages.py; do not edit." Run: `python tools/gen_codepages.py > src/codepages.inc`.

- [ ] **Step 4: Implement `src/encodings.hpp` and `src/encodings.cpp`** (includes `codepages.inc`).

- [ ] **Step 5: Build and run** `-R encodings`. Expected: pass.

- [ ] **Step 6: Commit**: `git add tools/gen_codepages.py src/codepages.inc src/encodings.hpp src/encodings.cpp test/cpp/test_encodings.cpp CMakeLists.txt && git commit -m "Add the code page tables and the text decoder"`.

---

### Task 4: Reader core: header tables, groups, paragraphs, runs, text decoding

**Files:**
- Create: `include/rtf/reader.hpp`, `src/properties.hpp`, `src/builder.hpp`, `src/builder.cpp`, `src/reader.cpp`, `test/cpp/helpers.hpp`, `test/cpp/test_reader_text.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces, public:

```cpp
namespace rtf {
struct ParseOptions { std::size_t maxDepth = 1024; };
struct ParseResult { bool ok = false; std::string error; RtfDocument document; };   // error: "not_rtf"
ParseResult parse(const unsigned char* data, std::size_t size, const ParseOptions& options = {});
ParseResult parse(std::string_view bytes, const ParseOptions& options = {});
}
```

- Produces, internal (`src/builder.hpp`, used by Tasks 5 and 6):

```cpp
namespace rtf::detail {
struct TableLevel {          // one per nesting level, index 1 = \intbl
  RowProps rowProps;         // the definition in effect
  std::vector<Cell> cellDefs;   // from \cellx, without blocks
  std::vector<Cell> cells;      // closed cells of the row being built
  std::vector<Block> openCell;  // blocks of the cell being built
  std::vector<Row> rows;        // rows of the table being built
  bool pending() const;         // cells or openCell not empty
};
struct BlockSink {           // where blocks go: a body, a header, a footnote, a shape's text, an object's result
  std::vector<Block>* target;
  std::optional<Paragraph> current;   // the paragraph being built
  std::vector<TableLevel> levels;     // index 0 unused
  // end_paragraph(props, level), end_cell(level), end_row(level, warnings), flush_tables(level, warnings), close(warnings)
};
}
```

- Produces, test helpers (`test/cpp/helpers.hpp`): `rtf::RtfDocument parse_ok(std::string_view rtf)` (CHECKs `ok`), `const rtf::Paragraph& body_paragraph(const rtf::RtfDocument&, std::size_t section, std::size_t block)`, `std::string run_text(const rtf::Paragraph&)` (concatenated TextRun text), `const rtf::TextRun& run(const rtf::Paragraph&, std::size_t i)`, `bool has_warning(const rtf::RtfDocument&, const char* code)`, `std::size_t count_warnings(const rtf::RtfDocument&, const char* code)`.

Behaviour this task implements (the spec's "Group state", "Destinations the reader understands" for the document tables, "Text decoding", "Paragraphs and runs", plus these decisions):

- The input may start with a UTF-8 BOM and whitespace; then it must start with `{\rtf`. Anything else, including an empty input, is `ok = false`, `error = "not_rtf"`, and the document is empty.
- `\rtf`, `\ansi` (1252), `\mac` (10000), `\pc` (437), `\pca` (850), `\ansicpgN`, `\deffN`, `\deflangN`, `\ucN`, `\paperw`, `\paperh`, `\margl`, `\margr`, `\margt`, `\margb`, `\landscape`, `\facingp` fill the document. Section words are Task 6; the first section exists from the start.
- A group's destination is decided by its first control word (after an optional `\*`). An unknown word after `\*` skips the group with `unknown_destination`. Known skipped destinations (the spec's list plus `\nonesttables`, `\shprslt`, `\nonshppict`, `\mmathPr`, and the Word words `\*\pnseclvl`, `\*\defchp`, `\*\defpap`, `\*\lsdlockedexcept`, `\*\aftnsep`, `\*\aftnsepc`, `\*\ftnsepc`) skip with `skipped_destination`. Anything else, including an unknown word without `\*`, is a formatting group whose text is kept.
- `\fonttbl`: entries in sub-groups or bare, each started by `\fN`; `\fnil \froman \fswiss \fmodern \fscript \fdecor \ftech \fbidi`, `\fcharsetN`, `\fprqN` (0 default, 1 fixed, 2 variable), `\cpgN`; `\*\falt`, `\*\panose`, `\*\fname` skipped silently; `\flomajor` and the other theme words ignored; the name is the entry's text without the trailing `;`, trimmed.
- `\colortbl`: entries split at `;`; `\redN \greenN \blueN`; an entry with none of them is `nullopt`; `\caccentone`, `\ctint`, `\cshade` and the other theme words ignored.
- `\stylesheet`: each sub-group is a style; `\sN` paragraph, `\*\csN` character, `\*\tsN` table, `\dsN` section, none of them paragraph 0; `\sbasedonN`, `\snextN`; the name is the text without the trailing `;`. Other `\*` sub-groups inside the stylesheet are skipped silently.
- `\info`: each sub-group `{\word text}` sets `info[word] = text` trimmed; `\creatim`, `\revtim`, `\printim`, `\buptim` become `YYYY-MM-DDTHH:MM:SS` from `\yr \mo \dy \hr \min \sec` (missing parts are 0; a group without `\yr` is dropped); a sub-group with a parameter and no text (`{\nofpages1}`) stores the number as a string; `\*` sub-groups are skipped silently. `{\*\generator text;}` sets `generator` without the `;`.
- Text decoding per the spec: `\'hh` and raw bytes above 0x7F through the current font's code page (`Font::codePage`, else `charset_to_code_page(charset, document code page)`, else the document code page when the font is unknown); Symbol fonts to U+F000 + byte with `symbol_font`; unsupported code pages to U+FFFD with `unsupported_code_page`; `\uN` with N below 0 plus 65536, surrogate pairs combined, then skip `uc` fallback units (a text byte or one `\'hh` each) stopping at a brace or any control word; `\ucN` is group-scoped, default 1.
- Special characters per the spec's table, plus `\{ \} \\` as text, `\tab` and a raw tab as `Tab`, `\line` as `LineBreak`, `\page` as `PageBreak`, `\column` as `ColumnBreak`, `\chpgn` as `Field{"PAGE", {}}`, `\chftn` as nothing, `\lbrN` ignored.
- Character properties: `\f \fs \b \i \ul \ulnone \ul0 \uld \uldb \ulw \ulth \ulwave \uldash \strike \striked \super \sub \nosupersub \up \dn \cf \cb \chcbpat \highlight \caps \scaps \v \outl \shad \lang \deleted \revised \plain`; `\bN` with N = 0 turns a toggle off; `\ulN` with N not 0 is single. `\af`, `\afs`, `\alang`, `\loch`, `\hich`, `\dbch`, `\rtlch`, `\ltrch`, `\fcs`, `\insrsid` and every other unknown word are ignored.
- Paragraph properties: `\ql \qc \qr \qj \li \ri \fi \sb \sa \sl \slmult \keep \keepn \pagebb \widctlpar \nowidctlpar \s \ls \ilvl \rtlpar \ltrpar \intbl \itap`; tab stops `\tx` with the preceding `\tqc \tqr \tqdec \tb` and leaders `\tldot \tlhyph \tlul \tlth \tleq`; paragraph borders `\brdrt \brdrl \brdrb \brdrr \brdrbtw \brdrbar \box` filled by the border words; paragraph shading `\shading \cbpat \cfpat`. `\pard` resets them. The props a paragraph carries are the ones in effect when it ends.
- Border words `\brdrs \brdrdb \brdrdot \brdrdash \brdrth \brdrhair \brdrtriple \brdrnone \brdrwN \brdrcfN \brspN` fill the current border target (`properties.hpp`: `BorderTarget { enum Kind { None, Paragraph, Cell, Row } kind; side }`), set by the last border-opening word and cleared by `\pard`, `\trowd`, `\cellx`, `\cell`. Any other `\brdr*` word gives `style = Other`.
- Text goes to the current paragraph of the innermost `BlockSink` (opening one if none), as a `TextRun` with the current `CharProps`, merged into the previous run when the props are equal. `\par` ends the paragraph. A paragraph without runs and without `listText` that is closed by the end of its container (a `}` that closes a destination, `\cell`, the end of the input) is dropped unless its container has no other block.
- A missing `}` at the end closes every open group with one `unbalanced_group` warning each; an extra `}` is ignored with the same warning. Depth above `maxDepth` adds `depth_limit` once; the extra `{` and their `}` are counted and otherwise ignored. A `clamped` `End` token adds `truncated_control`; a clamped parameter adds `param_overflow`; a clamped `\bin` adds `bin_truncated`.
- `\bin` outside a picture or object is skipped silently.

- [ ] **Step 1: Write the failing tests** in `test/cpp/test_reader_text.cpp`:

```cpp
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
  auto doc = parse_ok(R"({\rtf1 {\f1\fs18\ul\strike\super\up6\cf1\highlight7\caps\v\lang1033 x}{\b\plain y}{\ul0 z}\par})");
  const auto& p = body_paragraph(doc, 0, 0);
  const auto& x = run(p, 0).props;
  CHECK_EQ(*x.font, 1); CHECK_EQ(*x.size, 18); CHECK(*x.underline == rtf::Underline::Single); CHECK(x.strike);
  CHECK(*x.script == rtf::Script::Super); CHECK_EQ(*x.offset, 6); CHECK_EQ(*x.color, 1); CHECK_EQ(*x.highlight, 7);
  CHECK(x.caps); CHECK(x.hidden); CHECK_EQ(*x.language, 1033);
  CHECK(!run(p, 1).props.bold);
  CHECK(!run(p, 2).props.underline.has_value());
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
```

- [ ] **Step 2: Run to verify the build fails** (`rtf_add_test(reader_text)` added).

- [ ] **Step 3: Implement `properties.hpp`, `builder.hpp/.cpp`, `reader.cpp`, `reader.hpp`**

Shape of `reader.cpp`: a `class Reader` holding the `Tokenizer`, the options, the document, a `std::vector<GroupState>` stack (`CharProps`, `ParaState` = `ParagraphProps` plus `intbl` and `itap`, the destination enum, `uc`, a pending `\*` flag, the `BorderTarget`, and which text target applies: the sink's paragraph, an inline vector, or a string), a `std::vector<BlockSink>` stack, and per-destination handlers. The main loop is one `switch` on the token kind; control words go through one lookup function mapping the word to a handler (a sorted static table of `{name, handler}` pairs with binary search, or a `switch` on the first letter followed by comparisons; no `std::unordered_map<std::string>`). The paragraph level for Task 5 is `itap > 0 ? itap : (intbl ? 1 : 0)`.

- [ ] **Step 4: Build and run** `-R reader_text`. Expected: pass.

- [ ] **Step 5: Run every test under the GCC build too** (`ctest --test-dir build-gcc --output-on-failure`). Expected: pass.

- [ ] **Step 6: Commit**: `git add include/rtf/reader.hpp src/properties.hpp src/builder.hpp src/builder.cpp src/reader.cpp test/cpp/helpers.hpp test/cpp/test_reader_text.cpp CMakeLists.txt && git commit -m "Add the reader core: groups, header tables, paragraphs and text decoding"`.

---

### Task 5: Reader tables

**Files:**
- Modify: `src/builder.hpp`, `src/builder.cpp`, `src/reader.cpp`
- Create: `test/cpp/test_reader_tables.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 4's `BlockSink` and `TableLevel`.
- Produces: tables in the model as the spec's "Tables" section says, plus these decisions:
  - Target level of the table words: `\trowd`, `\cellx`, the `\cl*` cell words, the `\tr*` row words and `\row` target level 1, except inside `{\*\nesttableprops ...}` where they target the paragraph level in effect (2 or more) and `\nestrow` closes that level's row. `\cell` closes a level-1 cell, `\nestcell` a cell of the paragraph level in effect.
  - A paragraph at level N (N > 0) ends into `levels[N].openCell`; a paragraph at level 0 ends into the sink's blocks after `flush_tables(1)`.
  - `\cell` (or `\nestcell`): end the current paragraph into the open cell (dropping it if empty and the cell already has blocks; an empty cell keeps one empty paragraph), flush any deeper level's table into the open cell first, then move the open cell to `cells` with the next unused `cellDefs` entry's properties.
  - `\row` (or `\nestrow`): if the current paragraph has runs, treat as `\cell` first; then pad with empty cells or extend with cells of the last definition's width (`cell_count_mismatch` when counts differ; a row with no definitions gives `right = 0`, `width = 0`); push the `Row` with `rowProps`; keep `cellDefs` and `rowProps` for a following row without `\trowd`.
  - A level-2 table is flushed into the level-1 open cell when a level-1 paragraph ends, when `\cell` closes the level-1 cell, or when the level-1 row ends. The same rule applies one level up at every depth.
  - `flush_tables(level)` also closes a pending row with `row_not_closed`.
  - `width` of the first cell is `right - rowProps.left.value_or(0)`; every other cell's width is `right - previous right`.
  - `\clpadtN` fills `padding.left`, `\clpadlN` `padding.top`, `\clpadbN` `bottom`, `\clpadrN` `right` (the producers' usage; see the spec); `\clpadfl \clpadft \clpadfb \clpadfr` are ignored (unit 3 = twips assumed). `\trpaddlN \trpaddtN \trpaddbN \trpaddrN` fill the row padding as named; `\trpaddf*` ignored.
  - `\trrhN`: N > 0 gives `height = N`, `heightRule = AtLeast`; N < 0 gives `height = -N`, `heightRule = Exact`; `\trrh0` leaves both absent.
  - `\clwWidthN` with `\clftsWidthN`: type 1 Auto, 2 Percent, 3 Twips (type 0 or absent: no `preferredWidth`).
  - Vertical alignment `\clvertalt \clvertalc \clvertalb`; text direction `\cltxlrtb \cltxtbrl \cltxbtlr \cltxlrtbv \cltxtbrlv`; `\clNoWrap`; merges `\clmgf \clmrg \clvmgf \clvmrg`; shading `\clcbpatN \clcfpatN \clshdngN`; cell borders `\clbrdrt \clbrdrl \clbrdrb \clbrdrr`; row borders `\trbrdrt \trbrdrl \trbrdrb \trbrdrr \trbrdrh \trbrdrv`; `\trhdr \trkeep \trql \trqc \trqr \trleftN \trgaphN \trrhN \rtlrow`.
  - Ignored: `\irow \irowband \lastrow \ltrrow \ts \yts \tblind \tblindtype \trautofit \trftsWidth \trftsWidthB \trwWidth \tbllk*` and any other unknown table word.

- [ ] **Step 1: Write the failing tests** in `test/cpp/test_reader_tables.cpp` (helpers: `const rtf::Table& body_table(doc, section, block)`, `std::string cell_plain(const rtf::Cell&)` concatenating the text of the cell's paragraphs with "\n" between them):

```cpp
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
```

- [ ] **Step 2: Run to verify the build fails** (`rtf_add_test(reader_tables)` added; `body_table` and `cell_plain` go into `helpers.hpp`).

- [ ] **Step 3: Implement the table words in `reader.cpp` and the level logic in `builder.cpp`.**

- [ ] **Step 4: Build and run** `-R "reader_tables|reader_text"`. Expected: pass, both.

- [ ] **Step 5: Commit**: `git add src/builder.hpp src/builder.cpp src/reader.cpp test/cpp/helpers.hpp test/cpp/test_reader_tables.cpp CMakeLists.txt && git commit -m "Assemble tables, nested tables and their properties"`.

---

### Task 6: Reader structure: sections, headers, fields, footnotes, pictures, shapes, objects, lists

**Files:**
- Modify: `src/reader.cpp`, `src/builder.hpp`, `src/builder.cpp`
- Create: `test/cpp/test_reader_structure.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Tasks 4 and 5.
- Produces: the rest of the model, per the spec's "Sections and page setup" (as amended) and "Destinations the reader understands", plus:
  - `\sectd` resets the current section's `page` to the document page, `break_` to Page, `columns`, `titlePage`, `pageNumberStart` to their defaults, and nothing else. `\pgwsxn \pghsxn \marglsxn \margrsxn \margtsxn \margbsxn \lndscpsxn \headeryN \footeryN \sbknone \sbkcol \sbkpage \sbkeven \sbkodd \colsN \titlepg \pgnstartsN` (recorded as `pageNumberStart` only together with `\pgnrestart`) fill the current section. `\sect` ends the current paragraph and tables, pushes the section and starts a new one with the same `page`, `break_` and `columns` and no headers. At the end of the input the current section is pushed, always, so a document has at least one.
  - `\header \footer \headerf \footerf \headerl \footerl \headerr \footerr` open a `BlockSink` on the matching `HeaderSet` member of the current section (creating the vector; appending if it exists).
  - `\field`: a field frame; `\*\fldinst` sets the text target to the frame's instruction (skipping `\*\datafield`); `\fldrslt` sets the inline target to the frame's result; `\flddirty` and `\fldlock` set the flags; when the `\field` group closes, the instruction is trimmed of ASCII whitespace and the `Field` goes to the enclosing text target. A `\field` inside `\fldrslt` nests.
  - `\footnote`: a `BlockSink` on a new `Footnote`; `\ftnalt` inside sets `endnote`; on close the `Footnote` inline goes to the enclosing paragraph (opening one if none).
  - `\*\bkmkstart name` and `\*\bkmkend name` give `Bookmark` inlines. `\upr`: the first child group is skipped; a `\*\ud` child is read transparently (its content belongs to the enclosing destination).
  - `\pict`: `\pngblip \jpegblip \emfblip \wmetafileN \dibitmapN \wbitmapN` set the format (none: `Unknown` with `unknown_picture_format`); `\picwN \pichN \picwgoalN \pichgoalN \picscalexN \picscaleyN` set the sizes; text tokens are hex pairs (CR, LF and spaces ignored, state kept across tokens; a non-hex byte or an odd final digit stops decoding with `bad_picture_data`, keeping the bytes so far); a `Binary` token appends its bytes; `\*\blipuid` and `\*\picprop` are skipped silently. On close the `Picture` goes to the enclosing paragraph, or to the shape when the group is the value of a `pib` property.
  - `\*\shppict` is transparent; `\nonshppict` is skipped (`skipped_destination`).
  - `\shp`: a shape frame; `\*\shpinst` is transparent for its `\sp` children: `{\sp{\sn NAME}{\sv VALUE}}` sets the frame's picture when NAME is `pib` and VALUE holds a `\pict`, and is skipped otherwise; `\shptxt` opens a `BlockSink` on the frame's blocks; `\shprslt` is skipped with `skipped_destination`; every `\shp*` control word is ignored. On close a `Shape` inline goes to the enclosing paragraph when it has blocks or a picture, and `shape_geometry_dropped` is added once per document in every case.
  - `\object`: `\*\objclass` text is `className`; `\*\objdata` is skipped with `object_data_dropped`; `\result` opens a `BlockSink` on `result`; on close the `EmbeddedObject` goes to the enclosing paragraph.
  - `\listtext` and `\pntext` set the inline target to the current paragraph's `listText` (opening a paragraph if none); `\*\pn` is skipped with `skipped_destination`, as the spec's list says.

- [ ] **Step 1: Write the failing tests** in `test/cpp/test_reader_structure.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to verify the build fails** (`rtf_add_test(reader_structure)` added).

- [ ] **Step 3: Implement the section words, header sinks, field frames, footnotes, pictures, shapes, objects and lists in `reader.cpp` and `builder.cpp`.**

- [ ] **Step 4: Build and run all reader tests**: `ctest --test-dir build -C Release --output-on-failure -R reader`. Expected: pass.

- [ ] **Step 5: Commit**: `git add src/reader.cpp src/builder.hpp src/builder.cpp test/cpp/test_reader_structure.cpp CMakeLists.txt && git commit -m "Read sections, headers, fields, footnotes, pictures, shapes, objects and lists"`.

---

### Task 7: JSON writer

**Files:**
- Create: `include/rtf/json.hpp`, `src/json.cpp`, `test/cpp/test_json.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `std::string rtf::to_json(const RtfDocument&)` in canonical form (see "Global constraints"); `std::string rtf::detail::base64(std::string_view bytes)`; `rtf::detail::JsonWriter` (`src/json.cpp` only) with `begin_object`, `key`, `string`, `integer`, `boolean`, `null`, `begin_array`, `end_array`, `end_object` writing the canonical layout, where the caller emits keys in sorted order and a test checks the order.
- Mapping rules: every struct becomes an object with its camelCase keys; enums become the lower-case strings of the spec (`'left'`, `'single'`, `'png'`, `'page'`, `'atLeast'`, ...); `Inline` and `Block` objects carry `kind` (`text`, `lineBreak`, `tab`, `pageBreak`, `columnBreak`, `field`, `bookmark`, `footnote`, `picture`, `shape`, `object`, `paragraph`, `table`); `Bookmark.end` is written as `position: "start" | "end"`; `Picture.data` as base64; `fonts` as an object keyed by the decimal font number; `colors` entries as `{"b","g","r"}` objects or `null`; `break_` as `break` and `default_` as `default`.
- Omission rules: `std::optional` absent and `bool` false are omitted; `lineSpacingMultiple`, `facingPages`, `titlePage`, `dirty`, `locked`, `endnote`, `noWrap` and every other flag appear only when true; `listText` only when non-empty; `tabs` only when non-empty. Always present even when empty: `info`, `fonts`, `colors`, `styles`, `sections`, `warnings`, `body`, `headers`, `footers`, `props`, `runs`, `result`, `blocks`, `rows`, `cells`, and the eight `PageSetup` numbers plus `landscape`.

- [ ] **Step 1: Write the failing tests** in `test/cpp/test_json.cpp`:

```cpp
static void minimal_document_is_canonical() {
  auto doc = parse_ok(R"({\rtf1\ansi Hi\par})");
  const char* expected =
R"({
  "codePage": 1252,
  "colors": [],
  "defaultFont": 0,
  "fonts": {},
  "info": {},
  "page": {
    "footerDistance": 720,
    "headerDistance": 720,
    "height": 15840,
    "landscape": false,
    "marginBottom": 1440,
    "marginLeft": 1800,
    "marginRight": 1800,
    "marginTop": 1440,
    "width": 12240
  },
  "sections": [
    {
      "body": [
        {
          "kind": "paragraph",
          "props": {},
          "runs": [
            {
              "kind": "text",
              "props": {},
              "text": "Hi"
            }
          ]
        }
      ],
      "break": "page",
      "footers": {},
      "headers": {},
      "page": {
        "footerDistance": 720,
        "headerDistance": 720,
        "height": 15840,
        "landscape": false,
        "marginBottom": 1440,
        "marginLeft": 1800,
        "marginRight": 1800,
        "marginTop": 1440,
        "width": 12240
      }
    }
  ],
  "styles": [],
  "version": 1,
  "warnings": []
}
)";
  CHECK_EQ(rtf::to_json(doc), std::string(expected));
}
static void string_escapes() {
  auto doc = parse_ok("{\\rtf1 a\"b\\\\c\\line d\\tab e\\'1b\\'e9\\u20013?\\par}");
  // the paragraph has runs and breaks; only the escaping of two runs is checked here
  std::string json = rtf::to_json(doc);
  CHECK(json.find("\"text\": \"a\\\"b\\\\c\"") != std::string::npos);
  CHECK(json.find("\"text\": \"e\\u001b\xC3\xA9\xE4\xB8\xAD\"") != std::string::npos);
}
static void defaults_omitted_and_flags_present() {
  auto doc = parse_ok(R"({\rtf1 {\b\fs20 x}\par})");
  std::string json = rtf::to_json(doc);
  CHECK(json.find("\"bold\": true") != std::string::npos);
  CHECK(json.find("\"size\": 20") != std::string::npos);
  CHECK(json.find("\"italic\"") == std::string::npos);
  CHECK(json.find("\"listText\"") == std::string::npos);
}
static void fonts_keyed_by_number_and_colors_nullable() {
  auto doc = parse_ok(R"({\rtf1{\fonttbl{\f0 A;}{\f31506 B;}}{\colortbl;\red1\green2\blue3;} x\par})");
  std::string json = rtf::to_json(doc);
  CHECK(json.find("\"fonts\": {\n    \"0\": {") != std::string::npos);
  CHECK(json.find("\"31506\": {") != std::string::npos);
  CHECK(json.find("\"colors\": [\n    null,\n    {\n      \"b\": 3,\n      \"g\": 2,\n      \"r\": 1\n    }\n  ]") != std::string::npos);
}
static void pictures_are_base64_and_bookmarks_have_positions() {
  auto doc = parse_ok(R"({\rtf1 {\pict\pngblip 000102}{\pict\pngblip 00010203}{\*\bkmkstart A}x{\*\bkmkend A}\par})");
  std::string json = rtf::to_json(doc);
  CHECK(json.find("\"data\": \"AAEC\"") != std::string::npos);
  CHECK(json.find("\"data\": \"AAECAw==\"") != std::string::npos);
  CHECK(json.find("\"position\": \"start\"") != std::string::npos);
  CHECK(json.find("\"position\": \"end\"") != std::string::npos);
}
static void deterministic() {
  auto doc = parse_ok(R"({\rtf1\trowd\cellx100\pard\intbl a\cell\row})");
  CHECK_EQ(rtf::to_json(doc), rtf::to_json(doc));
}
```

- [ ] **Step 2: Run to verify the build fails.**

- [ ] **Step 3: Implement `json.hpp` and `json.cpp`.** One `write_*` function per model struct, keys written in sorted order by hand. Integers through `std::to_string`. No floating point anywhere.

- [ ] **Step 4: Build and run** `-R json`. Expected: pass.

- [ ] **Step 5: Commit**: `git add include/rtf/json.hpp src/json.cpp test/cpp/test_json.cpp CMakeLists.txt && git commit -m "Write the document model as canonical JSON"`.

---

### Task 8: Text projection and logical tables

**Files:**
- Create: `include/rtf/text.hpp`, `src/text.cpp`, `include/rtf/tables.hpp`, `src/tables.cpp`, `test/cpp/test_tables.cpp`
- Modify: `src/json.cpp`, `include/rtf/json.hpp` (add `to_json(const std::vector<LogicalTable>&)`), `CMakeLists.txt`

**Interfaces:**
- Produces:

```cpp
namespace rtf {
std::string inline_text(const std::vector<Inline>& inlines);  // TextRun text; Tab "\t"; LineBreak "\n"; PageBreak "\f"; ColumnBreak "\n"; Field its result; Footnote, Shape, EmbeddedObject their blocks via to_text rules; others nothing
std::string cell_text(const Cell& cell);                      // the cell's blocks, paragraphs joined with "\n", no trailing "\n"
std::string to_text(const RtfDocument& document);             // the projection in "The text projection"

struct LogicalCell { std::string text; int column = 0; int span = 1; };
struct LogicalRow { std::vector<LogicalCell> cells; bool spacer = false; int indent = 0; };
struct TablePart { int section = 0; int block = 0; };
struct LogicalTable {
  int id = 1;
  int firstSection = 0, lastSection = 0;
  std::vector<TablePart> parts;
  std::vector<int> edges;
  std::vector<std::string> columnNames;
  std::vector<Row> headerRows;
  std::vector<LogicalRow> rows;
};
std::vector<LogicalTable> logical_tables(const RtfDocument& document);
std::string to_json(const std::vector<LogicalTable>& tables);   // canonical; LogicalCell keys column, span, text; LogicalRow keys cells, indent, spacer; TablePart keys block, section
}
```

The logical-table JSON writes every field always, including `spacer: false`, `span: 1` and `indent: 0`; the omission rules of Task 7 apply to the document model only.

- Logical-table rules are the spec's five, with these details: a cell's `column` is the index in `edges` of the cell's left edge (the previous cell's right, or the row's `left`, else 0); `span` is the number of grid intervals between its left and right edges; two parts' header rows are equal when they have the same number of rows and each pair of rows has the same `right` values and the same `cell_text` per cell; a part with no header rows joins the previous table when every `right` of its first row is in the previous part's set of data-row rights; `indent` is `indentLeft.value_or(0)` of the first paragraph of the first cell; a spacer is a row whose every cell has empty `cell_text` after trimming ASCII whitespace and U+00A0; column names take line breaks as spaces and are trimmed; an empty name becomes `column_N` with N the 1-based grid column; a duplicate name gets `_2`, `_3`, ... in order of appearance; a header cell of an earlier row prefixes its text (plus one space) to the names of every grid column it covers.

- [ ] **Step 1: Write the failing tests** in `test/cpp/test_tables.cpp`:

```cpp
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
```

(`replace` is a three-line helper in the test file.)

- [ ] **Step 2: Run to verify the build fails** (`rtf_add_test(tables)` added).

- [ ] **Step 3: Implement `text.cpp`, `tables.cpp` and the logical-table JSON.**

- [ ] **Step 4: Build and run** `-R tables`. Expected: pass.

- [ ] **Step 5: Commit**: `git add include/rtf/text.hpp src/text.cpp include/rtf/tables.hpp src/tables.cpp include/rtf/json.hpp src/json.cpp test/cpp/test_tables.cpp CMakeLists.txt && git commit -m "Add the text projection and the logical tables"`.

---

### Task 9: The rtf2json command, the fixtures and their goldens

**Files:**
- Create: `tools/rtf2json.cpp`, `test/tools/make_fixtures.py`, `test/tools/regen_goldens.py`, `test/cpp/test_goldens.cpp`
- Create: `test/fixtures/sas_table_3pages.rtf`, `sas_listing_1page.rtf`, `word_minimal.rtf`, `word_tables.rtf`, `word_structure.rtf` (hand-written); `msword_disposition.rtf`, `msword_features.rtf`, `msword_sections.rtf`, `sas_disposition.rtf` (copied from Massimo's `W_*.rtf` files and `T_Disposition.RTF`); `word_text.rtf`, `pictures.rtf`, `malformed_missing_brace.rtf`, `malformed_extra_braces.rtf`, `malformed_truncated_control.rtf`, `malformed_bin_past_end.rtf`, `malformed_deep_nesting.rtf`, `malformed_u_before_brace.rtf`, `malformed_not_rtf.rtf`, `malformed_empty.rtf` (generated by `make_fixtures.py`); one `.json` and one `.tables.json` golden per fixture
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces the command `rtf2json [--json|--text|--tables|--summary] [--version] <file>`: reads the file as bytes; `--json` (default) prints `to_json(document)`; `--text` prints `to_text`; `--tables` prints `to_json(logical_tables)`; `--summary` prints the lines `file: <basename>`, `sections: N`, `tables: M`, then per table `table K: rows R, columns: name1 | name2 | ...`, then `warnings: code1 code2 ...` (distinct codes sorted, or `warnings: none`). Exit code 0 on success, 1 when the file is not RTF (printing `{"error": "not_rtf"}` on stdout for `--json` and `--tables`, nothing for the others), 2 for usage or read errors (message on stderr).
- Produces `test_goldens <fixtures_dir>`: for every `*.rtf` in the directory, parses it, compares `to_json(document)` (or `{"error": "not_rtf"}\n`) with `<name>.json` and `to_json(logical_tables)` with `<name>.tables.json`, byte for byte, and checks that parsing twice gives identical JSON. A mismatch prints the first differing line of each.
- Produces `regen_goldens.py <rtf2json> <fixtures_dir>`: runs the command in `--json` and `--tables` modes, checks that each output equals `json.dumps(json.loads(out), ensure_ascii=False, indent=2, sort_keys=True) + "\n"` (else fails with "not canonical"), and writes the goldens.

Fixture contents (hand-written text invented; hand-written and generated fixtures under 12 KB; the four copies are 72 KB, 469 KB, 65 KB and 8 KB):

- `msword_disposition.rtf`, `msword_features.rtf`, `msword_sections.rtf`, `sas_disposition.rtf`: byte-for-byte copies of `C:/Users/massi/Downloads/RTF/W_Disposition.rtf`, `W_Features.rtf`, `W_Sections.rtf` and `T_Disposition.RTF` as they are when Task 9 runs (`cp` from Git Bash; check with `cmp` after copying). They are the only fixtures with real producer output. The Word three: a SAS table re-saved by Word (`\headerr`/`\footerr`, the title table in the header, duplicated row definitions); a portrait document with a vertically merged table, a wide cell, two lists with Symbol bullets, a picture written as `{\*\shppict{\pict ...}}{\nonshppict{\pict ...}}` (an earlier save of the same file used the `pib` shape form, which the hand-written `pictures.rtf` keeps), a hyperlink field with a result, a page break, Greek, Chinese and an emoji; a landscape document with a nested table, two sections (the `\sect` sits inside a character group, like SAS's `{\row}`) and a footnote written as `\chftn{\footnote ... {\cs38\super\chftn} text}`. The SAS one: a one-page SAS ODS RTF table with the title and footnote tables in `\header` and `\footer`, `PAGE` and `NUMPAGES` fields without results, the `\upr` bookmark, two `\trhdr` rows, spacer rows, `\li288` and `\li576`, `\~`, and `{\row}` in braces; the hand-written `sas_table_3pages.rtf` follows its style over three sections.

- `sas_table_3pages.rtf`: the SAS ODS RTF layout from the survey of 2026-11-10. Prolog `{\rtf1\ansi\ansicpg1252\uc1\deff0\deflang2057\deflangfe2057{\fonttbl{\f1\fmodern\fprq1\fcharset0 Courier New;}}`, a 17-entry `\colortbl` starting with the automatic entry, `{\stylesheet{\widctlpar\adjustright\fs20\cgrid\snext0 Normal;}{\*\cs10\additive Default Paragraph Font;}}`, `{\info{\title ...}{\author ...}{\operator ...}{\version1}{\creatim\yr2026\mo11\dy16\hr9\min0\sec0}}`, `\widowctrl\ftnbj\aenddoc\formshade\viewkind1\viewscale100\pgbrdrhead\pgbrdrfoot\fet0\paperw16837\paperh11905\margl1080\margr1080\margt1080\margb360`. Then three sections, each `\sectd\linex0\endnhere\pgwsxn16837\pghsxn11905\lndscpsxn\headery1080\footery360\marglsxn1080\margrsxn1080\margtsxn1080\margbsxn360`, a header group `{\header\pard\plain\qc{ ... \pard{\par}}}` holding a title table of three rows (row 1: three cells `\cellx4862\cellx9802\cellx14674` with `\clcbpat8\clpadt10\clpadft3\clpadr10\clpadfr3`, left cell "Protocol ABC-123" with the asterisks written as `{\'2a}`, centre cell "Table 14.1.1", right cell `Page {\field{\*\fldinst { PAGE }}} of {\field{\*\fldinst { NUMPAGES }}}` at `\fs22`; row 2: three cells at `\fs18` with `\keepn`, "Summary of Subject Disposition", "Safety Population", empty; row 3: one full-width cell "Analysis set {\'2d} all randomised subjects"), a footer group `{\footer\pard\plain\qc{{\par}\trowd ... }}` with a one-cell footnote row and a two-cell row (left "Source: listing 16.2.1", right empty), the SAS bookmark `{\upr{\*\bkmkstart IDX}{\*\ud{\*\bkmkstart IDX}}}{\*\bkmkend IDX}` once in section 1, and a body table: two `\trowd\trkeep\trhdr\trqc` header rows (row 1: `\cellx3456` empty and `\cellx8670` "Treatment group" with `\clbrdrt\brdrs\brdrw10\brdrcf1` on both and `\clvertalb\clcbpat8`; row 2: `\cellx3456\cellx5194\cellx6932\cellx8670` with "", "Placebo\line (N=20)", "Drug A\line (N=21)", "Total\line (N=41)", `\clbrdrb\brdrs\brdrw10\brdrcf1`, `\fs19\keepn`), then data rows `\trowd\trkeep\trqc` with `\clvertalt\clcbpat8` and `\pard\plain\intbl\sb0\sa0\ql\f1\fs18\cf1`: spacer rows with one `\cellx8670` cell, term rows ("Randomised", "Completed study", "Discontinued", indented reasons with `\li288` and `\li576`, "Adverse event", "Withdrew consent", "Lost to follow-up", "Other") with values such as `15\~(75.0%)` using `\~`, about ten data rows per section, the last row of section 3 with `\clbrdrb\brdrs\brdrw10\brdrcf1`, and `{\row}` in braces after every row. Every cell paragraph ends with `\cell` inside braces as SAS writes it: `{text\cell}`. Sections 1 and 2 end with `\sect`.
- `sas_listing_1page.rtf`: the same prolog and one section; the title rows name a listing ("Listing 16.2.1", "Subject Eligibility"); the body table starts with one `\trhdr` row of three columns (`\cellx3456\cellx6922\cellx10388`: "Subject", "Criterion", "Result") and then rows that alternate two layouts in one table: three-column rows and one-column rows (`\cellx10388`) carrying a long comment that wraps, as the listing sample does; one comment holds `\line` twice.
- `word_minimal.rtf`: the Word prolog shape: `{\rtf1\adeflang1025\ansi\ansicpg1252\uc1\adeff0\deff0\stshfdbch31505\stshfloch31506\stshfhich31506\stshfbi31507\deflang2057\deflangfe2057\themelang2057`, a font table with `\fbidi`, `{\*\panose ...}`, `{\*\falt ...}` and theme fonts `\flomajor\f31500` and `\fhiminor\f31506`, a colour table with `\caccentone` and `\ctint`, a stylesheet with `\s1`, `\*\cs10`, `\*\ts11`, `\*\listtable` with one list, `\*\listoverridetable`, `\*\rsidtbl`, `\*\mmathPr` written as `{\mmathPr\mmathFont34\mbrkBin0}`, `\*\generator Microsoft Word-like text`, `{\info ... {\nofpages1}{\nofwords10}{\vern27}}`, `\*\xmlnstbl`, `\*\latentstyles` with a few `\lsdlocked0` entries, `\*\themedata` and `\*\datastore` with 40 hex characters each, `\*\colorschememapping`, `{\*\defchp ...}{\*\defpap ...}`, the Word section words `\sectd\ltrsect\linex0\headery708\footery708\colsx708\endnhere\sectlinegrid360\sectdefaultcl\sftnbj` written twice around `{\*\ftnsep\chftnsep}{\*\ftnsepc\chftnsepc}{\*\aftnsep\chftnsep}{\*\aftnsepc\chftnsepc}` and `{\*\pnseclvl1\pnucrm\pnstart1\pnindent720\pnhang{\pntxta .}}` for levels 1 to 9, then three paragraphs in the Word style `\pard\plain\ltrpar\ql\li0\ri0\sa160\sl278\slmult1\widctlpar\wrapdefault\aspalpha\aspnum\faauto\adjustright\rin0\lin0\itap0\rtlch\fcs1\af31507\afs24\alang1025\ltrch\fcs0\f31506\fs24\lang2057\langfe1033\kerning2\cgrid\langnp2057\langfenp1033 {\rtlch\fcs1\af31507\ltrch\fcs0\insrsid3697434 text}` with bold, italic, underline, colour `\cf6`, `\highlight7`, `\fs28`, `\s1`, `\expnd12\expndtw60`, `\lbr3\line`, and `\headerr`/`\footerr` groups with `\tqc\tx4513\tqr\tx9026` tab stops and a `PAGE` field with a result; portrait Letter.
- `word_tables.rtf`: the same prolog shortened (no theme blobs); one table in the Word style: `\trowd\irow0\irowband0\ltrrow\ts11\trgaph108\trleft-108\trbrdrt\brdrs\brdrw10 ... \trftsWidth1\trftsWidthB3\trautofit1\trpaddl108\trpaddr108\trpaddfl3\trpaddft3\trpaddfb3\trpaddfr3\tblrsid4080963\tbllkhdrrows\tbllkhdrcols\tbllknocolband\tblind0\tblindtype3` and cells `\clvertalt\clbrdrt\brdrs\brdrw10\clbrdrl\brdrs\brdrw10\clbrdrb\brdrs\brdrw10\clbrdrr\brdrs\brdrw10\cltxlrtb\clftsWidth3\clwWidth2311\cellx2203`; four rows of four grid columns where row 2 has a wide cell covering columns 2 and 3 (a horizontal merge Word style) and column 4 carries `\clvmgf` in row 2 and `\clvmrg` in row 3; `\clmgf`/`\clmrg` on two cells of row 4 (other producers' style); row heights `\trrh300` and `\trrh-400`; shading `\clcbpat5`; one row with every border style (`\brdrs \brdrdb \brdrdot \brdrdash \brdrth \brdrhair \brdrtriple \brdrnone`); the duplicated row definition before every `\row`, the second copy inside the character group as Word writes it (`{\rtlch\fcs1 ... \trowd ... \row\ltrrow}`), `\lastrow` on the last; a second table whose last row lacks `\row` (the file ends with `\pard\par}` after the cells); a nested table in the Word style (`\itap2`, `\nestcell{\nonesttables\par}`, `{\*\nesttableprops ... \nestrow}{\nonesttables\par}`).
- `word_structure.rtf`: the shortened prolog; three sections with different page setups and break kinds (`\sbkpage`, `\sbknone`, `\sbkeven`), section 1 with `\titlepg`, `\headerf`, `\headerl`, `\headerr`, `\footerl`, `\footerr`, `\facingp` on the document; `\page` inside a paragraph and a paragraph with `\pagebb`; `\column` with `\cols2` in section 2; `\pgnstarts3\pgnrestart` in section 3; a footnote (`{\super\chftn}{\footnote\pard\plain ... {\super\chftn} text\par}`) and an endnote (`\ftnalt`); `{\*\bkmkstart b1}...{\*\bkmkend b1}`; a nested field (a `HYPERLINK` whose result holds a `PAGE` field with result); a `\pntext` list with `{\*\pn\pnlvlblt\pnf3\pnindent360{\pntxtb\'b7}}` and a `\listtext` list with `\ls1\ilvl0` and `\ls2\ilvl1` paragraphs, bullets as `\'b7` under a `\fcharset2` font and numbers as text.
- `word_text.rtf` (generated): `\uc1` at the top, then paragraphs with: `\u8364?`, `\u963\'73`, `\uc0\u20013` followed directly by text, `\uc2\u20013\'a4\'a4`, the emoji pair `\u-10179\'3f\u-8699\'3f`, a `\uc1` run where the fallback is a brace (`{\u8364}`), cp1251 bytes under `\f5\fcharset204` written both as `\'c0\'e1` and as raw bytes 0xC0 0xE1, a Symbol-charset run `\f3\'b7\'a5`, raw bytes 0xE9 and 0xFC under the default font, a raw tab byte, every special-character word, `\{\}\\`, and `\bin4 ` followed by four bytes (0x00 0x01 0x7B 0x7D) outside any picture.
- `pictures.rtf` (generated): a valid 1×1 PNG written with Python's `zlib` and `struct`, as `\pict\pngblip\picw1\pich1\picwgoal15\pichgoal15` hex with a CRLF after every 128 hex characters; the same PNG inside `{\*\shppict{\pict ...}}{\nonshppict{\pict\wmetafile8 0102}}`; an EMF stub `{\pict\emfblip\picw100\pich100 01000000}`; a `\bin` payload `{\pict\jpegblip\bin4 ` + bytes 0xFF 0xD8 0x7B 0x7D + `}`; a Word 2016 shape with `pib` and `\shprslt`; a shape with `\shptxt`; an `\object` with `\*\objclass`, `\*\objdata` and `\result`.
- `malformed_*.rtf` (generated): `missing_brace` = `word_minimal.rtf` without its last `}`; `extra_braces` = `sas_listing_1page.rtf` plus `}}}`; `truncated_control` = the first 1000 bytes of `sas_table_3pages.rtf` cut inside a control word; `bin_past_end` = `{\rtf1 {\pict\pngblip\bin999 ab}`; `deep_nesting` = `{\rtf1 ` + 2000 × `{` + `x` + 2000 × `}` + `}`; `u_before_brace` = `{\rtf1 {\uc3\u8364}after\par}`; `not_rtf` = the bytes `hello world`; `empty` = zero bytes.

Goldens to verify by eye, once, before committing (each a `CHECK` worth adding to `test_goldens.cpp` as a second pass over the parsed fixtures): `sas_table_3pages.json` has 3 sections, each with `headers.default` holding one table of 3 rows and one empty paragraph, `footers.default` holding one empty paragraph and one table, two `PAGE`/`NUMPAGES` fields with empty results, a body table whose first two rows have `header: true`, cells with `verticalAlign: "bottom"` in header rows, `"\u00a0"` written raw as U+00A0 inside values, one bookmark start and one end, and `warnings: []`; `sas_table_3pages.tables.json` has exactly one table with `firstSection 0`, `lastSection 2`, 3 parts, 5 edges, the four column names `column_1`, `Treatment group Placebo (N=20)`, `Treatment group Drug A (N=21)`, `Treatment group Total (N=41)`, spacer rows flagged and the indented rows with `indent` 288 and 576; `sas_listing_1page.tables.json` has one table whose one-column rows have `span: 3`; `word_minimal.json` lists `skipped_destination` warnings for `themedata`, `datastore`, `latentstyles`, `rsidtbl`, `xmlnstbl`, `colorschememapping`, `listtable`, `listoverridetable` and `mmathPr` and no `unknown_destination`; `word_tables.json` shows `mergeVertical`, `mergeHorizontal`, the `row_not_closed` warning once, and the nested table inside a cell with no text from `\nonesttables`; `word_text.json` has no U+FFFD and the `symbol_font` warning; `pictures.json` has five pictures and one shape with a picture, `shape_geometry_dropped` and `object_data_dropped`; `malformed_not_rtf.json` and `malformed_empty.json` are `{"error": "not_rtf"}`; `malformed_deep_nesting.json` has `depth_limit` and the text `x`; `msword_disposition.json` has 1 section with `headers.right` and `footers.right` and no `default`, a body table whose first rows have `header: true`, and only `skipped_destination` warnings; `msword_features.json` has 1 section, a body table with two `mergeVertical` cells and no `mergeHorizontal`, exactly one picture (PNG, from `\*\shppict`; the `\nonshppict` metafile is absent) and no shape, seven paragraphs with `listText`, a `HYPERLINK` field with a result, one `pageBreak` inline, the emoji as a single four-byte UTF-8 sequence inside a run, and the warnings `skipped_destination` and `symbol_font` only; `msword_sections.json` has 2 sections, both `landscape`, a nested table inside a cell of the first body table, one `footnote` inline whose blocks hold one paragraph, no run containing a footnote mark character, and only `skipped_destination` warnings; `sas_disposition.json` has 1 section, `headers.default` with one table and one empty paragraph, `footers.default` with one empty paragraph and one table, two fields with empty results, a body table whose first two rows have `header: true`, one bookmark start and one end, and `warnings: []`; `sas_disposition.tables.json` has one table whose column names come from the two header rows and whose spacer rows are flagged; none of the four `.tables.json` has more than one table per section.

- [ ] **Step 1: Write `test/cpp/test_goldens.cpp` and register it**: `rtf_add_test(goldens ${CMAKE_SOURCE_DIR}/test/fixtures)`. Build; run; expected: fails because the fixtures directory does not exist.

- [ ] **Step 2: Write `tools/rtf2json.cpp`** and add the `rtf2json` executable under `RTF_BUILD_TOOLS`. Build. Run `build/Release/rtf2json.exe --version`; expected `0.1.0`.

- [ ] **Step 3: Write the five hand-written fixtures and copy the four producer files.** Run `rtf2json --summary` on each and read the output: section counts 3, 1, 1, 1, 3 for the hand-written ones and 1, 1, 2, 1 for the copies; `warnings: none` for the three SAS files; no `unknown_destination` for the copies (one means a Word word the plan did not foresee: add it to the known list in `reader.cpp`).

- [ ] **Step 4: Write `test/tools/make_fixtures.py`** and run `python test/tools/make_fixtures.py test/fixtures`. Run `rtf2json --summary` on each generated file; expected: no crash, exit code 1 for `malformed_not_rtf.rtf` and `malformed_empty.rtf`, exit code 0 for the rest.

- [ ] **Step 5: Write `test/tools/regen_goldens.py`** and run `python test/tools/regen_goldens.py build/Release/rtf2json.exe test/fixtures`. Expected: one `.json` and one `.tables.json` per fixture, "canonical" for every file. Then check each item of the by-eye list above against the files and add those checks to `test_goldens.cpp`.

- [ ] **Step 6: Run the goldens test and the whole suite** under both compilers. Expected: pass. (A GCC and MSVC disagreement in a golden is a bug in the code, not in the golden.)

- [ ] **Step 7: Commit**: `git add tools/rtf2json.cpp test/tools/make_fixtures.py test/tools/regen_goldens.py test/cpp/test_goldens.cpp test/fixtures CMakeLists.txt && git commit -m "Add the rtf2json command, the fixtures and their goldens"`.

---

### Task 10: The independent text extractor and the text-coverage test

**Files:**
- Create: `test/tools/rtf_text.py`, `test/tools/compare_text.py`, `test/tools/check_text_coverage.py`
- Modify: `CMakeLists.txt` (find Python3; register `text_coverage`)

**Interfaces:**
- Produces `rtf_text.py <file.rtf>`: prints the text projection of "The text projection" (the RTF-stream rules) to stdout as UTF-8; exits 0; never raises on malformed input (a bare `try` around the main loop is not acceptable; the tokenizer and the group stack must handle the malformed fixtures by construction).
- Produces `compare_text.py <a.txt> <b.txt>`: normalises both (split on "\n", `str.strip()` each line, drop empty lines), prints the first differing line pair with its index and exits 1 on a difference, else prints `same` and exits 0.
- Produces `check_text_coverage.py <rtf2json> <fixtures_dir>`: for every fixture whose name does not start with `malformed_`, runs `rtf2json --text` and `rtf_text.py`, compares with `compare_text.py`'s function, and exits 1 if any differ, naming the fixture. The three `msword_*.rtf` copies and `sas_disposition.rtf` are included; they are the first check of both readers against real producer output.

Independence rule (spec, "Left to the builder"): `rtf_text.py` is written by a fresh agent session that receives this task's text, the spec sections "The reader" and "The document model" and the plan section "The text projection", and that must not open `src/`, `include/` or the C++ tests. Its author may read the fixtures. The reviewer of this task checks the session's transcript or its prompt for that rule.

- [ ] **Step 1: Write `compare_text.py` and `check_text_coverage.py`**, register `text_coverage` in CMake with `add_test(NAME text_coverage COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/tools/check_text_coverage.py $<TARGET_FILE:rtf2json> ${CMAKE_SOURCE_DIR}/test/fixtures)` guarded by `if(Python3_Interpreter_FOUND)` with a `message(WARNING ...)` otherwise. Run the test; expected: fails because `rtf_text.py` is missing.

- [ ] **Step 2: Have `rtf_text.py` written under the independence rule** (about 100 to 150 lines: a regex tokenizer, a group stack carrying the destination, the `\uc` state and the font, a skip set, the block-destination set, a font table reader for charsets, Python's codecs for the code pages and `0xF000 + b` for Symbol).

- [ ] **Step 3: Run the test**: `ctest --test-dir build -C Release --output-on-failure -R text_coverage`. Expected: pass for every fixture. A difference means one of the two readers misreads the fixture; decide which from the spec, fix that one, and if the C++ changes, rerun `regen_goldens.py` and review the diff.

- [ ] **Step 4: Commit**: `git add test/tools/rtf_text.py test/tools/compare_text.py test/tools/check_text_coverage.py CMakeLists.txt && git commit -m "Check the text projection against an independent extractor"`.

---

### Task 11: Mutation test, large input and the fuzz target

**Files:**
- Create: `test/cpp/test_mutation.cpp`, `test/cpp/test_large.cpp`, `test/fuzz/fuzz_parse.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces `test_mutation <fixtures_dir> [count=10000] [seed=20261116]`: for every `*.rtf` in the directory, `count` times (a fixture above 64 KB gets `count / 10` mutants, so the 469 KB Word file does not dominate the run): copy the bytes, apply 1 to 3 edits drawn with a xorshift64 generator from {delete 1 to 16 bytes at a random position, insert 1 to 8 bytes drawn from the set `\ { } ; ' u - 0 1 2 3 4 5 6 7 8 9 a b c d e f p r t x space`, flip one random byte to a random value, truncate at a random position, insert a `{` or a `}` at a random position}, then `parse`, and when `ok`, `to_json`, `to_text` and `logical_tables`. Each parse is timed with `std::chrono::steady_clock` and its live heap is measured through a replacement of the global `operator new` and `operator delete` (all variants) in the test file that keeps the size in a 16-byte header before each block and tracks the current and peak totals. Fail when a parse takes over 1 s or its peak live heap exceeds `max(100 * input size, 1 MB)`, and in that case write the mutant to `mutant_failed.rtf` in the current directory and print the fixture name, the iteration and the seed. Prints the number of mutants parsed and the slowest parse per fixture.
- Produces `test_large [seconds=30]`: builds in memory an RTF of about 32 MB: the SAS prolog and one section with a four-column table of 110,000 rows (`\trowd\trkeep\trqc` and four `\cellx`, each cell `{\pard\plain\intbl Rk-Cj\cell}`), parses it, checks `ok`, one section, one table of 110,000 rows, `to_text` length above 1 MB, and the parse time under the budget.
- Produces `fuzz_parse.cpp` with `extern "C" int LLVMFuzzerTestOneInput(const uint8_t*, size_t)` calling `parse`, `to_json`, `to_text` and `logical_tables`; built only with `-DRTF_FUZZ=ON` (requires Clang; `-fsanitize=fuzzer,address`), never in CI.
- CTest: `rtf_add_test(mutation ${CMAKE_SOURCE_DIR}/test/fixtures)` with `set_tests_properties(mutation PROPERTIES TIMEOUT 900)`; the count can be lowered through the environment variable `RTF_MUTATIONS` (read by the test when set); `rtf_add_test(large)` with `TIMEOUT 300`.

- [ ] **Step 1: Write `test_mutation.cpp` and `test_large.cpp`, register them, build and run them.** Expected first run: likely failures (crashes, hangs or the budget). Each failure is a reader bug: write a regression test with the minimal mutant in the matching `test_reader_*.cpp`, fix, rerun. Repeat until `test_mutation` passes with the default count under both compilers and `test_large` passes in under 30 s in Release.

- [ ] **Step 2: Add `fuzz_parse.cpp` and the `RTF_FUZZ` option.** Not built on this machine (no Clang); the CMake branch is checked by configuring with `-DRTF_FUZZ=ON` and seeing the clear error message "RTF_FUZZ needs Clang" when the compiler is not Clang.

- [ ] **Step 3: Run the whole suite** under both compilers. Expected: pass.

- [ ] **Step 4: Commit**: `git add test/cpp/test_mutation.cpp test/cpp/test_large.cpp test/fuzz/fuzz_parse.cpp CMakeLists.txt src && git commit -m "Add the mutation test, the large-input test and the fuzz target"` (the `src` files only if Step 1 changed them; list them explicitly).

---

### Task 12: Local samples check, README and CI

**Files:**
- Create: `test/local/samples.sh`, `.github/workflows/ci.yml`
- Modify: `README.md`

**Interfaces:**
- Produces `RTF_SAMPLES_DIR=<dir> [RTF2JSON=<path>] [PYTHON=<python>] test/local/samples.sh`: for every `*.rtf` and `*.RTF` in the directory (sorted, case-insensitive), runs `rtf2json --summary` and compares the block for that file in `<dir>/expected.txt` (blocks start with the `file:` line and are separated by one empty line); when `expected.txt` does not exist it is written from the current output and the script prints `wrote expected.txt; review it against Word` and exits 0; a file without a block gets its block appended and is reported as `new`; a differing block prints a unified diff and fails. Then for every file it runs `rtf2json --text` and `rtf_text.py` and compares them with `compare_text.py`; a difference fails. Exit 1 on any failure, 0 otherwise, with a one-line summary `N files, M new, K failed`. The script never prints the files' text, only the summaries and the diffs of mismatches.
- Produces `ci.yml`: on push and pull request; a matrix of `ubuntu-latest`, `macos-latest`, `windows-latest` with the three commands from "Repository and conventions" (`-C Release` on every platform is harmless); a fourth job on Ubuntu with `-DRTF_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug` and `RTF_MUTATIONS=2000`; Python is present on every runner.
- README: a section "Build and test" with the commands, "The command" with the four modes of `rtf2json`, "Fixtures and goldens" (how to regenerate and that the diff is reviewed like code), "Local samples" (the script and that `expected.txt` lives next to the samples and is never committed), and "Status".

- [ ] **Step 1: Write `samples.sh`**, then run `RTF_SAMPLES_DIR=C:/Users/massi/Downloads/RTF RTF2JSON=build/Release/rtf2json.exe test/local/samples.sh` from Git Bash. Expected: `expected.txt` written, 15 files, no crash, and the text comparison passes for every file. Report the summary blocks (section counts, table counts, warning codes) to Massimo for his review; do not quote the files' text beyond what the summary lines carry. Expected from the survey of 2026-11-16: the SAS files list `warnings: none`; the three `W_` files list `skipped_destination` and `symbol_font` where a bullet appears; an `unknown_destination` in a Word sample names a word this plan did not foresee and is a reason to add it to the known list in `reader.cpp`, not a failure; `W_Sections.rtf` shows 1 section and 1 table (logical tables count body tables only, so the nested table is inside it); `W_Features.rtf` shows 1 section and 1 table; `W_Disposition.rtf` shows 1 section and 1 table.

- [ ] **Step 2: Write `ci.yml` and the README sections.**

- [ ] **Step 3: Run the full suite one last time under both compilers**; expected: pass. Check `git status` shows no stray files (`expected.txt` must not be in the repository).

- [ ] **Step 4: Commit**: `git add test/local/samples.sh .github/workflows/ci.yml README.md && git commit -m "Add the local samples check, the README and CI"`.

- [ ] **Step 5: Hand back to Massimo**: the branch `main` has 12 commits and nothing is pushed. He decides when to push (the first push creates the public history and starts CI) and reviews `expected.txt` against Word.

---

## Self-review notes

- Spec coverage: tokenizer (T2), group state and destinations (T4, T6), text decoding (T3, T4), paragraphs and runs (T4), tables (T5), sections and headers (T6), logical tables (T8), the document model and JSON (T1, T7), validation items 1 (T9, including the three Word-saved fixtures and `T_Disposition.RTF`, which Massimo allowed on 2026-11-16), 2 (T10), 3 (T11), 5 native half (T9), 8 (T12); items 4, 6, 7, 9, 10 belong to plans 2 to 5. The warning codes are fixed here because the spec leaves them to the builder.
- Amendments of 2026-11-16 covered: `fonts` keyed by number (T1, T7), `\headerr` (T6), `\sectd` repeats (T6), `\nonesttables` and `\shprslt` (T5, T6), `pib` pictures (T6), `\clpadt` as left (T5), Symbol bullets (T6), `\uc2` (T4), `\*\cs` and `\*\ts` styles (T4), numeric info (T4), `landscape` as a flag (T6).
- Names used across tasks: `parse`, `ParseResult`, `to_json` (two overloads), `to_text`, `inline_text`, `cell_text`, `logical_tables`, `LogicalTable`, `BlockSink`, `TableLevel`, `Tokenizer`, `Token`, `decode_byte`, `charset_to_code_page`, `append_utf8`, `rtf_add_test`, `parse_ok`, `body_paragraph`, `body_table`, `run`, `run_text`, `cell_plain`, `has_warning`, `count_warnings`. Each is defined in exactly one task and used unchanged afterwards.
