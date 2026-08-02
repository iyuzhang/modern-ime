#include "contracts/logging.h"
#include "contracts/xdg.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
namespace modernime { namespace { std::mutex logMutex; void log(std::string_view level, std::string_view component, std::string_view message) { std::scoped_lock lock(logMutex); const auto path = xdgPaths().state / "modern-ime.jsonl"; std::ofstream out(path, std::ios::app); const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); const auto line = "{\"ts\":" + std::to_string(millis) + ",\"level\":\"" + std::string(level) + "\",\"component\":\"" + std::string(component) + "\",\"message\":\"" + std::string(message) + "\"}"; if (out) out << line << '\n'; std::cerr << line << '\n'; } } void logInfo(std::string_view c, std::string_view m) { log("info", c, m); } void logError(std::string_view c, std::string_view m) { log("error", c, m); } }
