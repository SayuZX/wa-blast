// templates.cpp — template storage + placeholder rendering.
#include "templates.h"

#include <ctime>
#include <sstream>

#include "store.h"
#include "util.h"

namespace tmpl {

static std::string q(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out += c;
  }
  out += "'";
  return out;
}

nlohmann::json list() {
  return store::query("SELECT id,name,body,created_at FROM templates ORDER BY name");
}

nlohmann::json get(int id) {
  nlohmann::json rows = store::query(
      "SELECT id,name,body,created_at FROM templates WHERE id=" + std::to_string(id));
  return rows.empty() ? nlohmann::json() : rows[0];
}

nlohmann::json save(const std::string& name, const std::string& body, int id) {
  std::string sql;
  std::string err;
  if (id >= 0) {
    sql = "UPDATE templates SET name=" + q(name) + ", body=" + q(body) +
          " WHERE id=" + std::to_string(id);
    if (!store::exec_sql(sql, err)) return {{"error", err}};
    return {{"id", id}, {"name", name}};
  }
  sql = "INSERT INTO templates(name,body,created_at) VALUES("
        + q(name) + "," + q(body) + "," + q(util::now_iso8601()) + ")";
  if (!store::exec_sql(sql, err)) return {{"error", err}};
  // fetch last insert id
  nlohmann::json row = store::query("SELECT last_insert_rowid() AS id");
  int new_id = row.empty() ? -1 : row[0].value("id", -1);
  return {{"id", new_id}, {"name", name}};
}

bool remove(int id) {
  std::string err;
  return store::exec_sql("DELETE FROM templates WHERE id=" + std::to_string(id), err);
}

std::string render(const std::string& body, const nlohmann::json& vars) {
  std::string out = body;

  // Built-in date/time.
  std::time_t now = std::time(nullptr);
  std::tm tmv{};
#if defined(_WIN32)
  localtime_s(&tmv, &now);
#else
  localtime_r(&now, &tmv);
#endif
  char date_buf[16], time_buf[16];
  std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tmv);
  std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tmv);

  // Replace all {{var}} occurrences.
  for (auto it = vars.begin(); it != vars.end(); ++it) {
    std::string key = "{{" + it.key() + "}}";
    std::string val = it.value().is_string() ? it.value().get<std::string>()
                                              : it.value().dump();
    size_t pos = 0;
    while ((pos = out.find(key, pos)) != std::string::npos) {
      out.replace(pos, key.size(), val);
      pos += val.size();
    }
  }
  // Built-ins (in case not overridden).
  auto repl = [&](const std::string& key, const std::string& val) {
    size_t pos = 0;
    while ((pos = out.find(key, pos)) != std::string::npos) {
      out.replace(pos, key.size(), val);
      pos += val.size();
    }
  };
  repl("{{date}}", date_buf);
  repl("{{time}}", time_buf);

  return out;
}

}  // namespace tmpl
