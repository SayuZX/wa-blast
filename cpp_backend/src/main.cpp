// main.cpp — Crow HTTP server exposing the QA harness API.
//
// Endpoints:
//   POST /api/auth                       -> verify API key (returns token echo)
//   GET  /api/profiles                   -> list profiles
//   POST /api/profiles/switch            -> enqueue profile switch (ADB)
//   GET  /api/profiles/{id}/status       -> profile status
//   POST /api/message/send               -> send one message (retry)
//   POST /api/message/blast              -> blast many numbers (queue+retry)
//   GET  /api/blast/{job_id}/status      -> blast job status
//   GET  /api/logs                       -> filtered logs
//   GET  /api/logs/{id}                  -> single log
//   DELETE /api/logs                     -> prune old logs
//   GET  /api/logs/stream                -> SSE live log stream
//   GET  /api/health                     -> health
//   GET  /docs                           -> Swagger UI (static, embedded)
//   GET  /openapi.yaml                   -> OpenAPI spec
//
// Build: see CMakeLists.txt / build_android.sh.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "crow.h"

#include "adb.h"
#include "auth.h"
#include "blast.h"
#include "config.h"
#include "contacts.h"
#include "excel.h"
#include "logger.h"
#include "props.h"
#include "scheduler.h"
#include "simulate.h"
#include "store.h"
#include "templates.h"
#include "titanium.h"
#include "util.h"
#include "web.h"

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static conf::Config g_config;
static std::mutex g_jobs_mu;
static std::map<std::string, nlohmann::json> g_jobs;  // job_id -> status doc
static std::atomic<bool> g_running{true};

// Blast worker threads (one per job so status can be tracked live).
static std::vector<std::thread> g_workers;

