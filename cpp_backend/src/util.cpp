// util.cpp — implementations.
#include "util.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <random>
#include <sstream>

namespace util {

std::string uuid4() {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  static thread_local std::uniform_int_distribution<uint64_t> dist;
  uint64_t a = dist(rng);
  uint64_t b = dist(rng);
  // RFC 4122: set version (4) and variant bits.
  a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

  char buf[37];
  std::snprintf(buf, sizeof(buf),
                "%08x-%04x-%04x-%04x-%012llx",
                (uint32_t)(a >> 32), (uint32_t)((a >> 16) & 0xFFFF),
                (uint32_t)(a & 0xFFFF), (uint32_t)(b >> 48),
                (unsigned long long)(b & 0xFFFFFFFFFFFFULL));
  return std::string(buf);
}

std::string now_iso8601() {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  std::ostringstream os;
  os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.'
     << std::setw(3) << std::setfill('0') << ms.count() << 'Z';
  return os.str();
}

long long epoch_ms() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static const char* b64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string& data) {
  std::string out;
  int val = 0, valb = -6;
  for (unsigned char c : data) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(b64_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(b64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

std::string base64_decode(const std::string& data) {
  static int rev[256];
  static bool init = false;
  if (!init) {
    for (int i = 0; i < 64; i++) rev[(unsigned char)b64_chars[i]] = i;
    init = true;
  }
  std::string out;
  int val = 0, valb = -8;
  for (unsigned char c : data) {
    if (c == '=') break;
    if (rev[c] == 0 && c != 'A') continue;
    val = (val << 6) + rev[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back((char)((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

std::string trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

bool constant_time_equals(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  unsigned char diff = 0;
  for (size_t i = 0; i < a.size(); i++) diff |= (a[i] ^ b[i]);
  return diff == 0;
}

std::string random_hex(size_t bytes) {
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  static thread_local std::uniform_int_distribution<uint64_t> dist;
  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(bytes * 2);
  size_t words = (bytes + 7) / 8;
  for (size_t i = 0; i < words; i++) {
    uint64_t v = dist(rng);
    for (int j = 0; j < 16; j++) {
      out.push_back(hex[(v >> (4 * j)) & 0xF]);
    }
  }
  out.resize(bytes * 2);
  return out;
}

}  // namespace util
