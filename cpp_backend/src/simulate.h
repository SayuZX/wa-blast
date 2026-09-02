// simulate.h — simulation mode (non-root testing).
#pragma once

#include <string>

namespace simulate {

// Enable/disable simulation mode.
void set_enabled(bool on);

// True if simulation mode is active (no root / --simulate flag).
bool enabled();

// If simulation is on, log a fake send instead of doing real ADB.
// Returns true if the send was simulated (caller should skip real ADB).
bool maybe_simulate_send(const std::string& profile,
                         const std::string& number,
                         const std::string& message);

}  // namespace simulate
