#include "voice-worker/voice_worker.h"

#include "contracts/xdg.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/hook.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace modernime {
namespace {
constexpr auto kDefaultVoiceModel = "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20";
// HFP microphones commonly deliver a much quieter signal than built-in mics.
constexpr float kBluetoothInputGain = 4.0F;
constexpr float kSpeechLevel = 0.003F;

const pw_stream_events kStreamEvents = [] {
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = VoiceWorker::process;
    return events;
}();

QString modelDirectory(const QString &modelId) {
    const auto paths = xdgPaths();
    return QString::fromStdString((paths.data / "models" / modelId.toStdString()).string());
}
} // namespace

VoiceWorker::VoiceWorker(QString source, QString modelId, QObject *parent)
    : QObject(parent), source_(std::move(source)), model_id_(modelId.isEmpty() ? QString::fromLatin1(kDefaultVoiceModel) : std::move(modelId)) {
    input_gain_ = source_.startsWith(QStringLiteral("bluez_input.")) ? kBluetoothInputGain : 1.0F;
    meter_timer_.setInterval(200);
    connect(&meter_timer_, &QTimer::timeout, this, &VoiceWorker::publishMeter);
    pw_init(nullptr, nullptr);
}

VoiceWorker::~VoiceWorker() {
    closeStream();
    closeRecognizer();
    pw_deinit();
}

bool VoiceWorker::openStream() {
    if (stream_) return true;
    loop_ = pw_thread_loop_new("modern-ime-audio", nullptr);
    if (!loop_) return false;
    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (!context_) {
        closeStream();
        return false;
    }
    core_ = pw_context_connect(context_, nullptr, 0);
    if (!core_) {
        closeStream();
        return false;
    }
    stream_ = pw_stream_new(core_, "Modern IME Voice Capture",
                            pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
                                              PW_KEY_MEDIA_ROLE, "Communication", PW_KEY_TARGET_OBJECT,
                                              source_.toUtf8().constData(), nullptr));
    if (!stream_) {
        closeStream();
        return false;
    }
    stream_listener_opaque_ = new spa_hook{};
    pw_stream_add_listener(stream_, stream_listener_opaque_, &kStreamEvents, this);

    spa_audio_info_raw format{};
    format.format = SPA_AUDIO_FORMAT_F32;
    format.rate = 16'000;
    format.channels = 1;
    format.position[0] = SPA_AUDIO_CHANNEL_MONO;
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format)};
    if (pw_stream_connect(stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
                          static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                          params, 1) < 0) {
        closeStream();
        return false;
    }
    pw_thread_loop_start(loop_);
    return true;
}

void VoiceWorker::closeStream() {
    meter_timer_.stop();
    if (loop_) pw_thread_loop_stop(loop_);
    if (stream_listener_opaque_) {
        spa_hook_remove(stream_listener_opaque_);
        delete stream_listener_opaque_;
        stream_listener_opaque_ = nullptr;
    }
    if (stream_) {
        pw_stream_destroy(stream_);
        stream_ = nullptr;
    }
    if (core_) {
        pw_core_disconnect(core_);
        core_ = nullptr;
    }
    if (context_) {
        pw_context_destroy(context_);
        context_ = nullptr;
    }
    if (loop_) {
        pw_thread_loop_destroy(loop_);
        loop_ = nullptr;
    }
    listening_ = false;
}

