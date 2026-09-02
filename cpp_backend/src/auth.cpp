// auth.cpp — API-key verification.
#include "auth.h"

#include "config.h"
#include "util.h"

namespace auth {

static const conf::Config* g_cfg = nullptr;
void set_config(const conf::Config* cfg) { g_cfg = cfg; }

bool verify(const std::string& provided) {
  if (!g_cfg) return false;
  const std::string& expected = g_cfg->api_key;
  if (expected == "CHANGE_ME_GENERATE_A_RANDOM_KEY") return false;
  return util::constant_time_equals(provided, expected);
}

}  // namespace auth
