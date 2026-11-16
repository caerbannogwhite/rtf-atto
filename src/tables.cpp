// Logical tables: stitches the table blocks of the body into tables as printed,
// builds the column grid and names the columns from the header rows.
#include "rtf/tables.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "rtf/text.hpp"

namespace rtf {
namespace {

bool is_ascii_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

std::string trim(const std::string& s) {
  std::size_t begin = 0;
  std::size_t end = s.size();
  while (begin < end && is_ascii_space(s[begin])) ++begin;
  while (end > begin && is_ascii_space(s[end - 1])) --end;
  return s.substr(begin, end - begin);
}

// True when the text is empty after trimming ASCII whitespace and U+00A0.
bool is_blank(const std::string& s) {
  std::size_t i = 0;
  while (i < s.size()) {
    if (is_ascii_space(s[i])) {
      ++i;
    } else if (static_cast<unsigned char>(s[i]) == 0xC2 && i + 1 < s.size() &&
               static_cast<unsigned char>(s[i + 1]) == 0xA0) {
      i += 2;
    } else {
      return false;
    }
  }
  return true;
}

// A paragraph that may sit between two parts of one logical table.
bool is_gap_paragraph(const Paragraph& p) {
  if (!p.listText.empty()) return false;
  for (const Inline& item : p.runs) {
    if (!std::holds_alternative<PageBreak>(item.value)) return false;
  }
  return true;
}

int row_left(const Row& row) { return row.props.left.value_or(0); }

std::vector<const Row*> header_rows_of(const Table& table) {
  std::vector<const Row*> out;
  for (const Row& row : table.rows) {
    if (row.props.header) out.push_back(&row);
  }
  return out;
}

// The sorted rights of every cell of every data row.
std::vector<int> data_rights(const Table& table) {
  std::vector<int> out;
  for (const Row& row : table.rows) {
    if (row.props.header) continue;
    for (const Cell& cell : row.cells) out.push_back(cell.right);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

bool same_header_rows(const std::vector<const Row*>& a, const std::vector<const Row*>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i]->cells.size() != b[i]->cells.size()) return false;
    for (std::size_t c = 0; c < a[i]->cells.size(); ++c) {
      if (a[i]->cells[c].right != b[i]->cells[c].right) return false;
      if (cell_text(a[i]->cells[c]) != cell_text(b[i]->cells[c])) return false;
    }
  }
  return true;
}

// Whether `next` continues the logical table that `prev` belongs to.
bool continues(const Table& prev, const Table& next) {
  const std::vector<const Row*> next_headers = header_rows_of(next);
  if (!next_headers.empty()) {
    const std::vector<const Row*> prev_headers = header_rows_of(prev);
    return !prev_headers.empty() && same_header_rows(prev_headers, next_headers);
  }
  if (next.rows.empty()) return false;
  const std::vector<int> rights = data_rights(prev);
  for (const Cell& cell : next.rows.front().cells) {
    if (!std::binary_search(rights.begin(), rights.end(), cell.right)) return false;
  }
  return true;
}

struct PartRef {
  const Table* table = nullptr;
  int section = 0;
  int block = 0;
};

// Index of `value` in the sorted `edges`; 0 when it is missing.
int edge_index(const std::vector<int>& edges, int value) {
  auto it = std::lower_bound(edges.begin(), edges.end(), value);
  if (it == edges.end() || *it != value) return 0;
  return static_cast<int>(it - edges.begin());
}

// The grid columns [first, last) that a span from `left` to `right` overlaps.
std::pair<int, int> covered_columns(const std::vector<int>& edges, int left, int right) {
  const int columns = edges.empty() ? 0 : static_cast<int>(edges.size()) - 1;
  int first = static_cast<int>(std::upper_bound(edges.begin(), edges.end(), left) - edges.begin()) - 1;
  if (first < 0) first = 0;
  int last = static_cast<int>(std::lower_bound(edges.begin(), edges.end(), right) - edges.begin());
  if (last > columns) last = columns;
  return {first, last};
}

std::string header_name(const Cell& cell) {
  std::string text = cell_text(cell);
  std::replace(text.begin(), text.end(), '\n', ' ');
  return trim(text);
}

std::vector<std::string> column_names(const std::vector<int>& edges, const std::vector<Row>& header_rows) {
  const int columns = edges.empty() ? 0 : static_cast<int>(edges.size()) - 1;
  std::vector<std::string> names(static_cast<std::size_t>(columns));

  for (std::size_t r = header_rows.size(); r-- > 0;) {
    const Row& row = header_rows[r];
    const bool is_last = r + 1 == header_rows.size();
    int left = row_left(row);
    for (const Cell& cell : row.cells) {
      const std::string text = header_name(cell);
      const std::pair<int, int> range = covered_columns(edges, left, cell.right);
      left = cell.right;
      for (int c = range.first; c < range.second; ++c) {
        std::string& name = names[static_cast<std::size_t>(c)];
        if (is_last) {
          name = text;
        } else if (!text.empty()) {
          name = name.empty() ? text : text + " " + name;
        }
      }
    }
  }

  std::set<std::string> used;
  for (int c = 0; c < columns; ++c) {
    std::string& name = names[static_cast<std::size_t>(c)];
    if (name.empty()) name = "column_" + std::to_string(c + 1);
    std::string candidate = name;
    for (int n = 2; used.count(candidate) != 0; ++n) candidate = name + "_" + std::to_string(n);
    used.insert(candidate);
    name = candidate;
  }
  return names;
}

LogicalRow logical_row(const Row& row, const std::vector<int>& edges) {
  LogicalRow out;
  out.spacer = true;
  int left = row_left(row);
  for (const Cell& cell : row.cells) {
    LogicalCell lc;
    lc.text = cell_text(cell);
    lc.column = edge_index(edges, left);
    lc.span = std::max(1, edge_index(edges, cell.right) - lc.column);
    if (!is_blank(lc.text)) out.spacer = false;
    out.cells.push_back(std::move(lc));
    left = cell.right;
  }
  if (!row.cells.empty()) {
    for (const Block& block : row.cells.front().blocks) {
      if (const auto* p = std::get_if<Paragraph>(&block.value)) {
        out.indent = p->props.indentLeft.value_or(0);
        break;
      }
    }
  }
  return out;
}

LogicalTable build_table(int id, const std::vector<PartRef>& parts) {
  LogicalTable table;
  table.id = id;
  table.firstSection = parts.front().section;
  table.lastSection = parts.back().section;

  for (const PartRef& part : parts) table.parts.push_back(TablePart{part.section, part.block});

  for (const Row& row : parts.front().table->rows) {
    if (row.props.header) table.headerRows.push_back(row);
  }

  for (const PartRef& part : parts) {
    for (const Row& row : part.table->rows) {
      if (row.props.header) continue;
      table.edges.push_back(row_left(row));
      for (const Cell& cell : row.cells) table.edges.push_back(cell.right);
    }
  }
  std::sort(table.edges.begin(), table.edges.end());
  table.edges.erase(std::unique(table.edges.begin(), table.edges.end()), table.edges.end());

  for (const PartRef& part : parts) {
    for (const Row& row : part.table->rows) {
      if (!row.props.header) table.rows.push_back(logical_row(row, table.edges));
    }
  }

  table.columnNames = column_names(table.edges, table.headerRows);
  return table;
}

}  // namespace

std::vector<LogicalTable> logical_tables(const RtfDocument& document) {
  std::vector<LogicalTable> result;
  std::vector<PartRef> group;
  bool gap_clean = false;  // only gap paragraphs since the last part

  auto flush = [&]() {
    if (group.empty()) return;
    result.push_back(build_table(static_cast<int>(result.size()) + 1, group));
    group.clear();
  };

  for (std::size_t s = 0; s < document.sections.size(); ++s) {
    const std::vector<Block>& body = document.sections[s].body;
    for (std::size_t b = 0; b < body.size(); ++b) {
      if (const auto* p = std::get_if<Paragraph>(&body[b].value)) {
        if (!is_gap_paragraph(*p)) gap_clean = false;
        continue;
      }
      const auto* table = std::get_if<Table>(&body[b].value);
      if (table == nullptr) continue;
      if (group.empty() || !gap_clean || !continues(*group.back().table, *table)) flush();
      group.push_back(PartRef{table, static_cast<int>(s), static_cast<int>(b)});
      gap_clean = true;
    }
  }
  flush();
  return result;
}

}  // namespace rtf
