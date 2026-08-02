#include "language-core/language_core.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <vector>

int main() {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-latency-benchmark.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    std::vector<double> milliseconds;
    milliseconds.reserve(500);
    for (int iteration = 0; iteration < 500; ++iteration) {
        const auto started = std::chrono::steady_clock::now();
        const auto candidates = core.candidates("nihao", 9);
        const auto finished = std::chrono::steady_clock::now();
        if (candidates.empty()) return 2;
        milliseconds.push_back(std::chrono::duration<double, std::milli>(finished - started).count());
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const auto p95 = milliseconds.at(474);
    std::cout << "keyboard_candidate_model_p95_ms=" << p95 << '\n';
    std::filesystem::remove(database);
    return p95 <= 20.0 ? 0 : 3;
}
