// rtf2json: prints the document model of an RTF file.
// Usage: rtf2json [--json|--text|--tables|--summary] [--version] <file>
// Exit code 0 on success, 1 when the file is not RTF, 2 for usage or read errors.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "rtf/json.hpp"
#include "rtf/reader.hpp"
#include "rtf/tables.hpp"
#include "rtf/text.hpp"
#include "rtf/version.hpp"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

const char* const kUsage = "usage: rtf2json [--json|--text|--tables|--summary] [--version] <file>\n";
const char* const kNotRtf = "{\"error\": \"not_rtf\"}\n";

enum class Mode { Json, Text, Tables, Summary };

void print(const std::string& text) { std::fwrite(text.data(), 1, text.size(), stdout); }

// The file name without its directory.
std::string basename_of(const std::string& path) {
  std::size_t at = path.find_last_of("/\\");
  return at == std::string::npos ? path : path.substr(at + 1);
}

std::string summary(const rtf::RtfDocument& document) {
  std::vector<rtf::LogicalTable> tables = rtf::logical_tables(document);
  std::string out = "sections: " + std::to_string(document.sections.size()) + "\n";
  out += "tables: " + std::to_string(tables.size()) + "\n";
  for (std::size_t k = 0; k < tables.size(); ++k) {
    out += "table " + std::to_string(k + 1) + ": rows " + std::to_string(tables[k].rows.size()) + ", columns:";
    for (std::size_t c = 0; c < tables[k].columnNames.size(); ++c) {
      out += c == 0 ? " " : " | ";
      out += tables[k].columnNames[c];
    }
    out += "\n";
  }
  std::set<std::string> codes;
  for (const auto& w : document.warnings) codes.insert(w.code);
  out += "warnings:";
  if (codes.empty()) out += " none";
  for (const auto& code : codes) out += " " + code;
  out += "\n";
  return out;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Write the bytes exactly as produced, with no CR added before each LF.
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  Mode mode = Mode::Json;
  bool show_version = false;
  std::string file;
  bool have_file = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--json") mode = Mode::Json;
    else if (arg == "--text") mode = Mode::Text;
    else if (arg == "--tables") mode = Mode::Tables;
    else if (arg == "--summary") mode = Mode::Summary;
    else if (arg == "--version") show_version = true;
    else if (!arg.empty() && arg[0] == '-') {
      std::fputs(kUsage, stderr);
      return 2;
    } else if (!have_file) {
      file = arg;
      have_file = true;
    } else {
      std::fputs(kUsage, stderr);
      return 2;
    }
  }
  if (show_version) {
    print(std::string(rtf::version()) + "\n");
    return 0;
  }
  if (!have_file) {
    std::fputs(kUsage, stderr);
    return 2;
  }

  std::ifstream in(file, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "rtf2json: cannot read %s\n", file.c_str());
    return 2;
  }
  std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    std::fprintf(stderr, "rtf2json: cannot read %s\n", file.c_str());
    return 2;
  }

  rtf::ParseResult result = rtf::parse(bytes);
  switch (mode) {
    case Mode::Json:
      print(result.ok ? rtf::to_json(result.document) : std::string(kNotRtf));
      break;
    case Mode::Tables:
      print(result.ok ? rtf::to_json(rtf::logical_tables(result.document)) : std::string(kNotRtf));
      break;
    case Mode::Text:
      if (result.ok) print(rtf::to_text(result.document));
      break;
    case Mode::Summary:
      print("file: " + basename_of(file) + "\n");
      print(result.ok ? summary(result.document) : std::string("error: not_rtf\n"));
      break;
  }
  return result.ok ? 0 : 1;
}
