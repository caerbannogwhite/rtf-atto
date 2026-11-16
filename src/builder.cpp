#include "builder.hpp"

#include <string>
#include <utility>

#include "properties.hpp"

namespace rtf {
namespace detail {

BlockSink::BlockSink(std::vector<Block>* blocks) : target(blocks), levels(1) {}

Paragraph& BlockSink::begin_paragraph_if_needed() {
  if (!current) current.emplace();
  return *current;
}

void BlockSink::append_run(std::string_view text, const CharProps& props) {
  if (text.empty()) return;
  Paragraph& p = begin_paragraph_if_needed();
  if (!p.runs.empty()) {
    if (auto* last = std::get_if<TextRun>(&p.runs.back().value)) {
      if (same_char_props(last->props, props)) {
        last->text.append(text.data(), text.size());
        return;
      }
    }
  }
  p.runs.push_back(Inline{TextRun{std::string(text), props}});
}

void BlockSink::append_inline(Inline item) {
  begin_paragraph_if_needed().runs.push_back(std::move(item));
}

void BlockSink::end_paragraph(const ParagraphProps& props, int level) {
  // Task 5 sends paragraphs of level 1 and above to the open cell of that
  // level. Until then every paragraph goes to the target.
  (void)level;
  Paragraph& p = begin_paragraph_if_needed();
  p.props = props;
  target->push_back(Block{std::move(p)});
  current.reset();
}

void BlockSink::end_cell(int level) {
  // Task 5: move the open paragraphs into levels[level].openCell's cell.
  (void)level;
}

void BlockSink::end_row(int level, std::vector<Warning>& warnings) {
  // Task 5: close the row of levels[level].
  (void)level;
  (void)warnings;
}

void BlockSink::flush_tables(int level, std::vector<Warning>& warnings) {
  // Task 5: emit the tables of every level from `level` up as blocks.
  // Without a pending table there is nothing to do.
  (void)level;
  (void)warnings;
}

void BlockSink::close(std::vector<Warning>& warnings) {
  flush_tables(1, warnings);
  if (current) {
    // The design's rule: a paragraph that exists only because its
    // container closed is dropped, unless it is the container's only content.
    const bool empty = current->runs.empty() && current->listText.empty();
    if (!empty || target->empty()) target->push_back(Block{std::move(*current)});
    current.reset();
  } else if (target->empty()) {
    target->push_back(Block{Paragraph{}});
  }
}

}  // namespace detail
}  // namespace rtf
