import { test } from 'node:test';
import assert from 'node:assert/strict';
import { renderHtml } from '../src/index.js';
import type { Inline } from '../src/model.js';
import { doc, LETTER, para } from './builders.js';
import { count, stripTags } from './html-helpers.js';

const BREAK: Inline = { kind: 'pageBreak' };

// The HTML of each page, from its <section to the next one.
function pagesOf(html: string): string[] {
  return html.split('<section ').slice(1).map((page) => '<section ' + page);
}

function pageNumbers(html: string): string[] {
  return [...html.matchAll(/<section [^>]*data-page="([^"]*)"/g)].map((m) => m[1]!);
}

// The text of a page's header or footer area.
function areaText(page: string, area: 'header' | 'footer'): string {
  const start = page.indexOf(`<div class="rtf-${area}"`);
  const end = area === 'header' ? page.indexOf('<div class="rtf-body">') : page.indexOf('</section>');
  return stripTags(page.slice(start, end)).trim();
}

// The text of a page's body area.
function bodyText(page: string): string {
  const start = page.indexOf('<div class="rtf-body">');
  const end = page.indexOf('<div class="rtf-footer"');
  return stripTags(page.slice(start, end)).trim();
}

test('each_page_break_section_is_a_page', () => {
  const html = renderHtml(doc([
    { body: [para(['1'])] },
    { body: [para(['2'])] },
    { body: [para(['3'])] },
  ]));
  assert.equal(count(html, '<section class="rtf-page"'), 3);
  assert.deepEqual(pageNumbers(html), ['1', '2', '3']);
  const pages = pagesOf(html);
  assert.deepEqual(pages.map(bodyText), ['1', '2', '3']);
  assert.ok(pages[1]!.includes('data-section="1"'));
});

test('a_page_break_inline_splits_a_paragraph', () => {
  const html = renderHtml(doc([para(['A', BREAK, 'B'], { align: 'center' })]));
  const pages = pagesOf(html);
  assert.equal(pages.length, 2);
  assert.ok(pages[0]!.includes('<p class="rtf-p" style="text-align: center">A</p>\n'));
  assert.ok(pages[1]!.includes('<p class="rtf-p" style="text-align: center">B</p>\n'));
  assert.equal(count(html, '<p '), 2);

  // A break at the end of a paragraph still ends the page, and the empty
  // side gives no paragraph.
  const atEnd = pagesOf(renderHtml(doc([para(['A', BREAK]), para(['B'])])));
  assert.equal(atEnd.length, 2);
  assert.deepEqual(atEnd.map(bodyText), ['A', 'B']);
  assert.equal(count(atEnd.join(''), '<p '), 2);
});

test('page_break_before_starts_a_page', () => {
  const pages = pagesOf(renderHtml(doc([para(['A']), para(['B'], { pageBreakBefore: true })])));
  assert.equal(pages.length, 2);
  assert.deepEqual(pages.map(bodyText), ['A', 'B']);

  // On a page that holds no body content yet, it starts no new page.
  const first = pagesOf(renderHtml(doc([para(['A'], { pageBreakBefore: true })])));
  assert.equal(first.length, 1);
  const sections = pagesOf(renderHtml(doc([
    { body: [para(['A'])] },
    { body: [para(['B'], { pageBreakBefore: true })] },
  ])));
  assert.deepEqual(sections.map(bodyText), ['A', 'B']);
});

test('break_none_continues_the_page', () => {
  const html = renderHtml(doc([
    { headers: { default: [para(['H0'])] }, body: [para(['A'])] },
    { break: 'none', headers: { default: [para(['H1'])] }, body: [para(['B'])] },
  ]));
  const pages = pagesOf(html);
  assert.equal(pages.length, 1);
  assert.ok(pages[0]!.includes('>A</p>'));
  assert.ok(pages[0]!.includes('>B</p>'));
  assert.ok(pages[0]!.includes('data-section="0"'));
  assert.equal(areaText(pages[0]!, 'header'), 'H0');
});

test('headers_follow_the_rules', () => {
  const twoPages = [para(['A', BREAK, 'B'])];

  const byDefault = pagesOf(renderHtml(doc([
    { headers: { default: [para(['D'])] }, footers: { default: [para(['FD'])] }, body: twoPages },
  ])));
  assert.deepEqual(byDefault.map((p) => areaText(p, 'header')), ['D', 'D']);
  assert.deepEqual(byDefault.map((p) => areaText(p, 'footer')), ['FD', 'FD']);

  const onlyRight = pagesOf(renderHtml(doc([
    { headers: { right: [para(['R'])] }, body: twoPages },
  ])));
  assert.deepEqual(onlyRight.map((p) => areaText(p, 'header')), ['R', 'R']);

  const titlePage = pagesOf(renderHtml(doc([
    {
      titlePage: true,
      headers: { first: [para(['F'])], default: [para(['D'])] },
      footers: { first: [para(['FF'])], default: [para(['FD'])] },
      body: twoPages,
    },
  ])));
  assert.deepEqual(titlePage.map((p) => areaText(p, 'header')), ['F', 'D']);
  assert.deepEqual(titlePage.map((p) => areaText(p, 'footer')), ['FF', 'FD']);

  const facing = pagesOf(renderHtml(doc(
    [{ headers: { left: [para(['L'])], right: [para(['R'])] }, body: twoPages }],
    { page: { ...LETTER, facingPages: true } },
  )));
  assert.deepEqual(facing.map((p) => areaText(p, 'header')), ['R', 'L']);
});

test('page_areas_keep_the_margins', () => {
  const html = renderHtml(doc([para(['A'])]));
  assert.ok(html.includes('<div class="rtf-header" style="min-height: 96px; padding-top: 48px">\n</div>\n'));
  assert.ok(html.includes('<div class="rtf-footer" style="min-height: 96px; padding-bottom: 48px">\n</div>\n'));
  assert.ok(html.includes('width: 816px; min-height: 1056px'));
  assert.equal(
    html,
    '<section class="rtf-page" data-page="1" data-section="0" style="width: 816px; min-height: 1056px; '
      + 'padding-left: 96px; padding-right: 96px; font-family: &quot;Times New Roman&quot;, serif; font-size: 16px">\n'
      + '<div class="rtf-header" style="min-height: 96px; padding-top: 48px">\n</div>\n'
      + '<div class="rtf-body">\n<p class="rtf-p">A</p>\n</div>\n'
      + '<div class="rtf-footer" style="min-height: 96px; padding-bottom: 48px">\n</div>\n'
      + '</section>\n',
  );
});

test('landscape_uses_the_given_width_and_height', () => {
  const page = { ...LETTER, width: 15840, height: 12240, landscape: true };
  const html = renderHtml(doc([{ page, body: [para(['A'])] }], { page }));
  assert.ok(html.includes('<section class="rtf-page rtf-landscape"'));
  assert.ok(html.includes('width: 1056px; min-height: 816px'));
});

test('page_numbers_count_pages', () => {
  assert.deepEqual(pageNumbers(renderHtml(doc([
    { body: [para(['A'])] },
    { pageNumberStart: 5, body: [para(['B'])] },
  ]))), ['1', '5']);
  assert.deepEqual(pageNumbers(renderHtml(doc([
    { body: [para(['A', BREAK, 'A2'])] },
    { pageNumberStart: 5, body: [para(['B'])] },
  ]))), ['1', '2', '5']);
});
