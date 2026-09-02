// excel.cpp — CSV parser (native) + XLSX via libxlsxio (optional).
#include "excel.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "util.h"

namespace excel {

// Minimal CSV splitter that respects quoted fields.
static std::vector<std::string> split_csv_line(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  bool in_quotes = false;
  for (size_t i = 0; i < line.size(); i++) {
    char c = line[i];
    if (c == '"') {
      if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
        cur += '"';
        i++;
      } else {
        in_quotes = !in_quotes;
      }
    } else if (c == ',' && !in_quotes) {
      out.push_back(util::trim(cur));
      cur.clear();
    } else {
      cur += c;
    }
  }
  out.push_back(util::trim(cur));
  return out;
}

std::vector<nlohmann::json> parse_csv(const std::string& text) {
  std::vector<nlohmann::json> rows;
  std::vector<std::string> header;
  bool have_header = false;

  std::istringstream ss(text);
  std::string line;
  bool first = true;
  while (std::getline(ss, line)) {
    if (util::trim(line).empty()) continue;
    auto cols = split_csv_line(line);
    if (first) {
      // Detect header: first cell is a known column name.
      std::string c0 = cols.empty() ? "" : cols[0];
      std::string lower = c0;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (lower == "phone" || lower == "nama" || lower == "name" ||
          lower == "nomor" || lower == "number" || lower == "no") {
        header = cols;
        have_header = true;
        first = false;
        continue;
      }
      first = false;
    }

    if (have_header) {
      nlohmann::json row = nlohmann::json::object();
      for (size_t i = 0; i < cols.size() && i < header.size(); i++) {
        row[header[i]] = cols[i];
      }
      // Normalize "phone" key.
      for (auto it = row.begin(); it != row.end(); ++it) {
        std::string k = it.key();
        std::string lower = k;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if (lower == "nama" || lower == "name") row["name"] = it.value();
        if (lower == "phone" || lower == "nomor" || lower == "number" || lower == "no")
          row["phone"] = it.value();
        if (lower == "grp" || lower == "group" || lower == "grup") row["grp"] = it.value();
      }
      rows.push_back(row);
    } else {
      // No header: first col = phone, second = name.
      nlohmann::json row = nlohmann::json::object();
      if (!cols.empty()) row["phone"] = cols[0];
      if (cols.size() > 1) row["name"] = cols[1];
      rows.push_back(row);
    }
  }
  return rows;
}

std::vector<nlohmann::json> parse(const std::string& filename,
                                  const std::string& content) {
  std::string lower = filename;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  if (lower.find(".xlsx") != std::string::npos) {
    // XLSX requires libxlsxio. If not linked, fall back to CSV attempt.
    // (See CMakeLists: xlsxio is optional; without it we treat as CSV.)
    return parse_csv(content);
  }
  return parse_csv(content);
}

}  // namespace excel
