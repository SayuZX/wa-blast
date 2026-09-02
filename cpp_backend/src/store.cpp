// store.cpp — SQLite implementation.
#include "store.h"

#include <mutex>
#include <sstream>

#include "sqlite3.h"
#include "util.h"

namespace store {

static sqlite3* g_db = nullptr;
static std::mutex g_mu;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool exec(const std::string& sql, std::string& err) {
  char* emsg = nullptr;
  int rc = sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &emsg);
  if (rc != SQLITE_OK) {
    err = emsg ? emsg : "sqlite error";
    if (emsg) sqlite3_free(emsg);
    return false;
  }
  return true;
}

static std::string q(const std::string& s) {
  // single-quote escape
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out += c;
  }
  out += "'";
  return out;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
bool open(const std::string& path, std::string& err) {
  int rc = sqlite3_open(path.c_str(), &g_db);
  if (rc != SQLITE_OK) {
    err = sqlite3_errmsg(g_db);
    return false;
  }
  // Schema.
  const char* schema = R"SQL(
    CREATE TABLE IF NOT EXISTS profiles (
      name TEXT PRIMARY KEY,
      android_user INTEGER,
      status TEXT,
      spoof TEXT,
      created_at TEXT
    );
    CREATE TABLE IF NOT EXISTS commands (
      id TEXT PRIMARY KEY,
      type TEXT,
      payload TEXT,
      status TEXT,
      result TEXT,
      created_at TEXT
    );
    CREATE TABLE IF NOT EXISTS logs (
      log_id TEXT PRIMARY KEY,
      timestamp TEXT,
      profile TEXT,
      target TEXT,
      message_preview TEXT,
      status TEXT,
      attempt INTEGER,
      error_code INTEGER,
      error_message TEXT,
      duration_ms INTEGER
    );
    CREATE INDEX IF NOT EXISTS idx_logs_status ON logs(status);
    CREATE INDEX IF NOT EXISTS idx_logs_profile ON logs(profile);
    CREATE TABLE IF NOT EXISTS contacts (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      name TEXT,
      phone TEXT,
      grp TEXT,
      custom_fields TEXT,
      created_at TEXT
    );
    CREATE TABLE IF NOT EXISTS templates (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      name TEXT,
      body TEXT,
      created_at TEXT
    );
    CREATE TABLE IF NOT EXISTS schedules (
      id TEXT PRIMARY KEY,
      name TEXT,
      type TEXT,
      target TEXT,
      message TEXT,
      template_id INTEGER,
      variables TEXT,
      profile TEXT,
      run_at TEXT,
      recurring TEXT,
      status TEXT,
      last_run TEXT,
      created_at TEXT
    );
  )SQL";
  return exec(schema, err);
}

nlohmann::json list_profiles() {
  std::lock_guard<std::mutex> lk(g_mu);
  nlohmann::json arr = nlohmann::json::array();
  sqlite3_stmt* st = nullptr;
  const char* sql = "SELECT name, android_user, status, spoof, created_at FROM profiles ORDER BY name";
  if (sqlite3_prepare_v2(g_db, sql, -1, &st, nullptr) != SQLITE_OK) return arr;
  while (sqlite3_step(st) == SQLITE_ROW) {
    nlohmann::json o;
    o["name"] = (const char*)sqlite3_column_text(st, 0);
    o["android_user"] = sqlite3_column_int(st, 1);
    o["status"] = (const char*)sqlite3_column_text(st, 2);
    const char* spoof = (const char*)sqlite3_column_text(st, 3);
    o["spoof"] = spoof ? nlohmann::json::parse(spoof) : nlohmann::json::object();
    o["created_at"] = (const char*)sqlite3_column_text(st, 4);
    arr.push_back(o);
  }
  sqlite3_finalize(st);
  return arr;
}

void upsert_profile(const nlohmann::json& p) {
  std::lock_guard<std::mutex> lk(g_mu);
  std::string name = p.value("name", "");
  std::string spoof = p.contains("spoof") ? p["spoof"].dump() : "{}";
  std::string status = p.value("status", "inactive");
  std::string created = p.value("created_at", util::now_iso8601());
  int au = p.value("android_user", 0);
  std::string sql =
      "INSERT INTO profiles(name, android_user, status, spoof, created_at) VALUES("
      + q(name) + "," + std::to_string(au) + "," + q(status) + "," + q(spoof) + "," + q(created) + ") "
      "ON CONFLICT(name) DO UPDATE SET android_user=excluded.android_user,"
      " status=excluded.status, spoof=excluded.spoof";
  char* emsg = nullptr;
  sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &emsg);
  if (emsg) sqlite3_free(emsg);
}

