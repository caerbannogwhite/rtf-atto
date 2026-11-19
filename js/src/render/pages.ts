// Pagination and the page frame: which blocks go on which page, the page
// numbers, the header and footer of each page, and the page markup.
import type { Block, HeaderSet, Inline, Paragraph } from '../model.js';
import { renderBlock, renderParagraph } from './blocks.js';
import { fontFamily } from './css.js';
import { escapeHtml, styleAttr } from './html.js';
import type { RenderContext } from './options.js';

export interface Page {
  number: number;            // decision 3: counts pages, restarts at pageNumberStart
  section: number;           // the section whose page setup, header and footer the page uses
  firstOfSection: boolean;   // the first page of that section
  body: string[];            // the rendered body blocks, each ending with a newline
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

  ctx.doc.sections.forEach((section, index) => {
    if (current === undefined || section.break !== 'none') {
      current = startPage(index, true, section.pageNumberStart);
    }
    // A page break inside the section starts a page of this section.
    const breakPage = (): void => {
      current = startPage(index, false);
    };
    for (const block of section.body) {
      if (block.kind === 'table') {
        current.body.push(renderBlock(ctx, block));
        continue;
      }
      if (block.props.pageBreakBefore && current.body.length > 0) breakPage();
      addParagraph(ctx, block, () => current!, breakPage);
    }
  });
  return pages;
}

// Adds a body paragraph, ending the page at each top-level pageBreak. Each
// side keeps the paragraph's props; an empty side gives no paragraph. A
// paragraph with no page break is always emitted, even when empty.
function addParagraph(
  ctx: RenderContext,
  paragraph: Paragraph,
  page: () => Page,
  breakPage: () => void,
): void {
  const sides: Inline[][] = [[]];
  for (const inline of paragraph.runs) {
    if (inline.kind === 'pageBreak') sides.push([]);
    else sides[sides.length - 1]!.push(inline);
  }
  if (sides.length === 1) {
    page().body.push(renderParagraph(ctx, paragraph.props, paragraph.listText, paragraph.runs));
    return;
  }
  sides.forEach((side, index) => {
    if (index > 0) breakPage();
    const listText = index === 0 ? paragraph.listText : undefined;
    if (side.length === 0 && (listText === undefined || listText.length === 0)) return;
    page().body.push(renderParagraph(ctx, paragraph.props, listText, side));
  });
}

// The header or footer of a page: the first candidate that is present.
function chooseArea(ctx: RenderContext, set: HeaderSet, page: Page): Block[] | undefined {
  const candidates: Array<Block[] | undefined> = [];
  if (ctx.doc.sections[page.section]!.titlePage && page.firstOfSection) candidates.push(set.first);
  if (ctx.doc.page.facingPages) candidates.push(page.number % 2 === 0 ? set.left : set.right);
  candidates.push(set.default, set.right, set.left);
  return candidates.find((blocks) => blocks !== undefined);
}

function renderArea(ctx: RenderContext, name: string, style: string[], blocks: Block[] | undefined): string {
  let out = `<div class="${escapeHtml(ctx.cls(name))}"${styleAttr(style)}>\n`;
  for (const block of blocks ?? []) out += renderBlock(ctx, block);
  return out + '</div>\n';
}

export function renderPage(ctx: RenderContext, page: Page): string {
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
  out += `<div class="${escapeHtml(ctx.cls('body'))}">\n${page.body.join('')}</div>\n`;
  out += renderArea(ctx, 'footer', [
    `min-height: ${ctx.px(setup.marginBottom)}`,
    `padding-bottom: ${ctx.px(setup.footerDistance)}`,
  ], chooseArea(ctx, section.footers, page));
  return out + '</section>\n';
}
