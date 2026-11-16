#pragma once
// Helpers shared by the reader tests. A failed lookup is reported with
// CHECK and returns an empty object, so one bad index does not crash the test.
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "check.hpp"
#include "rtf/reader.hpp"

// Parses the input and checks that it was accepted as RTF.
inline rtf::RtfDocument parse_ok(std::string_view rtf) {
  rtf::ParseResult result = rtf::parse(rtf);
  CHECK(result.ok);
  return std::move(result.document);
}

// Returns block number `block` of section number `section`, which must be a paragraph.
inline const rtf::Paragraph& body_paragraph(const rtf::RtfDocument& doc, std::size_t section, std::size_t block) {
  static const rtf::Paragraph empty;
  CHECK(section < doc.sections.size());
  if (section >= doc.sections.size()) return empty;
  const auto& body = doc.sections[section].body;
  CHECK(block < body.size());
  if (block >= body.size()) return empty;
  const rtf::Paragraph* p = std::get_if<rtf::Paragraph>(&body[block].value);
  CHECK(p != nullptr);
  return p != nullptr ? *p : empty;
}

// Concatenates the text of every TextRun of the paragraph.
inline std::string run_text(const rtf::Paragraph& p) {
  std::string out;
  for (const auto& item : p.runs) {
    if (const auto* t = std::get_if<rtf::TextRun>(&item.value)) out += t->text;
  }
  return out;
}

// Returns inline number i of the paragraph, which must be a TextRun.
inline const rtf::TextRun& run(const rtf::Paragraph& p, std::size_t i) {
  static const rtf::TextRun empty;
  CHECK(i < p.runs.size());
  if (i >= p.runs.size()) return empty;
  const rtf::TextRun* t = std::get_if<rtf::TextRun>(&p.runs[i].value);
  CHECK(t != nullptr);
  return t != nullptr ? *t : empty;
}

inline std::size_t count_warnings(const rtf::RtfDocument& doc, const char* code) {
  std::size_t n = 0;
  for (const auto& w : doc.warnings) {
    if (w.code == code) ++n;
  }
  return n;
}

inline bool has_warning(const rtf::RtfDocument& doc, const char* code) { return count_warnings(doc, code) > 0; }

// Returns block number `block` of section number `section`, which must be a table.
inline const rtf::Table& body_table(const rtf::RtfDocument& doc, std::size_t section, std::size_t block) {
  static const rtf::Table empty;
  CHECK(section < doc.sections.size());
  if (section >= doc.sections.size()) return empty;
  const auto& body = doc.sections[section].body;
  CHECK(block < body.size());
  if (block >= body.size()) return empty;
  const rtf::Table* t = std::get_if<rtf::Table>(&body[block].value);
  CHECK(t != nullptr);
  return t != nullptr ? *t : empty;
}

// The text of the cell's paragraphs, joined with "\n".
inline std::string cell_plain(const rtf::Cell& cell) {
  std::string out;
  bool first = true;
  for (const auto& block : cell.blocks) {
    const rtf::Paragraph* p = std::get_if<rtf::Paragraph>(&block.value);
    if (p == nullptr) continue;
    if (!first) out += "\n";
    out += run_text(*p);
    first = false;
  }
  return out;
}
