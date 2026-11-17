# RTF npm package implementation plan (rtf-atto, plan 2 of 5)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the reader of plan 1 as the npm package `rtf-atto`: a C ABI over the C++ core, an Emscripten build of it, a TypeScript wrapper with the model types, node tests that prove the wasm gives the same JSON as the native build for every fixture, and the CI and publishing workflows.

**Architecture:** `wasm/exports.cpp` wraps `rtf::parse`, `rtf::to_json` and `rtf::logical_tables` in four `extern "C"` functions that take a byte range and return a malloc'd UTF-8 buffer (JSON on success, a short message on failure). Emscripten compiles the core and that file to `rtf.js` (an ES module factory) plus `rtf.wasm`. The TypeScript wrapper in `js/src/index.ts` loads the wasm bytes itself (fetch in browsers, `fs` in Node), copies the input into wasm memory, calls the ABI, decodes the JSON and frees both buffers. `js/src/model.ts` holds the TypeScript shape of the document model; a compile-time check proves every committed golden satisfies it. Nothing in the C++ core changes.

**Tech Stack:** C++17 (unchanged core), CMake 3.20+, Emscripten (3.1.71 on Massimo's machine, 6.0.13 in CI), Node 24 in CI (26 on Massimo's machine), TypeScript 5.9, `node:test` and `node:assert`, GitHub Actions, npm with provenance.

**Spec:** `docs/plans/2026-11-10-rtf-parser-design.md`, sections "Constraints", "How it gets made" (Repositories, Toolchain), "The document model", "The npm package", "Validation" items 1 and 5, "Work order and size" item 2, and "Decisions already taken". The spec is the authority; where this plan and the spec disagree, the spec is right and the plan gets corrected. The spec leaves the ABI error codes, the exact Emscripten flags, the package layout and the test layout to the builder; this plan fixes them in "Decisions this plan takes".

## Global constraints

