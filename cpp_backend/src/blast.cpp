#include "blast.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>  // rand()
#include <fstream>
#include <thread>

#include "adb.h"
#include "config.h"
#include "logger.h"
#include "simulate.h"
#include "store.h"
#include "util.h"

namespace blast {

static const conf::Config* g_cfg = nullptr;
static std::mutex g_lock_mutex;
static std::atomic<bool> g_blast_running{false};

void set_config(const conf::Config* cfg) { g_cfg = cfg; }

const char* status_str(Status s) {
  switch (s) {
    case Status::PENDING: return "PENDING";
    case Status::SENDING: return "SENDING";
    case Status::SUCCESS: return "SUCCESS";
    case Status::FAILED: return "FAILED";
    case Status::RETRY: return "RETRY";
  }
  return "UNKNOWN";
}

static std::string lock_path() {
  return g_cfg ? g_cfg->blast.lock_file : "/data/local/tmp/.wa_lock";
}

bool try_acquire_lock() {
  std::lock_guard<std::mutex> lk(g_lock_mutex);
  std::ifstream f(lock_path());
  if (f.good()) return false;  // already locked
  std::ofstream out(lock_path());
  if (!out) return false;
  out << util::now_iso8601();
  return true;
}

void release_lock() {
  std::lock_guard<std::mutex> lk(g_lock_mutex);
  std::remove(lock_path().c_str());
}

bool is_locked() {
  std::lock_guard<std::mutex> lk(g_lock_mutex);
  std::ifstream f(lock_path());
  return f.good();
}

void reset() { g_blast_running = false; }

static void log_attempt(const std::string& profile,
                        const std::string& target,
                        const std::string& message,
                        Status st,
                        int attempt,
                        int error_code,
                        const std::string& error_message,
                        long long duration_ms) {
  nlohmann::json e;
  e["log_id"] = util::uuid4();
  e["timestamp"] = util::now_iso8601();
  e["profile"] = profile;
  e["target"] = target;
  std::string preview = message.size() > 20 ? message.substr(0, 20) + "..." : message;
  e["message_preview"] = preview;
  e["status"] = status_str(st);
  e["attempt"] = attempt;
  e["error_code"] = error_code;
  e["error_message"] = error_message;
  e["duration_ms"] = duration_ms;
  store::add_log(e);
}

static int do_send_attempt(const std::string& number, const std::string& message, std::string& err) {
  // Preflight: ensure app foreground.
  if (g_cfg && g_cfg->blast.preflight_check) {
    if (!adb::ensure_app_foreground()) {
      err = "WhatsApp not focused / preflight failed (check root + dumpsys window)";
      return 1;
    }
  }

  // Open the chat via deep link.
  adb::Result r = adb::open_chat(number);
  if (!r.ok()) {
    err = "open_chat failed: " + r.stderr_;
    return 3;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  // Method 1: input text (short text).
  if (message.size() <= 256) {
    r = adb::input_text(message);
    if (r.ok()) {
      adb::keyevent(66);  // ENTER
      return 0;
    }
    // Surface the real error (e.g. "root unavailable", "Permission denied").
    err = "input_text failed: " + r.stderr_;
    // Fall through to clipboard fallback if enabled.
  }

  // Method 2: clipboard fallback (long/special text).
  if (g_cfg && g_cfg->blast.fallback_clipboard) {
    adb::Result cr = adb::clipboard_set(message);
    if (!cr.ok()) {
      err = "clipboard_set failed: " + cr.stderr_;
      return 2;
    }
    adb::clipboard_paste();
    adb::keyevent(66);  // ENTER
    err = "";
    return 0;  // assume success after paste+enter
  }

  if (err.empty()) err = "input_text failed and clipboard fallback disabled";
  return 2;
}

TargetResult send_one(const std::string& profile,
                      const std::string& number,
                      const std::string& message) {
  TargetResult res;
  res.number = number;

  // Simulation mode: skip real ADB, log a fake SUCCESS.
  if (simulate::enabled()) {
    simulate::maybe_simulate_send(profile, number, message);
    res.status = Status::SUCCESS;
    res.attempts = 1;
    res.error_code = 0;
    res.error_message = "SIMULATED";
    res.duration_ms = 0;
    return res;
  }

  int max_retries = g_cfg ? g_cfg->blast.max_retries : 3;
  const auto& backoff = g_cfg ? g_cfg->blast.backoff_seconds : std::vector<int>{5, 10, 20};

  for (int attempt = 0; attempt <= max_retries; attempt++) {
    auto t0 = util::epoch_ms();
    std::string err;
    int code = do_send_attempt(number, message, err);
    res.duration_ms = util::epoch_ms() - t0;
    res.attempts = attempt + 1;

    if (code == 0) {
      res.status = Status::SUCCESS;
      res.error_code = 0;
      res.error_message = "";
      log_attempt(profile, number, message, Status::SUCCESS, attempt + 1, 0, "", res.duration_ms);
      return res;
    }

    res.error_code = code;
    res.error_message = err;

    if (attempt < max_retries) {
      res.status = Status::RETRY;
      int wait = (attempt < (int)backoff.size()) ? backoff[attempt] : backoff.back();
      log_attempt(profile, number, message, Status::RETRY, attempt + 1, code, err, res.duration_ms);
      std::this_thread::sleep_for(std::chrono::seconds(wait));
    } else {
      res.status = Status::FAILED;
      log_attempt(profile, number, message, Status::FAILED, attempt + 1, code, err, res.duration_ms);
    }
  }
  return res;
}

void blast(const std::string& profile,
           const std::vector<std::string>& targets,
           const std::string& message,
           const std::function<void(const TargetResult&)>& on_progress) {
  g_blast_running = true;
  int delay = g_cfg ? g_cfg->blast.delay_between_seconds : 8;

  // Anti-ban settings.
  const bool anti_ban = g_cfg ? g_cfg->blast.anti_ban : true;
  const int max_per_hour = g_cfg ? g_cfg->blast.max_per_hour : 60;
  const int cooldown_after = g_cfg ? g_cfg->blast.cooldown_after : 20;
  const int cooldown_seconds = g_cfg ? g_cfg->blast.cooldown_seconds : 300;
  const int jitter_seconds = g_cfg ? g_cfg->blast.jitter_seconds : 3;

  int sent_in_window = 0;        // messages sent since last cooldown
  long long window_start = util::epoch_ms();

  for (size_t i = 0; i < targets.size(); i++) {
    if (!g_blast_running) break;  // allow cancellation

    TargetResult r = send_one(profile, targets[i], message);
    if (on_progress) on_progress(r);
    sent_in_window++;

    // --- Anti-ban: hard hourly cap ---
    if (anti_ban && max_per_hour > 0) {
      long long elapsed_s = (util::epoch_ms() - window_start) / 1000;
      if (sent_in_window >= max_per_hour && elapsed_s < 3600) {
        // Would exceed the hourly cap: stop the blast to avoid blocking.
        logger::warn("anti-ban: hourly cap reached (" + std::to_string(max_per_hour) +
                     " msgs). Stopping blast.");
        g_blast_running = false;
        break;
      }
    }

    // --- Inter-message delay with jitter (only if not last) ---
    if (i + 1 < targets.size()) {
      int actual_delay = delay;
      if (anti_ban && jitter_seconds > 0) {
        // Random +/- jitter to look less robotic.
        int jit = (rand() % (jitter_seconds * 2 + 1)) - jitter_seconds;
        actual_delay = delay + jit;
        if (actual_delay < 1) actual_delay = 1;
      }
      std::this_thread::sleep_for(std::chrono::seconds(actual_delay));
    }

    // --- Cooldown every N messages ---
    if (anti_ban && cooldown_after > 0 && sent_in_window >= cooldown_after &&
        i + 1 < targets.size()) {
      logger::info("anti-ban: cooldown " + std::to_string(cooldown_seconds) +
                   "s after " + std::to_string(sent_in_window) + " messages");
      std::this_thread::sleep_for(std::chrono::seconds(cooldown_seconds));
      sent_in_window = 0;
      window_start = util::epoch_ms();
    }
  }
  g_blast_running = false;
}

}  // namespace blast
