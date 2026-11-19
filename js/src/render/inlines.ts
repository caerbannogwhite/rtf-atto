// Inline content: text runs, tabs, breaks, bookmarks, fields, footnote
// marks and pictures. Shapes and objects are blocks: they render here but
// go after the paragraph, through ctx.state.after.
import type {
  CharProps,
  EmbeddedObject,
  Field,
  Footnote,
  Inline,
  Picture,
  Shape,
  TabStop,
  TextRun,
} from '../model.js';
import { renderBlocks } from './blocks.js';
import { colorOf, fmt, fontFamily, lookup, num } from './css.js';
import { escapeHtml, styleAttr } from './html.js';
import type { RenderContext } from './options.js';

// The width of a tab when the paragraph has no stop left, in twips.
const DEFAULT_TAB = 720;

// The tab state of one paragraph. Each tab moves to the next stop.
export interface TabState {
  stops: TabStop[];
  count: number;        // tabs rendered so far
  previous: number;     // the position of the last stop used, in twips
}

export function newTabState(stops: TabStop[] | undefined): TabState {
  return { stops: stops ?? [], count: 0, previous: 0 };
}

function tabWidth(state: TabState): number {
  state.count += 1;
  const stop = state.stops[state.count - 1];
  if (stop === undefined) return DEFAULT_TAB;
  const position = num(stop.position);
  const width = Math.max(0, position - state.previous);
  state.previous = position;
  return width;
}

const DECORATION_STYLES: Readonly<Record<string, string>> = Object.freeze({
  double: 'double',
  dotted: 'dotted',
  dash: 'dashed',
  wave: 'wavy',
});

function runStyle(ctx: RenderContext, props: CharProps): string[] {
  const out: string[] = [];
  const family = fontFamily(ctx, props.font);
  if (family !== undefined) out.push(`font-family: ${family}`);
  if (props.size !== undefined) out.push(`font-size: ${ctx.px(props.size * 10)}`);
  if (props.bold) out.push('font-weight: bold');
  if (props.italic) out.push('font-style: italic');
  const lines: string[] = [];
  const underline = props.underline !== undefined && props.underline !== 'none';
  if (underline) lines.push('underline');
  if (props.strike || props.doubleStrike) lines.push('line-through');
  if (lines.length > 0) out.push(`text-decoration-line: ${lines.join(' ')}`);
  const decoration = underline ? lookup(DECORATION_STYLES, props.underline) : undefined;
  if (decoration !== undefined) out.push(`text-decoration-style: ${decoration}`);
  const color = colorOf(ctx, props.color);
  if (color !== undefined) out.push(`color: ${color}`);
  const background = colorOf(ctx, props.highlight) ?? colorOf(ctx, props.background);
  if (background !== undefined) out.push(`background-color: ${background}`);
  if (props.smallCaps) out.push('font-variant: small-caps');
  if (props.caps) out.push('text-transform: uppercase');
  if (props.offset !== undefined && props.offset !== 0) {
    out.push(`vertical-align: ${ctx.px(props.offset * 10)}`);
  }
  return out;
}

function renderRun(ctx: RenderContext, run: TextRun): string {
  if (run.props.hidden || run.text === '') return '';
  const text = escapeHtml(run.text);
  const style = runStyle(ctx, run.props);
  const span = style.length === 0 ? text : `<span${styleAttr(style)}>${text}</span>`;
  if (run.props.script === 'super') return `<sup>${span}</sup>`;
  if (run.props.script === 'sub') return `<sub>${span}</sub>`;
  return span;
}

// Renders inlines in order. A pageBreak renders nothing here: the page
// splitting of body paragraphs happens in pages.ts before this call.
export function renderInlines(ctx: RenderContext, inlines: Inline[], tabs: TabState): string {
  let out = '';
  for (const inline of inlines) out += renderInline(ctx, inline, tabs);
  return out;
}

function renderInline(ctx: RenderContext, inline: Inline, tabs: TabState): string {
  switch (inline.kind) {
    case 'text':
      return renderRun(ctx, inline);
    case 'lineBreak':
    case 'columnBreak':
      return '<br>';
    case 'tab':
      return `<span class="${escapeHtml(ctx.cls('tab'))}"${styleAttr([
        'display: inline-block',
        `width: ${ctx.px(tabWidth(tabs))}`,
      ])}></span>`;
    case 'bookmark':
      if (inline.position !== 'start') return '';
      return `<a id="${escapeHtml(ctx.cls('bm-') + encodeURIComponent(inline.name))}"></a>`;
    case 'field':
      return renderField(ctx, inline, tabs);
    case 'footnote':
      return renderFootnoteRef(ctx, inline);
    case 'picture':
      return renderPicture(ctx, inline);
    case 'shape':
      ctx.state.after.push(renderShape(ctx, inline));
      return '';
    case 'object':
      ctx.state.after.push(renderObject(ctx, inline));
      return '';
    case 'pageBreak':
      return '';
  }
}

