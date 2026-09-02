// util.h — small cross-platform helpers (uuid, base64, time, trim).
#pragma once

#include <string>
#include <vector>
#include <chrono>

namespace util {

// RFC-4122 v4 UUID (random).
std::string uuid4();

// ISO8601 UTC timestamp, e.g. "2026-09-02T17:40:00.123Z".
std::string now_iso8601();

// Milliseconds since epoch.
long long epoch_ms();

// base64 encode/decode (for clipboard-safe ADB text).
std::string base64_encode(const std::string& data);
std::string base64_decode(const std::string& data);

// Trim whitespace.
std::string trim(const std::string& s);

// Simple constant-time string comparison (API-key safe compare).
bool constant_time_equals(const std::string& a, const std::string& b);

// Generate a random hex token (for API key suggestions).
std::string random_hex(size_t bytes);

}  // namespace util
