// scheduler.cpp — background scheduler that polls the `schedules` table.
#include "scheduler.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "blast.h"
#include "logger.h"
#include "simulate.h"
#include "store.h"
#include "templates.h"
#include "util.h"

namespace sched {

static std::atomic<bool> g_running{false};
static std::thread g_thread;

static std::string q(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out += c;
  }
  out += "'";
  return out;
}

// Check if a schedule is due and execute it.
static void execute_due(const nlohmann::json& sched_row) {
  std::string id = sched_row.value("id", "");
  std::string type = sched_row.value("type", "send");
  std::string target = sched_row.value("target", "");
  std::string message = sched_row.value("message", "");
  std::string profile = sched_row.value("profile", "WA_1");
  int template_id = sched_row.value("template_id", -1);
  std::string variables = sched_row.value("variables", "{}");

  // If a template is referenced, render it.
  if (template_id >= 0) {
    nlohmann::json t = tmpl::get(template_id);
    if (!t.is_null()) {
      nlohmann::json vars = nlohmann::json::parse(variables, nullptr, false);
      message = tmpl::render(t.value("body", ""), vars.is_discarded() ? nlohmann::json::object() : vars);
    }
  }

  // Execute (simulate or real).
  bool ok = true;
  if (simulate::enabled()) {
    simulate::maybe_simulate_send(profile, target, message);
  } else {
    blast::TargetResult r = blast::send_one(profile, target, message);
    ok = (r.status == blast::Status::SUCCESS);
  }

  // Update last_run + status.
  std::string err;
  store::exec_sql(
      "UPDATE schedules SET status=" + q(ok ? "done" : "failed") +
          ", last_run=" + q(util::now_iso8601()) + " WHERE id=" + q(id),
      err);
  logger::info("schedule " + id + " executed: " + (ok ? "done" : "failed"));
}

static void worker_loop() {
  while (g_running) {
    try {
      // Find due schedules: run_at <= now AND status not in ('done','failed').
      nlohmann::json due = store::query(
          "SELECT * FROM schedules WHERE run_at <= datetime('now') "
          "AND (status IS NULL OR status NOT IN ('done','failed'))");
      for (const auto& row : due) {
        execute_due(row);
      }
    } catch (const std::exception& e) {
      logger::error(std::string("scheduler error: ") + e.what());
    }
    // Sleep 60s between checks.
    for (int i = 0; i < 60 && g_running; i++) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void start() {
  if (g_running) return;
  g_running = true;
  g_thread = std::thread(worker_loop);
}

void stop() {
  g_running = false;
  if (g_thread.joinable()) g_thread.join();
}

nlohmann::json create(const nlohmann::json& job) {
  std::string id = util::uuid4();
  std::string name = job.value("name", "");
  std::string type = job.value("type", "send");
  std::string target = job.value("target", "");
  std::string message = job.value("message", "");
  int template_id = job.value("template_id", -1);
  std::string variables = job.contains("variables") ? job["variables"].dump() : "{}";
  std::string profile = job.value("profile", "WA_1");
  std::string run_at = job.value("run_at", "");
  std::string recurring = job.value("recurring", "");  // "", "daily", "weekly"

  std::string sql =
      "INSERT INTO schedules(id,name,type,target,message,template_id,variables,profile,run_at,recurring,status,created_at) VALUES("
      + q(id) + "," + q(name) + "," + q(type) + "," + q(target) + "," + q(message) + ","
      + std::to_string(template_id) + "," + q(variables) + "," + q(profile) + "," + q(run_at)
      + "," + q(recurring) + ",'scheduled'," + q(util::now_iso8601()) + ")";
  std::string err;
  if (!store::exec_sql(sql, err)) {
    return {{"error", err}};
  }
  return {{"id", id}, {"name", name}, {"run_at", run_at}, {"recurring", recurring}};
}

nlohmann::json list(const std::string& status_filter) {
  std::string sql = "SELECT * FROM schedules";
  if (!status_filter.empty()) sql += " WHERE status=" + q(status_filter);
  sql += " ORDER BY run_at";
  return store::query(sql);
}

bool remove(const std::string& id) {
  std::string err;
  return store::exec_sql("DELETE FROM schedules WHERE id=" + q(id), err);
}

}  // namespace sched
