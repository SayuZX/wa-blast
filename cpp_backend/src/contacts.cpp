// contacts.cpp — SQLite-backed contact CRUD + import.
#include "contacts.h"

#include <sstream>

#include "store.h"
#include "util.h"

namespace contacts {

static std::string q(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "''";
    else out += c;
  }
  out += "'";
  return out;
}

nlohmann::json list(const std::string& grp) {
  std::string sql = "SELECT id,name,phone,grp,custom_fields,created_at FROM contacts";
  if (!grp.empty()) sql += " WHERE grp=" + q(grp);
  sql += " ORDER BY name";
  return store::query(sql);
}

nlohmann::json add(const std::string& name,
                   const std::string& phone,
                   const std::string& grp,
                   const nlohmann::json& custom_fields) {
  std::string custom = custom_fields.is_null() ? "{}" : custom_fields.dump();
  std::string sql =
      "INSERT INTO contacts(name,phone,grp,custom_fields,created_at) VALUES("
      + q(name) + "," + q(phone) + "," + q(grp) + "," + q(custom) + ","
      + q(util::now_iso8601()) + ")";
  std::string err;
  if (!store::exec_sql(sql, err)) {
    nlohmann::json e;
    e["error"] = err;
    return e;
  }
  nlohmann::json r;
  r["ok"] = true;
  r["name"] = name;
  r["phone"] = phone;
  return r;
}

nlohmann::json import(const std::vector<nlohmann::json>& rows) {
  int imported = 0, failed = 0;
  for (const auto& row : rows) {
    std::string name = row.value("name", "");
    std::string phone = row.value("phone", "");
    if (phone.empty()) { failed++; continue; }
    // custom fields = everything except name/phone/grp.
    nlohmann::json custom = nlohmann::json::object();
    for (auto it = row.begin(); it != row.end(); ++it) {
      const std::string& k = it.key();
      if (k == "name" || k == "phone" || k == "grp") continue;
      custom[k] = it.value();
    }
    std::string grp = row.value("grp", "");
    nlohmann::json r = add(name, phone, grp, custom);
    if (r.contains("ok") && r["ok"].get<bool>()) imported++; else failed++;
  }
  nlohmann::json out;
  out["imported"] = imported;
  out["failed"] = failed;
  return out;
}

bool remove(int id) {
  std::string sql = "DELETE FROM contacts WHERE id=" + std::to_string(id);
  std::string err;
  return store::exec_sql(sql, err);
}

nlohmann::json search(const std::string& qstr) {
  std::string sql =
      "SELECT id,name,phone,grp,custom_fields,created_at FROM contacts WHERE "
      "name LIKE " + q("%" + qstr + "%") + " OR phone LIKE " + q("%" + qstr + "%");
  return store::query(sql);
}

}  // namespace contacts
