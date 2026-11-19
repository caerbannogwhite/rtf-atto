import { test } from 'node:test';
import assert from 'node:assert/strict';
import { load } from '../src/index.js';
import type { RtfDocument } from '../src/model.js';
import { logicalTables } from '../src/tables.js';
import { cell, doc, para, row, table } from './builders.js';
import { listFixtures, NOT_RTF, readFixture, readGolden } from './helpers.js';

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

test('a_part_without_headers_joins_on_subset_edges', () => {
  const first = table([
    row([cell(3000, ['A']), cell(6000, ['B'])], { header: true }),
    row([cell(3000, ['1']), cell(6000, ['2'])]),
  ]);
  const second = table([row([cell(3000, ['3']), cell(6000, ['4'])])]);
  const third = table([row([cell(4000, ['5'])])]);
  const tables = logicalTables(doc([{ body: [first] }, { body: [second, third] }]));
  assert.equal(tables.length, 2);
  assert.equal(tables[0]!.parts.length, 2);
  assert.equal(tables[0]!.rows.length, 2);
  assert.deepEqual(tables[0]!.parts, [
    { section: 0, block: 0 },
    { section: 1, block: 0 },
  ]);
  assert.equal(tables[1]!.id, 2);
  assert.deepEqual(tables[1]!.parts, [{ section: 1, block: 1 }]);
});

test('a_text_paragraph_between_parts_splits_them', () => {
  const part = (): ReturnType<typeof table> => table([row([cell(3000, ['a']), cell(6000, ['b'])])]);
  assert.equal(logicalTables(doc([part(), para(['x']), part()])).length, 2);
  assert.equal(logicalTables(doc([part(), para([]), part()])).length, 1);
});

test('column_names_prefix_and_dedupe', () => {
  const grouped = table([
    row([cell(6000, ['Group A']), cell(9000, [''])], { header: true }),
    row([cell(3000, ['x']), cell(6000, ['y']), cell(9000, ['z'])], { header: true }),
    row([cell(3000, ['1']), cell(6000, ['2']), cell(9000, ['3'])]),
  ]);
  assert.deepEqual(logicalTables(doc([grouped]))[0]!.columnNames, ['Group A x', 'Group A y', 'z']);

  const repeated = table([
    row([cell(3000, ['n']), cell(6000, ['n']), cell(9000, [''])], { header: true }),
    row([cell(3000, ['1']), cell(6000, ['2']), cell(9000, ['3'])]),
  ]);
  assert.deepEqual(logicalTables(doc([repeated]))[0]!.columnNames, ['n', 'n_2', 'column_3']);
});

test('blank_cells_make_a_spacer_row', () => {
  const t = table([
    row([cell(3000, ['']), cell(6000, ['\u00a0'])]),
    row([cell(3000, ['a']), cell(6000, [''])]),
  ]);
  const rows = logicalTables(doc([t]))[0]!.rows;
  assert.equal(rows[0]!.spacer, true);
  assert.equal(rows[1]!.spacer, false);
});

test('indent_is_the_first_paragraphs_indent_left', () => {
  const t = table([
    row([cell(3000, [para(['a'], { indentLeft: 288 })]), cell(6000, ['b'])]),
    row([cell(3000, []), cell(6000, ['c'])]),
  ]);
  const rows = logicalTables(doc([t]))[0]!.rows;
  assert.equal(rows[0]!.indent, 288);
  assert.equal(rows[1]!.indent, 0);
});
