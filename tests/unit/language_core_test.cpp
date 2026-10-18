#include "language-core/language_core.h"
#include "language-core/lexicon_store.h"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <filesystem>
TEST_CASE("language core keeps raw English candidate") { const auto database = std::filesystem::temp_directory_path() / "modern-ime-language-test.db"; std::filesystem::remove(database); modernime::LanguageCore core(database); const auto candidates = core.candidates("GitHub", 5); REQUIRE_FALSE(candidates.empty()); REQUIRE(candidates.front().text == "GitHub"); std::filesystem::remove(database); }
TEST_CASE("language core keeps literal English tokens with separators") { const auto database = std::filesystem::temp_directory_path() / "modern-ime-language-separator-test.db"; std::filesystem::remove(database); modernime::LanguageCore core(database); const auto candidates = core.candidates("music_version", 5); REQUIRE_FALSE(candidates.empty()); REQUIRE(candidates.front().text == "music_version"); std::filesystem::remove(database); }
TEST_CASE("language core generates Chinese candidates") { const auto database = std::filesystem::temp_directory_path() / "modern-ime-pinyin-test.db"; std::filesystem::remove(database); modernime::LanguageCore core(database); const auto candidates = core.candidates("nihao", 9); REQUIRE_FALSE(candidates.empty()); bool hasChinese = false; for (const auto &candidate : candidates) if (candidate.text.find("你好") != std::string::npos) hasChinese = true; REQUIRE(hasChinese); std::filesystem::remove(database); }

TEST_CASE("automatic mixed input promotes developer words without hiding Chinese") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-developer-word-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    const auto candidates = core.candidates("main", 9);
    REQUIRE_FALSE(candidates.empty());
    REQUIRE(candidates.front().text == "main");
    REQUIRE(candidates.front().literal);
    REQUIRE(std::any_of(candidates.begin(), candidates.end(), [](const modernime::Candidate &candidate) { return !candidate.literal; }));
    modernime::InputContextFeatures chinese;
    chinese.mode = modernime::InputMode::Chinese;
    const auto chineseCandidates = core.candidates("main", 9, chinese);
    REQUIRE_FALSE(chineseCandidates.empty());
    REQUIRE_FALSE(chineseCandidates.front().literal);
    REQUIRE(chineseCandidates.back().literal);
    std::filesystem::remove(database);
}

TEST_CASE("automatic mixed input recognizes common shell commands and aliases") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-shell-command-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    modernime::InputContextFeatures features;
    for (const auto token : {"ls", "ll", "cd", "pwd", "rg", "rm", "grep", "docker", "kubectl", "systemctl"}) {
        const auto candidates = core.candidates(token, 9, features);
        REQUIRE_FALSE(candidates.empty());
        REQUIRE(candidates.front().text == token);
        REQUIRE(candidates.front().literal);
        REQUIRE(core.shouldAutoCommitLiteral(token, features, candidates));
    }
    // These are valid and common pinyin syllables, so command recognition
    // must not make their Chinese candidates unreachable.
    for (const auto token : {"su", "du", "man", "ping"}) {
        const auto candidates = core.candidates(token, 9, features);
        REQUIRE_FALSE(candidates.front().literal);
        REQUIRE_FALSE(core.shouldAutoCommitLiteral(token, features, candidates));
    }
    const auto bash = core.candidates("bash", 9, features);
    REQUIRE_FALSE(core.shouldAutoCommitLiteral("bash", features, bash));
    std::filesystem::remove(database);
}

TEST_CASE("automatic mixed input preserves case and code structure") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-code-token-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    for (const auto token : {"React", "useEffect", "std::vector", "sha256", "user_id"}) {
        const auto candidates = core.candidates(token, 7);
        REQUIRE_FALSE(candidates.empty());
        REQUIRE(candidates.front().text == token);
        REQUIRE(candidates.front().literal);
    }
    std::filesystem::remove(database);
}

TEST_CASE("mixed input learns literal versus Chinese choice with application scope") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-input-choice-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    REQUIRE(core.lexicon().recordInputPreference("main", "writer", false));
    REQUIRE(core.lexicon().recordInputPreference("main", "code", true));
    core.refreshUserLexicon();
    modernime::InputContextFeatures writer;
    writer.scope = "writer";
    modernime::InputContextFeatures code;
    code.scope = "code";
    REQUIRE_FALSE(core.candidates("main", 9, writer).front().literal);
    REQUIRE(core.candidates("main", 9, code).front().literal);
    REQUIRE(core.lexicon().recordInputPreference("main", "code", false));
    core.refreshUserLexicon();
    REQUIRE_FALSE(core.candidates("main", 9, code).front().literal);
    std::filesystem::remove(database);
}

TEST_CASE("a code delimiter in surrounding context promotes the literal token") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-code-context-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    modernime::InputContextFeatures features;
    features.code_context = true;
    const auto candidates = core.candidates("shi", 7, features);
    REQUIRE_FALSE(candidates.empty());
    REQUIRE(candidates.front().text == "shi");
    REQUIRE(candidates.front().literal);
    std::filesystem::remove(database);
}

TEST_CASE("only unambiguous developer prefixes absorb candidate-selection digits") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-digit-boundary-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    REQUIRE(core.shouldAppendDigit("sha"));
    REQUIRE(core.shouldAppendDigit("utf"));
    REQUIRE_FALSE(core.shouldAppendDigit("main"));
    REQUIRE_FALSE(core.shouldAppendDigit("shi"));
    modernime::InputContextFeatures code;
    code.code_context = true;
    REQUIRE(core.shouldAppendDigit("a", code));
    std::filesystem::remove(database);
}

