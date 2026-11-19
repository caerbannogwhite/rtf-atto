// HTML escaping and attribute writing. Every text node and every
// attribute value goes through escapeHtml.

const ESCAPES: Record<string, string> = {
  '&': '&amp;',
  '<': '&lt;',
  '>': '&gt;',
  '"': '&quot;',
  "'": '&#39;',
};

export function escapeHtml(text: string): string {
  return text.replace(/[&<>"']/g, (c) => ESCAPES[c]!);
}

// The style attribute with a leading space, such as ' style="a: 1; b: 2"',
// or the empty string when there are no declarations.
export function styleAttr(declarations: string[]): string {
  if (declarations.length === 0) return '';
  return ' style="' + escapeHtml(declarations.join('; ')) + '"';
}
