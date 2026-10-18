#include "language-core/language_core.h"
#include "language-core/lexicon_store.h"

#include <libime/core/languagemodel.h>
#include <libime/core/segmentgraph.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/pinyin/pinyinime.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_set>

namespace modernime {
namespace {
bool looksLikeLatinOrNumber(std::string_view value) { return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) { return std::isalnum(character) || character == '.' || character == '_' || character == '-' || character == '/' || character == '@' || character == ':' || character == '+' || character == '#' || character == '>'; }); }
bool isPinyinInput(std::string_view value) { return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) { return (character >= 'a' && character <= 'z') || character == '\''; }); }
std::string normalizedToken(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}
std::string preferenceKey(std::string_view token, std::string_view scope) {
    std::string result(token);
    result.push_back('\0');
    result.append(scope);
    return result;
}
bool hasDeveloperStructure(std::string_view value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isupper(character) || std::isdigit(character) || character == '.' || character == '_' || character == '-' || character == '/' || character == '@' || character == ':' || character == '+' || character == '#' || character == '>';
    });
}
bool isDeveloperWord(std::string_view value) {
    static const std::unordered_set<std::string> words{
        // Shell commands and common aliases that do not form useful complete
        // pinyin readings. Ambiguous commands such as "su" and "du" stay out
        // of this direct set so normal Chinese input remains reachable.
        "alias", "apt", "awk", "bash", "bg", "brew", "cat", "cd", "chmod", "chown", "clear", "cmake", "code", "cp", "curl", "cut", "df", "dig", "docker", "egrep", "env", "exit", "export", "fg", "fgrep", "find", "flatpak", "free", "gcc", "gdb", "git", "grep", "gunzip", "gzip", "head", "history", "htop", "jobs", "journalctl", "kill", "kubectl", "less", "ll", "ln", "ls", "mkdir", "more", "mv", "nano", "netstat", "ninja", "nohup", "npm", "nvim", "pacman", "pgrep", "pip", "pkill", "pnpm", "printenv", "ps", "pwd", "rg", "rm", "rmdir", "rsync", "scp", "screen", "sed", "snap", "sort", "source", "ssh", "stat", "sudo", "systemctl", "tail", "tar", "tee", "tmux", "top", "touch", "tr", "unalias", "uniq", "unzip", "vim", "wc", "wget", "whereis", "which", "xargs", "yarn", "zip", "zsh",
        // Languages, tools, APIs, code vocabulary, and established project
        // names. Case and structural punctuation are handled separately.
        "agent", "angular", "api", "async", "await", "backend", "bool", "branch", "build", "cache", "class", "claude", "client", "clone", "codex", "commit", "config", "const", "context", "css", "data", "database", "debug", "double", "enum", "fetch", "float", "frontend", "from", "function", "github", "gitlab", "html", "http", "https", "import", "int", "java", "javascript", "json", "kotlin", "kubernetes", "lambda", "linux", "main", "make", "merge", "model", "module", "mysql", "node", "null", "package", "postgres", "private", "prompt", "public", "pull", "push", "python", "react", "rebase", "redis", "release", "request", "response", "return", "rust", "self", "server", "sha", "shell", "sql", "state", "status", "std", "string", "struct", "swift", "test", "this", "token", "true", "typescript", "ubuntu", "utf", "variable", "vector", "void", "vue", "xml", "yaml"
    };
    return words.contains(normalizedToken(value));
}
bool isAmbiguousDeveloperWord(std::string_view value) {
    // These tokens are valid pinyin or prefixes of a likely longer pinyin
    // sequence. They remain editable until the user explicitly chooses the
    // raw candidate once; that choice is then learned for future use.
    static const std::unordered_set<std::string> words{"bash", "cat", "main", "make", "more", "sed", "sudo", "tail", "touch", "zip"};
    return words.contains(normalizedToken(value));
}
constexpr size_t kPreferredSentenceCandidates = 12;
constexpr size_t kPreferredPrefixCandidates = 6;
}

