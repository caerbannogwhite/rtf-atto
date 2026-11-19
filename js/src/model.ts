// The document model: the shape of the JSON that the reader writes.
// The C++ structs in include/rtf/model.hpp and include/rtf/tables.hpp hold
// the same members under the same names. This file holds types only.
//
// Conventions:
// - Lengths are in twips (1/20 point). Font sizes are in half-points.
// - Colours are indexes into RtfDocument.colors. Fonts are RTF font numbers,
//   the keys of RtfDocument.fonts. An absent colour or font property means
//   the automatic colour or the default font.
// - A property at its default value is omitted from the JSON. Its type is
//   optional and its comment names the default.

export interface RtfDocument {
  version: 1;
  generator?: string;                 // {\*\generator ...}
  info: Record<string, string>;       // title, author, operator, company, ...;
                                      // times as ISO 8601 strings
  codePage: number;                   // \ansicpg, else 1252 (\mac 10000, \pc 437, \pca 850)
  defaultFont: number;                // \deff
  defaultLanguage?: number;           // \deflang
  fonts: Record<number, Font>;        // key = RTF font number, written as a string in the JSON;
                                      // Word numbers its theme fonts up to 31586, so no array
  colors: (Color | null)[];           // index = RTF colour number; null = auto
  styles: Style[];                    // names only; not applied
  page: PageSetup;                    // document-level \paperw \paperh \margl ... \landscape \facingp
  sections: Section[];                // at least one
  warnings: Warning[];
}

export interface Font {
  name: string;
  family: 'nil' | 'roman' | 'swiss' | 'modern' | 'script' | 'decor' | 'tech' | 'bidi';
  charset: number;
  pitch: 'default' | 'fixed' | 'variable';
  codePage?: number;                  // \cpg
}

export interface Color {
  r: number;
  g: number;
  b: number;
}

export interface Style {
  index: number;
  type: 'paragraph' | 'character' | 'section' | 'table';
  name: string;
  basedOn?: number;
  next?: number;
}

export interface PageSetup {
  width: number;
  height: number;
  landscape: boolean;                 // always written
  marginTop: number;
  marginBottom: number;
  marginLeft: number;
  marginRight: number;
  headerDistance: number;
  footerDistance: number;
  facingPages?: boolean;              // \facingp; written only when true
}

export interface Warning {
  code: string;                       // short snake_case code, such as unknown_destination
  message: string;
  offset: number;                     // byte offset in the input
}

// The spec writes this type inline in Section.headers and Section.footers.
export interface HeaderSet {
  default?: Block[];
  first?: Block[];
  left?: Block[];
  right?: Block[];
}

export interface Section {
  page: PageSetup;                    // effective: document values overridden by \pgwsxn ... \lndscpsxn
  break: 'page' | 'none' | 'column' | 'even' | 'odd';   // \sbkpage (default) \sbknone \sbkcol \sbkeven \sbkodd
  columns?: number;                   // \cols, default 1
  titlePage?: boolean;                // \titlepg
  pageNumberStart?: number;           // \pgnstarts with \pgnrestart
  headers: HeaderSet;
  footers: HeaderSet;
  body: Block[];
}

export type Block = Paragraph | Table;

export interface Paragraph {
  kind: 'paragraph';
  props: ParagraphProps;
  listText?: Inline[];                // the already formatted number or bullet (\pntext, \listtext);
                                      // written only when not empty
  runs: Inline[];
}

export interface ParagraphProps {
  align?: 'left' | 'center' | 'right' | 'justify';   // default left
  indentLeft?: number;                // \li
  indentRight?: number;               // \ri
  indentFirst?: number;               // \fi
  spaceBefore?: number;               // \sb
  spaceAfter?: number;                // \sa
  lineSpacing?: number;               // \sl
  lineSpacingMultiple?: boolean;      // \slmult; written only when true
  keepTogether?: boolean;             // \keep; written only when true
  keepWithNext?: boolean;             // \keepn; written only when true
  pageBreakBefore?: boolean;          // \pagebb; written only when true
  widowControl?: boolean;             // \widctlpar or \nowidctlpar; can be false
  style?: number;                     // \s
  list?: { id: number; level: number };   // \ls \ilvl
  tabs?: TabStop[];                   // written only when not empty
  borders?: Borders;
  shading?: Shading;
  rtl?: boolean;                      // written only when true
}

export interface TabStop {
  position: number;
  align: 'left' | 'center' | 'right' | 'decimal' | 'bar';
  leader: 'none' | 'dots' | 'hyphens' | 'underline' | 'thick' | 'equal';
}

export type Inline =
  | TextRun
  | LineBreak
  | Tab
  | PageBreak
  | ColumnBreak
  | Field
  | Bookmark
  | Footnote
  | Picture
  | Shape
  | EmbeddedObject;

export interface TextRun {
  kind: 'text';
  text: string;
  props: CharProps;
}

