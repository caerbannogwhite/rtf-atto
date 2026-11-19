import { test } from 'node:test';
import assert from 'node:assert/strict';
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
