import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { load, RtfError } from '../src/index.js';
import type { RtfAtto } from '../src/index.js';
import { distFile, readFixture, readGolden } from './helpers.js';

function assertWorks(rtf: RtfAtto): void {
  assert.equal(rtf.version, '0.1.0');
  assert.equal(rtf.parseToJson(readFixture('word_minimal')), readGolden('word_minimal', '.json'));
}

test('load_without_options_is_shared', async () => {
  const a = await load();
  const b = await load();
  assert.equal(a, b);
  assert.equal(await load(undefined), a);
});

test('load_with_bytes_is_a_new_instance', async () => {
  const shared = await load();
  const rtf = await load({ wasm: readFileSync(distFile('rtf.wasm')) });
  assert.notEqual(rtf, shared);
  assertWorks(rtf);
});

test('load_with_a_file_url', async () => {
  const rtf = await load({ wasm: new URL(pathToFileURL(distFile('rtf.wasm'))) });
  assertWorks(rtf);
});

test('load_with_a_string', async () => {
  const rtf = await load({ wasm: './rtf.wasm' });
  assertWorks(rtf);
});

test('load_with_bad_bytes_rejects', async () => {
  await assert.rejects(
    load({ wasm: new Uint8Array([0, 1, 2, 3]) }),
    (e: unknown) => e instanceof RtfError && e.code === 'load_failed',
  );
});
