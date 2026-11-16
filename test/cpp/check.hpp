#pragma once
// Minimal test helpers. Nothing is printed when all checks pass.
#include <cstddef>
#include <cstdio>
#include <string>
#include <type_traits>

namespace rtf_test {

inline int& failures() {
  static int count = 0;
  return count;
}

template <class T, class = typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value>::type>
std::string to_string(T v) { return std::to_string(v); }
inline std::string to_string(bool v) { return v ? "true" : "false"; }
inline std::string to_string(const std::string& v) { return "\"" + v + "\""; }
inline std::string to_string(const char* v) { return std::string("\"") + v + "\""; }

inline int result() { return failures() == 0 ? 0 : 1; }

}  // namespace rtf_test

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++rtf_test::failures();                                            \
    }                                                                    \
  } while (0)

#define CHECK_EQ(a, b)                                                   \
  do {                                                                   \
    const auto& rtf_a_ = (a);                                            \
    const auto& rtf_b_ = (b);                                            \
    if (!(rtf_a_ == rtf_b_)) {                                           \
      std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s != %s\n", __FILE__, __LINE__, \
                   rtf_test::to_string(rtf_a_).c_str(),                  \
                   rtf_test::to_string(rtf_b_).c_str());                 \
      ++rtf_test::failures();                                            \
    }                                                                    \
  } while (0)
