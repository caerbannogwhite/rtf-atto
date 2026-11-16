#include <string>

#include "check.hpp"
#include "helpers.hpp"
#include "rtf/json.hpp"
#include "rtf/reader.hpp"

static void minimal_document_is_canonical() {
  auto doc = parse_ok(R"({\rtf1\ansi Hi\par})");
  const char* expected =
R"({
  "codePage": 1252,
  "colors": [],
  "defaultFont": 0,
  "fonts": {},
  "info": {},
  "page": {
    "footerDistance": 720,
    "headerDistance": 720,
    "height": 15840,
    "landscape": false,
    "marginBottom": 1440,
    "marginLeft": 1800,
    "marginRight": 1800,
    "marginTop": 1440,
    "width": 12240
  },
  "sections": [
    {
      "body": [
        {
          "kind": "paragraph",
          "props": {},
          "runs": [
            {
              "kind": "text",
              "props": {},
              "text": "Hi"
            }
          ]
        }
      ],
      "break": "page",
      "footers": {},
      "headers": {},
      "page": {
        "footerDistance": 720,
        "headerDistance": 720,
        "height": 15840,
        "landscape": false,
        "marginBottom": 1440,
        "marginLeft": 1800,
        "marginRight": 1800,
        "marginTop": 1440,
        "width": 12240
      }
    }
  ],
  "styles": [],
  "version": 1,
  "warnings": []
}
)";
  CHECK_EQ(rtf::to_json(doc), std::string(expected));
}
static void string_escapes() {
  auto doc = parse_ok("{\\rtf1 a\"b\\\\c\\line d\\tab e\\'1b\\'e9\\u20013?\\par}");
  // the paragraph has runs and breaks; only the escaping of two runs is checked here
  std::string json = rtf::to_json(doc);
  CHECK(json.find("\"text\": \"a\\\"b\\\\c\"") != std::string::npos);
  CHECK(json.find("\"text\": \"e\\u001b\xC3\xA9\xE4\xB8\xAD\"") != std::string::npos);
}
static void defaults_omitted_and_flags_present() {
  auto doc = parse_ok(R"({\rtf1 {\b\fs20 x}\par})");
  std::string json = rtf::to_json(doc);
  CHECK(json.find("\"bold\": true") != std::string::npos);
  CHECK(json.find("\"size\": 20") != std::string::npos);
  CHECK(json.find("\"italic\"") == std::string::npos);
  CHECK(json.find("\"listText\"") == std::string::npos);
}
static void fonts_keyed_by_number_and_colors_nullable() {
  auto doc = parse_ok(R"({\rtf1{\fonttbl{\f0 A;}{\f31506 B;}}{\colortbl;\red1\green2\blue3;} x\par})");
  std::string json = rtf::to_json(doc);
  CHECK(json.find("\"fonts\": {\n    \"0\": {") != std::string::npos);
  CHECK(json.find("\"31506\": {") != std::string::npos);
  CHECK(json.find("\"colors\": [\n    null,\n    {\n      \"b\": 3,\n      \"g\": 2,\n      \"r\": 1\n    }\n  ]") != std::string::npos);
}
static void pictures_are_base64_and_bookmarks_have_positions() {
  auto doc = parse_ok(R"({\rtf1 {\pict\pngblip 000102}{\pict\pngblip 00010203}{\*\bkmkstart A}x{\*\bkmkend A}\par})");
  std::string json = rtf::to_json(doc);
  CHECK(json.find("\"data\": \"AAEC\"") != std::string::npos);
  CHECK(json.find("\"data\": \"AAECAw==\"") != std::string::npos);
  CHECK(json.find("\"position\": \"start\"") != std::string::npos);
  CHECK(json.find("\"position\": \"end\"") != std::string::npos);
}
static void deterministic() {
  auto doc = parse_ok(R"({\rtf1\trowd\cellx100\pard\intbl a\cell\row})");
  CHECK_EQ(rtf::to_json(doc), rtf::to_json(doc));
}

int main() {
  minimal_document_is_canonical();
  string_escapes();
  defaults_omitted_and_flags_present();
  fonts_keyed_by_number_and_colors_nullable();
  pictures_are_base64_and_bookmarks_have_positions();
  deterministic();
  return rtf_test::result();
}
