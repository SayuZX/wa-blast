// blast.h — message send/blast with retry, per-target status, and locking.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace conf { struct Config; }

namespace blast {

// Set the global config pointer (called once at startup).
void set_config(const conf::Config* cfg);

// Status of a single send attempt/target.
enum class Status { PENDING, SENDING, SUCCESS, FAILED, RETRY };

const char* status_str(Status s);

struct TargetResult {
  std::string number;
  Status status = Status::PENDING;
  int attempts = 0;
  int error_code = 0;
  std::string error_message;
  long long duration_ms = 0;
};

// Send a single message to `number`, with preflight + retry.
TargetResult send_one(const std::string& profile,
                      const std::string& number,
                      const std::string& message);

void blast(const std::string& profile,
           const std::vector<std::string>& targets,
           const std::string& message,
           const std::function<void(const TargetResult&)>& on_progress);

// Acquire/release the blast lock (file lock `.wa_lock`).
bool try_acquire_lock();
void release_lock();
bool is_locked();

// Reset the in-flight flag (used on shutdown).
void reset();

}  // namespace blast
