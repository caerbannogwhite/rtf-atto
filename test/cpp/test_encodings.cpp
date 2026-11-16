#include <cstdint>
#include <string>

#include "check.hpp"
#include "encodings.hpp"

using namespace rtf::detail;

static void charset_table() {
  CHECK_EQ(charset_to_code_page(0, 1252), 1252);
  CHECK_EQ(charset_to_code_page(1, 1250), 1250);
  CHECK_EQ(charset_to_code_page(2, 1252), kSymbolCodePage);
  CHECK_EQ(charset_to_code_page(77, 1252), 10000);
  CHECK_EQ(charset_to_code_page(128, 1252), 932);
  CHECK_EQ(charset_to_code_page(129, 1252), 949);
  CHECK_EQ(charset_to_code_page(130, 1252), 1361);
  CHECK_EQ(charset_to_code_page(134, 1252), 936);
  CHECK_EQ(charset_to_code_page(136, 1252), 950);
  CHECK_EQ(charset_to_code_page(161, 1252), 1253);
  CHECK_EQ(charset_to_code_page(162, 1252), 1254);
  CHECK_EQ(charset_to_code_page(163, 1252), 1258);
  CHECK_EQ(charset_to_code_page(177, 1252), 1255);
  CHECK_EQ(charset_to_code_page(178, 1252), 1256);
  CHECK_EQ(charset_to_code_page(186, 1252), 1257);
  CHECK_EQ(charset_to_code_page(204, 1252), 1251);
  CHECK_EQ(charset_to_code_page(222, 1252), 874);
  CHECK_EQ(charset_to_code_page(238, 1252), 1250);
  CHECK_EQ(charset_to_code_page(254, 1252), 437);
  CHECK_EQ(charset_to_code_page(255, 1252), 850);
  CHECK_EQ(charset_to_code_page(99, 1252), 1252);
}

static void single_byte_tables() {
  CHECK_EQ(decode_byte(1252, 0x41), 0x41u);
  CHECK_EQ(decode_byte(1252, 0x80), 0x20ACu);
  CHECK_EQ(decode_byte(1252, 0x81), 0xFFFDu);
  CHECK_EQ(decode_byte(1252, 0x9F), 0x0178u);
  CHECK_EQ(decode_byte(1252, 0xE9), 0x00E9u);
  CHECK_EQ(decode_byte(1250, 0xA5), 0x0104u);
  CHECK_EQ(decode_byte(1251, 0xC0), 0x0410u);
  CHECK_EQ(decode_byte(1253, 0xC1), 0x0391u);
  CHECK_EQ(decode_byte(1254, 0xDD), 0x0130u);
  CHECK_EQ(decode_byte(1255, 0xE0), 0x05D0u);
  CHECK_EQ(decode_byte(1256, 0xC7), 0x0627u);
  CHECK_EQ(decode_byte(1257, 0xC0), 0x0104u);
  CHECK_EQ(decode_byte(1258, 0xD5), 0x01A0u);
  CHECK_EQ(decode_byte(874, 0xA1), 0x0E01u);
  CHECK_EQ(decode_byte(437, 0x80), 0x00C7u);
  CHECK_EQ(decode_byte(437, 0xE1), 0x00DFu);
  CHECK_EQ(decode_byte(850, 0x9E), 0x00D7u);
  CHECK_EQ(decode_byte(852, 0x85), 0x016Fu);
  CHECK_EQ(decode_byte(866, 0x80), 0x0410u);
  CHECK_EQ(decode_byte(10000, 0xA5), 0x2022u);
  CHECK_EQ(decode_byte(10000, 0xDB), 0x20ACu);
  CHECK_EQ(decode_byte(kSymbolCodePage, 0xB7), 0xF0B7u);
  CHECK_EQ(decode_byte(932, 0x82), 0xFFFDu);
  CHECK(code_page_supported(1252));
  CHECK(!code_page_supported(932));
  CHECK(!code_page_supported(65001));
}

static void utf8_output() {
  std::string s;
  append_utf8(s, 0x41);
  append_utf8(s, 0xE9);
  append_utf8(s, 0x20AC);
  append_utf8(s, 0x1F605);
  CHECK_EQ(s, std::string("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x85"));
  s.clear();
  append_utf8(s, 0xD800);
  append_utf8(s, 0x110000);
  CHECK_EQ(s, std::string("\xEF\xBF\xBD\xEF\xBF\xBD"));
}

int main() {
  charset_table();
  single_byte_tables();
  utf8_output();
  return rtf_test::result();
}
