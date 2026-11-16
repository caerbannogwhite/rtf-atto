#pragma once
// Internal header. Turns RTF bytes into tokens. Tokens are views into the input.
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rtf {
namespace detail {

enum class TokenKind { GroupOpen, GroupClose, ControlWord, ControlSymbol, HexByte, Unicode, Binary, Text, End };

struct Token {
  TokenKind kind = TokenKind::End;
  std::string_view word;        // ControlWord: the letters, without the backslash
  bool hasParam = false;
  std::int32_t param = 0;       // ControlWord and Unicode
  unsigned char symbol = 0;     // ControlSymbol: the byte; HexByte: the value
  std::string_view text;        // Text and Binary: the bytes
  std::size_t offset = 0;       // byte offset of the token's first byte
  bool clamped = false;         // param_overflow, bin_truncated, truncated_control
};

class Tokenizer {
 public:
  Tokenizer(const unsigned char* data, std::size_t size);
  Token next();                 // End forever once the input is consumed
  std::size_t position() const;

 private:
  Token control(std::size_t start);

  const unsigned char* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

}  // namespace detail
}  // namespace rtf
