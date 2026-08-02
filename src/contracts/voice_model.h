#pragma once

#include "contracts/contracts.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace modernime {

inline constexpr auto kDefaultVoiceModel = "sherpa-onnx-streaming-zipformer-zh-int8-2025-06-30";
inline constexpr auto kLegacyBilingualVoiceModel = "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20";
inline constexpr auto kPunctuationModel = "sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12-int8";

struct VoiceModelSpec {
    QString id;
    QString directory;
    QString encoder;
    QString decoder;
    QString joiner;
    QString tokens;
    QString modeling_unit;
    QString bpe_vocab;
    QString source;
    QString license;
    bool supports_hotwords = false;
};

struct VoiceHotword {
    QString phrase;
    float score = 0.0F; // Zero uses the recognizer's global score.
};

// Loads a verified local model description and resolves every referenced file
// below directory. Legacy installations are recognized only by their exact,
// previously shipped model id; new models must declare their layout in
// model.json so adding a model never requires another worker code change.
std::optional<VoiceModelSpec> loadVoiceModelSpec(const QString &directory, QString *error = nullptr);
bool isValidVoiceModelId(QStringView modelId);
std::optional<VoiceHotword> voiceHotwordForLexeme(const Lexeme &lexeme);

} // namespace modernime