// The field name: the first word of the instruction, upper-cased. Word
// writes switches after it, as in "PAGE \* MERGEFORMAT".
function fieldName(instruction: unknown): string {
  if (typeof instruction !== 'string') return '';
  return (instruction.trim().split(/\s+/)[0] ?? '').toUpperCase();
}

// A PAGE or NUMPAGES field with no result shows the page number or the
// page count when pageNumbers is 'sections'. Every other field shows its
// result. The instruction is compared, never written.
function renderField(ctx: RenderContext, field: Field, tabs: TabState): string {
  const result = Array.isArray(field.result) ? field.result : [];
  if (result.length === 0 && ctx.options.pageNumbers === 'sections') {
    const name = fieldName(field.instruction);
    if (name === 'PAGE') return escapeHtml(String(num(ctx.state.pageNumber)));
    if (name === 'NUMPAGES') return escapeHtml(String(num(ctx.state.pageCount)));
  }
  return renderInlines(ctx, result, tabs);
}

// The reference mark. The note itself is listed at the end of the page.
function renderFootnoteRef(ctx: RenderContext, note: Footnote): string {
  ctx.state.noteCount += 1;
  const number = ctx.state.noteCount;
  ctx.state.notes.push({ number, blocks: Array.isArray(note.blocks) ? note.blocks : [] });
  return `<sup class="${escapeHtml(ctx.cls('footnote-ref'))}">${escapeHtml(String(number))}</sup>`;
}

// The formats that get a data URL, with their mime types.
const MIME_TYPES: Readonly<Record<string, string>> = Object.freeze({
  png: 'image/png',
  jpeg: 'image/jpeg',
});

// The names shown in the box of a picture that is not drawn.
const FORMAT_NAMES: Readonly<Record<string, string>> = Object.freeze({
  png: 'PNG',
  jpeg: 'JPEG',
  emf: 'EMF',
  wmf: 'WMF',
  dib: 'DIB',
  bmp: 'BMP',
  unknown: 'UNKNOWN',
});

// Decision 11: the longest base64 text that becomes a data URL, about
// 3 MB of bytes.
const MAX_BASE64 = 4_194_304;
const BASE64 = /^[A-Za-z0-9+/]*={0,2}$/;

// The picture's size as width and height declarations, or undefined when
// no size is known. Goal sizes are twips scaled by percent; plain sizes
// are pixels.
function pictureSize(ctx: RenderContext, picture: Picture): string[] | undefined {
  if (picture.goalWidth !== undefined && picture.goalHeight !== undefined) {
    const width = num(picture.goalWidth) * num(picture.scaleX ?? 100) / 100;
    const height = num(picture.goalHeight) * num(picture.scaleY ?? 100) / 100;
    return [`width: ${ctx.px(width)}`, `height: ${ctx.px(height)}`];
  }
  if (picture.width !== undefined && picture.height !== undefined) {
    return [`width: ${fmt(num(picture.width))}px`, `height: ${fmt(num(picture.height))}px`];
  }
  return undefined;
}

// A PNG or JPEG within the rules is an <img> with a data URL. Anything
// else is a box with the format name. pictures: false renders nothing.
function renderPicture(ctx: RenderContext, picture: Picture): string {
  if (!ctx.options.pictures) return '';
  const size = pictureSize(ctx, picture);
  const mime = lookup(MIME_TYPES, picture.format);
  const data = picture.data;
  if (mime !== undefined && typeof data === 'string' && data.length <= MAX_BASE64 && BASE64.test(data)) {
    const src = `data:${mime};base64,${data}`;
    return `<img class="${escapeHtml(ctx.cls('picture'))}" src="${escapeHtml(src)}" alt=""${styleAttr(size ?? [])}>`;
  }
  const name = lookup(FORMAT_NAMES, picture.format) ?? 'UNKNOWN';
  const box = ['display: inline-block', ...(size ?? ['width: 100px', 'height: 100px'])];
  return `<span class="${escapeHtml(ctx.cls('picture-box'))}"${styleAttr(box)}>${escapeHtml(name)}</span>`;
}

// A shape is a block after its paragraph: its text blocks, then its picture.
function renderShape(ctx: RenderContext, shape: Shape): string {
  const blocks = Array.isArray(shape.blocks) ? renderBlocks(ctx, shape.blocks) : '';
  const picture = shape.picture !== undefined && shape.picture !== null ? renderPicture(ctx, shape.picture) : '';
  return `<div class="${escapeHtml(ctx.cls('shape'))}">${blocks}${picture}</div>\n`;
}

// An object is a block after its paragraph with its result blocks.
function renderObject(ctx: RenderContext, object: EmbeddedObject): string {
  const blocks = Array.isArray(object.result) ? renderBlocks(ctx, object.result) : '';
  return `<div class="${escapeHtml(ctx.cls('object'))}">${blocks}</div>\n`;
}

// The list text with the Symbol-font bullet U+F0B7 shown as U+2022.
export function listTextInlines(listText: Inline[]): Inline[] {
  return listText.map((inline) =>
    inline.kind === 'text' ? { ...inline, text: inline.text.replace(/\uf0b7/g, '\u2022') } : inline,
  );
}
