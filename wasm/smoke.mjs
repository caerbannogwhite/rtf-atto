// Smoke test for the wasm build. Usage: node wasm/smoke.mjs <build-dir>
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";

const buildDir = path.resolve(process.argv[2] ?? "build-wasm");
const glue = await import(pathToFileURL(path.join(buildDir, "rtf.js")).href);
const wasmBinary = fs.readFileSync(path.join(buildDir, "rtf.wasm"));
const module = await glue.default({ wasmBinary });

// Read the NUL-terminated version string.
const versionPtr = module._rtf_version();
const heap = module.HEAPU8;
let end = versionPtr;
while (heap[end] !== 0) end++;
const version = new TextDecoder().decode(heap.subarray(versionPtr, end));

// Parse a tiny document.
const input = new TextEncoder().encode("{\\rtf1\\ansi Hello\\par}");
const inPtr = module._malloc(input.length);
const slots = module._malloc(8);
module.HEAPU8.set(input, inPtr);
const code = module._rtf_parse(inPtr, input.length, slots, slots + 4);

// Fetch fresh views after the call, because the memory may have grown.
const slotView = new Uint32Array(module.HEAPU8.buffer, slots, 2);
const outPtr = slotView[0];
const outLen = slotView[1];
let json = "";
if (code === 0) {
  json = new TextDecoder().decode(module.HEAPU8.slice(outPtr, outPtr + outLen));
  module._rtf_free(outPtr);
}
module._free(slots);
module._free(inPtr);

if (code !== 0 || !json.includes('"Hello"')) {
  console.error(`smoke test failed: parse code ${code}, json: ${json}`);
  process.exit(1);
}
console.log(`version ${version}, parse code ${code}, json ${outLen} bytes`);
