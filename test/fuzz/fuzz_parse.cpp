// libFuzzer entry point: parses the input and, when it is RTF, runs every
// output pass. Build with -DRTF_FUZZ=ON and Clang.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "rtf/json.hpp"
#include "rtf/reader.hpp"
#include "rtf/tables.hpp"
#include "rtf/text.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  rtf::ParseResult result = rtf::parse(data, size);
  if (result.ok) {
    std::string json = rtf::to_json(result.document);
    std::string text = rtf::to_text(result.document);
    std::vector<rtf::LogicalTable> tables = rtf::logical_tables(result.document);
    (void)json;
    (void)text;
    (void)tables;
  }
  return 0;
}
