#include "contracts/xdg.h"
#include "contracts/voice_model.h"
#include "voice-worker/sherpa_api.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {
uint32_t readU32(const char *value) {
    return static_cast<uint32_t>(static_cast<unsigned char>(value[0])) |
           (static_cast<uint32_t>(static_cast<unsigned char>(value[1])) << 8U) |
           (static_cast<uint32_t>(static_cast<unsigned char>(value[2])) << 16U) |
           (static_cast<uint32_t>(static_cast<unsigned char>(value[3])) << 24U);
}

uint16_t readU16(const char *value) {
    return static_cast<uint16_t>(static_cast<unsigned char>(value[0])) |
           (static_cast<uint16_t>(static_cast<unsigned char>(value[1])) << 8U);
}

bool readPcm16Wave(const QString &path, int *sampleRate, std::vector<float> *samples) {
    QFile wave(path);
    if (!wave.open(QIODevice::ReadOnly)) return false;
    const auto bytes = wave.readAll();
    if (bytes.size() < 44 || std::memcmp(bytes.constData(), "RIFF", 4) != 0 || std::memcmp(bytes.constData() + 8, "WAVE", 4) != 0) return false;
    int channels = 0;
    int offset = 12;
    QByteArray audio;
    while (offset + 8 <= bytes.size()) {
        const char *chunk = bytes.constData() + offset;
        const auto size = static_cast<int>(readU32(chunk + 4));
        const int payload = offset + 8;
        if (payload + size > bytes.size()) return false;
        if (std::memcmp(chunk, "fmt ", 4) == 0 && size >= 16) {
            if (readU16(chunk + 8) != 1 || readU16(chunk + 8 + 14) != 16) return false;
            channels = readU16(chunk + 8 + 2);
            *sampleRate = static_cast<int>(readU32(chunk + 8 + 4));
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            audio = bytes.mid(payload, size);
        }
        offset = payload + size + (size % 2);
    }
    if (channels != 1 || *sampleRate <= 0 || audio.isEmpty() || audio.size() % 2 != 0) return false;
    samples->reserve(static_cast<size_t>(audio.size() / 2));
    for (int index = 0; index < audio.size(); index += 2) samples->push_back(static_cast<float>(static_cast<int16_t>(readU16(audio.constData() + index))) / 32768.0F);
    return true;
}

QString normalizedForCer(QString value) {
    value = value.normalized(QString::NormalizationForm_KC).toCaseFolded();
    value.remove(QRegularExpression(QStringLiteral("[\\s\\p{P}\\p{S}]+")));
    return value;
}

