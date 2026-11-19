// The renderer's options and the context that every render function receives.
import type { RtfDocument } from '../model.js';
import { fmt } from './css.js';

export interface RenderOptions {
  pageNumbers?: 'sections' | 'none';   // default 'sections'
  fonts?: Record<string, string>;      // RTF font name -> CSS font-family, inserted as given (trusted)
  pxPerTwip?: number;                  // default 1 / 15
  pictures?: boolean;                  // default true
  classPrefix?: string;                // default 'rtf-'
}

// The options with every default filled in.
export interface ResolvedOptions {
  pageNumbers: 'sections' | 'none';
  fonts: Record<string, string>;
  pxPerTwip: number;
  pictures: boolean;
  classPrefix: string;
}

// What the block and inline renderers share for one renderHtml call.
export interface RenderContext {
  doc: RtfDocument;
  options: ResolvedOptions;
  // A length in twips as a CSS pixel value, such as "96px".
  px(twips: number): string;
  // A class name with the prefix, such as cls('p') = "rtf-p".
  cls(name: string): string;
}

export function resolveOptions(options: RenderOptions | undefined): ResolvedOptions {
  return {
    pageNumbers: options?.pageNumbers ?? 'sections',
    fonts: options?.fonts ?? {},
    pxPerTwip: options?.pxPerTwip ?? 1 / 15,
    pictures: options?.pictures ?? true,
    classPrefix: options?.classPrefix ?? 'rtf-',
  };
}

export function createContext(doc: RtfDocument, options: RenderOptions | undefined): RenderContext {
  const resolved = resolveOptions(options);
  return {
    doc,
    options: resolved,
    px: (twips) => fmt(twips * resolved.pxPerTwip) + 'px',
    cls: (name) => resolved.classPrefix + name,
  };
}
