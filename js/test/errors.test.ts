import { test } from 'node:test';
import assert from 'node:assert/strict';
import { load, RtfError } from '../src/index.js';
import { readFixture, readGolden, rtfBytes } from './helpers.js';

function assertNotRtf(e: unknown): boolean {
  assert.ok(e instanceof Error);
  assert.ok(e instanceof RtfError);
  assert.equal(e.name, 'RtfError');
  assert.equal(e.code, 'not_rtf');
  assert.equal(e.message, 'not_rtf');
  return true;
}

test('empty_input_is_not_rtf', async () => {
  const rtf = await load();
  assert.throws(() => rtf.parse(new Uint8Array(0)), assertNotRtf);
  assert.throws(() => rtf.parse(new ArrayBuffer(0)), assertNotRtf);
  assert.throws(() => rtf.logicalTables(new Uint8Array(0)), assertNotRtf);
});

test('text_is_not_rtf', async () => {
  const rtf = await load();
  assert.throws(() => rtf.parse(rtfBytes('hello')), assertNotRtf);
  assert.throws(() => rtf.logicalTablesJson(rtfBytes('hello')), assertNotRtf);
});

test('wrong_input_type_is_invalid_argument', async () => {
  // A fresh instance, so that a call into the wasm could not hide behind
  // the other tests. The type check throws before any allocation.
  const rtf = await load({});
  const isInvalid = (e: unknown): boolean => e instanceof RtfError && e.code === 'invalid_argument';
  assert.throws(() => rtf.parse('x' as unknown as Uint8Array), isInvalid);
  assert.throws(() => rtf.parseToJson(null as unknown as Uint8Array), isInvalid);
  assert.throws(() => rtf.logicalTables([1, 2] as unknown as Uint8Array), isInvalid);
  // The instance still works afterwards.
  assert.equal(rtf.parseToJson(readFixture('word_minimal')), readGolden('word_minimal', '.json'));
});
