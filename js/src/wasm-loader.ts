// Reads the bytes of rtf.wasm. Internal to the package.
import { RtfError } from './errors.js';
import type { LoadOptions } from './index.js';

type NodeFs = typeof import('node:fs/promises');

// Reaches node:fs/promises without a static import, so that a bundler that
// builds for the browser sees no Node built-in.
async function nodeFs(): Promise<NodeFs> {
  if (typeof process !== 'undefined' && typeof process.getBuiltinModule === 'function') {
    return process.getBuiltinModule('node:fs/promises');
  }
  const spec = 'node:fs/promises';
  return (await import(spec)) as NodeFs;
}

function messageOf(e: unknown): string {
  return e instanceof Error ? e.message : String(e);
}

// Returns the wasm bytes. Bytes are used as they are. A URL or a string is
// resolved against base, which is the URL of index.js. index.js passes the
// default URL itself, in the form bundlers detect. A file: URL is read from
// disk, any other URL is fetched. Every failure is an RtfError with the
// code load_failed.
export async function readWasm(source: LoadOptions['wasm'], base: string): Promise<Uint8Array> {
  if (source instanceof Uint8Array) return source;
  if (source instanceof ArrayBuffer) return new Uint8Array(source);
  try {
    const url = new URL(source ?? './rtf.wasm', base);
    if (url.protocol === 'file:') {
      const fs = await nodeFs();
      const data = await fs.readFile(url);
      return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }
    const response = await fetch(url);
    if (!response.ok) {
      throw new RtfError(
        'load_failed',
        `fetching ${url.href} failed: ${response.status} ${response.statusText}`,
      );
    }
    return new Uint8Array(await response.arrayBuffer());
  } catch (e) {
    if (e instanceof RtfError) throw e;
    throw new RtfError('load_failed', messageOf(e), { cause: e });
  }
}
