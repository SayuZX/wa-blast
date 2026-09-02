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

  // --- Anti-ban / rate-limit ---
  bool anti_ban = true;           // master switch
  int max_per_hour = 60;          // hard cap messages/hour (0 = unlimited)
  int cooldown_after = 20;        // insert a long cooldown every N messages
  int cooldown_seconds = 300;     // length of that cooldown (default 5 min)
  int jitter_seconds = 3;         // random +/- jitter added to each delay
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
  bool on_device = true;  // true: run commands directly via su (binary on device)

  BlastConfig blast;

  std::string db_path = "./wa_harness.db";
  int log_retention_days = 7;

  std::vector<Profile> profiles;

  // --- New flat fields (wa-cli / APK schema, prompt section 8) ---
  std::vector<std::string> profile_names;   // "profiles": ["WA_1","WA_2",...]
  std::string active_profile;               // "active_profile": "WA_1"
  int delay_between_messages_ms = 8000;     // "delay_between_messages_ms"
  int max_retry = 3;                        // "max_retry"
  std::string wa_package = "com.whatsapp";  // "wa_package" alias
};

// Load config from path; returns false + sets err on failure.
bool load(const std::string& path, Config& out, std::string& err);

}  // namespace conf
