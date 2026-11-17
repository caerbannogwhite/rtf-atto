// Builds the WebAssembly reader with Emscripten and copies it into dist/src.
// The build directory is RTF_WASM_BUILD_DIR, else <root>/build-wasm.
// Emscripten (emcmake) and CMake must be on PATH. On Windows, Ninja must be
// on PATH too, because the default Visual Studio generator does not work
// with emcmake. Other platforms use the default CMake generator.
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptsDir = path.dirname(fileURLToPath(import.meta.url));
const jsDir = path.join(scriptsDir, '..');
const root = path.join(scriptsDir, '..', '..');
const buildDir = process.env.RTF_WASM_BUILD_DIR
  ? path.resolve(process.env.RTF_WASM_BUILD_DIR)
  : path.join(root, 'build-wasm');
const sourceDir = path.join(root, 'wasm');

// Runs one step and exits with the child's status when it fails.
function run(result) {
  if (result.error) {
    console.error(result.error.message);
    process.exit(1);
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

const generator = process.platform === 'win32' ? ['-G', 'Ninja'] : [];
const configureArgs = ['cmake', '-S', sourceDir, '-B', buildDir, ...generator, '-DCMAKE_BUILD_TYPE=Release'];
const buildArgs = ['--build', buildDir];

if (process.platform === 'win32') {
  // A .bat file needs a shell. One command string avoids Node's warning
  // about passing arguments together with shell: true.
  const quote = (s) => (/[\s"]/.test(s) ? `"${s}"` : s);
  run(spawnSync(['emcmake.bat', ...configureArgs].map(quote).join(' '), { shell: true, stdio: 'inherit' }));
  run(spawnSync(['cmake', ...buildArgs].map(quote).join(' '), { shell: true, stdio: 'inherit' }));
} else {
  run(spawnSync('emcmake', configureArgs, { shell: false, stdio: 'inherit' }));
  run(spawnSync('cmake', buildArgs, { shell: false, stdio: 'inherit' }));
}

const outDir = path.join(jsDir, 'dist', 'src');
fs.mkdirSync(outDir, { recursive: true });
fs.copyFileSync(path.join(buildDir, 'rtf.js'), path.join(outDir, 'rtf.js'));
fs.copyFileSync(path.join(buildDir, 'rtf.wasm'), path.join(outDir, 'rtf.wasm'));
// tsc does not emit declaration files that are inputs, so copy this one.
fs.copyFileSync(path.join(jsDir, 'src', 'rtf.d.ts'), path.join(outDir, 'rtf.d.ts'));
console.log(`copied rtf.js, rtf.wasm and rtf.d.ts to ${outDir}`);
