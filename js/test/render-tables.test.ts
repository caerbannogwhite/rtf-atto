import { test } from 'node:test';
import assert from 'node:assert/strict';
import { renderHtml } from '../src/index.js';
import type { Block, RtfDocument } from '../src/model.js';
import { cellText } from '../src/text.js';
import { cell, doc, row, table } from './builders.js';
import { readGolden } from './helpers.js';
import { count, stripTags } from './html-helpers.js';

// The padding of a cell with no padding in a row with no gapHalf.
const PAD = 'padding: 0px 7.2px 0px 7.2px';

test('grid_from_the_union_of_edges', () => {
  const html = renderHtml(doc([table([
    row([cell(3000, ['a']), cell(6000, ['b'])]),
    row([cell(2000, ['c']), cell(6000, ['d'])]),
  ])]));
  assert.ok(html.includes(
    '<table class="rtf-table" style="width: 400px; table-layout: fixed; border-collapse: collapse">\n'
      + '<colgroup><col style="width: 133.33px"><col style="width: 66.67px"><col style="width: 200px"></colgroup>\n'
      + '<tbody>\n'
      + `<tr><td colspan="2" style="${PAD}"><p class="rtf-p">a</p>\n</td><td style="${PAD}"><p class="rtf-p">b</p>\n</td></tr>\n`
      + `<tr><td style="${PAD}"><p class="rtf-p">c</p>\n</td><td colspan="2" style="${PAD}"><p class="rtf-p">d</p>\n</td></tr>\n`
      + '</tbody>\n'
      + '</table>\n',
  ));
});

test('header_rows_in_thead_as_th', () => {
  const html = renderHtml(doc([table([
    row([cell(3000, ['h1'])], { header: true }),
    row([cell(3000, ['h2'])], { header: true }),
    row([cell(3000, ['x'])]),
  ])]));
  assert.equal(count(html, '<thead>'), 1);
  assert.equal(count(html, '<tbody>'), 1);
  const thead = html.slice(html.indexOf('<thead>'), html.indexOf('</thead>'));
  const tbody = html.slice(html.indexOf('<tbody>'), html.indexOf('</tbody>'));
  assert.equal(count(thead, '<tr'), 2);
  assert.equal(count(thead, '<th '), 2);
  assert.equal(count(thead, '<td'), 0);
  assert.equal(count(tbody, '<tr'), 1);
  assert.equal(count(tbody, '<td '), 1);
  assert.ok(thead.includes(`<th style="${PAD}"><p class="rtf-p">h1</p>\n</th>`));
});

test('horizontal_merge_extends_colspan', () => {
  const html = renderHtml(doc([table([
    row([
      cell(3000, ['a'], { mergeHorizontal: 'first' }),
      cell(6000, ['gone'], { mergeHorizontal: 'continued' }),
      cell(9000, ['x']),
    ]),
  ])]));
  assert.ok(html.includes(
    `<tr><td colspan="2" style="${PAD}"><p class="rtf-p">a</p>\n</td><td style="${PAD}"><p class="rtf-p">x</p>\n</td></tr>\n`,
  ));
  assert.equal(count(html, '<td'), 2);
  assert.ok(!html.includes('gone'));
});

test('vertical_merge_sets_rowspan_and_skips_continued', () => {
  const html = renderHtml(doc([table([
    row([cell(3000, ['a'], { mergeVertical: 'first' }), cell(6000, ['b'])]),
    row([cell(3000, ['gone'], { mergeVertical: 'continued' }), cell(6000, ['d'])]),
  ])]));
  assert.ok(html.includes(
    '<tbody>\n'
      + `<tr><td rowspan="2" style="${PAD}"><p class="rtf-p">a</p>\n</td><td style="${PAD}"><p class="rtf-p">b</p>\n</td></tr>\n`
      + `<tr><td style="${PAD}"><p class="rtf-p">d</p>\n</td></tr>\n`
      + '</tbody>\n',
  ));
  assert.ok(!html.includes('gone'));
});

