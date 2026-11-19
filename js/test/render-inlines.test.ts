import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mount, renderHtml } from '../src/index.js';
import type { Block, Field, Footnote, Inline, Picture, RtfDocument, Shape } from '../src/model.js';
import { cell, doc, para, row, run, table } from './builders.js';
import { readGolden } from './helpers.js';
import { count, stripTags } from './html-helpers.js';

const BREAK: Inline = { kind: 'pageBreak' };

function field(instruction: string, result: Inline[] = []): Field {
  return { kind: 'field', instruction, result };
}

function footnote(text: string): Footnote {
  return { kind: 'footnote', blocks: [para([text])] };
}

function picture(part: Partial<Picture>): Picture {
  return { kind: 'picture', format: 'png', data: '', ...part };
}

// The HTML of each page, from its <section to the next one.
function pagesOf(html: string): string[] {
  return html.split('<section ').slice(1).map((page) => '<section ' + page);
}

function footerText(page: string): string {
  const text = stripTags(page.slice(page.indexOf('<div class="rtf-footer"'), page.indexOf('</section>')));
  return text.replace(/^\n+|\n+$/g, '');
}

// The text of a page's footnote list, or undefined when it has none.
function notesText(page: string): string | undefined {
  const start = page.indexOf('<div class="rtf-footnotes">');
  if (start < 0) return undefined;
  return stripTags(page.slice(start, page.indexOf('<div class="rtf-footer"'))).replace(/\s+/g, ' ').trim();
}

// The 1x1 PNG of the pictures fixture.
function fixturePng(): Picture {
  const parsed = JSON.parse(readGolden('pictures', '.json')) as RtfDocument;
  const first = parsed.sections[0]!.body[0]!;
  assert.equal(first.kind, 'paragraph');
  const found = first.kind === 'paragraph' ? first.runs.find((r) => r.kind === 'picture') : undefined;
  assert.ok(found !== undefined && found.kind === 'picture');
  return found;
}

test('page_fields_follow_the_option', () => {
  const footer = (page: Field): Block[] => [para([page, ' of ', field('NUMPAGES')])];
  const sections = (pageField: Field, start?: number) => [
    { body: [para(['a', BREAK, 'b'])], footers: { default: footer(pageField) } },
    { body: [para(['c'])], footers: { default: footer(pageField) }, ...(start === undefined ? {} : { pageNumberStart: start }) },
  ];

  const plain = pagesOf(renderHtml(doc(sections(field('PAGE')))));
  assert.deepEqual(plain.map(footerText), ['1 of 3', '2 of 3', '3 of 3']);

  // A section that restarts the numbers changes PAGE but not NUMPAGES.
  const restarted = pagesOf(renderHtml(doc(sections(field('PAGE'), 7))));
  assert.deepEqual(restarted.map(footerText), ['1 of 3', '2 of 3', '7 of 3']);

  // A field with a result renders the result.
  const roman = pagesOf(renderHtml(doc(sections(field('PAGE', [run('IX')])))));
  assert.deepEqual(roman.map(footerText), ['IX of 3', 'IX of 3', 'IX of 3']);

  // pageNumbers 'none' renders the empty results.
  const none = pagesOf(renderHtml(doc(sections(field('PAGE'))), { pageNumbers: 'none' }));
  assert.deepEqual(none.map(footerText), [' of ', ' of ', ' of ']);

  // Word writes switches after the field name.
  const word = pagesOf(renderHtml(doc(sections(field(' PAGE \\* MERGEFORMAT ')))));
  assert.deepEqual(word.map(footerText), ['1 of 3', '2 of 3', '3 of 3']);

  // The name must be a whole word.
  const other = renderHtml(doc([para([field('PAGEREF x'), field('NUMPAGESX')])]));
  assert.equal(count(other, '<p class="rtf-p"><br></p>'), 1);
});