LanguageCore::LanguageCore(std::filesystem::path database, std::filesystem::path system_dictionary) : lexicon_(std::make_unique<LexiconStore>(std::move(database))) {
    auto dictionary = std::make_unique<libime::PinyinDictionary>();
    if (std::filesystem::exists(system_dictionary)) dictionary->load(libime::PinyinDictionary::SystemDict, system_dictionary.c_str(), libime::PinyinDictFormat::Binary);
    auto model = std::make_unique<libime::UserLanguageModel>(libime::DefaultLanguageModelResolver::instance().languageModelFileForLanguage("zh_CN"));
    ime_ = std::make_unique<libime::PinyinIME>(std::move(dictionary), std::move(model)); ime_->setNBest(32); ime_->setBeamSize(32); refreshUserLexicon();
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
    input_preferences_.clear();
    for (auto preference : lexicon_->inputPreferences()) {
        preference.token = normalizedToken(preference.token);
        input_preferences_.insert_or_assign(preferenceKey(preference.token, preference.scope), std::move(preference));
    }
}

std::optional<bool> LanguageCore::learnedLiteralPreference(std::string_view input, const InputContextFeatures &features, bool requireRepeatedLiteral) const {
    const auto token = normalizedToken(input);
    uint64_t literal = 0;
    uint64_t chinese = 0;
    uint64_t literalSelections = 0;
    std::optional<bool> lastLiteral;
    const auto include = [&](std::string_view scope, uint64_t weight) {
        const auto found = input_preferences_.find(preferenceKey(token, scope));
        if (found == input_preferences_.end()) return;
        literal += found->second.literal_count * weight;
        chinese += found->second.chinese_count * weight;
        literalSelections = std::max(literalSelections, found->second.literal_count);
        if (!lastLiteral || !scope.empty()) lastLiteral = found->second.last_literal;
    };
    include({}, 1);
    if (!features.scope.empty()) include(features.scope, 3);
    std::optional<bool> learned;
    if (literal != chinese) learned = literal > chinese;
    else if (lastLiteral && literal + chinese > 0) learned = *lastLiteral;
    // A single raw selection is often an accidental Space/Enter while the
    // candidate order is still being tuned. Do not let that one event turn a
    // normal, complete pinyin reading such as "nihao" into permanent English
    // passthrough. Known developer words can still learn after one explicit
    // choice; other lowercase tokens require a repeated literal selection.
    if (learned && *learned && requireRepeatedLiteral && literalSelections < 2) return std::nullopt;
    if (learned) return learned;
    return std::nullopt;
}

bool LanguageCore::prefersLiteral(std::string_view input, const InputContextFeatures &features) const {
    if (features.mode == InputMode::English) return true;
    if (features.mode == InputMode::Chinese || input.empty()) return false;
    if (hasDeveloperStructure(input) || features.url || features.email || features.code_context) return true;
    const auto token = normalizedToken(input);
    if (const auto learned = learnedLiteralPreference(input, features, !isDeveloperWord(token))) return *learned;
    return isDeveloperWord(token);
}

bool LanguageCore::shouldAutoCommitLiteral(std::string_view input, const InputContextFeatures &features, const std::vector<Candidate> &) const {
    if (features.mode != InputMode::Auto || input.empty()) return false;
    if (hasDeveloperStructure(input) || features.url || features.email || features.code_context) return true;
    const bool developerWord = isDeveloperWord(input);
    // A learned raw choice for "n" or "ai" must not commit the beginning of
    // a longer pinyin word before the user can finish typing it.
    if (!developerWord && input.size() <= 2 && isPinyinInput(input)) return false;
    if (const auto learned = learnedLiteralPreference(input, features, !developerWord)) return *learned;
    if (!developerWord) return false;
    return !isAmbiguousDeveloperWord(input);
}

bool LanguageCore::shouldAppendDigit(std::string_view input, const InputContextFeatures &features) const {
    if (features.mode != InputMode::Auto || input.empty()) return false;
    if (features.code_context || hasDeveloperStructure(input)) return true;
    static const std::unordered_set<std::string> versionedPrefixes{
        "css", "ecma", "es", "html", "http", "https", "ipv", "node", "npm", "python", "qt", "sha", "tls", "utf", "vue"
    };
    return versionedPrefixes.contains(normalizedToken(input));
}

