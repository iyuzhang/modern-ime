#include "language-core/lexicon_store.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <string>

namespace modernime {
namespace {

int64_t nowSeconds() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
void bindText(sqlite3_stmt *statement, int index, std::string_view value) { const char *data = value.empty() ? "" : value.data(); sqlite3_bind_text(statement, index, data, static_cast<int>(value.size()), SQLITE_TRANSIENT); }
std::string columnText(sqlite3_stmt *statement, int index) { const auto *value = reinterpret_cast<const char *>(sqlite3_column_text(statement, index)); return value ? value : ""; }
Lexeme row(sqlite3_stmt *statement) { return {sqlite3_column_int64(statement, 0), columnText(statement, 1), columnText(statement, 2), columnText(statement, 3), columnText(statement, 4), sqlite3_column_int(statement, 5) != 0, sqlite3_column_int(statement, 6) != 0, sqlite3_column_double(statement, 7), static_cast<uint64_t>(sqlite3_column_int64(statement, 8)), sqlite3_column_int64(statement, 9)}; }
QString asQString(std::string_view value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
std::string asString(const QString &value) { return value.toUtf8().toStdString(); }

} // namespace

LexiconStore::LexiconStore(std::filesystem::path database) : database_(std::move(database)) {
    if (sqlite3_open_v2(database_.c_str(), &database_handle_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) { setError(database_handle_ ? sqlite3_errmsg(database_handle_) : "cannot open database"); return; }
    execute("PRAGMA journal_mode=WAL"); execute("PRAGMA foreign_keys=ON"); execute("PRAGMA busy_timeout=1000"); migrate();
}

LexiconStore::~LexiconStore() { std::scoped_lock lock(mutex_); if (database_handle_) sqlite3_close_v2(database_handle_); }
bool LexiconStore::healthy() const { std::scoped_lock lock(mutex_); return database_handle_ != nullptr && last_error_.empty(); }
std::string LexiconStore::lastError() const { std::scoped_lock lock(mutex_); return last_error_; }
void LexiconStore::setError(std::string message) const { last_error_ = std::move(message); }
bool LexiconStore::execute(const char *sql) const { char *error = nullptr; if (!database_handle_ || sqlite3_exec(database_handle_, sql, nullptr, nullptr, &error) != SQLITE_OK) { setError(error ? error : "sqlite execution failed"); sqlite3_free(error); return false; } return true; }

bool LexiconStore::migrate() {
    std::scoped_lock lock(mutex_);
    return execute("BEGIN IMMEDIATE;"
                   "CREATE TABLE IF NOT EXISTS schema_meta (schema_version INTEGER NOT NULL, created_at INTEGER NOT NULL);"
                   "INSERT INTO schema_meta (schema_version, created_at) SELECT 1, strftime('%s','now') WHERE NOT EXISTS (SELECT 1 FROM schema_meta);"
                   "CREATE TABLE IF NOT EXISTS lexeme (id INTEGER PRIMARY KEY, reading TEXT NOT NULL, output TEXT NOT NULL, language_tag TEXT NOT NULL DEFAULT 'und', kind TEXT NOT NULL CHECK(kind IN ('manual','learned','replacement')), pinned INTEGER NOT NULL DEFAULT 0, blocked INTEGER NOT NULL DEFAULT 0, base_weight REAL NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, UNIQUE(reading, output));"
                   "CREATE TABLE IF NOT EXISTS selection_stat (lexeme_id INTEGER PRIMARY KEY REFERENCES lexeme(id) ON DELETE CASCADE, select_count INTEGER NOT NULL DEFAULT 0, last_selected_at INTEGER NOT NULL DEFAULT 0, negative_count INTEGER NOT NULL DEFAULT 0);"
                   "CREATE INDEX IF NOT EXISTS lexeme_reading_index ON lexeme(reading);"
                   "COMMIT;");
}

std::vector<Lexeme> LexiconStore::list(std::string_view query) const {
    std::scoped_lock lock(mutex_); std::vector<Lexeme> result; if (!database_handle_) return result;
    const char *sql = "SELECT l.id,l.reading,l.output,l.language_tag,l.kind,l.pinned,l.blocked,l.base_weight,COALESCE(s.select_count,0),COALESCE(s.last_selected_at,0) FROM lexeme l LEFT JOIN selection_stat s ON s.lexeme_id=l.id WHERE (?1='' OR l.reading LIKE '%' || ?1 || '%' OR l.output LIKE '%' || ?1 || '%') ORDER BY l.pinned DESC,l.updated_at DESC,l.id DESC";
    sqlite3_stmt *statement = nullptr; if (sqlite3_prepare_v2(database_handle_, sql, -1, &statement, nullptr) != SQLITE_OK) { setError(sqlite3_errmsg(database_handle_)); return result; } bindText(statement, 1, query); while (sqlite3_step(statement) == SQLITE_ROW) result.push_back(row(statement)); sqlite3_finalize(statement); return result;
}

std::vector<Lexeme> LexiconStore::matching(std::string_view reading) const {
    std::scoped_lock lock(mutex_); std::vector<Lexeme> result; if (!database_handle_) return result;
    const char *sql = "SELECT l.id,l.reading,l.output,l.language_tag,l.kind,l.pinned,l.blocked,l.base_weight,COALESCE(s.select_count,0),COALESCE(s.last_selected_at,0) FROM lexeme l LEFT JOIN selection_stat s ON s.lexeme_id=l.id WHERE l.reading=?1 AND l.blocked=0 ORDER BY l.pinned DESC,(l.kind='manual') DESC,COALESCE(s.select_count,0) DESC,COALESCE(s.last_selected_at,0) DESC,l.base_weight DESC,l.output ASC";
    sqlite3_stmt *statement = nullptr; if (sqlite3_prepare_v2(database_handle_, sql, -1, &statement, nullptr) != SQLITE_OK) { setError(sqlite3_errmsg(database_handle_)); return result; } bindText(statement, 1, reading); while (sqlite3_step(statement) == SQLITE_ROW) result.push_back(row(statement)); sqlite3_finalize(statement); return result;
}

std::optional<Lexeme> LexiconStore::upsert(Lexeme lexeme) {
    if (lexeme.reading.empty() || lexeme.output.empty() || lexeme.reading.size() > 256 || lexeme.output.size() > 1024) return std::nullopt;
    std::scoped_lock lock(mutex_); if (!database_handle_) return std::nullopt; const int64_t now = nowSeconds();
    const char *sql = "INSERT INTO lexeme(reading,output,language_tag,kind,pinned,blocked,base_weight,created_at,updated_at) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?8) ON CONFLICT(reading,output) DO UPDATE SET language_tag=excluded.language_tag,kind=excluded.kind,pinned=excluded.pinned,blocked=excluded.blocked,base_weight=excluded.base_weight,updated_at=excluded.updated_at RETURNING id,reading,output,language_tag,kind,pinned,blocked,base_weight,0,0";
    sqlite3_stmt *statement = nullptr; if (sqlite3_prepare_v2(database_handle_, sql, -1, &statement, nullptr) != SQLITE_OK) { setError(sqlite3_errmsg(database_handle_)); return std::nullopt; } bindText(statement, 1, lexeme.reading); bindText(statement, 2, lexeme.output); bindText(statement, 3, lexeme.language_tag); bindText(statement, 4, lexeme.kind); sqlite3_bind_int(statement, 5, lexeme.pinned); sqlite3_bind_int(statement, 6, lexeme.blocked); sqlite3_bind_double(statement, 7, lexeme.base_weight); sqlite3_bind_int64(statement, 8, now); std::optional<Lexeme> result; if (sqlite3_step(statement) == SQLITE_ROW) result = row(statement); else setError(sqlite3_errmsg(database_handle_)); sqlite3_finalize(statement); return result;
}

bool LexiconStore::remove(int64_t id) { std::scoped_lock lock(mutex_); sqlite3_stmt *statement = nullptr; if (!database_handle_ || sqlite3_prepare_v2(database_handle_, "DELETE FROM lexeme WHERE id=?1", -1, &statement, nullptr) != SQLITE_OK) return false; sqlite3_bind_int64(statement, 1, id); const bool result = sqlite3_step(statement) == SQLITE_DONE; sqlite3_finalize(statement); return result; }
bool LexiconStore::clearLearned() { std::scoped_lock lock(mutex_); return execute("DELETE FROM lexeme WHERE kind='learned'"); }
bool LexiconStore::recordSelection(std::string_view reading, std::string_view output) {
    std::scoped_lock lock(mutex_); if (!database_handle_) return false; const char *sql = "INSERT INTO lexeme(reading,output,language_tag,kind,created_at,updated_at) VALUES(?1,?2,'und','learned',?3,?3) ON CONFLICT(reading,output) DO UPDATE SET updated_at=excluded.updated_at; INSERT INTO selection_stat(lexeme_id,select_count,last_selected_at) SELECT id,1,?3 FROM lexeme WHERE reading=?1 AND output=?2 ON CONFLICT(lexeme_id) DO UPDATE SET select_count=select_count+1,last_selected_at=excluded.last_selected_at"; char *error = nullptr; sqlite3_stmt *statement = nullptr; if (sqlite3_exec(database_handle_, "BEGIN IMMEDIATE", nullptr, nullptr, &error) != SQLITE_OK || sqlite3_prepare_v2(database_handle_, sql, -1, &statement, nullptr) != SQLITE_OK) { sqlite3_free(error); execute("ROLLBACK"); return false; } bindText(statement, 1, reading); bindText(statement, 2, output); sqlite3_bind_int64(statement, 3, nowSeconds()); const bool result = sqlite3_step(statement) == SQLITE_DONE && execute("COMMIT"); sqlite3_finalize(statement); if (!result) execute("ROLLBACK"); return result;
}

std::string LexiconStore::exportJson() const { QJsonArray entries; for (const auto &item : list()) entries.append(QJsonObject{{QStringLiteral("reading"), asQString(item.reading)}, {QStringLiteral("output"), asQString(item.output)}, {QStringLiteral("language_tag"), asQString(item.language_tag)}, {QStringLiteral("kind"), asQString(item.kind)}, {QStringLiteral("pinned"), item.pinned}, {QStringLiteral("blocked"), item.blocked}, {QStringLiteral("base_weight"), item.base_weight}}); return QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("modern-ime-lexicon")}, {QStringLiteral("version"), static_cast<int>(kLexiconFormatVersion)}, {QStringLiteral("entries"), entries}}).toJson(QJsonDocument::Compact).toStdString(); }