TEST_CASE("high confidence literals auto commit while ambiguous pinyin stays editable") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-auto-literal-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    modernime::InputContextFeatures features;
    const auto git = core.candidates("git", 9, features);
    const auto main = core.candidates("main", 9, features);
    const auto nihao = core.candidates("nihao", 9, features);
    const auto react = core.candidates("React", 9, features);
    REQUIRE(core.shouldAutoCommitLiteral("git", features, git));
    REQUIRE_FALSE(core.shouldAutoCommitLiteral("main", features, main));
    REQUIRE_FALSE(core.shouldAutoCommitLiteral("nihao", features, nihao));
    REQUIRE(core.shouldAutoCommitLiteral("React", features, react));
    REQUIRE(core.knownDeveloperWord("gitlab"));
    REQUIRE_FALSE(core.knownDeveloperWord("agentnihao"));
    for (const auto token : {"n", "ai"}) {
        REQUIRE(core.lexicon().recordInputPreference(token, {}, true));
        REQUIRE(core.lexicon().recordInputPreference(token, {}, true));
    }
    core.refreshUserLexicon();
    REQUIRE_FALSE(core.shouldAutoCommitLiteral("n", features, core.candidates("n", 9, features)));
    REQUIRE_FALSE(core.shouldAutoCommitLiteral("ai", features, core.candidates("ai", 9, features)));
    REQUIRE(core.lexicon().recordInputPreference("main", {}, true));
    core.refreshUserLexicon();
    const auto learnedMain = core.candidates("main", 9, features);
    REQUIRE(core.shouldAutoCommitLiteral("main", features, learnedMain));
    modernime::InputContextFeatures terminal;
    terminal.scope = "wezterm-gui";
    terminal.terminal = true;
    REQUIRE(core.lexicon().recordInputPreference("nihao", terminal.scope, true));
    core.refreshUserLexicon();
    const auto accidentallyRawNihao = core.candidates("nihao", 9, terminal);
    REQUIRE_FALSE(accidentallyRawNihao.front().literal);
    REQUIRE_FALSE(core.shouldAutoCommitLiteral("nihao", terminal, accidentallyRawNihao));
    REQUIRE(core.lexicon().recordInputPreference("nihao", terminal.scope, true));
    core.refreshUserLexicon();
    const auto repeatedlyRawNihao = core.candidates("nihao", 9, terminal);
    REQUIRE(repeatedlyRawNihao.front().literal);
    REQUIRE(core.shouldAutoCommitLiteral("nihao", terminal, repeatedlyRawNihao));
    features.mode = modernime::InputMode::Chinese;
    REQUIRE_FALSE(core.shouldAutoCommitLiteral("git", features, git));
    std::filesystem::remove(database);
}

TEST_CASE("pinyin prefix candidates preserve the unconsumed sentence suffix") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-pinyin-prefix-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    const auto candidates = core.candidates("miaokongchumoban", 35);
    const auto prefix = std::find_if(candidates.begin(), candidates.end(), [](const modernime::Candidate &candidate) {
        return candidate.text == "妙";
    });
    REQUIRE(prefix != candidates.end());
    REQUIRE(prefix->consumed_input == 4);
    const auto first = modernime::advanceComposition({}, "miaokongchumoban", *prefix);
    REQUIRE(first);
    REQUIRE(first->composed_text == "妙");
    REQUIRE(first->selected_reading == "miao");
    REQUIRE(first->remaining_input == "kongchumoban");
    REQUIRE_FALSE(first->complete);

    const auto remaining = core.candidates(first->remaining_input, 35);
    const auto touchpad = std::find_if(remaining.begin(), remaining.end(), [](const modernime::Candidate &candidate) {
        return candidate.text == "控触摸板";
    });
    REQUIRE(touchpad != remaining.end());
    const auto completed = modernime::advanceComposition(first->composed_text, first->remaining_input, *touchpad);
    REQUIRE(completed);
    REQUIRE(completed->complete);
    REQUIRE(completed->composed_text == "妙控触摸板");
    REQUIRE(completed->selected_reading == "kongchumoban");
    REQUIRE(completed->remaining_input.empty());

    const auto direct = std::find_if(candidates.begin(), candidates.end(), [](const modernime::Candidate &candidate) {
        return candidate.text == "妙控触摸板";
    });
    REQUIRE(direct != candidates.end());
    std::filesystem::remove(database);
}

TEST_CASE("learned composed sentence becomes the first candidate after refresh") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-composed-sentence-learning-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    REQUIRE(core.lexicon().recordSelection("miaokongchumoban", "妙控触摸板"));
    core.refreshUserLexicon();

    const auto candidates = core.candidates("miaokongchumoban", 35);
    REQUIRE_FALSE(candidates.empty());
    REQUIRE(candidates.front().text == "妙控触摸板");
    REQUIRE(candidates.front().annotation == "个人词库");
    REQUIRE(candidates.front().consumed_input == std::string("miaokongchumoban").size());
    std::filesystem::remove(database);
}

TEST_CASE("composition learning preserves explicit pinyin separators") {
    const modernime::Candidate prefix{"妙", "拼音", false, false, 4};
    const auto advance = modernime::advanceComposition({}, "miao'kong", prefix);
    REQUIRE(advance);
    REQUIRE(advance->selected_reading == "miao'");
    REQUIRE(advance->remaining_input == "kong");
}

TEST_CASE("pinyin conversion keeps raw input as the final fallback candidate") {
    const auto database = std::filesystem::temp_directory_path() / "modern-ime-pinyin-raw-test.db";
    std::filesystem::remove(database);
    modernime::LanguageCore core(database);
    const auto candidates = core.candidates("nihao", 9);
    REQUIRE(candidates.size() == 9);
    REQUIRE(candidates.back().text == "nihao");
    REQUIRE(candidates.back().literal);
    std::filesystem::remove(database);
}
