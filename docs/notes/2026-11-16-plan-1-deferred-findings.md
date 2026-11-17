# Plan 1 (the reader): deferred findings and rulings

Date: 2026-11-16. Source: the subagent-driven execution ledger of
`docs/plans/2026-11-16-rtf-reader-implementation.md`. Every task passed a
spec-and-quality review; the whole-branch review approved the branch after
one fix wave. The lines below are the review findings that were deferred
(all judged safe to wait by the final reviewer) and the controller's
rulings, kept so that plans 2 to 5 can pick them up. Each ruling ends with
what it costs if it turns out wrong.

## Lines from the ledger

- Ruling: implementation commits go to `main` in the dedicated clone, without a worktree — the approved plan says "Commits go to main during plan 1", the clone holds nothing else, and nothing is pushed until Massimo says so — if wrong, the commits are local and move to a branch with one reset.
- Ruling: JSON omission rule stated for T7's dispatch: every field that is neither `std::optional` nor `bool` is always written (so `version`, `break`, `kind`, `right`, `width`, `instruction`, `name`, `position`, `format`, `data`, `text`, `index`, `type`, `code`, `message`, `offset` always appear); optionals when set; bools when true; `landscape` always; `listText` and `tabs` only when non-empty; `facingPages` only when true — this is what the plan's expected JSON and its examples imply — if wrong, the goldens are regenerated once.
- Task 1: minor (deferred): RTF_SANITIZE is silently ignored on MSVC (CMakeLists.txt else-branch); consider a message(WARNING). Done 2026-11-17: CMake warns and the README names the Clang that Visual Studio installs.
- Task 1: minor (deferred): check.hpp has no CHECK_EQ overloads for unsigned, long long, char (Task 3 adds unsigned/uint32_t).
- Task 1: minor (deferred): test_model.cpp checks only some PageSetup defaults.
- Task 2: minor (deferred): parameter digit count includes leading zeros, so 12+ digits with leading zeros clamp wrongly (src/tokenizer.cpp:165-172).
- Task 2: minor (deferred): no tests for input ending inside a hex escape, a dash without digits, a 12-digit parameter, u/bin without parameter, bin with zero bytes left.
- Task 2: minor (deferred): INT32_MAX/MIN rely on <cstdint> via the header.
- Task 3: implementer DONE, commit ca85514; review dispatched (sonnet) on review-b99d0a3..ca85514.diff. Implementer note: check.hpp int overloads replaced by one integral template (size_t = unsigned long long on Windows).
- Task 3: minor (deferred): undefined-byte mapping tested for cp1252 only; UTF-8 boundary values untested.
- Ruling: the plan's Task 6 said a skipped \*\pn is silent while the spec lists it among the destinations skipped with a warning; the spec wins: \*\pn warns with skipped_destination and the Task 6 test lists_as_text now expects 2 warnings (plan amended, commit after ca85514; Task 6 brief regenerated) — cost if wrong: one extra warning line on old-style Word lists.
- Ruling: \*\datafield (binary data inside a field, seen in Word hyperlinks) is skipped silently rather than warned as an unknown destination, and the font-table children \*\falt, \*\panose, \*\fname and the info children are silent too; the spec's warned list keeps \*\blipuid and \*\picprop warned — cost if wrong: one missing or one extra warning line per Word file.
- Task 4: implementer NEEDS_CONTEXT: the brief's test char_props_and_plain had groups {\b\plain y}{\ul0 z} whose runs merge under the equal-props rule, so run(p, 2) did not exist. Ruling: keep the merge rule, change the third group to {\i\ul\ul0 z} and assert 3 runs, italic on z, underline cleared (plan amended, brief regenerated after the implementer commits) — cost if wrong: none beyond a test edit. Implementer also reports reader.cpp at 1457 lines (226 of them the sorted control-word table); accepted, noted for the final review.
- Ruling: three of the minors enter fix round 1 with the Critical because Tasks 5 and 6 build on them (tab align/leader group state; flattened-group skip flag; stable sink targets, i.e. sections built in a non-moving container) — cost if wrong: a slightly larger fix diff to re-review.
- Task 4: minor (deferred): on_hex decodes (and can warn) even when the text target is Discard.
- Task 4: minor (deferred): ilvl without ls creates list id 0; RTF treats a missing ls as no list.
- Task 4: minor (deferred): at end of input with open groups the final paragraph takes the root group's props.
- Task 4: minor (deferred): tests missing for uc0, unpaired surrogate, colortbl last entry without ;, box, pard clearing the border target, skipped group with nested braces, depth cap with skipping, the drop rule at destination close, ul0 after uld.
- Ruling: the commit trailer names the model that made the commit (this one says Claude Opus 5.5; Tasks 1-3 say Claude Fable 5.1 because the dispatch told them so); both are truthful, nothing is rewritten; later dispatches say 'use the Co-Authored-By line your harness gives you' and the plan's Global Constraints line is relaxed to match — cost if wrong: an inconsistent trailer in local history.
- Ruling: table nesting is clamped at a named constant kMaxTableNesting = 32 instead of the moving bound (design: depth bounded by the group cap; 32 keeps a garbage itap cheap) — cost if wrong: a document nested deeper than 32 tables loses levels, which Word never writes.
- Ruling: a block-sink destination (header, footer, footnote, shptxt, result) resets ParaState and CharProps at open, as if pard plain were written — cost if wrong: a producer relying on inherited formatting inside a header loses it; Word and SAS write pard plain there anyway.
- Task 5: minor (deferred): no definitions plus trleft gives right = left, not 0.
- Task 5: minor (deferred): row border target not cleared by later tr* words; stray row-row emits a row of empty cells (unpinned); reader.cpp 1754 lines.
- Ruling: Task 6 may move its structural destination handlers into src/reader_structure.cpp with a private src/reader_impl.hpp declaring the Reader class if reader.cpp would pass about 2300 lines — the plan's single reader.cpp was a size estimate, not a requirement; cost if wrong: one extra internal header.
- Ruling: inside a field result or list text, par/cell/row/sect become LineBreak inlines with one field_result_flattened warning per document; a stray container part is read as a formatting group so its text survives — cost if wrong: a flattened TOC shows line breaks instead of paragraphs, which is what a viewer can show anyway.
- Task 6: minor (deferred): upr drops its first child whatever it is; a cleaner rule skips every child that is not ud.
- Task 6: minor (deferred): a field, footnote or picture whose parent target is a name or instruction is dropped silently (defensible; a warning would make it visible).
- Task 6: minor (deferred): pibValue inherited by a nested shape; ShapeFrame::property not cleared per sp.
- Task 6: minor (deferred): pgnrestart without pgnstarts leaves pageNumberStart empty (RTF default start is 1).
- Task 6: minor (deferred): a pending hex half-digit survives a bin payload.
- Task 6: minor (deferred): no tests for unknown_picture_format, pgnrestart before pgnstarts, fldlock, wbitmap, picscaley, sect inside a header or footnote, input ending inside a frame; the 27-input malformed probe is not committed (Task 11 commits malformed fixtures).
- Task 6: minor (deferred): reader.cpp 2271 lines; a split needs a private header because Reader sits in an anonymous namespace.
- Task 6: minor (deferred): text of a stray container part inside a Field or Shape target lands in that target; untraced, untested.
- Task 7: minor (deferred): test_json.cpp covers about 60 percent of the writer's branches (tables, borders, shading, padding, field, footnote, shape, object, headers, info, generator, widowControl false, the b/f/r/t escapes untested); the fonts string-sort is not pinned by a test (Task 9's canonical check covers it).
- Task 8: minor (deferred): header-only table yields no edges and no column names; a zero-row part never joins and breaks the chain; two header-less parts still need the subset test (uncommented); a header-less page between two header pages starts a new table; zero-cell rows are spacers vacuously; json.cpp opens a second namespace rtf block; tests missing for zero-row part, header-only table, differing header cell counts, the _3 suffix, Bookmark/Picture/ColumnBreak/Shape/Object in inline_text.
- Task 9: minor (deferred): determinism check compares document JSON only; msword_disposition header-row check pins rows[0] only; sas_disposition column names pinned for column 0 only; nested-cell paragraph check partial; sas_table_3pages has 9/7/5 body rows instead of about ten; regen_goldens.py tracebacks on non-UTF-8 or non-JSON output; make_fixtures.py cut guard incomplete; orphan goldens undetected; narrow-path ifstream on Windows; chpgn absent from fixtures.
- Ruling: for the extractor, the nesttableprops group is transparent (it holds control words only, and its nestrow ends a nested row); the plan's projection paragraph never listed it as skipped, so only the dispatch was wrong — cost if wrong: none.
- Task 10: note for Task 12: add __pycache__/ to .gitignore.
- Task 10: minor (deferred): State.kids incremented for every group though only upr/shpinst use it; errors=replace on encode unreachable; a stranded comment and an undocumented backslash-newline branch are folded into fix round 1 as polish.
- Ruling: the mutation test prints its summary (mutants parsed, slowest parse per fixture) only when RTF_MUTATIONS_VERBOSE is set, so that test output stays pristine on success as the plan's convention says; the brief's 'prints' line yields to the convention — cost if wrong: one environment variable to see the numbers.
- Ruling: the large-input test's 21 MB stands in for the brief's 'about 32 MB'; the row text the controller fixed gives that size and the linear-time claim holds at 0.58 s — cost if wrong: none.
- Task 11: note: the memory cap floor of 1 MiB leaves an 8 percent margin for the deepest fixture; GroupState is about 950 bytes; candidate for the final review (raise the floor to 2 MiB or shrink GroupState).
- Ruling: the mutation test's memory cap floor rises from 1 MiB to 4 MiB with a comment (depth memory is a design constant that the 100-times-input rule cannot express; another standard library could tip the 8 percent margin) — cost if wrong: a looser floor for inputs under 40 KB.
- Ruling: two minors promoted into fix round 1 because they decide whether a failure can be reproduced or noticed: one RNG per fixture seeded from the seed and the file name; a zero or non-numeric RTF_MUTATIONS is a usage error and the test checks it parsed more than zero mutants — cost if wrong: a slightly larger fix diff.
- Task 11: minor (deferred): size + kHeader can wrap near SIZE_MAX in the allocation replacement (unreachable through containers); the timed window also covers destructors (uncommented); fuzz_parse.cpp uses uint8_t and size_t without std::.
- Task 11: minor (deferred): the zero-count usage message does not name RTF_MUTATIONS; under RTF_FUZZ the flags also instrument tests and tools (harmless); the fuzz build itself has never been compiled (no Clang here). Compiled and run 2026-11-17 with Visual Studio's Clang; see the last section.
- Ruling: test_large also honours an environment variable RTF_LARGE_SECONDS so the CI sanitizer job can widen its budget — cost if wrong: none.
- Ruling: the privacy rule wins over the dispatch wording: compare_text.py gains --quiet (index only) and the script prints only 'text <name>: differ at line N' — cost if wrong: less detail when a private sample disagrees; the user can run the two tools by hand.
- Ruling: two minors promoted (tool exit codes checked in the script; RTF_LARGE_SECONDS=240 in CI so the test's own message precedes the CTest timeout) — cost if wrong: none.
- Task 12: minor (deferred): strtod accepts inf and 30abc for RTF_LARGE_SECONDS; a doubled blank separator when expected.txt ends with an extra blank line.

## Final adjudication

- Adjudication of residuals: every deferred minor the final reviewer marked 'may wait' is parked for later plans — Ruling: parked, real but nothing downstream builds on them before plan 2; the fuzz target gets its first Clang compile when Massimo has Clang or CI gets a Linux Clang entry. The 'declined to judge' items (fields at the depth cap, text directly inside a field/shp/object group, header inside a header, sectd inside a header, 100x amplification for par-only streams, double-byte pages, styles not applied) — Ruling: parked, outside the design or non-producer input, no crash in any probe.

## 2026-11-17: the first fuzz run and the sanitizer build on Windows

Massimo pointed out that Visual Studio installs Clang (component "C++ Clang
tools for Windows": clang 19.1.5 with the AddressSanitizer, UBSan and
libFuzzer runtimes). Three changes made the sanitizer and fuzzer builds work
there, all in one commit after this note's date:

- `CMakeLists.txt` selects the static release C runtime for those builds on
  Windows, because Clang's runtimes are built against it, and warns when
  `RTF_SANITIZE` is set with the MSVC compiler, which ignores it.
- The libFuzzer build turns off the MSVC STL's container annotations
  (`_DISABLE_STRING_ANNOTATION`, `_DISABLE_VECTOR_ANNOTATION`). The prebuilt
  runtime was compiled without them and the linker folds its `std::string`
  code with ours; the mixed state produced one false container-overflow
  report in `logical_tables` after about 2,000 executions. The same input
  passed with the container check off, under a fuzzer built without the
  annotations, and under the plain sanitizer build, so it was not a reader
  bug. The plain sanitizer build keeps the annotations.
- `test_mutation.cpp` leaves `operator new` and `operator delete` to
  AddressSanitizer on Windows (`RTF_COUNTS_MEMORY` is 0 there), because the
  COFF linker refuses the second definition. The memory cap is not enforced
  in that one build; Linux ASan and every other build are unchanged.

Results on Massimo's machine: the full suite passes under Clang ASan and
UBSan on Windows (12 of 12 with `RTF_MUTATIONS=2000`, 44 s); the fuzzer ran
79,818 executions in 180 s seeded with the fixtures (coverage 3,353 edges,
corpus 2,185 inputs) with no finding. The README has the commands. CI still
has no Clang entry; the fuzzer stays a developer-machine tool.
