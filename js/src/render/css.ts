// CSS values built from the model. Every value is made from numbers and
// fixed words. The one exception is a font name, which is cleaned first.
// renderHtml takes a plain object, so the model may hold any JSON value
// where the types say number or a fixed word. num() and lookup() check
// such values on the way in.
import type { Border, Font } from '../model.js';
import type { RenderContext } from './options.js';

// The value when it is a finite number, else 0.
export function num(value: unknown): number {
  return typeof value === 'number' && Number.isFinite(value) ? value : 0;
}

// The entry of a fixed table for a key that the table itself holds. Keys
// that come from the prototype, such as "constructor", give undefined.
export function lookup<T>(table: Readonly<Record<string, T>>, key: unknown): T | undefined {
  return typeof key === 'string' && Object.hasOwn(table, key) ? table[key] : undefined;
}

// A number with at most two decimals and no trailing zeros. A value that
// is not a finite number is written as 0.
export function fmt(n: number): string {
  return String(num(Math.round(num(n) * 100) / 100));
}

// The colour at an index of the colour table as rgb(r, g, b). Undefined
// for the automatic colour, an absent index or one out of range.
export function colorOf(ctx: RenderContext, index: number | undefined): string | undefined {
  if (index === undefined || !Number.isInteger(index) || index < 0) return undefined;
  const color = ctx.doc.colors[index];
  if (color === undefined || color === null || typeof color !== 'object') return undefined;
  return `rgb(${fmt(color.r)}, ${fmt(color.g)}, ${fmt(color.b)})`;
}

// Keeps letters, digits, spaces and hyphens, collapses runs of spaces
// and trims.
export function cleanFontName(name: string): string {
  return String(name).replace(/[^\p{L}\p{N} -]/gu, '').replace(/ +/g, ' ').trim();
}

function genericFamily(font: Font): string {
  switch (font.family) {
    case 'modern': return 'monospace';
    case 'swiss': return 'sans-serif';
    case 'roman': return 'serif';
    default: return 'sans-serif';
  }
}

// The CSS font-family for an RTF font number. The fonts option wins by the
// font's name. Undefined for an unknown font number.
export function fontFamily(ctx: RenderContext, index: number | undefined): string | undefined {
  if (index === undefined) return undefined;
  const fonts = ctx.doc.fonts;
  if (!Object.hasOwn(fonts, index)) return undefined;
  const font = fonts[index];
  if (font === undefined || font === null || typeof font !== 'object') return undefined;
  const override = lookup(ctx.options.fonts, font.name);
  if (override !== undefined) return override;
  const cleaned = cleanFontName(font.name);
  const generic = genericFamily(font);
  return cleaned === '' ? generic : `"${cleaned}", ${generic}`;
}

const BORDER_STYLES: Readonly<Record<Border['style'], string>> = Object.freeze({
  single: 'solid',
  thick: 'solid',
  hairline: 'solid',
  other: 'solid',
  double: 'double',
  triple: 'double',
  dotted: 'dotted',
  dashed: 'dashed',
  none: 'none',
});

// A CSS border value, such as "1px solid rgb(0, 0, 0)". An unknown style
// is solid.
export function borderCss(ctx: RenderContext, border: Border): string {
  const style = lookup(BORDER_STYLES, border.style) ?? 'solid';
  if (style === 'none') return 'none';
  const width = Math.max(1, Math.round(num(border.width) * num(ctx.options.pxPerTwip)));
  const color = colorOf(ctx, border.color);
  return color === undefined ? `${width}px ${style}` : `${width}px ${style} ${color}`;
}
