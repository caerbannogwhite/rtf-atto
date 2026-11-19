# RTF renderer implementation plan (rtf-atto, plan 3 of 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render an `RtfDocument` to HTML in pure TypeScript inside the npm package, port the logical tables to TypeScript so that nothing parses the bytes a second time, and apply the package decisions Massimo took after the review of plan 2.

**Architecture:** `js/src/tables.ts` ports `src/tables.cpp` over the model, so `logicalTables(doc)` is a pure function pinned to the C++ output by the committed `.tables.json` goldens. `js/src/render/` turns a document into pages, blocks and inlines as an HTML string with inline styles; `rtf.css` holds the page frame and the print rules; `mount()` is a thin browser helper over `renderHtml()`. A demo page served by a small Node script gives Massimo the browser check against Word. The C++ core is unchanged; the wasm stops exporting `_rtf_logical_tables`.

**Tech Stack:** TypeScript 5.9 strict, Node 22 or later, `node:test` and `node:assert`, no runtime dependencies, ESM only. Emscripten as in plan 2.

**Spec:** `docs/plans/2026-11-10-rtf-parser-design.md`, sections "The renderer", "Logical tables", "The npm package", "Validation" items 6 and 7, "Work order" item 3. The decisions Massimo took on 2026-11-18 (below) amend the spec and plan 2.

## Massimo's decisions of 2026-11-18

These replace the open items of `docs/notes/2026-11-17-plan-2-deferred-findings.md`.

1. **No second parse.** Plan 2's decision 1 is overruled: `logicalTables` must not parse the bytes again. This plan ports the stitching rules to TypeScript (Task 3).
2. **Node floor 22.** The package runs on Node 22 or later; CI tests on Node 22 and 24 (Task 1).
3. **A `src`-only type check** without the Node types (Task 1).
4. **Actions keep their major tags; Dependabot moves them; trusted publishing replaces the npm token** (Task 1).
5. **The dead-instance test is added** (Task 2).
6. **The catch-all and the depth-limit text loss stay as they are.** Nothing in this plan touches them.
7. **Publishing waits until the end of plan 3.** No tag and no `npm publish` in this plan. The first version is published by hand; the workflow then takes over through trusted publishing.

## Global constraints