- The C++ core is not changed by this plan. `src/` and `include/` stay as plan 1 left them; the only new C++ is `wasm/exports.cpp` and a test. The goldens stay byte for byte as they are.
- C++17 and the standard library only, no exceptions, no RTTI, warnings as errors: the shared target `rtf_atto_flags` applies `-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror` under em++ as under GCC and Clang. A warning em++ raises is fixed in the code, never silenced by removing `-Werror`.
- The wasm build flags are exactly the list in Task 2. Changing a flag is a plan amendment.
- The npm package is ESM only (`"type": "module"`), has no runtime dependencies, and its only development dependencies are `typescript` and `@types/node`. No test framework, no bundler, no linter: `tsc`, `node --test` and `node:assert`.
- TypeScript strict mode. Relative imports carry the `.js` extension (`moduleResolution: NodeNext`).
- Runtime support is Node 20 or later and current browsers. Developing and testing needs Node 22 or later (the tests are selected with a glob).
- The wasm and the native build give the same bytes of canonical JSON for every fixture. The committed goldens are the authority; a test compares the wasm output with them, so a difference is a bug in this plan, not a reason to regenerate.
- The package version in `js/package.json`, `RTF_ATTO_VERSION` in `include/rtf/version.hpp` and the `rtf_version()` string are the same text (`0.1.0` now). A test pins it.
- Massimo's sample files are not used by this plan at all. The tests read only the committed fixtures in `test/fixtures`. Eleven of the SAS-made files in `C:\Users\massi\Downloads\RTF` are never copied into the repository, neither bytes nor text.
- Plain English in code comments, commit messages and documents (`~/.claude/CLAUDE.md`).
- Commit messages: one imperative sentence in plain English, no type prefix, an optional body, and at the end the `Co-Authored-By` line the harness gives. Commits go to the branch `plan-2-npm-package`, created from `main` at the start; the branch is pushed when Task 5 is complete so CI runs the new jobs (Massimo's approval of this plan is the consent for that push); the pull request and the merge are his decision at the end.

## Review focus

1. **Memory growth during a call.** `-sALLOW_MEMORY_GROWTH=1` replaces the wasm memory buffer when the parser needs more than the initial 16 MB, so a `HEAPU8` or `HEAPU32` view taken before the call is detached after it. Every read of a pointer or of the output after an ABI call must go through a fresh view. Test `growth_then_second_parse` in Task 4: a 20,000-row table, then `word_minimal.rtf` again.
2. **Deep nesting and the wasm stack.** The default Emscripten stack is 64 KB; `to_json` recurses once per nesting level and the reader accepts 1024 levels. Natively, 1000 nested fields, 1000 nested footnotes and 5000 nested groups parse with a 1 MB stack (probe of 2026-11-17). The build sets `-sSTACK_SIZE=4194304`; test `deep_nesting_parses` in Task 4 pins it with the same three inputs.
3. **The glue running in Node without `node` in `-sENVIRONMENT`.** The wrapper always passes `wasmBinary`, so the glue never has to locate or read a file; every node test exercises this path. If the first `node --test` run fails while importing `rtf.js` with an environment error, Task 2's fallback rule applies (add `node` to the list) and the plan is amended.
4. **Empty and non-RTF input, null pointers.** `rtf_parse(nullptr, 0, ...)` and `parse(new Uint8Array(0))` report `not_rtf` and nothing crashes; a null `out` pointer reports `invalid_argument`. Tests `not_rtf_and_empty_input` and `invalid_arguments` in Task 1, `empty_input_is_not_rtf` in Task 4.
5. **Model drift.** A field the reader writes that `model.ts` does not declare, a union member the types do not list, or a required field the types mark optional. Task 3's shape check compiles every golden against the types with `satisfies`, so an excess property, a missing property or a wrong literal fails the build.

## Repository and conventions

- Local clone: `C:\Users\massi\source\repos\rtf-atto`, remote `origin` = https://github.com/caerbannogwhite/rtf-atto.git (public; `main` has 31 commits and a green CI run as of 2026-11-17). Work happens on the branch `plan-2-npm-package`.
- Namespaces and naming as in plan 1: C++ in `rtf`, types PascalCase, functions snake_case, members camelCase equal to the JSON keys. TypeScript: types PascalCase, functions and variables camelCase, files kebab-case.
- C++ tests are plain executables over `test/cpp/check.hpp` (`CHECK`, `CHECK_EQ`), one `main` that calls the test functions in order, nothing printed on success. Node tests are `js/test/*.test.ts` compiled by `tsc` to `js/dist/test/*.test.js` and run with `node --test`; each file uses `test` from `node:test` and `assert` from `node:assert/strict`; test names are the behaviour they pin.
- Commands below are for Git Bash on Massimo's machine with forward slashes, run from the repository root unless a `cd` is written.
  - Native (MSVC): `cmake --build build --config Release -j 8 && ctest --test-dir build -C Release --output-on-failure`.
  - Native (GCC): `cmake --build build-gcc && ctest --test-dir build-gcc --output-on-failure`.
  - Wasm: `export PATH="/c/Users/massi/source/repos/emsdk/upstream/emscripten:$PATH"` once per shell, then `emcmake.bat cmake -S wasm -B build-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-wasm`. The emsdk there is 3.1.71 (`emcc.bat --version`); it is not on PATH by default and is not activated with `--permanent`. The `.bat` launchers need `python` on PATH, which the Store Python provides.
  - Package: `cd js && npm install` once, then `npm run build && npm test` (with the same PATH export, because `npm run build` runs the wasm build).
- Massimo disabled his antivirus on 2026-11-10 because it blocked freshly compiled executables. If a test executable fails with "Access is denied" right after linking, retry once before debugging.
- The ABI header `wasm/rtf_abi.h` is C: no C++ in it, so a C caller could use it.

## File structure

```
rtf-atto/
  CMakeLists.txt                     + library rtf_atto_abi (wasm/exports.cpp), test abi      (Task 1)
  .gitignore                         + js/node_modules/, js/dist/, js/build/, js/LICENSE, *.tgz (Task 3)
  .gitattributes                     + *.h *.ts *.mjs text eol=lf                              (Task 1)
  .github/workflows/ci.yml           + job wasm                                               (Task 5)
  .github/workflows/publish.yml      npm publish with provenance on a v* tag                  (Task 5)
  README.md                          + section "npm package", "Status" updated                (Task 5)
  docs/plans/2026-11-17-rtf-npm-package.md   this plan
  wasm/rtf_abi.h                     the C ABI declarations                                   (Task 1)
  wasm/exports.cpp                   the C ABI over rtf::parse, to_json, logical_tables       (Task 1)
  wasm/CMakeLists.txt                the Emscripten build: target rtf -> rtf.js + rtf.wasm    (Task 2)
  wasm/smoke.mjs                     loads a built rtf.js and prints the version (Task 2 check)
  test/cpp/test_abi.cpp              native test of the ABI                                   (Task 1)
  js/package.json                    name rtf-atto, ESM, exports, scripts                     (Task 3)
  js/package-lock.json               committed, for npm ci                                    (Task 3)
  js/tsconfig.json                   src + test -> dist, declarations                         (Task 3)
  js/tsconfig.shape.json             the generated shape files, noEmit                        (Task 3)
  js/README.md                       the npm README                                           (Task 5)
  js/scripts/build-wasm.mjs          runs the Emscripten build, copies rtf.js/rtf.wasm/rtf.d.ts to dist/src (Task 3)
  js/scripts/shape-check.mjs         writes build/shape/*.ts from the goldens, runs tsc       (Task 3)
  js/scripts/prepack.mjs             copies ../LICENSE, checks dist exists                    (Task 3)
  js/src/model.ts                    the TypeScript types of the document model and tables    (Task 3)
  js/src/rtf.d.ts                    the type of the Emscripten factory and module            (Task 3)
  js/src/errors.ts                   RtfError, RtfErrorCode                                   (Task 4)
  js/src/wasm-loader.ts              readWasm(): bytes from Uint8Array, URL or default        (Task 4)
  js/src/index.ts                    load(), RtfAtto, re-exports                              (Task 4)
  js/test/helpers.ts                 fixture paths, readers, NOT_RTF                          (Task 4)
  js/test/goldens.test.ts            every fixture: wasm JSON == golden                       (Task 4)
  js/test/errors.test.ts             RtfError codes, empty input, bad input types             (Task 4)
  js/test/load.test.ts               load() sharing, explicit bytes and URLs, bad bytes       (Task 4)
  js/test/growth.test.ts             memory growth, then a second parse                       (Task 4)
  js/test/deep.test.ts               1000 nested fields and footnotes, 5000 groups            (Task 4)
  js/test/package.test.ts            version equality, wasm size, exports map                 (Task 4)
```

## Decisions this plan takes

Each is the builder's; Massimo can overrule any of them before execution.

1. **`logicalTables` takes bytes, not a document.** The spec's API sketch has `rtf.logicalTables(doc)`, but the stitching rules live in C++ and the JavaScript side has only JSON. The wrapper offers `logicalTables(bytes)` and `logicalTablesJson(bytes)`, which parse the bytes again in C++ (a few milliseconds for the largest fixture). The ABI stays stateless: every call is a function over a byte range, as the spec's constraints say. A TypeScript port of the stitching rules can come with the renderer if a consumer needs tables from a document it already holds.
2. **No `rtf_write` yet, no `rtf_text`.** The writer is plan 4; its ABI entry and `write()` in the wrapper come with it. The text projection is not exposed: no consumer needs it before the renderer, and the model carries the text.
3. **The glue is built for `web,worker`; the wrapper reads the wasm bytes.** With `node` in `-sENVIRONMENT`, Emscripten's ES6 glue imports the `module` built-in behind a runtime check, which bundlers flag when they build for the browser. The wrapper always passes `wasmBinary`, so the glue never has to locate or read the file; the wrapper reads it with `fs` in Node (file URLs) and `fetch` elsewhere. Fallback rule (Task 2): if `rtf.js` cannot be imported in Node because of an environment check, add `node` to the list and amend this plan.
4. **Stack 4 MB, depth cap unchanged.** `-sSTACK_SIZE=4194304` instead of lowering `maxDepth`, so the wasm and native builds accept the same inputs and produce the same JSON. A test pins the deepest inputs.
5. **Two Emscripten versions.** Massimo's machine has 3.1.71 only; CI installs 6.0.13 with `mymindstorm/setup-emsdk@v14`. Every flag in Task 2 exists in both. The published wasm is the one CI builds. If `setup-emsdk` cannot install 6.0.13, the implementer uses the newest version `emsdk list` offers and records the number in this plan and in `js/README.md`.
6. **Model types checked by the compiler.** Instead of a hand-written runtime validator, a script writes one TypeScript file per golden (`const doc = <the JSON> satisfies RtfDocument;`) and runs `tsc --noEmit` over them. A fresh object literal under `satisfies` gets excess-property checks and literal-union checks, so this proves both directions with no second description of the model.
7. **Wasm size.** The spec's target is under 300 KB. The test ceiling is 400 KB, so an accidental pull-in (iostream, locale) fails the build while a normal size does not. The implementer reports the measured size; `-Oz` is the first knob if it is above 300 KB.
8. **The `reader.cpp` split waits.** Plan 1's final review suggested moving the control-word table and the structural handlers out of `reader.cpp` before plan 2. This plan adds no code to `reader.cpp`, so the split waits for the first plan that edits it (plan 4 or 5) and stays recorded in `docs/notes/2026-11-16-plan-1-deferred-findings.md`.
9. **A branch and a pull request.** `main` is public with CI, so this plan's commits go to `plan-2-npm-package`; the push at the end of Task 5 gives the first CI run of the new jobs on GitHub's emsdk and Node.
10. **Browser run deferred to plan 3.** No browser automation is added. The glue is built for the web; the first browser run is the demo page of plan 3, which Massimo opens.

---

### Task 1: The C ABI and its native test

**Files:**
- Create: `wasm/rtf_abi.h`, `wasm/exports.cpp`, `test/cpp/test_abi.cpp`
- Modify: `CMakeLists.txt`, `.gitattributes`

**Interfaces:**
- Consumes: `rtf::parse(const unsigned char*, std::size_t, const ParseOptions&)`, `rtf::to_json(const RtfDocument&)`, `rtf::to_json(const std::vector<LogicalTable>&)`, `rtf::logical_tables(const RtfDocument&)`, `rtf::version()`, `RTF_ATTO_VERSION` (all plan 1).
- Produces `wasm/rtf_abi.h`, included by `exports.cpp`, the test and later the wasm link step (Task 2 exports these names) and mirrored by `js/src/rtf.d.ts` (Task 3):

```c
#ifndef RTF_ABI_H
#define RTF_ABI_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
  RTF_OK = 0,
  RTF_ERR_NOT_RTF = -1,           /* the input does not start with {\rtf */
  RTF_ERR_INVALID_ARGUMENT = -2,  /* out or out_len is null, or data is null with size > 0 */
  RTF_ERR_OUT_OF_MEMORY = -3      /* malloc failed for the output */
};

/* Parses size bytes at data. On RTF_OK, *out is a malloc'd buffer holding the
   canonical JSON of the document (UTF-8, *out_len bytes, followed by one NUL
   byte that is not counted). On a negative code, *out holds a short message
   the same way ("not_rtf"), except RTF_ERR_OUT_OF_MEMORY and
   RTF_ERR_INVALID_ARGUMENT, which store NULL and 0 when they can. The caller
   frees *out with rtf_free. data may be NULL when size is 0. */
int rtf_parse(const unsigned char* data, size_t size, unsigned char** out, size_t* out_len);

/* The same contract; the JSON is the array of logical tables. */
int rtf_logical_tables(const unsigned char* data, size_t size, unsigned char** out, size_t* out_len);

/* Frees a buffer returned in *out. Accepts NULL. */
void rtf_free(void* p);

/* The version string of the core, static storage, NUL-terminated. */
const char* rtf_version(void);

#ifdef __cplusplus
}
#endif
#endif
```

- Produces the CMake target `rtf_atto_abi` (static library of `wasm/exports.cpp`, public include directory `wasm`, links `rtf_atto`), always defined, and the CTest test `abi` with the fixtures directory as its argument.

- [ ] **Step 1: Write the failing test `test/cpp/test_abi.cpp`**, with `main(int argc, char** argv)` taking the fixtures directory and calling, in order:
  - `version_matches_header`: `CHECK_EQ(std::string(rtf_version()), std::string(RTF_ATTO_VERSION))` and `CHECK_EQ(std::string(rtf_version()), std::string(rtf::version()))`.
  - `parse_matches_to_json`: for `word_minimal.rtf` and `sas_table_3pages.rtf` read as bytes with `std::ifstream` in binary mode: `rtf_parse` returns `RTF_OK`; the output bytes equal `rtf::to_json(rtf::parse(bytes).document)`; `out[out_len] == 0`; `rtf_logical_tables` returns `RTF_OK` and equals `rtf::to_json(rtf::logical_tables(document))`; both buffers are released with `rtf_free`.
  - `not_rtf_and_empty_input`: the bytes of `"hello"` give `RTF_ERR_NOT_RTF`, `out_len == 7` and the text `not_rtf`; `rtf_parse(nullptr, 0, &out, &len)` gives `RTF_ERR_NOT_RTF` and the same message; `rtf_logical_tables` behaves the same on both inputs.
  - `invalid_arguments`: `rtf_parse(data, 5, nullptr, &len)` and `rtf_parse(data, 5, &out, nullptr)` return `RTF_ERR_INVALID_ARGUMENT`; `rtf_parse(nullptr, 5, &out, &len)` returns `RTF_ERR_INVALID_ARGUMENT` with `out == nullptr` and `len == 0` (both set to a sentinel before the call).
  - `free_accepts_null`: `rtf_free(nullptr)` returns.
  - `deep_fields_parse`: a string of 1000 nested `{\field{\*\fldinst F}{\fldrslt ...}}` groups around `x` inside `{\rtf1\ansi ...\par}` returns `RTF_OK` with `out_len > 1000000` (the native probe of 2026-11-17 gave 3.2 MB).
  The file ends with `return rtf_test::result();`.

- [ ] **Step 2: Add the targets to `CMakeLists.txt`** after the `rtf_atto` library:

```cmake
# The C ABI used by the wasm build. Built natively too, so the tests cover it.
add_library(rtf_atto_abi STATIC wasm/exports.cpp)
target_include_directories(rtf_atto_abi PUBLIC wasm)
target_link_libraries(rtf_atto_abi PUBLIC rtf_atto)
```

and in the tests block, after `rtf_add_test(large)`: `rtf_add_test(abi ${CMAKE_SOURCE_DIR}/test/fixtures)` followed by `target_link_libraries(test_abi PRIVATE rtf_atto_abi)`. Add `*.h text eol=lf`, `*.ts text eol=lf` and `*.mjs text eol=lf` to `.gitattributes`.

- [ ] **Step 3: Configure and build; expected: the build fails** because `exports.cpp` does not exist yet (or the test fails to link). Run `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` once so the new test is registered.

- [ ] **Step 4: Write `wasm/exports.cpp`.** Includes: `rtf_abi.h`, `rtf/json.hpp`, `rtf/reader.hpp`, `rtf/tables.hpp`, `rtf/version.hpp`, `<cstdlib>`, `<cstring>`, `<string>`. One static helper `int deliver(const std::string& text, int code, unsigned char** out, size_t* out_len)` that `std::malloc`s `text.size() + 1` bytes, copies the text and the NUL, stores the pointer and the size, and returns `code`; when malloc fails it stores `nullptr` and 0 and returns `RTF_ERR_OUT_OF_MEMORY`. Both entry points validate the arguments first (null `out` or `out_len`: return `RTF_ERR_INVALID_ARGUMENT` without touching them; null `data` with `size > 0`: store `nullptr` and 0, return `RTF_ERR_INVALID_ARGUMENT`), then call `rtf::parse(data, size)` (with `data` replaced by a pointer to a static empty byte when it is null), and `deliver` either the JSON with `RTF_OK` or `result.error` with `RTF_ERR_NOT_RTF`. `rtf_free` calls `std::free`. `rtf_version` returns `rtf::version()`. No `static` mutable state, no `new`, no exceptions.

- [ ] **Step 5: Build and run the suite on both compilers; expected: 13 tests pass on each**, including `abi`.

- [ ] **Step 6: Commit**: `git add wasm/rtf_abi.h wasm/exports.cpp test/cpp/test_abi.cpp CMakeLists.txt .gitattributes && git commit -m "Add the C ABI over the reader and its native test"`.

---

### Task 2: The Emscripten build

**Files:**
- Create: `wasm/CMakeLists.txt`, `wasm/smoke.mjs`

**Interfaces:**
- Consumes: `rtf_atto` and `rtf_atto_flags` from the root `CMakeLists.txt`; `wasm/exports.cpp` and the four names of Task 1.
- Produces: `emcmake cmake -S wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release && cmake --build build-wasm` writes `build-wasm/rtf.js` (an ES module whose default export is the factory `createRtfModule(options?)`, returning a promise of the module) and `build-wasm/rtf.wasm`. The module exposes `_rtf_parse`, `_rtf_logical_tables`, `_rtf_free`, `_rtf_version`, `_malloc`, `_free`, `HEAPU8` and `HEAPU32`; the factory accepts `wasmBinary`, `onAbort`, `print` and `printErr`. Task 3's `build-wasm.mjs` runs these commands and copies the two files.

- [ ] **Step 1: Write `wasm/CMakeLists.txt`:**

```cmake
cmake_minimum_required(VERSION 3.20)
project(rtf-atto-wasm LANGUAGES CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure this directory with emcmake: emcmake cmake -S wasm -B build-wasm")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(RTF_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RTF_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. rtf_atto_core)

add_executable(rtf exports.cpp)
target_link_libraries(rtf PRIVATE rtf_atto rtf_atto_flags)
target_link_options(rtf PRIVATE
  -O2
  --no-entry
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createRtfModule
  -sENVIRONMENT=web,worker
  -sALLOW_MEMORY_GROWTH=1
  -sFILESYSTEM=0
  -sSTACK_SIZE=4194304
  -sEXPORTED_FUNCTIONS=_rtf_parse,_rtf_logical_tables,_rtf_free,_rtf_version,_malloc,_free
  -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32
  -sINCOMING_MODULE_JS_API=wasmBinary,onAbort,print,printErr)
```

The Emscripten toolchain names the outputs `rtf.js` and `rtf.wasm`. `exports.cpp` is compiled a second time here (the root defines `rtf_atto_abi` too) so that this directory links exactly one object with the exports; that is deliberate.

- [ ] **Step 2: Build it** with the wasm commands from "Repository and conventions". Expected: `build-wasm/rtf.js` and `build-wasm/rtf.wasm` exist, no warnings (the `-Werror` flags of `rtf_atto_flags` apply). If em++ reports a warning in the core, fix the code with the smallest change that keeps the goldens unchanged and report it (DONE_WITH_CONCERNS). Print `ls -l build-wasm/rtf.wasm` and keep the number for the report.

- [ ] **Step 3: Write `wasm/smoke.mjs`**, plain JavaScript: `node wasm/smoke.mjs <build-dir>` imports `<build-dir>/rtf.js` (through `pathToFileURL`), reads `<build-dir>/rtf.wasm` with `fs`, calls the factory with `{ wasmBinary }`, reads the NUL-terminated string at `_rtf_version()` from `HEAPU8`, then parses the bytes of `{\rtf1\ansi Hello\par}`: `_malloc` for the input and for two 4-byte slots, `HEAPU8.set`, `_rtf_parse(input, size, slots, slots + 4)`, reads the pointer and length from a fresh `HEAPU32`, decodes the JSON with `TextDecoder`, frees everything, and prints `version 0.1.0, parse code 0, json 1234 bytes` (the real numbers). Exit code 1 when the code is not 0 or the JSON does not contain `"Hello"`.

- [ ] **Step 4: Run `node wasm/smoke.mjs build-wasm`.** Expected: exit 0, `version 0.1.0`, `parse code 0`. This is the check of decision 3 (the glue runs in Node without `node` in `-sENVIRONMENT`). If the import itself throws an environment error, add `node` to `-sENVIRONMENT`, rebuild, rerun, and report the change so the plan is amended.

- [ ] **Step 5: Commit**: `git add wasm/CMakeLists.txt wasm/smoke.mjs && git commit -m "Add the Emscripten build of the C ABI"`.

---

### Task 3: The package skeleton, the model types and the shape check

**Files:**
- Create: `js/package.json`, `js/package-lock.json` (generated), `js/tsconfig.json`, `js/tsconfig.shape.json`, `js/scripts/build-wasm.mjs`, `js/scripts/shape-check.mjs`, `js/scripts/prepack.mjs`, `js/src/model.ts`, `js/src/rtf.d.ts`
- Modify: `.gitignore`

**Interfaces:**
- Consumes: the wasm build of Task 2; the committed goldens `test/fixtures/*.json` and `*.tables.json`; the spec section "The document model".
- Produces for Task 4: every exported type of `js/src/model.ts` (`RtfDocument`, `Font`, `Color`, `Style`, `PageSetup`, `Warning`, `Section`, `HeaderSet`, `Block`, `Paragraph`, `ParagraphProps`, `TabStop`, `Inline`, `TextRun`, `CharProps`, `LineBreak`, `Tab`, `PageBreak`, `ColumnBreak`, `Field`, `Bookmark`, `Footnote`, `Picture`, `Shape`, `EmbeddedObject`, `Table`, `Row`, `RowProps`, `Cell`, `Borders`, `Border`, `Shading`, `Padding`, `LogicalTable`, `LogicalRow`, `LogicalCell`, `TablePart`); the declaration `js/src/rtf.d.ts`; `npm run build:wasm`, `npm run build:ts`, `npm run build` (both), `npm run test:shape`; `dist/src/` holding `rtf.js`, `rtf.wasm`, `rtf.d.ts` and the compiled sources.

- [ ] **Step 1: Write `js/package.json`:**

```json
{
  "name": "rtf-atto",
  "version": "0.1.0",
  "description": "RTF reader: the document model and the logical tables of an RTF file as JSON, from a C++ core compiled to WebAssembly",
  "license": "Apache-2.0",
  "repository": { "type": "git", "url": "git+https://github.com/caerbannogwhite/rtf-atto.git", "directory": "js" },
  "homepage": "https://github.com/caerbannogwhite/rtf-atto#readme",
  "type": "module",
  "main": "./dist/src/index.js",
  "types": "./dist/src/index.d.ts",
  "exports": {
    ".": { "types": "./dist/src/index.d.ts", "default": "./dist/src/index.js" },
    "./rtf.wasm": "./dist/src/rtf.wasm",
    "./package.json": "./package.json"
  },
  "files": ["dist/src"],
  "sideEffects": false,
  "engines": { "node": ">=20" },
  "keywords": ["rtf", "rtf-parser", "wasm", "webassembly", "sas", "clinical-trials", "tables"],
  "scripts": {
    "build:wasm": "node scripts/build-wasm.mjs",
    "build:ts": "tsc -p tsconfig.json",
    "build": "npm run build:wasm && npm run build:ts",
    "test:shape": "node scripts/shape-check.mjs",
    "test:unit": "node --test \"dist/test/*.test.js\"",
    "test": "npm run test:shape && npm run test:unit",
    "prepack": "node scripts/prepack.mjs"
  },
  "devDependencies": { "typescript": "^5.9.0", "@types/node": "^24.0.0" }
}
```

`npm pack` adds `package.json`, `README.md` and `LICENSE` on its own; `files` lists only `dist/src`, so the compiled tests never ship.

- [ ] **Step 2: Write `js/tsconfig.json`** with `compilerOptions` `target: "ES2022"`, `module: "NodeNext"`, `moduleResolution: "NodeNext"`, `rootDir: "."`, `outDir: "dist"`, `declaration: true`, `strict: true`, `verbatimModuleSyntax: true`, `types: ["node"]`, `skipLibCheck: true`, and `include: ["src", "test"]`. Write `js/tsconfig.shape.json` with `compilerOptions` `target: "ES2022"`, `module: "NodeNext"`, `moduleResolution: "NodeNext"`, `strict: true`, `noEmit: true`, `skipLibCheck: true`, and `include: ["build/shape"]`. Run `cd js && npm install`; expected: `node_modules/` and `package-lock.json` appear, TypeScript 5.9.x installed (`npx tsc --version`).

- [ ] **Step 3: Add to `.gitignore`**: `js/node_modules/`, `js/dist/`, `js/build/`, `js/LICENSE`, `*.tgz`. (`build*/` already covers `build-wasm/`.)

- [ ] **Step 4: Write `js/scripts/build-wasm.mjs`.** It resolves the repository root from `import.meta.url` (two levels up from `js/scripts/`), takes the build directory from `RTF_WASM_BUILD_DIR` (default `<root>/build-wasm`), runs the configure command and the build command with `child_process.spawnSync` and `stdio: 'inherit'`, and exits with the child's status when one fails. On `win32` the commands are `emcmake.bat cmake -S <root>/wasm -B <dir> -G Ninja -DCMAKE_BUILD_TYPE=Release` and `cmake --build <dir>`, each passed as one string with `shell: true` (a `.bat` file cannot be spawned without a shell, and one string avoids Node's deprecation warning about shell arguments); elsewhere `emcmake` and `cmake` are spawned directly with argument arrays and no shell. Then it creates `js/dist/src/` and copies `<dir>/rtf.js`, `<dir>/rtf.wasm` and `js/src/rtf.d.ts` into it. Paths come from `path.join`, never from string concatenation.

