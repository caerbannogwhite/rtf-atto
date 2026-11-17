// The error that every method of the package throws.

// not_rtf, invalid_argument and out_of_memory come from the core.
// wasm_aborted: the wasm instance trapped or aborted; load a new one.
// load_failed: the wasm file could not be read or compiled.
// internal: the core returned a code that this wrapper does not know.
export type RtfErrorCode =
  | 'not_rtf'
  | 'invalid_argument'
  | 'out_of_memory'
  | 'wasm_aborted'
  | 'load_failed'
  | 'internal';

export class RtfError extends Error {
  readonly code: RtfErrorCode;

  constructor(code: RtfErrorCode, message: string) {
    super(message);
    this.name = 'RtfError';
    this.code = code;
  }
}