bool VoiceWorker::openRecognizer(ErrorCode *error) {
    if (recognizer_) return true;
    const auto paths = xdgPaths();
    const auto runtime = QString::fromStdString((paths.data / "runtime" / "sherpa-onnx-v1.13.2" / "lib" / "libsherpa-onnx-c-api.so").string());
    const auto model = modelDirectory(model_id_);
    const QStringList required = {QStringLiteral("encoder-epoch-99-avg-1.int8.onnx"), QStringLiteral("decoder-epoch-99-avg-1.onnx"), QStringLiteral("joiner-epoch-99-avg-1.int8.onnx"), QStringLiteral("tokens.txt"), QStringLiteral("model.json")};
    if (!QFileInfo::exists(runtime) || std::any_of(required.cbegin(), required.cend(), [&model](const QString &file) { return !QFileInfo::exists(model + QLatin1Char('/') + file); })) {
        if (error) *error = ErrorCode::ModelMissing;
        return false;
    }
    QString loadingError;
    if (!sherpa_.load(runtime, &loadingError)) {
        if (error) *error = ErrorCode::ModelLoadFailed;
        return false;
    }

    const auto encoder = (model + QStringLiteral("/encoder-epoch-99-avg-1.int8.onnx")).toUtf8();
    const auto decoder = (model + QStringLiteral("/decoder-epoch-99-avg-1.onnx")).toUtf8();
    const auto joiner = (model + QStringLiteral("/joiner-epoch-99-avg-1.int8.onnx")).toUtf8();
    const auto tokens = (model + QStringLiteral("/tokens.txt")).toUtf8();
    SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = 16'000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoder.constData();
    config.model_config.transducer.decoder = decoder.constData();
    config.model_config.transducer.joiner = joiner.constData();
    config.model_config.tokens = tokens.constData();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = std::clamp(QThread::idealThreadCount() / 2, 1, 4);
    config.model_config.modeling_unit = "cjkchar";
    config.decoding_method = "greedy_search";
    recognizer_ = sherpa_.createRecognizer(&config);
    if (!recognizer_) {
        if (error) *error = ErrorCode::ModelLoadFailed;
        return false;
    }
    return true;
}

void VoiceWorker::closeRecognitionStream() {
    if (recognition_stream_) sherpa_.destroyStream(recognition_stream_);
    recognition_stream_ = nullptr;
}

void VoiceWorker::closeRecognizer() {
    closeRecognitionStream();
    if (recognizer_ && sherpa_.ready()) sherpa_.destroyRecognizer(recognizer_);
    recognizer_ = nullptr;
}

void VoiceWorker::updateTranscript() {
    if (!recognizer_ || !recognition_stream_) return;
    const auto *result = sherpa_.getResult(recognizer_, recognition_stream_);
    if (!result) return;
    if (result->text) transcript_ = result->text;
    sherpa_.destroyResult(result);
}

