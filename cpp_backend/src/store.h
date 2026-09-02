// store.h — SQLite-backed state & log persistence.
#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace store {

// Open (or create) the SQLite database at `path`. Returns false + err on fail.
bool open(const std::string& path, std::string& err);

// --- profiles ---
nlohmann::json list_profiles();
void upsert_profile(const nlohmann::json& p);
nlohmann::json get_profile(const std::string& name);

// --- commands (queued ADB jobs) ---
std::string enqueue_command(const nlohmann::json& cmd);
nlohmann::json list_commands(const std::string& status_filter);
void update_command(const std::string& id, const nlohmann::json& patch);

// --- logs ---
void add_log(const nlohmann::json& entry);
nlohmann::json list_logs(const std::string& profile,
                         const std::string& status,
                         const std::string& date,
                         int limit);
nlohmann::json get_log(const std::string& id);
int delete_old_logs(int retention_days);

// --- raw SQL access (for contacts/templates/scheduler modules) ---
// Execute a single statement (no result). Returns false + err on failure.
bool exec_sql(const std::string& sql, std::string& err);
// Run a SELECT and return rows as JSON array of objects (columns keyed by name).
nlohmann::json query(const std::string& sql);

}  // namespace store
