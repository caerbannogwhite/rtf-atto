#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "check.hpp"
#include "tokenizer.hpp"

using rtf::detail::Token;
using rtf::detail::TokenKind;
using rtf::detail::Tokenizer;

static std::vector<Token> all(std::string_view s) {
  Tokenizer tk(reinterpret_cast<const unsigned char*>(s.data()), s.size());
  std::vector<Token> out;
  for (;;) {
    Token t = tk.next();
    if (t.kind == TokenKind::End) break;
    out.push_back(t);
  }
  return out;
}

static void words_symbols_and_groups() {
  auto t = all(R"({\rtf1\ansi\b0 x\par})");
  CHECK_EQ(t.size(), std::size_t(7));
  CHECK(t[0].kind == TokenKind::GroupOpen);
  CHECK(t[1].kind == TokenKind::ControlWord); CHECK_EQ(std::string(t[1].word), std::string("rtf"));
  CHECK(t[1].hasParam); CHECK_EQ(t[1].param, 1);
  CHECK(!t[2].hasParam);
  CHECK_EQ(t[3].param, 0); CHECK(t[3].hasParam);
  CHECK(t[4].kind == TokenKind::Text); CHECK_EQ(std::string(t[4].text), std::string("x"));  // the space after \b0 is consumed
  CHECK_EQ(std::string(t[5].word), std::string("par"));
  CHECK(t[6].kind == TokenKind::GroupClose);
}
static void hex_unicode_and_symbols() {
  auto t = all(R"(\'e9\u8364?\u-10179\*\{\}\\\~\'zz)");
  CHECK(t[0].kind == TokenKind::HexByte); CHECK_EQ(int(t[0].symbol), 0xE9);
  CHECK(t[1].kind == TokenKind::Unicode); CHECK_EQ(t[1].param, 8364);
  CHECK(t[2].kind == TokenKind::Text); CHECK_EQ(std::string(t[2].text), std::string("?"));
  CHECK_EQ(t[3].param, -10179);
  CHECK(t[4].kind == TokenKind::ControlSymbol); CHECK_EQ(int(t[4].symbol), int('*'));
  CHECK_EQ(int(t[5].symbol), int('{')); CHECK_EQ(int(t[6].symbol), int('}')); CHECK_EQ(int(t[7].symbol), int('\\'));
  CHECK_EQ(int(t[8].symbol), int('~'));
  CHECK(t[9].kind == TokenKind::ControlSymbol); CHECK_EQ(int(t[9].symbol), int('\''));
  CHECK_EQ(std::string(t[10].text), std::string("zz"));
}
static void line_ends_tabs_and_backslash_newline() {
  auto t = all("a\r\nb\tc\\\nd");
  CHECK_EQ(std::string(t[0].text), std::string("a"));
  CHECK_EQ(std::string(t[1].text), std::string("b"));
  CHECK_EQ(std::string(t[2].word), std::string("tab"));
  CHECK_EQ(std::string(t[3].text), std::string("c"));
  CHECK_EQ(std::string(t[4].word), std::string("par"));
  CHECK_EQ(std::string(t[5].text), std::string("d"));
}
static void binary_and_clamping() {
  auto t = all("\\bin3 ab{d");          // 3 bytes: 'a' 'b' '{'
  CHECK(t[0].kind == TokenKind::Binary); CHECK_EQ(std::string(t[0].text), std::string("ab{"));
  CHECK(!t[0].clamped);
  CHECK_EQ(std::string(t[1].text), std::string("d"));
  auto u = all("\\bin10 abc");
  CHECK_EQ(std::string(u[0].text), std::string("abc")); CHECK(u[0].clamped);
  auto v = all("\\bin-5 abc");
  CHECK(v[0].kind == TokenKind::Binary); CHECK(v[0].text.empty()); CHECK_EQ(std::string(v[1].text), std::string("abc"));
  auto w = all("\\fs99999999999 x");
  CHECK_EQ(w[0].param, 2147483647); CHECK(w[0].clamped);
  auto x = all("\\fs-99999999999 x");
  CHECK_EQ(x[0].param, -2147483647 - 1); CHECK(x[0].clamped);
}
static void letters_only_and_offsets() {
  auto t = all("\\f1a \\par");
  CHECK_EQ(std::string(t[0].word), std::string("f")); CHECK_EQ(t[0].param, 1);
  CHECK_EQ(std::string(t[1].text), std::string("a "));
  CHECK_EQ(t[2].offset, std::size_t(5));
}
static void trailing_backslash() {
  Tokenizer tk(reinterpret_cast<const unsigned char*>("ab\\"), 3);
  Token a = tk.next(); CHECK(a.kind == TokenKind::Text);
  Token b = tk.next(); CHECK(b.kind == TokenKind::End); CHECK(b.clamped);
  CHECK(tk.next().kind == TokenKind::End);
}

int main() {
  words_symbols_and_groups();
  hex_unicode_and_symbols();
  line_ends_tabs_and_backslash_newline();
  binary_and_clamping();
  letters_only_and_offsets();
  trailing_backslash();
  return rtf_test::result();
}
