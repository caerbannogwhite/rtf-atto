# rtf-atto

rtf-atto reads RTF files.
It is aimed at clinical tables made by SAS and also reads general RTF.
The reader is a C++17 core compiled to WebAssembly.
This package returns the document model, the logical tables and an HTML rendering of a file.

The package has no writer yet.
The model holds the bytes of pictures as base64 text. The HTML shows PNG and JPEG pictures; other formats show as a box.
The package is ESM only and has no runtime dependencies.
It runs on Node 22 or later and in current browsers.

## Install

    npm install rtf-atto

## Quick start

```js
import { readFile } from 'node:fs/promises';
import { load, logicalTables, RtfError } from 'rtf-atto';

const rtf = await load();
try {
  const bytes = await readFile('table.rtf');
  const doc = rtf.parse(bytes);
  console.log(doc.sections.length, 'sections');
  const tables = logicalTables(doc);
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
- `logicalTables(doc: RtfDocument): LogicalTable[]` returns the logical tables of a document. It is a function over the model and needs no wasm. It applies the same rules as the C++ core, and the tests pin it to the core's output for every fixture.
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
A viewer puts the number of the page in place of `PAGE`.
Pages count from 1 across the document.
A section with `pageNumberStart` restarts the count on its first page.
A viewer puts the number of pages in place of `NUMPAGES`.
SAS output has one section per page, so the page number is the section number there.

## Rendering

```js
import { load, renderHtml } from 'rtf-atto';
import 'rtf-atto/rtf.css';

const rtf = await load();
const html = renderHtml(rtf.parse(bytes));
```

- `renderHtml(doc: RtfDocument, options?: RenderOptions): string` returns the document as HTML with inline styles, one `<section>` per page. It needs neither the wasm nor the DOM, so it also runs in Node.
- `mount(doc: RtfDocument, container: Element, options?: RenderOptions): MountResult` sets `container.innerHTML` to `renderHtml(doc, options)`. It returns `{ pages }`, the page elements in order. It touches the DOM only through `innerHTML` and `querySelectorAll`.
- `rtf-atto/rtf.css` holds the page frame: the page box, its shadow on screen, and a page break after each page in print. Everything that comes from the document is an inline style.

The fields of `RenderOptions`, all optional:

- `pageNumbers: 'sections' | 'none'`, default `'sections'`. With `'none'`, `PAGE` and `NUMPAGES` fields with no result show nothing.
- `fonts: Record<string, string>`, default `{}`. It maps an RTF font name to a CSS `font-family` value. The value is written as given, so it must be trusted.
- `pxPerTwip: number`, default `1 / 15`, which is 96 pixels per inch.
- `pictures: boolean`, default `true`. With `false`, pictures render nothing, not even a box.
- `classPrefix: string`, default `'rtf-'`. It renames the classes only. `rtf.css` is written for the default prefix.

The page model:

- Each section starts a page. A page break inside a section starts a page too.
- A section with `break: 'none'` continues the current page. That page keeps the header and footer of the section that started it.
- The renderer does not re-flow text. A page with more content than its height grows; nothing is cut off.
- The header and footer of a page follow the first page, left page and right page rules of the section.
- `PAGE` and `NUMPAGES` follow the rule in the model section above. A field with a result shows its result.
- Footnotes and endnotes are numbered across the document from 1. Each note is listed at the bottom of the page that holds its reference.

Untrusted input:

- Every text and every attribute value is HTML-escaped.
- Style values are built from numbers and from fixed lists of words. The one document string that reaches CSS is a font name. It is cleaned to letters, digits, spaces and hyphens and then quoted.
- The output has no `<script>`, no event attributes and no external URLs. A hyperlink field shows its result as plain text.
- A picture becomes a data URL only when it is PNG or JPEG and its base64 text is valid and at most 4,194,304 characters long (about 3 MB of bytes). Any other picture is a box with the format name.

What is not rendered:

- Pictures in EMF, WMF, DIB, BMP or an unknown format. They show as a box with the format name.
- A column break shows as a line break. Columns are ignored.
- `even`, `odd` and `column` section breaks start a new page. No blank page is inserted.
- Deleted and inserted text shows without a revision style.
- Text direction in cells, preferred cell widths, shading patterns, the border `space`, tab leaders, the `keep` properties and widow control are ignored.
- The geometry of shapes is dropped. A shape shows its text and its picture as a block after its paragraph. An embedded object shows its result the same way.

On Node 26 on Windows, `renderHtml` took 1.0 ms for the fixture `sas_table_3pages` (3 pages) after one warm-up call.
The HTML of that fixture is 31,933 bytes.

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
A local build with Emscripten 3.1.71 is 165,604 bytes.
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
The package and its tests need Node 22 or later.
The C++ core and its tests are in the repository root.

## Demo

The demo page renders a document in the browser, so that you can compare
the result with Word. From `js`:

    npm install
    npm run build
    npm run demo

The script prints `http://127.0.0.1:8787/js/demo/`. Set `PORT` to use
another port. The server listens on 127.0.0.1 only and serves the
repository read-only.

Choose a fixture from the list, or open your own `.rtf` files. The page
shows the rendered pages and a status line with the core version, the parse
time, the render time, the page count and the warning count. The warnings
are listed below the status line. The controls switch the page numbers and
the pictures, set the zoom, and show the logical tables below the pages.
Spacer rows are grey.

Files you open stay in the browser; nothing is uploaded or stored.

## Licence

Apache-2.0.