- [ ] **Step 5: Write `js/scripts/prepack.mjs`**: copies `<root>/LICENSE` to `js/LICENSE`; exits 1 with the message `run npm run build first` when `js/dist/src/index.js` or `js/dist/src/rtf.wasm` is missing.

- [ ] **Step 6: Write the failing shape check `js/scripts/shape-check.mjs`.** For every `<name>.json` in `<root>/test/fixtures` whose content is not `{"error": "not_rtf"}` plus a newline, it writes `js/build/shape/<name>.ts`:

```ts
import type { RtfDocument } from '../../src/model.js';
const doc = <the golden JSON text> satisfies RtfDocument;
export default doc;
```

and for every `<name>.tables.json` with the same exclusion, `js/build/shape/<name>.tables.ts` with `LogicalTable[]` in place of `RtfDocument` (file names with dots are fine for tsc). Then it runs `tsc -p tsconfig.shape.json` through `spawnSync(process.execPath, [<js>/node_modules/typescript/bin/tsc, '-p', 'tsconfig.shape.json'], { cwd: <js>, stdio: 'inherit' })` and exits with its status. Run `npm run test:shape`; expected: it fails because `src/model.ts` does not exist.

- [ ] **Step 7: Write `js/src/model.ts`.** Transcribe the TypeScript block of the spec section "The document model" as exported interfaces and type aliases, with these amendments (all recorded in the spec on 2026-11-16 and visible in the goldens):
  - `fonts: Record<number, Font>` (the JSON keys are the RTF font numbers as strings; TypeScript accepts numeric-string keys for a number index).
  - `Shape` has `picture?: Picture`.
  - Colour properties (`color`, `background`, `highlight`, `Border.color`, `Shading.background`, `Shading.foreground`) are `number` when present, never `null`; only `colors` entries can be `null`.
  - `Bookmark` is `{ kind: 'bookmark'; name: string; position: 'start' | 'end' }`.
  - `PageSetup.landscape` is required; `facingPages?: boolean` appears only when true.
  - `ParagraphProps.widowControl?: boolean` can be `false` in the JSON; `lineSpacingMultiple?`, `keepTogether?`, `keepWithNext?`, `pageBreakBefore?`, `rtl?` appear only when true; `tabs?: TabStop[]` and `Paragraph.listText?: Inline[]` appear only when non-empty.
  - `Picture.data` is the base64 string.
  - Add the logical tables, from `include/rtf/tables.hpp`, every field required because the JSON writes every field: `interface LogicalCell { text: string; column: number; span: number }`, `interface LogicalRow { cells: LogicalCell[]; spacer: boolean; indent: number }`, `interface TablePart { section: number; block: number }`, `interface LogicalTable { id: number; firstSection: number; lastSection: number; parts: TablePart[]; edges: number[]; columnNames: string[]; headerRows: Row[]; rows: LogicalRow[] }`.
  Each property keeps the spec's comment (the RTF control word, the unit, the default when omitted). The file has no runtime code.

