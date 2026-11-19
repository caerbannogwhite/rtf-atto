// Tables: the grid of columns, the spans of merged cells and the cell
// styles. The grid's edges are the union of every row's left and every
// cell's right, so rows with different cell edges share one set of columns.
import type { Border, Borders, Cell, Row, Table } from '../model.js';
import { borderCss, colorOf, lookup, num } from './css.js';
import { renderBlocks, renderParagraph } from './blocks.js';
import { escapeHtml, styleAttr } from './html.js';
import type { RenderContext } from './options.js';

// Top is the CSS default, so it has no entry.
const VERTICAL_ALIGNS: Readonly<Record<string, string>> = Object.freeze({
  center: 'middle',
  bottom: 'bottom',
});

// Left and any unknown value fall back to the row's left as a margin.
const TABLE_ALIGNS: Readonly<Record<string, readonly string[]>> = Object.freeze({
  center: Object.freeze(['margin-left: auto', 'margin-right: auto']),
  right: Object.freeze(['margin-left: auto']),
});

const SIDES = ['top', 'right', 'bottom', 'left'] as const;

// One cell that the table writes, with the grid columns it covers.
interface Slot {
  cell: Cell;
  left: number;          // the cell's own left edge in twips
  right: number;         // the cell's own right edge in twips
  span: number;          // grid columns, at least 1
  rowspan: number;       // rows, at least 1
  hidden: boolean;       // a continued cell taken into a rowspan above
}

function rowLeft(row: Row): number {
  return num(row.props.left);
}

function gridEdges(rows: Row[]): number[] {
  const edges = new Set<number>();
  for (const row of rows) {
    edges.add(rowLeft(row));
    for (const cell of row.cells) edges.add(num(cell.right));
  }
  return [...edges].sort((a, b) => a - b);
}

// The cells of one row that the table writes. A horizontally continued
// cell adds its columns to the previous written cell of the row.
function rowSlots(row: Row, columnOf: Map<number, number>): Slot[] {
  const slots: Slot[] = [];
  let left = rowLeft(row);
  for (const cell of row.cells) {
    const right = num(cell.right);
    // A right edge below the left edge gives a span of 1, never less.
    const span = Math.max(1, columnOf.get(right)! - columnOf.get(left)!);
    const previous = slots[slots.length - 1];
    if (cell.mergeHorizontal === 'continued' && previous !== undefined) {
      previous.span += span;
    } else {
      slots.push({ cell, left, right, span, rowspan: 1, hidden: false });
    }
    left = right;
  }
  return slots;
}

// A vertically first cell takes the continued cells with the same edges
// in the rows that follow it, until a row has none. The scan stays inside
// one row group, because a rowspan cannot cross from thead into tbody.
function mergeVertically(group: Slot[][]): void {
  for (let i = 0; i < group.length; i += 1) {
    for (const first of group[i]!) {
      if (first.hidden || first.cell.mergeVertical !== 'first') continue;
      for (let j = i + 1; j < group.length; j += 1) {
        const below = group[j]!.find((slot) => !slot.hidden
          && slot.cell.mergeVertical === 'continued'
          && slot.left === first.left
          && slot.right === first.right);
        if (below === undefined) break;
        below.hidden = true;
        first.rowspan += 1;
      }
    }
  }
}

// Decision 13: the cell's padding, else the row's, else gapHalf (108 twips
// by default) on the left and right and 0 on the top and bottom.
function cellPadding(ctx: RenderContext, cell: Cell, row: Row): string {
  const gap = row.props.gapHalf ?? 108;
  const side = (name: typeof SIDES[number]): string => {
    const value = cell.padding?.[name] ?? row.props.padding?.[name];
    if (value !== undefined) return ctx.px(value);
    return ctx.px(name === 'left' || name === 'right' ? gap : 0);
  };
  return `padding: ${SIDES.map(side).join(' ')}`;
}

function cellStyle(ctx: RenderContext, cell: Cell, row: Row): string[] {
  const out: string[] = [];
  const valign = lookup(VERTICAL_ALIGNS, cell.verticalAlign);
  if (valign !== undefined) out.push(`vertical-align: ${valign}`);
  out.push(cellPadding(ctx, cell, row));
  const background = colorOf(ctx, cell.shading?.background);
  if (background !== undefined) out.push(`background-color: ${background}`);
  const cellBorders: Borders | undefined = cell.borders;
  const rowBorders: Borders | undefined = row.props.borders;
  for (const name of SIDES) {
    const border: Border | undefined = cellBorders?.[name] ?? rowBorders?.[name];
    if (border !== undefined) out.push(`border-${name}: ${borderCss(ctx, border)}`);
  }
  if (cell.noWrap === true) out.push('white-space: nowrap');
  return out;
}

function renderCell(ctx: RenderContext, slot: Slot, row: Row, tag: 'td' | 'th'): string {
  let attrs = '';
  if (slot.span > 1) attrs += ` colspan="${slot.span}"`;
  if (slot.rowspan > 1) attrs += ` rowspan="${slot.rowspan}"`;
  attrs += styleAttr(cellStyle(ctx, slot.cell, row));
  const content = slot.cell.blocks.length === 0
    ? renderParagraph(ctx, {}, undefined, [])
    : renderBlocks(ctx, slot.cell.blocks);
  return `<${tag}${attrs}>${content}</${tag}>`;
}

function renderRow(ctx: RenderContext, row: Row, slots: Slot[], tag: 'td' | 'th'): string {
  const height = Math.abs(num(row.props.height));
  let out = height !== 0 ? `<tr${styleAttr([`height: ${ctx.px(height)}`])}>` : '<tr>';
  for (const slot of slots) {
    if (!slot.hidden) out += renderCell(ctx, slot, row, tag);
  }
  return out + '</tr>\n';
}

export function renderTable(ctx: RenderContext, table: Table): string {
  const rows = table.rows;
  const edges = gridEdges(rows);
  const columnOf = new Map(edges.map((edge, index) => [edge, index]));
  const first = edges[0] ?? 0;
  const last = edges[edges.length - 1] ?? 0;

  const style = [`width: ${ctx.px(last - first)}`, 'table-layout: fixed', 'border-collapse: collapse'];
  const align = lookup(TABLE_ALIGNS, rows[0]?.props.align);
  if (align !== undefined) style.push(...align);
  else if (first !== 0) style.push(`margin-left: ${ctx.px(first)}`);

  let out = `<table class="${escapeHtml(ctx.cls('table'))}"${styleAttr(style)}>\n<colgroup>`;
  for (let i = 0; i + 1 < edges.length; i += 1) {
    out += `<col${styleAttr([`width: ${ctx.px(edges[i + 1]! - edges[i]!)}`])}>`;
  }
  out += '</colgroup>\n';

  let headerCount = 0;
  while (headerCount < rows.length && rows[headerCount]!.props.header === true) headerCount += 1;
  const slots = rows.map((row) => rowSlots(row, columnOf));
  const headSlots = slots.slice(0, headerCount);
  const bodySlots = slots.slice(headerCount);
  mergeVertically(headSlots);
  mergeVertically(bodySlots);

  if (headerCount > 0) {
    out += '<thead>\n';
    for (let i = 0; i < headerCount; i += 1) out += renderRow(ctx, rows[i]!, headSlots[i]!, 'th');
    out += '</thead>\n';
  }
  out += '<tbody>\n';
  for (let i = headerCount; i < rows.length; i += 1) {
    out += renderRow(ctx, rows[i]!, bodySlots[i - headerCount]!, 'td');
  }
  return out + '</tbody>\n</table>\n';
}