- The C++ core is not changed: `src/`, `include/`, `wasm/exports.cpp`, `wasm/rtf_abi.h` and the goldens stay byte for byte as they are. The only build change is the export list in `wasm/CMakeLists.txt` (Task 3).
- The npm package is ESM only, has no runtime dependencies, and its only development dependencies are `typescript` and `@types/node`. No test framework, no bundler, no linter.
- TypeScript strict mode. Relative imports carry the `.js` extension. `src/` compiles with and without the Node types (Task 1).
- Runtime support is Node 22 or later and current browsers (Massimo, 2026-11-18).
- The renderer is pure TypeScript: `renderHtml` touches neither the wasm nor the DOM. `mount` touches the DOM only through the container's `innerHTML` and `querySelectorAll`.
- Untrusted input (spec): every text node and attribute value is HTML-escaped. Style values are built from numbers and from fixed vocabularies; the only document string that reaches CSS is a font name cleaned to letters, digits, spaces and hyphens and quoted. Data URLs are emitted only for PNG and JPEG under the size cap of decision 11. No `<script>`, no event attributes, no external URLs; a hyperlink field renders its result as text.
- Determinism: the same document and options give the same string. No dates, no random identifiers, no locale-dependent formatting.
- The wasm and the native build give the same bytes of JSON for every fixture. The committed `.json` and `.tables.json` goldens are the authority for the TypeScript port too; a difference is a bug in the port, never a reason to regenerate.
- Measured numbers only in READMEs and comments. The implementer measures and reports; the plan's numbers in tests are exact by construction.
- Massimo's sample files are never used by the tests and never enter the repository. The demo page reads local files in the browser with `FileReader`; nothing is uploaded or stored.
- Plain English in code comments, commit messages and documents (`~/.claude/CLAUDE.md`).
- Commit messages: one imperative sentence in plain English, no type prefix, an optional body, and at the end the `Co-Authored-By` line the harness gives. Commits go to the branch `plan-3-renderer`, created from `main`; the plan file and the note update are its first commit; the branch is pushed when Task 7 is complete so CI runs (Massimo's approval of this plan is the consent for that push); the pull request or merge is his decision at the end.

## Review focus

The five input classes the spec implies but no task's tests would exercise without these lines. Each has its test in the owning task.

1. **Markup in text and in font names.** A run that reads `<script>alert(1)</script>` must come out escaped, and a font named `"Arial"; } body { color: red` must reach CSS as `Arial body color red`. Test `html_escapes_text_and_font_names` (Task 4).
2. **Malformed merges.** A vertically continued cell with no first cell above it, and a horizontally continued cell at the start of a row, must render as plain cells, never as a negative or zero span. Test `malformed_merges_render_as_plain_cells` (Task 5).
3. **Empty things keep their height.** An empty paragraph keeps one line, and a page with no header or footer keeps its margin areas, so that rows per page match Word. Tests `empty_paragraph_keeps_a_line` and `page_areas_keep_the_margins` (Task 4).
4. **Page fields at the edges.** A `PAGE` field with a result, the option `pageNumbers: 'none'`, a section with `pageNumberStart`, and a `NUMPAGES` in a footer when a page break splits a section. Test `page_fields_follow_the_option` (Task 6).
5. **Pictures outside the rules.** Data that is not base64, data above the cap, an EMF, and `pictures: false`. Test `pictures_outside_the_rules_become_boxes` (Task 6).

## Repository and conventions

- Local clone: `C:\Users\massi\source\repos\rtf-atto`, remote `origin` = https://github.com/caerbannogwhite/rtf-atto.git. `main` holds plans 1 and 2 (135b754). Work happens on the branch `plan-3-renderer`.
- Naming as in plan 2: TypeScript types PascalCase, functions and variables camelCase, files kebab-case; CSS classes `rtf-` plus a kebab-case word.
- Node tests are `js/test/*.test.ts`, compiled by `tsc` to `js/dist/test/` and run with `node --test`; each file uses `test` from `node:test` and `assert` from `node:assert/strict`; test names are the behaviour they pin. Renderer tests go through `renderHtml` on documents built with `js/test/builders.ts`; they never import the render modules directly, so the internal module layout stays the implementer's.
- Commands are for Git Bash on Massimo's machine, from the repository root unless a `cd` is written:
  - Native (MSVC): `cmake --build build --config Release -j 8 && ctest --test-dir build -C Release --output-on-failure`. Native (GCC): `cmake --build build-gcc && ctest --test-dir build-gcc --output-on-failure`. Both must stay at 13 of 13; nothing in this plan changes them.
  - Package: `export PATH="/c/Users/massi/source/repos/emsdk/upstream/emscripten:$PATH"` once per shell, then `cd js && npm run build && npm test`. The local emsdk is 3.1.71; CI uses 6.0.13. Node here is 26.13.
- If a test executable fails with "Access is denied" right after linking, retry once (antivirus).

## File structure

```
rtf-atto/
  .gitattributes                       + *.html *.css text eol=lf                               (Task 1)
  .github/dependabot.yml               github-actions weekly; npm in /js weekly                 (Task 1)
  .github/workflows/ci.yml             wasm job on Node 22 and 24                               (Task 1)
  .github/workflows/publish.yml        trusted publishing; no-op when the version is on npm     (Task 1)
  README.md                            "Releasing" section; "npm package" and "Status" updated  (Tasks 1, 7)
  wasm/CMakeLists.txt                  export list without _rtf_logical_tables                  (Task 3)
  docs/notes/2026-11-17-plan-2-deferred-findings.md   decisions of 2026-11-18 (first commit, with this plan)
  js/package.json                      engines >=22; build:ts; exports ./rtf.css; scripts regen:html, demo (Tasks 1, 6, 7)
  js/tsconfig.src.json                 src only, no Node types, noEmit                          (Task 1)
  js/src/wasm-loader.ts                compiles without the Node types                          (Task 1)
  js/src/create.ts                     createInstance(), useModuleFactory(), DEAD_MESSAGE       (Task 2)
  js/src/index.ts                      load() over create.ts; exports logicalTables, renderHtml, mount (Tasks 2, 3, 4, 6)
  js/src/rtf.d.ts                      without _rtf_logical_tables                              (Task 3)
  js/src/text.ts                       inlineText(), blocksText(), cellText()                   (Task 3)
  js/src/tables.ts                     logicalTables(document)                                  (Task 3)
  js/src/render/options.ts             RenderOptions, resolveOptions()                          (Task 4)
  js/src/render/html.ts                escaping, elements, style attributes                     (Task 4)
  js/src/render/css.ts                 twips to px, colours, fonts, borders                     (Task 4)
  js/src/render/pages.ts               sections to pages, header and footer choice, numbering   (Task 4, Task 6)
  js/src/render/inlines.ts             runs, tabs, breaks, fields, bookmarks, footnote marks, pictures, shapes, objects (Tasks 4, 6)
  js/src/render/blocks.ts              paragraphs; tables delegated to tables.ts                (Task 4)
  js/src/render/tables.ts              table grid, spans, cell styles                           (Task 5)
  js/src/render/render.ts              renderHtml()                                             (Task 4)
  js/src/mount.ts                      mount()                                                  (Task 6)
  js/src/rtf.css                       page frame, table reset, print rules                     (Task 6)
  js/scripts/copy-assets.mjs           copies src/rtf.css into dist/src                         (Task 6)
  js/scripts/regen-html-goldens.mjs    writes test/goldens/*.html from the fixtures             (Task 6)
  js/scripts/serve.mjs                 the demo's static server                                 (Task 7)
  js/demo/index.html, demo.js, demo.css   the demo page                                         (Task 7)
  js/test/goldens/*.html               17 HTML goldens, one per fixture that parses             (Task 6)
  js/test/builders.ts                  doc(), para(), run(), table(), LETTER                    (Task 3)
  js/test/html-helpers.ts              stripTags()                                              (Task 4)
  js/test/instance.test.ts             the dead instance                                        (Task 2)
  js/test/tables.test.ts               logicalTables against the goldens and unit cases         (Task 3)
  js/test/render-pages.test.ts         pages, headers, footers, geometry                        (Task 4)
  js/test/render-text.test.ts          paragraphs, runs, tabs, escaping                         (Task 4)
  js/test/render-tables.test.ts        tables                                                   (Task 5)
  js/test/render-inlines.test.ts       fields, footnotes, pictures, bookmarks, shapes, objects, mount (Task 6)
  js/test/render-goldens.test.ts       HTML goldens and text coverage                           (Task 6)
  js/test/serve.test.ts                the demo server                                          (Task 7)
  js/test/goldens.test.ts, errors.test.ts, growth.test.ts, package.test.ts   updated (Tasks 1, 3, 6)
  js/README.md                         Node 22; API without the byte-taking table methods; "Rendering"; "Demo" (Tasks 1, 3, 6, 7)
```

## Decisions this plan takes

Each is the builder's; Massimo can overrule any of them before execution.

1. **`logicalTables(doc)` is a function exported from the package root, not a method of `RtfAtto`.** It needs no wasm. The methods `logicalTables(bytes)` and `logicalTablesJson(bytes)` are removed; nothing is published yet, so nothing breaks. The C ABI keeps `rtf_logical_tables` for C callers and for the native test `abi`; the wasm no longer exports it, which removes the stitching code from `rtf.wasm`.
2. **The port keeps the C++ structure.** `text.ts` ports `src/text.cpp` (`inlineText`, `blocksText`, `cellText`) and `tables.ts` ports `src/tables.cpp` function by function with the same names in camelCase. Deep equality against every `.tables.json` golden is the test; the input of that test is the committed `.json` golden, so it needs no wasm.
3. **Page numbers count pages.** `PAGE` is the page's number counting from 1 across the document, restarting at `pageNumberStart` on the first page of a section that sets it; `NUMPAGES` is the number of pages rendered. For SAS files, one section per page, this equals the spec's wording "section number" and "section count". The viewer's rule in `js/README.md` is reworded to pages.
4. **HTML goldens live in `js/test/goldens/<name>.html`**, one per fixture that parses (17 of 19), written by `npm run regen:html` and reviewed in the pull request like code. The two `not_rtf` fixtures have none.
5. **The module-factory seam is `useModuleFactory()` in `js/src/create.ts`.** The tests import `create.js` by its path. The file ships in `dist/src` but `exports` does not list it, so a consumer cannot import it.
6. **Trusted publishing with a no-op for published versions.** The publish workflow has no token: `npm publish --provenance --access public` under `id-token: write`, the pattern of npm's trusted-publishing documentation. A step skips everything when the tag's version is already on npm, so the `v0.1.0` tag that follows Massimo's hand publish gives a green run. Until he registers the workflow as the trusted publisher on npmjs.com, a tag for an unpublished version fails at the publish step, which is the right signal.
7. **The wasm job runs on Node 22 and 24.** The publish job stays on 24.
8. **The demo server listens on 127.0.0.1 only**, port 8787 unless `PORT` is set, serves the repository root read-only with no directory listing, and answers `GET /api/fixtures` with the fixture names. The page imports the built package from `../dist/src/index.js`, so the browser fetch path of the wasm loader is exercised for the first time.
9. **Output formatting.** One newline after every page element and every block-level element (`p`, `table`, `div`), none inside inline markup. Numbers are written with at most two decimals (`793.67px`), trailing zeros dropped.
10. **Rules the spec leaves open**, decided here: a `columnBreak` renders as `<br>`; a section with `break: 'none'` continues the current page, whose header and footer stay those of the section that started it; `'even'`, `'odd'` and `'column'` start a new page with no blank page inserted; the Symbol-charset bullet U+F0B7 in `listText` shows as U+2022; `deleted` and `inserted` runs render unstyled; `textDirection`, `preferredWidth`, shading patterns, border `space`, tab leaders, `keep*`, `widowControl` and `columns` are ignored; footnotes and endnotes are numbered across the document and listed at the bottom of the page that holds their reference; `classPrefix` renames the classes only, `rtf.css` is written for the default prefix.
11. **Pictures.** PNG and JPEG become `<img>` with a data URL when the base64 text is at most 4,194,304 characters (about 3 MB of bytes) and matches `^[A-Za-z0-9+/]*={0,2}$`. Anything else, and every other format, becomes a box with the format name. Size: `goalWidth` and `goalHeight` in twips, scaled by `scaleX` and `scaleY` percent when present; else `width` and `height` as pixels; else the natural size.
12. **Page geometry.** The page is `width: page.width`, `min-height: page.height`, `padding-left: marginLeft`, `padding-right: marginRight`, base font from `defaultFont` at 24 half-points. Inside it, a header area with `min-height: marginTop` and `padding-top: headerDistance`, the body, and a footer area with `min-height: marginBottom` and `padding-bottom: footerDistance` whose content sits at its bottom. A page taller than `page.height` grows; nothing is clipped or re-flowed. The `landscape` flag adds the class `rtf-landscape` only; width and height are used as given.
13. **Cell padding** is the cell's `padding`, else the row's, else left and right of `gapHalf ?? 108` twips and top and bottom of 0.
14. **Tabs.** The n-th tab of a paragraph (counting from 1, `listText` included) spans from the previous stop (0 for the first) to `tabs[n-1].position`; when the paragraph has no stop left, it spans 720 twips. A tab is `<span class="rtf-tab" style="display: inline-block; width: ...px"></span>`.

---

### Task 1: Node 22, the src-only type check, Dependabot and trusted publishing

**Files:**
- Create: `js/tsconfig.src.json`, `.github/dependabot.yml`
- Modify: `js/package.json`, `js/src/wasm-loader.ts`, `.github/workflows/ci.yml`, `.github/workflows/publish.yml`, `.gitattributes`, `js/README.md:11`, `js/README.md:135-136`, `README.md` (new section before "Status"), `js/test/package.test.ts`, `js/test/helpers.ts:32-34`

**Interfaces:**
- Consumes: the plan 2 package as it is.
- Produces: `npm run build:ts` = `tsc -p tsconfig.src.json && tsc -p tsconfig.json`; `packageJson()` in `helpers.ts` returns `{ version: string; exports: unknown; engines: { node: string } }`.

- [ ] **Step 1: Write the failing test** in `js/test/package.test.ts`:

```ts
test('engines_is_node_22_or_later', () => {
  assert.equal(packageJson().engines.node, '>=22');
});
```

Extend the return type of `packageJson()` in `helpers.ts` with `engines: { node: string }`.

- [ ] **Step 2: Run `cd js && npm run build && npm test`; expected: FAIL**, the new test reports `'>=20'`.

- [ ] **Step 3: Write `js/tsconfig.src.json`**:

```json
{
  "extends": "./tsconfig.json",
  "compilerOptions": {
    "noEmit": true,
    "types": [],
    "lib": ["ES2022", "DOM", "DOM.Iterable"]
  },
  "include": ["src"]
}
```

- [ ] **Step 4: Run `npx tsc -p tsconfig.src.json` in `js`; expected: errors in `src/wasm-loader.ts`** (`process` is not defined; `node:fs/promises` cannot be found). No other file may fail; if one does, fix it the same way.

- [ ] **Step 5: Make `wasm-loader.ts` compile without the Node types.** Replace the `NodeFs` alias and `nodeFs()` with local types and a `globalThis` lookup; behaviour unchanged:

```ts
interface FsPromisesLike { readFile(path: URL): Promise<Uint8Array>; }
interface ProcessLike { getBuiltinModule?(id: string): unknown; }

async function nodeFs(): Promise<FsPromisesLike> {
  const proc = (globalThis as { process?: ProcessLike }).process;
  if (proc?.getBuiltinModule) return proc.getBuiltinModule('node:fs/promises') as FsPromisesLike;
  const spec = 'node:fs/promises';   // a variable, so a bundler does not resolve it
  return (await import(spec)) as FsPromisesLike;
}
```

`tsc -p tsconfig.src.json` now prints nothing.

- [ ] **Step 6: Edit `js/package.json`**: `"engines": { "node": ">=22" }`; `"build:ts": "tsc -p tsconfig.src.json && tsc -p tsconfig.json"`.

- [ ] **Step 7: Edit `.github/workflows/ci.yml`**: the `wasm` job gets

```yaml
    strategy:
      matrix:
        node: [22, 24]
```

and `node-version: ${{ matrix.node }}`; everything else in the job stays.

- [ ] **Step 8: Rewrite `.github/workflows/publish.yml`** to this text. Every `run:` is a block scalar, so no plain scalar can hold `: ` (the plan 2 pitfall):

```yaml
# Publishes the npm package when a tag such as v0.1.0 is pushed.
# The first version is published by hand from a developer machine. After
# that, Massimo registers this workflow as the package's trusted publisher
# on npmjs.com (package settings, GitHub Actions, repository
# caerbannogwhite/rtf-atto, workflow publish.yml). From then on a tag
# publishes with provenance and no token. A tag whose version is already on
# npm publishes nothing and the run stays green.
name: publish

on:
  push:
    tags: ['v*']

permissions:
  contents: read
  id-token: write

jobs:
  publish:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v5
      - uses: actions/setup-node@v5
        with:
          node-version: 24
          registry-url: https://registry.npmjs.org
          cache: npm
          cache-dependency-path: js/package-lock.json
      - name: Check that the tag matches the package version
        run: |
          node -e "const v='v'+require('./js/package.json').version; const t=process.env.GITHUB_REF_NAME; console.log('package: '+v+', tag: '+t); if(v!==t) process.exit(1)"
      - name: Check whether this version is already on npm
        id: check
        run: |
          v=$(node -p "require('./js/package.json').version")
          if npm view "rtf-atto@$v" version > /dev/null 2>&1; then
            echo "rtf-atto@$v is already on npm; nothing to publish"
            echo "published=true" >> "$GITHUB_OUTPUT"
          else
            echo "published=false" >> "$GITHUB_OUTPUT"
          fi
      - uses: mymindstorm/setup-emsdk@v14
        if: steps.check.outputs.published != 'true'
        with:
          version: 6.0.13
      - name: Update npm, because trusted publishing needs 11.5.1 or later
        if: steps.check.outputs.published != 'true'
        run: |
          npm install -g npm@latest
          npm --version
      - if: steps.check.outputs.published != 'true'
        working-directory: js
        run: |
          npm ci
          npm run build
          npm test
      - if: steps.check.outputs.published != 'true'
        working-directory: js
        run: |
          npm publish --provenance --access public
```

- [ ] **Step 9: Write `.github/dependabot.yml`**:

```yaml
version: 2
updates:
  - package-ecosystem: github-actions
    directory: /
    schedule:
      interval: weekly
  - package-ecosystem: npm
    directory: /js
    schedule:
      interval: weekly
    open-pull-requests-limit: 3
```

- [ ] **Step 10: Edit the READMEs and `.gitattributes`.** `js/README.md:11` becomes `It runs on Node 22 or later and in current browsers.`; lines 135 and 136 become one line, `The package and its tests need Node 22 or later.` In `README.md`, add before "## Status":

```markdown
## Releasing

The version lives in `js/package.json`, in `include/rtf/version.hpp` and in the
`rtf_version()` string, and a test pins the three to the same text. A release
is a plain tag `vX.Y.Z` on `main`. The codename goes in the title of the
GitHub release, not in the version, because npm reads a suffix such as
`-mortician` as a pre-release that `^` ranges skip. The first version of the
package is published by hand from `js` with `npm publish`. After that the
`publish` workflow publishes every tag with provenance through npm's trusted
publishing. A tag whose version is already on npm publishes nothing.
```

Append `*.html text eol=lf` and `*.css text eol=lf` to `.gitattributes`.

- [ ] **Step 11: Run `npm run build && npm test`; expected: PASS**, 16 tests. Run `npm pack --dry-run` and check the file list is unchanged (14 files).

- [ ] **Step 12: Commit**: `git add .gitattributes .github/dependabot.yml .github/workflows/ci.yml .github/workflows/publish.yml README.md js/package.json js/tsconfig.src.json js/src/wasm-loader.ts js/README.md js/test/package.test.ts js/test/helpers.ts && git commit -m "Raise the Node floor to 22, check src without the Node types and publish through npm's trusted publishing"`.

---

### Task 2: The dead-instance test

**Files:**
- Create: `js/src/create.ts`, `js/test/instance.test.ts`
- Modify: `js/src/index.ts`

**Interfaces:**
- Consumes: `createRtfModule`, `RtfWasmModule`, `RtfWasmModuleOptions` from `./rtf.js`; `readWasm` from `./wasm-loader.js`; `RtfError`.
- Produces, in `js/src/create.ts`:

```ts
export type ModuleFactory = (options: RtfWasmModuleOptions) => Promise<RtfWasmModule>;
export const DEAD_MESSAGE = 'the wasm instance aborted; call load() again';
// Replaces the Emscripten factory; undefined restores it. For tests.
export function useModuleFactory(factory?: ModuleFactory): void;
// Builds one instance over the current factory. onDeath runs once when the
// instance dies.
export function createInstance(source: LoadOptions['wasm'], onDeath: () => void): Promise<RtfAtto>;
```

`index.ts` keeps `LoadOptions`, `RtfAtto`, `load()` and the re-exports; `Instance`, `Life`, `codeName`, `toBytes` and `create()` move to `create.ts` as `createInstance`. `load()` calls `createInstance` exactly where it called `create`.

- [ ] **Step 1: Write the failing tests** in `js/test/instance.test.ts`:

```ts
import createRtfModule from '../src/rtf.js';
import type { RtfWasmModuleOptions } from '../src/rtf.js';
import { DEAD_MESSAGE, useModuleFactory } from '../src/create.js';
import { load, RtfError } from '../src/index.js';
import { packageJson, readFixture, readGolden } from './helpers.js';

test('a_dead_instance_throws_and_the_shared_load_is_replaced', async () => {
  let captured: RtfWasmModuleOptions | undefined;
  useModuleFactory((options) => { captured = options; return createRtfModule(options); });
  try {
    const a = await load();
    assert.ok(captured?.onAbort);
    captured.onAbort('test abort');
    captured.onAbort('a second call is harmless');
    const isDead = (e: unknown) => e instanceof RtfError && e.code === 'wasm_aborted' && e.message === DEAD_MESSAGE;
    assert.throws(() => a.parse(readFixture('word_minimal')), isDead);
    assert.throws(() => a.parseToJson(readFixture('word_minimal')), isDead);
    assert.equal(a.version, packageJson().version);
    const b = await load();
    assert.notEqual(a, b);
    assert.equal(b.parseToJson(readFixture('word_minimal')), readGolden('word_minimal', '.json'));
  } finally {
    useModuleFactory(undefined);
  }
});

test('a_trap_during_a_call_kills_the_instance', async () => {
  useModuleFactory(async (options) => {
    const real = await createRtfModule(options);
    const broken = Object.create(real);   // the prototype keeps the live HEAPU8 and HEAPU32
    broken._rtf_parse = () => { throw new WebAssembly.RuntimeError('unreachable'); };
    return broken;
  });
  try {
    const rtf = await load({});
    assert.throws(() => rtf.parse(readFixture('word_minimal')),
      (e: unknown) => e instanceof RtfError && e.code === 'wasm_aborted' && e.cause instanceof WebAssembly.RuntimeError);
    assert.throws(() => rtf.parse(readFixture('word_minimal')),
      (e: unknown) => e instanceof RtfError && e.message === DEAD_MESSAGE);
  } finally {
    useModuleFactory(undefined);
  }
});
```

- [ ] **Step 2: Run `npm run build`; expected: FAIL**, `../src/create.js` not found.

- [ ] **Step 3: Create `js/src/create.ts`** by moving the code named above out of `index.ts`; add the module-level `let factory: ModuleFactory = createRtfModule;` that `useModuleFactory` sets and `createInstance` calls. `index.ts` imports `createInstance` from `./create.js` and keeps `import type` for `LoadOptions` cycles out of runtime.

- [ ] **Step 4: Run `npm run build && npm test`; expected: PASS**, 18 tests; `tsc -p tsconfig.src.json` still prints nothing.

- [ ] **Step 5: Commit**: `git add js/src/create.ts js/src/index.ts js/test/instance.test.ts && git commit -m "Move the instance into create.ts and test the dead-instance rule through an injected module factory"`.

---

### Task 3: The logical tables in TypeScript

**Files:**
- Create: `js/src/text.ts`, `js/src/tables.ts`, `js/test/builders.ts`, `js/test/tables.test.ts`
- Modify: `js/src/index.ts`, `js/src/create.ts` (the `Entry` type), `js/src/rtf.d.ts:7`, `wasm/CMakeLists.txt:26`, `js/test/goldens.test.ts`, `js/test/errors.test.ts:19,25,35`, `js/test/growth.test.ts:17`, `js/README.md` (quick start, API list, "Size and speed")

**Interfaces:**
- Consumes: the model types; `src/text.cpp` and `src/tables.cpp` as the algorithm to port; the design's "Logical tables" rules.
- Produces:

```ts
// js/src/text.ts, a port of src/text.cpp
export function inlineText(inlines: Inline[]): string;              // TextRun its text; Tab "\t"; LineBreak and ColumnBreak "\n"; PageBreak "\f"; Field its result; Footnote, Shape and EmbeddedObject their blocks; Bookmark and Picture nothing
export function blocksText(blocks: Block[], dropLastNewline: boolean): string;   // paragraphs: listText then runs then "\n" (not after the last when dropLastNewline); tables: every cell's blocksText(cell.blocks, true) + "\t", "\n" per row
export function cellText(cell: Cell): string;                        // blocksText(cell.blocks, true)

// js/src/tables.ts, a port of src/tables.cpp
export function logicalTables(document: RtfDocument): LogicalTable[];

// js/test/builders.ts
export const LETTER: PageSetup;   // 12240 x 15840, margins 1440, distances 720, landscape false
export function doc(body: Block[] | Partial<Section>[], extra?: Partial<RtfDocument>): RtfDocument;
// version 1, info {}, codePage 1252, defaultFont 0, fonts { 0: Times New Roman roman charset 0 pitch variable, 1: Courier New modern charset 0 pitch fixed },
// colors [null, { r: 255, g: 0, b: 0 }, { r: 0, g: 0, b: 255 }], styles [], page LETTER, warnings [];
// a Block[] becomes one section { page: LETTER, break: 'page', headers: {}, footers: {}, body }.
export function run(text: string, props?: CharProps): TextRun;
export function para(runs: Array<Inline | string>, props?: ParagraphProps): Paragraph;   // a string is run(text)
export function cell(right: number, blocks: Array<Block | string>, extra?: Partial<Cell>): Cell;   // a string is para([text]); width is filled by row()
export function row(cells: Cell[], props?: RowProps): Row;   // sets each cell's width from the previous right (or props.left ?? 0)
export function table(rows: Row[]): Table;
```

`index.ts` adds `export { logicalTables } from './tables.js';` and removes the two byte-taking methods from `RtfAtto`. `create.ts` narrows `Entry` to `'_rtf_parse'`.

- [ ] **Step 1: Write the failing tests** in `js/test/tables.test.ts`:

```ts
test('logical_tables_match_the_goldens', () => {
  for (const name of listFixtures()) {
    const golden = readGolden(name, '.json');
    if (golden === NOT_RTF) continue;
    const document = JSON.parse(golden) as RtfDocument;
    assert.deepEqual(logicalTables(document), JSON.parse(readGolden(name, '.tables.json')), name);
  }
});
test('logical_tables_of_a_wasm_document', async () => {
  const rtf = await load();
  const document = rtf.parse(readFixture('sas_table_3pages'));
  assert.deepEqual(logicalTables(document), JSON.parse(readGolden('sas_table_3pages', '.tables.json')));
});
test('a_part_without_headers_joins_on_subset_edges', ...);   // two sections: a table with header row [A|B] edges 3000,6000 and one data row; then a table with one row with edges 3000,6000 and no header -> one logical table, parts length 2, rows length 2; a third table whose first row has edge 4000 -> a second logical table
test('a_text_paragraph_between_parts_splits_them', ...);     // table, paragraph "x", same table -> two logical tables; table, empty paragraph, table -> one
test('column_names_prefix_and_dedupe', ...);                 // header rows [Group A spanning 2 columns][B] over [x|y|z] -> ['Group A x', 'Group A y', 'z']; two last-row cells both 'n' -> ['n', 'n_2']; an empty name -> 'column_3'
test('blank_cells_make_a_spacer_row', ...);                  // a row whose cells hold '' and '\u00a0' -> spacer true; a row with 'a' -> spacer false
test('indent_is_the_first_paragraphs_indent_left', ...);     // first cell paragraph with indentLeft 288 -> indent 288; no paragraph -> 0
```

The unit cases build documents with `builders.ts`.

- [ ] **Step 2: Run `npm run build`; expected: FAIL**, `../src/tables.js` not found.

- [ ] **Step 3: Write `text.ts` and `tables.ts`**, function by function from the C++: `isAsciiSpace`, `trim` (ASCII whitespace only, not `String.prototype.trim`, which also strips U+00A0 and more), `isBlank` (ASCII whitespace and U+00A0), `isGapParagraph`, `rowLeft`, `headerRowsOf`, `dataRights`, `sameHeaderRows`, `continues`, `edgeIndex` (0 when missing), `coveredColumns`, `headerName`, `columnNames`, `logicalRow`, `buildTable`, `logicalTables`. `id` starts at 1. `headerRows` are the first part's header `Row` objects themselves. Sorting edges is numeric. Column names dedupe with a `Set`.

- [ ] **Step 4: Run `npm run build && npm test`; expected: `tables.test.ts` passes**, the other suites still pass.

- [ ] **Step 5: Remove the byte-taking methods.** `RtfAtto` keeps `version`, `parse`, `parseToJson`. `rtf.d.ts` loses `_rtf_logical_tables`. `wasm/CMakeLists.txt:26` becomes `-sEXPORTED_FUNCTIONS=_rtf_parse,_rtf_free,_rtf_version,_malloc,_free`. Update the tests: `goldens.test.ts` asserts `assert.deepEqual(logicalTables(rtf.parse(bytes)), JSON.parse(tablesGolden), name)` for fixtures that parse and only `rtf.parse` throwing for the two that do not; `errors.test.ts` drops lines 19, 25 and 35; `growth.test.ts:17` becomes `const tables = logicalTables(rtf.parse(rtfBytes(parts.join(''))));`.

- [ ] **Step 6: Run `npm run build && npm test`; expected: PASS.** The wasm is rebuilt because the flags changed; note its new size from the `wasm_is_under_the_ceiling` diagnostic.

- [ ] **Step 7: Update `js/README.md`**: the quick start imports `logicalTables` and calls `logicalTables(doc)`; the API list replaces the two method lines with `` `logicalTables(doc: RtfDocument): LogicalTable[]` returns the logical tables of a document. It is a function over the model and needs no wasm. It applies the same rules as the C++ core, and the tests pin it to the core's output for every fixture. ``; "Size and speed" gets the measured local wasm size (the CI size is filled in after the push).

- [ ] **Step 8: Commit**: `git add js/src/text.ts js/src/tables.ts js/src/index.ts js/src/create.ts js/src/rtf.d.ts wasm/CMakeLists.txt js/test/builders.ts js/test/tables.test.ts js/test/goldens.test.ts js/test/errors.test.ts js/test/growth.test.ts js/README.md && git commit -m "Port the logical tables to TypeScript so that nothing parses the bytes twice"`.

---

### Task 4: The renderer: pages, paragraphs and runs

**Files:**
- Create: `js/src/render/options.ts`, `html.ts`, `css.ts`, `pages.ts`, `inlines.ts`, `blocks.ts`, `render.ts`; `js/test/html-helpers.ts`; `js/test/render-pages.test.ts`; `js/test/render-text.test.ts`
- Modify: `js/src/index.ts` (exports), `js/test/builders.ts` (header and footer helpers if needed)

**Interfaces:**
- Consumes: the model types; `builders.ts`.
- Produces (public):

```ts
export interface RenderOptions {
  pageNumbers?: 'sections' | 'none';   // default 'sections'
  fonts?: Record<string, string>;      // RTF font name -> CSS font-family, inserted as given (trusted)
  pxPerTwip?: number;                  // default 1 / 15
  pictures?: boolean;                  // default true
  classPrefix?: string;                // default 'rtf-'
}
export function renderHtml(doc: RtfDocument, options?: RenderOptions): string;
```

and in `js/test/html-helpers.ts`: `stripTags(html: string): string` (removes every tag, decodes `&amp;` `&lt;` `&gt;` `&quot;` `&#39;` and numeric references) and `count(html: string, needle: string): number`.

The output shape, which the tests and Task 6's goldens pin:

```html
<section class="rtf-page" data-page="1" data-section="0" style="width: 816px; min-height: 1056px; padding-left: 96px; padding-right: 96px; font-family: &quot;Times New Roman&quot;, serif; font-size: 16px">
<div class="rtf-header" style="min-height: 96px; padding-top: 48px">
...header blocks...
</div>
<div class="rtf-body">
...body blocks...
</div>
<div class="rtf-footer" style="min-height: 96px; padding-bottom: 48px">
...footer blocks...
</div>
</section>
```

`rtf-landscape` is added to the page's class when `page.landscape`. An absent header or footer gives an empty `div` with the same style. The numbers above are Letter at 1/15 px per twip.

**Rules** (spec, with the exact values this plan fixes):

- **Pagination.** Walk the sections. A section with `break` other than `'none'` starts a new page; the first section always does. Inside a section, a paragraph with `pageBreakBefore` starts a new page before it, and a `pageBreak` inline ends the current page: the inlines before it close the paragraph on the old page and the inlines after it open a paragraph with the same `props` on the new page (no paragraph is emitted for an empty side). A table never splits. Page numbers follow decision 3. Header choice per page: `first` when `titlePage` and the page is the section's first; else `left` on even and `right` on odd page numbers when the document's `page.facingPages` is true; else `default`, else `right`, else `left`; footers the same way.
- **Paragraph.** `<p class="rtf-p" style="...">`: `text-align` for `align` other than left; `margin-left` for `indentLeft`, `margin-right` for `indentRight`, `text-indent` for `indentFirst`; `margin-top` for `spaceBefore`, `margin-bottom` for `spaceAfter`; `line-height: {lineSpacing / 240}` when `lineSpacingMultiple`, else `line-height: px(|lineSpacing|)` when `lineSpacing` is set and not 0; `direction: rtl` when `rtl`; paragraph `borders` as `border-top` and so on with `borderCss`; `shading.background` as `background-color`. `listText` inlines come first, with U+F0B7 shown as U+2022. A paragraph that renders no inline markup gets `<br>` as its content.
- **Run.** `<span style="...">` with `font-family` (Task 4's `fontFamily`), `font-size: px(size * 10)`, `font-weight: bold`, `font-style: italic`, `text-decoration-line` with `underline` and `line-through` (strike or doubleStrike) joined by a space, `text-decoration-style` double, dotted, dashed or wavy for `double`, `dotted`, `dash`, `wave`; `color` and `background-color` (`highlight`, else `background`) from `colorOf`; `font-variant: small-caps`; `text-transform: uppercase` for `caps`; `vertical-align: px(offset * 10)` for `offset`. `script: 'super'` wraps the span in `<sup>`, `'sub'` in `<sub>`. A run with no style is a bare text node. `hidden` runs are omitted. The text is escaped; U+00A0 stays a character; `.rtf-p` has `white-space: pre-wrap` in `rtf.css`, so spaces survive.
- **Other inlines in this task.** `lineBreak` and `columnBreak` are `<br>`; `tab` follows decision 14; `bookmark` with `position: 'start'` is `<a id="rtf-bm-{encodeURIComponent(name)}"></a>`, `'end'` is nothing; a `field` renders its result (Task 6 adds the page fields); `footnote`, `picture`, `shape` and `object` render nothing until Task 6.
- **Tables** in this task: `<table class="rtf-table">` with one `<tr>` per row and one `<td>` per cell holding the cell's blocks, no spans and no styles. Task 5 replaces this.
- **CSS helpers.** `fmt(n)` is `String(Math.round(n * 100) / 100)`; `px(twips)` is `fmt(twips * pxPerTwip) + 'px'`. `colorOf(index)` is `rgb(r, g, b)` for an entry, undefined for `null`, an absent index or one out of range. `cleanFontName` keeps letters, digits, spaces and hyphens, collapses runs of spaces, trims. `fontFamily(index)`: the override from `options.fonts` by the font's name if present; else `"{cleaned}", {generic}` where generic is `monospace` for `modern`, `sans-serif` for `swiss`, `serif` for `roman`, else `sans-serif`; undefined for an unknown font number. `borderCss(border)`: `none` for style `none`; else `{max(1, round(px(width)))}px {style} {color}` with `solid` for single, thick, hairline and other, `double` for double and triple, `dotted`, `dashed`; the colour from `colorOf`, omitted when automatic.
- **Page base.** The page's inline style ends with `font-family: {fontFamily(defaultFont)}; font-size: 16px` at the default scale (24 half-points).

- [ ] **Step 1: Write the failing tests**, through `renderHtml` on `builders.ts` documents.

`render-pages.test.ts`:

```ts
test('each_page_break_section_is_a_page', ...);      // three sections -> count(html, '<section class="rtf-page"') === 3; data-page 1..3
test('a_page_break_inline_splits_a_paragraph', ...);  // para(['A', { kind: 'pageBreak' }, 'B'], { align: 'center' }) -> 2 pages; page 1 holds <p class="rtf-p" style="text-align: center">A</p>, page 2 the same with B
test('page_break_before_starts_a_page', ...);
test('break_none_continues_the_page', ...);           // sections [page, none] -> 1 page holding both bodies
test('headers_follow_the_rules', ...);                // default on every page; only right -> right; titlePage + first -> first on page 1, default on page 2; facingPages with left/right -> left on page 2, right on page 1
test('page_areas_keep_the_margins', ...);             // no headers: the header div has style="min-height: 96px; padding-top: 48px", the footer "min-height: 96px; padding-bottom: 48px"; the page has width: 816px; min-height: 1056px
test('landscape_uses_the_given_width_and_height', ...);  // page 15840 x 12240 landscape -> class "rtf-page rtf-landscape", width: 1056px
test('page_numbers_count_pages', ...);                // sections [page, page with pageNumberStart 5] -> data-page 1 then 5; a pageBreak inside the first section -> 1, 2, 5
```

`render-text.test.ts`:

```ts
test('paragraph_styles_from_props', ...);     // align center, indentLeft 720, indentFirst -360, spaceBefore 120, lineSpacing 360 multiple -> 'text-align: center; margin-left: 48px; text-indent: -24px; margin-top: 8px; line-height: 1.5'
test('empty_paragraph_keeps_a_line', ...);    // para([]) -> '<p class="rtf-p"><br></p>'
test('run_styles_from_props', ...);           // bold, italic, underline double, strike, color 1, highlight 2, size 20, font 1 -> font-family: &quot;Courier New&quot;, monospace; font-size: 13.33px; font-weight: bold; font-style: italic; text-decoration-line: underline line-through; text-decoration-style: double; color: rgb(255, 0, 0); background-color: rgb(0, 0, 255)
test('superscript_and_small_caps', ...);      // script super -> <sup>; smallCaps -> font-variant: small-caps
test('hidden_text_is_omitted', ...);
test('html_escapes_text_and_font_names', ...);   // text '<script>alert(1)</script>' -> '&lt;script&gt;alert(1)&lt;/script&gt;'; a font named '"Arial"; } body { color: red' -> 'font-family: &quot;Arial body color red&quot;, sans-serif'; no '<script' in the output
test('tabs_take_the_stop_widths', ...);          // tabs at 1440 and 2880, three tabs -> widths 96px, 96px, 48px
test('list_text_comes_first_and_the_symbol_bullet_shows_as_a_bullet', ...);   // listText [run('\uf0b7'), tab] runs ['x'] -> '\u2022' before x
test('nbsp_is_kept', ...);                       // text 'a\u00a0b' -> contains 'a\u00a0b'
test('fonts_option_overrides_a_name', ...);      // fonts: { 'Courier New': 'Menlo, monospace' } -> font-family: Menlo, monospace
test('class_prefix_renames_the_classes', ...);   // classPrefix 'x-' -> 'x-page', 'x-p', no 'rtf-'
test('output_is_deterministic', ...);            // two renders of word_structure's golden document are equal
```

- [ ] **Step 2: Run `npm run build`; expected: FAIL**, `renderHtml` is not exported.

- [ ] **Step 3: Write the render modules** per the rules and the output shape. Module boundaries are the implementer's; the names in "File structure" are the suggestion. `index.ts` adds `export { renderHtml } from './render/render.js'; export type { RenderOptions } from './render/options.js';`.

- [ ] **Step 4: Run `npm run build && npm test`; expected: PASS**; `tsc -p tsconfig.src.json` prints nothing.

- [ ] **Step 5: Commit**: `git add js/src/render js/src/index.ts js/test/builders.ts js/test/html-helpers.ts js/test/render-pages.test.ts js/test/render-text.test.ts && git commit -m "Add the renderer's pages, paragraphs and runs"`.

---

### Task 5: Tables

**Files:**
- Create: `js/src/render/tables.ts`, `js/test/render-tables.test.ts`
- Modify: `js/src/render/blocks.ts` (delegates `Table` blocks)

**Interfaces:**
- Consumes: Task 4's helpers and context; `builders.ts` (`cell`, `row`, `table`).
- Produces: `renderTable(table: Table, ctx): string`, called by the block renderer; no public change.

**Rules:**

- **Grid.** `edges` is the sorted set of every row's `props.left ?? 0` and every cell's `right`. `<table class="rtf-table" style="width: px(last - first); table-layout: fixed; border-collapse: collapse; ...">` with `<colgroup>` holding one `<col style="width: px(e[i+1] - e[i])">` per adjacent pair. Alignment from the first row: `align: 'center'` adds `margin-left: auto; margin-right: auto`, `'right'` adds `margin-left: auto`, otherwise `margin-left: px(first)` when `first` is not 0 (Word's `trleft -108` gives `-7.2px`).
- **Rows.** Leading rows with `header: true` go in `<thead>` as `<th>`; the rest in `<tbody>` as `<td>`. `<tr style="height: px(|height|)">` when `height` is set and not 0.
- **Cells.** Each cell covers the grid columns from the index of its left (the previous cell's `right`, or the row left) to the index of its `right`; `colspan` when more than 1. A `mergeHorizontal: 'continued'` cell is folded into the previous emitted cell of the row, whose `colspan` grows; with no previous cell it is emitted as a normal cell. A `mergeVertical: 'first'` cell gets `rowspan` 1 plus the number of consecutive following rows that hold a `mergeVertical: 'continued'` cell with the same left and right; those continued cells are not emitted. A `continued` cell with no matching `first` above is emitted as a normal cell.
- **Cell style.** `vertical-align: middle` or `bottom` (top is the default); `padding` per decision 13 as `padding: t r b l`; `background-color` from `shading.background`; `border-top` and so on from the cell's `borders[side] ?? row.props.borders?.[side]` through `borderCss`; `white-space: nowrap` for `noWrap`. The cell's content is its blocks; an empty block list renders `<p class="rtf-p"><br></p>`. A nested `Table` block renders recursively.

- [ ] **Step 1: Write the failing tests** in `render-tables.test.ts`:

```ts
test('grid_from_the_union_of_edges', ...);       // rows with rights [3000, 6000] and [2000, 6000] -> colgroup widths 133.33px, 66.67px, 200px; first row cells colspan 2 then 1; second row 1 then 2
test('header_rows_in_thead_as_th', ...);          // two header rows then one data row -> one <thead> with 2 <tr>, <th> cells, <tbody> with 1 <tr>
test('horizontal_merge_extends_colspan', ...);    // cells first(3000) continued(6000) x(9000) -> one <td colspan="2"> then <td>, no cell for the continued one
test('vertical_merge_sets_rowspan_and_skips_continued', ...);   // rows: [first(3000), b(6000)], [continued(3000), d(6000)] -> rowspan="2" on the first cell, the second row has one <td>
test('malformed_merges_render_as_plain_cells', ...);   // a continued vertical cell with nothing above; a continued horizontal cell first in the row -> plain <td>, no rowspan/colspan attribute
test('cell_styles', ...);   // shading background 1 -> background-color: rgb(255, 0, 0); verticalAlign bottom; no padding -> padding: 0px 7.2px 0px 7.2px; row gapHalf 60 -> 4px; border single width 10 -> border-top: 1px solid; border double 30 colour 2 -> 2px double rgb(0, 0, 255); noWrap
test('row_height_and_alignment', ...);   // height 300 -> <tr style="height: 20px">; align center -> margin-left: auto; margin-right: auto; left -108 -> margin-left: -7.2px
test('nested_table_renders_inside_the_cell', ...);
test('word_tables_fixture_renders_every_cell', ...);   // JSON.parse(readGolden('word_tables', '.json')): no throw; stripTags(html) contains the text of every cell (through cellText from Task 3) except cells with mergeVertical 'continued'
```

- [ ] **Step 2: Run `npm run build && npm test`; expected: FAIL** on the new assertions.

- [ ] **Step 3: Write `render/tables.ts`** and route `Table` blocks to it.

- [ ] **Step 4: Run `npm run build && npm test`; expected: PASS.**

- [ ] **Step 5: Commit**: `git add js/src/render js/test/render-tables.test.ts && git commit -m "Render tables with the grid, the spans and the cell styles"`.

---

### Task 6: Fields, footnotes, pictures, shapes, objects, rtf.css, mount and the HTML goldens

**Files:**
- Create: `js/src/mount.ts`, `js/src/rtf.css`, `js/scripts/copy-assets.mjs`, `js/scripts/regen-html-goldens.mjs`, `js/test/goldens/*.html` (17), `js/test/render-inlines.test.ts`, `js/test/render-goldens.test.ts`
- Modify: `js/src/render/inlines.ts`, `js/src/render/pages.ts`, `js/src/index.ts`, `js/package.json`, `js/test/package.test.ts`, `js/README.md`

**Interfaces:**
- Produces (public):

```ts
export interface MountResult { pages: Element[]; }
// Sets container.innerHTML to renderHtml(doc, options) and returns the page elements.
export function mount(doc: RtfDocument, container: Element, options?: RenderOptions): MountResult;
```

`package.json`: `"exports"` gains `"./rtf.css": "./dist/src/rtf.css"`; `build:ts` ends with `&& node scripts/copy-assets.mjs`; a script `"regen:html": "node scripts/regen-html-goldens.mjs"`.

**Rules:**

- **Page fields.** A field whose `instruction.trim()` starts with `PAGE` or `NUMPAGES` as a whole word, case-insensitively (Word writes `PAGE \* MERGEFORMAT`), with an empty `result` and `pageNumbers: 'sections'`, renders the page number or the page count as escaped text with no span. Every other case renders the result.
- **Footnotes.** The reference is `<sup class="rtf-footnote-ref">n</sup>`; `n` counts footnotes and endnotes across the document from 1. At the end of the page's body: `<div class="rtf-footnotes">` holding, per note, `<div class="rtf-footnote"><sup>n</sup>` + the note's blocks + `</div>`. No div when the page has no notes.
- **Pictures.** Decision 11. `<img class="rtf-picture" src="data:image/png;base64,..." alt="" style="width: ...px; height: ...px">` (mime `image/jpeg` for `jpeg`), the style omitted when no size is known. The box: `<span class="rtf-picture-box" style="display: inline-block; width: ...px; height: ...px">EMF</span>` with the format name upper-cased, the size the same way, `100px` by `100px` when unknown. `pictures: false` renders nothing, not even the box.
- **Shapes and objects.** `<div class="rtf-shape">` with the blocks then the picture; `<div class="rtf-object">` with the result blocks.
- **`rtf.css`**, exact text:

```css
/* The page frame of rtf-atto's renderer. Everything that comes from the
   document is an inline style; this file holds only the frame. */
.rtf-page { box-sizing: border-box; display: flex; flex-direction: column; margin: 16px auto; background: #fff; color: #000; box-shadow: 0 0 6px rgba(0, 0, 0, 0.3); }
.rtf-header, .rtf-footer { box-sizing: border-box; }
.rtf-footer { display: flex; flex-direction: column; justify-content: flex-end; }
.rtf-body { flex: 1 0 auto; }
.rtf-p { margin: 0; white-space: pre-wrap; }
.rtf-table { border-collapse: collapse; }
.rtf-table td, .rtf-table th { padding: 0; vertical-align: top; font-weight: inherit; text-align: inherit; }
.rtf-footnotes { border-top: 1px solid #000; margin-top: 8px; padding-top: 4px; }
.rtf-picture-box { border: 1px dashed #888; font: 10px sans-serif; text-align: center; vertical-align: middle; }
@media print { .rtf-page { margin: 0; box-shadow: none; break-after: page; } }
```

- **Goldens.** `regen-html-goldens.mjs` imports `../dist/src/index.js`, calls `load()`, and for every fixture whose `.json` golden is not `{"error": "not_rtf"}` writes `renderHtml(rtf.parse(bytes))` to `js/test/goldens/<name>.html` with LF line ends; it prints the count. The test compares strings and, on a mismatch, names the fixture.
- **Text coverage** (spec, validation item 6): with `stripTags`, (a) every non-hidden `TextRun` text with non-blank content appears in the stripped HTML, skipping runs inside `mergeVertical: 'continued'` cells; (b) for each section's body, the first such text of each paragraph, in document order with table cells in row order, is found at a position no earlier than the previous one.

- [ ] **Step 1: Write the failing tests.**

`render-inlines.test.ts`:

```ts
test('page_fields_follow_the_option', ...);   // footer para [field PAGE, ' of ', field NUMPAGES]; two sections with a pageBreak in the first -> footers '1 of 3', '2 of 3', '3 of 3'; pageNumberStart 7 on section 2 -> '7 of 3'; a PAGE field with result 'IX' -> 'IX'; pageNumbers 'none' with empty results -> ' of '; instruction ' PAGE \\* MERGEFORMAT ' counts as PAGE
test('footnotes_are_listed_at_the_bottom_of_the_page', ...);   // two footnotes on page 1, one on page 2 -> refs 1, 2 then 3; each page's rtf-footnotes holds its notes' text
test('pictures_become_data_urls', ...);   // pictures fixture's 1x1 PNG (96 base64 chars, goal 15 x 15) -> <img ... src="data:image/png;base64,..." style="width: 1px; height: 1px">; a jpeg -> image/jpeg
test('pictures_outside_the_rules_become_boxes', ...);   // data 'not base64!' -> box; 4,194,305 'A's -> box; emf 100 x 100 -> '<span class="rtf-picture-box" style="display: inline-block; width: 100px; height: 100px">EMF</span>'; pictures: false -> no img and no box
test('shapes_and_objects_render_their_blocks', ...);
test('bookmarks_are_anchors', ...);   // name 'a b' -> '<a id="rtf-bm-a%20b"></a>'; end -> nothing
test('hyperlink_fields_render_their_result_only', ...);   // no '<a href'
test('mount_sets_inner_html_and_returns_pages', ...);   // a fake container { innerHTML: '', querySelectorAll: () => [p1, p2] } cast to Element -> innerHTML equals renderHtml(doc), pages.length 2
```

`render-goldens.test.ts`:

```ts
test('every_fixture_matches_its_html_golden', async () => { ... });   // 17 goldens; assert.equal(renderHtml(rtf.parse(bytes)), golden, name)
test('html_contains_every_text_run_in_order', async () => { ... });  // the coverage rule above, over the 17 fixtures
```

`package.test.ts`: `exports_point_at_existing_files` now also covers `./rtf.css` (no change in code, the new export is checked by the existing loop); add `test('rtf_css_is_shipped', ...)` asserting `dist/src/rtf.css` exists and `npm pack --dry-run` lists it (spawn `npm pack --dry-run --json` and check the files array).

- [ ] **Step 2: Run `npm run build && npm test`; expected: FAIL** (no goldens, no `mount`).

- [ ] **Step 3: Implement the rules**, `mount.ts`, `rtf.css`, `copy-assets.mjs` (copies `src/rtf.css` to `dist/src/rtf.css`), the `package.json` changes, and `index.ts` exports `mount`, `MountResult`.

- [ ] **Step 4: Run `npm run build && npm run regen:html`; expected: 17 files written.** Read three of them (`sas_table_3pages`, `word_structure`, `pictures`) and check by eye: three pages with the header table and `Page 1 of 3` in the footer; the first-page header and the footnote list; the 1x1 PNG as an `img` and the EMF box.

- [ ] **Step 5: Run `npm test`; expected: PASS.** Measure and note: the render time of `sas_table_3pages` (one `renderHtml` call after one warm-up call, `performance.now()`), the size of its HTML.

- [ ] **Step 6: Update `js/README.md`**: lines 6 to 9 say the package returns the model, the logical tables and an HTML rendering; a section "Rendering" after "The model" with the API (`renderHtml`, `mount`, `RenderOptions` fields with defaults, `import 'rtf-atto/rtf.css'`), the page model (one page per section with a page break, no re-flow, pages grow), the page-field rule reworded to pages (decision 3), the untrusted-input promises, the list of what is not rendered (decision 10 and the non-PNG/JPEG formats), and the two measured numbers from Step 5.

- [ ] **Step 7: Commit in two parts**: `git add js/src js/scripts/copy-assets.mjs js/scripts/regen-html-goldens.mjs js/package.json js/test/render-inlines.test.ts js/test/render-goldens.test.ts js/test/package.test.ts js/README.md && git commit -m "Render fields, footnotes, pictures, shapes and objects, ship rtf.css and add mount()"`, then `git add js/test/goldens && git commit -m "Add the HTML goldens of the fixtures"`.

---

### Task 7: The demo page and its server

**Files:**
- Create: `js/scripts/serve.mjs`, `js/demo/index.html`, `js/demo/demo.js`, `js/demo/demo.css`, `js/test/serve.test.ts`
- Modify: `js/package.json` (script `"demo": "node scripts/serve.mjs"`), `README.md` ("npm package" and "Status"), `js/README.md` ("Demo")

**Interfaces:**
- Produces, in `js/scripts/serve.mjs`:

```js
// Returns an http.Server over the repository root. GET only. Paths are
// resolved under root and anything else is 404. No directory listing.
// GET /api/fixtures answers the sorted .rtf names of test/fixtures as JSON.
// Every response carries Cache-Control: no-store.
export function createDemoServer(root);
// When run as a script: listens on 127.0.0.1 and PORT or 8787, prints the URL of js/demo/.
```

Content types: `.html` text/html, `.js` and `.mjs` text/javascript, `.css` text/css, `.json` application/json, `.wasm` application/wasm, `.rtf` application/rtf, else application/octet-stream; every text type with `; charset=utf-8`.

**The page.** `index.html` loads `../dist/src/rtf.css` and `demo.css`, and `demo.js` as a module. `demo.js` imports `load`, `renderHtml`, `logicalTables` from `../dist/src/index.js`. Controls: a `<select>` filled from `/api/fixtures`, an `<input type="file" multiple accept=".rtf">`, checkboxes for page numbers and pictures, a zoom `<select>` (50, 75, 100, 125, 150 percent, which sets `pxPerTwip` to the factor over 15), and a checkbox "show logical tables". A status line shows the core version, the parse time, the render time, the page count and the warning count; a `<details>` lists the warnings. The pages render into `<main id="pages">` through `renderHtml` and `innerHTML`. With the tables checkbox, each logical table renders below the pages as a plain `<table>` with the column names as the header and `spacer` rows in grey. Files from the file input are read with `FileReader` as an `ArrayBuffer`; nothing is sent anywhere. Errors show in the status line with the `RtfError` code.

- [ ] **Step 1: Write the failing test** `js/test/serve.test.ts`: import `createDemoServer` from `'../../scripts/serve.mjs'` (the compiled test sits in `dist/test`), start it on port 0 at the repository root, then with `fetch`: `/api/fixtures` is JSON including `'word_minimal'` and not including anything that is not an `.rtf` name; `/js/demo/index.html` is 200 with `text/html; charset=utf-8`; `/js/dist/src/rtf.wasm` is 200 with `application/wasm`; `/../package.json` and `/%2e%2e/package.json` are 404; `/js/demo/` is 200 (serves `index.html`); `/js/` is 404 (no listing); a `POST` is 405. Close the server. Test name `demo_server_serves_the_root_only`. Add a `// @ts-expect-error` free import by declaring the module in a small `js/test/serve.d.ts` if `tsc` needs it.

- [ ] **Step 2: Run `npm run build && npm test`; expected: FAIL**, module not found.

- [ ] **Step 3: Write `serve.mjs`, the demo files and the `demo` script.**

- [ ] **Step 4: Run `npm run build && npm test`; expected: PASS.** Then run `npm run demo`, open the printed URL in a browser, load `sas_table_3pages` and `msword_features`, and check: three pages with `Page 1 of 3`, the picture shows, no console errors, the wasm came over `fetch`. Close the server.

- [ ] **Step 5: Update the READMEs.** `README.md` "npm package": one sentence on the renderer and `npm run demo`; "Status": plan 3 complete, the writer and the DuckDB integration remain. `js/README.md`: a "Demo" section with the three commands (`npm install`, `npm run build`, `npm run demo`) and the sentence that files stay in the browser.

- [ ] **Step 6: Commit**: `git add js/scripts/serve.mjs js/demo js/test/serve.test.ts js/package.json README.md js/README.md && git commit -m "Add the demo page and the static server that serves it"`.

- [ ] **Step 7: Push the branch** (`git push -u origin plan-3-renderer`) so CI runs the wasm job on Node 22 and 24. Record the CI wasm size in `js/README.md` in the final review's fix wave.

**After the plan (Massimo, validation item 7):** open `npm run demo`, load the SAS fixtures and his own samples, and compare with Word: same page count, same rows per page, same titles and footnotes, `Page X of Y` right, borders and alignment as in Word. Differences become issues or a fix round; they do not block the plan's completion.

---

## Self-review notes

- Spec coverage: every rule of the design's "The renderer" section has a task: pages and headers (Task 4), fields (Task 6), paragraphs and runs (Task 4), tables (Task 5), fonts (Task 4), other inlines (Task 6), untrusted input (Tasks 4 and 6, review focus 1 and 5), stylesheet (Task 6), goldens (Task 6, validation 6), demo and Massimo's check (Task 7, validation 7). "Logical tables" is Task 3; the npm section's `renderHtml` and `mount` are Tasks 4 and 6. Massimo's seven decisions map to Tasks 1, 2, 3 and the constraints.
- Type consistency: `RenderOptions` and `renderHtml` appear in Tasks 4, 6 and 7 with the same signature; `logicalTables(document: RtfDocument)` in Tasks 3 and 7; `cellText` from Task 3 is used by Task 5's fixture test; `builders.ts` is created in Task 3 and used by Tasks 4 to 6.
- The two `not_rtf` fixtures have no HTML golden and `regen:html` skips them; the goldens test counts 17.
- Proportion: the plan fixes values and rules and leaves module layout and bodies to the implementer; the only code blocks are the exact copy the spec or this plan pins (the workflow, Dependabot, `tsconfig.src.json`, `rtf.css`, the loader's Node path) and the test skeletons.
