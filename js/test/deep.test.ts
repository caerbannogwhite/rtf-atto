import { test } from 'node:test';
import assert from 'node:assert/strict';
import { load } from '../src/index.js';
import type { Inline, Paragraph } from '../src/index.js';
import { rtfBytes } from './helpers.js';

function documentOf(body: string): Uint8Array {
  return rtfBytes('{\\rtf1\\ansi ' + body + '\\par}');
}

function firstParagraph(blocks: unknown[]): Paragraph {
  const block = blocks[0] as Paragraph;
  assert.equal(block.kind, 'paragraph');
  return block;
}

test('deep_nesting_parses', async () => {
  const rtf = await load();

  // 1000 nested fields around x. Each field opens two groups, and the
  // reader flattens groups deeper than its default maxDepth of 1024. So the
  // model keeps 512 fields, the innermost one is empty, and the document
  // has a depth_limit warning. The native rtf2json gives the same JSON.
  let fields = 'x';
  for (let i = 0; i < 1000; i++) {
    fields = '{\\field{\\*\\fldinst F}{\\fldrslt ' + fields + '}}';
  }
  const doc = rtf.parse(documentOf(fields));
  let inline: Inline | undefined = firstParagraph(doc.sections[0]!.body).runs[0];
  let innermost: Inline | undefined;
  let depth = 0;
  while (inline !== undefined && inline.kind === 'field') {
    depth++;
    innermost = inline;
    inline = inline.result[0];
  }
  assert.equal(depth, 512);
  assert.ok(innermost !== undefined && innermost.kind === 'field');
  assert.deepEqual(innermost.result, []);
  assert.ok(doc.warnings.some((w) => w.code === 'depth_limit'));

  // 1000 nested footnotes around x.
  let footnotes = 'x';
  for (let i = 0; i < 1000; i++) {
    footnotes = 'a{\\footnote ' + footnotes + '\\par}';
  }
  assert.ok(rtf.parseToJson(documentOf(footnotes)).length > 1_000_000);

  // 5000 nested groups around x.
  const groups = '{'.repeat(5000) + 'x' + '}'.repeat(5000);
  const flat = rtf.parse(documentOf(groups));
  const body = flat.sections[0]!.body;
  assert.equal(body.length, 1);
  const runs = firstParagraph(body).runs;
  const text = runs.map((run) => (run.kind === 'text' ? run.text : '')).join('');
  assert.equal(text, 'x');
});
