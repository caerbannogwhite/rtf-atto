# RTF reader and writer design

Date: 2026-11-10, amended 2026-11-16. Status: approved by Massimo on
2026-11-16 ("the design document should be ready"), then amended the same
day with what three Word-saved samples showed. Nothing is built yet; the
first implementation plan is `docs/plans/2026-11-16-rtf-reader-implementation.md`
in rtf-atto.

This document describes an RTF reader and an RTF writer that serve three
consumers: bedevere-wise, which displays SAS-made RTF documents in the
browser; the-stats-duck, which imports tables from RTF files and writes
tables to RTF files; and anyone who installs the npm package. One C++ core
serves all three.

This document is the authority for the implementation plans. Where a plan
and this document disagree, this document is right, and the plan should be
corrected rather than reinterpreted. Each decision below is marked as
Massimo's (he said it) or as mine (I chose it; he can overrule it).

## Why

Clinical study reports are delivered as tables, listings and figures (TLFs)
in RTF files, most of them written by SAS through ODS RTF. The people who
receive them open them in Word. bedevere-wise wants to show these documents
in the browser, with everything the file contains, so a reader must turn
the RTF file into a document model that a viewer can draw. Occasionally a
table from such a file has to be queried, for example to compare the
numbers against a second programming of the same table; that is the DuckDB
import, and it is secondary. The reverse direction, writing a table from a
query as an RTF file with fixed titles and footnotes, serves reporting.

Massimo's own words, 2026-11-10: the reader's main purpose is "to display
the content on the browser"; "the parser must be able to get everything
from the document in order to display the full content"; importing a table
into DuckDB "would be nice" but he has never had to; the writer is "for
reporting: ie, writing a table as a RTF file, with fixed headers and
footers"; the files come "mainly from SAS, but it should be general
purpose"; figures "are not relevant for now". On the renderer (2026-11-16):
"the C++ core should not deal with rendering, but the NPM package
(TypeScript) will implement that".

## Scope

### Goals

1. **A reader** for RTF files as the RTF 1.9.1 specification defines them,
   producing a document model complete enough that a viewer can draw the
   whole document: sections, page setup, headers and footers, paragraphs,
   text runs with their formatting, tables with their cell geometry,
   borders and shading, fields, bookmarks, footnotes, pictures as bytes.
   Files written by SAS ODS RTF are the first-class target and the fixture
   source. Files that went through Word or other producers must read
   correctly too; constructs the reader does not model are skipped with a
   warning, never with lost text.
2. **A writer** that turns one table, with titles and footnotes, into an
   RTF file in the SAS TLF style: titles in the page header, footnotes in
   the page footer, header rows marked as repeating, Word left to
   paginate.
3. **An npm package** with the reader and the writer compiled to a
   stand-alone WebAssembly module, usable in the browser and in Node, with
   TypeScript types for the document model and the renderer below.
4. **the-stats-duck functions**: `read_rtf`, `rtf_tables` and `rtf_cells`
   table functions, and `COPY ... TO 'file.rtf' (FORMAT rtf, ...)`,
   verified on DuckDB 1.4.3 and 2.0.0 locally and on the CI matrix (1.4.5,
   1.5.6, 2.0.0), native and in duckdb-wasm.
5. **A logical-table helper** shared by the DuckDB import and the viewer:
   it finds the tables that SAS split across pages and stitches them back
   together.
6. **A renderer**, written in TypeScript inside the npm package, that turns
   the document model into HTML page by page. bedevere-wise mounts its
   output instead of drawing the model itself (Massimo, 2026-11-16).

### Non-goals

- **Rendering in the C++ core.** The core produces the model and nothing
  visual. The renderer is TypeScript in the npm package (Massimo,
  2026-11-16). It produces HTML and CSS, not canvas, not PDF.
- **Layout.** No line breaking, no page layout, no font metrics, in the
  core or in the renderer. The reader reports what the file says (section
  breaks, explicit page breaks, twips). The renderer shows a section as a
  page and lets a section that is taller than its page grow; it does not
  break a long section into pages (first release, mine).
- **Figures.** Pictures are carried as bytes with their format and size;
  nothing decodes them (Massimo: figures are not relevant for now). The
  renderer shows PNG and JPEG pictures through a data URL, because that
  costs nothing, and a labelled box for every other format (mine).
- **Writing arbitrary documents.** The writer writes one table document in
  one fixed style. Reading a document, changing it and writing it back is
  not a goal.
- **OLE objects, macros, drawing geometry, revision tracking semantics,
  list numbering definitions.** Skipped with a warning, text kept where
  there is text.
- **Double-byte code pages (932, 936, 949, 950, 1361) in the first
  release.** `\u` escapes always work; raw double-byte bytes become
  U+FFFD plus a warning. The decoder is table-driven so the tables can be
  added later (mine).
- **DOCX, PDF, RTFD.**

### Anti-goals: what would make this a failure even if it works

- A crash or a hang on any input. The browser hands the reader files a
  user downloaded. Malformed input yields a document plus warnings, or an
  error value, never a fault.
- Silent loss of text. If the reader does not understand a construct, the
  text inside it still comes out, and a warning says what was skipped.
- A model shaped only for SAS. Merged cells, nested tables, lists,
  pictures, footnotes, page breaks and the four header kinds all have a
  place in the model even though the SAS sample uses none of them.
- A core that depends on DuckDB headers, on duckdb-wasm's toolchain, or on
  a default-hash `std::unordered_map<std::string>` (the `PortableStringHash`
  lesson in `notes/engineering/2026-06-duckdb-version-must-match-duckdb-wasm.md`).
- Fixtures copied from Massimo's files. His sample files are synthetic but
  he asked that they never go into a repository (2026-11-10).

## Constraints

- C++17, standard library only. No Eigen, no DuckDB, no third-party JSON
  library (mine; see "JSON").
- The core throws no exceptions and uses no RTTI. The stand-alone CI build
  compiles with `-fno-exceptions -fno-rtti` to prove it. Inside DuckDB the
  same sources compile with DuckDB's flags.
- Linear time in the input size. No re-parse per output chunk (the ReadStat
  lesson in `notes/engineering/2026-06-xpt-reader-quadratic.md`).
- Bounded memory and depth: group nesting is capped (1024, mine); `\bin`
  lengths are clamped to the remaining input; a `\u` fallback skip never
  crosses a brace.
- No globals, no static mutable state. A parse is a function call on a
  byte range.
- Licence Apache-2.0, the same as the-stats-duck (mine).
- Plain English in code comments, commit messages and documents
  (`~/.claude/CLAUDE.md`).
- Names, options and column names are lower-case snake_case on the SQL
  side, camelCase in the JSON model and the TypeScript types.

## How it gets made

### Repositories

- **The core** lives in `rtf-atto`, the repository Massimo created on
  2026-11-16 at https://github.com/caerbannogwhite/rtf-atto (public, empty,
  under his own account rather than KoliStat). It holds the C++ sources,
  the fixtures, the C++ tests, the WebAssembly build, the TypeScript
  wrapper, the renderer and the npm package, which is also called
  `rtf-atto`, unscoped. The name was free on npm on 2026-11-16. The
  separate-repository shape was proposed on 2026-11-04 and confirmed by
  his creating it.
