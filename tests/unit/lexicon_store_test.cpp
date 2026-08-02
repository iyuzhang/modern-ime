#include "language-core/lexicon_store.h"
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
TEST_CASE("manual entries rank ahead and export losslessly") { const auto database = std::filesystem::temp_directory_path() / "modern-ime-lexicon-test.db"; std::filesystem::remove(database); modernime::LexiconStore store(database); auto saved = store.upsert({.reading = "github actions", .output = "GitHub Actions", .language_tag = "en", .kind = "manual", .pinned = true}); REQUIRE(saved); const auto matching = store.matching("github actions"); REQUIRE(matching.size() == 1); REQUIRE(matching.front().pinned); const auto document = store.exportJson(); REQUIRE(document.find("GitHub Actions") != std::string::npos); REQUIRE(store.clearLearned()); std::filesystem::remove(database); }
TEST_CASE("invalid lexicon import is rejected") { const auto database = std::filesystem::temp_directory_path() / "modern-ime-lexicon-import-test.db"; std::filesystem::remove(database); modernime::LexiconStore store(database); REQUIRE_FALSE(store.importJson("{}")); std::filesystem::remove(database); }
TEST_CASE("recording selections increments learned statistics transactionally") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-lexicon-selection-test.db";
    std::filesystem::remove(database);
    modernime::LexiconStore store(database);
    REQUIRE(store.recordSelection("balala", "巴拉拉"));
    REQUIRE(store.recordSelection("balala", "巴拉拉"));
    const auto entries = store.matching("balala");
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.front().kind == "learned");
    REQUIRE(entries.front().select_count == 2);
    REQUIRE(entries.front().last_selected_at > 0);
    std::filesystem::remove(database);
}
