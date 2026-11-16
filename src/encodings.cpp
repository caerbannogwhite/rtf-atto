#include "encodings.hpp"

#include <cstdint>

namespace rtf {
namespace detail {

// Defines the table arrays and code_page_table(). Only this file includes it.
#include "codepages.inc"

int charset_to_code_page(int charset, int documentCodePage) {
  switch (charset) {
    case 2: return kSymbolCodePage;
    case 77: return 10000;
    case 128: return 932;
    case 129: return 949;
    case 130: return 1361;
    case 134: return 936;
    case 136: return 950;
    case 161: return 1253;
    case 162: return 1254;
    case 163: return 1258;
    case 177: return 1255;
    case 178: return 1256;
    case 186: return 1257;
    case 204: return 1251;
    case 222: return 874;
    case 238: return 1250;
    case 254: return 437;
    case 255: return 850;
    default: return documentCodePage;
  }
}

bool code_page_supported(int codePage) {
  return codePage == kSymbolCodePage || code_page_table(codePage) != nullptr;
}

std::uint32_t decode_byte(int codePage, unsigned char b) {
  if (b < 0x80) return b;
  if (codePage == kSymbolCodePage) return 0xF000u + b;
  const std::uint16_t* table = code_page_table(codePage);
  if (table == nullptr) return 0xFFFD;
  std::uint16_t value = table[b - 0x80];
  return value == 0 ? 0xFFFDu : value;
}

void append_utf8(std::string& out, std::uint32_t codePoint) {
  if ((codePoint >= 0xD800 && codePoint <= 0xDFFF) || codePoint > 0x10FFFF) codePoint = 0xFFFD;
  if (codePoint < 0x80) {
    out.push_back(static_cast<char>(codePoint));
  } else if (codePoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else if (codePoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
  }
}

}  // namespace detail
}  // namespace rtf
