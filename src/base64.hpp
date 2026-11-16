#pragma once
// Base64 encoder (standard alphabet, "=" padding).
#include <cstddef>
#include <string>
#include <string_view>

namespace rtf::detail {

inline std::string base64(std::string_view bytes) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((bytes.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 3 <= bytes.size(); i += 3) {
    unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                 static_cast<unsigned char>(bytes[i + 2]);
    out += alphabet[(v >> 18) & 63];
    out += alphabet[(v >> 12) & 63];
    out += alphabet[(v >> 6) & 63];
    out += alphabet[v & 63];
  }
  std::size_t rest = bytes.size() - i;
  if (rest == 1) {
    unsigned v = static_cast<unsigned char>(bytes[i]) << 16;
    out += alphabet[(v >> 18) & 63];
    out += alphabet[(v >> 12) & 63];
    out += "==";
  } else if (rest == 2) {
    unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8);
    out += alphabet[(v >> 18) & 63];
    out += alphabet[(v >> 12) & 63];
    out += alphabet[(v >> 6) & 63];
    out += '=';
  }
  return out;
}

}  // namespace rtf::detail
