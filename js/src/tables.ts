// Logical tables, a port of src/tables.cpp: stitches the table blocks of the
// body into tables as printed, builds the column grid and names the columns
// from the header rows. The C++ output, in the .tables.json goldens, is the
// reference.
import type { Cell, LogicalCell, LogicalRow, LogicalTable, Paragraph, Row, RtfDocument, Table } from './model.js';
import { cellText } from './text.js';

function isAsciiSpace(c: string): boolean {
  return c === ' ' || c === '\t' || c === '\n' || c === '\r' || c === '\f' || c === '\v';
}

// Strips ASCII whitespace only. String.prototype.trim strips more, such as
// U+00A0, so it is not used.
function trim(s: string): string {
  let begin = 0;
  let end = s.length;
  while (begin < end && isAsciiSpace(s[begin]!)) ++begin;
  while (end > begin && isAsciiSpace(s[end - 1]!)) --end;
  return s.slice(begin, end);
}

// True when the text is empty after trimming ASCII whitespace and U+00A0.
function isBlank(s: string): boolean {
  for (const c of s) {
    if (!isAsciiSpace(c) && c !== ' ') return false;
  }
  return true;
}

// A paragraph that may sit between two parts of one logical table.
function isGapParagraph(p: Paragraph): boolean {
  if (p.listText !== undefined && p.listText.length > 0) return false;
  for (const item of p.runs) {
    if (item.kind !== 'pageBreak') return false;
  }
  return true;
}

function rowLeft(row: Row): number {
  return row.props.left ?? 0;
}

function headerRowsOf(table: Table): Row[] {
  return table.rows.filter((row) => row.props.header === true);
}

// Sorts numbers in ascending order and removes duplicates, like std::sort
// followed by std::unique.
function sortedUnique(values: number[]): number[] {
  values.sort((a, b) => a - b);
  const out: number[] = [];
  for (const v of values) {
    if (out.length === 0 || out[out.length - 1] !== v) out.push(v);
  }
  return out;
}

