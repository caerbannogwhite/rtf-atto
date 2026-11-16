#pragma once
// Logical tables: the tables a reader of the printed document sees as one
// table, stitched across pages.
#include <string>
#include <vector>

#include "rtf/model.hpp"

namespace rtf {

struct LogicalCell {
  std::string text;
  int column = 0;  // index in LogicalTable::edges of the cell's left edge
  int span = 1;    // number of grid columns the cell covers, at least 1
};
struct LogicalRow {
  std::vector<LogicalCell> cells;
  bool spacer = false;
  int indent = 0;
};
struct TablePart {
  int section = 0;
  int block = 0;
};
struct LogicalTable {
  int id = 1;
  int firstSection = 0, lastSection = 0;
  std::vector<TablePart> parts;
  std::vector<int> edges;
  std::vector<std::string> columnNames;
  std::vector<Row> headerRows;
  std::vector<LogicalRow> rows;
};

std::vector<LogicalTable> logical_tables(const RtfDocument& document);

}  // namespace rtf
