#pragma once
// Internal header. Collects paragraphs and tables into a list of blocks.
#include <cstddef>
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
  Cell nextDef;                 // the cell definition that the next \cellx closes
  int nextWidth = 0;            // \clwWidthN of nextDef
  int nextWidthType = 0;        // \clftsWidthN of nextDef
  std::vector<Cell> cells;      // closed cells of the row being built
  std::vector<Block> openCell;  // blocks of the cell being built
  std::vector<Row> rows;        // rows of the table being built

  bool pending() const { return !cells.empty() || !openCell.empty(); }

  // \trowd: forget the row and cell definitions.
  void reset_definition();
  // \cellxN: close the cell definition under construction at right edge N.
  void define_cell(int right);
};

// Where blocks go: a section body, a header or footer, a footnote, the text
// of a shape, or the result of an object. The reader keeps a stack of sinks;
// text always goes to the innermost one.
//
// The table methods take the warning list and the byte offset of the token
// that called them, for cell_count_mismatch and row_not_closed.
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

  // Returns the table of nesting level n (at least 1), creating it. The
  // reader caps n at kMaxTableNesting.
  TableLevel& table(int n);

  // Ends the open paragraph (an empty one when none is open) with the
  // properties in effect at that moment. level is the table nesting level.
  // Level 0 ends the tables and appends to the target. Level N appends to
  // the open cell of level N, after the tables deeper than N are flushed
  // into that cell.
  void end_paragraph(const ParagraphProps& props, int level, std::vector<Warning>& warnings,
                     std::size_t offset);

  // \cell and \nestcell: close the open cell of the level.
  void end_cell(const ParagraphProps& props, int level, std::vector<Warning>& warnings, std::size_t offset);

  // \row and \nestrow: close the row of the level.
  void end_row(const ParagraphProps& props, int level, std::vector<Warning>& warnings, std::size_t offset);

  // Emits the tables of every level from `level` up as blocks, the deepest
  // first. A pending row is closed with row_not_closed. A section break
  // calls this with level 1.
  void flush_tables(int level, std::vector<Warning>& warnings, std::size_t offset);

  // Called when the container ends. The tables are flushed. Then a
  // paragraph with no runs and no list text is dropped, unless the
  // container would otherwise stay empty.
  void close(std::vector<Warning>& warnings, std::size_t offset);

 private:
  std::vector<Block>& container(int level);
  void take_paragraph(const ParagraphProps& props, std::vector<Block>& blocks);
  void finish_row(TableLevel& t, std::vector<Warning>& warnings, std::size_t offset);
  void emit_table(int level, std::vector<Warning>& warnings, std::size_t offset);
};

}  // namespace detail
}  // namespace rtf
