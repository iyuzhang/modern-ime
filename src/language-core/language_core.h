#pragma once
#include "contracts/contracts.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>
namespace libime { class PinyinIME; }
namespace modernime {
class LexiconStore;

struct CompositionAdvance {
    std::string composed_text;
    std::string remaining_input;
    std::string selected_reading;
    bool complete = false;
};

struct InputContextFeatures {
    InputMode mode = InputMode::Auto;
    std::string scope;
    bool terminal = false;
    bool url = false;
    bool email = false;
    bool code_context = false;
};

std::optional<CompositionAdvance> advanceComposition(std::string_view composedText, std::string_view input, const Candidate &candidate);

class LanguageCore {
public:
    LanguageCore(std::filesystem::path database, std::filesystem::path system_dictionary = "/usr/share/libime/sc.dict");
    ~LanguageCore();
    std::vector<Candidate> candidates(std::string_view input, size_t limit, const InputContextFeatures &features = {}) const;
    bool prefersLiteral(std::string_view input, const InputContextFeatures &features = {}) const;
    bool shouldAutoCommitLiteral(std::string_view input, const InputContextFeatures &features, const std::vector<Candidate> &candidates) const;
    bool shouldAppendDigit(std::string_view input, const InputContextFeatures &features = {}) const;
    std::string normalizeForCommit(std::string_view text) const;
    // Reloads the immutable user-lexicon snapshot outside the key-event path.
    void refreshUserLexicon();
    LexiconStore &lexicon();
    const LexiconStore &lexicon() const;
    bool ready() const;
private:
    std::optional<bool> learnedLiteralPreference(std::string_view input, const InputContextFeatures &features, bool requireRepeatedLiteral = false) const;
    std::unique_ptr<libime::PinyinIME> ime_;
    std::unique_ptr<LexiconStore> lexicon_;
    std::vector<Lexeme> user_lexicon_;
    std::unordered_map<std::string, InputPreference> input_preferences_;
};
}
