// test_config.cpp — test conf::load parsing (both flat and nested schema).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "config.h"

static int failures = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("FAIL: %s (line %d)\n", #cond, __LINE__);               \
      failures++;                                                         \
    }                                                                     \
  } while (0)

static void write(const std::string& path, const std::string& content) {
  std::ofstream f(path);
  f << content;
  f.close();
}

int main() {
  // --- Flat schema (wa-cli) ---
  const char* flat = R"({
    "profiles": ["WA_1", "WA_2", "WA_3"],
    "active_profile": "WA_1",
    "api_port": 8080,
    "api_key": "supersecret",
    "delay_between_messages_ms": 8000,
    "max_retry": 3,
    "adb_path": "/system/bin/adb",
    "wa_package": "com.whatsapp",
    "on_device": true
  })";
  write("test_flat.json", flat);
  conf::Config c1;
  std::string err;
  CHECK(conf::load("test_flat.json", c1, err));
  CHECK(c1.profile_names.size() == 3);
  CHECK(c1.profile_names[0] == "WA_1");
  CHECK(c1.active_profile == "WA_1");
  CHECK(c1.port == 8080);
  CHECK(c1.api_key == "supersecret");
  CHECK(c1.delay_between_messages_ms == 8000);
  CHECK(c1.max_retry == 3);
  CHECK(c1.target_package == "com.whatsapp");
  CHECK(c1.on_device == true);
  CHECK(c1.profiles.size() == 3);
  CHECK(c1.profiles[0].name == "WA_1");
  CHECK(c1.profiles[0].android_user == 0);   // WA_1 -> Owner
  CHECK(c1.profiles[1].android_user == 10);  // WA_2 -> secondary

  // --- Nested schema (structured profiles) ---
  const char* nested = R"({
    "server": {"port": 9000, "api_key": "abc"},
    "adb": {"target_package": "com.telegram", "on_device": false},
    "profiles": [
      {"name": "WA_A", "android_user": 10, "imei": "123456789012345", "device_model": "X"}
    ]
  })";
  write("test_nested.json", nested);
  conf::Config c2;
  CHECK(conf::load("test_nested.json", c2, err));
  CHECK(c2.port == 9000);
  CHECK(c2.target_package == "com.telegram");
  CHECK(c2.on_device == false);
  CHECK(c2.profiles.size() == 1);
  CHECK(c2.profiles[0].name == "WA_A");
  CHECK(c2.profiles[0].imei == "123456789012345");

  // --- Missing file ---
  conf::Config c3;
  CHECK(!conf::load("does_not_exist.json", c3, err));

  std::remove("test_flat.json");
  std::remove("test_nested.json");

  if (failures == 0) {
    std::printf("ALL CONFIG TESTS PASSED\n");
    return 0;
  }
  std::printf("%d CONFIG TEST(S) FAILED\n", failures);
  return 1;
}
