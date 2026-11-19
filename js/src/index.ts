// The public API: load() gives an RtfAtto instance over the wasm reader.
import { createInstance } from './create.js';
import type { RtfDocument } from './model.js';

export type * from './model.js';
export { RtfError } from './errors.js';
export { logicalTables } from './tables.js';
export { renderHtml } from './render/render.js';
export type { RenderOptions } from './render/options.js';
export type { RtfErrorCode } from './errors.js';

export interface LoadOptions {
  // The wasm bytes, or where to get them. The default is rtf.wasm next to
  // this module.
  wasm?: Uint8Array | ArrayBuffer | URL | string;
}

export interface RtfAtto {
  // The core's version, such as "0.1.0".
  readonly version: string;
  parse(bytes: Uint8Array | ArrayBuffer): RtfDocument;
  // The canonical JSON, the same bytes as the rtf2json tool writes.
  parseToJson(bytes: Uint8Array | ArrayBuffer): string;
}

let shared: Promise<RtfAtto> | undefined;

// load() with no argument returns one shared instance. load(options)
// always creates a new instance.
export function load(options?: LoadOptions): Promise<RtfAtto> {
  if (options !== undefined) return createInstance(options.wasm, () => {});
  if (shared === undefined) {
    const promise = createInstance(undefined, () => {
      if (shared === promise) shared = undefined;
    });
    shared = promise;
    // A failed load is dropped too, so that the next load() tries again.
    promise.catch(() => {
      if (shared === promise) shared = undefined;
    });
  }
  return shared;
}