nlohmann::json get_profile(const std::string& name) {
  auto all = list_profiles();
  for (auto& p : all) if (p.value("name", "") == name) return p;
  return nlohmann::json();
}

std::string enqueue_command(const nlohmann::json& cmd) {
  std::lock_guard<std::mutex> lk(g_mu);
  std::string id = util::uuid4();
  std::string type = cmd.value("type", "");
  std::string payload = cmd.dump();
  std::string created = util::now_iso8601();
  std::string sql =
      "INSERT INTO commands(id,type,payload,status,result,created_at) VALUES("
      + q(id) + "," + q(type) + "," + q(payload) + ",'pending',''," + q(created) + ")";
  char* emsg = nullptr;
  sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &emsg);
  if (emsg) sqlite3_free(emsg);
  return id;
}

nlohmann::json list_commands(const std::string& status_filter) {
  std::lock_guard<std::mutex> lk(g_mu);
  nlohmann::json arr = nlohmann::json::array();
  std::string sql = "SELECT id,type,payload,status,result,created_at FROM commands";
  if (!status_filter.empty()) sql += " WHERE status=" + q(status_filter);
  sql += " ORDER BY created_at DESC";
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return arr;
  while (sqlite3_step(st) == SQLITE_ROW) {
    nlohmann::json o;
    o["id"] = (const char*)sqlite3_column_text(st, 0);
    o["type"] = (const char*)sqlite3_column_text(st, 1);
    const char* payload = (const char*)sqlite3_column_text(st, 2);
    o["payload"] = payload ? nlohmann::json::parse(payload) : nlohmann::json::object();
    o["status"] = (const char*)sqlite3_column_text(st, 3);
    const char* result = (const char*)sqlite3_column_text(st, 4);
    o["result"] = (result && result[0]) ? nlohmann::json::parse(result) : nlohmann::json::object();
    o["created_at"] = (const char*)sqlite3_column_text(st, 5);
    arr.push_back(o);
  }
  sqlite3_finalize(st);
  return arr;
}

void update_command(const std::string& id, const nlohmann::json& patch) {
  std::lock_guard<std::mutex> lk(g_mu);
  std::string sql = "UPDATE commands SET ";
  bool first = true;
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (!first) sql += ",";
    first = false;
    sql += it.key() + "=" + q(it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
  }
  sql += " WHERE id=" + q(id);
  char* emsg = nullptr;
  sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &emsg);
  if (emsg) sqlite3_free(emsg);
}

void add_log(const nlohmann::json& e) {
  std::lock_guard<std::mutex> lk(g_mu);
  std::string sql =
      "INSERT INTO logs(log_id,timestamp,profile,target,message_preview,status,attempt,error_code,error_message,duration_ms) VALUES("
      + q(e.value("log_id", util::uuid4())) + ","
      + q(e.value("timestamp", util::now_iso8601())) + ","
      + q(e.value("profile", "")) + ","
      + q(e.value("target", "")) + ","
      + q(e.value("message_preview", "")) + ","
      + q(e.value("status", "")) + ","
      + std::to_string(e.value("attempt", 0)) + ","
      + std::to_string(e.value("error_code", 0)) + ","
      + q(e.value("error_message", "")) + ","
      + std::to_string(e.value("duration_ms", 0)) + ")";
  char* emsg = nullptr;
  sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &emsg);
  if (emsg) sqlite3_free(emsg);
}