std::vector<Candidate> LanguageCore::candidates(std::string_view input, size_t limit, const InputContextFeatures &features) const {
    std::vector<Candidate> result; if (input.empty() || limit == 0) return result; const std::string raw(input); std::unordered_set<std::string> seen;
    const bool includeLiteral = looksLikeLatinOrNumber(raw);
    bool preferLiteral = includeLiteral && prefersLiteral(raw, features);
    const size_t convertedLimit = includeLiteral && !preferLiteral && limit > 1 ? limit - 1 : limit;
    const auto append = [&](const Candidate &candidate) {
        if (result.size() >= convertedLimit || !seen.insert(candidate.text).second) return false;
        result.push_back(candidate);
        return true;
    };
    if (preferLiteral) append({raw, hasDeveloperStructure(raw) ? "代码" : isDeveloperWord(raw) ? "开发词" : "个人英文", false, true, raw.size()});
    for (const auto &entry : user_lexicon_) if (!entry.blocked && entry.reading == raw) append({entry.output, entry.output == raw ? "个人英文" : entry.pinned ? "固定词" : "个人词库", entry.pinned, entry.output == raw, raw.size()});
    if (isPinyinInput(raw) && ime_) {
        libime::PinyinContext context(ime_.get());
        std::vector<Candidate> sentences;
        std::vector<Candidate> prefixes;
        if (context.type(raw)) {
            for (const auto &candidate : context.candidates()) {
                const auto output = candidate.toString();
                size_t consumed = raw.size();
                if (!candidate.sentence().empty()) consumed = candidate.sentence().back()->to()->index();
                if (!output.empty() && consumed > 0 && consumed <= raw.size()) {
                    auto &bucket = consumed == raw.size() ? sentences : prefixes;
                    bucket.push_back({output, "拼音", false, false, consumed});
                }
            }
        }
        if (!preferLiteral && features.mode == InputMode::Auto && sentences.empty() && includeLiteral) {
            preferLiteral = true;
            append({raw, features.terminal ? "终端原文" : "原文", false, true, raw.size()});
        }
        const auto preferredSentences = std::min(kPreferredSentenceCandidates, sentences.size());
        const auto preferredPrefixes = std::min(kPreferredPrefixCandidates, prefixes.size());
        for (size_t index = 0; index < preferredSentences; ++index) append(sentences[index]);
        for (size_t index = 0; index < preferredPrefixes; ++index) append(prefixes[index]);
        for (size_t index = preferredSentences; index < sentences.size(); ++index) append(sentences[index]);
        for (size_t index = preferredPrefixes; index < prefixes.size(); ++index) append(prefixes[index]);
    }
    if (includeLiteral && seen.insert(raw).second && result.size() < limit) result.push_back({raw, preferLiteral ? "代码" : "原文", false, true, raw.size()});
    if (result.empty()) result.push_back({raw, "原文", false, true, raw.size()});
    return result;
}

std::optional<CompositionAdvance> advanceComposition(std::string_view composedText, std::string_view input, const Candidate &candidate) {
    if (candidate.consumed_input == 0 || candidate.consumed_input > input.size()) return std::nullopt;
    CompositionAdvance result;
    result.composed_text = std::string(composedText) + candidate.text;
    result.selected_reading = std::string(input.substr(0, candidate.consumed_input));
    result.remaining_input = input.substr(candidate.consumed_input);
    while (!result.remaining_input.empty() && result.remaining_input.front() == '\'') {
        // Keep explicit syllable separators in the learned full reading even
        // though the next candidate round must not start with one.
        result.selected_reading.push_back('\'');
        result.remaining_input.erase(0, 1);
    }
    result.complete = candidate.literal || result.remaining_input.empty();
    return result;
}

std::string LanguageCore::normalizeForCommit(std::string_view text) const {
    std::string result; result.reserve(text.size()); bool previous_space = false; for (unsigned char character : text) { if (std::isspace(character)) { if (!previous_space && !result.empty()) result.push_back(' '); previous_space = true; } else { result.push_back(static_cast<char>(character)); previous_space = false; } } while (!result.empty() && result.back() == ' ') result.pop_back(); return result;
}
} // namespace modernime
