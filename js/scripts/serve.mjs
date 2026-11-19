// A small static server for the demo page in js/demo. It serves the
// repository root read-only. It answers GET only, lists no directories, and
// answers GET /api/fixtures with the names of the .rtf files in test/fixtures.
// Run as a script, it listens on 127.0.0.1 and prints the URL of the page.
import fs from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.wasm': 'application/wasm',
  '.rtf': 'application/rtf',
};
const TEXT = 'text/plain; charset=utf-8';

function contentType(file) {
  return TYPES[path.extname(file).toLowerCase()] ?? 'application/octet-stream';
}

// True when target is root itself or a path below it.
function isInside(root, target) {
  const relative = path.relative(root, target);
  return !relative.startsWith('..') && !path.isAbsolute(relative);
}

function send(response, status, type, body, headers = {}) {
  response.writeHead(status, {
    'Content-Type': type,
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
    ...headers,
  });
  response.end(body);
}

function notFound(response) {
  send(response, 404, TEXT, 'not found\n');
}

async function sendFixtures(root, response) {
  const entries = await fs.readdir(path.join(root, 'test', 'fixtures'), { withFileTypes: true });
  const names = entries
    .filter((entry) => entry.isFile() && entry.name.endsWith('.rtf'))
    .map((entry) => entry.name.slice(0, -'.rtf'.length))
    .sort();
  send(response, 200, TYPES['.json'], JSON.stringify(names));
}

// The real path of the file a request names, or undefined when there is
// none inside the root. A directory gives its index.html. Symbolic links
// are resolved, and a link that leads out of the root counts as missing.
async function resolveFile(realRoot, target) {
  let real;
  try {
    real = await fs.realpath(target);
  } catch {
    return undefined;
  }
  if (!isInside(realRoot, real)) return undefined;
  const stat = await fs.stat(real);
  if (stat.isDirectory()) return resolveFile(realRoot, path.join(real, 'index.html'));
  return stat.isFile() ? real : undefined;
}

async function handle(root, request, response) {
  if (request.method !== 'GET') {
    send(response, 405, TEXT, 'method not allowed\n', { Allow: 'GET' });
    return;
  }
  const rawPath = (request.url ?? '/').split('?')[0];
  if (rawPath === '/api/fixtures') {
    await sendFixtures(root, response);
    return;
  }
  let decoded;
  try {
    decoded = decodeURIComponent(rawPath);
  } catch {
    send(response, 400, TEXT, 'bad request\n');
    return;
  }
  if (decoded.includes('\0')) {
    send(response, 400, TEXT, 'bad request\n');
    return;
  }
  // A backslash is a separator on Windows, so it is read as a slash before
  // the dot segments are removed.
  const normalized = path.posix.normalize(decoded.replaceAll('\\', '/'));
  const target = path.join(root, normalized);
  if (!isInside(root, target)) {
    notFound(response);
    return;
  }
  const file = await resolveFile(await fs.realpath(root), target);
  if (file === undefined) {
    notFound(response);
    return;
  }
  const body = await fs.readFile(file);
  send(response, 200, contentType(file), body);
}

// Returns an http.Server over root that is not yet listening.
export function createDemoServer(root) {
  const absoluteRoot = path.resolve(root);
  return http.createServer((request, response) => {
    handle(absoluteRoot, request, response).catch(() => {
      if (response.headersSent) response.destroy();
      else send(response, 500, TEXT, 'internal error\n');
    });
  });
}

const runAsScript =
  process.argv[1] !== undefined && fileURLToPath(import.meta.url) === path.resolve(process.argv[1]);

if (runAsScript) {
  const root = path.join(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
  const port = Number(process.env.PORT) || 8787;
  const server = createDemoServer(root);
  server.listen(port, '127.0.0.1', () => {
    console.log(`http://127.0.0.1:${server.address().port}/js/demo/`);
  });
}
