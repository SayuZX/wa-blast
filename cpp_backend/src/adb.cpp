// adb.cpp — ADB implementation via popen(), with shell-escaping helpers.
#include "adb.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <sys/wait.h>  // WEXITSTATUS

#include "config.h"
#include "util.h"

namespace adb {

// Global config handle, set once at startup (see main.cpp).
static const conf::Config* g_cfg = nullptr;
void set_config(const conf::Config* cfg) { g_cfg = cfg; }

static std::string cfg_adb_path() { return g_cfg ? g_cfg->adb_path : "adb"; }
static std::string cfg_serial() { return g_cfg ? g_cfg->adb_serial : ""; }
static std::string cfg_pkg() { return g_cfg ? g_cfg->target_package : "com.whatsapp"; }
static std::string cfg_act() { return g_cfg ? g_cfg->target_activity : "com.whatsapp.Main"; }

// Minimal single-quote shell escaping for values we embed in commands.
static std::string shq(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  out += "'";
  return out;
}

Result run(const std::string& command) {
  Result r;
  std::array<char, 4096> buf{};
  std::string full = command + " 2>&1";
  FILE* pipe = popen(full.c_str(), "r");
  if (!pipe) {
    r.stderr_ = "popen failed";
    return r;
  }
  while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
    r.stdout_ += buf.data();
  }
  int status = pclose(pipe);
  // pclose returns wait status; extract exit code portably enough.
  r.exit_code = (status == -1) ? -1 : (WEXITSTATUS(status));
  return r;
}

Result shell(const std::string& command) {
  return run(prefix() + " shell " + command);
}

std::string prefix() {
  std::string p = cfg_adb_path();
  if (!cfg_serial().empty()) p += " -s " + cfg_serial();
  return p;
}

bool is_app_foreground() {
  Result r = shell("dumpsys window | grep -E 'mCurrentFocus|mFocusedApp'");
  // Look for the package name in the focus line.
  return r.stdout_.find(cfg_pkg()) != std::string::npos;
}

bool ensure_app_foreground() {
  // Try up to N times to bring the app to front and confirm focus.
  for (int attempt = 0; attempt < 3; attempt++) {
    if (is_app_foreground()) return true;
    start_app();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    if (is_app_foreground()) return true;
  }
  return false;
}

bool ui_contains(const std::string& needle) {
  // Dump the UI hierarchy to a temp file and grep it.
  std::string tmp = "/data/local/tmp/ui_dump.xml";
  shell("uiautomator dump " + tmp);
  Result r = shell("cat " + tmp);
  return r.stdout_.find(needle) != std::string::npos;
}

Result tap(int x, int y) {
  return shell("input tap " + std::to_string(x) + " " + std::to_string(y));
}

Result keyevent(int code) {
  return shell("input keyevent " + std::to_string(code));
}

Result input_text(const std::string& text) {
  // base64-encode to survive spaces/special chars safely through the shell.
  std::string b64 = util::base64_encode(text);
  return shell("input text " + shq(b64));
}

Result clipboard_set(const std::string& text) {
  std::string b64 = util::base64_encode(text);
  // Set clipboard via `service call clipboard` (API-level dependent). We use
  // the widely-supported approach: write text via `cmd clipboard` isn't
  // universal, so fall back to a helper shell snippet that base64-decodes.
  std::string cmd =
      "echo " + shq(b64) +
      " | base64 -d > /data/local/tmp/clip.txt && "
      "cat /data/local/tmp/clip.txt | "
      "su -c 'am broadcast -a clipper.set -e text \"$(cat /data/local/tmp/clip.txt)\"'";
  return shell(cmd);
}

Result clipboard_paste() {
  // Long-press paste: KEYCODE_PASTE (279) is not universal; use menu+paste.
  return keyevent(279);
}

Result open_chat(const std::string& number) {
  std::string uri = "https://wa.me/" + number;
  return shell("am start -a android.intent.action.VIEW -d " + shq(uri));
}

Result switch_user(int user_id) {
  return shell("am switch-user " + std::to_string(user_id));
}

Result force_stop() {
  return shell("am force-stop " + cfg_pkg());
}

Result start_app() {
  return shell("am start -n " + cfg_pkg() + "/" + cfg_act());
}

}  // namespace adb
