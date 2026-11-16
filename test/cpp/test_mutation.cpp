// Parses random mutants of every fixture and checks that the reader stays
// within a time budget and a memory cap.
// Usage: test_mutation <fixtures_dir> [count=10000] [seed=20261116]
// The environment variable RTF_MUTATIONS, when set, overrides count.
// RTF_MUTATIONS_VERBOSE, when set, prints a summary per fixture.
// A fixture above 64 KB gets count / 10 mutants.

// MSVC deprecates std::getenv; the test reads two variables and nothing else.
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <system_error>
#include <vector>

#include "check.hpp"
#include "rtf/json.hpp"
#include "rtf/reader.hpp"
#include "rtf/tables.hpp"
#include "rtf/text.hpp"

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Allocation accounting. Every global allocation goes through these
// replacements. Each block carries a 16-byte header with its size, so the
// matching delete can subtract it from the live total.

static std::size_t g_live = 0;
static std::size_t g_peak = 0;

static constexpr std::size_t kHeader = 16;

static void count_alloc(std::size_t size) {
  g_live += size;
  if (g_live > g_peak) g_peak = g_live;
}

// Plain blocks: [size | pad][user bytes].
static void* plain_alloc(std::size_t size) {
  void* raw = std::malloc(size + kHeader);
  if (raw == nullptr) std::abort();
  *static_cast<std::size_t*>(raw) = size;
  count_alloc(size);
  return static_cast<unsigned char*>(raw) + kHeader;
}

static void plain_free(void* p) {
  if (p == nullptr) return;
  unsigned char* raw = static_cast<unsigned char*>(p) - kHeader;
  g_live -= *reinterpret_cast<std::size_t*>(raw);
  std::free(raw);
}

// Over-aligned blocks: the size and the pointer that malloc returned are
// stored in the 16 bytes just below the returned pointer.
static void* aligned_alloc_counted(std::size_t size, std::align_val_t align) {
  std::size_t a = static_cast<std::size_t>(align);
  if (a < kHeader) a = kHeader;
  void* raw = std::malloc(size + a + kHeader);
  if (raw == nullptr) std::abort();
  std::uintptr_t base = reinterpret_cast<std::uintptr_t>(raw) + kHeader;
  std::uintptr_t user = (base + a - 1) / a * a;
  std::size_t* slots = reinterpret_cast<std::size_t*>(user - kHeader);
  slots[0] = size;
  reinterpret_cast<void**>(user - kHeader)[1] = raw;
  count_alloc(size);
  return reinterpret_cast<void*>(user);
}

static void aligned_free(void* p) {
  if (p == nullptr) return;
  std::uintptr_t user = reinterpret_cast<std::uintptr_t>(p);
  g_live -= reinterpret_cast<std::size_t*>(user - kHeader)[0];
  std::free(reinterpret_cast<void**>(user - kHeader)[1]);
}