- **the-stats-duck** pulls the core in as a git submodule at
  `third_party/rtf-atto`, the same way it pulls ReadStat, and lists the
  core's `.cpp` files in `CMakeLists.txt` as it does for ReadStat. The
  DuckDB table functions and the copy function live in the-stats-duck's
  `src/`, not in the core.
- **bedevere-wise** installs the npm package and mounts the renderer's
  output. It is not touched by this work.

Layout of `rtf-atto` (mine; the builder may adjust names):

```
rtf-atto/
  CMakeLists.txt            library target `rtf`, tests, the CLI
  LICENSE                   Apache-2.0
  README.md
  include/rtf/model.hpp     the document model (structs)
  include/rtf/reader.hpp    parse()
  include/rtf/writer.hpp    write()
  include/rtf/tables.hpp    logical_tables()
  include/rtf/json.hpp      to_json(), table_spec_from_json()
  src/tokenizer.cpp         bytes -> tokens
  src/reader.cpp            tokens -> model (group state, destinations)
  src/tables.cpp            row and cell assembly, logical tables
  src/encodings.cpp         code page tables, \u handling
  src/json.cpp              the JSON writer and the small JSON reader
  src/writer.cpp            TableSpec -> RTF bytes
  tools/rtf2json.cpp        CLI: file in, JSON (or text, or tables) out
  test/cpp/*.cpp            plain C++ tests, one executable per area
  test/fixtures/*.rtf       hand-written fixtures
  test/fixtures/*.json      golden model of each fixture
  test/tools/rtf_text.py    the independent plain-text extractor
  test/local/samples.sh     the local-only check over RTF_SAMPLES_DIR
  wasm/CMakeLists.txt       Emscripten build of the C ABI
  wasm/exports.cpp          the C ABI
  js/package.json           the npm package
  js/src/index.ts           load(), parse(), logicalTables(), write(), renderHtml(), mount()
  js/src/model.ts           TypeScript types of the model
  js/src/render.ts          the renderer: model -> HTML string
  js/rtf.css                the stylesheet the renderer's HTML relies on
  js/demo/index.html        a local page: pick a file, see it rendered (not published)
  js/test/*.test.ts         node:test over the shared fixtures and the HTML goldens
  .github/workflows/        build + test (Linux, macOS, Windows), wasm, publish
```

Plan 1 adds to this tree: `docs/plans/` (this document and the plans),
`include/rtf/text.hpp` and `include/rtf/version.hpp`, `src/builder.cpp`,
`src/text.cpp` and the generated `src/codepages.inc`,
`tools/gen_codepages.py`, and under `test/tools/` the scripts
`compare_text.py`, `make_fixtures.py` and `regen_goldens.py` (2026-11-16).

### Toolchain

- CMake 3.20+, any C++17 compiler. Local verification on Massimo's machine
  uses MSVC (VS 2022) and TDM-GCC, as the-stats-duck does.
- Emscripten for the npm module: a current emsdk, not duckdb-wasm's 3.1.71,
  because the stand-alone module shares nothing with duckdb-wasm but source
  (memory `project_duckdb_wasm_version`). Flags (mine): `-O2`,
  `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node
  -sALLOW_MEMORY_GROWTH=1 -sFILESYSTEM=0 -fno-exceptions -fno-rtti`,
  exports limited to the C ABI below. Target size under 300 KB of wasm.
- Node 20+ for the TypeScript wrapper, `tsc` for types and build,
  `node:test` for tests (mine: no test framework dependency).
- Python 3 for the independent text extractor and the golden regeneration
  script.

## The document model

The model is the contract between the reader and every consumer. It is
defined once as C++ structs in `model.hpp` and once as TypeScript types in
`model.ts`; the JSON produced by `to_json` is the same shape as the
TypeScript types. `document.version` names the model version; it starts at
1.

Changing the model (mine): a builder may add an optional field during a
plan when a fixture or a consumer needs it; the pull request that adds it
amends this document in the same change, and the version stays at 1 until
the first npm release. Renaming or removing a field, or changing what a
field means, goes through this document first, bumps the version and
regenerates the goldens.

Conventions (mine):

- Lengths are in twips (1/20 point), as RTF states them. Font sizes are in
  half-points, as RTF states them. The viewer converts.
- Colours are indexes into `document.colors`, whose entry `null` means
  "automatic"; fonts are RTF font numbers, the keys of `document.fonts`.
  A colour or font property that is absent means the automatic colour or
  the default font.
- Every paragraph, run, row and cell carries its full effective properties,
  resolved through the group stack, not a delta. To keep the JSON small,
  properties at their default value are omitted from the JSON and the
  TypeScript types mark them optional with the default in a comment.
- Text is UTF-8 in C++ and a JavaScript string in the browser. Special
  characters map to Unicode: `\~` to U+00A0, `\_` to U+2011, `\-` to
  U+00AD, `\emdash` to U+2014, `\endash` to U+2013, `\bullet` to U+2022,
  `\lquote` `\rquote` `\ldblquote` `\rdblquote` to U+2018 U+2019 U+201C
  U+201D, `\emspace` `\enspace` `\qmspace` to U+2003 U+2002 U+2005,
  `\zwj` `\zwnj` to U+200D U+200C.

The shape, in TypeScript:

