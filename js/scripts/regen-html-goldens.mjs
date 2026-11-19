// Writes js/test/goldens/<name>.html: the rendering of every fixture that
// parses, with the default options. The fixtures that are not RTF have no
// HTML golden. Run it after npm run build, then review the diff like code.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { load, renderHtml } from '../dist/src/index.js';

const scriptsDir = path.dirname(fileURLToPath(import.meta.url));
const fixturesDir = path.join(scriptsDir, '..', '..', 'test', 'fixtures');
const goldensDir = path.join(scriptsDir, '..', 'test', 'goldens');

const NOT_RTF = '{"error": "not_rtf"}\n';

const rtf = await load();
fs.mkdirSync(goldensDir, { recursive: true });
let count = 0;
for (const file of fs.readdirSync(fixturesDir).sort()) {
  if (!file.endsWith('.rtf')) continue;
  const name = file.slice(0, -'.rtf'.length);
  const golden = fs.readFileSync(path.join(fixturesDir, name + '.json'), 'utf8');
  if (golden === NOT_RTF) continue;
  const bytes = new Uint8Array(fs.readFileSync(path.join(fixturesDir, file)));
  // renderHtml writes LF line ends only, so the bytes go out as they are.
  fs.writeFileSync(path.join(goldensDir, name + '.html'), renderHtml(rtf.parse(bytes)));
  count += 1;
}
console.log(`wrote ${count} files to ${goldensDir}`);