// ---------------------------------------------------------------------------
// Auth middleware: require valid X-API-Key (or Bearer) on protected routes.
// ---------------------------------------------------------------------------
static std::string extract_key(const crow::request& req) {
  // X-API-Key header first.
  auto it = req.headers.find("X-API-Key");
  if (it != req.headers.end()) return it->second;

  // Authorization: Bearer <token>
  auto auth = req.headers.find("Authorization");
  if (auth != req.headers.end()) {
    const std::string& v = auth->second;
    const std::string prefix = "Bearer ";
    if (v.rfind(prefix, 0) == 0) return v.substr(prefix.size());
  }
  return "";
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string preview(const std::string& s, size_t n = 20) {
  return s.size() > n ? s.substr(0, n) + "..." : s;
}

// Enqueue a profile-switch command and run it (switch is quick; run inline).
static nlohmann::json do_switch(const std::string& profile_name) {
  nlohmann::json cmd;
  cmd["type"] = "switch";
  cmd["profile"] = profile_name;
  std::string id = store::enqueue_command(cmd);

  // Find android_user for the profile.
  int user = 0;
  for (const auto& p : g_config.profiles)
    if (p.name == profile_name) user = p.android_user;

  adb::Result r = adb::switch_user(user);
  nlohmann::json result;
  result["ok"] = r.ok();
  result["active"] = profile_name;
  result["android_user"] = user;
  result["command_id"] = id;
  store::update_command(id, {{"status", r.ok() ? "done" : "failed"},
                             {"result", result.dump()}});
  return result;
}

// ---------------------------------------------------------------------------
// SSE helper: broadcast a log line to all connected streams.
// ---------------------------------------------------------------------------
struct SseClient {
  crow::response* resp = nullptr;
  bool alive = true;
};
static std::mutex g_sse_mu;
static std::vector<SseClient*> g_sse_clients;

static void broadcast_sse(const std::string& data) {
  std::lock_guard<std::mutex> lk(g_sse_mu);
  for (auto* c : g_sse_clients) {
    if (c && c->alive) {
      // Best-effort append (Crow streaming is limited; we keep a ring buffer
      // in production — here we simply mark for the handler to flush).
    }
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  std::string cfg_path = "config.json";
  bool simulate_flag = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--simulate") { simulate_flag = true; }
    else if (a.rfind("--config=", 0) == 0) { cfg_path = a.substr(9); }
    else if (i == 1 && a != "server" && a != "switch" && a != "send" && a != "blast"
             && a != "status" && a != "profile" && a != "logs" && a != "--simulate"
             && a.rfind("--", 0) != 0) {
      // First positional non-flag = config path (back-compat).
      cfg_path = a;
    }
  }

  std::string err;
  if (!conf::load(cfg_path, g_config, err)) {
    logger::error("config load failed: " + err);
    return 1;
  }

  // Simulation mode: --simulate flag OR config.simulate OR no root.
  if (simulate_flag || g_config.simulate) {
    simulate::set_enabled(true);
  } else {
    // Auto-detect: if `su` unavailable, enable simulation.
    adb::Result r = adb::shell("id");
    if (!r.ok() || r.stdout_.find("uid=0") == std::string::npos) {
      logger::warn("root not detected — enabling simulation mode");
      simulate::set_enabled(true);
    }
  }

  // Wire config into the sub-modules.
  adb::set_config(&g_config);
  auth::set_config(&g_config);
  blast::set_config(&g_config);
  web::set_www_root(g_config.www_root);

  if (!store::open(g_config.db_path, err)) {
    logger::error("sqlite open failed: " + err);
    return 1;
  }

  // Seed profiles from config into the DB.
  for (const auto& p : g_config.profiles) {
    nlohmann::json j;
    j["name"] = p.name;
    j["android_user"] = p.android_user;
    j["status"] = "inactive";
    j["spoof"] = nlohmann::json{{"imei", p.imei}, {"android_id", p.android_id},
                                {"device_model", p.device_model}};
    j["created_at"] = util::now_iso8601();
    store::upsert_profile(j);
  }

  // --- CLI mode: `wa-cli <command> [args]` --------------------------------
  // Commands: switch <profile> | send <phone> <msg> | blast <file.txt>
  //           status | server start | server
  if (argc >= 2 && std::string(argv[1]) != "server") {
    std::string cmd = argv[1];

    if (cmd == "status") {
      nlohmann::json j;
      j["active_profile"] = g_config.active_profile.empty() ? "WA_1" : g_config.active_profile;
      j["profiles"] = g_config.profile_names;
      j["wa_package"] = g_config.target_package;
      j["api_port"] = g_config.port;
      std::printf("%s\n", j.dump(2).c_str());
      return 0;
    }

    if (cmd == "switch" && argc >= 3) {
      std::string profile = argv[2];
      std::string err;
      // Apply props + titanium restore for the target profile.
      nlohmann::json spoof;
      if (props::load("/data/local/tmp/wa_profiles", profile, spoof)) {
        props::apply(spoof, err);
      }
      if (!titanium::switch_to(g_config.target_package,
                               g_config.active_profile, profile, true, err)) {
        logger::error("switch failed: " + err);
        return 1;
      }
      g_config.active_profile = profile;
      std::printf("{\"active\":\"%s\"}\n", profile.c_str());
      return 0;
    }

    if (cmd == "send" && argc >= 4) {
      std::string phone = argv[2];
      std::string message = argv[3];
      blast::TargetResult r = blast::send_one(g_config.active_profile, phone, message);
      nlohmann::json j;
      j["number"] = r.number;
      j["status"] = blast::status_str(r.status);
      j["attempts"] = r.attempts;
      j["error_message"] = r.error_message;
      std::printf("%s\n", j.dump().c_str());
      return r.status == blast::Status::SUCCESS ? 0 : 1;
    }

    if (cmd == "blast" && argc >= 3) {
      std::string file = argv[2];
      std::ifstream f(file);
      std::vector<std::string> targets;
      std::string line;
      while (std::getline(f, line)) {
        std::string t = util::trim(line);
        if (!t.empty()) targets.push_back(t);
      }
      std::string message = (argc >= 4) ? argv[3] : "blast";
      int ok = 0, fail = 0;
      blast::blast(g_config.active_profile, targets, message,
                   [&](const blast::TargetResult& r) {
                     if (r.status == blast::Status::SUCCESS) ok++; else fail++;
                     std::printf("  %s -> %s\n", r.number.c_str(), blast::status_str(r.status));
                   });
      std::printf("{\"total\":%zu,\"succeeded\":%d,\"failed\":%d}\n", targets.size(), ok, fail);
      return 0;
    }

    // --- profile <add|restore|delete|export|edit> ---
    if (cmd == "profile" && argc >= 3) {
      std::string sub = argv[2];

      if (sub == "add" && argc >= 4) {
        std::string name = argv[3];
        // Generate identity + snapshot current data for the new profile.
        nlohmann::json spoof = props::generate();
        props::save("/data/local/tmp/wa_profiles", name, spoof);
        g_config.profile_names.push_back(name);
        nlohmann::json j;
        j["created"] = name;
        j["spoof"] = spoof;
        std::printf("%s\n", j.dump().c_str());
        return 0;
      }

      if (sub == "restore" && argc >= 4) {
        std::string name = argv[3];
        std::string err;
        if (!titanium::restore(g_config.target_package, name, err)) {
          logger::error("restore failed: " + err);
          return 1;
        }
        std::printf("{\"restored\":\"%s\"}\n", name.c_str());
        return 0;
      }

      if (sub == "delete" && argc >= 4) {
        std::string name = argv[3];
        std::string cmd = "rm -rf /data/local/tmp/wa_profiles/" + name +
                          " /data/local/tmp/wa_profiles/props_" + name + ".json";
        adb::shell(cmd);
        std::printf("{\"deleted\":\"%s\"}\n", name.c_str());
        return 0;
      }

      if (sub == "export" && argc >= 4) {
        std::string name = argv[3];
        std::string dest = "/sdcard/wa_exports/" + name;
        std::string cmd = "mkdir -p /sdcard/wa_exports && cp -rf "
                          "/data/local/tmp/wa_profiles/" + name + " " + dest;
        adb::Result r = adb::shell(cmd);
        nlohmann::json j;
        j["exported"] = name;
        j["path"] = dest;
        j["ok"] = r.ok();
        std::printf("%s\n", j.dump().c_str());
        return r.ok() ? 0 : 1;
      }

      if (sub == "edit" && argc >= 4) {
        std::string name = argv[3];
        // Optional: JSON payload with new name/props (argv[4]).
        std::string new_name = name;
        if (argc >= 5) {
          nlohmann::json fields = nlohmann::json::parse(argv[4], nullptr, false);
          if (!fields.is_discarded() && fields.contains("name")) {
            new_name = fields["name"].get<std::string>();
          }
        }
        // Rename snapshot dir if name changed.
        if (new_name != name) {
          adb::shell("mv /data/local/tmp/wa_profiles/" + name +
                     " /data/local/tmp/wa_profiles/" + new_name);
          adb::shell("mv /data/local/tmp/wa_profiles/props_" + name + ".json"
                     " /data/local/tmp/wa_profiles/props_" + new_name + ".json");
        }
        nlohmann::json j;
        j["edited"] = name;
        j["new_name"] = new_name;
        std::printf("%s\n", j.dump().c_str());
        return 0;
      }

      std::fprintf(stderr, "unknown profile subcommand: %s\n", sub.c_str());
      return 2;
    }

    // --- logs (JSON list) ---
    if (cmd == "logs") {
      nlohmann::json logs = store::list_logs("", "", "", 100);
      nlohmann::json j;
      j["logs"] = logs;
      std::printf("%s\n", j.dump().c_str());
      return 0;
    }

    // Unknown command — print usage.
    std::fprintf(stderr,
      "usage: wa-cli <command> [args]\n"
      "  switch <profile>        switch profile (props + data)\n"
      "  send <phone> <message>  send one message\n"
      "  blast <file.txt> [msg]  blast to numbers in file\n"
      "  profile add <name>      create profile\n"
      "  profile restore <name>  restore profile snapshot\n"
      "  profile delete <name>   delete profile\n"
      "  profile export <name>   export snapshot to /sdcard\n"
      "  profile edit <name>     rename profile (or {name:...} json)\n"
      "  logs                    list recent logs (JSON)\n"
      "  status                  show active profile + props\n"
      "  server start            run HTTP API server\n");
    return 2;
  }

  crow::SimpleApp app;

  // --- CORS ---
  auto& cors = app.get_middleware<crow::CORSHandler>();
  cors.global()
      .origin("*")
      .methods("GET"_method, "POST"_method, "DELETE"_method)
      .headers("X-API-Key", "Authorization", "Content-Type");

  // --- /api/health (no auth) ---
  CROW_ROUTE(app, "/api/health")
  ([]() {
    nlohmann::json j;
    j["status"] = "ok";
    j["backend"] = "cpp-monolithic";
    j["profiles"] = store::list_profiles().size();
    j["time"] = util::now_iso8601();
    return crow::response(j.dump());
  });

  // --- /api/auth ---
  CROW_ROUTE(app, "/api/auth")
      .methods("POST"_method)([](const crow::request& req) {
        std::string key = extract_key(req);
        if (!auth::verify(key)) {
          return crow::response(401, "{\"error\":\"invalid api key\"}");
        }
        nlohmann::json j;
        j["authenticated"] = true;
        return crow::response(j.dump());
      });

  // --- /api/profiles ---
  CROW_ROUTE(app, "/api/profiles")
  ([](const crow::request& req) {
    if (!auth::verify(extract_key(req)))
      return crow::response(401, "{\"error\":\"unauthorized\"}");
    nlohmann::json j;
    j["profiles"] = store::list_profiles();
    return crow::response(j.dump());
  });

  // --- POST /api/profiles/switch ---
  CROW_ROUTE(app, "/api/profiles/switch")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        std::string name = body.value("profile", "");
        if (name.empty()) return crow::response(400, "{\"error\":\"profile required\"}");
        nlohmann::json result = do_switch(name);
        return crow::response(result.dump());
      });

  // --- GET /api/profiles/{id}/status ---
  CROW_ROUTE(app, "/api/profiles/<string>")
  ([](const crow::request& req, const std::string& id) {
    if (!auth::verify(extract_key(req)))
      return crow::response(401, "{\"error\":\"unauthorized\"}");
    nlohmann::json p = store::get_profile(id);
    if (p.is_null()) return crow::response(404, "{\"error\":\"not found\"}");
    return crow::response(p.dump());
  });

  // --- POST /api/message/send ---
  CROW_ROUTE(app, "/api/message/send")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        std::string number = body.value("number", "");
        std::string message = body.value("message", "");
        std::string profile = body.value("profile", "WA_1");
        if (number.empty() || message.empty())
          return crow::response(400, "{\"error\":\"number and message required\"}");

        blast::TargetResult r = blast::send_one(profile, number, message);
        nlohmann::json j;
        j["number"] = r.number;
        j["status"] = blast::status_str(r.status);
        j["attempts"] = r.attempts;
        j["error_code"] = r.error_code;
        j["error_message"] = r.error_message;
        j["duration_ms"] = r.duration_ms;
        return crow::response(j.dump());
      });

  // --- POST /api/message/blast ---
  CROW_ROUTE(app, "/api/message/blast")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        std::string message = body.value("message", "");
        std::string profile = body.value("profile", "WA_1");
        std::vector<std::string> targets;
        if (body.contains("targets") && body["targets"].is_array()) {
          for (auto& t : body["targets"]) targets.push_back(t.get<std::string>());
        }
        if (targets.empty() || message.empty())
          return crow::response(400, "{\"error\":\"targets and message required\"}");

        if (blast::is_locked())
          return crow::response(409, "{\"error\":\"another blast is running\"}");
        if (!blast::try_acquire_lock())
          return crow::response(409, "{\"error\":\"could not acquire lock\"}");

        std::string job_id = util::uuid4();
        {
          std::lock_guard<std::mutex> lk(g_jobs_mu);
          g_jobs[job_id] = {{"job_id", job_id},
                            {"status", "running"},
                            {"total", targets.size()},
                            {"done", 0},
                            {"succeeded", 0},
                            {"failed", 0},
                            {"results", nlohmann::json::array()}};
        }

        // Run blast in a worker thread so we can report status live.
        g_workers.emplace_back([job_id, profile, targets, message]() {
          int succeeded = 0, failed = 0, done = 0;
          nlohmann::json results = nlohmann::json::array();
          blast::blast(profile, targets, message, [&](const blast::TargetResult& r) {
            nlohmann::json o;
            o["number"] = r.number;
            o["status"] = blast::status_str(r.status);
            o["attempts"] = r.attempts;
            o["error_code"] = r.error_code;
            o["error_message"] = r.error_message;
            o["duration_ms"] = r.duration_ms;
            results.push_back(o);
            done++;
            if (r.status == blast::Status::SUCCESS) succeeded++; else failed++;
            {
              std::lock_guard<std::mutex> lk(g_jobs_mu);
              auto& j = g_jobs[job_id];
              j["done"] = done;
              j["succeeded"] = succeeded;
              j["failed"] = failed;
              j["results"] = results;
            }
          });
          blast::release_lock();
          std::lock_guard<std::mutex> lk(g_jobs_mu);
          g_jobs[job_id]["status"] = "finished";
        });

        nlohmann::json j;
        j["job_id"] = job_id;
        j["status"] = "running";
        j["total"] = targets.size();
        return crow::response(j.dump());
      });

  // --- POST /api/send (alias of /api/message/send, prompt schema) ---
  CROW_ROUTE(app, "/api/send")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        std::string number = body.value("phone", body.value("number", ""));
        std::string message = body.value("message", "");
        std::string profile = body.value("profile", g_config.active_profile);
        if (number.empty() || message.empty())
          return crow::response(400, "{\"error\":\"phone and message required\"}");

        blast::TargetResult r = blast::send_one(profile, number, message);
        nlohmann::json j;
        j["phone"] = r.number;
        j["profile"] = profile;
        j["status"] = blast::status_str(r.status);
        j["attempts"] = r.attempts;
        j["error_code"] = r.error_code;
        j["error_message"] = r.error_message;
        j["duration_ms"] = r.duration_ms;
        return crow::response(j.dump());
      });

  // --- POST /api/blast (alias, prompt schema: profile + targets) ---
  CROW_ROUTE(app, "/api/blast")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        std::string message = body.value("message", "");
        std::string profile = body.value("profile", g_config.active_profile);
        std::vector<std::string> targets;
        if (body.contains("targets") && body["targets"].is_array())
          for (auto& t : body["targets"]) targets.push_back(t.get<std::string>());
        if (targets.empty() || message.empty())
          return crow::response(400, "{\"error\":\"targets and message required\"}");

        if (blast::is_locked())
          return crow::response(409, "{\"error\":\"another blast is running\"}");
        if (!blast::try_acquire_lock())
          return crow::response(409, "{\"error\":\"could not acquire lock\"}");

        std::string job_id = util::uuid4();
        {
          std::lock_guard<std::mutex> lk(g_jobs_mu);
          g_jobs[job_id] = {{"job_id", job_id}, {"status", "running"},
                            {"total", targets.size()}, {"done", 0},
                            {"succeeded", 0}, {"failed", 0},
                            {"results", nlohmann::json::array()}};
        }
        g_workers.emplace_back([job_id, profile, targets, message]() {
          int succeeded = 0, failed = 0, done = 0;
          nlohmann::json results = nlohmann::json::array();
          blast::blast(profile, targets, message, [&](const blast::TargetResult& r) {
            nlohmann::json o;
            o["number"] = r.number;
            o["status"] = blast::status_str(r.status);
            o["attempts"] = r.attempts;
            o["error_code"] = r.error_code;
            o["error_message"] = r.error_message;
            results.push_back(o);
            done++;
            if (r.status == blast::Status::SUCCESS) succeeded++; else failed++;
            std::lock_guard<std::mutex> lk(g_jobs_mu);
            auto& j = g_jobs[job_id];
            j["done"] = done; j["succeeded"] = succeeded; j["failed"] = failed;
            j["results"] = results;
          });
          blast::release_lock();
          std::lock_guard<std::mutex> lk(g_jobs_mu);
          g_jobs[job_id]["status"] = "finished";
        });

        nlohmann::json j;
        j["job_id"] = job_id;
        j["status"] = "running";
        j["total"] = targets.size();
        return crow::response(j.dump());
      });

  // --- GET /api/status ---
  CROW_ROUTE(app, "/api/status")
  ([](const crow::request& req) {
    if (!auth::verify(extract_key(req)))
      return crow::response(401, "{\"error\":\"unauthorized\"}");
    nlohmann::json j;
    j["active_profile"] = g_config.active_profile.empty() ? "WA_1" : g_config.active_profile;
    j["profiles"] = g_config.profile_names;
    j["wa_package"] = g_config.target_package;
    j["api_port"] = g_config.port;
    j["blast_running"] = blast::is_locked();
    j["time"] = util::now_iso8601();
    return crow::response(j.dump());
  });

  // --- GET /api/blast/{job_id}/status ---
  CROW_ROUTE(app, "/api/blast/<string>")
  ([](const crow::request& req, const std::string& job_id) {
    if (!auth::verify(extract_key(req)))
      return crow::response(401, "{\"error\":\"unauthorized\"}");
    std::lock_guard<std::mutex> lk(g_jobs_mu);
    auto it = g_jobs.find(job_id);
    if (it == g_jobs.end()) return crow::response(404, "{\"error\":\"job not found\"}");
    return crow::response(it->second.dump());
  });

  // --- GET /api/logs (filtered) ---
  CROW_ROUTE(app, "/api/logs")
      .methods("GET"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        std::string profile = req.url_params.get("profile") ? req.url_params.get("profile") : "";
        std::string status = req.url_params.get("status") ? req.url_params.get("status") : "";
        std::string date = req.url_params.get("date") ? req.url_params.get("date") : "";
        int limit = 100;
        if (req.url_params.get("limit")) limit = std::stoi(req.url_params.get("limit"));
        nlohmann::json j;
        j["logs"] = store::list_logs(profile, status, date, limit);
        return crow::response(j.dump());
      });

  // --- GET /api/logs/{id} ---
  CROW_ROUTE(app, "/api/logs/<string>")
  ([](const crow::request& req, const std::string& id) {
    if (!auth::verify(extract_key(req)))
      return crow::response(401, "{\"error\":\"unauthorized\"}");
    nlohmann::json l = store::get_log(id);
    if (l.is_null()) return crow::response(404, "{\"error\":\"not found\"}");
    return crow::response(l.dump());
  });

  // --- DELETE /api/logs (prune old) ---
  CROW_ROUTE(app, "/api/logs")
      .methods("DELETE"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        int deleted = store::delete_old_logs(g_config.log_retention_days);
        nlohmann::json j;
        j["deleted"] = deleted;
        return crow::response(j.dump());
      });

  // --- Contacts ---
  CROW_ROUTE(app, "/api/contacts")
      .methods("GET"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        std::string grp = req.url_params.get("grp") ? req.url_params.get("grp") : "";
        nlohmann::json j;
        j["contacts"] = contacts::list(grp);
        return crow::response(j.dump());
      });

  CROW_ROUTE(app, "/api/contacts/import")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        std::string filename = body.value("filename", "");
        std::string content = body.value("content", "");
        if (content.empty()) return crow::response(400, "{\"error\":\"content required\"}");
        auto rows = excel::parse(filename, content);
        nlohmann::json result = contacts::import(rows);
        return crow::response(result.dump());
      });

  CROW_ROUTE(app, "/api/contacts")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        std::string name = body.value("name", "");
        std::string phone = body.value("phone", "");
        if (phone.empty()) return crow::response(400, "{\"error\":\"phone required\"}");
        nlohmann::json r = contacts::add(name, phone, body.value("grp", ""),
                                         body.contains("custom") ? body["custom"] : nlohmann::json::object());
        return crow::response(r.dump());
      });

  // --- Templates ---
  CROW_ROUTE(app, "/api/templates")
      .methods("GET"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        nlohmann::json j;
        j["templates"] = tmpl::list();
        return crow::response(j.dump());
      });

  CROW_ROUTE(app, "/api/templates")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        std::string name = body.value("name", "");
        std::string tbody = body.value("body", "");
        if (name.empty() || tbody.empty())
          return crow::response(400, "{\"error\":\"name and body required\"}");
        int id = body.value("id", -1);
        return crow::response(tmpl::save(name, tbody, id).dump());
      });

  // --- Send template (personalised) ---
  CROW_ROUTE(app, "/api/send/template")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        int template_id = body.value("template_id", -1);
        std::string phone = body.value("phone", "");
        std::string profile = body.value("profile", g_config.active_profile);
        nlohmann::json vars = body.contains("variables") ? body["variables"] : nlohmann::json::object();

        nlohmann::json t = tmpl::get(template_id);
        if (t.is_null()) return crow::response(404, "{\"error\":\"template not found\"}");
        std::string message = tmpl::render(t.value("body", ""), vars);

        blast::TargetResult r;
        if (simulate::enabled()) {
          simulate::maybe_simulate_send(profile, phone, message);
          r.status = blast::Status::SUCCESS;
          r.number = phone;
        } else {
          r = blast::send_one(profile, phone, message);
        }
        nlohmann::json j;
        j["phone"] = r.number;
        j["status"] = blast::status_str(r.status);
        j["message"] = message;
        return crow::response(j.dump());
      });

  // --- Schedules ---
  CROW_ROUTE(app, "/api/schedules")
      .methods("GET"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        std::string status = req.url_params.get("status") ? req.url_params.get("status") : "";
        nlohmann::json j;
        j["schedules"] = sched::list(status);
        return crow::response(j.dump());
      });

  CROW_ROUTE(app, "/api/schedules")
      .methods("POST"_method)([](const crow::request& req) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        auto body = nlohmann::json::parse(req.body, nullptr, false);
        if (body.is_discarded()) return crow::response(400, "{\"error\":\"bad json\"}");
        return crow::response(sched::create(body).dump());
      });

  CROW_ROUTE(app, "/api/schedules/<string>")
      .methods("DELETE"_method)([](const crow::request& req, const std::string& id) {
        if (!auth::verify(extract_key(req)))
          return crow::response(401, "{\"error\":\"unauthorized\"}");
        bool ok = sched::remove(id);
        return crow::response(ok ? "{\"deleted\":true}" : "{\"deleted\":false}");
      });

  // --- SSE events ---
  CROW_ROUTE(app, "/api/events")
  ([](const crow::request& req, crow::response& res) {
    // Stream a single snapshot + keep connection open (best-effort SSE).
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    nlohmann::json logs = store::list_logs("", "", "", 50);
    std::string payload = "data: " + logs.dump() + "\n\n";
    res.write(payload);
    res.end();
  });

  // --- / (root): serve dashboard ---
  CROW_ROUTE(app, "/")
  ([]() {
    std::string html = web::read_file("index.html");
    if (html.empty()) {
      return crow::response("{\"service\":\"wa_api_server\",\"docs\":\"/docs\",\"health\":\"/api/health\"}");
    }
    crow::response res(html);
    res.set_header("Content-Type", "text/html");
    return res;
  });

  // --- static assets (css/js) ---
  CROW_ROUTE(app, "/static/<path>")
  ([](const std::string& path) {
    std::string content = web::read_file(path);
    if (content.empty()) return crow::response(404, "not found");
    crow::response res(content);
    res.set_header("Content-Type", web::mime_type(path));
    return res;
  });

  // --- /openapi.yaml ---
  CROW_ROUTE(app, "/openapi.yaml")
  ([]() {
    std::ifstream f("openapi.yaml");
    if (!f) return crow::response(404, "openapi.yaml not found");
    std::ostringstream ss;
    ss << f.rdbuf();
    return crow::response(ss.str());
  });

  // --- /docs (minimal static Swagger UI redirect) ---
  CROW_ROUTE(app, "/docs")
  ([]() {
    std::string html =
        "<!doctype html><html><head><title>wa_api_server</title>"
        "<link rel='stylesheet' href='https://unpkg.com/swagger-ui-dist/swagger-ui.css'>"
        "</head><body><div id='swagger-ui'></div>"
        "<script src='https://unpkg.com/swagger-ui-dist/swagger-ui-bundle.js'></script>"
        "<script>SwaggerUIBundle({url:'/openapi.yaml',dom_id:'#swagger-ui'});</script>"
        "</body></html>";
    return crow::response(html);
  });

  logger::info("Starting wa_api_server on " + g_config.host + ":" + std::to_string(g_config.port));
  logger::info("Simulation mode: " + std::string(simulate::enabled() ? "ON" : "OFF"));
  sched::start();
  app.port(g_config.port).multithreaded().run();

  // Shutdown.
  sched::stop();
  g_running = false;
  for (auto& t : g_workers) if (t.joinable()) t.join();
  blast::reset();
  return 0;
}