```ts
interface RtfDocument {
  version: 1;
  generator?: string;                 // {\*\generator ...}
  info: Record<string, string>;       // title, author, operator, company, ...;
                                      // times as ISO 8601 strings
  codePage: number;                   // \ansicpg, else 1252 (\mac 10000, \pc 437, \pca 850)
  defaultFont: number;                // \deff
  defaultLanguage?: number;           // \deflang
  fonts: Record<number, Font>;        // key = RTF font number; Word numbers its theme
                                      // fonts up to 31586 (seen 2026-11-16), so no array
  colors: (Color | null)[];           // index = RTF colour number; null = auto
  styles: Style[];                    // names only; not applied (see "Styles")
  page: PageSetup;                    // document-level \paperw \paperh \margl ... \landscape \facingp
  sections: Section[];                // at least one
  warnings: Warning[];
}

interface Font { name: string; family: 'nil'|'roman'|'swiss'|'modern'|'script'|'decor'|'tech'|'bidi';
                 charset: number; pitch: 'default'|'fixed'|'variable'; codePage?: number; }
interface Color { r: number; g: number; b: number; }
interface Style { index: number; type: 'paragraph'|'character'|'section'|'table'; name: string;
                  basedOn?: number; next?: number; }
interface PageSetup { width: number; height: number; landscape: boolean;
                      marginTop: number; marginBottom: number; marginLeft: number; marginRight: number;
                      headerDistance: number; footerDistance: number; facingPages?: boolean; }
interface Warning { code: string; message: string; offset: number; }   // offset = byte in the input

interface Section {
  page: PageSetup;                    // effective: document values overridden by \pgwsxn ... \lndscpsxn
  break: 'page'|'none'|'column'|'even'|'odd';   // \sbkpage (default) \sbknone \sbkcol \sbkeven \sbkodd
  columns?: number;                   // \cols, default 1
  titlePage?: boolean;                // \titlepg
  pageNumberStart?: number;           // \pgnstarts with \pgnrestart
  headers: { default?: Block[]; first?: Block[]; left?: Block[]; right?: Block[] };
  footers: { default?: Block[]; first?: Block[]; left?: Block[]; right?: Block[] };
  body: Block[];
}

type Block = Paragraph | Table;

interface Paragraph {
  kind: 'paragraph';
  props: ParagraphProps;
  listText?: Inline[];                // the already formatted number or bullet (\pntext, \listtext)
  runs: Inline[];
}

interface ParagraphProps {
  align?: 'left'|'center'|'right'|'justify';   // default left
  indentLeft?: number; indentRight?: number; indentFirst?: number;   // \li \ri \fi
  spaceBefore?: number; spaceAfter?: number;   // \sb \sa
  lineSpacing?: number; lineSpacingMultiple?: boolean;  // \sl \slmult
  keepTogether?: boolean; keepWithNext?: boolean; pageBreakBefore?: boolean;  // \keep \keepn \pagebb
  widowControl?: boolean;
  style?: number;                     // \s
  list?: { id: number; level: number };    // \ls \ilvl
  tabs?: TabStop[];
  borders?: Borders; shading?: Shading;
  rtl?: boolean;
}
interface TabStop { position: number; align: 'left'|'center'|'right'|'decimal'|'bar';
                    leader: 'none'|'dots'|'hyphens'|'underline'|'thick'|'equal'; }

type Inline = TextRun | LineBreak | Tab | PageBreak | ColumnBreak | Field | Bookmark
            | Footnote | Picture | Shape | EmbeddedObject;

interface TextRun { kind: 'text'; text: string; props: CharProps; }
interface CharProps {
  font?: number; size?: number;       // \f \fs (half-points, default 24)
  bold?: boolean; italic?: boolean; underline?: 'none'|'single'|'double'|'word'|'dotted'|'dash'|'wave'|'thick';
  strike?: boolean; doubleStrike?: boolean;
  script?: 'normal'|'super'|'sub';    // \super \sub \nosupersub
  offset?: number;                    // \up \dn, half-points, signed
  color?: number; background?: number; highlight?: number;   // absent = automatic
  caps?: boolean; smallCaps?: boolean; hidden?: boolean;   // \caps \scaps \v
  outline?: boolean; shadow?: boolean;
  language?: number;                  // \lang
  deleted?: boolean; inserted?: boolean;   // \deleted \revised
}
interface LineBreak { kind: 'lineBreak'; }          // \line
interface Tab { kind: 'tab'; }                      // \tab or a raw tab
interface PageBreak { kind: 'pageBreak'; }          // \page
interface ColumnBreak { kind: 'columnBreak'; }      // \column
interface Field { kind: 'field'; instruction: string; result: Inline[];   // {\*\fldinst} {\fldrslt}
                  dirty?: boolean; locked?: boolean; }
interface Bookmark { kind: 'bookmark'; name: string; position: 'start'|'end'; }
interface Footnote { kind: 'footnote'; endnote?: boolean; blocks: Block[]; }
interface Picture { kind: 'picture'; format: 'png'|'jpeg'|'emf'|'wmf'|'dib'|'bmp'|'unknown';
                    data: string;                   // base64
                    width?: number; height?: number;            // \picw \pich, pixels or source units
                    goalWidth?: number; goalHeight?: number;    // \picwgoal \pichgoal, twips
                    scaleX?: number; scaleY?: number; }
interface Shape { kind: 'shape'; blocks: Block[]; picture?: Picture; }   // \shptxt text and the pib picture; geometry dropped
interface EmbeddedObject { kind: 'object'; className?: string; result: Block[]; }  // \object: the \result branch

interface Table { kind: 'table'; rows: Row[]; }
interface Row {
  props: RowProps;
  cells: Cell[];
}
interface RowProps {
  header?: boolean; keep?: boolean;   // \trhdr \trkeep
  align?: 'left'|'center'|'right';    // \trql \trqc \trqr
  left?: number;                      // \trleft
  height?: number; heightRule?: 'auto'|'atLeast'|'exact';   // \trrh (negative = exact)
  gapHalf?: number;                   // \trgaph
  padding?: Padding;                  // \trpadd*, row defaults for the cells
  borders?: Borders;                  // \trbrdr*
  rtl?: boolean;
}
interface Cell {
  right: number;                      // \cellx, twips from the row's left
  width: number;                      // derived: right minus the previous cell's right (or left)
  preferredWidth?: { type: 'auto'|'percent'|'twips'; value: number };   // \clwWidth \clftsWidth
  borders?: Borders;                  // \clbrdr*
  shading?: Shading;                  // \clcbpat \clcfpat \clshdng
  verticalAlign?: 'top'|'center'|'bottom';   // \clvertalt (default) \clvertalc \clvertalb
  textDirection?: 'lrtb'|'tbrl'|'btlr'|'lrtbv'|'tbrlv';
  padding?: Padding;                  // \clpad*, explicit, else the row's
  mergeHorizontal?: 'first'|'continued';   // \clmgf \clmrg
  mergeVertical?: 'first'|'continued';     // \clvmgf \clvmrg
  noWrap?: boolean;
  blocks: Block[];                    // paragraphs and nested tables
}
interface Borders { top?: Border; left?: Border; bottom?: Border; right?: Border;
                    horizontal?: Border; vertical?: Border; }
interface Border { style: 'single'|'double'|'dotted'|'dashed'|'thick'|'hairline'|'triple'|'none'|'other';
                   width: number; color?: number; space?: number; }
interface Shading { background?: number; foreground?: number; percent?: number; pattern?: string; }
interface Padding { top?: number; left?: number; bottom?: number; right?: number; }
```

Notes on the shape:

- **A page is a section where the producer says so.** SAS writes one
  section per page, and every section break defaults to a page break
  (`\sbkpage`), so a viewer that starts a new page at each section with
  `break: 'page'` shows SAS output exactly as Word does. For a Word
  document with one long section, the viewer must paginate itself or show
  it as a scroll; the reader cannot know where Word breaks a page.
- **Nothing states how many rows fit on a page.** RTF has no such
  control word. A page fills by height: the page height minus the margins
  and the header and footer, divided by the row heights, which Word
  computes from the fonts at display time unless a row carries an exact
  `\trrh`. SAS does that computation when it writes the file and records
  the result as one section per page (about 38 rows per page in the
  sample); Word redoes nothing for those files because every section is
  already a page. The reader reports sections and explicit breaks and
  nothing else; the renderer shows a section as a page.
- **Fields keep their instruction.** SAS writes `PAGE` and `NUMPAGES`
  fields with no result at all (verified on 2026-11-10 in every sample
  file). The reader reports `instruction: "PAGE"` and `result: []`. A
  viewer that shows one section per page substitutes the section's number
  for `PAGE` and the section count for `NUMPAGES`. That rule goes in the
  npm README as the viewer's contract.
- **Tables are blocks, cells hold blocks.** A nested table is a `Table`
  block inside a cell's `blocks`. Horizontal and vertical merges are flags
  on the cell; the viewer draws spans from them.
- **Lists are text.** Word writes the formatted number or bullet in
  `\pntext` or `\listtext`; the reader keeps it as `listText` on the
  paragraph and drops the numbering definitions. Any viewer can show the
  text; none has to understand list levels (mine). Word's bullet is the
  byte 0xB7 in a Symbol-charset font, which the decoder maps to U+F0B7
  (seen 2026-11-16); the renderer may show it as U+2022.
