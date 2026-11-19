// Block content: paragraphs and tables. Each block ends with a newline.
import type { Block, Inline, ParagraphProps } from '../model.js';
import { borderCss, colorOf, fmt, isObject, list, lookup } from './css.js';
import { escapeHtml, styleAttr } from './html.js';
import { listTextInlines, newTabState, renderInlines } from './inlines.js';
import type { RenderContext } from './options.js';
import { renderTable } from './tables.js';

const ALIGNS: Readonly<Record<string, string>> = Object.freeze({
  center: 'center',
  right: 'right',
  justify: 'justify',
});

function paragraphStyle(ctx: RenderContext, props: ParagraphProps): string[] {
  const out: string[] = [];
  // Left and any unknown value give no declaration.
  const align = lookup(ALIGNS, props.align);
  if (align !== undefined) out.push(`text-align: ${align}`);
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
  if (isObject(borders)) {
    if (isObject(borders.top)) out.push(`border-top: ${borderCss(ctx, borders.top)}`);
    if (isObject(borders.right)) out.push(`border-right: ${borderCss(ctx, borders.right)}`);
    if (isObject(borders.bottom)) out.push(`border-bottom: ${borderCss(ctx, borders.bottom)}`);
    if (isObject(borders.left)) out.push(`border-left: ${borderCss(ctx, borders.left)}`);
  }
  const background = colorOf(ctx, props.shading?.background);
  if (background !== undefined) out.push(`background-color: ${background}`);
  return out;
}

// One paragraph from its parts. pages.ts calls this for each side of a
// paragraph that a page break splits; the list text goes on the first side.
// The paragraph's shapes and objects follow it as blocks, in order, since
// a block inside a <p> is not valid HTML.
export function renderParagraph(
  ctx: RenderContext,
  props: ParagraphProps,
  listText: Inline[] | undefined,
  runs: Inline[],
): string {
  const outer = ctx.state.after;
  const after: string[] = [];
  ctx.state.after = after;
  const tabs = newTabState(props.tabs);
  let content = '';
  if (listText !== undefined) content += renderInlines(ctx, listTextInlines(listText), tabs);
  content += renderInlines(ctx, runs, tabs);
  ctx.state.after = outer;
  if (content === '') content = '<br>';
  return `<p class="${escapeHtml(ctx.cls('p'))}"${styleAttr(paragraphStyle(ctx, props))}>${content}</p>\n`
    + after.join('');
}

// A whole block. Page breaks render nothing here: pages.ts splits the
// body paragraphs at their page breaks before this call, and a page break
// in a header, a footer or a table cell is ignored.
export function renderBlock(ctx: RenderContext, block: Block): string {
  if (block.kind === 'table') return renderTable(ctx, block);
  return renderParagraph(ctx, block.props, block.listText, block.runs);
}

export function renderBlocks(ctx: RenderContext, blocks: Block[]): string {
  let out = '';
  for (const block of list(blocks)) out += renderBlock(ctx, block);
  return out;
}
