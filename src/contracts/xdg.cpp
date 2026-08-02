#include "contracts/xdg.h"
#include <cstdlib>
namespace modernime {
namespace { std::filesystem::path fromEnv(const char *name, const std::filesystem::path &fallback) { if (const char *value = std::getenv(name); value && *value) return value; return fallback; } }
XdgPaths xdgPaths() { const auto home = std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : "."); return {fromEnv("XDG_CONFIG_HOME", home / ".config") / "modern-ime", fromEnv("XDG_DATA_HOME", home / ".local/share") / "modern-ime", fromEnv("XDG_STATE_HOME", home / ".local/state") / "modern-ime", fromEnv("XDG_CACHE_HOME", home / ".cache") / "modern-ime", fromEnv("XDG_RUNTIME_DIR", "/tmp") / "modern-ime"}; }
bool ensureXdgDirectories(const XdgPaths &paths) { std::error_code error; for (const auto &path : {paths.config, paths.data, paths.state, paths.cache, paths.runtime, paths.data / "models"}) { std::filesystem::create_directories(path, error); if (error) return false; } return true; }
}
