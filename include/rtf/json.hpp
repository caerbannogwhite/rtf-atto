#pragma once
// Writes the document model as canonical JSON: the same bytes as Python's
// json.dumps(obj, ensure_ascii=False, indent=2, sort_keys=True) plus one newline.
#include <string>
#include <vector>

#include "rtf/model.hpp"
#include "rtf/tables.hpp"

namespace rtf {

std::string to_json(const RtfDocument& doc);
// The logical tables as a JSON array, with every field written.
std::string to_json(const std::vector<LogicalTable>& tables);

}  // namespace rtf