- [ ] **Step 8: Write `js/src/rtf.d.ts`**, the hand-written type of the Emscripten output:

```ts
export interface RtfWasmModule {
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
  _malloc(size: number): number;
  _free(ptr: number): void;
  _rtf_parse(data: number, size: number, out: number, outLen: number): number;
  _rtf_logical_tables(data: number, size: number, out: number, outLen: number): number;
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
```

- [ ] **Step 9: Run `npm run test:shape`; expected: pass** (tsc compiles 34 generated files without an error). If a golden does not satisfy the types, the types are wrong, not the golden: compare with the spec and the C++ `model.hpp` and fix `model.ts`. Then run `npm run build`; expected: `dist/src/model.js`, `dist/src/model.d.ts`, `dist/src/rtf.js`, `dist/src/rtf.wasm`, `dist/src/rtf.d.ts` exist.

- [ ] **Step 10: Commit**: `git add .gitignore js/package.json js/package-lock.json js/tsconfig.json js/tsconfig.shape.json js/scripts/build-wasm.mjs js/scripts/shape-check.mjs js/scripts/prepack.mjs js/src/model.ts js/src/rtf.d.ts && git commit -m "Add the npm package skeleton, the model types and the shape check over the goldens"`.

---

### Task 4: The wrapper and the node tests

