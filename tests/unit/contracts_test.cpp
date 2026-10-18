#include "contracts/contracts.h"
#include "contracts/audio_health.h"
#include "contracts/voice_model.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <catch2/catch_test_macros.hpp>
TEST_CASE("candidate snapshot serialization is versioned and round trips") { modernime::CandidateSnapshot input; input.producer_id = "engine-instance-a"; input.sequence = 42; input.preedit = "nihao"; input.candidates = {{"你好", "拼音", false, false}, {"nihao", "原文", false, true}}; input.visible = true; const auto parsed = modernime::parseCandidateSnapshot(modernime::serialize(input)); REQUIRE(parsed); REQUIRE(parsed->producer_id == "engine-instance-a"); REQUIRE(parsed->sequence == 42); REQUIRE(parsed->candidates.at(0).text == "你好"); REQUIRE(parsed->visible); }
TEST_CASE("config validation clamps unsafe visual values") { auto parsed = modernime::parseConfigSnapshot("{\"protocol_version\":1,\"candidate_count\":99,\"font_size\":1,\"corner_radius\":90,\"opacity\":1}"); REQUIRE(parsed); REQUIRE(parsed->candidate_count == 9); REQUIRE(parsed->font_size == 12); REQUIRE(parsed->corner_radius == 32); REQUIRE(parsed->opacity == 65); }
TEST_CASE("config migrations require explicit microphone selection and retain the model default") { auto parsed = modernime::parseConfigSnapshot("{\"protocol_version\":1}"); REQUIRE(parsed); REQUIRE(parsed->microphone.empty()); REQUIRE(parsed->model_id == "sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30"); }
TEST_CASE("input strategy defaults to automatic mixed input and round trips") {
    modernime::ConfigSnapshot input;
    REQUIRE(input.input_mode == modernime::InputMode::Auto);
    REQUIRE(input.control_mode_toggle);
    REQUIRE(input.mode_toggle_hotkey == "Control+Alt+space");
    input.input_mode = modernime::InputMode::Chinese;
    input.control_mode_toggle = false;
    input.mode_toggle_hotkey = "Control+Shift+m";
    auto parsed = modernime::parseConfigSnapshot(modernime::serialize(input));
    REQUIRE(parsed);
    REQUIRE(parsed->input_mode == modernime::InputMode::Chinese);
    REQUIRE_FALSE(parsed->control_mode_toggle);
    REQUIRE(parsed->mode_toggle_hotkey == "Control+Shift+m");
    parsed = modernime::parseConfigSnapshot("{\"protocol_version\":1,\"chinese_mode\":false}");
    REQUIRE(parsed);
    REQUIRE(parsed->input_mode == modernime::InputMode::English);
    parsed = modernime::parseConfigSnapshot("{\"protocol_version\":1,\"shift_mode_toggle\":false}");
    REQUIRE(parsed);
    REQUIRE_FALSE(parsed->control_mode_toggle);
    REQUIRE(parsed->mode_toggle_hotkey == "Control+Alt+space");
}
TEST_CASE("candidate themes round trip and unknown themes retain the current default") {
    const std::vector<modernime::CandidateTheme> themes{
        modernime::CandidateTheme::Starlight,
        modernime::CandidateTheme::Sakura,
        modernime::CandidateTheme::Matcha,
        modernime::CandidateTheme::Lavender,
        modernime::CandidateTheme::PeachSoda,
        modernime::CandidateTheme::MoonRabbit,
        modernime::CandidateTheme::MintCat,
        modernime::CandidateTheme::BerryBear,
    };
    for (const auto theme : themes) {
        modernime::ConfigSnapshot input;
        input.theme = theme;
        const auto saved = modernime::parseConfigSnapshot(modernime::serialize(input));
        REQUIRE(saved);
        REQUIRE(saved->theme == theme);
    }
    const auto unknown = modernime::parseConfigSnapshot("{\"protocol_version\":1,\"theme\":\"not-a-theme\"}");
    REQUIRE(unknown);
    REQUIRE(unknown->theme == modernime::CandidateTheme::Midnight);
}
TEST_CASE("hotkey configuration round trips with portable Fcitx names") { modernime::ConfigSnapshot input; input.voice_hotkey = "Control+F8"; input.cancel_hotkey = "Control+Escape"; input.next_page_hotkey = "bracketright"; const auto parsed = modernime::parseConfigSnapshot(modernime::serialize(input)); REQUIRE(parsed); REQUIRE(parsed->voice_hotkey == "Control+F8"); REQUIRE(parsed->cancel_hotkey == "Control+Escape"); REQUIRE(parsed->next_page_hotkey == "bracketright"); }
TEST_CASE("voice error round trips with a stable error code") { modernime::VoiceEvent input; input.session_id = "voice-1"; input.state = modernime::VoiceState::Error; input.error = modernime::ErrorCode::ModelMissing; const auto parsed = modernime::parseVoiceEvent(modernime::serialize(input)); REQUIRE(parsed); REQUIRE(parsed->error == modernime::ErrorCode::ModelMissing); }
TEST_CASE("recognizer empty output is distinct from missing speech") { modernime::VoiceEvent input; input.session_id = "voice-rare-word"; input.state = modernime::VoiceState::Error; input.error = modernime::ErrorCode::RecognizerNoResult; const auto parsed = modernime::parseVoiceEvent(modernime::serialize(input)); REQUIRE(parsed); REQUIRE(parsed->error == modernime::ErrorCode::RecognizerNoResult); }
TEST_CASE("digitally silent audio retries once before reporting a broken stream") {
    modernime::SilentCaptureMonitor monitor;
    REQUIRE(monitor.observe(7'999, 0.0F) == modernime::SilentCaptureAction::None);
    REQUIRE(monitor.observe(8'000, 0.001F) == modernime::SilentCaptureAction::None);
    REQUIRE(monitor.observe(8'000, 0.0F) == modernime::SilentCaptureAction::Reconnect);
    REQUIRE(monitor.recoveryAttempted());
    REQUIRE(monitor.observe(16'000, 0.0F) == modernime::SilentCaptureAction::Fail);
    monitor.reset();
    REQUIRE_FALSE(monitor.recoveryAttempted());
    REQUIRE(modernime::isDigitallySilentCapture(1'600, 0.0F));
    REQUIRE_FALSE(modernime::isDigitallySilentCapture(1'599, 0.0F));
}
TEST_CASE("silent audio device error round trips with a stable error code") {
    modernime::VoiceEvent input;
    input.session_id = "voice-silent-device";
    input.state = modernime::VoiceState::Error;
    input.error = modernime::ErrorCode::AudioStreamSilent;
    const auto parsed = modernime::parseVoiceEvent(modernime::serialize(input));
    REQUIRE(parsed);
    REQUIRE(parsed->error == modernime::ErrorCode::AudioStreamSilent);
}
TEST_CASE("final voice transcripts always have a clean sentence boundary") {
    REQUIRE(modernime::finalizeVoiceTranscript("  今天天气不错  ") == "今天天气不错。");
    REQUIRE(modernime::finalizeVoiceTranscript("你好 ，世界！") == "你好，世界！");
    REQUIRE(modernime::finalizeVoiceTranscript("这是一句未完成的话，") == "这是一句未完成的话。");
    REQUIRE(modernime::finalizeVoiceTranscript("hello world") == "hello world.");
    REQUIRE(modernime::finalizeVoiceTranscript("已经有问号？") == "已经有问号？");
    REQUIRE(modernime::finalizeVoiceTranscript("   ").empty());
}
TEST_CASE("live voice previews keep internal punctuation without an unstable sentence ending") {
    REQUIRE(modernime::previewVoiceTranscript("今天天气不错，我们下午三点开会。") == "今天天气不错，我们下午三点开会");
    REQUIRE(modernime::previewVoiceTranscript("你好 ，世界！") == "你好，世界");
    REQUIRE(modernime::previewVoiceTranscript("hello, world.") == "hello, world");
    REQUIRE(modernime::previewVoiceTranscript("   ").empty());
}
TEST_CASE("unknown protocol is rejected") { REQUIRE_FALSE(modernime::parseVoiceEvent("{\"protocol_version\":7}")); }
TEST_CASE("voice model manifests resolve only local declared files") {
    REQUIRE(modernime::isValidVoiceModelId(QStringLiteral("sherpa-onnx-model_1.0")));
    REQUIRE_FALSE(modernime::isValidVoiceModelId(QStringLiteral("../outside")));
    QTemporaryDir directory;
    REQUIRE(directory.isValid());
    for (const auto &name : {"encoder.onnx", "decoder.onnx", "joiner.onnx", "tokens.txt"}) {
        QFile file(QDir(directory.path()).filePath(QString::fromLatin1(name)));
        REQUIRE(file.open(QIODevice::WriteOnly));
        REQUIRE(file.write("fixture") == 7);
    }
    QFile manifest(QDir(directory.path()).filePath(QStringLiteral("model.json")));
    REQUIRE(manifest.open(QIODevice::WriteOnly));
    const QByteArray valid = R"({"format":"modern-ime-model","id":"test-zh","kind":"transducer","modeling_unit":"cjkchar","supports_hotwords":true,"files":{"encoder":"encoder.onnx","decoder":"decoder.onnx","joiner":"joiner.onnx","tokens":"tokens.txt"}})";
    REQUIRE(manifest.write(valid) == valid.size());
    manifest.close();
    QString error;
    const auto parsed = modernime::loadVoiceModelSpec(directory.path(), &error);
    REQUIRE(parsed);
    REQUIRE(parsed->supports_hotwords);
    REQUIRE(parsed->modeling_unit == QStringLiteral("cjkchar"));
    REQUIRE(parsed->license == QStringLiteral("UNKNOWN"));

    REQUIRE(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray escaping = R"({"format":"modern-ime-model","id":"bad","kind":"transducer","files":{"encoder":"../encoder.onnx","decoder":"decoder.onnx","joiner":"joiner.onnx","tokens":"tokens.txt"}})";
    REQUIRE(manifest.write(escaping) == escaping.size());
    manifest.close();
    REQUIRE_FALSE(modernime::loadVoiceModelSpec(directory.path(), &error));
}
TEST_CASE("voice hotwords prefer curated and repeated multi-character lexemes") {
    modernime::Lexeme learned;
    learned.output = "的";
    learned.kind = "learned";
    REQUIRE_FALSE(modernime::voiceHotwordForLexeme(learned));
    learned.output = "你好";
    learned.select_count = 1;
    REQUIRE_FALSE(modernime::voiceHotwordForLexeme(learned));
    learned.select_count = 2;
    const auto repeated = modernime::voiceHotwordForLexeme(learned);
    REQUIRE(repeated);
    REQUIRE(repeated->score == 0.0F);
    learned.output = "巴拉拉";
    learned.select_count = 0;
    REQUIRE(modernime::voiceHotwordForLexeme(learned));
    modernime::Lexeme pinned;
    pinned.output = "周望君";
    pinned.kind = "manual";
    pinned.pinned = true;
    const auto curated = modernime::voiceHotwordForLexeme(pinned);
    REQUIRE(curated);
    REQUIRE(curated->score == 3.0F);
    pinned.output = "bad:100";
    REQUIRE_FALSE(modernime::voiceHotwordForLexeme(pinned));
}
