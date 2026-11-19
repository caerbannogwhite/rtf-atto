// Copies the files that tsc does not emit into dist/src: the stylesheet.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const jsDir = path.join(path.dirname(fileURLToPath(import.meta.url)), '..');
const outDir = path.join(jsDir, 'dist', 'src');

fs.mkdirSync(outDir, { recursive: true });
fs.copyFileSync(path.join(jsDir, 'src', 'rtf.css'), path.join(outDir, 'rtf.css'));
