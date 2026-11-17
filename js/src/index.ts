// The public API: load() gives an RtfAtto instance over the wasm reader.
import createRtfModule from './rtf.js';
import type { RtfWasmModule } from './rtf.js';
import { RtfError } from './errors.js';
import type { RtfErrorCode } from './errors.js';
import { readWasm } from './wasm-loader.js';
import type { LogicalTable, RtfDocument } from './model.js';

export type * from './model.js';
export { RtfError } from './errors.js';
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
  logicalTables(bytes: Uint8Array | ArrayBuffer): LogicalTable[];
  logicalTablesJson(bytes: Uint8Array | ArrayBuffer): string;
}

const DEAD_MESSAGE = 'the wasm instance aborted; call load() again';
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
      this.#life.die();
      throw new RtfError('wasm_aborted', String(e));
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

async function create(source: LoadOptions['wasm'], onDeath: () => void): Promise<RtfAtto> {
  const wasmBinary = await readWasm(source, import.meta.url);
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
    // The glue writes a line to stderr before it aborts. The rejection or
    // the RtfError carries the same text, so printErr drops the line.
    module = await createRtfModule({
      wasmBinary,
      onAbort: () => life.die(),
      printErr: () => {},
    });
  } catch (e) {
    throw new RtfError('load_failed', e instanceof Error ? e.message : String(e));
  }
  return new Instance(module, life);
}

let shared: Promise<RtfAtto> | undefined;

// load() with no argument returns one shared instance. load(options)
// always creates a new instance.
export function load(options?: LoadOptions): Promise<RtfAtto> {
  if (options !== undefined) return create(options.wasm, () => {});
  if (shared === undefined) {
    const promise = create(undefined, () => {
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
