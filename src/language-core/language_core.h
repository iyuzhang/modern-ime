#pragma once
#include "contracts/contracts.h"
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>
namespace libime { class PinyinIME; }
namespace modernime {
class LexiconStore;
class LanguageCore {
public:
    LanguageCore(std::filesystem::path database, std::filesystem::path system_dictionary = "/usr/share/libime/sc.dict");
    ~LanguageCore();
    std::vector<Candidate> candidates(std::string_view input, size_t limit) const;
    std::string normalizeForCommit(std::string_view text) const;
    // Reloads the immutable user-lexicon snapshot outside the key-event path.
    void refreshUserLexicon();
    LexiconStore &lexicon();
    const LexiconStore &lexicon() const;
    bool ready() const;
private:
    std::unique_ptr<libime::PinyinIME> ime_;
    std::unique_ptr<LexiconStore> lexicon_;
    std::vector<Lexeme> user_lexicon_;
};
}
