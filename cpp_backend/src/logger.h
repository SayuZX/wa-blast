// logger.h — simple leveled console logger (also mirrors to store if desired).
#pragma once

#include <string>

namespace logger {

enum Level { DEBUG, INFO, WARN, ERROR };

void log(Level lvl, const std::string& msg);
void info(const std::string& msg);
void warn(const std::string& msg);
void error(const std::string& msg);

}  // namespace logger
