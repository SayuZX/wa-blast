// scheduler.h — background job scheduler (checks DB every 60s).
#pragma once

#include <atomic>
#include <string>

#include "nlohmann/json.hpp"

namespace sched {

// Start the scheduler background thread. Safe to call once.
void start();

// Stop the scheduler thread (used on shutdown).
void stop();

// Create a schedule. Returns {id, ...}.
nlohmann::json create(const nlohmann::json& job);

// List schedules (optionally filter by status).
nlohmann::json list(const std::string& status_filter);

// Delete a schedule by id.
bool remove(const std::string& id);

}  // namespace sched
