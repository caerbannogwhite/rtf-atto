// Shared helpers for the node tests. The paths are relative to the
// compiled file in dist/test/.
import { readdirSync, readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

// The repository's test/fixtures/ directory.
export const fixturesDir = new URL('../../../test/fixtures/', import.meta.url);

// The golden of a fixture that is not RTF.
export const NOT_RTF = '{"error": "not_rtf"}\n';

// The names of the .rtf fixtures, without the extension, sorted.
export function listFixtures(): string[] {
  return readdirSync(fixturesDir)
    .filter((file) => file.endsWith('.rtf'))
    .map((file) => file.slice(0, -'.rtf'.length))
    .sort();
}

export function readFixture(name: string): Uint8Array {
  return new Uint8Array(readFileSync(new URL(name + '.rtf', fixturesDir)));
}

export function readGolden(name: string, suffix: '.json' | '.tables.json'): string {
  return readFileSync(new URL(name + suffix, fixturesDir), 'utf8');
}

export function rtfBytes(text: string): Uint8Array {
  return new TextEncoder().encode(text);
}

export function packageJson(): { version: string; exports: unknown; engines: { node: string } } {
  return JSON.parse(readFileSync(new URL('../../package.json', import.meta.url), 'utf8'));
}

// A path under dist/src/.
export function distFile(name: string): string {
  return fileURLToPath(new URL('../src/' + name, import.meta.url));
}
