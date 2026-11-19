import { test } from 'node:test';
import assert from 'node:assert/strict';
import { renderHtml } from '../src/index.js';
import type { Paragraph, RtfDocument, TabStop } from '../src/model.js';
import { doc, para, run } from './builders.js';
import { readGolden } from './helpers.js';
import { stripTags } from './html-helpers.js';

function stop(position: number): TabStop {
  return { position, align: 'left', leader: 'none' };
}

test('paragraph_styles_from_props', () => {
  const html = renderHtml(doc([para(['x'], {
    align: 'center',
    indentLeft: 720,
    indentFirst: -360,
    spaceBefore: 120,
    lineSpacing: 360,
    lineSpacingMultiple: true,
  })]));
  assert.ok(html.includes(
    '<p class="rtf-p" style="text-align: center; margin-left: 48px; text-indent: -24px; margin-top: 8px; line-height: 1.5">x</p>\n',
  ));
});

test('empty_paragraph_keeps_a_line', () => {
  const html = renderHtml(doc([para([])]));
  assert.ok(html.includes('<p class="rtf-p"><br></p>\n'));
});

test('run_styles_from_props', () => {
  const html = renderHtml(doc([para([run('x', {
    bold: true,
    italic: true,
    underline: 'double',
    strike: true,
    color: 1,
    highlight: 2,
    size: 20,
    font: 1,
  })])]));
  assert.ok(html.includes(
    '<span style="font-family: &quot;Courier New&quot;, monospace; font-size: 13.33px; font-weight: bold; '
      + 'font-style: italic; text-decoration-line: underline line-through; text-decoration-style: double; '
      + 'color: rgb(255, 0, 0); background-color: rgb(0, 0, 255)">x</span>',
  ));
});

test('superscript_and_small_caps', () => {
  const html = renderHtml(doc([para([
    run('2', { script: 'super' }),
    run('a', { smallCaps: true }),
  ])]));
  assert.ok(html.includes('<sup>2</sup>'));
  assert.ok(html.includes('<span style="font-variant: small-caps">a</span>'));
});

test('hidden_text_is_omitted', () => {
  const html = renderHtml(doc([
    para(['a', run('secret', { hidden: true }), 'b']),
    para([run('only hidden', { hidden: true })]),
  ]));
  assert.ok(!html.includes('secret'));
  assert.ok(!html.includes('only hidden'));
  assert.ok(html.includes('<p class="rtf-p">ab</p>\n'));
  assert.ok(html.includes('<p class="rtf-p"><br></p>\n'));
});

test('html_escapes_text_and_font_names', () => {
  const base = doc([]);
  const html = renderHtml(doc([para([run('<script>alert(1)</script>', { font: 2 })])], {
    fonts: {
      ...base.fonts,
      2: { name: '"Arial"; } body { color: red', family: 'swiss', charset: 0, pitch: 'default' },
    },
  }));
  assert.ok(html.includes('&lt;script&gt;alert(1)&lt;/script&gt;'));
  assert.ok(html.includes('font-family: &quot;Arial body color red&quot;, sans-serif'));
  assert.ok(!html.includes('<script'));
});

test('tabs_take_the_stop_widths', () => {
  const html = renderHtml(doc([para(
    [{ kind: 'tab' }, 'a', { kind: 'tab' }, 'b', { kind: 'tab' }, 'c'],
    { tabs: [stop(1440), stop(2880)] },
  )]));
  const widths = [...html.matchAll(/<span class="rtf-tab" style="display: inline-block; width: ([^"]*)"><\/span>/g)]
    .map((m) => m[1]);
  assert.deepEqual(widths, ['96px', '96px', '48px']);
});

test('list_text_comes_first_and_the_symbol_bullet_shows_as_a_bullet', () => {
  const p: Paragraph = { ...para(['x']), listText: [run('\uf0b7'), { kind: 'tab' }] };
  const html = renderHtml(doc([p]));
  const text = stripTags(html);
  assert.ok(!text.includes('\uf0b7'));
  assert.ok(text.includes('\u2022'));
  assert.ok(text.indexOf('\u2022') < text.indexOf('x'));
});

test('nbsp_is_kept', () => {
  const html = renderHtml(doc([para(['a\u00a0b'])]));
  assert.ok(html.includes('a\u00a0b'));
});

test('fonts_option_overrides_a_name', () => {
  const html = renderHtml(doc([para([run('x', { font: 1 })])]), {
    fonts: { 'Courier New': 'Menlo, monospace' },
  });
  assert.ok(html.includes('font-family: Menlo, monospace'));
});

test('class_prefix_renames_the_classes', () => {
  const html = renderHtml(doc([para(['a', { kind: 'tab' }, { kind: 'bookmark', name: 'b', position: 'start' }])]), {
    classPrefix: 'x-',
  });
  assert.ok(html.includes('class="x-page"'));
  assert.ok(html.includes('class="x-p"'));
  assert.ok(html.includes('class="x-tab"'));
  assert.ok(html.includes('<a id="x-bm-b"></a>'));
  assert.ok(!html.includes('rtf-'));
});

test('output_is_deterministic', () => {
  const document = JSON.parse(readGolden('word_structure', '.json')) as RtfDocument;
  const first = renderHtml(document);
  const second = renderHtml(document);
  assert.ok(first.length > 0);
  assert.equal(first, second);
});

test('zero_indents_and_spacing_are_not_written', () => {
  const zeros = { indentLeft: 0, indentRight: 0, indentFirst: 0, spaceBefore: 0, spaceAfter: 0 };
  const html = renderHtml(doc([para(['x'], zeros)]));
  assert.ok(html.includes('<p class="rtf-p">x</p>\n'));
  const negative = renderHtml(doc([para(['x'], { ...zeros, indentFirst: -360 })]));
  assert.ok(negative.includes('<p class="rtf-p" style="text-indent: -24px">x</p>\n'));
});

test('hostile_model_values_never_reach_markup_or_css', () => {
  // renderHtml takes a plain object, so a document from another source
  // can hold any value where the types say number or a fixed word.
  const hostile = {
    pageNumberStart: '1"><img src=x onerror=alert(1)>',
    align: 'center; background-image: url(http://x/y)',
    underline: 'constructor',
    borderStyle: 'toString',
  } as unknown as {
    pageNumberStart: number;
    align: 'center';
    underline: 'single';
    borderStyle: 'single';
  };
  const html = renderHtml(doc([{
    pageNumberStart: hostile.pageNumberStart,
    body: [para([run('x', { underline: hostile.underline })], {
      align: hostile.align,
      borders: { top: { style: hostile.borderStyle, width: 15 } },
    })],
  }]));
  for (const needle of ['<img', 'onerror', 'url(', 'function', 'background-image']) {
    assert.ok(!html.includes(needle), needle);
  }
  assert.ok(html.includes('data-page="1"'));
  assert.ok(html.includes('<p class="rtf-p" style="border-top: 1px solid">'));
  assert.ok(html.includes('<span style="text-decoration-line: underline">x</span>'));
});
