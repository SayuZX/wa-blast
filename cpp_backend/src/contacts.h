// contacts.h — contact management (SQLite-backed CRUD + import).
#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace contacts {

// List contacts, optionally filtered by group.
nlohmann::json list(const std::string& grp);

// Add a single contact.
nlohmann::json add(const std::string& name,
                   const std::string& phone,
                   const std::string& grp,
                   const nlohmann::json& custom_fields);

// Import a list of contacts (each is {name, phone, grp, ...custom}).
// Returns {imported: N, failed: M}.
nlohmann::json import(const std::vector<nlohmann::json>& rows);

// Delete a contact by id.
bool remove(int id);

// Search contacts by name/phone substring.
nlohmann::json search(const std::string& q);

}  // namespace contacts