void* operator new(std::size_t size) { return plain_alloc(size); }
void* operator new[](std::size_t size) { return plain_alloc(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return plain_alloc(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return plain_alloc(size); }
void* operator new(std::size_t size, std::align_val_t a) { return aligned_alloc_counted(size, a); }
void* operator new[](std::size_t size, std::align_val_t a) { return aligned_alloc_counted(size, a); }
void* operator new(std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept {
  return aligned_alloc_counted(size, a);
}
void* operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t&) noexcept {
  return aligned_alloc_counted(size, a);
}

void operator delete(void* p) noexcept { plain_free(p); }
void operator delete[](void* p) noexcept { plain_free(p); }
void operator delete(void* p, std::size_t) noexcept { plain_free(p); }
void operator delete[](void* p, std::size_t) noexcept { plain_free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { plain_free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { plain_free(p); }
void operator delete(void* p, std::align_val_t) noexcept { aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { aligned_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { aligned_free(p); }

// ---------------------------------------------------------------------------
// Mutants.

// The time allowed for the parse and the output passes of one mutant.
static constexpr double kBudgetSeconds = 1.0;

// xorshift64. The state must never be zero.
struct Rng {
  std::uint64_t state;
  explicit Rng(std::uint64_t seed) : state(seed == 0 ? 0x9E3779B97F4A7C15ull : seed) {}
  std::uint64_t next() {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
  }
  // A number in [0, n). n must be above zero.
  std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
};

static const char kInsertSet[] = "\\{};'u-0123456789abcdefprtx ";

// A position in [0, size], so that an insertion may go at the end.
static std::size_t position(Rng& rng, const std::string& s) { return rng.below(s.size() + 1); }

static void mutate_once(Rng& rng, std::string& s) {
  switch (rng.below(5)) {
    case 0: {  // delete 1 to 16 bytes
      std::size_t at = position(rng, s);
      std::size_t n = 1 + rng.below(16);
      s.erase(at, std::min(n, s.size() - at));
      break;
    }
    case 1: {  // insert 1 to 8 bytes from the set
      std::size_t at = position(rng, s);
      std::size_t n = 1 + rng.below(8);
      std::string bytes;
      for (std::size_t i = 0; i < n; ++i) bytes += kInsertSet[rng.below(sizeof(kInsertSet) - 1)];
      s.insert(at, bytes);
      break;
    }
    case 2: {  // flip one byte to a random value
      if (s.empty()) break;
      std::size_t at = rng.below(s.size());
      s[at] = static_cast<char>(rng.below(256));
      break;
    }
    case 3: {  // truncate
      s.resize(position(rng, s));
      break;
    }
    default: {  // insert a brace
      std::size_t at = position(rng, s);
      s.insert(at, 1, rng.below(2) == 0 ? '{' : '}');
      break;
    }
  }
}

static std::string make_mutant(Rng& rng, const std::string& original) {
  std::string s = original;
  std::size_t edits = 1 + rng.below(3);
  for (std::size_t i = 0; i < edits; ++i) mutate_once(rng, s);
  return s;
}

// ---------------------------------------------------------------------------
// Files.

static bool read_file(const fs::path& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

static void write_file(const fs::path& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static std::vector<fs::path> list_fixtures(const fs::path& dir) {
  std::vector<fs::path> out;
  std::error_code ec;
  fs::directory_iterator it(dir, ec);
  if (ec) {
    std::fprintf(stderr, "cannot list %s\n", dir.string().c_str());
    CHECK(!"fixtures directory exists");
    return out;
  }
  for (; it != fs::directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (it->path().extension() == ".rtf") out.push_back(it->path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

// ---------------------------------------------------------------------------

struct Outcome {
  double seconds = 0;
  std::size_t peak = 0;  // bytes above the live total before the parse
  bool error = false;    // parse failed with an error other than not_rtf
};

// Parses the bytes and, when they are RTF, runs every output pass.
static Outcome run_passes(const std::string& bytes) {
  Outcome out;
  std::size_t live_before = g_live;
  g_peak = g_live;
  auto start = std::chrono::steady_clock::now();
  {
    rtf::ParseResult result = rtf::parse(bytes);
    if (result.ok) {
      std::string json = rtf::to_json(result.document);
      std::string text = rtf::to_text(result.document);
      std::vector<rtf::LogicalTable> tables = rtf::logical_tables(result.document);
      (void)json;
      (void)text;
      (void)tables;
    } else if (result.error != "not_rtf") {
      out.error = true;
    }
  }
  auto stop = std::chrono::steady_clock::now();
  out.seconds = std::chrono::duration<double>(stop - start).count();
  out.peak = g_peak - live_before;
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: test_mutation <fixtures_dir> [count] [seed]\n");
    return 2;
  }
  std::size_t count = argc > 2 ? static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10)) : 10000;
  std::uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 20261116ull;
  if (const char* env = std::getenv("RTF_MUTATIONS")) count = static_cast<std::size_t>(std::strtoull(env, nullptr, 10));
  const bool verbose = std::getenv("RTF_MUTATIONS_VERBOSE") != nullptr;

  std::vector<fs::path> fixtures = list_fixtures(argv[1]);
  CHECK(!fixtures.empty());
  Rng rng(seed);
  std::size_t total = 0;
  // The cap is max(100 * input size, 1 MB), so peak / cap is the memory figure that matters.
  double worst_cap_share = 0;
  for (const auto& path : fixtures) {
    std::string original;
    CHECK(read_file(path, original));
    std::size_t n = original.size() > 64 * 1024 ? count / 10 : count;
    std::string name = path.filename().string();
    double slowest = 0;
    std::size_t largest_peak = 0;
    double cap_share = 0;
    for (std::size_t i = 0; i < n; ++i) {
      std::string mutant = make_mutant(rng, original);
      Outcome o = run_passes(mutant);
      ++total;
      std::size_t cap = std::max<std::size_t>(100 * mutant.size(), 1024 * 1024);
      slowest = std::max(slowest, o.seconds);
      largest_peak = std::max(largest_peak, o.peak);
      cap_share = std::max(cap_share, static_cast<double>(o.peak) / static_cast<double>(cap));
      if (o.seconds > kBudgetSeconds || o.peak > cap || o.error) {
        write_file("mutant_failed.rtf", mutant);
        std::fprintf(stderr,
                     "mutant failed: fixture %s, iteration %zu, seed %llu, %.3f s, peak %zu bytes, "
                     "input %zu bytes%s\n",
                     name.c_str(), i, static_cast<unsigned long long>(seed), o.seconds, o.peak, mutant.size(),
                     o.error ? ", parse error" : "");
        CHECK(o.seconds <= kBudgetSeconds);
        CHECK(o.peak <= cap);
        CHECK(!o.error);
        return rtf_test::result();
      }
    }
    worst_cap_share = std::max(worst_cap_share, cap_share);
    if (verbose) {
      std::printf("%-34s %6zu mutants, slowest %.4f s, largest peak %8zu bytes, peak/cap %.3f\n", name.c_str(), n,
                  slowest, largest_peak, cap_share);
    }
  }
  if (verbose) std::printf("%zu mutants parsed, highest peak/cap %.3f\n", total, worst_cap_share);
  return rtf_test::result();
}
