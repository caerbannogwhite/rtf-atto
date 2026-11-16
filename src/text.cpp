// The plain-text projection. One pass over the model, appending to one string.
#include "rtf/text.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace rtf {
namespace {

void append_blocks(const std::vector<Block>& blocks, std::string& out, bool drop_last_newline);

void append_inlines(const std::vector<Inline>& items, std::string& out) {
  for (const Inline& item : items) {
    const auto& v = item.value;
    if (const auto* t = std::get_if<TextRun>(&v)) {
      out += t->text;
    } else if (std::holds_alternative<Tab>(v)) {
      out += '\t';
    } else if (std::holds_alternative<LineBreak>(v) || std::holds_alternative<ColumnBreak>(v)) {
      out += '\n';
    } else if (std::holds_alternative<PageBreak>(v)) {
      out += '\f';
    } else if (const auto* f = std::get_if<Field>(&v)) {
      append_inlines(f->result, out);
    } else if (const auto* n = std::get_if<Footnote>(&v)) {
      append_blocks(n->blocks, out, false);
    } else if (const auto* s = std::get_if<Shape>(&v)) {
      append_blocks(s->blocks, out, false);
    } else if (const auto* o = std::get_if<EmbeddedObject>(&v)) {
      append_blocks(o->result, out, false);
    }
  }
}

void append_table(const Table& table, std::string& out) {
  for (const Row& row : table.rows) {
    for (const Cell& cell : row.cells) {
      append_blocks(cell.blocks, out, true);
      out += '\t';
    }
    out += '\n';
  }
}

// When drop_last_newline is set, a paragraph in the last position adds no "\n".
void append_blocks(const std::vector<Block>& blocks, std::string& out, bool drop_last_newline) {
  for (std::size_t i = 0; i < blocks.size(); ++i) {
    const bool last = i + 1 == blocks.size();
    if (const auto* p = std::get_if<Paragraph>(&blocks[i].value)) {
      append_inlines(p->listText, out);
      append_inlines(p->runs, out);
      if (!(last && drop_last_newline)) out += '\n';
    } else if (const auto* t = std::get_if<Table>(&blocks[i].value)) {
      append_table(*t, out);
    }
  }
}

void append_optional(const std::optional<std::vector<Block>>& blocks, std::string& out) {
  if (blocks) append_blocks(*blocks, out, false);
}

}  // namespace

std::string inline_text(const std::vector<Inline>& inlines) {
  std::string out;
  append_inlines(inlines, out);
  return out;
}

std::string cell_text(const Cell& cell) {
  std::string out;
  append_blocks(cell.blocks, out, true);
  return out;
}

std::string to_text(const RtfDocument& document) {
  std::string out;
  for (const Section& s : document.sections) {
    append_optional(s.headers.default_, out);
    append_optional(s.headers.first, out);
    append_optional(s.headers.left, out);
    append_optional(s.headers.right, out);
    append_optional(s.footers.default_, out);
    append_optional(s.footers.first, out);
    append_optional(s.footers.left, out);
    append_optional(s.footers.right, out);
    append_blocks(s.body, out, false);
  }
  return out;
}

}  // namespace rtf
