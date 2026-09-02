// test_util.cpp — pure-logic unit test for util.cpp (no POSIX deps).
// Compiles on any C++20 toolchain (including MSVC).
#include <cassert>
#include <cstdio>
#include <string>

#include "util.h"

static int failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL: %s (line %d)\n", #cond, __LINE__);               \
      failures++;                                                         \
    }                                                                     \
  } while (0)

int main() {
  // --- uuid4: format 8-4-4-4-12, version 4 ---
  std::string id = util::uuid4();
  CHECK(id.size() == 36);
  CHECK(id[8] == '-');
  CHECK(id[13] == '-');
  CHECK(id[18] == '-');
  CHECK(id[23] == '-');
  CHECK(id[14] == '4');  // version nibble

  // --- base64 round-trip ---
  std::string plain = "Hello, WhatsApp! 123 \u00e9\u00e8";
  std::string enc = util::base64_encode(plain);
  std::string dec = util::base64_decode(enc);
  CHECK(dec == plain);

  // --- base64 known vector: "hello" -> "aGVsbG8=" ---
  CHECK(util::base64_encode("hello") == "aGVsbG8=");

  // --- trim ---
  CHECK(util::trim("  hi  ") == "hi");
  CHECK(util::trim("\t\n x \r\n") == "x");

  // --- constant_time_equals ---
  CHECK(util::constant_time_equals("abc", "abc"));
  CHECK(!util::constant_time_equals("abc", "abd"));
  CHECK(!util::constant_time_equals("abc", "ab"));  // length mismatch

  // --- random_hex length ---
  CHECK(util::random_hex(8).size() == 16);

  // --- now_iso8601 / epoch_ms basic sanity ---
  std::string ts = util::now_iso8601();
  CHECK(ts.size() >= 20);
  CHECK(ts.back() == 'Z');
  CHECK(util::epoch_ms() > 1700000000000LL);

  if (failures == 0) {
    std::printf("ALL TESTS PASSED\n");
    return 0;
  }
  std::printf("%d TEST(S) FAILED\n", failures);
  return 1;
}
