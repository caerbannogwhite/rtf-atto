# Plan 3 (the renderer): deferred findings

Date: 2026-11-19. Source: the per-task reviews and the final whole-branch
review of the branch `plan-3-renderer`, and the controller's rulings. The
rulings taken while the plan ran are in the plan's own "Amendments during
execution" section. The lines below are the findings the final review
marked "can wait", plus three questions that belong to Massimo. Each line
ends with what waiting costs.

## Questions for Massimo

- **Header inheritance across sections.** The design says a new section
  starts with no headers, and the reader does so. Word links a section's
  headers to the previous section's when the file repeats no header
  groups, so a multi-section Word document can lose its running header in
  later sections in the renderer. A rule "a section with no headers uses
  the previous section's set" could live in the renderer or in the core.
  Cost of waiting: running headers missing on later pages of such files.
- **A dropped tab in the reader.** The header of `msword_features` reads
  "This is in the centrethis is on the right" with one run and one tab in
  the model, so the second tab is absent from the file or dropped by the
  reader. Cost of waiting: a text run glued to the next one in that header.
- **Tab alignment kinds.** Decision 14 renders every tab as a fixed-width
  span from the previous stop, so centred and right-aligned tab text starts
  at the stop's left edge and the width of the text before the tab is
  ignored. Cost of waiting: header lines with centred or right tabs sit
  left of where Word puts them.

## Renderer

- `pageNumberStart` on a section with `break: 'none'` is ignored; Word
  applies the restart to the page that holds the continuous break. Cost:
  a page number on a corner-case document.
- A bookmark in a header or footer repeats on every page and gives
  duplicate ids. Cost: none visible; browsers tolerate it.
- `size` 0 gives `font-size: 0px`; a negative `lineSpacing` with
  `lineSpacingMultiple` gives an invalid `line-height` that browsers drop.
  Cost: the output says what the file says.
- `gapHalf` and the padding sides reach `px` without `num()`; a string is
  coerced by the multiplication (digits only, no injection). Cost: none.
- A duplicate `right`, or a `right` at or below the running left, gives a
  cell that covers no grid column; the row then has one column more than
  the grid. The horizontal fold runs before the vertical scan, so a cell
  merged both ways over a partly continued row can overlap a normal cell.
  Word produces neither. Cost: layout on malformed input, no text lost.
- `inlines.ts` and `blocks.ts` import each other (no load-time use); the
  root `after` list of the render state is never read, so a shape rendered
  through `renderInlines` outside `renderParagraph` would be dropped (no
  such caller). Cost: a trap for the next editor; a comment would do.
- The `fonts` option value is written unchecked when it is not a string
  (caller data by design). Cost: none for document input.
- The renderer guards null borders, non-array block lists and lone
  surrogates in bookmark names, but a `null` element inside a model array
  (`body: [null]`, `props: null`) still throws a `TypeError`. The core never
  writes one. Cost: a throw on hand-made JSON, never on a parsed file.
- A computed page number takes the character properties of the text run
  next to it but not a `hidden` flag on that run. Cost: a visible number
  next to hidden text, which Word does not produce.
- The header comment of `rtf.css` says the file "holds only the frame"; it
  also resets paragraph margins, keeps white space and resets table cells.
  The README says so; the comment keeps the plan's exact text. Cost: none.
- `regen-html-goldens.mjs` duplicates `NOT_RTF` from the test helpers;
  `npm pack --dry-run` in `package.test.ts` runs `prepack`, which copies
  LICENSE into `js/` (git-ignored) on every test run. Cost: none.
- Unit coverage gaps: a non-empty upper header over an empty lower name,
  a gap paragraph holding only page breaks, a first cell whose first block
  is a table (tables); a first cell in the last header row over a continued
  body cell, the per-side mixed padding fallback, `align: 'right'`
  (table rendering); a `fonts` override containing a quote, and
  `pageNumberStart` on a `break: 'none'` section (pages). The fixtures and
  the goldens cover the real inputs. Cost: a regression in one of those
  branches reaches the goldens first.
- `instance.test.ts` relies on `node --test` running each file in its own
  process and leaves a shared instance from the injected factory alive;
  the trap test covers `onDeath` through `onAbort` only. Cost: none today.

## Demo and server

- The demo opens the alphabetically first fixture, `malformed_bin_past_end`,
  by default. Cost: an odd first screen; Massimo picks the SAS fixtures.
- The logical-table view ignores `LogicalCell.column`, so a row whose cells
  skip a column shifts left; SAS indent and spacer rows are the usual case.
  Cost: a misaligned check view of the stitching.
- `serve.mjs` reads whole files into memory with no size bound; its script
  check compares `fileURLToPath(import.meta.url)` with
  `path.resolve(process.argv[1])`, which differ through a junction, so the
  script would exit silently; `/js/demo` without the trailing slash serves
  the page with broken relative links; the server test does not assert
  `no-store` on the 400 response and the HTML 200. Cost: none for the
  repository's own files on localhost.

## Package and workflows

- `publish.yml` treats any `npm view` failure (a registry outage) as "not
  published" and goes on; the publish step then fails loudly. `npm@latest`
  is installed unpinned. `dependabot.yml` has no ignore rule for
  `@types/node` majors above the Node floor. Cost: none until a registry
  outage coincides with a tag.
- `.gitattributes` has mixed line endings in the working copy; the index
  is LF. Cost: a git warning.
