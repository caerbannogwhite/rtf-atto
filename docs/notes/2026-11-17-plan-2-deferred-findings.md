# Plan 2 (the npm package): deferred findings

Date: 2026-11-17. Source: the final whole-branch review of the branch
`plan-2-npm-package` and the controller's rulings. The rulings taken while
the plan ran are in the plan's own "Amendments during execution" section.
The lines below are the review findings that were deferred, kept so that
plan 3 and the later plans can pick them up. Each one is Massimo's to
decide, and each ends with what waiting costs.

- **Node 20 is not in the CI matrix.** The package says it runs on Node 20
  or later. The tests are selected with a glob in `npm run test:unit`,
  which needs Node 22 or later, so a Node 20 job needs a test invocation
  that lists the files. Cost of waiting: the Node 20 claim is tested by
  nobody.
- **`src` is type-checked with the Node types.** `js/tsconfig.json`
  includes `src` and `test` together with `"types": ["node"]`, so a Node
  global used by mistake in the wrapper would type-check. A second
  configuration over `src` alone, without `@types/node`, would catch it.
  Cost of waiting: the browser build of the wrapper is proven by the tests
  and by reading, not by the compiler.
- **The workflows name the actions by major tag.** `actions/checkout@v5`,
  `actions/setup-node@v5` and `mymindstorm/setup-emsdk@v14`. Pinning them
  by commit SHA, with Dependabot to move the pins, is a policy choice for
  the repository. The v7 majors of checkout and setup-node exist. Cost of
  waiting: the publish job trusts the tag owners.
- **The dead-instance rule has no test.** After the wasm aborts, every
  method of the instance throws `RtfError('wasm_aborted')` and the next
  `load()` builds a new instance. No fixture makes the wasm abort, so the
  rule is covered by reading only. A test needs a module factory injected
  into `load()` so that `onAbort` can be fired on purpose. Cost of
  waiting: a regression in the rule would reach a consumer first.
- **The catch-all on the call path stays.** Any exception that is not an
  `RtfError` marks the instance dead and is rethrown as `wasm_aborted`.
  A wasm stack overflow surfaces in V8 as a `RangeError`, not as a
  `WebAssembly.RuntimeError`, so the check cannot be narrowed without
  leaving a dead instance alive. Cost: an output above 512 million
  characters, which makes string building throw, reports a healthy
  instance as aborted.
- **`logicalTables` takes bytes.** Decision 1 of the plan: the stitching
  rules live in C++, so the wrapper parses the bytes again instead of
  taking a document. A TypeScript port of the rules can come with the
  renderer if a consumer needs tables from a document it already holds.
  Cost of waiting: a consumer that has both the document and the tables
  parses the file twice, a few milliseconds for the largest fixture.

Before the first `v0.1.0` tag, the repository needs the `NPM_TOKEN`
secret, or the publish workflow needs npm's trusted publishing, which can
be set up only once the package exists on npm. The name `rtf-atto` was
free on the registry on 2026-11-17.

## Decisions of 2026-11-18

Massimo decided: the Node floor rises to 22; the `src`-only type check is
added; the actions keep their major tags and Dependabot moves them, with
npm's trusted publishing instead of a token; the dead-instance test is
added; the catch-all and the depth-limit text loss stay as they are. Plan 2's
decision 1 is overruled: `logicalTables` must not parse the bytes a second
time, so the stitching rules are ported to TypeScript. Publishing waits until
the end of plan 3. All of this is carried out by
`docs/plans/2026-11-18-rtf-renderer.md`.
