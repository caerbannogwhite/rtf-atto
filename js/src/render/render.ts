// renderHtml: a document as an HTML string with inline styles, one
// <section> per page. It touches neither the wasm nor the DOM.
import type { RtfDocument } from '../model.js';
import { createContext, type RenderOptions } from './options.js';
import { paginate, renderPage } from './pages.js';

export function renderHtml(doc: RtfDocument, options?: RenderOptions): string {
  const ctx = createContext(doc, options);
  let out = '';
  for (const page of paginate(ctx)) out += renderPage(ctx, page);
  return out;
}
