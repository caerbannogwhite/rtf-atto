// Inline content: text runs, tabs, breaks, bookmarks and field results.
import type { CharProps, Inline, TabStop, TextRun } from '../model.js';
import { colorOf, fontFamily, lookup, num } from './css.js';
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
      return renderInlines(ctx, inline.result, tabs);
    case 'pageBreak':
    case 'footnote':
    case 'picture':
    case 'shape':
    case 'object':
      return '';
  }
}

// The list text with the Symbol-font bullet U+F0B7 shown as U+2022.
export function listTextInlines(listText: Inline[]): Inline[] {
  return listText.map((inline) =>
    inline.kind === 'text' ? { ...inline, text: inline.text.replace(/\uf0b7/g, '\u2022') } : inline,
  );
}
