#include "language-core/lexicon_store.h"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <sqlite3.h>
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
TEST_CASE("mixed input preferences persist globally and per application") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-input-preference-test.db";
    std::filesystem::remove(database);
    modernime::LexiconStore store(database);
    REQUIRE(store.recordInputPreference("main", "code", true));
    REQUIRE(store.recordInputPreference("main", "code", false));
    const auto preferences = store.inputPreferences();
    const auto global = std::find_if(preferences.begin(), preferences.end(), [](const modernime::InputPreference &preference) { return preference.token == "main" && preference.scope.empty(); });
    const auto scoped = std::find_if(preferences.begin(), preferences.end(), [](const modernime::InputPreference &preference) { return preference.token == "main" && preference.scope == "code"; });
    REQUIRE(global != preferences.end());
    REQUIRE(scoped != preferences.end());
    REQUIRE(global->literal_count == 1);
    REQUIRE(global->chinese_count == 1);
    REQUIRE(scoped->literal_count == 1);
    REQUIRE(scoped->chinese_count == 1);
    REQUIRE(store.clearLearned());
    REQUIRE(store.inputPreferences().empty());
    std::filesystem::remove(database);
}
TEST_CASE("version two preference tables gain the last-choice column") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-input-preference-migration-test.db";
    std::filesystem::remove(database);
    sqlite3 *handle = nullptr;
    REQUIRE(sqlite3_open(database.c_str(), &handle) == SQLITE_OK);
    REQUIRE(sqlite3_exec(handle,
                         "CREATE TABLE schema_meta(schema_version INTEGER NOT NULL,created_at INTEGER NOT NULL);"
                         "INSERT INTO schema_meta VALUES(2,0);"
                         "CREATE TABLE input_preference(token TEXT NOT NULL,scope TEXT NOT NULL DEFAULT '',literal_count INTEGER NOT NULL DEFAULT 0,chinese_count INTEGER NOT NULL DEFAULT 0,last_selected_at INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(token,scope)) WITHOUT ROWID;",
                         nullptr, nullptr, nullptr) == SQLITE_OK);
    sqlite3_close(handle);
    modernime::LexiconStore store(database);
    REQUIRE(store.healthy());
    REQUIRE(store.recordInputPreference("main", "code", true));
    const auto preferences = store.inputPreferences();
    REQUIRE(preferences.size() == 2);
    REQUIRE(std::all_of(preferences.begin(), preferences.end(), [](const modernime::InputPreference &preference) { return preference.last_literal; }));
    std::filesystem::remove(database);
}
