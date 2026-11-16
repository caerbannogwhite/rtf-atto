#include "tokenizer.hpp"

namespace rtf {
namespace detail {

namespace {

bool is_letter(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(unsigned char c) { return c >= '0' && c <= '9'; }

// Returns the value of a hex digit, or -1 when c is not one.
int hex_value(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

Tokenizer::Tokenizer(const unsigned char* data, std::size_t size) : data_(data), size_(size) {}

std::size_t Tokenizer::position() const { return pos_; }

Token Tokenizer::next() {
  // CR and LF outside \bin only end a text run. They are dropped.
  while (pos_ < size_ && (data_[pos_] == '\r' || data_[pos_] == '\n')) ++pos_;

  Token tok;
  tok.offset = pos_;
  if (pos_ >= size_) return tok;  // End

  const unsigned char c = data_[pos_];
  if (c == '{') {
    ++pos_;
    tok.kind = TokenKind::GroupOpen;
    return tok;
  }
  if (c == '}') {
    ++pos_;
    tok.kind = TokenKind::GroupClose;
    return tok;
  }
  if (c == '\t') {
    ++pos_;
    tok.kind = TokenKind::ControlWord;
    tok.word = std::string_view("tab");
    return tok;
  }
  if (c == '\\') return control(pos_);

  const std::size_t begin = pos_;
  while (pos_ < size_) {
    const unsigned char d = data_[pos_];
    if (d == '\\' || d == '{' || d == '}' || d == '\r' || d == '\n' || d == '\t') break;
    ++pos_;
  }
  tok.kind = TokenKind::Text;
  tok.text = std::string_view(reinterpret_cast<const char*>(data_ + begin), pos_ - begin);
  return tok;
}

// Reads a token that starts with a backslash at index start.
Token Tokenizer::control(std::size_t start) {
  Token tok;
  tok.offset = start;
  pos_ = start + 1;
  if (pos_ >= size_) {
    // A lone backslash at the end of the input.
    tok.clamped = true;
    return tok;  // End
  }

  const unsigned char c = data_[pos_];
  if (!is_letter(c)) {
    if (c == '\r' || c == '\n') {
      ++pos_;
      tok.kind = TokenKind::ControlWord;
      tok.word = std::string_view("par");
      return tok;
    }
    if (c == '\'' && pos_ + 2 < size_ && hex_value(data_[pos_ + 1]) >= 0 &&
        hex_value(data_[pos_ + 2]) >= 0) {
      tok.kind = TokenKind::HexByte;
      tok.symbol = static_cast<unsigned char>(hex_value(data_[pos_ + 1]) * 16 + hex_value(data_[pos_ + 2]));
      pos_ += 3;
      return tok;
    }
    ++pos_;
    tok.kind = TokenKind::ControlSymbol;
    tok.symbol = c;
    return tok;
  }

  // A control word: letters, then an optional minus sign and digits.
  const std::size_t word_begin = pos_;
  while (pos_ < size_ && is_letter(data_[pos_])) ++pos_;
  tok.kind = TokenKind::ControlWord;
  tok.word = std::string_view(reinterpret_cast<const char*>(data_ + word_begin), pos_ - word_begin);

  bool negative = false;
  std::size_t digits_at = pos_;
  if (digits_at < size_ && data_[digits_at] == '-') ++digits_at;
  if (digits_at < size_ && is_digit(data_[digits_at])) {
    negative = digits_at != pos_;
    std::int64_t value = 0;
    int count = 0;
    while (digits_at < size_ && is_digit(data_[digits_at])) {
      // Eleven digits fit in 64 bits. Later digits are read but not added.
      if (count < 11) value = value * 10 + (data_[digits_at] - '0');
      ++count;
      ++digits_at;
    }
    pos_ = digits_at;
    if (negative) value = -value;
    // Too many digits, or a value outside 32 bits, clamp toward the sign.
    if (count > 11 || value > INT32_MAX || value < INT32_MIN) {
      value = negative ? INT32_MIN : INT32_MAX;
      tok.clamped = true;
    }
    tok.hasParam = true;
    tok.param = static_cast<std::int32_t>(value);
  }
  // One space after the word or its parameter belongs to the word.
  if (pos_ < size_ && data_[pos_] == ' ') ++pos_;

  if (tok.hasParam && tok.word == "u") {
    tok.kind = TokenKind::Unicode;
  } else if (tok.hasParam && tok.word == "bin") {
    const std::size_t want = tok.param > 0 ? static_cast<std::size_t>(tok.param) : 0;
    std::size_t have = size_ - pos_;
    if (want > have) {
      tok.clamped = true;
    } else {
      have = want;
    }
    tok.kind = TokenKind::Binary;
    tok.word = std::string_view();
    tok.hasParam = false;
    tok.param = 0;
    tok.text = std::string_view(reinterpret_cast<const char*>(data_ + pos_), have);
    pos_ += have;
  }
  return tok;
}

}  // namespace detail
}  // namespace rtf
