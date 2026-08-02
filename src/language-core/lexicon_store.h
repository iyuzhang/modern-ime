#pragma once

#include "contracts/contracts.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <vector>

struct sqlite3;

namespace modernime {

class LexiconStore {
public:
    explicit LexiconStore(std::filesystem::path database);
    ~LexiconStore();
    LexiconStore(const LexiconStore &) = delete;
    LexiconStore &operator=(const LexiconStore &) = delete;

    bool healthy() const;
    std::string lastError() const;
    std::vector<Lexeme> list(std::string_view query = {}) const;
    std::vector<Lexeme> matching(std::string_view reading) const;
    std::optional<Lexeme> upsert(Lexeme lexeme);
    bool remove(int64_t id);
    bool clearLearned();
    bool recordSelection(std::string_view reading, std::string_view output);
    std::string exportJson() const;
    bool importJson(std::string_view document, std::string *reason = nullptr);

private:
    bool execute(const char *sql) const;
    bool migrate();
    void setError(std::string message) const;
    std::filesystem::path database_;
    mutable std::mutex mutex_;
    mutable sqlite3 *database_handle_ = nullptr;
    mutable std::string last_error_;
};

} // namespace modernime