**Files:**
- Create: `js/src/errors.ts`, `js/src/wasm-loader.ts`, `js/src/index.ts`, `js/test/helpers.ts`, `js/test/goldens.test.ts`, `js/test/errors.test.ts`, `js/test/load.test.ts`, `js/test/growth.test.ts`, `js/test/deep.test.ts`, `js/test/package.test.ts`

**Interfaces:**
- Consumes: `RtfWasmModule`, `RtfWasmModuleOptions`, `createRtfModule` from `./rtf.js` (Task 3); the types of `./model.js`; the ABI codes of Task 1 (0, -1, -2, -3).
- Produces the public API of the package, all from `js/src/index.ts`:

```ts
export type * from './model.js';
export { RtfError } from './errors.js';
export type { RtfErrorCode } from './errors.js';

export interface LoadOptions {
  wasm?: Uint8Array | ArrayBuffer | URL | string;   // bytes, or where to get them; default: rtf.wasm next to this module
}
export interface RtfAtto {
  readonly version: string;                              // the core's version, "0.1.0"
  parse(bytes: Uint8Array | ArrayBuffer): RtfDocument;   // throws RtfError
  parseToJson(bytes: Uint8Array | ArrayBuffer): string;  // the canonical JSON, same bytes as rtf2json
  logicalTables(bytes: Uint8Array | ArrayBuffer): LogicalTable[];
  logicalTablesJson(bytes: Uint8Array | ArrayBuffer): string;
}
export function load(options?: LoadOptions): Promise<RtfAtto>;
```

  and from `js/src/errors.ts`: `export type RtfErrorCode = 'not_rtf' | 'invalid_argument' | 'out_of_memory' | 'wasm_aborted' | 'load_failed' | 'internal';` and `export class RtfError extends Error { readonly code: RtfErrorCode; constructor(code: RtfErrorCode, message: string); }` with `name` set to `'RtfError'`.
  `js/src/wasm-loader.ts` exports `readWasm(source: LoadOptions['wasm'], base: string): Promise<Uint8Array>` (internal).

