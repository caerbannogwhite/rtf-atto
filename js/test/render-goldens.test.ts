import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { load, renderHtml } from '../src/index.js';
import type { Block, Inline, RtfDocument } from '../src/model.js';
import { listFixtures, NOT_RTF, readFixture, readGolden } from './helpers.js';
import { stripTags } from './html-helpers.js';

// js/test/goldens/, from the compiled file in dist/test/.
const goldensDir = new URL('../../test/goldens/', import.meta.url);

// The fixtures that parse: every fixture whose golden is not not_rtf.
function parsingFixtures(): string[] {
  return listFixtures().filter((name) => readGolden(name, '.json') !== NOT_RTF);
}

test('every_fixture_matches_its_html_golden', async () => {
  const rtf = await load();
  const names = parsingFixtures();
  assert.equal(names.length, 17);
  for (const name of names) {
    const golden = readFileSync(new URL(name + '.html', goldensDir), 'utf8');
    assert.equal(renderHtml(rtf.parse(readFixture(name))), golden, name);
  }
});

// The text of a run that the renderer must show: not hidden, not blank.
function shownText(inline: Inline): string | undefined {
  if (inline.kind !== 'text' || inline.props.hidden === true || inline.text.trim() === '') return undefined;
  return inline.text;
}

// The list text shows the Symbol-font bullet U+F0B7 as U+2022.
function listInlines(listText: Inline[] | undefined): Inline[] {
  return (listText ?? []).map((inline) =>
    inline.kind === 'text' ? { ...inline, text: inline.text.replace(/\uf0b7/g, '\u2022') } : inline);
}

function shownCells(blocks: Block[], visit: (cellBlocks: Block[]) => void): void {
  for (const block of blocks) {
    if (block.kind !== 'table') continue;
    for (const row of block.rows) {
      for (const cell of row.cells) {
        if (cell.mergeVertical === 'continued' || cell.mergeHorizontal === 'continued') continue;
        visit(cell.blocks);
      }
    }
  }
}

// Every shown text under blocks, including the blocks of footnotes,
// shapes and objects.
function allTexts(blocks: Block[], out: string[]): void {
  const inlines = (list: Inline[]): void => {
    for (const inline of list) {
      const text = shownText(inline);
      if (text !== undefined) out.push(text);
      if (inline.kind === 'field') inlines(inline.result);
      if (inline.kind === 'footnote' || inline.kind === 'shape') allTexts(inline.blocks, out);
      if (inline.kind === 'object') allTexts(inline.result, out);
    }
  };
  for (const block of blocks) {
    if (block.kind === 'paragraph') {
      inlines(listInlines(block.listText));
      inlines(block.runs);
    }
  }
  shownCells(blocks, (cellBlocks) => allTexts(cellBlocks, out));
}

// The first shown text of an inline list, looking into field results only.
function firstText(list: Inline[]): string | undefined {
  for (const inline of list) {
    const text = inline.kind === 'field' ? firstText(inline.result) : shownText(inline);
    if (text !== undefined) return text;
  }
  return undefined;
}

// The first shown text of each paragraph, in document order, with table
// cells in row order. Footnotes, shapes and objects are left out: a note
// is listed at the end of its page.
function firstTexts(blocks: Block[], out: string[]): void {
  for (const block of blocks) {
    if (block.kind === 'paragraph') {
      const text = firstText([...listInlines(block.listText), ...block.runs]);
      if (text !== undefined) out.push(text);
    } else {
      shownCells([block], (cellBlocks) => firstTexts(cellBlocks, out));
    }
  }
}

test('html_contains_every_text_run_in_order', async () => {
  const rtf = await load();
  const names = parsingFixtures();
  assert.equal(names.length, 17);
  for (const name of names) {
    const document: RtfDocument = rtf.parse(readFixture(name));
    const text = stripTags(renderHtml(document));
    // The sections render in order too, so the position carries over.
    let position = 0;
    for (const section of document.sections) {
      const all: string[] = [];
      allTexts(section.body, all);
      for (const run of all) assert.ok(text.includes(run), `${name}: ${JSON.stringify(run)} is missing`);

      const firsts: string[] = [];
      firstTexts(section.body, firsts);
      for (const first of firsts) {
        const found = text.indexOf(first, position);
        assert.ok(found >= 0, `${name}: ${JSON.stringify(first)} is not at or after position ${position}`);
        position = found + first.length;
      }
    }
  }
});
