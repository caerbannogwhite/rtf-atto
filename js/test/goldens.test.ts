import { test } from 'node:test';
import assert from 'node:assert/strict';
import { load, RtfError } from '../src/index.js';
import { listFixtures, NOT_RTF, readFixture, readGolden } from './helpers.js';

const isNotRtf = (e: unknown): boolean => e instanceof RtfError && e.code === 'not_rtf';

test('every_fixture_matches_its_goldens', async () => {
  const rtf = await load();
  const names = listFixtures();
  assert.equal(names.length, 19);
  for (const name of names) {
    const bytes = readFixture(name);
    const golden = readGolden(name, '.json');
    const tablesGolden = readGolden(name, '.tables.json');
    if (golden === NOT_RTF) {
      assert.throws(() => rtf.parse(bytes), isNotRtf, name);
      assert.throws(() => rtf.logicalTablesJson(bytes), isNotRtf, name);
      continue;
    }
    assert.equal(rtf.parseToJson(bytes), golden, name);
    assert.equal(rtf.logicalTablesJson(bytes), tablesGolden, name);
    assert.deepEqual(rtf.parse(bytes), JSON.parse(golden), name);
    assert.deepEqual(rtf.logicalTables(bytes), JSON.parse(tablesGolden), name);
  }
});

test('parse_is_deterministic', async () => {
  const rtf = await load();
  const bytes = readFixture('msword_features');
  assert.equal(rtf.parseToJson(bytes), rtf.parseToJson(bytes));
});
