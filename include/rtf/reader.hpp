#pragma once
// Public entry point of the reader: RTF bytes in, document model out.
#include <cstddef>
#include <string>
#include <string_view>

#include "rtf/model.hpp"

namespace rtf {

struct ParseOptions {
  std::size_t maxDepth = 1024;  // groups deeper than this are flattened into their parent
};

struct ParseResult {
  bool ok = false;
  std::string error;      // "not_rtf" when the input does not start with {\rtf
  RtfDocument document;   // empty when ok is false
};

ParseResult parse(const unsigned char* data, std::size_t size, const ParseOptions& options = {});
ParseResult parse(std::string_view bytes, const ParseOptions& options = {});

}  // namespace rtf
