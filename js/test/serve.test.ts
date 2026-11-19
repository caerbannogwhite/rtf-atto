import { test } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import path from 'node:path';
import type { AddressInfo } from 'node:net';
import { fileURLToPath } from 'node:url';
import type * as Serve from '../scripts/serve.mjs';

// The compiled test sits in dist/test, three levels below the repository
// root. tsc resolves the type import above from js/test; at run time the
// script is two levels up from dist/test, so it is imported by that path.
const root = fileURLToPath(new URL('../../../', import.meta.url));
const { createDemoServer } = (await import(
  new URL('../../scripts/serve.mjs', import.meta.url).href
)) as typeof Serve;

// fetch normalises the URL before it sends it, so the dot segments never
// reach the server. This sends the path as it is written.
function rawStatus(port: number, path: string): Promise<number> {
  return new Promise((resolve, reject) => {
    const request = http.get({ host: '127.0.0.1', port, path }, (response) => {
      response.resume();
      response.on('end', () => resolve(response.statusCode ?? 0));
    });
    request.on('error', reject);
  });
}

test('demo_server_serves_the_root_only', async () => {
  const server = createDemoServer(root);
  await new Promise<void>((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => resolve());
  });
  try {
    const port = (server.address() as AddressInfo).port;
    const base = `http://127.0.0.1:${port}`;

    const fixtures = await fetch(`${base}/api/fixtures`);
    assert.equal(fixtures.status, 200);
    assert.equal(fixtures.headers.get('content-type'), 'application/json; charset=utf-8');
    assert.equal(fixtures.headers.get('cache-control'), 'no-store');
    const names = (await fixtures.json()) as unknown[];
    assert.ok(Array.isArray(names));
    assert.ok(names.includes('word_minimal'));
    for (const name of names) {
      assert.equal(typeof name, 'string');
      assert.match(name as string, /^[A-Za-z0-9_-]+$/);
    }
    assert.ok(!names.includes('word_minimal.tables'));
    assert.deepEqual(names, [...names].sort());

    const page = await fetch(`${base}/js/demo/index.html`);
    assert.equal(page.status, 200);
    assert.equal(page.headers.get('content-type'), 'text/html; charset=utf-8');
    await page.arrayBuffer();

    const wasm = await fetch(`${base}/js/dist/src/rtf.wasm`);
    assert.equal(wasm.status, 200);
    assert.equal(wasm.headers.get('content-type'), 'application/wasm');
    await wasm.arrayBuffer();

    for (const outside of ['/../package.json', '/%2e%2e/package.json']) {
      const response = await fetch(`${base}${outside}`);
      assert.equal(response.status, 404, outside);
      assert.equal(response.headers.get('cache-control'), 'no-store');
      await response.arrayBuffer();
    }

    // A server that joined the raw path to the root would serve these
    // files: the first three climb out of the root and back into it.
    const folder = encodeURIComponent(path.basename(root));
    for (const outside of [
      `/../${folder}/README.md`,
      `/%2e%2e/${folder}/README.md`,
      `/..%5C${folder}%5CREADME.md`,
      '/C:/Windows/win.ini',
    ]) {
      assert.equal(await rawStatus(port, outside), 404, outside);
    }
    assert.equal(await rawStatus(port, '/%E0%A4%A'), 400);

    const index = await fetch(`${base}/js/demo/`);
    assert.equal(index.status, 200);
    assert.equal(index.headers.get('content-type'), 'text/html; charset=utf-8');
    await index.arrayBuffer();

    const listing = await fetch(`${base}/js/`);
    assert.equal(listing.status, 404);
    await listing.arrayBuffer();

    const post = await fetch(`${base}/js/demo/index.html`, { method: 'POST' });
    assert.equal(post.status, 405);
    assert.equal(post.headers.get('allow'), 'GET');
    assert.equal(post.headers.get('cache-control'), 'no-store');
    await post.arrayBuffer();
  } finally {
    await new Promise<void>((resolve) => server.close(() => resolve()));
  }
});
