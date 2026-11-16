#pragma once
// Internal header. Collects paragraphs and tables into a list of blocks.
#include <optional>
#include <string_view>
#include <vector>

#include "rtf/model.hpp"

namespace rtf {
namespace detail {

// The table being built at one nesting level. Index 1 is the level of \intbl.
struct TableLevel {
  RowProps rowProps;            // the row definition in effect
  std::vector<Cell> cellDefs;   // from \cellx, without blocks
  std::vector<Cell> cells;      // closed cells of the row being built
  std::vector<Block> openCell;  // blocks of the cell being built
  std::vector<Row> rows;        // rows of the table being built

  bool pending() const { return !cells.empty() || !openCell.empty(); }
};

// Where blocks go: a section body, a header or footer, a footnote, the text
// of a shape, or the result of an object. The reader keeps a stack of sinks;
// text always goes to the innermost one.
struct BlockSink {
  std::vector<Block>* target = nullptr;
  std::optional<Paragraph> current;    // the paragraph being built
  std::vector<TableLevel> levels;      // index 0 unused

  explicit BlockSink(std::vector<Block>* blocks);

  // Opens a paragraph when none is open and returns it.
  Paragraph& begin_paragraph_if_needed();

  // Appends text with the given properties. The text joins the previous
  // run when that run has the same properties.
  void append_run(std::string_view text, const CharProps& props);

  // Appends a break, a field or another inline item.
  void append_inline(Inline item);

  // Ends the open paragraph (an empty one when none is open) with the
  // properties in effect at that moment. level is the table nesting level.
  void end_paragraph(const ParagraphProps& props, int level);

  // Table assembly. Task 5 fills these in; without a table they do nothing.
  void end_cell(int level);
  void end_row(int level, std::vector<Warning>& warnings);
  void flush_tables(int level, std::vector<Warning>& warnings);

  // Called when the container ends. A paragraph with no runs and no list
  // text is dropped, unless the container would otherwise stay empty.
  void close(std::vector<Warning>& warnings);
};

}  // namespace detail
}  // namespace rtf
