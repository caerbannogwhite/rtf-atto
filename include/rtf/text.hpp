#pragma once
// The plain-text projection of the document model.
#include <string>
#include <vector>

#include "rtf/model.hpp"

namespace rtf {

// TextRun its text; Tab "\t"; LineBreak "\n"; PageBreak "\f"; ColumnBreak "\n";
// Field its result; Footnote, Shape and EmbeddedObject their blocks; the rest nothing.
std::string inline_text(const std::vector<Inline>& inlines);

// The cell's blocks, paragraphs joined with "\n", no trailing "\n".
std::string cell_text(const Cell& cell);

// Headers, footers and body of every section, in the order of the plan.
std::string to_text(const RtfDocument& document);

}  // namespace rtf
