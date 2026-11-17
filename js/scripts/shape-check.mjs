// Checks at compile time that every committed golden JSON satisfies the
// model types. For each golden it writes a TypeScript file into build/shape
// that declares the JSON with `satisfies`, then runs tsc on those files.
// The goldens of inputs that are not RTF hold only {"error": "not_rtf"} and
// are skipped.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptsDir = path.dirname(fileURLToPath(import.meta.url));
const jsDir = path.join(scriptsDir, '..');
const root = path.join(scriptsDir, '..', '..');
const fixturesDir = path.join(root, 'test', 'fixtures');
const shapeDir = path.join(jsDir, 'build', 'shape');

const NOT_RTF = '{"error": "not_rtf"}\n';

fs.rmSync(shapeDir, { recursive: true, force: true });
fs.mkdirSync(shapeDir, { recursive: true });

let count = 0;
for (const file of fs.readdirSync(fixturesDir).sort()) {
  if (!file.endsWith('.json')) continue;
  const text = fs.readFileSync(path.join(fixturesDir, file), 'utf8');
  if (text === NOT_RTF) continue;
  const isTables = file.endsWith('.tables.json');
  const typeName = isTables ? 'LogicalTable' : 'RtfDocument';
  const typeExpr = isTables ? 'LogicalTable[]' : 'RtfDocument';
  const source =
    `import type { ${typeName} } from '../../src/model.js';\n` +
    `const doc = ${text.trimEnd()} satisfies ${typeExpr};\n` +
    `export default doc;\n`;
  const name = file.slice(0, -'.json'.length);
  fs.writeFileSync(path.join(shapeDir, `${name}.ts`), source);
  count += 1;
}
console.log(`wrote ${count} files to ${shapeDir}`);

const tsc = path.join(jsDir, 'node_modules', 'typescript', 'bin', 'tsc');
const result = spawnSync(process.execPath, [tsc, '-p', 'tsconfig.shape.json'], {
  cwd: jsDir,
  stdio: 'inherit',
});
if (result.error) {
  console.error(result.error.message);
  process.exit(1);
}
process.exit(result.status ?? 1);