test('footnotes_are_listed_at_the_bottom_of_the_page', () => {
  const html = renderHtml(doc([
    { body: [para(['A', footnote('note one'), 'B', footnote('note two'), BREAK, 'C', footnote('note three')])] },
    { body: [para(['D'])] },
  ]));
  const pages = pagesOf(html);
  assert.equal(pages.length, 3);
  assert.equal(count(pages[0]!, '<sup class="rtf-footnote-ref">1</sup>'), 1);
  assert.equal(count(pages[0]!, '<sup class="rtf-footnote-ref">2</sup>'), 1);
  assert.equal(count(pages[1]!, '<sup class="rtf-footnote-ref">3</sup>'), 1);
  assert.equal(notesText(pages[0]!), '1note one 2note two');
  assert.equal(notesText(pages[1]!), '3note three');
  assert.equal(notesText(pages[2]!), undefined);
  assert.ok(pages[0]!.includes(
    '<div class="rtf-footnotes">\n'
      + '<div class="rtf-footnote"><sup>1</sup><p class="rtf-p">note one</p>\n</div>\n'
      + '<div class="rtf-footnote"><sup>2</sup><p class="rtf-p">note two</p>\n</div>\n'
      + '</div>\n</div>\n<div class="rtf-footer"',
  ));

  // An endnote is numbered and listed the same way.
  const endnote: Footnote = { kind: 'footnote', endnote: true, blocks: [para(['end'])] };
  const withEnd = renderHtml(doc([para(['x', footnote('first'), endnote])]));
  assert.equal(notesText(withEnd), '1first 2end');
});

test('pictures_become_data_urls', () => {
  const png = fixturePng();
  assert.equal(png.data.length, 96);
  const html = renderHtml(doc([para([png])]));
  assert.ok(html.includes(
    `<img class="rtf-picture" src="data:image/png;base64,${png.data}" alt="" style="width: 1px; height: 1px">`,
  ));

  const jpeg = renderHtml(doc([para([picture({ format: 'jpeg', data: '/9h7fQ==' })])]));
  assert.ok(jpeg.includes('<img class="rtf-picture" src="data:image/jpeg;base64,/9h7fQ==" alt="">'));

  // Scaled goal sizes, then the plain sizes as pixels.
  const scaled = renderHtml(doc([para([picture({ data: 'AAAA', goalWidth: 1500, goalHeight: 300, scaleX: 50, scaleY: 200 })])]));
  assert.ok(scaled.includes('style="width: 50px; height: 40px"'));
  const sized = renderHtml(doc([para([picture({ data: 'AAAA', width: 10, height: 20 })])]));
  assert.ok(sized.includes('style="width: 10px; height: 20px"'));

  // The cap itself is allowed.
  const atCap = renderHtml(doc([para([picture({ data: 'A'.repeat(4_194_304) })])]));
  assert.equal(count(atCap, '<img '), 1);
});

test('pictures_outside_the_rules_become_boxes', () => {
  const box = (html: string): void => {
    assert.equal(count(html, '<img'), 0);
    assert.equal(count(html, 'class="rtf-picture-box"'), 1);
  };
  box(renderHtml(doc([para([picture({ data: 'not base64!' })])])));
  box(renderHtml(doc([para([picture({ data: '"><script>alert(1)</script>' })])])));
  box(renderHtml(doc([para([picture({ data: 'A'.repeat(4_194_305) })])])));

  const emf = renderHtml(doc([para([picture({ format: 'emf', data: 'AQAAAA==', width: 100, height: 100 })])]));
  assert.ok(emf.includes(
    '<span class="rtf-picture-box" style="display: inline-block; width: 100px; height: 100px">EMF</span>',
  ));

  // An unknown size is 100px by 100px; an unknown format is UNKNOWN.
  const odd = renderHtml(doc([para([picture({ format: '<b>svg</b>' as Picture['format'], data: 'AAAA' })])]));
  assert.ok(odd.includes(
    '<span class="rtf-picture-box" style="display: inline-block; width: 100px; height: 100px">UNKNOWN</span>',
  ));
  const proto = renderHtml(doc([para([picture({ format: 'constructor' as Picture['format'], data: 'AAAA' })])]));
  assert.ok(proto.includes('>UNKNOWN</span>'));
  assert.equal(count(proto, '<img'), 0);

  // pictures: false renders nothing, not even the box.
  const off = renderHtml(doc([para([fixturePng(), picture({ format: 'emf', data: 'AQAAAA==' })])]), { pictures: false });
  assert.equal(count(off, '<img'), 0);
  assert.equal(count(off, 'rtf-picture-box'), 0);
});

