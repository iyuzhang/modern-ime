// SPDX-License-Identifier: Apache-2.0
// Minimal ABI declarations derived from sherpa-onnx c-api.h v1.13.2.
// The full upstream API is available at https://github.com/k2-fsa/sherpa-onnx.
#pragma once

#include <QLibrary>
#include <QString>

#include <cstdint>

namespace modernime {

struct SherpaOnnxOnlineTransducerModelConfig {
    const char *encoder;
    const char *decoder;
    const char *joiner;
};
struct SherpaOnnxOnlineParaformerModelConfig { const char *encoder; const char *decoder; };
struct SherpaOnnxOnlineZipformer2CtcModelConfig { const char *model; };
struct SherpaOnnxOnlineNemoCtcModelConfig { const char *model; };
struct SherpaOnnxOnlineToneCtcModelConfig { const char *model; };
struct SherpaOnnxOnlineModelConfig {
    SherpaOnnxOnlineTransducerModelConfig transducer;
    SherpaOnnxOnlineParaformerModelConfig paraformer;
    SherpaOnnxOnlineZipformer2CtcModelConfig zipformer2_ctc;
    const char *tokens;
    int32_t num_threads;
    const char *provider;
    int32_t debug;
    const char *model_type;
    const char *modeling_unit;
    const char *bpe_vocab;
    const char *tokens_buf;
    int32_t tokens_buf_size;
    SherpaOnnxOnlineNemoCtcModelConfig nemo_ctc;
    SherpaOnnxOnlineToneCtcModelConfig t_one_ctc;
};
struct SherpaOnnxFeatureConfig { int32_t sample_rate; int32_t feature_dim; };
struct SherpaOnnxOnlineCtcFstDecoderConfig { const char *graph; int32_t max_active; };
struct SherpaOnnxHomophoneReplacerConfig { const char *dict_dir; const char *lexicon; const char *rule_fsts; };
struct SherpaOnnxOnlineRecognizerConfig {
    SherpaOnnxFeatureConfig feat_config;
    SherpaOnnxOnlineModelConfig model_config;
    const char *decoding_method;
    int32_t max_active_paths;
    int32_t enable_endpoint;
    float rule1_min_trailing_silence;
    float rule2_min_trailing_silence;
    float rule3_min_utterance_length;
    const char *hotwords_file;
    float hotwords_score;
    SherpaOnnxOnlineCtcFstDecoderConfig ctc_fst_decoder_config;
    const char *rule_fsts;
    const char *rule_fars;
    float blank_penalty;
    const char *hotwords_buf;
    int32_t hotwords_buf_size;
    SherpaOnnxHomophoneReplacerConfig hr;
};
struct SherpaOnnxOnlineRecognizerResult {
    const char *text;
    const char *tokens;
    const char *const *tokens_arr;
    float *timestamps;
    int32_t count;
    const char *json;
};
struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineStream;

class SherpaApi final {
public:
    using CreateRecognizer = const SherpaOnnxOnlineRecognizer *(*)(const SherpaOnnxOnlineRecognizerConfig *);
    using DestroyRecognizer = void (*)(const SherpaOnnxOnlineRecognizer *);
    using CreateStream = const SherpaOnnxOnlineStream *(*)(const SherpaOnnxOnlineRecognizer *);
    using DestroyStream = void (*)(const SherpaOnnxOnlineStream *);
    using AcceptWaveform = void (*)(const SherpaOnnxOnlineStream *, int32_t, const float *, int32_t);
    using IsReady = int32_t (*)(const SherpaOnnxOnlineRecognizer *, const SherpaOnnxOnlineStream *);
    using Decode = void (*)(const SherpaOnnxOnlineRecognizer *, const SherpaOnnxOnlineStream *);
    using GetResult = const SherpaOnnxOnlineRecognizerResult *(*)(const SherpaOnnxOnlineRecognizer *, const SherpaOnnxOnlineStream *);
    using DestroyResult = void (*)(const SherpaOnnxOnlineRecognizerResult *);
    using InputFinished = void (*)(const SherpaOnnxOnlineStream *);

    bool load(const QString &libraryPath, QString *error);
    bool ready() const;

    CreateRecognizer createRecognizer = nullptr;
    DestroyRecognizer destroyRecognizer = nullptr;
    CreateStream createStream = nullptr;
    DestroyStream destroyStream = nullptr;
    AcceptWaveform acceptWaveform = nullptr;
    IsReady isReady = nullptr;
    Decode decode = nullptr;
    GetResult getResult = nullptr;
    DestroyResult destroyResult = nullptr;
    InputFinished inputFinished = nullptr;

private:
    QLibrary library_;
};

} // namespace modernime