void VoiceWorker::decodeAudio(size_t maximumChunks) {
    if (!recognition_stream_ || !sherpa_.ready()) return;
    size_t decoded = 0;
    while (maximumChunks == 0 || decoded < maximumChunks) {
        const auto read = read_sequence_.load(std::memory_order_relaxed);
        const auto written = write_sequence_.load(std::memory_order_acquire);
        if (read == written) break;
        const auto count = std::min<uint64_t>(kDecodeChunkFrames, written - read);
        for (uint64_t i = 0; i < count; ++i) decode_buffer_[i] = audio_ring_[(read + i) % kAudioRingFrames];
        read_sequence_.store(read + count, std::memory_order_release);
        sherpa_.acceptWaveform(recognition_stream_, 16'000, decode_buffer_.data(), static_cast<int32_t>(count));
        while (sherpa_.isReady(recognizer_, recognition_stream_)) sherpa_.decode(recognizer_, recognition_stream_);
        updateTranscript();
        ++decoded;
    }
}

void VoiceWorker::publish(VoiceState state, std::string text, std::optional<ErrorCode> error) {
    VoiceEvent event{.session_id = session_id_.toUtf8().toStdString(), .focus_generation = focus_generation_, .state = state,
                     .text = std::move(text), .level = level_.load(std::memory_order_relaxed),
                     .stability = state == VoiceState::Review ? 1.0F : 0.0F, .error = error};
    const auto serialized = QString::fromUtf8(serialize(event));
    if (state == VoiceState::Review) final_result_ = serialized;
    else if (state == VoiceState::Error) final_result_.clear();
    emit Event(serialized);
}

bool VoiceWorker::Start(const QString &sessionId, qulonglong focusGeneration) {
    if (listening_) return session_id_ == sessionId;
    session_id_ = sessionId;
    focus_generation_ = focusGeneration;
    frames_ = 0;
    speech_frames_ = 0;
    level_ = 0.0F;
    peak_ = 0.0F;
    write_sequence_ = 0;
    read_sequence_ = 0;
    audio_overrun_ = false;
    transcript_.clear();
    final_result_.clear();
    publish(VoiceState::Starting);
    if (source_.contains(QStringLiteral(".monitor"))) {
        publish(VoiceState::Error, {}, ErrorCode::AudioDeviceMissing);
        return false;
    }
    ErrorCode recognizerError = ErrorCode::ModelLoadFailed;
    if (!openRecognizer(&recognizerError)) {
        publish(VoiceState::Error, {}, recognizerError);
        return false;
    }
    closeRecognitionStream();
    recognition_stream_ = sherpa_.createStream(recognizer_);
    if (!recognition_stream_ || !openStream()) {
        closeRecognitionStream();
        publish(VoiceState::Error, {}, ErrorCode::AudioDeviceMissing);
        return false;
    }
    listening_ = true;
    meter_timer_.start();
    publish(VoiceState::Listening);
    return true;
}

bool VoiceWorker::Stop(const QString &sessionId) {
    if (!listening_ || sessionId != session_id_) return false;
    publish(VoiceState::Finalizing);
    closeStream();
    decodeAudio(0);
    sherpa_.inputFinished(recognition_stream_);
    while (sherpa_.isReady(recognizer_, recognition_stream_)) sherpa_.decode(recognizer_, recognition_stream_);
    updateTranscript();
    if (audio_overrun_.load(std::memory_order_relaxed)) {
        publish(VoiceState::Error, {}, ErrorCode::AudioBufferOverrun);
    } else if (speech_frames_.load(std::memory_order_relaxed) < 1'600 || transcript_.empty()) {
        publish(VoiceState::Error, {}, ErrorCode::EmptySpeech);
    } else {
        publish(VoiceState::Review, transcript_);
    }
    closeRecognitionStream();
    meter_timer_.stop();
    listening_ = false;
    return true;
}

bool VoiceWorker::Cancel(const QString &sessionId) {
    if (!listening_ || sessionId != session_id_) return false;
    closeStream();
    closeRecognitionStream();
    meter_timer_.stop();
    listening_ = false;
    transcript_.clear();
    final_result_.clear();
    publish(VoiceState::Idle);
    return true;
}

QString VoiceWorker::GetResult(const QString &sessionId) const {
    return sessionId == session_id_ ? final_result_ : QString{};
}

QString VoiceWorker::Status() {
    QJsonObject value;
    value.insert(QStringLiteral("source"), source_);
    value.insert(QStringLiteral("model_id"), model_id_);
    value.insert(QStringLiteral("listening"), listening_);
    value.insert(QStringLiteral("frames"), static_cast<qint64>(frames_.load(std::memory_order_relaxed)));
    value.insert(QStringLiteral("speech_frames"), static_cast<qint64>(speech_frames_.load(std::memory_order_relaxed)));
    value.insert(QStringLiteral("level"), level_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("peak"), peak_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("input_gain"), input_gain_);
    value.insert(QStringLiteral("audio_overrun"), audio_overrun_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("recognizer_loaded"), recognizer_ != nullptr);
    return QJsonDocument(value).toJson(QJsonDocument::Compact);
}

void VoiceWorker::publishMeter() {
    if (!listening_) return;
    decodeAudio(3);
    publish(VoiceState::Listening, transcript_);
}

void VoiceWorker::process(void *data) {
    auto *self = static_cast<VoiceWorker *>(data);
    auto *buffer = pw_stream_dequeue_buffer(self->stream_);
    if (!buffer) return;
    auto *spa = buffer->buffer;
    if (spa->n_datas > 0 && spa->datas[0].data && spa->datas[0].chunk) {
        const auto bytes = spa->datas[0].chunk->size;
        const auto count = bytes / sizeof(float);
        const auto *samples = static_cast<const float *>(spa->datas[0].data) + spa->datas[0].chunk->offset / sizeof(float);
        double sum = 0.0;
        float peak = 0.0F;
        uint64_t speech = 0;
        auto write = self->write_sequence_.load(std::memory_order_relaxed);
        const auto read = self->read_sequence_.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < count; ++i) {
            const auto sample = std::clamp(samples[i] * self->input_gain_, -1.0F, 1.0F);
            sum += static_cast<double>(sample) * sample;
            peak = std::max(peak, std::abs(sample));
            if (std::abs(sample) >= kSpeechLevel) ++speech;
            if (write - read < kAudioRingFrames) self->audio_ring_[write++ % kAudioRingFrames] = sample;
            else self->audio_overrun_.store(true, std::memory_order_relaxed);
        }
        self->write_sequence_.store(write, std::memory_order_release);
        self->level_.store(count ? static_cast<float>(std::sqrt(sum / count)) : 0.0F, std::memory_order_relaxed);
        self->peak_.store(std::max(peak, self->peak_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
        self->frames_.fetch_add(count, std::memory_order_relaxed);
        self->speech_frames_.fetch_add(speech, std::memory_order_relaxed);
    }
    pw_stream_queue_buffer(self->stream_, buffer);
}
} // namespace modernime