test('shapes_and_objects_render_their_blocks', () => {
  const textBox: Shape = { kind: 'shape', blocks: [para(['Inside'])] };
  const withPicture: Shape = { kind: 'shape', blocks: [], picture: picture({ format: 'emf', data: 'AQAAAA==', width: 10, height: 10 }) };
  const object: Inline = { kind: 'object', className: 'Excel.Sheet', result: [para(['Result'])] };
  const html = renderHtml(doc([
    para(['A text box: ', textBox]),
    para([withPicture]),
    para(['An object: ', object, 'after']),
    table([row([cell(3000, [para(['in a cell', textBox])])])]),
  ]));
  assert.ok(html.includes(
    '<p class="rtf-p">A text box: </p>\n<div class="rtf-shape"><p class="rtf-p">Inside</p>\n</div>\n',
  ));
  assert.ok(html.includes(
    '<p class="rtf-p"><br></p>\n<div class="rtf-shape"><span class="rtf-picture-box" style="display: inline-block; width: 10px; height: 10px">EMF</span></div>\n',
  ));
  assert.ok(html.includes(
    '<p class="rtf-p">An object: after</p>\n<div class="rtf-object"><p class="rtf-p">Result</p>\n</div>\n',
  ));
  assert.ok(html.includes(
    '<p class="rtf-p">in a cell</p>\n<div class="rtf-shape"><p class="rtf-p">Inside</p>\n</div>\n</td>',
  ));
  // No block element sits inside a paragraph.
  for (const p of html.match(/<p [^>]*>.*?<\/p>/gs) ?? []) assert.ok(!p.includes('<div'), p);
});

test('bookmarks_are_anchors', () => {
  const html = renderHtml(doc([para([
    { kind: 'bookmark', name: 'a b', position: 'start' },
    'x',
    { kind: 'bookmark', name: 'a b', position: 'end' },
  ])]));
  assert.ok(html.includes('<p class="rtf-p"><a id="rtf-bm-a%20b"></a>x</p>\n'));
  assert.equal(count(html, '<a '), 1);
});

test('hyperlink_fields_render_their_result_only', () => {
  const html = renderHtml(doc([para([field('HYPERLINK "https://example.com/"', [run('Example')])])]));
  assert.ok(html.includes('<p class="rtf-p">Example</p>\n'));
  assert.equal(count(html, '<a href'), 0);
  assert.equal(count(html, 'example.com'), 0);
});

test('mount_sets_inner_html_and_returns_pages', () => {
  const p1 = { id: 'p1' };
  const p2 = { id: 'p2' };
  let selector = '';
  const container = {
    innerHTML: '',
    querySelectorAll: (s: string) => {
      selector = s;
      return [p1, p2];
    },
  };
  const document = doc([{ body: [para(['1'])] }, { body: [para(['2'])] }]);
  const result = mount(document, container as unknown as Element);
  assert.equal(container.innerHTML, renderHtml(document));
  assert.equal(selector, 'section[data-page]');
  assert.equal(result.pages.length, 2);
  assert.equal(result.pages[0], p1 as unknown as Element);
  assert.ok(Array.isArray(result.pages));
});
