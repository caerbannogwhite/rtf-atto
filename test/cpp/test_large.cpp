// Parses a SAS-style table of 110,000 rows built in memory and checks the
// result and the parse time.
// Usage: test_large [seconds=30]
// When the argument is absent, the variable RTF_LARGE_SECONDS sets the budget.
// MSVC marks std::getenv as unsafe. It is safe here.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "check.hpp"
#include "helpers.hpp"
#include "rtf/reader.hpp"
#include "rtf/tables.hpp"
#include "rtf/text.hpp"

static const std::size_t kRows = 110000;

static std::string build_input() {
  std::string s =
      "{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0{\\fonttbl{\\f0\\fmodern\\fprq1\\fcharset0 Courier New;}}"
      "\\paperw16837\\paperh11905\\sectd\\lndscpsxn";
  s.reserve(32u * 1024u * 1024u);
  for (std::size_t k = 1; k <= kRows; ++k) {
    s += "\\trowd\\trkeep\\trqc\\cellx2000\\cellx4000\\cellx6000\\cellx8000";
    for (int j = 1; j <= 4; ++j) {
      s += "{\\pard\\plain\\intbl R";
      s += std::to_string(k);
      s += "-C";
      s += std::to_string(j);
      s += "\\cell}";
    }
    s += "{\\row}";
  }
  s += "}";
  return s;
}

int main(int argc, char** argv) {
  double budget = 30.0;
  const char* text = argc > 1 ? argv[1] : std::getenv("RTF_LARGE_SECONDS");
  if (text != nullptr) {
    double value = std::strtod(text, nullptr);
    if (value > 0.0) budget = value;
  }
  std::string input = build_input();

  auto start = std::chrono::steady_clock::now();
  rtf::ParseResult result = rtf::parse(input);
  double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

  CHECK(result.ok);
  const rtf::RtfDocument& doc = result.document;
  CHECK_EQ(doc.sections.size(), std::size_t(1));
  if (!doc.sections.empty()) {
    CHECK_EQ(doc.sections[0].body.size(), std::size_t(1));
    const rtf::Table& table = body_table(doc, 0, 0);
    CHECK_EQ(table.rows.size(), kRows);
  }
  CHECK(rtf::to_text(doc).size() > 1024u * 1024u);

  std::vector<rtf::LogicalTable> tables = rtf::logical_tables(doc);
  CHECK_EQ(tables.size(), std::size_t(1));
  if (!tables.empty()) {
    CHECK_EQ(tables[0].rows.size(), kRows);
    CHECK_EQ(tables[0].columnNames.size(), std::size_t(4));
    if (tables[0].columnNames.size() == 4) {
      CHECK_EQ(tables[0].columnNames[0], std::string("column_1"));
      CHECK_EQ(tables[0].columnNames[1], std::string("column_2"));
      CHECK_EQ(tables[0].columnNames[2], std::string("column_3"));
      CHECK_EQ(tables[0].columnNames[3], std::string("column_4"));
    }
  }

  if (seconds > budget) std::fprintf(stderr, "parse took %.2f s, budget %.2f s\n", seconds, budget);
  CHECK(seconds <= budget);
  return rtf_test::result();
}
