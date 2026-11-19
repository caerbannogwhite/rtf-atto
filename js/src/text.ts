// The plain-text projection, a port of src/text.cpp. One pass over the
// model, appending to one list of pieces.
import type { Block, Cell, Inline, Table } from './model.js';

function appendInlines(items: Inline[], out: string[]): void {
  for (const item of items) {
    switch (item.kind) {
      case 'text':
        out.push(item.text);
        break;
      case 'tab':
        out.push('\t');
        break;
      case 'lineBreak':
      case 'columnBreak':
        out.push('\n');
        break;
      case 'pageBreak':
        out.push('\f');
        break;
      case 'field':
        appendInlines(item.result, out);
        break;
      case 'footnote':
        appendBlocks(item.blocks, out, false);
        break;
      case 'shape':
        appendBlocks(item.blocks, out, false);
        break;
      case 'object':
        appendBlocks(item.result, out, false);
        break;
      default:
        // A bookmark or a picture adds no text.
        break;
    }
  }
}

function appendTable(table: Table, out: string[]): void {
  for (const row of table.rows) {
    for (const cell of row.cells) {
      appendBlocks(cell.blocks, out, true);
      out.push('\t');
    }
    out.push('\n');
  }
}

// When dropLastNewline is set, a paragraph in the last position adds no "\n".
function appendBlocks(blocks: Block[], out: string[], dropLastNewline: boolean): void {
  for (let i = 0; i < blocks.length; i++) {
    const last = i + 1 === blocks.length;
    const block = blocks[i]!;
    if (block.kind === 'paragraph') {
      appendInlines(block.listText ?? [], out);
      appendInlines(block.runs, out);
      if (!(last && dropLastNewline)) out.push('\n');
    } else if (block.kind === 'table') {
      appendTable(block, out);
    }
  }
}

export function inlineText(inlines: Inline[]): string {
  const out: string[] = [];
  appendInlines(inlines, out);
  return out.join('');
}

export function blocksText(blocks: Block[], dropLastNewline: boolean): string {
  const out: string[] = [];
  appendBlocks(blocks, out, dropLastNewline);
  return out.join('');
}

export function cellText(cell: Cell): string {
  return blocksText(cell.blocks, true);
}