double characterErrorRate(const QString &expected, const QString &actual) {
    const auto reference = normalizedForCer(expected).toUcs4();
    const auto hypothesis = normalizedForCer(actual).toUcs4();
    if (reference.empty()) return hypothesis.empty() ? 0.0 : 1.0;
    std::vector<size_t> previous(static_cast<size_t>(hypothesis.size()) + 1);
    std::vector<size_t> current(previous.size());
    for (size_t index = 0; index < previous.size(); ++index) previous[index] = index;
    for (qsizetype referenceIndex = 0; referenceIndex < reference.size(); ++referenceIndex) {
        current[0] = static_cast<size_t>(referenceIndex) + 1;
        for (qsizetype hypothesisIndex = 0; hypothesisIndex < hypothesis.size(); ++hypothesisIndex) {
            const auto substitution = previous[static_cast<size_t>(hypothesisIndex)] + (reference.at(referenceIndex) == hypothesis.at(hypothesisIndex) ? 0U : 1U);
            current[static_cast<size_t>(hypothesisIndex) + 1] = std::min({previous[static_cast<size_t>(hypothesisIndex) + 1] + 1, current[static_cast<size_t>(hypothesisIndex)] + 1, substitution});
        }
        previous.swap(current);
    }
    return static_cast<double>(previous.back()) / static_cast<double>(reference.size());
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("wav"), QStringLiteral("PCM16 mono WAV to evaluate"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("model-dir"), QStringLiteral("ASR model directory (defaults to the installed production model)"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("expected"), QStringLiteral("Reference transcript used to calculate CER"), QStringLiteral("text")});
    parser.addOption({QStringLiteral("max-cer"), QStringLiteral("Fail when CER exceeds this ratio"), QStringLiteral("ratio")});
    parser.addOption({QStringLiteral("hotwords"), QStringLiteral("Optional sherpa hotwords file"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("json"), QStringLiteral("Print machine-readable evaluation output")});
    parser.process(app);
    const auto paths = modernime::xdgPaths();
    const auto model = parser.value(QStringLiteral("model-dir")).isEmpty()
                           ? QString::fromStdString((paths.data / "models" / modernime::kDefaultVoiceModel).string())
                           : parser.value(QStringLiteral("model-dir"));
    const auto runtime = QString::fromStdString((paths.data / "runtime" / "sherpa-onnx-v1.13.2" / "lib" / "libsherpa-onnx-c-api.so").string());
    const auto wav = parser.value(QStringLiteral("wav")).isEmpty() ? model + QStringLiteral("/test_wavs/0.wav") : parser.value(QStringLiteral("wav"));
    int sampleRate = 0;
    std::vector<float> samples;
    if (!readPcm16Wave(wav, &sampleRate, &samples)) {
        QTextStream(stderr) << "Cannot read bundled PCM16 smoke-test wave: " << wav << '\n';
        return 2;
    }
    modernime::SherpaApi api;
    QString error;
    if (!api.load(runtime, &error)) {
        QTextStream(stderr) << error << '\n';
        return 3;
    }
    const auto spec = modernime::loadVoiceModelSpec(model, &error);
    if (!spec) {
        QTextStream(stderr) << error << '\n';
        return 4;
    }
    const auto encoder = spec->encoder.toUtf8();
    const auto decoder = spec->decoder.toUtf8();
    const auto joiner = spec->joiner.toUtf8();
    const auto tokens = spec->tokens.toUtf8();
    const auto modelingUnit = spec->modeling_unit.toUtf8();
    const auto bpeVocab = spec->bpe_vocab.toUtf8();
    modernime::SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = 16'000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoder.constData();
    config.model_config.transducer.decoder = decoder.constData();
    config.model_config.transducer.joiner = joiner.constData();
    config.model_config.tokens = tokens.constData();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 2;
    config.model_config.modeling_unit = modelingUnit.constData();
    if (!bpeVocab.isEmpty()) config.model_config.bpe_vocab = bpeVocab.constData();
    config.decoding_method = "modified_beam_search";
    config.max_active_paths = 4;
    const auto hotwords = parser.value(QStringLiteral("hotwords")).toUtf8();
    if (!hotwords.isEmpty() && spec->supports_hotwords) {
        config.hotwords_file = hotwords.constData();
        config.hotwords_score = 2.0F;
    }
    const auto *recognizer = api.createRecognizer(&config);
    if (!recognizer) return 4;
    const auto *stream = api.createStream(recognizer);
    if (!stream) {
        api.destroyRecognizer(recognizer);
        return 5;
    }
    QElapsedTimer decodeTimer;
    decodeTimer.start();
    constexpr size_t kStreamingChunk = 3'200;
    for (size_t offset = 0; offset < samples.size(); offset += kStreamingChunk) {
        const auto count = std::min(kStreamingChunk, samples.size() - offset);
        api.acceptWaveform(stream, sampleRate, samples.data() + offset, static_cast<int32_t>(count));
        while (api.isReady(recognizer, stream)) api.decode(recognizer, stream);
    }
    api.inputFinished(stream);
    while (api.isReady(recognizer, stream)) api.decode(recognizer, stream);
    const auto *result = api.getResult(recognizer, stream);
    QString text = result && result->text ? QString::fromUtf8(result->text).trimmed() : QString{};
    if (result) api.destroyResult(result);
    api.destroyStream(stream);
    api.destroyRecognizer(recognizer);
    const double decodeSeconds = static_cast<double>(decodeTimer.nsecsElapsed()) / 1'000'000'000.0;
    if (text.isEmpty()) return 6;
    const auto punctuationPath = QString::fromStdString((paths.data / "models" / modernime::kPunctuationModel / "model.int8.onnx").string()).toUtf8();
    modernime::SherpaOnnxOfflinePunctuationConfig punctuationConfig{};
    punctuationConfig.model.ct_transformer = punctuationPath.constData();
    punctuationConfig.model.num_threads = 1;
    punctuationConfig.model.provider = "cpu";
    const auto *punctuation = api.createOfflinePunctuation(&punctuationConfig);
    if (!punctuation) return 7;
    QElapsedTimer punctuationTimer;
    punctuationTimer.start();
    const auto rawText = text;
    const auto rawUtf8 = text.toUtf8();
    const auto *punctuated = api.addOfflinePunctuation(punctuation, rawUtf8.constData());
    if (!punctuated) {
        api.destroyOfflinePunctuation(punctuation);
        return 8;
    }
    text = QString::fromUtf8(punctuated).trimmed();
    api.freeOfflinePunctuationText(punctuated);
    api.destroyOfflinePunctuation(punctuation);
    const double punctuationSeconds = static_cast<double>(punctuationTimer.nsecsElapsed()) / 1'000'000'000.0;
    if (!text.contains(QRegularExpression(QStringLiteral("[，。！？,.!?]")))) return 9;
    const auto expected = parser.value(QStringLiteral("expected"));
    const double cer = expected.isEmpty() ? 0.0 : characterErrorRate(expected, rawText);
    bool maxCerOk = true;
    if (parser.isSet(QStringLiteral("max-cer"))) {
        bool parsed = false;
        const double maximum = parser.value(QStringLiteral("max-cer")).toDouble(&parsed);
        if (!parsed || maximum < 0.0) return 10;
        maxCerOk = !expected.isEmpty() && cer <= maximum;
    }
    if (parser.isSet(QStringLiteral("json"))) {
        QJsonObject resultObject;
        resultObject.insert(QStringLiteral("wav"), wav);
        resultObject.insert(QStringLiteral("audio_seconds"), static_cast<double>(samples.size()) / sampleRate);
        resultObject.insert(QStringLiteral("decode_seconds"), decodeSeconds);
        resultObject.insert(QStringLiteral("rtf"), decodeSeconds / (static_cast<double>(samples.size()) / sampleRate));
        resultObject.insert(QStringLiteral("punctuation_seconds"), punctuationSeconds);
        resultObject.insert(QStringLiteral("raw_text"), rawText);
        resultObject.insert(QStringLiteral("text"), text);
        if (!expected.isEmpty()) {
            resultObject.insert(QStringLiteral("expected"), expected);
            resultObject.insert(QStringLiteral("cer"), cer);
        }
        QTextStream(stdout) << QJsonDocument(resultObject).toJson(QJsonDocument::Compact) << '\n';
    } else {
        QTextStream(stdout) << text << '\n';
    }
    if (!maxCerOk) return 11;
    return 0;
}
