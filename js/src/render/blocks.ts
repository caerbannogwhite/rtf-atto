// Block content: paragraphs and tables. Each block ends with a newline.
import type { Block, Inline, ParagraphProps, Table } from '../model.js';
import { borderCss, colorOf, fmt } from './css.js';
import { escapeHtml, styleAttr } from './html.js';
import { listTextInlines, newTabState, renderInlines } from './inlines.js';
import type { RenderContext } from './options.js';

function paragraphStyle(ctx: RenderContext, props: ParagraphProps): string[] {
  const out: string[] = [];
  if (props.align !== undefined && props.align !== 'left') out.push(`text-align: ${props.align}`);
  // Zero indents and spacing are the CSS defaults, so they are not written.
  if (props.indentLeft !== undefined && props.indentLeft !== 0) out.push(`margin-left: ${ctx.px(props.indentLeft)}`);
  if (props.indentRight !== undefined && props.indentRight !== 0) out.push(`margin-right: ${ctx.px(props.indentRight)}`);
  if (props.indentFirst !== undefined && props.indentFirst !== 0) out.push(`text-indent: ${ctx.px(props.indentFirst)}`);
  if (props.spaceBefore !== undefined && props.spaceBefore !== 0) out.push(`margin-top: ${ctx.px(props.spaceBefore)}`);
  if (props.spaceAfter !== undefined && props.spaceAfter !== 0) out.push(`margin-bottom: ${ctx.px(props.spaceAfter)}`);
  const spacing = props.lineSpacing;
  if (spacing !== undefined && spacing !== 0) {
    out.push(props.lineSpacingMultiple
      ? `line-height: ${fmt(spacing / 240)}`
      : `line-height: ${ctx.px(Math.abs(spacing))}`);
  }
  if (props.rtl) out.push('direction: rtl');
  const borders = props.borders;
  if (borders !== undefined) {
    if (borders.top !== undefined) out.push(`border-top: ${borderCss(ctx, borders.top)}`);
    if (borders.right !== undefined) out.push(`border-right: ${borderCss(ctx, borders.right)}`);
    if (borders.bottom !== undefined) out.push(`border-bottom: ${borderCss(ctx, borders.bottom)}`);
    if (borders.left !== undefined) out.push(`border-left: ${borderCss(ctx, borders.left)}`);
  }
  const background = colorOf(ctx, props.shading?.background);
  if (background !== undefined) out.push(`background-color: ${background}`);
  return out;
}

// One paragraph from its parts. pages.ts calls this for each side of a
// paragraph that a page break splits; the list text goes on the first side.
export function renderParagraph(
  ctx: RenderContext,
  props: ParagraphProps,
  listText: Inline[] | undefined,
  runs: Inline[],
): string {
  const tabs = newTabState(props.tabs);
  let content = '';
  if (listText !== undefined) content += renderInlines(ctx, listTextInlines(listText), tabs);
  content += renderInlines(ctx, runs, tabs);
  if (content === '') content = '<br>';
  return `<p class="${escapeHtml(ctx.cls('p'))}"${styleAttr(paragraphStyle(ctx, props))}>${content}</p>\n`;
}

// A plain grid of the cells' blocks, with no spans and no styles.
function renderTable(ctx: RenderContext, table: Table): string {
  let out = `<table class="${escapeHtml(ctx.cls('table'))}">\n`;
  for (const row of table.rows) {
    out += '<tr>';
    for (const cell of row.cells) out += `<td>${renderBlocks(ctx, cell.blocks)}</td>`;
    out += '</tr>\n';
  }
  return out + '</table>\n';
}

// A block outside the page flow: in a header, a footer or a table cell.
// Page breaks there render nothing.
export function renderBlock(ctx: RenderContext, block: Block): string {
  if (block.kind === 'table') return renderTable(ctx, block);
  return renderParagraph(ctx, block.props, block.listText, block.runs);
}

export function renderBlocks(ctx: RenderContext, blocks: Block[]): string {
  let out = '';
  for (const block of blocks) out += renderBlock(ctx, block);
  return out;
}
