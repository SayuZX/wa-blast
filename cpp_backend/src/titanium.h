// titanium.h — profile cloning/switching via data isolation (Titanium-style).
#pragma once

#include <string>
#include <vector>

namespace conf { struct Config; }

namespace titanium {

// Set the global config pointer (called once at startup).
void set_config(const conf::Config* cfg);

// Path to the isolated profile storage (default /data/local/tmp/wa_profiles).
std::string profiles_dir();

// List available profiles (subdirectories under profiles_dir).
std::vector<std::string> list_profiles();

// Snapshot the current WhatsApp data into <profiles_dir>/<profile>.
// Uses `cp -rf /data/data/<pkg>/* <dest>/` (root).
bool snapshot(const std::string& pkg, const std::string& profile, std::string& err);

// Restore a profile: wipe current data then `cp -rf` back.
bool restore(const std::string& pkg, const std::string& profile, std::string& err);

// Switch to profile: snapshot current (optional), restore target, relaunch app.
bool switch_to(const std::string& pkg,
               const std::string& from_profile,
               const std::string& to_profile,
               bool snapshot_current,
               std::string& err);

}  // namespace titanium