- **Styles are names.** Word writes every property explicitly in the text,
  so applying style inheritance would change nothing for Word or SAS
  files. The reader records the style table and each paragraph's style
  index, and does not apply the style's properties (mine; a known
  limitation for producers that rely on styles alone).
- **Warnings are part of the document.** Codes are short snake_case
  strings such as `unknown_destination`, `unbalanced_group`,
  `unsupported_code_page`, `object_data_dropped`, `shape_geometry_dropped`,
  `depth_limit`. The exact list is the builder's.

## The reader

### Tokenizer

The tokenizer turns bytes into tokens: group open, group close, control
word with optional signed parameter, control symbol, hex byte `\'hh`,
Unicode escape `\uN`, binary blob `\binN`, text. Rules, from the
specification:

- A control word is `\` followed by letters, then an optional signed
  decimal parameter. The parameter is read as a 32-bit signed integer; a
  longer digit run is clamped with a warning. One space after the control
  word is part of the token; any other delimiter is not consumed.
- A control symbol is `\` followed by one non-letter byte. `\{`, `\}` and
  `\\` are text. `\` followed by CR or LF is `\par`. `\*` marks the next
  destination as ignorable when unknown. `\:` and `\|` are ignored.
- CR and LF outside `\bin` are ignored. A raw tab is a `\tab`. A raw byte
  at or above 0x80 outside an escape is decoded with the current code
  page.
- `\binN` is followed by one space and N raw bytes. N is clamped to the
  bytes remaining.
- The input must start with `{\rtf` after an optional UTF-8 BOM and
  whitespace. Anything else is the error `not_rtf`. The version parameter
  is recorded and not checked.

### Group state

Each `{` pushes a copy of the current state: character properties,
paragraph properties, section properties (new copies on `\sectd`), the
current destination, the current `\uc` count, the current font's code
page. Each `}` pops it. `\plain` resets character properties to the
defaults, `\pard` paragraph properties, `\sectd` section properties.
Unknown control words are ignored, as the specification requires. An
unknown destination introduced with `\*` is skipped to its closing brace
with one `unknown_destination` warning per name per document. An unknown
destination without `\*` is read as text (the specification's rule), which
is what keeps text from being lost.

A missing `}` at the end of the input closes every open group with an
`unbalanced_group` warning. An extra `}` is ignored with the same warning.
Group depth above the cap yields a `depth_limit` warning and the deeper
groups are flattened into the current one.

### Destinations the reader understands

Document tables: `\fonttbl` (per font: number, family, `\fcharset`,
`\fprq`, `\cpg`, name, `\*\falt` ignored, `\*\panose` skipped),
`\colortbl`, `\stylesheet` (names, types, `\sbasedon`, `\snext`; its `\*\cs`
and `\*\ts` entries are character and table styles, not ignorable
destinations), `\info` (every string and time destination; Word's numeric
entries such as `{\nofpages1}` become strings), `\*\generator`.

Section parts: `\header`, `\footer`, `\headerf`, `\footerf`, `\headerl`,
`\headerr`, `\footerl`, `\footerr`. Each group's blocks attach to the
section being defined. `\header` and `\footer` fill `default`; the suffixed
forms fill `first`, `left` and `right`.

Text containers: `\field` with `\*\fldinst` and `\fldrslt` (nested fields
allowed), `\footnote` (with `\ftnalt` marking an endnote), `\*\bkmkstart`
and `\*\bkmkend`, `\pntext` and `\listtext`, `\shp` with `\shptxt` and,
inside `\*\shpinst`, the property `{\sp{\sn pib}{\sv{\pict ...}}}`, which
is how Word 2016 and later write an inline picture (seen 2026-11-16); the
rest of `\*\shpinst` is geometry and is skipped, and so is the `\shprslt`
fallback. `\object` with `\result` (`\*\objdata` dropped, `\*\objclass`
kept as `className`).

Pictures: `\pict` with `\pngblip`, `\jpegblip`, `\emfblip`, `\wmetafile`,
`\dibitmap`, `\wbitmap`, sizes, scaling; the hex payload or a `\bin`
payload becomes the bytes. `\*\shppict` is read and its `\nonshppict`
fallback skipped.

Dual groups: `\upr` holds an ANSI branch and a `\*\ud` branch with the same
content; the reader takes the `\ud` branch and drops the other, which is
how SAS writes its bookmarks (`{\upr{\*\bkmkstart X}{\*\ud{\*\bkmkstart X}}}`).

Skipped with a warning, text never inside them: `\*\listtable`,
`\*\listoverridetable`, `\*\pn` (the definition; `\pntext` is kept),
`\*\revtbl`, `\*\rsidtbl`, `\*\themedata`, `\*\colorschememapping`,
`\*\datastore`, `\*\latentstyles`, `\*\xmlnstbl`, `\*\pgdsctbl`,
`\*\protusertbl`, `\*\mmathPr`, `\*\wgrffmtfilter`, `\*\background`,
`\*\do`, `\*\annotation` and the other annotation destinations,
`\*\objdata`, `\*\blipuid`, `\*\picprop`, `\*\htmltag`, `\*\mhtmltag`,
`\*\xe`, `\*\tc`, `\*\ftnsep` and its siblings, `\*\atnid`, `\userprops`,
and four destinations that Word writes without `\*` although they hold
only fallback copies or settings: `\nonesttables` (the text of a nested
table repeated for old readers), `\shprslt` (the shape rendered as a
metafile), `\nonshppict` and `\mmathPr` (all seen 2026-11-16). Any other
`\*` destination is skipped by the general rule.

### Text decoding

- `\'hh` is decoded with the code page of the current font when the font
  table gives one (`\fcharset` mapped through the standard charset table:
  0 and 1 to the document code page, 2 to Symbol, 77 to 10000, 161 to
  1253, 162 to 1254, 163 to 1258, 177 to 1255, 178 to 1256, 186 to 1257,
  204 to 1251, 222 to 874, 238 to 1250, 254 to 437, 255 to 850; `\cpg`
  overrides), else with the document code page.
- Built-in code pages: 1250 to 1258, 874, 437, 850, 852, 866 and 10000
  (Mac Roman). Each is a 128-entry table. An unsupported code page yields
  U+FFFD per byte and one `unsupported_code_page` warning.
- A Symbol-charset font (2) maps bytes to U+F000 plus the byte, the Word
  convention, so a viewer can still pick a symbol font; one
  `symbol_font` warning.
- `\uN`: N below 0 is N + 65536. Two consecutive escapes that form a
  surrogate pair become one code point. After the escape the reader skips
  the current `\uc` count of fallback units, where a unit is one text
  byte or one `\'hh`; the skip stops early at a brace or a control word.
- `\ucN` sets the count for the current group and its children. The
  document default is 1.

### Paragraphs and runs

Text accumulates in the current paragraph as runs; a run ends when a
character property changes. `\par` ends the paragraph. `\line`, `\tab`,
`\page`, `\column` are inlines. `\sect` ends the paragraph and the section.
The end of a header, footer, footnote, cell or field group ends the
paragraph it was building. A paragraph with no runs that exists only
because a destination closed is dropped unless it is the only content of
its container.

### Tables

A paragraph with `\intbl` (or `\itap` above 0) belongs to a table. Rows
and cells are assembled as the specification and Word's output require:

- `\trowd` starts a row definition and resets row and cell definitions.
  `\cellxN` closes one cell definition at right edge N. Cell definitions
  are matched to cells in order.
