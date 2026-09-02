// templates.h — message templates with placeholder substitution.
#pragma once

#include <string>

#include "nlohmann/json.hpp"

namespace tmpl {

// List all templates.
nlohmann::json list();

// Get a template by id (returns null if not found).
nlohmann::json get(int id);

// Create/update a template. Returns {id, name}.
nlohmann::json save(const std::string& name, const std::string& body, int id = -1);

// Delete a template.
bool remove(int id);

// Render a template body by substituting {{var}} placeholders with `vars`.
// Built-in variables: {{date}} -> YYYY-MM-DD, {{time}} -> HH:MM:SS.
std::string render(const std::string& body, const nlohmann::json& vars);

}  // namespace tmpl
