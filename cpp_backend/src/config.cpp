// config.cpp — parse config.json (nlohmann/json).
#include "config.h"

#include <fstream>
#include <sstream>

#include "nlohmann/json.hpp"

namespace conf {

using nlohmann::json;

static std::string read_file(const std::string& path) {
  std::ifstream f(path);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static std::string s(const json& j, const std::string& k, const std::string& d) {
  if (j.contains(k) && j[k].is_string()) return j[k].get<std::string>();
  return d;
}

static int i(const json& j, const std::string& k, int d) {
  if (j.contains(k) && j[k].is_number_integer()) return j[k].get<int>();
  return d;
}

static bool b(const json& j, const std::string& k, bool d) {
  if (j.contains(k) && j[k].is_boolean()) return j[k].get<bool>();
  return d;
}

bool load(const std::string& path, Config& out, std::string& err) {
  std::string raw = read_file(path);
  if (raw.empty()) {
    err = "cannot read config file: " + path;
    return false;
  }
  json j;
  try {
    j = json::parse(raw);
  } catch (const std::exception& e) {
    err = std::string("config JSON parse error: ") + e.what();
    return false;
  }

  if (j.contains("server")) {
    const auto& srv = j["server"];
    out.host = s(srv, "host", out.host);
    out.port = i(srv, "port", out.port);
    out.api_key = s(srv, "api_key", out.api_key);
    out.threads = i(srv, "threads", out.threads);
  }

  if (j.contains("adb")) {
    const auto& a = j["adb"];
    out.adb_path = s(a, "path", out.adb_path);
    out.adb_serial = s(a, "serial", out.adb_serial);
    out.target_package = s(a, "target_package", out.target_package);
    out.target_activity = s(a, "target_activity", out.target_activity);
  }

  if (j.contains("blast")) {
    const auto& bl = j["blast"];
    out.blast.delay_between_seconds = i(bl, "delay_between_seconds", out.blast.delay_between_seconds);
    out.blast.max_retries = i(bl, "max_retries", out.blast.max_retries);
    out.blast.preflight_check = b(bl, "preflight_check", out.blast.preflight_check);
    out.blast.lock_file = s(bl, "lock_file", out.blast.lock_file);
    out.blast.fallback_clipboard = b(bl, "fallback_clipboard", out.blast.fallback_clipboard);
    if (bl.contains("backoff_seconds") && bl["backoff_seconds"].is_array()) {
      out.blast.backoff_seconds.clear();
      for (const auto& v : bl["backoff_seconds"])
        out.blast.backoff_seconds.push_back(v.get<int>());
    }
  }

  if (j.contains("storage")) {
    const auto& st = j["storage"];
    out.db_path = s(st, "db_path", out.db_path);
    out.log_retention_days = i(st, "log_retention_days", out.log_retention_days);
  }

  if (j.contains("profiles") && j["profiles"].is_array()) {
    out.profiles.clear();
    for (const auto& p : j["profiles"]) {
      Profile pr;
      pr.name = s(p, "name", "");
      pr.android_user = i(p, "android_user", 0);
      pr.imei = s(p, "imei", "");
      pr.android_id = s(p, "android_id", "");
      pr.device_model = s(p, "device_model", "");
      if (!pr.name.empty()) out.profiles.push_back(pr);
    }
  }

  return true;
}

}  // namespace conf