test('malformed_merges_render_as_plain_cells', () => {
  const html = renderHtml(doc([table([
    row([cell(3000, ['v'], { mergeVertical: 'continued' }), cell(6000, ['b'])]),
    row([cell(3000, ['h'], { mergeHorizontal: 'continued' }), cell(6000, ['d'])]),
  ])]));
  assert.ok(html.includes(`<td style="${PAD}"><p class="rtf-p">v</p>\n</td>`));
  assert.ok(html.includes(`<td style="${PAD}"><p class="rtf-p">h</p>\n</td>`));
  assert.equal(count(html, '<td'), 4);
  assert.ok(!html.includes('rowspan'));
  assert.ok(!html.includes('colspan'));
});

test('cell_styles', () => {
  const html = renderHtml(doc([
    table([row([cell(3000, ['x'], {
      shading: { background: 1 },
      verticalAlign: 'bottom',
      borders: {
        top: { style: 'single', width: 10 },
        left: { style: 'double', width: 30, color: 2 },
      },
      noWrap: true,
    })], { borders: { bottom: { style: 'dotted', width: 15 } } })]),
    table([row([cell(3000, ['y'])], { gapHalf: 60 })]),
  ]));
  assert.ok(html.includes(
    '<td style="vertical-align: bottom; padding: 0px 7.2px 0px 7.2px; background-color: rgb(255, 0, 0); '
      + 'border-top: 1px solid; border-bottom: 1px dotted; border-left: 2px double rgb(0, 0, 255); '
      + 'white-space: nowrap"><p class="rtf-p">x</p>\n</td>',
  ));
  assert.ok(html.includes('<td style="padding: 0px 4px 0px 4px"><p class="rtf-p">y</p>\n</td>'));
});

test('row_height_and_alignment', () => {
  const html = renderHtml(doc([
    table([row([cell(3000, ['a'])], { height: 300, align: 'center' })]),
    table([row([cell(3000, ['b'])], { height: -300, left: -108 })]),
  ]));
  assert.ok(html.includes(
    '<table class="rtf-table" style="width: 200px; table-layout: fixed; border-collapse: collapse; '
      + 'margin-left: auto; margin-right: auto">\n',
  ));
  assert.ok(html.includes(
    '<table class="rtf-table" style="width: 207.2px; table-layout: fixed; border-collapse: collapse; '
      + 'margin-left: -7.2px">\n',
  ));
  assert.equal(count(html, '<tr style="height: 20px">'), 2);
});

test('nested_table_renders_inside_the_cell', () => {
  const inner = table([row([cell(1000, ['inner'])])]);
  const html = renderHtml(doc([table([row([cell(3000, [inner]), cell(6000, [])])])]));
  assert.equal(count(html, '<table'), 2);
  assert.ok(html.includes(
    `<td style="${PAD}"><table class="rtf-table" style="width: 66.67px; table-layout: fixed; border-collapse: collapse">\n`
      + '<colgroup><col style="width: 66.67px"></colgroup>\n'
      + '<tbody>\n'
      + `<tr><td style="${PAD}"><p class="rtf-p">inner</p>\n</td></tr>\n`
      + '</tbody>\n'
      + '</table>\n'
      + `</td><td style="${PAD}"><p class="rtf-p"><br></p>\n</td></tr>\n`,
  ));
});

test('word_tables_fixture_renders_every_cell', () => {
  const document = JSON.parse(readGolden('word_tables', '.json')) as RtfDocument;
  const text = stripTags(renderHtml(document));
  let checked = 0;
  for (const section of document.sections) {
    for (const block of section.body as Block[]) {
      if (block.kind !== 'table') continue;
      for (const r of block.rows) {
        for (const c of r.cells) {
          if (c.mergeVertical === 'continued' || c.mergeHorizontal === 'continued') continue;
          for (const piece of cellText(c).split(/[\t\n\f]/)) {
            const trimmed = piece.trim();
            if (trimmed === '') continue;
            assert.ok(text.includes(trimmed), `missing cell text: ${trimmed}`);
            checked += 1;
          }
        }
      }
    }
  }
  assert.ok(checked > 0);
});
