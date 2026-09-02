// titanium.cpp — data-isolation based profile switching (Titanium-style).
#include "titanium.h"

#include <chrono>
#include <thread>

#include "adb.h"
#include "config.h"

namespace titanium {

static const conf::Config* g_cfg = nullptr;
void set_config(const conf::Config* cfg) { g_cfg = cfg; }

std::string profiles_dir() {
  return "/data/local/tmp/wa_profiles";
}

std::vector<std::string> list_profiles() {
  std::vector<std::string> out;
  adb::Result r = adb::shell("ls " + profiles_dir());
  // Parse ls output (one name per line).
  std::string line;
  for (char c : r.stdout_) {
    if (c == '\n') {
      if (!line.empty() && line != "lost+found") out.push_back(line);
      line.clear();
    } else if (c != '\r') {
      line += c;
    }
  }
  return out;
}

std::string pkg() { return g_cfg ? g_cfg->target_package : "com.whatsapp"; }

bool snapshot(const std::string& pkg, const std::string& profile, std::string& err) {
  std::string dest = profiles_dir() + "/" + profile;
  std::string src = "/data/data/" + pkg;
  std::string cmd =
      "mkdir -p " + dest + " && rm -rf " + dest + "/* && cp -rf " + src + "/* " + dest + "/";
  adb::Result r = adb::shell(cmd);
  if (!r.ok()) { err = "snapshot failed: " + r.stdout_; return false; }
  return true;
}

bool restore(const std::string& pkg, const std::string& profile, std::string& err) {
  std::string src = profiles_dir() + "/" + profile;
  std::string dest = "/data/data/" + pkg;
  std::string cmd =
      "rm -rf " + dest + "/* && mkdir -p " + dest + " && cp -rf " + src + "/* " + dest + "/";
  adb::Result r = adb::shell(cmd);
  if (!r.ok()) { err = "restore failed: " + r.stdout_; return false; }

  // Fix ownership (app runs as its own uid, not root).
  adb::shell("chown -R $(stat -c %u " + dest + " 2>/dev/null || echo 0):"
             "$(stat -c %g " + dest + " 2>/dev/null || echo 0) " + dest);
  return true;
}

bool switch_to(const std::string& pkg,
               const std::string& from_profile,
               const std::string& to_profile,
               bool snapshot_current,
               std::string& err) {
  if (snapshot_current && !from_profile.empty()) {
    if (!snapshot(pkg, from_profile, err)) return false;
  }
  if (!restore(pkg, to_profile, err)) return false;

  // Relaunch the app.
  adb::force_stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  adb::start_app();
  return true;
}

}  // namespace titanium
