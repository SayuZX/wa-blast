// props.h — per-profile device identity spoofing (props switch).
#pragma once

#include <string>

#include "nlohmann/json.hpp"

namespace props {

bool load(const std::string& dir, const std::string& profile, nlohmann::json& out);
bool save(const std::string& dir, const std::string& profile, const nlohmann::json& data);
bool apply(const nlohmann::json& spoof, std::string& err);

nlohmann::json generate();

}  // namespace props
