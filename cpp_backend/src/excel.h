// excel.h — parse CSV and XLSX files into contact rows.
#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace excel {

// Parse file content (by filename extension). Returns a list of contact rows
// (each {name, phone, grp, ...custom}). The "phone" column is mandatory.
// For XLSX, requires libxlsxio linked in (see CMakeLists). Falls back to
// treating the bytes as CSV if extension is unknown.
std::vector<nlohmann::json> parse(const std::string& filename,
                                  const std::string& content);

// Parse CSV text (with optional header row).
std::vector<nlohmann::json> parse_csv(const std::string& text);

}  // namespace excel
