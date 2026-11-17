// The C ABI over the reader. See rtf_abi.h for the contract.
#include "rtf_abi.h"

#include <cstdlib>
#include <cstring>
#include <string>

#include "rtf/json.hpp"
#include "rtf/reader.hpp"
#include "rtf/tables.hpp"
#include "rtf/version.hpp"

namespace {

// Copies text and one NUL byte into a malloc'd buffer and hands it to the caller.
int deliver(const std::string& text, int code, unsigned char** out, size_t* out_len) {
  void* buffer = std::malloc(text.size() + 1);
  if (buffer == nullptr) {
    *out = nullptr;
    *out_len = 0;
    return RTF_ERR_OUT_OF_MEMORY;
  }
  std::memcpy(buffer, text.data(), text.size());
  static_cast<unsigned char*>(buffer)[text.size()] = 0;
  *out = static_cast<unsigned char*>(buffer);
  *out_len = text.size();
  return code;
}

// Checks the arguments, parses, and delivers either the JSON or the error text.
// as_tables selects the logical tables JSON instead of the document JSON.
int run(const unsigned char* data, size_t size, unsigned char** out, size_t* out_len,
        bool as_tables) {
  if (out == nullptr || out_len == nullptr) return RTF_ERR_INVALID_ARGUMENT;
  if (data == nullptr && size > 0) {
    *out = nullptr;
    *out_len = 0;
    return RTF_ERR_INVALID_ARGUMENT;
  }
  const unsigned char empty = 0;
  if (data == nullptr) data = &empty;

  const rtf::ParseResult result = rtf::parse(data, size);
  if (!result.ok) return deliver(result.error, RTF_ERR_NOT_RTF, out, out_len);
  const std::string json =
      as_tables ? rtf::to_json(rtf::logical_tables(result.document)) : rtf::to_json(result.document);
  return deliver(json, RTF_OK, out, out_len);
}

}  // namespace

extern "C" {

int rtf_parse(const unsigned char* data, size_t size, unsigned char** out, size_t* out_len) {
  return run(data, size, out, out_len, false);
}

int rtf_logical_tables(const unsigned char* data, size_t size, unsigned char** out,
                       size_t* out_len) {
  return run(data, size, out, out_len, true);
}

void rtf_free(void* p) { std::free(p); }

const char* rtf_version(void) { return rtf::version(); }

}  // extern "C"