Rules of the wrapper (the implementer writes the bodies from these):
- `load()` with no argument returns the same promise every time (one shared instance); `load(options)` with an argument always creates a new instance. When an instance dies (below) and it was the shared one, the shared promise is dropped so the next `load()` builds a fresh one.
- `readWasm`: a `Uint8Array` is used as is, an `ArrayBuffer` is wrapped; a `URL`, a `string` or `undefined` becomes `new URL(source ?? './rtf.wasm', base)` where `base` is `import.meta.url` of `index.ts`. A `file:` URL is read with `fs.promises.readFile(url)` obtained through `process.getBuiltinModule('node:fs/promises')` when that function exists and through `await import(spec)` with `spec = 'node:fs/promises'` held in a variable otherwise (so bundlers see no built-in import); any other URL goes through `fetch` and `response.ok` is checked. Every failure is thrown as `RtfError('load_failed', message)`, including a factory rejection (bad wasm bytes).
- The factory is called with `{ wasmBinary, onAbort }`; `onAbort` marks the instance dead.
- Every ABI call goes through one internal function: check the input type (`Uint8Array` or `ArrayBuffer`, else `RtfError('invalid_argument', ...)`); `_malloc(max(1, size))` for the input and `_malloc(8)` for the two output slots; `HEAPU8.set`; call; then read the pointer and the length from a **fresh** `HEAPU32` and the bytes from a **fresh** `HEAPU8` (memory growth replaces the views; a comment says so); decode with one shared `TextDecoder('utf-8')`; in `finally`, `_rtf_free(pointer)`, `_free(slots)`, `_free(input)`. A non-zero code throws `RtfError` with the code mapped -1 `not_rtf`, -2 `invalid_argument`, -3 `out_of_memory`, anything else `internal`, and the decoded message. Any exception that is not an `RtfError` (a wasm trap, an abort) marks the instance dead and is rethrown as `RtfError('wasm_aborted', String(e))`. A dead instance throws `RtfError('wasm_aborted', 'the wasm instance aborted; call load() again')` from every method before touching the module.
- `version` is read once at load from the NUL-terminated string at `_rtf_version()`.
- `parse` is `JSON.parse(parseToJson(bytes))` typed as `RtfDocument`; `logicalTables` the same over `logicalTablesJson`.

