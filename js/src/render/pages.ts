// Pagination and the page frame: which blocks go on which page, the page
// numbers, the header and footer of each page, the footnote list and the
// page markup. paginate only places the blocks. renderPage renders them,
// once the page count is known for NUMPAGES.
import type { Block, HeaderSet, Inline, Paragraph, ParagraphProps } from '../model.js';
import { renderBlock, renderBlocks, renderParagraph } from './blocks.js';
import { fontFamily, list } from './css.js';
import { escapeHtml, styleAttr } from './html.js';
import type { RenderContext } from './options.js';

// One item of a page's body: a whole block, or the part of a paragraph
// between page breaks.
export type PageItem =
  | { kind: 'block'; block: Block }
  | { kind: 'part'; props: ParagraphProps; listText: Inline[] | undefined; runs: Inline[] };

export interface Page {
  number: number;            // decision 3: counts pages, restarts at pageNumberStart
  section: number;           // the section whose page setup, header and footer the page uses
  firstOfSection: boolean;   // the first page of that section
  body: PageItem[];          // what goes in the page's body, in order
}

// Splits the sections into pages. A section with break 'none' continues
// the current page, which keeps the section that started it.
export function paginate(ctx: RenderContext): Page[] {
  const pages: Page[] = [];
  let current: Page | undefined;

  const startPage = (section: number, firstOfSection: boolean, restart?: number): Page => {
    const previous = pages[pages.length - 1];
    // A start that is not a finite number counts as absent.
    const start = typeof restart === 'number' && Number.isFinite(restart) ? restart : undefined;
    const number = start ?? (previous === undefined ? 1 : previous.number + 1);
    const page: Page = { number, section, firstOfSection, body: [] };
    pages.push(page);
    return page;
  };

  list(ctx.doc.sections).forEach((section, index) => {
    if (current === undefined || section.break !== 'none') {
      current = startPage(index, true, section.pageNumberStart);
    }
    // A page break inside the section starts a page of this section.
    const breakPage = (): void => {
      current = startPage(index, false);
    };
    for (const block of list(section.body)) {
      if (block.kind === 'table') {
        current.body.push({ kind: 'block', block });
        continue;
      }
      if (block.props.pageBreakBefore && current.body.length > 0) breakPage();
      addParagraph(block, () => current!, breakPage);
    }
  });
  return pages;
}

// Adds a body paragraph, ending the page at each top-level pageBreak. Each
// side keeps the paragraph's props; an empty side gives no paragraph. A
// paragraph with no page break is always emitted, even when empty.
function addParagraph(
  paragraph: Paragraph,
  page: () => Page,
  breakPage: () => void,
): void {
  const sides: Inline[][] = [[]];
  for (const inline of list(paragraph.runs)) {
    if (inline.kind === 'pageBreak') sides.push([]);
    else sides[sides.length - 1]!.push(inline);
  }
  if (sides.length === 1) {
    page().body.push({ kind: 'block', block: paragraph });
    return;
  }
  sides.forEach((side, index) => {
    if (index > 0) breakPage();
    const listText = index === 0 ? paragraph.listText : undefined;
    if (side.length === 0 && list(listText).length === 0) return;
    page().body.push({ kind: 'part', props: paragraph.props, listText, runs: side });
  });
}

// The header or footer of a page, as Word chooses it. On the first page
// of a section with a title page: the first set, else nothing. With facing
// pages: the set of the page's parity (left on an even number, right on an
// odd one), else the default set, else nothing. Otherwise: the default
// set, else the right set, else the left set, else nothing.
function chooseArea(ctx: RenderContext, set: HeaderSet, page: Page): Block[] {
  const area: HeaderSet = typeof set === 'object' && set !== null ? set : {};
  if (ctx.doc.sections[page.section]!.titlePage && page.firstOfSection) return area.first ?? [];
  if (ctx.doc.page.facingPages) {
    return (page.number % 2 === 0 ? area.left : area.right) ?? area.default ?? [];
  }
  return area.default ?? area.right ?? area.left ?? [];
}

function renderArea(ctx: RenderContext, name: string, style: string[], blocks: Block[]): string {
  return `<div class="${escapeHtml(ctx.cls(name))}"${styleAttr(style)}>\n${renderBlocks(ctx, blocks)}</div>\n`;
}

function renderItem(ctx: RenderContext, item: PageItem): string {
  if (item.kind === 'block') return renderBlock(ctx, item.block);
  return renderParagraph(ctx, item.props, item.listText, item.runs);
}

// The notes referenced on the page, in the order of their numbers. A note
// referenced inside another note joins the list while the list renders.
function renderNotes(ctx: RenderContext): string {
  const notes = ctx.state.notes;
  if (notes.length === 0) return '';
  let out = `<div class="${escapeHtml(ctx.cls('footnotes'))}">\n`;
  for (let i = 0; i < notes.length; i += 1) {
    const note = notes[i]!;
    out += `<div class="${escapeHtml(ctx.cls('footnote'))}"><sup>${escapeHtml(String(note.number))}</sup>`
      + `${renderBlocks(ctx, note.blocks)}</div>\n`;
  }
  return out + '</div>\n';
}

// Renders one page. ctx.state.pageCount must hold the number of pages.
// The header, the body and the footer render in that order, so the
// footnote numbers follow that order. The notes of all three are listed
// at the end of the body.
export function renderPage(ctx: RenderContext, page: Page): string {
  ctx.state.pageNumber = page.number;
  ctx.state.notes = [];
  const section = ctx.doc.sections[page.section]!;
  const setup = section.page;
  const classes = setup.landscape ? `${ctx.cls('page')} ${ctx.cls('landscape')}` : ctx.cls('page');
  const style = [
    `width: ${ctx.px(setup.width)}`,
    `min-height: ${ctx.px(setup.height)}`,
    `padding-left: ${ctx.px(setup.marginLeft)}`,
    `padding-right: ${ctx.px(setup.marginRight)}`,
  ];
  const family = fontFamily(ctx, ctx.doc.defaultFont);
  if (family !== undefined) style.push(`font-family: ${family}`);
  style.push(`font-size: ${ctx.px(24 * 10)}`);

  let out = `<section class="${escapeHtml(classes)}" data-page="${escapeHtml(String(page.number))}" data-section="${escapeHtml(String(page.section))}"`
    + `${styleAttr(style)}>\n`;
  out += renderArea(ctx, 'header', [
    `min-height: ${ctx.px(setup.marginTop)}`,
    `padding-top: ${ctx.px(setup.headerDistance)}`,
  ], chooseArea(ctx, section.headers, page));
  let body = '';
  for (const item of page.body) body += renderItem(ctx, item);
  const footer = renderArea(ctx, 'footer', [
    `min-height: ${ctx.px(setup.marginBottom)}`,
    `padding-bottom: ${ctx.px(setup.footerDistance)}`,
  ], chooseArea(ctx, section.footers, page));
  out += `<div class="${escapeHtml(ctx.cls('body'))}">\n${body}${renderNotes(ctx)}</div>\n`;
  return out + footer + '</section>\n';
}
