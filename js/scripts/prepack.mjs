// Runs before npm pack and npm publish.
// It checks that the build output exists and copies the repository's
// LICENSE into the package directory.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptsDir = path.dirname(fileURLToPath(import.meta.url));
const jsDir = path.join(scriptsDir, '..');
const root = path.join(scriptsDir, '..', '..');

const required = [
  path.join(jsDir, 'dist', 'src', 'index.js'),
  path.join(jsDir, 'dist', 'src', 'rtf.wasm'),
];
if (!required.every((file) => fs.existsSync(file))) {
  console.error('run npm run build first');
  process.exit(1);
}

fs.copyFileSync(path.join(root, 'LICENSE'), path.join(jsDir, 'LICENSE'));
