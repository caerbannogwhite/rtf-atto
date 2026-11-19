// Helpers for tests that read the renderer's HTML as a string.

const NAMED: Record<string, string> = { amp: '&', lt: '<', gt: '>', quot: '"', '#39': "'" };

// Removes every tag and decodes the character references the renderer
// writes: &amp; &lt; &gt; &quot; &#39; and numeric references.
export function stripTags(html: string): string {
  return html
    .replace(/<[^>]*>/g, '')
    .replace(/&(amp|lt|gt|quot|#39|#[0-9]+|#x[0-9a-fA-F]+);/g, (whole, name: string) => {
      const named = NAMED[name];
      if (named !== undefined) return named;
      const code = name[1] === 'x' || name[1] === 'X'
        ? parseInt(name.slice(2), 16)
        : parseInt(name.slice(1), 10);
      return Number.isFinite(code) ? String.fromCodePoint(code) : whole;
    });
}

// The number of times needle occurs in html, without overlaps.
export function count(html: string, needle: string): number {
  if (needle === '') return 0;
  return html.split(needle).length - 1;
}
