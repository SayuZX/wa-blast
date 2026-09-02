#include "blast.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>

#include "adb.h"
#include "config.h"
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
      err = "WhatsApp not focused / preflight failed";
      return 1;
    }
  }

  // Open the chat via deep link.
  adb::Result r = adb::open_chat(number);
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));

  // Method 1: input text (short text).
  if (message.size() <= 256) {
    r = adb::input_text(message);
    if (r.ok()) {
      adb::keyevent(66);  // ENTER
      return 0;
    }
  }

  // Method 2: clipboard fallback (long/special text).
  if (g_cfg && g_cfg->blast.fallback_clipboard) {
    adb::clipboard_set(message);
    adb::clipboard_paste();
    adb::keyevent(66);  // ENTER
    err = "";
    return 0;  // assume success after paste+enter
  }

  err = "input_text failed and clipboard fallback disabled";
  return 2;
}

TargetResult send_one(const std::string& profile,
                      const std::string& number,
                      const std::string& message) {
  TargetResult res;
  res.number = number;

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

  for (size_t i = 0; i < targets.size(); i++) {
    if (!g_blast_running) break;  // allow cancellation
    TargetResult r = send_one(profile, targets[i], message);
    if (on_progress) on_progress(r);
    if (i + 1 < targets.size()) {
      std::this_thread::sleep_for(std::chrono::seconds(delay));
    }
  }
  g_blast_running = false;
}

}  // namespace blast
