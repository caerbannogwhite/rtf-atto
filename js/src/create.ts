// The wasm instance behind the public API, and the seam tests use to replace
// the module factory.
import createRtfModule from './rtf.js';
import type { RtfWasmModule, RtfWasmModuleOptions } from './rtf.js';
import { RtfError } from './errors.js';
import type { RtfErrorCode } from './errors.js';
import { readWasm } from './wasm-loader.js';
import type { LogicalTable, RtfDocument } from './model.js';
import type { LoadOptions, RtfAtto } from './index.js';

export type ModuleFactory = (options: RtfWasmModuleOptions) => Promise<RtfWasmModule>;

export const DEAD_MESSAGE = 'the wasm instance aborted; call load() again';

let factory: ModuleFactory = createRtfModule;

// Replaces the Emscripten factory; undefined restores it. For tests.
export function useModuleFactory(replacement?: ModuleFactory): void {
  factory = replacement ?? createRtfModule;
}

const decoder = new TextDecoder('utf-8');

// Maps a non-zero return code of the C ABI to an error code.
function codeName(code: number): RtfErrorCode {
  switch (code) {
    case -1:
      return 'not_rtf';
    case -2:
      return 'invalid_argument';
    case -3:
      return 'out_of_memory';
    default:
      return 'internal';
  }
}

function toBytes(bytes: unknown): Uint8Array {
  if (bytes instanceof Uint8Array) return bytes;
  if (bytes instanceof ArrayBuffer) return new Uint8Array(bytes);
  throw new RtfError('invalid_argument', 'the input must be a Uint8Array or an ArrayBuffer');
}

// Whether the instance is dead, shared between the instance and onAbort.
interface Life {
  dead: boolean;
  die(): void;
}

type Entry = '_rtf_parse' | '_rtf_logical_tables';

class Instance implements RtfAtto {
  readonly version: string;
  readonly #module: RtfWasmModule;
  readonly #life: Life;

  constructor(module: RtfWasmModule, life: Life) {
    this.#module = module;
    this.#life = life;
    const start = module._rtf_version();
    const heap = module.HEAPU8;
    let end = start;
    while (heap[end] !== 0) end++;
    this.version = decoder.decode(heap.subarray(start, end));
  }

  parse(bytes: Uint8Array | ArrayBuffer): RtfDocument {
    return JSON.parse(this.parseToJson(bytes)) as RtfDocument;
  }

  parseToJson(bytes: Uint8Array | ArrayBuffer): string {
    return this.#call('_rtf_parse', bytes);
  }

  logicalTables(bytes: Uint8Array | ArrayBuffer): LogicalTable[] {
    return JSON.parse(this.logicalTablesJson(bytes)) as LogicalTable[];
  }

  logicalTablesJson(bytes: Uint8Array | ArrayBuffer): string {
    return this.#call('_rtf_logical_tables', bytes);
  }

  // Every ABI call goes through here.
  #call(entry: Entry, bytes: Uint8Array | ArrayBuffer): string {
    if (this.#life.dead) throw new RtfError('wasm_aborted', DEAD_MESSAGE);
    const input = toBytes(bytes);
    try {
      return this.#invoke(entry, input);
    } catch (e) {
      if (e instanceof RtfError) throw e;
      // A trap or an abort leaves the instance in an unknown state.
      // Any other exception counts, because V8 reports a wasm stack overflow
      // as a RangeError and not as a WebAssembly.RuntimeError. The rare false
      // positive: a RangeError from decoding an output above V8's string
      // limit marks a healthy instance as dead.
      this.#life.die();
      throw new RtfError('wasm_aborted', String(e), { cause: e });
    }
  }

  #invoke(entry: Entry, input: Uint8Array): string {
    const m = this.#module;
    const inPtr = m._malloc(Math.max(1, input.length));
    if (inPtr === 0) throw new RtfError('out_of_memory', 'out_of_memory');
    let slots = 0;
    let outPtr = 0;
    try {
      slots = m._malloc(8);
      if (slots === 0) throw new RtfError('out_of_memory', 'out_of_memory');
      m.HEAPU8.set(input, inPtr);
      m.HEAPU32[slots >>> 2] = 0;
      m.HEAPU32[(slots >>> 2) + 1] = 0;
      const code = m[entry](inPtr, input.length, slots, slots + 4);
      // Memory growth during the call replaces HEAPU8 and HEAPU32, so read
      // them from the module again instead of keeping a view from before.
      const heap32 = m.HEAPU32;
      outPtr = heap32[slots >>> 2]!;
      const outLen = heap32[(slots >>> 2) + 1]!;
      const text = outPtr === 0 ? '' : decoder.decode(m.HEAPU8.subarray(outPtr, outPtr + outLen));
      if (code !== 0) {
        const name = codeName(code);
        throw new RtfError(name, text === '' ? name : text);
      }
      return text;
    } finally {
      if (outPtr !== 0) m._rtf_free(outPtr);
      if (slots !== 0) m._free(slots);
      m._free(inPtr);
    }
  }
}


// Builds one instance over the current factory. onDeath runs once when the
// instance dies.
export async function createInstance(source: LoadOptions['wasm'], onDeath: () => void): Promise<RtfAtto> {
  // Bundlers detect only this literal form and copy rtf.wasm as an asset.
  // The glue's own new URL("rtf.wasm", import.meta.url) is not what we read.
  const wasmBinary = await readWasm(source ?? new URL('./rtf.wasm', import.meta.url), import.meta.url);
  const life: Life = {
    dead: false,
    die() {
      if (life.dead) return;
      life.dead = true;
      onDeath();
    },
  };
  let module: RtfWasmModule;
  try {
    // printErr drops every line the glue writes to stderr, for the life of
    // the instance. The glue writes such a line before it aborts. The
    // rejection or the RtfError carries the same text.
    module = await factory({
      wasmBinary,
      onAbort: () => life.die(),
      printErr: () => {},
    });
  } catch (e) {
    throw new RtfError('load_failed', e instanceof Error ? e.message : String(e), { cause: e });
  }
  return new Instance(module, life);
}
