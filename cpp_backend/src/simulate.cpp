// simulate.cpp — simulation mode implementation.
#include "simulate.h"

#include <cstdio>

#include "logger.h"
#include "store.h"
#include "util.h"

namespace simulate {

static bool g_enabled = false;

void set_enabled(bool on) { g_enabled = on; }
bool enabled() { return g_enabled; }

bool maybe_simulate_send(const std::string& profile,
                         const std::string& number,
                         const std::string& message) {
  if (!g_enabled) return false;

  logger::info("[SIMULASI] Mengirim ke " + number + " : " + message);

  // Log a SUCCESS entry so the full business flow is testable without root.
  nlohmann::json e;
  e["log_id"] = util::uuid4();
  e["timestamp"] = util::now_iso8601();
  e["profile"] = profile;
  e["target"] = number;
  e["message_preview"] = message.size() > 20 ? message.substr(0, 20) + "..." : message;
  e["status"] = "SUCCESS";
  e["attempt"] = 1;
  e["error_code"] = 0;
  e["error_message"] = "SIMULATED";
  e["duration_ms"] = 0;
  store::add_log(e);
  return true;
}

}  // namespace simulate
