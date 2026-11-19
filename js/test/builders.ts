// Small builders for documents in unit tests. Each one fills the required
// fields with fixed values, so a test states only what it checks.
import type {
  Block,
  Cell,
  CharProps,
  Inline,
  PageSetup,
  Paragraph,
  ParagraphProps,
  Row,
  RowProps,
  RtfDocument,
  Section,
  Table,
  TextRun,
} from '../src/model.js';

// US Letter with one-inch margins, in twips.
export const LETTER: PageSetup = {
  width: 12240,
  height: 15840,
  landscape: false,
  marginTop: 1440,
  marginBottom: 1440,
  marginLeft: 1440,
  marginRight: 1440,
  headerDistance: 720,
  footerDistance: 720,
};

function section(part: Partial<Section>): Section {
  return { page: LETTER, break: 'page', headers: {}, footers: {}, body: [], ...part };
}

function isBlockList(body: Block[] | Partial<Section>[]): body is Block[] {
  return body.length === 0 || 'kind' in body[0]!;
}

// A Block[] becomes the body of one section. A list of partial sections
// becomes that many sections, each with the defaults filled in.
export function doc(body: Block[] | Partial<Section>[], extra?: Partial<RtfDocument>): RtfDocument {
  const sections = isBlockList(body) ? [section({ body })] : body.map(section);
  return {
    version: 1,
    info: {},
    codePage: 1252,
    defaultFont: 0,
    fonts: {
      0: { name: 'Times New Roman', family: 'roman', charset: 0, pitch: 'variable' },
      1: { name: 'Courier New', family: 'modern', charset: 0, pitch: 'fixed' },
    },
    colors: [null, { r: 255, g: 0, b: 0 }, { r: 0, g: 0, b: 255 }],
    styles: [],
    page: LETTER,
    sections,
    warnings: [],
    ...extra,
  };
}

export function run(text: string, props?: CharProps): TextRun {
  return { kind: 'text', text, props: props ?? {} };
}

// A string in runs becomes run(text).
export function para(runs: Array<Inline | string>, props?: ParagraphProps): Paragraph {
  return {
    kind: 'paragraph',
    props: props ?? {},
    runs: runs.map((r) => (typeof r === 'string' ? run(r) : r)),
  };
}

// A string in blocks becomes para([text]). row() fills in the width.
export function cell(right: number, blocks: Array<Block | string>, extra?: Partial<Cell>): Cell {
  return {
    right,
    width: 0,
    blocks: blocks.map((b) => (typeof b === 'string' ? para([b]) : b)),
    ...extra,
  };
}

// Sets each cell's width from the previous cell's right, or from the row's
// left for the first cell.
export function row(cells: Cell[], props?: RowProps): Row {
  const rowProps = props ?? {};
  let left = rowProps.left ?? 0;
  const out: Cell[] = [];
  for (const c of cells) {
    out.push({ ...c, width: c.right - left });
    left = c.right;
  }
  return { props: rowProps, cells: out };
}

export function table(rows: Row[]): Table {
  return { kind: 'table', rows };
}
