#include "builder.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "properties.hpp"

namespace rtf {
namespace detail {
namespace {

bool is_empty(const Paragraph& p) { return p.runs.empty() && p.listText.empty(); }

}  // namespace

void TableLevel::reset_definition() {
  rowProps = RowProps{};
  cellDefs.clear();
  nextDef = Cell{};
  nextWidth = 0;
  nextWidthType = 0;
}

void TableLevel::define_cell(int right) {
  nextDef.right = right;
  // \clftsWidth gives the unit of \clwWidth: 1 auto, 2 percent, 3 twips.
  // Type 0 or no type means no preferred width.
  switch (nextWidthType) {
    case 1: nextDef.preferredWidth = PreferredWidth{WidthType::Auto, nextWidth}; break;
    case 2: nextDef.preferredWidth = PreferredWidth{WidthType::Percent, nextWidth}; break;
    case 3: nextDef.preferredWidth = PreferredWidth{WidthType::Twips, nextWidth}; break;
    default: break;
  }
  cellDefs.push_back(std::move(nextDef));
  nextDef = Cell{};
  nextWidth = 0;
  nextWidthType = 0;
}

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

// A table level can be at most one deeper than the deepest level that
// exists. Level 2 is always allowed, because a cell may start with a
// nested table before any level-1 word. A paragraph with a large \itapN
// and no tables around it then creates two levels, not N. This keeps the
// model's size in proportion to the input.
int BlockSink::bounded(int n) const {
  const std::size_t deepest = std::max<std::size_t>(levels.size(), 2);
  return static_cast<int>(std::min(static_cast<std::size_t>(std::max(n, 1)), deepest));
}

TableLevel& BlockSink::table(int n) {
  const std::size_t i = static_cast<std::size_t>(bounded(n));
  if (levels.size() <= i) levels.resize(i + 1);
  return levels[i];
}

// A level-1 table goes to the target. A deeper table goes into the open
// cell of the level above it.
std::vector<Block>& BlockSink::container(int level) {
  if (level <= 1) return *target;
  return table(level - 1).openCell;
}

void BlockSink::end_paragraph(const ParagraphProps& props, int level, std::vector<Warning>& warnings,
                              std::size_t offset) {
  Paragraph& p = begin_paragraph_if_needed();
  p.props = props;
  if (level <= 0) {
    // A paragraph outside any table ends the tables before it.
    flush_tables(1, warnings, offset);
    target->push_back(Block{std::move(*current)});
  } else {
    level = bounded(level);
    flush_tables(level + 1, warnings, offset);
    table(level).openCell.push_back(Block{std::move(*current)});
  }
  current.reset();
}

// The paragraph that \cell ends. An empty one is dropped when the cell
// already has blocks; an empty cell keeps one empty paragraph.
void BlockSink::take_paragraph(const ParagraphProps& props, std::vector<Block>& blocks) {
  if (current && !is_empty(*current)) {
    current->props = props;
    blocks.push_back(Block{std::move(*current)});
  } else if (blocks.empty()) {
    Paragraph p = current ? std::move(*current) : Paragraph{};
    p.props = props;
    blocks.push_back(Block{std::move(p)});
  }
  current.reset();
}

void BlockSink::end_cell(const ParagraphProps& props, int level, std::vector<Warning>& warnings,
                         std::size_t offset) {
  level = bounded(level);
  // A deeper table ends inside this cell, before the cell's last paragraph.
  flush_tables(level + 1, warnings, offset);
  TableLevel& t = table(level);
  take_paragraph(props, t.openCell);
  // The cell's properties come from the definitions in effect at \row,
  // because Word writes the row's definition again just before \row.
  t.cells.emplace_back();
  t.cells.back().blocks = std::move(t.openCell);
  t.openCell.clear();
}

void BlockSink::end_row(const ParagraphProps& props, int level, std::vector<Warning>& warnings,
                        std::size_t offset) {
  level = bounded(level);
  flush_tables(level + 1, warnings, offset);
  // Text after the last \cell, or a deeper table flushed just now, makes
  // one more cell.
  if ((current && !is_empty(*current)) || !table(level).openCell.empty()) end_cell(props, level, warnings, offset);
  finish_row(table(level), warnings, offset);
}

// Matches the row's cells to the definitions in order. Missing cells are
// empty; extra cells get the width of the last definition. The definitions
// stay for a following row written without \trowd.
void BlockSink::finish_row(TableLevel& t, std::vector<Warning>& warnings, std::size_t offset) {
  const std::size_t n = t.cells.size();
  const std::size_t d = t.cellDefs.size();
  if (n == 0 && d == 0) return;  // a \row with neither cells nor definitions
  if (n != d) {
    warnings.push_back(Warning{"cell_count_mismatch",
                               "row has " + std::to_string(n) + " cells and " + std::to_string(d) + " definitions",
                               offset});
  }
  const int left = t.rowProps.left.value_or(0);
  int lastWidth = 0;
  if (d > 0) lastWidth = t.cellDefs[d - 1].right - (d > 1 ? t.cellDefs[d - 2].right : left);

  Row row;
  row.props = t.rowProps;
  row.cells.reserve(std::max(n, d));
  int previous = left;
  for (std::size_t i = 0; i < std::max(n, d); ++i) {
    Cell cell;
    if (i < d) {
      cell = t.cellDefs[i];
    } else {
      cell.right = previous + lastWidth;
    }
    if (i < n) {
      cell.blocks = std::move(t.cells[i].blocks);
    } else {
      cell.blocks.push_back(Block{Paragraph{}});
    }
    // Widths are kept as computed, negative ones included.
    cell.width = cell.right - previous;
    previous = cell.right;
    row.cells.push_back(std::move(cell));
  }
  t.rows.push_back(std::move(row));
  t.cells.clear();
  t.openCell.clear();
}

// Emits the table of one level. A row still open is closed first.
void BlockSink::emit_table(int level, std::vector<Warning>& warnings, std::size_t offset) {
  TableLevel& t = levels[static_cast<std::size_t>(level)];
  if (t.pending()) {
    warnings.push_back(Warning{"row_not_closed", "a table row ends without \\row", offset});
    if (!t.openCell.empty()) {
      t.cells.emplace_back();
      t.cells.back().blocks = std::move(t.openCell);
      t.openCell.clear();
    }
    finish_row(t, warnings, offset);
  }
  if (t.rows.empty()) return;
  Table table;
  table.rows = std::move(t.rows);
  t.rows.clear();
  container(level).push_back(Block{std::move(table)});
}

void BlockSink::flush_tables(int level, std::vector<Warning>& warnings, std::size_t offset) {
  const std::size_t from = static_cast<std::size_t>(std::max(level, 1));
  for (std::size_t k = levels.size(); k-- > from;) emit_table(static_cast<int>(k), warnings, offset);
  // The levels above `level` are empty now. Dropping them keeps the next
  // flush short, so the work stays linear in the input.
  if (levels.size() > from + 1) levels.erase(levels.begin() + static_cast<std::ptrdiff_t>(from + 1), levels.end());
}

void BlockSink::close(std::vector<Warning>& warnings, std::size_t offset) {
  flush_tables(1, warnings, offset);
  if (current) {
    // The design's rule: a paragraph that exists only because its
    // container closed is dropped, unless it is the container's only content.
    if (!is_empty(*current) || target->empty()) target->push_back(Block{std::move(*current)});
    current.reset();
  } else if (target->empty()) {
    target->push_back(Block{Paragraph{}});
  }
}

}  // namespace detail
}  // namespace rtf