- `\cell` ends a cell (its paragraphs go into the cell), `\row` ends the
  row. Word writes the row's definition twice, before the cells and again
  just before `\row`; the definition seen last before `\row` wins.
- A row that ends at the end of the input or at a section boundary without
  `\row` is closed with a warning.
- More cells than definitions: the extra cells get the width of the last
  definition. Fewer cells than definitions: the missing cells are empty.
- Nested tables (`\itapN` with N above 1, `\nestcell`, `\nestrow`,
  `{\*\nesttableprops ... \nestrow}`) become `Table` blocks inside the
  cell of the enclosing level. Depth is bounded by the group cap.
- Consecutive rows form one `Table` block. A non-table paragraph, a
  section break or a change of nesting level ends the table. Two tables
  separated only by a row-less `\trowd` stay one table.
- Merges: `\clmgf` and `\clmrg` set `mergeHorizontal`; `\clvmgf` and
  `\clvmrg` set `mergeVertical`. The reader does not remove merged cells,
  the viewer does. Word writes a horizontal merge as one wider cell and
  never emits `\clmgf` (the merged-cells sample of 2026-11-16 has
  `\clvmgf` and `\clvmrg` only), so consumers derive column spans from
  cell edges in every case; the flags cover other producers.
- Cell padding: Word and SAS both write `\clpadt` for the left padding
  and `\clpadl` for the top one, the reverse of the specification's
  names (both write `\clpadt10\clpadr10` for the usual left and right
  margins; checked 2026-11-16). The reader follows the producers:
  `\clpadt` fills `left`, `\clpadl` fills `top`, `\clpadb` `bottom` and
  `\clpadr` `right`. The row-level `\trpaddl` and its siblings mean what
  their names say.
- Borders: `\clbrdrt` and its siblings open a border definition filled by
  the following `\brdr*` words until the next cell or border word.
  `\brdrnone` and width 0 give `style: 'none'`.

### Sections and page setup

Document-level `\paperw`, `\paperh`, `\margl`, `\margr`, `\margt`,
`\margb`, `\landscape`, `\facingp` fill `document.page`. `\sectd` resets
the current section's properties to those values, then `\pgwsxn`,
`\pghsxn`, `\marglsxn` and siblings, `\lndscpsxn`, `\headery`, `\footery`,
`\sbk*`, `\cols`, `\titlepg`, `\pgnstarts` and `\pgnrestart` override.
`\sectd` does not start a section and does not drop headers already read:
Word writes it three times for one section, around its footnote separator
groups (seen 2026-11-16). `\sect` closes the section; the next one starts
with the same properties and no headers. A document with no `\sect` has
one section. A header or footer group attaches to the section being read
when it appears, which for Word and SAS is right after `\sectd`.
`landscape` is a flag only: both producers write the swapped paper size
themselves (`\paperw16838\paperh11906` with `\lndscpsxn` and no
`\landscape`), so nothing swaps width and height.

Word writes `\headerr` and `\footerr`, and no `\header` or `\footer`, for
a document without `\facingp` (all three Word samples, 2026-11-16). The
reader stores them as `right`, as the file says. A consumer takes
`default`, else `right`, else `left` for a section without `facingPages`;
the renderer rule below says the same.

## Logical tables

`logical_tables(document)` returns the tables a reader of the printed
document would see as one table, stitched across pages. The rules (mine):

1. Walk every section's `body` in order. Each `Table` block is a part.
2. Two consecutive parts belong to the same logical table when they are
   separated only by section breaks, page breaks and empty paragraphs, and
   their header rows (rows with `header: true`) are equal in cell edges and
   in text. A part with no header rows joins the previous logical table
   when its first row's cell edges are a subset of the previous part's data
   row edges, else it starts a new logical table.
3. The header rows of parts after the first are dropped; the data rows are
   concatenated.
4. The column grid is the sorted union of every data row's cell edges. A
   cell that spans several grid columns is reported with `span`. A row
   whose cells are all empty is a spacer and is flagged, not removed.
5. Column names come from the header rows: the last header row's cells
   name the grid columns they cover; a cell in an earlier header row that
   covers several columns prefixes its text to each of them, joined with a
   space. Line breaks inside header text become spaces. Empty names become
   `column_N`; duplicates get `_2`, `_3`.

The result:

```ts
interface LogicalTable {
  id: number;                         // 1-based, in document order
  firstSection: number; lastSection: number;   // 0-based section indexes
  parts: { section: number; block: number }[];
  edges: number[];                    // the grid, twips
  columnNames: string[];
  headerRows: Row[];                  // from the first part
  rows: { cells: { text: string; column: number; span: number }[]; spacer: boolean; indent: number }[];
}
```

`text` is the cell's plain text: runs joined, `lineBreak` as "\n",
`tab` as "\t", fields by their result, U+00A0 kept. `indent` is the first
paragraph's `indentLeft`, which is how SAS shows nested terms.

## The writer

`write(TableSpec)` returns the bytes of a complete RTF file in the style of
the SAS sample: one section, titles in the page header, footnotes in the
page footer, one body table whose header rows repeat, Word paginating.

```ts
interface TableSpec {
  columns: { label: string; width?: number; align?: 'left'|'center'|'right' }[];   // label may contain "\n"
  rows: (string | null)[][];
  titles?: string[];                  // one paragraph each, top of every page
  footnotes?: string[];               // one paragraph each, bottom of every page
  pageNumbers?: boolean;              // "Page X of Y" top right, default true
  rowsPerPage?: number;               // when set, one section per page like SAS; else Word paginates
  page?: { width?: number; height?: number; landscape?: boolean;      // twips; default A4 landscape
           marginTop?: number; marginBottom?: number; marginLeft?: number; marginRight?: number };
  font?: { name?: string; size?: number };   // default "Courier New", 9 points
  nullString?: string;                // default ""
  generator?: string;                 // written to {\*\generator}; default "rtf-atto <version>"
}
```

What the writer emits (mine, modelled on the sample; the builder keeps the
control words the sample uses so Word shows the same thing):

- Header: `{\rtf1\ansi\ansicpg1252\uc1\deff0\deflang1033`, a font table
  with one font (`\fmodern\fprq1` for a fixed-pitch name, `\fswiss` for
  Arial-like names, `\froman` otherwise; `\fcharset0`), a colour table
  with black and white, an `\info` group with `\creatim` and the
  generator, the page setup (`\paperw`, `\paperh`, margins, `\landscape`
  when landscape), then `\sectd` with `\lndscpsxn`, `\pgwsxn`, `\pghsxn`,
  the `*sxn` margins, `\headery` and `\footery`.
- `{\header ...}`: a table of one centred full-width cell per title line
  (`\trowd\trkeep\trqc`, `\cellx` at the text width, `\pard\plain\intbl`,
  the first title at `\fs` + 4). When `pageNumbers` is on, the first title
  row has three cells: empty, the title, and `Page {\field{\*\fldinst
  { PAGE }}{\fldrslt }} of {\field{\*\fldinst { NUMPAGES }}{\fldrslt }}`
  right-aligned. The result destination is written empty (the spec
  requires one, SAS omits it, Word computes it either way).
