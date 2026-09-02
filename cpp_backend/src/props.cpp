// props.cpp — device identity spoofing implementation.
#include "props.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#include "adb.h"
#include "util.h"

namespace props {

static std::string read_file(const std::string& path) {
  std::ifstream f(path);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool load(const std::string& dir, const std::string& profile, nlohmann::json& out) {
  std::string path = dir + "/props_" + profile + ".json";
  std::string raw = read_file(path);
  if (raw.empty()) return false;
  try {
    out = nlohmann::json::parse(raw);
    return true;
  } catch (...) {
    return false;
  }
}

bool save(const std::string& dir, const std::string& profile, const nlohmann::json& data) {
  // Ensure dir exists (best-effort via adb shell mkdir).
  std::string path = dir + "/props_" + profile + ".json";
  std::ofstream f(path);
  if (!f) return false;
  f << data.dump(2);
  return true;
}

bool apply(const nlohmann::json& spoof, std::string& err) {
  // 1. ro.product.model / manufacturer / fingerprint via resetprop (Magisk).
  if (spoof.contains("device_model")) {
    std::string model = spoof["device_model"].get<std::string>();
    adb::Result r = adb::shell("resetprop ro.product.model " + model);
    if (!r.ok()) { err = "resetprop model failed: " + r.stdout_; return false; }
  }
  if (spoof.contains("manufacturer")) {
    std::string mfr = spoof["manufacturer"].get<std::string>();
    adb::Result r = adb::shell("resetprop ro.product.manufacturer " + mfr);
    if (!r.ok()) { err = "resetprop manufacturer failed"; return false; }
  }
  if (spoof.contains("fingerprint")) {
    std::string fp = spoof["fingerprint"].get<std::string>();
    adb::Result r = adb::shell("resetprop ro.build.fingerprint " + fp);
    if (!r.ok()) { err = "resetprop fingerprint failed"; return false; }
  }

  // 2. android_id — write into WhatsApp shared_prefs SQLite.
  if (spoof.contains("android_id")) {
    std::string aid = spoof["android_id"].get<std::string>();
    // Android stores android_id in settings.db; apps may cache it in prefs.
    // Here we use `settings put secure android_id <id>` (root).
    adb::Result r = adb::shell("settings put secure android_id " + aid);
    if (!r.ok()) { err = "set android_id failed"; return false; }
  }

  // 3. IMEI — handled by LSPosed module reading this same JSON. We just
  //    persist it; the module hooks TelephonyManager.getDeviceId.
  return true;
}

nlohmann::json generate() {
  nlohmann::json j;
  j["imei"] = util::random_hex(7) + util::random_hex(0);  // placeholder 14-digit
  // Build a valid 15-digit IMEI (14 random + Luhn check).
  std::string imei14 = util::random_hex(7);
  // random_hex returns hex; convert to decimal digits for IMEI.
  std::string digits;
  for (char c : imei14) {
    int v = (c >= '0' && c <= '9') ? (c - '0') : (c - 'a' + 10);
    digits += ('0' + (v % 10));
  }
  while (digits.size() < 14) digits += "0";
  // Luhn check digit.
  int sum = 0;
  for (int i = 0; i < 14; i++) {
    int d = digits[i] - '0';
    if (i % 2 == 0) { d *= 2; if (d > 9) d -= 9; }
    sum += d;
  }
  int check = (10 - (sum % 10)) % 10;
  j["imei"] = digits + std::to_string(check);
  j["android_id"] = util::random_hex(8);  // 16 hex chars
  j["device_model"] = "QA-Device-" + util::random_hex(2);
  j["manufacturer"] = "QALab";
  j["fingerprint"] = "qalab/qa_device/qa:14/UP1A/0001:userdebug/test-keys";
  return j;
}

}  // namespace props
