#pragma once
// Internal header. The formatting state that each RTF group carries.
// Character properties use the model's CharProps as they are.
#include <cstddef>
#include <optional>

#include "rtf/model.hpp"

namespace rtf {
namespace detail {

// Paragraph properties plus the two words that place a paragraph in a table.
struct ParaState {
  ParagraphProps props;
  bool inTable = false;  // \intbl
  int itap = 0;          // \itapN, the table nesting level
  // The alignment and leader words come before the \tx they apply to.
  // They are group state, so '}' restores them.
  TabAlign pendingTabAlign = TabAlign::Left;
  TabLeader pendingTabLeader = TabLeader::None;
};

// Word keeps at most 64 tab stops per paragraph. The cap also bounds the
// copy of the paragraph state that each '{' makes.
constexpr std::size_t kMaxTabStops = 64;

// The deepest table nesting level the reader builds. A paragraph with a
// larger \itapN is treated as this level. The cap is far beyond what Word
// writes. It bounds the chain of wrapper tables that a garbage \itap can
// create.
constexpr int kMaxTableNesting = 32;

// The properties of the section being read. \sectd resets them to the
// document's page setup (design, "Sections and page setup").
struct SectionState {
  PageSetup page;
  SectionBreak break_ = SectionBreak::Page;
  std::optional<int> columns;
  bool titlePage = false;
  // \pgnstartsN. It becomes the section's pageNumberStart only when the
  // section also has \pgnrestart, in either order. \pgnrestart alone
  // gives the section a pageNumberStart of 1.
  std::optional<int> pageNumberStart;
  bool pageNumberRestart = false;

  void reset(const PageSetup& documentPage) {
    *this = SectionState{};
    page = documentPage;
  }
};

// The border that the \brdr* words fill. The last border-opening word sets
// it; \pard, \trowd, \cellx and \cell clear it.
struct BorderTarget {
  enum class Kind { None, Paragraph, Cell, Row };
  enum class Side { Top, Left, Bottom, Right, Horizontal, Vertical, Between, Bar, Box };
  Kind kind = Kind::None;
  Side side = Side::Top;
};

// True when every character property is equal. Two runs with equal
// properties merge into one.
inline bool same_char_props(const CharProps& a, const CharProps& b) {
  return a.font == b.font && a.size == b.size && a.bold == b.bold && a.italic == b.italic &&
         a.underline == b.underline && a.strike == b.strike && a.doubleStrike == b.doubleStrike &&
         a.script == b.script && a.offset == b.offset && a.color == b.color &&
         a.background == b.background && a.highlight == b.highlight && a.caps == b.caps &&
         a.smallCaps == b.smallCaps && a.hidden == b.hidden && a.outline == b.outline &&
         a.shadow == b.shadow && a.language == b.language && a.deleted == b.deleted &&
         a.inserted == b.inserted;
}

}  // namespace detail
}  // namespace rtf
