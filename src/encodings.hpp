#pragma once
// Internal header. Maps RTF charsets and code pages to Unicode and writes UTF-8.
#include <cstdint>
#include <string>

namespace rtf {
namespace detail {

// Word's value for the Symbol charset. It is not a real Windows code page.
constexpr int kSymbolCodePage = 42;

// Returns the code page for an RTF \fcharset value.
// Charsets 0 and 1 and every unknown charset give documentCodePage.
int charset_to_code_page(int charset, int documentCodePage);

// True for the code pages that decode_byte has a table for.
bool code_page_supported(int codePage);

// Decodes one byte. Bytes below 0x80 return themselves.
// The Symbol code page returns 0xF000 + b for the other bytes.
// An undefined byte or an unsupported code page returns 0xFFFD.
std::uint32_t decode_byte(int codePage, unsigned char b);

// Appends a code point as UTF-8. Surrogates and values above 0x10FFFF
// are written as U+FFFD.
void append_utf8(std::string& out, std::uint32_t codePoint);

}  // namespace detail
}  // namespace rtf
