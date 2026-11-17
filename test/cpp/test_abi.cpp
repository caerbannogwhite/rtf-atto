// Tests the C ABI in wasm/rtf_abi.h against the C++ reader.
// Usage: test_abi <fixtures_dir>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "check.hpp"
#include "rtf/json.hpp"
#include "rtf/reader.hpp"
#include "rtf/tables.hpp"
#include "rtf/version.hpp"
#include "rtf_abi.h"

static bool read_file(const std::string& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

static const unsigned char* bytes(const std::string& s) {
  return reinterpret_cast<const unsigned char*>(s.data());
}

static std::string as_string(const unsigned char* p, std::size_t n) {
  return std::string(reinterpret_cast<const char*>(p), n);
}

static void version_matches_header() {
  CHECK_EQ(std::string(rtf_version()), std::string(RTF_ATTO_VERSION));
  CHECK_EQ(std::string(rtf_version()), std::string(rtf::version()));
}

static void parse_matches_to_json(const std::string& dir) {
  const char* names[] = {"word_minimal.rtf", "sas_table_3pages.rtf"};
  for (const char* name : names) {
    std::string input;
    if (!read_file(dir + "/" + name, input)) {
      std::fprintf(stderr, "cannot read %s/%s\n", dir.c_str(), name);
      CHECK(false);
      continue;
    }
    rtf::ParseResult expected = rtf::parse(bytes(input), input.size());
    CHECK(expected.ok);

    unsigned char* out = nullptr;
    std::size_t len = 0;
    int code = rtf_parse(bytes(input), input.size(), &out, &len);
    CHECK_EQ(code, static_cast<int>(RTF_OK));
    if (out != nullptr) {
      CHECK_EQ(as_string(out, len), rtf::to_json(expected.document));
      CHECK_EQ(static_cast<int>(out[len]), 0);
    } else {
      CHECK(false);
    }
    rtf_free(out);

    out = nullptr;
    len = 0;
    code = rtf_logical_tables(bytes(input), input.size(), &out, &len);
    CHECK_EQ(code, static_cast<int>(RTF_OK));
    if (out != nullptr) {
      CHECK_EQ(as_string(out, len), rtf::to_json(rtf::logical_tables(expected.document)));
      CHECK_EQ(static_cast<int>(out[len]), 0);
    } else {
      CHECK(false);
    }
    rtf_free(out);
  }
}

// Checks one failing call: the code, the message "not_rtf" and the NUL byte.
static void check_not_rtf(int code, unsigned char* out, std::size_t len) {
  CHECK_EQ(code, static_cast<int>(RTF_ERR_NOT_RTF));
  CHECK_EQ(len, static_cast<std::size_t>(7));
  if (out != nullptr) {
    CHECK_EQ(as_string(out, len), std::string("not_rtf"));
    CHECK_EQ(static_cast<int>(out[len]), 0);
  } else {
    CHECK(false);
  }
  rtf_free(out);
}

static void not_rtf_and_empty_input() {
  const std::string hello = "hello";
  unsigned char* out = nullptr;
  std::size_t len = 0;

  int code = rtf_parse(bytes(hello), hello.size(), &out, &len);
  check_not_rtf(code, out, len);

  out = nullptr;
  len = 0;
  code = rtf_parse(nullptr, 0, &out, &len);
  check_not_rtf(code, out, len);

  out = nullptr;
  len = 0;
  code = rtf_logical_tables(bytes(hello), hello.size(), &out, &len);
  check_not_rtf(code, out, len);

  out = nullptr;
  len = 0;
  code = rtf_logical_tables(nullptr, 0, &out, &len);
  check_not_rtf(code, out, len);
}

static void invalid_arguments() {
  const std::string data = "hello";
  unsigned char* out = nullptr;
  std::size_t len = 0;

  CHECK_EQ(rtf_parse(bytes(data), 5, nullptr, &len), static_cast<int>(RTF_ERR_INVALID_ARGUMENT));
  CHECK_EQ(rtf_parse(bytes(data), 5, &out, nullptr), static_cast<int>(RTF_ERR_INVALID_ARGUMENT));

  // A non-null address and a non-zero length must be reset by the call.
  out = reinterpret_cast<unsigned char*>(&len);
  len = 99;
  CHECK_EQ(rtf_parse(nullptr, 5, &out, &len), static_cast<int>(RTF_ERR_INVALID_ARGUMENT));
  CHECK(out == nullptr);
  CHECK_EQ(len, static_cast<std::size_t>(0));
}

static void free_accepts_null() {
  rtf_free(nullptr);
}

static void deep_fields_parse() {
  std::string s = "x";
  for (int i = 0; i < 1000; ++i) s = "{\\field{\\*\\fldinst F}{\\fldrslt " + s + "}}";
  const std::string input = "{\\rtf1\\ansi " + s + "\\par}";

  unsigned char* out = nullptr;
  std::size_t len = 0;
  const int code = rtf_parse(bytes(input), input.size(), &out, &len);
  CHECK_EQ(code, static_cast<int>(RTF_OK));
  CHECK(len > 1000000);
  rtf_free(out);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: test_abi <fixtures_dir>\n");
    return 2;
  }
  version_matches_header();
  parse_matches_to_json(argv[1]);
  not_rtf_and_empty_input();
  invalid_arguments();
  free_accepts_null();
  deep_fields_parse();
  return rtf_test::result();
}