- `{\footer ...}`: one left-aligned full-width cell per footnote.
- Body: header rows carry `\trhdr\trkeep`, a top border on the first and a
  bottom border on the last (`\clbrdrt\brdrs\brdrw10\brdrcf1`), cells
  `\clvertalb`; data rows carry `\trkeep`, cells `\clvertalt`; the last
  data row carries a bottom border. Every cell: `\clcbpat` white,
  `\cltxlrtb`, `\cellx` cumulative; every paragraph
  `\pard\plain\intbl\sb0\sa0` plus `\ql`, `\qc` or `\qr`, `\f0\fsN\cf1`.
- Column widths: `width` when given. Otherwise the longest line of the
  column (label lines and values) in characters, times the character
  advance (0.6 em for a fixed-pitch font, 0.5 em otherwise, in twips at
  the font size), plus 80 twips of padding. If the sum exceeds the text
  width (page width minus margins), every width is scaled down in
  proportion; Word then wraps.
- Text: `\`, `{`, `}` escaped; a newline becomes `\line`; a tab `\tab`;
  U+00A0 `\~`; every code point above 0x7F becomes `\uN?` (UTF-16 units,
  negative above 32767, pairs for the astral planes). The output is pure
  ASCII. A null cell shows `nullString`.
- Table alignment: centred on the page (`\trqc`), as the sample.
- `rowsPerPage`: when set, the writer does what SAS does. It cuts the data
  rows into pages of that many rows and writes one section per page, each
  with its own header and footer groups and its own body table that starts
  with the header rows. The renderer then shows the same pages Word shows,
  and `PAGE` and `NUMPAGES` agree with the section count. The caller
  chooses the number; the writer does not measure text. When unset, the
  writer emits one section and Word breaks the pages (mine).
- Spanning header groups and column labels longer than a cell are not in
  the first release; the shape of `TableSpec` leaves room (`spanners`)
  for a later increment (Massimo, 2026-11-10: "display labels and
  spanning headers later").

## The npm package

The package is `rtf-atto` (Massimo, 2026-11-16). API (mine):

```ts
import { load } from 'rtf-atto';
const rtf = await load({ wasm?: Uint8Array | URL | string });   // loads the wasm once
const doc: RtfDocument = rtf.parse(bytes);          // throws RtfError on not_rtf
const json: string = rtf.parseToJson(bytes);        // the cheapest path; same content
const tables: LogicalTable[] = rtf.logicalTables(doc);   // or rtf.logicalTablesJson(bytes)
const out: Uint8Array = rtf.write(spec);            // TableSpec -> bytes
rtf.version;                                        // the core's version string

import { renderHtml, mount } from 'rtf-atto';      // pure TypeScript, no wasm needed
const html: string = renderHtml(doc, options);      // works in Node too
const view = mount(doc, containerElement, options); // browser: sets innerHTML, returns { pages }
```

- The wasm is located with `new URL('./rtf.wasm', import.meta.url)`;
  `load({ wasm })` accepts bytes or a URL for bundlers that cannot follow
  that pattern. In Node the file is read with `fs`. The package is ESM
  only, `"type": "module"`, with `exports` for `.` and `./rtf.wasm`.
- The C ABI the wrapper calls, exported from `wasm/exports.cpp`:
  `rtf_parse(const uint8_t*, size_t, uint8_t** out, size_t* out_len) -> int`,
  `rtf_logical_tables(...)` (same shape), `rtf_write(const char* json,
  size_t, uint8_t** out, size_t* out_len) -> int`, `rtf_free(void*)`,
  `rtf_version() -> const char*`, plus `malloc` and `free`. Return 0 on
  success, a negative code on failure with the error message in `out`.
  JSON crosses the boundary as UTF-8; the wrapper calls `JSON.parse`.
- `model.ts` is maintained by hand and checked by a test that parses every
  golden JSON against the types (a runtime shape check over the goldens;
  mine).
- Tests: `node:test` runs every fixture through the wasm and compares the
  JSON with the committed golden, byte for byte after canonical
  formatting, so the wasm and the native build must agree.
- CI: build the wasm on Linux with emsdk, run the tests, publish on a
  `v*` tag with provenance. Publishing needs an npm token in the
  repository secrets (Massimo's).

## the-stats-duck integration

The-stats-duck adds the core as a submodule and four SQL surfaces. Design
(mine, since import is secondary; names and options can change before the
plan):

```sql
SELECT * FROM read_rtf('report.rtf', table := 1);
SELECT * FROM rtf_tables('report.rtf');
SELECT * FROM rtf_cells('report.rtf');
COPY (SELECT ...) TO 'out.rtf' (FORMAT rtf, titles ['Table 1', 'Demographics'],
                                footnotes ['Source: ...'], font 'Courier New', font_size 9);
