import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { load } from '../src/index.js';
import { distFile, packageJson } from './helpers.js';

test('version_matches_package_json', async () => {
  const rtf = await load();
  assert.equal(rtf.version, packageJson().version);
});

test('wasm_is_under_the_ceiling', (t) => {
  const size = statSync(distFile('rtf.wasm')).size;
  t.diagnostic(`rtf.wasm is ${size} bytes`);
  assert.ok(size < 400 * 1024);
});

// Collects every string value of the exports map, at any depth.
function exportPaths(value: unknown): string[] {
  if (typeof value === 'string') return [value];
  if (value !== null && typeof value === 'object') {
    return Object.values(value).flatMap(exportPaths);
  }
  return [];
}

test('exports_point_at_existing_files', () => {
  const jsDir = new URL('../../', import.meta.url);
  const paths = exportPaths(packageJson().exports);
  assert.ok(paths.length > 0);
  for (const path of paths) {
    const file = fileURLToPath(new URL(path, jsDir));
    assert.ok(existsSync(file), `${path} does not exist`);
  }
});

test('engines_is_node_22_or_later', () => {
  assert.equal(packageJson().engines.node, '>=22');
});
