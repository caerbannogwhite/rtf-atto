export interface RtfWasmModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _rtf_parse(data: number, size: number, out: number, outLen: number): number;
  _rtf_free(ptr: number): void;
  _rtf_version(): number;
}
export interface RtfWasmModuleOptions {
  wasmBinary?: Uint8Array | ArrayBuffer;
  onAbort?: (reason: unknown) => void;
  print?: (text: string) => void;
  printErr?: (text: string) => void;
}
export default function createRtfModule(options?: RtfWasmModuleOptions): Promise<RtfWasmModule>;
