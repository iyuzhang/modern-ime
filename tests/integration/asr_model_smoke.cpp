#include "contracts/xdg.h"
#include "voice-worker/sherpa_api.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cstring>
#include <vector>

namespace {
constexpr auto kModelId = "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20";

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
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto paths = modernime::xdgPaths();
    const auto model = QString::fromStdString((paths.data / "models" / kModelId).string());
    const auto runtime = QString::fromStdString((paths.data / "runtime" / "sherpa-onnx-v1.13.2" / "lib" / "libsherpa-onnx-c-api.so").string());
    const auto wav = model + QStringLiteral("/test_wavs/0.wav");
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
    const auto encoder = (model + QStringLiteral("/encoder-epoch-99-avg-1.int8.onnx")).toUtf8();
    const auto decoder = (model + QStringLiteral("/decoder-epoch-99-avg-1.onnx")).toUtf8();
    const auto joiner = (model + QStringLiteral("/joiner-epoch-99-avg-1.int8.onnx")).toUtf8();
    const auto tokens = (model + QStringLiteral("/tokens.txt")).toUtf8();
    modernime::SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = 16'000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoder.constData();
    config.model_config.transducer.decoder = decoder.constData();
    config.model_config.transducer.joiner = joiner.constData();
    config.model_config.tokens = tokens.constData();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = 2;
    config.model_config.modeling_unit = "cjkchar";
    config.decoding_method = "greedy_search";
    const auto *recognizer = api.createRecognizer(&config);
    if (!recognizer) return 4;
    const auto *stream = api.createStream(recognizer);
    if (!stream) {
        api.destroyRecognizer(recognizer);
        return 5;
    }
    api.acceptWaveform(stream, sampleRate, samples.data(), static_cast<int32_t>(samples.size()));
    api.inputFinished(stream);
    while (api.isReady(recognizer, stream)) api.decode(recognizer, stream);
    const auto *result = api.getResult(recognizer, stream);
    const QString text = result && result->text ? QString::fromUtf8(result->text).trimmed() : QString{};
    if (result) api.destroyResult(result);
    api.destroyStream(stream);
    api.destroyRecognizer(recognizer);
    if (text.isEmpty()) return 6;
    QTextStream(stdout) << text << '\n';
    return 0;
}
