// adb.h — ADB execution layer: popen-based, preflight check, clipboard fallback.
#pragma once

#include <string>

namespace conf { struct Config; }

namespace adb {

// Set the global config pointer (called once at startup).
void set_config(const conf::Config* cfg);

struct Result {
  int exit_code = -1;
  std::string stdout_;
  std::string stderr_;
  bool ok() const { return exit_code == 0; }
};

// Run an adb command (single string, passed to `sh -c`). Returns captured output.
Result run(const std::string& command);

// Run adb shell command on the device.
Result shell(const std::string& command);

// Compose the `adb [-s serial]` prefix from config.
std::string prefix();

// --- UI / device helpers ---

// Is the target app in the foreground? Uses `dumpsys window`.
bool is_app_foreground();

// Bring the target app to the foreground and wait for layout to settle.
bool ensure_app_foreground();

// Dump UI hierarchy and return true if `needle` appears in it.
bool ui_contains(const std::string& needle);

// Tap / swipe / keyevent / input-text primitives.
Result tap(int x, int y);
Result keyevent(int code);
Result input_text(const std::string& text);

// Clipboard-based text injection (fallback for long/special text).
Result clipboard_set(const std::string& text);
Result clipboard_paste();

// Open a chat via ACTION_VIEW deep link.
Result open_chat(const std::string& number);

// Switch to a given Android user (profile switch).
Result switch_user(int user_id);

// Force-stop / relaunch the target app.
Result force_stop();
Result start_app();

}  // namespace adb
