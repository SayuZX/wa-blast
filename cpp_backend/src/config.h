// config.h — load & expose config.json settings.
#pragma once

#include <string>
#include <vector>

namespace conf {

struct Profile {
  std::string name;
  int android_user = 0;
  std::string imei;
  std::string android_id;
  std::string device_model;
};

struct BlastConfig {
  int delay_between_seconds = 8;
  int max_retries = 3;
  std::vector<int> backoff_seconds{5, 10, 20};
  bool preflight_check = true;
  std::string lock_file = "/data/local/tmp/.wa_lock";
  bool fallback_clipboard = true;
};

struct Config {
  std::string host = "0.0.0.0";
  int port = 8080;
  std::string api_key = "CHANGE_ME_GENERATE_A_RANDOM_KEY";
  int threads = 4;

  std::string adb_path = "adb";
  std::string adb_serial;
  std::string target_package = "com.whatsapp";
  std::string target_activity = "com.whatsapp.Main";

  BlastConfig blast;

  std::string db_path = "./wa_harness.db";
  int log_retention_days = 7;

  std::vector<Profile> profiles;
};

// Load config from path; returns false + sets err on failure.
bool load(const std::string& path, Config& out, std::string& err);

}  // namespace conf