export interface CharProps {
  font?: number;                      // \f
  size?: number;                      // \fs, half-points, default 24
  bold?: boolean;
  italic?: boolean;
  underline?: 'none' | 'single' | 'double' | 'word' | 'dotted' | 'dash' | 'wave' | 'thick';
  strike?: boolean;
  doubleStrike?: boolean;
  script?: 'normal' | 'super' | 'sub';   // \super \sub \nosupersub
  offset?: number;                    // \up \dn, half-points, signed
  color?: number;                     // absent = automatic
  background?: number;                // absent = automatic
  highlight?: number;                 // absent = automatic
  caps?: boolean;                     // \caps
  smallCaps?: boolean;                // \scaps
  hidden?: boolean;                   // \v
  outline?: boolean;
  shadow?: boolean;
  language?: number;                  // \lang
  deleted?: boolean;                  // \deleted
  inserted?: boolean;                 // \revised
}

export interface LineBreak { kind: 'lineBreak'; }       // \line
export interface Tab { kind: 'tab'; }                   // \tab or a raw tab
export interface PageBreak { kind: 'pageBreak'; }       // \page
export interface ColumnBreak { kind: 'columnBreak'; }   // \column

export interface Field {
  kind: 'field';
  instruction: string;                // {\*\fldinst}
  result: Inline[];                   // {\fldrslt}
  dirty?: boolean;
  locked?: boolean;
}

export interface Bookmark {
  kind: 'bookmark';
  name: string;
  position: 'start' | 'end';          // \bkmkstart or \bkmkend
}

export interface Footnote {
  kind: 'footnote';
  endnote?: boolean;
  blocks: Block[];
}

export interface Picture {
  kind: 'picture';
  format: 'png' | 'jpeg' | 'emf' | 'wmf' | 'dib' | 'bmp' | 'unknown';
  data: string;                       // base64
  width?: number;                     // \picw, pixels or source units
  height?: number;                    // \pich, pixels or source units
  goalWidth?: number;                 // \picwgoal, twips
  goalHeight?: number;                // \pichgoal, twips
  scaleX?: number;
  scaleY?: number;
}

// \shptxt text and the pib picture; the geometry is dropped.
export interface Shape {
  kind: 'shape';
  blocks: Block[];
  picture?: Picture;
}

// \object: the \result branch.
export interface EmbeddedObject {
  kind: 'object';
  className?: string;
  result: Block[];
}

export interface Table {
  kind: 'table';
  rows: Row[];
}

export interface Row {
  props: RowProps;
  cells: Cell[];
}

export interface RowProps {
  header?: boolean;                   // \trhdr
  keep?: boolean;                     // \trkeep
  align?: 'left' | 'center' | 'right';   // \trql \trqc \trqr
  left?: number;                      // \trleft
  height?: number;                    // \trrh (negative = exact)
  heightRule?: 'auto' | 'atLeast' | 'exact';
  gapHalf?: number;                   // \trgaph
  padding?: Padding;                  // \trpadd*, row defaults for the cells
  borders?: Borders;                  // \trbrdr*
  rtl?: boolean;
}

export interface Cell {
  right: number;                      // \cellx, twips, in the same coordinate as the row's left
  width: number;                      // derived: right minus the previous cell's right (or left)
  preferredWidth?: { type: 'auto' | 'percent' | 'twips'; value: number };   // \clwWidth \clftsWidth
  borders?: Borders;                  // \clbrdr*
  shading?: Shading;                  // \clcbpat \clcfpat \clshdng
  verticalAlign?: 'top' | 'center' | 'bottom';   // \clvertalt (default) \clvertalc \clvertalb
  textDirection?: 'lrtb' | 'tbrl' | 'btlr' | 'lrtbv' | 'tbrlv';
  padding?: Padding;                  // \clpad*, explicit, else the row's
  mergeHorizontal?: 'first' | 'continued';   // \clmgf \clmrg
  mergeVertical?: 'first' | 'continued';     // \clvmgf \clvmrg
  noWrap?: boolean;
  blocks: Block[];                    // paragraphs and nested tables
}

export interface Borders {
  top?: Border;
  left?: Border;
  bottom?: Border;
  right?: Border;
  horizontal?: Border;
  vertical?: Border;
}

export interface Border {
  style: 'single' | 'double' | 'dotted' | 'dashed' | 'thick' | 'hairline' | 'triple' | 'none' | 'other';
  width: number;
  color?: number;                     // absent = automatic
  space?: number;
}

export interface Shading {
  background?: number;                // absent = automatic
  foreground?: number;                // absent = automatic
  percent?: number;
  pattern?: string;
}

export interface Padding {
  top?: number;
  left?: number;
  bottom?: number;
  right?: number;
}

// The logical tables: the tables a reader of the printed document sees as
// one table, stitched across pages. Every field is always written.

export interface LogicalCell {
  text: string;
  column: number;                     // index in LogicalTable.edges of the cell's left edge
  span: number;                       // number of grid columns the cell covers, at least 1
}

export interface LogicalRow {
  cells: LogicalCell[];
  spacer: boolean;
  indent: number;
}

// Where one piece of a logical table sits in the document.
export interface TablePart {
  section: number;                    // index in RtfDocument.sections
  block: number;                      // index in that section's body
}

export interface LogicalTable {
  id: number;
  firstSection: number;
  lastSection: number;
  parts: TablePart[];
  edges: number[];
  columnNames: string[];
  headerRows: Row[];
  rows: LogicalRow[];
}
