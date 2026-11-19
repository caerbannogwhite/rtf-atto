// The renderer's options and the context that every render function receives.
import type { Block, RtfDocument } from '../model.js';
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

// A footnote or endnote whose reference has been rendered on the page.
export interface PendingNote {
  number: number;
  blocks: Block[];
}

// The state of the page being rendered. renderPage sets the page fields
// before it renders the page's header, body and footer.
export interface RenderState {
  pageNumber: number;      // the PAGE value of the page
  pageCount: number;       // the NUMPAGES value: the number of pages rendered
  noteCount: number;       // notes numbered so far, across the document
  notes: PendingNote[];    // the notes referenced on the page, in order
  // Blocks that go after the paragraph being rendered: the markup of its
  // shapes and objects, which may not sit inside a <p>.
  after: string[];
}

// What the block and inline renderers share for one renderHtml call.
export interface RenderContext {
  doc: RtfDocument;
  options: ResolvedOptions;
  state: RenderState;
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
    state: { pageNumber: 0, pageCount: 0, noteCount: 0, notes: [], after: [] },
    px: (twips) => fmt(twips * resolved.pxPerTwip) + 'px',
    cls: (name) => resolved.classPrefix + name,
  };
}