bool LexiconStore::importJson(std::string_view document, std::string *reason) {
    const auto parsed = QJsonDocument::fromJson(QByteArray(document.data(), static_cast<qsizetype>(document.size()))); if (!parsed.isObject() || parsed.object().value(QStringLiteral("format")) != QStringLiteral("modern-ime-lexicon") || parsed.object().value(QStringLiteral("version")).toInt() != static_cast<int>(kLexiconFormatVersion)) { if (reason) *reason = "unsupported lexicon format"; return false; }
    const auto entries = parsed.object().value(QStringLiteral("entries")).toArray(); if (entries.size() > 100000) { if (reason) *reason = "too many entries"; return false; }
    std::vector<Lexeme> parsed_entries; for (const auto value : entries) { const auto entry = value.toObject(); Lexeme lexeme; lexeme.reading = asString(entry.value(QStringLiteral("reading")).toString()); lexeme.output = asString(entry.value(QStringLiteral("output")).toString()); lexeme.language_tag = asString(entry.value(QStringLiteral("language_tag")).toString(QStringLiteral("und"))); lexeme.kind = asString(entry.value(QStringLiteral("kind")).toString(QStringLiteral("manual"))); lexeme.pinned = entry.value(QStringLiteral("pinned")).toBool(); lexeme.blocked = entry.value(QStringLiteral("blocked")).toBool(); lexeme.base_weight = entry.value(QStringLiteral("base_weight")).toDouble(); if (lexeme.reading.empty() || lexeme.output.empty() || (lexeme.kind != "manual" && lexeme.kind != "learned" && lexeme.kind != "replacement")) { if (reason) *reason = "invalid entry"; return false; } parsed_entries.push_back(std::move(lexeme)); }
    for (auto &entry : parsed_entries) {
        if (!upsert(std::move(entry))) {
            if (reason) *reason = lastError();
            return false;
        }
    }
    return true;
}

} // namespace modernime
