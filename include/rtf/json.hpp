#pragma once
// Writes the document model as canonical JSON: the same bytes as Python's
// json.dumps(obj, ensure_ascii=False, indent=2, sort_keys=True) plus one newline.
#include <string>

#include "rtf/model.hpp"

namespace rtf {

std::string to_json(const RtfDocument& doc);

}  // namespace rtf
