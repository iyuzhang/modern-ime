#pragma once
#include <filesystem>
namespace modernime {
struct XdgPaths { std::filesystem::path config; std::filesystem::path data; std::filesystem::path state; std::filesystem::path cache; std::filesystem::path runtime; };
XdgPaths xdgPaths();
bool ensureXdgDirectories(const XdgPaths &paths);
}