```

- `read_rtf(path, table := N)` returns the data rows of logical table N
  (default 1) as VARCHAR columns named by `columnNames`, spacer rows
  dropped, a cell with `span` above 1 filling its first column and NULL in
  the rest. An error names the table count when N is out of range.
- `rtf_tables(path)` returns one row per logical table: `table_id`,
  `first_section`, `last_section`, `pages`, `header_rows`, `data_rows`,
  `columns`, `column_names VARCHAR[]`, `first_header VARCHAR` (the header
  text joined, for a human to pick a table).
- `rtf_cells(path)` is the long format for everything else: `section`,
  `part` ('header', 'body', 'footer'), `block`, `table_id` (NULL outside a
  logical table), `row`, `col`, `is_header`, `text`. Titles and footnotes
  are reachable here, since in SAS output they are table cells in the
  header and footer groups.
- All three read the whole file through DuckDB's `FileSystem`, not
  `fopen`, so `httpfs` paths and duckdb-wasm's registered files work; the
  bytes go to `rtf::parse` once at bind, and the model lives in the bind
  data. Files are small (the largest sample is 700 KB) and the model is
  needed for the schema, so bind-time parsing is the right trade.
- `COPY ... TO (FORMAT rtf)` registers a `CopyFunction` with
  `copy_to_bind`, `copy_to_initialize_global`, `copy_to_initialize_local`,
  `copy_to_sink`, `copy_to_combine`, `copy_to_finalize` and
  `REGULAR_COPY_TO_FILE` execution mode (single writer, insertion order
  kept). The sink casts every column to VARCHAR (`NULL` to `nullstr`) and
  buffers rows in the global state; finalize builds the `TableSpec` from
  the column names and options and writes the file through `FileSystem`.
  The signatures of these six callbacks are the same in `copy_function.hpp`
  on 1.4.3 and 2.0.0 (checked 2026-11-10); 2.0.0 adds an optional
  `CopyFunctionInfo` to the bind input, which the compat header may need
  to paper over. Options: `titles`, `footnotes`, `labels` (overrides the
  column names), `align` (a list of 'l', 'c', 'r'), `widths` (twips),
  `landscape` (default true), `paper` ('a4' default, 'letter'), `font`,
  `font_size`, `nullstr`, `page_numbers`.
- Registration follows `read_stat_function.cpp`'s pattern through
  `ExtensionLoader`. The function docs net in `test/sql/function_docs.test`
  counts registered functions and must move by three.
- `scripts/check-wasm-string-hash.sh` scans `src/` only; it gains the
  submodule's sources or the core's own CI runs the same check (the latter,
  mine).
- The wasm flavours of the extension compile the core through
  extension-ci-tools without changes, as they do ReadStat.
  `scripts/wasm-verify` gains a `read_rtf` probe over a fixture registered
  as a file buffer.

## The renderer

The renderer is TypeScript in the npm package and needs no wasm: it takes
an `RtfDocument` and returns an HTML string. `mount` is a thin browser
helper around it. bedevere-wise calls one of the two.

```ts
interface RenderOptions {
  pageNumbers?: 'sections' | 'none';      // default 'sections': PAGE and NUMPAGES per section
  fonts?: Record<string, string>;         // RTF font name -> CSS font-family override
  pxPerTwip?: number;                     // default 1/15 (96 dpi)
  pictures?: boolean;                     // default true
  classPrefix?: string;                   // default 'rtf-'
}
```

Rules (mine; the builder keeps them unless a fixture proves one wrong):

- **Pages.** Each section with `break: 'page'` (the default, and the SAS
  case) starts a new page. Inside a section, a `pageBreak` inline and a
  paragraph with `pageBreakBefore` start a new page too. A page is a
  `<section class="rtf-page">` sized from the section's `PageSetup` in CSS
  pixels, with the margins as padding, the header at `headerDistance` from
  the top, the footer at `footerDistance` from the bottom and the body
  between them. A page whose content is taller than the page grows; nothing
  is clipped or re-flowed into a next page.
- **Headers and footers.** `default` on every page of the section, and
  when `default` is absent, `right`, else `left` (Word writes only
  `\headerr` for a document without facing pages); `first` on the first
  page when `titlePage` is set; `left` and `right` on even and odd pages
  when `facingPages` is set.
- **Fields.** `PAGE` becomes the page's number and `NUMPAGES` the page
  count when `result` is empty and `pageNumbers` is `'sections'`; otherwise
  the field's result is rendered. Other fields render their result.
- **Paragraphs and runs.** `<p>` with alignment, indents and spacing as
  inline styles; `<span>` per run with font, size, bold, italic, underline,
  strike, colour, background, superscript and subscript, small caps; hidden
  text is omitted; `lineBreak` is `<br>`; `tab` is a span with a fixed
  width (the default tab stop of 720 twips, or the paragraph's next tab
  stop); U+00A0 stays U+00A0.
- **Tables.** `<table>` with a `<colgroup>` from the union of the row
  edges, `colspan` from each cell's edges, `rowspan` from vertical merges
  (`mergeVertical: 'continued'` cells are not emitted), `<thead>` for
  header rows, borders, padding, shading and vertical alignment as inline
  styles, nested tables inside the cell.
- **Fonts.** `fonts[i].name` becomes a quoted CSS font-family followed by
  a generic fallback from the RTF family (`modern` to monospace, `swiss`
  to sans-serif, `roman` to serif, else sans-serif). `options.fonts`
  overrides a name.
- **Other inlines.** Bookmarks become `<a id>` anchors; footnotes a
  superscript marker with the note's blocks listed at the bottom of the
  page body; pictures `<img>` with a data URL for PNG and JPEG and a box
  with the format name otherwise; shapes and objects render their blocks.
- **Untrusted input.** Every text node is HTML-escaped. Style values are
  generated from numbers and from a fixed vocabulary; the only document
  string that reaches CSS is a font name, quoted and stripped of anything
  but letters, digits, spaces and hyphens. Data URLs are emitted only for
  PNG and JPEG with a size cap. No script, no event attributes, no external
  URLs.
- **Stylesheet.** `rtf.css` holds the page frame, the default table reset
  and the print rules (one page per sheet); everything that comes from the
  document is an inline style, so the HTML is self-contained without it.

## Validation

Acceptance is a checklist, each item a command that passes or fails:

1. **Goldens.** Every fixture in `test/fixtures` parses to its committed
   `.json` golden, byte for byte after canonical formatting (sorted keys,
   fixed indentation). `regen_goldens.py` rewrites them on purpose, and the
   diff is reviewed in the pull request like code.
2. **Text coverage.** For every fixture, the plain-text projection of the
   model equals the output of `test/tools/rtf_text.py`, an extractor of
   about 80 lines written independently of the C++ (different author
   session, no shared code). The projection rule: runs in document order;
   `tab` and `cell` as "\t"; `lineBreak`, `par`, `row` and `sect` as "\n";
   `pageBreak` as "\f"; header and footer text where their groups appear;
   a field's result, not its instruction; `listText` before the paragraph;
   footnote text in place; pictures and skipped destinations contribute
   nothing.
3. **No crash.** A mutation test in CTest takes every fixture, applies
   10,000 random byte-level edits (delete, insert, flip, truncate,
   duplicate a brace) and parses each result with a 1 s budget and a
   memory cap of 100 times the input; any fault, hang or cap breach fails.
   A libFuzzer target exists for longer runs on a developer's machine;
   CI runs the mutation test only.
4. **Round trip.** `logical_tables(parse(write(spec)))` has exactly one
   table whose column names, rows and alignments equal `spec`, for a set
   of specs: empty rows, one row, 300 rows, every column alignment,
   labels with line breaks, cell text with line breaks, tabs, braces,
   backslashes, accented and CJK characters, an emoji, a null cell, a
   custom width, portrait Letter, no page numbers.
5. **Determinism.** Parsing the same bytes twice gives the same JSON, and
   the native and wasm builds give the same JSON for every fixture.
6. **Renderer goldens.** `renderHtml` of every fixture equals its committed
   `.html` golden, and after tag stripping the HTML contains every text run
   of the model in document order (the text-coverage rule again, applied
   to the HTML). `renderHtml` runs in Node without a DOM.
7. **Renderer in the browser.** Massimo opens `js/demo/index.html`, loads
   the SAS fixtures and his own samples, and compares the pages with Word:
   same page count, same rows per page, same titles and footnotes, "Page X
   of Y" right, borders and alignment as in Word. Manual; only he can run
   it.
8. **Local samples.** `RTF_SAMPLES_DIR=<dir> test/local/samples.sh` parses
   every `.rtf` in the directory and compares each file against
   `<dir>/expected.txt`: section count, logical table count, column names
   per table, and the warning codes the file is allowed to produce. Any
   warning not listed fails, and so does any file whose text projection
   differs from `rtf_text.py`. The CLI writes `expected.txt` on the first
   run; Massimo reviews it once against what Word shows for each file,
   and from then on it is a regression baseline that lives next to his
   samples and is never committed (mine). SAS files are expected to list
   no warnings at all. Files saved from Word go in the same directory with
   a `W_` prefix and list the `unknown_destination` codes Word files
   produce by design. Massimo made three on 2026-11-16: `W_Disposition.rtf`,
   a SAS table re-saved by Word; `W_Features.rtf`, a portrait document
   with a table merged across and down, a bulleted and a numbered list, a
   picture, bold, italic, underlined, coloured and highlighted text, a
   header and a footer with a page number, a page break, a hyperlink,
   accented, Greek and Chinese letters and an emoji; `W_Sections.rtf`, a
   landscape document with a table inside a table cell. None of the three
   holds a footnote or a second section; the fixtures cover those. This
   check runs on Massimo's machine only; it is not in CI.
9. **DuckDB.** sqllogictest files `test/sql/read_rtf.test` and
   `test/sql/copy_rtf.test` in the-stats-duck over the submodule's
   fixtures, passing on 1.4.3 and 2.0.0 with the usual absolute-path
   invocation; a `COPY ... TO` into the test directory read back with
   `read_rtf`; the wasm-verify harness probe.
10. **Word.** Massimo opens a written file in Word and checks: titles and
   footnotes on every page, header rows repeated after a page break,
   "Page X of Y" correct, no stray characters. Only he can run this; the
   dev machine has neither Word nor LibreOffice (checked 2026-11-10).

Fixtures to write by hand (mine; the builder adds what the tests need):

- `sas_table_3pages.rtf`: the SAS layout from the survey: three sections,
  title and footnote tables in the header and footer groups with `PAGE`
  and `NUMPAGES` fields without results, a body table per section with two
  `\trhdr` rows, spanning headers by width, spacer rows, `\li288` and
  `\li576` indents, `\~`, `\line` inside a cell, `\keepn`, the three hex
  escapes, the `\upr`/`\ud` bookmark, `\clcbpat8`, `\clpad*`,
  `\clvertalb` on headers, Courier, A4 landscape.
- `sas_listing_1page.rtf`: one section, rows with two different cell
  layouts in one table, a long text column.
- `word_minimal.rtf`: a Word-style header with `\*\generator`,
  `\*\rsidtbl`, `\*\listtable`, `\*\themedata`, `\*\datastore`, `\*\latentstyles`,
  three paragraphs with bold, italic, underline, colour, size, a style.
- `word_tables.rtf`: merged cells both ways, a nested table, row heights,
  borders of every style, shading, `\trleft`, the duplicated row
  definition before `\row`, a row without `\row` at the end.
- `word_text.rtf`: `\u` escapes with `\uc1` and `\uc0`, a surrogate pair,
  cp1251 bytes under a `\fcharset204` font, a Symbol-charset run, every
  special character control word, `\bin`, raw bytes above 0x7F, raw tabs.
- `word_structure.rtf`: three sections with different page setups and
  break kinds, first-page and left/right headers, `\titlepg`, `\page`,
  `\pagebb`, `\column`, `\pgnstarts`, a footnote and an endnote, bookmarks,
  nested fields, a hyperlink field with a result, a `\pntext` list and a
  `\listtext` list.
- `pictures.rtf`: a 1-pixel PNG as hex, the same inside `\*\shppict` with
  a `\nonshppict` fallback, an EMF stub, a `\bin` payload, a shape with
  `\shptxt`, an `\object` with a `\result`.
- `malformed_*.rtf`: missing final brace, extra braces, truncated in the
  middle of a control word, `\bin` past the end, nesting 2000 deep, a
  `\u` with a brace as fallback, a non-RTF file, an empty file, a
  32 MB file of repeated rows (generated at test time, not committed).

## Work order and size

This is a project with five implementation plans, run in this order,
which follows Massimo's priorities (display first, reporting second,
import third):

1. **The reader** in `rtf-atto`: tokenizer, group state, model and
   JSON, text decoding, paragraphs, tables, sections and headers, fields
   and the other inlines, pictures, the warnings, the fixtures and
   goldens, the text extractor, the mutation test, the CLI, the local
   samples script. On the order of ten tasks. The tokenizer and the
   model are verifiable alone; tables are the largest task and come after
   paragraphs; the SAS fixtures come before the Word ones.
2. **The npm package**: the C ABI, the Emscripten build, the wrapper and
   types, the node tests over the goldens, CI and publishing. Three to
   four tasks.
3. **The renderer**: pages, paragraphs and runs, tables, fields and the
   other inlines, the stylesheet, the HTML goldens, the demo page,
   Massimo's browser check against Word. Three to four tasks.
4. **The writer**: `TableSpec`, the JSON reader for it, the writer with
   `rowsPerPage`, the round-trip tests, `write` in the wrapper, Massimo's
   Word check. Three to four tasks.
5. **the-stats-duck**: the submodule, `read_rtf`, `rtf_tables`,
   `rtf_cells`, the copy function, docs, CHANGELOG, the function docs net,
   wasm-verify, verification on 1.4.3 and 2.0.0 locally and on the CI
   matrix. Four to five tasks. It targets the release after 0.10, because
   the 0.10.0 milestone is closed at 21 issues, and gets its own issue
   there (mine; Massimo can pull it into 0.10 instead).

The logical-table helper is part of plan 1 (it is needed by the goldens of
the SAS fixtures and by the local samples check) and is exposed in plan 2.

If plan 1 grows beyond the reader's rules above, that is a signal to cut
the Word-only constructs into a later increment rather than to write a
longer plan; the SAS fixtures must pass first.

## Decisions already taken

Massimo's:

- The C++ core does not render; the renderer is TypeScript in the npm
  package, and bedevere-wise mounts its output (2026-11-16).
- The reader returns everything in the document, for display; the DuckDB
  import is secondary.
- General purpose, SAS first.
- Figures out of scope for now; listings in scope.
- The writer serves reporting: a table with fixed titles and footnotes in
  the page header and footer, Word paginates, header rows repeat.
- A JS library with its own wasm binary, separate from the DuckDB
  extension's.
- His sample files stay out of every repository; fixtures are written by
  hand.

Mine (overrule any of them):

- The core in its own repository as a submodule, two wasm binaries from
  one source, no DuckDB headers in the core (proposed 2026-11-04).
- The JSON model shape above, twips and half-points as RTF states them,
  defaults omitted from the JSON.
- Lists as text, styles as names, pictures as base64 bytes, shapes as
  their text, objects as their result.
- The built-in code page set; double-byte code pages deferred.
- No exceptions, no RTTI, hand-written JSON writer and reader, no
  dependencies.
- The C ABI with JSON across the boundary; ESM-only package; `node:test`.
- The logical-table rules and the SQL surface: three table functions,
  parse at bind, the copy function's options.
- The writer's control words, the width heuristic, pure ASCII output, an
  empty `\fldrslt`, the optional `rowsPerPage` that writes a section per
  page like SAS.
- The renderer's output shape (an HTML string plus `rtf.css`), 96 dpi,
  sections as pages without re-flow, PNG and JPEG shown, the escaping
  rules.
- Apache-2.0.
- This document lives in `rtf-atto/docs/plans/` from 2026-11-16; the copy
  in the-stats-duck's `notes/plans/` is kept identical for the
  integration plan.

## Left to the builder

- Internal class layout, token and state representations, the warning
  code list, JSON key order, the CLI flags, CMake details, the CI matrix.
- Whether the JSON reader for `TableSpec` is hand-written or a vendored
  single-header library under a permissive licence and under two thousand
  lines; hand-written is the default.
- How the mutation test seeds and reports.
- The exact `compat` shims the copy function needs on DuckDB 2.0.0.
- How `rtf_text.py` is kept independent (a different session writes it
  from this document, not from the C++).

## Questions for Massimo

Raised by the builder check on 2026-11-10; the answers replace this list.

1. Answered 2026-11-16: the repository is
   https://github.com/caerbannogwhite/rtf-atto, public, and the npm
   package is `rtf-atto`, unscoped. The check's constraint holds: every
   extension-ci-tools job clones submodules recursively with the default
   token, the community-extensions build clones with no access at all,
   and npm provenance needs a public source repository, so the repository
   stays public.
2. Answered 2026-11-16: Massimo saves Word files into his samples
   directory following the procedure in validation item 8.
