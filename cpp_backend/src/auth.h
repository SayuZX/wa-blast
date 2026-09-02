// auth.h — API-key authentication (Bearer token or X-API-Key header).
#pragma once

#include <string>

namespace conf { struct Config; }

namespace auth {

// Set the global config pointer (called once at startup).
void set_config(const conf::Config* cfg);

// Check a presented token/key against the configured API key.
// `provided` may come from `Authorization: Bearer <t>` or `X-API-Key` header.
bool verify(const std::string& provided);

}  // namespace auth
