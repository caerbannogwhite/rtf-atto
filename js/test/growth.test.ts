import { test } from 'node:test';
import assert from 'node:assert/strict';
import { load } from '../src/index.js';
import { readFixture, readGolden, rtfBytes } from './helpers.js';

// A large table makes the wasm memory grow during the call. The next call
// on the same instance must still read the right bytes.
test('growth_then_second_parse', async () => {
  const rtf = await load();
  const parts = ['{\\rtf1\\ansi'];
  for (let i = 0; i < 20000; i++) {
    parts.push(
      `\\trowd\\cellx3000\\cellx6000\\cellx9000 r${i}c0\\cell r${i}c1\\cell r${i}c2\\cell\\row`,
    );
  }
  parts.push('}');
  const tables = rtf.logicalTables(rtfBytes(parts.join('')));
  assert.equal(tables.length, 1);
  const rows = tables[0]!.rows;
  assert.equal(rows.length, 20000);
  assert.equal(rows[19999]!.cells[2]!.text, 'r19999c2');

  assert.equal(rtf.parseToJson(readFixture('word_minimal')), readGolden('word_minimal', '.json'));
});