- [ ] **Step 1: Write `js/test/helpers.ts`**: `fixturesDir = new URL('../../../test/fixtures/', import.meta.url)` (from `dist/test/`), `listFixtures(): string[]` (the `.rtf` names, sorted), `readFixture(name): Uint8Array`, `readGolden(name, suffix: '.json' | '.tables.json'): string` (UTF-8 text), `NOT_RTF = '{"error": "not_rtf"}\n'`, `rtfBytes(text: string): Uint8Array` (`TextEncoder`), `packageJson()` (parsed `js/package.json`), `distFile(name): string` (a path under `dist/src/`).

- [ ] **Step 2: Write the failing tests:**
  - `goldens.test.ts`, test `every_fixture_matches_its_goldens`: `const rtf = await load()`; for each fixture name: when `readGolden(name, '.json') === NOT_RTF`, `assert.throws(() => rtf.parse(bytes), (e) => e instanceof RtfError && e.code === 'not_rtf')` and the same for `logicalTablesJson`; otherwise `assert.equal(rtf.parseToJson(bytes), golden)`, `assert.equal(rtf.logicalTablesJson(bytes), tablesGolden)`, `assert.deepEqual(rtf.parse(bytes), JSON.parse(golden))`, `assert.deepEqual(rtf.logicalTables(bytes), JSON.parse(tablesGolden))`. Test `parse_is_deterministic`: `parseToJson` of `msword_features.rtf` twice gives equal strings.
  - `errors.test.ts`: `empty_input_is_not_rtf` (`new Uint8Array(0)` and `new ArrayBuffer(0)`: code `not_rtf`, message `not_rtf`, `name === 'RtfError'`, `instanceof Error`); `text_is_not_rtf` (`rtfBytes('hello')`); `wrong_input_type_is_invalid_argument` (`rtf.parse('x' as unknown as Uint8Array)` throws code `invalid_argument` without calling into the wasm).
  - `load.test.ts`: `load_without_options_is_shared` (`await load() === await load()`); `load_with_bytes_is_a_new_instance` (`load({ wasm: readFileSync(distFile('rtf.wasm')) })` is a different object whose `parseToJson` of `word_minimal.rtf` equals the golden); `load_with_a_file_url` (`new URL(pathToFileURL(distFile('rtf.wasm')))`) and `load_with_a_string` (`'./rtf.wasm'`, resolved against the module) both work; `load_with_bad_bytes_rejects` (`new Uint8Array([0, 1, 2, 3])` rejects with `RtfError` code `load_failed`).
  - `growth.test.ts`, test `growth_then_second_parse`: builds `{\rtf1\ansi` + 20,000 rows of `\trowd\cellx3000\cellx6000\cellx9000 r<i>c0\cell r<i>c1\cell r<i>c2\cell\row` + `}`; `logicalTables` gives one table with `rows.length === 20000` and `rows[19999].cells[2].text === 'r19999c2'`; then `parseToJson(word_minimal)` on the same instance equals its golden.
  - `deep.test.ts`, test `deep_nesting_parses`: 1000 nested fields (`{\field{\*\fldinst F}{\fldrslt ...}}` around `x`, inside `{\rtf1\ansi ...\par}`): `parse` succeeds and walking `result[0]` from the first run of the first body paragraph reaches depth 1000 with text `x` at the end; 1000 nested footnotes (`a{\footnote ...\par}`): `parseToJson(bytes).length > 1_000_000`; 5000 nested groups (`{` × 5000 + `x` + `}` × 5000): `parse` succeeds with one paragraph whose text is `x`.
  - `package.test.ts`: `version_matches_package_json` (`rtf.version === packageJson().version`); `wasm_is_under_the_ceiling` (`statSync(distFile('rtf.wasm')).size < 400 * 1024`, and the test logs the size with `t.diagnostic`); `exports_point_at_existing_files` (every path in `package.json` `exports` exists).
  Run `npm run build && npm run test:unit`; expected: compile errors or failures because `index.ts` does not exist.

- [ ] **Step 3: Write `js/src/errors.ts`, `js/src/wasm-loader.ts` and `js/src/index.ts`** per the rules above.

- [ ] **Step 4: Run `npm run build && npm test`; expected: all tests pass.** Record in the report: the wasm size, the time of `every_fixture_matches_its_goldens`, and the time of `deep_nesting_parses`. If `deep_nesting_parses` fails with a `RuntimeError` (stack overflow), do not lower the inputs: report it; the stack size decision is the controller's.

