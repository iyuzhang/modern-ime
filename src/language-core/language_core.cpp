#include "language-core/language_core.h"
#include "language-core/lexicon_store.h"

#include <libime/core/languagemodel.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinime.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_set>

namespace modernime {
namespace {
bool looksLikeLatinOrNumber(std::string_view value) { return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) { return std::isalnum(character) || character == '.' || character == '_' || character == '-' || character == '/' || character == '@'; }); }
bool isPinyinInput(std::string_view value) { return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) { return (character >= 'a' && character <= 'z') || character == '\''; }); }
}

LanguageCore::LanguageCore(std::filesystem::path database, std::filesystem::path system_dictionary) : lexicon_(std::make_unique<LexiconStore>(std::move(database))) {
    auto dictionary = std::make_unique<libime::PinyinDictionary>();
    if (std::filesystem::exists(system_dictionary)) dictionary->load(libime::PinyinDictionary::SystemDict, system_dictionary.c_str(), libime::PinyinDictFormat::Binary);
    auto model = std::make_unique<libime::UserLanguageModel>(libime::DefaultLanguageModelResolver::instance().languageModelFileForLanguage("zh_CN"));
    ime_ = std::make_unique<libime::PinyinIME>(std::move(dictionary), std::move(model)); ime_->setNBest(12); ime_->setBeamSize(32); refreshUserLexicon();
}
LanguageCore::~LanguageCore() = default;
bool LanguageCore::ready() const { return ime_ != nullptr && lexicon_->healthy(); }
LexiconStore &LanguageCore::lexicon() { return *lexicon_; }
const LexiconStore &LanguageCore::lexicon() const { return *lexicon_; }

void LanguageCore::refreshUserLexicon() {
    user_lexicon_ = lexicon_->list();
    std::sort(user_lexicon_.begin(), user_lexicon_.end(), [](const Lexeme &left, const Lexeme &right) {
        if (left.pinned != right.pinned) return left.pinned;
        const bool leftManual = left.kind == "manual";
        const bool rightManual = right.kind == "manual";
        if (leftManual != rightManual) return leftManual;
        if (left.select_count != right.select_count) return left.select_count > right.select_count;
        if (left.last_selected_at != right.last_selected_at) return left.last_selected_at > right.last_selected_at;
        if (left.base_weight != right.base_weight) return left.base_weight > right.base_weight;
        return left.output < right.output;
    });
}

std::vector<Candidate> LanguageCore::candidates(std::string_view input, size_t limit) const {
    std::vector<Candidate> result; if (input.empty() || limit == 0) return result; const std::string raw(input); std::unordered_set<std::string> seen;
    for (const auto &entry : user_lexicon_) if (!entry.blocked && entry.reading == raw && seen.insert(entry.output).second && result.size() < limit) result.push_back({entry.output, entry.pinned ? "固定词" : "个人词库", entry.pinned, false});
    if (isPinyinInput(raw) && ime_) { libime::PinyinContext context(ime_.get()); if (context.type(raw)) for (const auto &candidate : context.candidates()) { const auto output = candidate.toString(); if (!output.empty() && seen.insert(output).second && result.size() < limit) result.push_back({output, "拼音", false, false}); } }
    if (looksLikeLatinOrNumber(raw) && seen.insert(raw).second && result.size() < limit) result.push_back({raw, "原文", false, true});
    if (result.empty()) result.push_back({raw, "原文", false, true});
    return result;
}

std::string LanguageCore::normalizeForCommit(std::string_view text) const {
    std::string result; result.reserve(text.size()); bool previous_space = false; for (unsigned char character : text) { if (std::isspace(character)) { if (!previous_space && !result.empty()) result.push_back(' '); previous_space = true; } else { result.push_back(static_cast<char>(character)); previous_space = false; } } while (!result.empty() && result.back() == ' ') result.pop_back(); return result;
}
} // namespace modernime