nlohmann::json list_logs(const std::string& profile,
                         const std::string& status,
                         const std::string& date,
                         int limit) {
  std::lock_guard<std::mutex> lk(g_mu);
  nlohmann::json arr = nlohmann::json::array();
  std::string sql = "SELECT log_id,timestamp,profile,target,message_preview,status,attempt,error_code,error_message,duration_ms FROM logs";
  std::string where;
  auto add_cond = [&](const std::string& c) {
    where += (where.empty() ? " WHERE " : " AND ") + c;
  };
  if (!profile.empty()) add_cond("profile=" + q(profile));
  if (!status.empty()) add_cond("status=" + q(status));
  if (!date.empty()) add_cond("substr(timestamp,1,10)=" + q(date));
  sql += where + " ORDER BY timestamp DESC LIMIT " + std::to_string(limit);

  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return arr;
  while (sqlite3_step(st) == SQLITE_ROW) {
    nlohmann::json o;
    o["log_id"] = (const char*)sqlite3_column_text(st, 0);
    o["timestamp"] = (const char*)sqlite3_column_text(st, 1);
    o["profile"] = (const char*)sqlite3_column_text(st, 2);
    o["target"] = (const char*)sqlite3_column_text(st, 3);
    o["message_preview"] = (const char*)sqlite3_column_text(st, 4);
    o["status"] = (const char*)sqlite3_column_text(st, 5);
    o["attempt"] = sqlite3_column_int(st, 6);
    o["error_code"] = sqlite3_column_int(st, 7);
    o["error_message"] = (const char*)sqlite3_column_text(st, 8);
    o["duration_ms"] = sqlite3_column_int(st, 9);
    arr.push_back(o);
  }
  sqlite3_finalize(st);
  return arr;
}

nlohmann::json get_log(const std::string& id) {
  std::lock_guard<std::mutex> lk(g_mu);
  nlohmann::json o;
  std::string sql = "SELECT log_id,timestamp,profile,target,message_preview,status,attempt,error_code,error_message,duration_ms FROM logs WHERE log_id=" + q(id);
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return o;
  if (sqlite3_step(st) == SQLITE_ROW) {
    o["log_id"] = (const char*)sqlite3_column_text(st, 0);
    o["timestamp"] = (const char*)sqlite3_column_text(st, 1);
    o["profile"] = (const char*)sqlite3_column_text(st, 2);
    o["target"] = (const char*)sqlite3_column_text(st, 3);
    o["message_preview"] = (const char*)sqlite3_column_text(st, 4);
    o["status"] = (const char*)sqlite3_column_text(st, 5);
    o["attempt"] = sqlite3_column_int(st, 6);
    o["error_code"] = sqlite3_column_int(st, 7);
    o["error_message"] = (const char*)sqlite3_column_text(st, 8);
    o["duration_ms"] = sqlite3_column_int(st, 9);
  }
  sqlite3_finalize(st);
  return o;
}

int delete_old_logs(int retention_days) {
  std::lock_guard<std::mutex> lk(g_mu);
  std::string cutoff = "now";  // placeholder — computed in SQL below
  // Delete logs older than N days using SQLite date arithmetic.
  std::string sql =
      "DELETE FROM logs WHERE timestamp < datetime('now','-" + std::to_string(retention_days) + " days')";
  char* emsg = nullptr;
  int rc = sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &emsg);
  if (emsg) sqlite3_free(emsg);
  int deleted = sqlite3_changes(g_db);
  return (rc == SQLITE_OK) ? deleted : -1;
}

// ---------------------------------------------------------------------------
// Raw SQL access (shared by contacts/templates/scheduler modules)
// ---------------------------------------------------------------------------
bool exec_sql(const std::string& sql, std::string& err) {
  std::lock_guard<std::mutex> lk(g_mu);
  char* emsg = nullptr;
  int rc = sqlite3_exec(g_db, sql.c_str(), nullptr, nullptr, &emsg);
  if (rc != SQLITE_OK) {
    err = emsg ? emsg : "sqlite error";
    if (emsg) sqlite3_free(emsg);
    return false;
  }
  return true;
}

nlohmann::json query(const std::string& sql) {
  std::lock_guard<std::mutex> lk(g_mu);
  nlohmann::json arr = nlohmann::json::array();
  sqlite3_stmt* st = nullptr;
  if (sqlite3_prepare_v2(g_db, sql.c_str(), -1, &st, nullptr) != SQLITE_OK) return arr;
  int ncol = sqlite3_column_count(st);
  while (sqlite3_step(st) == SQLITE_ROW) {
    nlohmann::json o = nlohmann::json::object();
    for (int c = 0; c < ncol; c++) {
      const char* name = sqlite3_column_name(st, c);
      int type = sqlite3_column_type(st, c);
      if (type == SQLITE_INTEGER) {
        o[name] = sqlite3_column_int64(st, c);
      } else if (type == SQLITE_FLOAT) {
        o[name] = sqlite3_column_double(st, c);
      } else if (type == SQLITE_NULL) {
        o[name] = nullptr;
      } else {
        const char* txt = (const char*)sqlite3_column_text(st, c);
        o[name] = txt ? std::string(txt) : std::string("");
      }
    }
    arr.push_back(o);
  }
  sqlite3_finalize(st);
  return arr;
}

}  // namespace store