- [ ] **Step 5: Commit**: `git add js/src/errors.ts js/src/wasm-loader.ts js/src/index.ts js/test && git commit -m "Add the TypeScript wrapper and the node tests over the goldens"`.

---

### Task 5: READMEs, CI and publishing

**Files:**
- Create: `js/README.md`, `.github/workflows/publish.yml`
- Modify: `.github/workflows/ci.yml`, `README.md`

**Interfaces:**
- Consumes: everything above.
- Produces: the CI job `wasm` on every push and pull request; the workflow `publish` on tags `v*`; the npm README that ships in the package.

- [ ] **Step 1: Add the `wasm` job to `ci.yml`:**

```yaml
  wasm:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: mymindstorm/setup-emsdk@v14
        with:
          version: 6.0.13
      - uses: actions/setup-node@v4
        with:
          node-version: 24
          cache: npm
          cache-dependency-path: js/package-lock.json
      - run: npm ci
        working-directory: js
      - run: npm run build
        working-directory: js
      - run: npm test
        working-directory: js
      - run: ls -l dist/src/rtf.wasm && npm pack --dry-run
        working-directory: js
```

- [ ] **Step 2: Write `publish.yml`**: `on: push: tags: ['v*']`; `permissions: contents: read, id-token: write`; one job on `ubuntu-latest` with the same checkout, emsdk and Node steps (plus `registry-url: https://registry.npmjs.org` on setup-node), then a step that fails unless `GITHUB_REF_NAME` equals `'v' + version` from `js/package.json` (a `node -e` one-liner), then `npm ci`, `npm run build`, `npm test`, and `npm publish --provenance --access public` in `js` with `NODE_AUTH_TOKEN: ${{ secrets.NPM_TOKEN }}`. A comment at the top says the secret is Massimo's and that npm's trusted publishing can replace the token once the package exists.

- [ ] **Step 3: Write `js/README.md`** with these sections, in plain English: what the package is (the reader of rtf-atto; what it returns; what it does not do yet: no renderer, no writer, no figures); install (`npm install rtf-atto`); a quick start (`load`, `parse`, `logicalTables`, the error class) as a code block; the API (one line per member of `RtfAtto`, `load`, `LoadOptions`, `RtfError` and its codes); the model (units in twips and half-points, defaults omitted, fonts keyed by RTF font number, colours as indexes into `colors` with `null` meaning automatic, one section per page in SAS output, and the viewer's contract from the spec: a `PAGE` field with an empty result shows the section's number and `NUMPAGES` the section count); loading the wasm (the default URL next to the module, `load({ wasm })` for bundlers that cannot follow `new URL(..., import.meta.url)`, Node reads file URLs itself); untrusted input (the reader never throws anything but `RtfError`; an abort inside the wasm leaves the instance unusable and the next `load()` builds a new one; the input is copied into wasm memory, so a 10 MB file needs tens of megabytes); size and speed (the measured wasm size and the measured time of the goldens test from Task 4); building from source (emsdk versions, the three commands); licence.

- [ ] **Step 4: Edit the root `README.md`**: a section "npm package" after "The command" (what `js/` holds, the build and test commands, a pointer to `js/README.md`), and "Status" updated: plan 2 is complete; the renderer, the writer and the DuckDB integration belong to later plans.

- [ ] **Step 5: Verify locally**: `npm run build && npm test` in `js`, both native suites, and `git status` clean except ignored files. Then commit: `git add .github/workflows/ci.yml .github/workflows/publish.yml js/README.md README.md && git commit -m "Add the npm README, the wasm CI job and the publish workflow"`.

- [ ] **Step 6: Push the branch for CI** (the controller, per the Global constraints): `git push -u origin plan-2-npm-package`, then `gh run watch` the run; expected: the four plan-1 jobs and `wasm` green. A failure in `wasm` is fixed on the branch (emsdk 6.0.13 is the first suspect: a flag renamed, a warning promoted) and pushed again. The implementation is complete when CI is green; the pull request and the merge are Massimo's decision (finishing-a-development-branch).

---

## Self-review notes

- Spec coverage: the C ABI (T1), the Emscripten build and its flags (T2; `-sENVIRONMENT`, the stack and `-sINCOMING_MODULE_JS_API` are the builder's amendments, recorded in "Decisions"), `model.ts` checked against every golden (T3; spec: "a runtime shape check over the goldens", done at compile time instead, decision 6), the wrapper API (T4; `logicalTables(bytes)` instead of `(doc)`, decision 1; `write` waits for plan 4, decision 2), node tests over the goldens (T4; validation items 1 and 5, the wasm half), CI build on Linux with emsdk and publishing on a `v*` tag with provenance (T5). The viewer's contract goes into the npm README (T5). Validation item 7 (the browser) is plan 3's.
- Step scan: every code step names its file, its signatures and the exact values (codes, flags, versions, sizes); every test step names the test and its assertions; the only prose bodies are the wrapper rules in T4, which fix the behaviour the signatures do not.
- Type consistency: `rtf_parse`, `rtf_logical_tables`, `rtf_free`, `rtf_version` (T1) are the names in `-sEXPORTED_FUNCTIONS` (T2) and in `rtf.d.ts` with the leading underscore (T3) and the ones `index.ts` calls (T4). `createRtfModule` is `-sEXPORT_NAME` (T2) and the default export in `rtf.d.ts` (T3). `RtfDocument` and `LogicalTable` (T3) are what `parse` and `logicalTables` return (T4) and what the shape files import (T3). `NOT_RTF` text is the one `regen_goldens.py` writes.
- Review focus: items 1 and 2 have their tests in T4 (`growth_then_second_parse`, `deep_nesting_parses`), item 3 is exercised by every node test and by `wasm/smoke.mjs` (T2), item 4 in T1 (`not_rtf_and_empty_input`, `invalid_arguments`) and T4 (`empty_input_is_not_rtf`), item 5 is T3's shape check.
- Proportion: five tasks, two short code blocks of configuration, the rest signatures, rules and test names. The plan is shorter than the code it asks for.
