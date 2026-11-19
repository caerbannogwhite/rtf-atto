// mount: renders a document into a container in the browser.
import type { RtfDocument } from './model.js';
import type { RenderOptions } from './render/options.js';
import { renderHtml } from './render/render.js';

export interface MountResult {
  // The page elements, one <section> per page, in order.
  pages: Element[];
}

// Sets container.innerHTML to renderHtml(doc, options) and returns the page
// elements. It touches the DOM only through innerHTML and querySelectorAll.
// The selector does not use the class prefix, so any prefix works.
export function mount(doc: RtfDocument, container: Element, options?: RenderOptions): MountResult {
  container.innerHTML = renderHtml(doc, options);
  return { pages: Array.from(container.querySelectorAll('section[data-page]')) };
}
