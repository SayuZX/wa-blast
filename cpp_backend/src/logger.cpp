// logger.cpp — leveled console logger.
#include "logger.h"

#include <cstdio>
#include <ctime>

#include "util.h"

namespace logger {

static const char* level_name(Level l) {
  switch (l) {
    case DEBUG: return "DEBUG";
    case INFO: return "INFO";
    case WARN: return "WARN";
    case ERROR: return "ERROR";
  }
  return "?";
}

void log(Level lvl, const std::string& msg) {
  std::fprintf(stderr, "[%s] %s %s\n", level_name(lvl), util::now_iso8601().c_str(), msg.c_str());
}

void info(const std::string& msg) { log(INFO, msg); }
void warn(const std::string& msg) { log(WARN, msg); }
void error(const std::string& msg) { log(ERROR, msg); }

}  // namespace logger
