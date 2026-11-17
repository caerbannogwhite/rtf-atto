# rtf-atto

rtf-atto reads RTF files.
It is aimed at clinical tables made by SAS and also reads general RTF.
The reader is a C++17 core compiled to WebAssembly.
This package returns the document model and the logical tables of a file.

The package has no renderer yet. It has no writer yet.
It returns the bytes of pictures in the model as base64 text, but it does not render figures.
The package is ESM only and has no runtime dependencies.
It runs on Node 20 or later and in current browsers.

## Install

    npm install rtf-atto

## Quick start

```js
import { readFile } from 'node:fs/promises';
import { load, RtfError } from 'rtf-atto';

const rtf = await load();
try {
  const bytes = await readFile('table.rtf');
  const doc = rtf.parse(bytes);
  console.log(doc.sections.length, 'sections');
  const tables = rtf.logicalTables(bytes);
  console.log(tables.length, 'logical tables');
} catch (e) {
  if (e instanceof RtfError && e.code === 'not_rtf') {
    console.error('This is not an RTF file.');
  } else {
    throw e;
  }
}
```

## API

- `load(options?: LoadOptions): Promise<RtfAtto>` loads the wasm and returns the reader. With no argument it returns one shared instance. With `options` it builds a new one.
- `LoadOptions { wasm?: Uint8Array | ArrayBuffer | URL | string }` gives the wasm bytes, or says where to get them.
- `RtfAtto.version: string` is the version of the C++ core.
- `RtfAtto.parse(bytes): RtfDocument` returns the document model.
- `RtfAtto.parseToJson(bytes): string` returns the same model as a JSON string.
- `RtfAtto.logicalTables(bytes): LogicalTable[]` returns the logical tables. It takes the RTF bytes, not a document, and parses them again in the core.
- `RtfAtto.logicalTablesJson(bytes): string` returns the logical tables as a JSON string.
- `RtfError` extends `Error`. Its `name` is `'RtfError'` and its `code` is an `RtfErrorCode`.

`bytes` is a `Uint8Array` or an `ArrayBuffer`.
The types `RtfDocument` and `LogicalTable` are exported, for example
`import type { RtfDocument, LogicalTable } from 'rtf-atto'`.

The error codes:

- `not_rtf`: the input does not start with `{\rtf`. Empty input is also `not_rtf`.
- `invalid_argument`: the input is not a `Uint8Array` or an `ArrayBuffer`.
- `out_of_memory`: the wasm could not allocate the output.
- `wasm_aborted`: the wasm instance trapped or aborted. The instance is dead.
- `load_failed`: the wasm bytes could not be read or compiled.
- `internal`: the core returned an unexpected code.

## The model

Page setup, indents and cell edges are in twips.
Font sizes are in half-points.
A property at its default value is left out of the JSON.
`fonts` is keyed by the RTF font number.
A colour is an index into `colors`. A `null` entry in `colors` means the automatic colour.
SAS output has one section per page.
The full shape of the model is in `src/model.ts`, shipped as `dist/src/model.d.ts`.

A viewer has one rule to follow.
SAS writes `PAGE` and `NUMPAGES` fields with no result at all.
The reader reports `instruction: "PAGE"` and `result: []`.
A viewer that shows one section per page puts the number of the section in place of `PAGE`.
It puts the number of sections in place of `NUMPAGES`.

## Loading the wasm

By default the package reads `rtf.wasm` from the directory of the module.
It finds the file with the literal expression `new URL('./rtf.wasm', import.meta.url)`.
Vite, webpack 5 and Parcel detect this form and copy `rtf.wasm` as an asset.
In Node, a `file:` URL is read with `fs`.
Any other URL is read with `fetch`.

Where the default does not work, pass the wasm yourself with `load({ wasm })`.
The value is a `Uint8Array`, an `ArrayBuffer`, a `URL` or a URL string.
A string is a URL string. It is resolved against the URL of the module.
A file path must go through `pathToFileURL` from `node:url` first.
A Windows path such as `C:\app\rtf.wasm` parses as a URL with the scheme `c:` and fails with `load_failed`.
The package exports the file as `rtf-atto/rtf.wasm`.

## Untrusted input

Every method throws `RtfError` and nothing else.
When the wasm aborts, the instance is unusable.
The method throws `wasm_aborted`.
For the shared instance, the next `load()` builds a new instance.
For an instance from `load(options)`, call `load(options)` again.

The input is copied into wasm memory, and the model and the JSON live there too.
A file of 1.6 MB with 20,000 table rows took the wasm memory from 21 MB to 91 MB during the call.
Memory grows with the size of the document.
The core is built without exceptions.
When wasm memory runs out during parsing, the allocation failure calls `abort()`.
The method then throws `wasm_aborted`.
`out_of_memory` covers only the allocation of the final output buffer.
Wasm memory never shrinks.
The shared instance keeps its peak size, such as 91 MB after the file above, for the life of the page or process.
`load({})` gives an instance that you can drop when you are done with it.

## Size and speed

The size of `rtf.wasm` depends on the Emscripten version.
The published file is built in CI with emsdk 6.0.13 and is 184,396 bytes.
A local build with Emscripten 3.1.71 is 185,053 bytes.
Parsing all 19 test fixtures through the wasm and comparing the JSON with the
golden files took 84 ms on Node 26 on Windows.

## Building from source

The wasm needs Emscripten. The author builds with 3.1.71. CI builds with 6.0.13.
You need Emscripten (with `emcmake` on the PATH) and CMake.
On Windows you also need Ninja on the PATH.

    cd js
    npm install
    npm run build
    npm test

`npm run build` compiles `wasm/` with `emcmake` and CMake. Then it runs `tsc`.
On Windows it passes `-G Ninja` to CMake.
`npm test` checks the model types against the golden files, then runs the node tests.
The tests need Node 22 or later, because `node --test` selects the files with a glob.
The package itself runs on Node 20 or later.
The C++ core and its tests are in the repository root.

## Licence

Apache-2.0.