// The first index whose value is not less than `value`, like std::lower_bound.
function lowerBound(sorted: number[], value: number): number {
  let lo = 0;
  let hi = sorted.length;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (sorted[mid]! < value) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

// The first index whose value is greater than `value`, like std::upper_bound.
function upperBound(sorted: number[], value: number): number {
  let lo = 0;
  let hi = sorted.length;
  while (lo < hi) {
    const mid = (lo + hi) >>> 1;
    if (sorted[mid]! <= value) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

// The sorted rights of every cell of every data row.
function dataRights(table: Table): number[] {
  const out: number[] = [];
  for (const row of table.rows) {
    if (row.props.header) continue;
    for (const cell of row.cells) out.push(cell.right);
  }
  return sortedUnique(out);
}

function sameHeaderRows(a: Row[], b: Row[]): boolean {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    const ra = a[i]!;
    const rb = b[i]!;
    if (ra.cells.length !== rb.cells.length) return false;
    for (let c = 0; c < ra.cells.length; c++) {
      if (ra.cells[c]!.right !== rb.cells[c]!.right) return false;
      if (cellText(ra.cells[c]!) !== cellText(rb.cells[c]!)) return false;
    }
  }
  return true;
}

// Whether `next` continues the logical table that `prev` belongs to.
function continues(prev: Table, next: Table): boolean {
  const nextHeaders = headerRowsOf(next);
  if (nextHeaders.length > 0) {
    const prevHeaders = headerRowsOf(prev);
    return prevHeaders.length > 0 && sameHeaderRows(prevHeaders, nextHeaders);
  }
  if (next.rows.length === 0) return false;
  const rights = dataRights(prev);
  for (const cell of next.rows[0]!.cells) {
    const i = lowerBound(rights, cell.right);
    if (i === rights.length || rights[i] !== cell.right) return false;
  }
  return true;
}

interface PartRef {
  table: Table;
  section: number;
  block: number;
}

// Index of `value` in the sorted `edges`; 0 when it is missing.
function edgeIndex(edges: number[], value: number): number {
  const i = lowerBound(edges, value);
  if (i === edges.length || edges[i] !== value) return 0;
  return i;
}

// The grid columns [first, last) that a span from `left` to `right` overlaps.
function coveredColumns(edges: number[], left: number, right: number): [number, number] {
  const columns = edges.length === 0 ? 0 : edges.length - 1;
  let first = upperBound(edges, left) - 1;
  if (first < 0) first = 0;
  let last = lowerBound(edges, right);
  if (last > columns) last = columns;
  return [first, last];
}

function headerName(cell: Cell): string {
  return trim(cellText(cell).replaceAll('\n', ' '));
}

function columnNames(edges: number[], headerRows: Row[]): string[] {
  const columns = edges.length === 0 ? 0 : edges.length - 1;
  const names: string[] = new Array<string>(columns).fill('');

  // The last header row sets the names. Each row above it puts its text in
  // front of the names of the columns it covers.
  for (let r = headerRows.length - 1; r >= 0; r--) {
    const row = headerRows[r]!;
    const isLast = r + 1 === headerRows.length;
    let left = rowLeft(row);
    for (const cell of row.cells) {
      const text = headerName(cell);
      const [first, last] = coveredColumns(edges, left, cell.right);
      left = cell.right;
      for (let c = first; c < last; c++) {
        if (isLast) {
          names[c] = text;
        } else if (text !== '') {
          names[c] = names[c] === '' ? text : text + ' ' + names[c];
        }
      }
    }
  }

  const used = new Set<string>();
  for (let c = 0; c < columns; c++) {
    let name = names[c]!;
    if (name === '') name = 'column_' + String(c + 1);
    let candidate = name;
    for (let n = 2; used.has(candidate); ++n) candidate = name + '_' + String(n);
    used.add(candidate);
    names[c] = candidate;
  }
  return names;
}

function logicalRow(row: Row, edges: number[]): LogicalRow {
  const out: LogicalRow = { cells: [], spacer: true, indent: 0 };
  let left = rowLeft(row);
  for (const cell of row.cells) {
    const text = cellText(cell);
    const column = edgeIndex(edges, left);
    const lc: LogicalCell = { text, column, span: Math.max(1, edgeIndex(edges, cell.right) - column) };
    if (!isBlank(lc.text)) out.spacer = false;
    out.cells.push(lc);
    left = cell.right;
  }
  if (row.cells.length > 0) {
    for (const block of row.cells[0]!.blocks) {
      if (block.kind === 'paragraph') {
        out.indent = block.props.indentLeft ?? 0;
        break;
      }
    }
  }
  return out;
}

function buildTable(id: number, parts: PartRef[]): LogicalTable {
  const table: LogicalTable = {
    id,
    firstSection: parts[0]!.section,
    lastSection: parts[parts.length - 1]!.section,
    parts: [],
    edges: [],
    columnNames: [],
    headerRows: [],
    rows: [],
  };

  for (const part of parts) table.parts.push({ section: part.section, block: part.block });

  for (const row of parts[0]!.table.rows) {
    if (row.props.header) table.headerRows.push(row);
  }

  const edges: number[] = [];
  for (const part of parts) {
    for (const row of part.table.rows) {
      if (row.props.header) continue;
      edges.push(rowLeft(row));
      for (const cell of row.cells) edges.push(cell.right);
    }
  }
  table.edges = sortedUnique(edges);

  for (const part of parts) {
    for (const row of part.table.rows) {
      if (!row.props.header) table.rows.push(logicalRow(row, table.edges));
    }
  }

  table.columnNames = columnNames(table.edges, table.headerRows);
  return table;
}

export function logicalTables(document: RtfDocument): LogicalTable[] {
  const result: LogicalTable[] = [];
  let group: PartRef[] = [];
  let gapClean = false; // only gap paragraphs since the last part

  const flush = (): void => {
    if (group.length === 0) return;
    result.push(buildTable(result.length + 1, group));
    group = [];
  };

  for (let s = 0; s < document.sections.length; s++) {
    const body = document.sections[s]!.body;
    for (let b = 0; b < body.length; b++) {
      const block = body[b]!;
      if (block.kind === 'paragraph') {
        if (!isGapParagraph(block)) gapClean = false;
        continue;
      }
      if (block.kind !== 'table') continue;
      if (group.length === 0 || !gapClean || !continues(group[group.length - 1]!.table, block)) flush();
      group.push({ table: block, section: s, block: b });
      gapClean = true;
    }
  }
  flush();
  return result;
}
