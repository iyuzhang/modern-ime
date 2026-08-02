#include "voice-worker/voice_worker.h"

#include "contracts/xdg.h"
#include "contracts/voice_model.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/utils/hook.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace modernime {
namespace {
// HFP microphones commonly deliver a much quieter signal than built-in mics.
constexpr float kBluetoothInputGain = 4.0F;
constexpr float kSpeechLevel = 0.003F;
constexpr qint64 kStreamReadyTimeoutMs = 3'000;

const pw_stream_events kStreamEvents = [] {
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = VoiceWorker::stateChanged;
    events.process = VoiceWorker::process;
    return events;
}();

QString modelDirectory(const QString &modelId) {
    const auto paths = xdgPaths();
    return QString::fromStdString((paths.data / "models" / modelId.toStdString()).string());
}
} // namespace

VoiceWorker::VoiceWorker(QString source, QString modelId, QObject *parent)
    : QObject(parent), source_(std::move(source)), model_id_(isValidVoiceModelId(modelId) ? std::move(modelId) : QString::fromLatin1(kDefaultVoiceModel)) {
    input_gain_ = source_.startsWith(QStringLiteral("bluez_input.")) ? kBluetoothInputGain : 1.0F;
    meter_timer_.setInterval(200);
    connect(&meter_timer_, &QTimer::timeout, this, &VoiceWorker::publishMeter);
    pw_init(nullptr, nullptr);
}

VoiceWorker::~VoiceWorker() {
    closeStream();
    finishWarmup();
    closePunctuation();
    closeVad();
    closeRecognizer();
    pw_deinit();
}

bool VoiceWorker::openStream() {
    return connectStream() && setStreamActive(true);
}

bool VoiceWorker::connectStream() {
    if (stream_) {
        const auto state = static_cast<pw_stream_state>(stream_state_.load(std::memory_order_acquire));
        if (state != PW_STREAM_STATE_ERROR && state != PW_STREAM_STATE_UNCONNECTED) return true;
        closeStream();
    }
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
    stream_state_.store(PW_STREAM_STATE_CONNECTING, std::memory_order_release);
    if (pw_stream_connect(stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
                          static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_INACTIVE),
                          params, 1) < 0) {
        closeStream();
        return false;
    }
    if (pw_thread_loop_start(loop_) < 0) {
        closeStream();
        return false;
    }

    // Establish and negotiate the device while inactive. Keeping this paused
    // stream alive avoids re-opening a Bluetooth HFP graph for every phrase,
    // but an inactive stream does not receive or retain microphone samples.
    QElapsedTimer readyTimer;
    readyTimer.start();
    while (readyTimer.elapsed() < kStreamReadyTimeoutMs) {
        const auto state = static_cast<pw_stream_state>(stream_state_.load(std::memory_order_acquire));
        if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) break;
        if (state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_STREAMING) return true;
        QThread::msleep(5);
    }
    closeStream();
    return false;
}

bool VoiceWorker::setStreamActive(bool active) {
    if (!stream_ || !loop_) return false;
    pw_thread_loop_lock(loop_);
    const int result = pw_stream_set_active(stream_, active);
    pw_thread_loop_unlock(loop_);
    if (result < 0) return false;

    QElapsedTimer stateTimer;
    stateTimer.start();
    while (stateTimer.elapsed() < kStreamReadyTimeoutMs) {
        const auto state = static_cast<pw_stream_state>(stream_state_.load(std::memory_order_acquire));
        if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) return false;
        if (active && state == PW_STREAM_STATE_STREAMING && frames_.load(std::memory_order_acquire) > 0) return true;
        if (!active && state == PW_STREAM_STATE_PAUSED) return true;
        QThread::msleep(5);
    }
    return false;
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
    stream_state_.store(PW_STREAM_STATE_UNCONNECTED, std::memory_order_release);
    listening_ = false;
}

bool VoiceWorker::openRecognizer(ErrorCode *error) {
    if (recognizer_) return true;
    const auto paths = xdgPaths();
    const auto model = modelDirectory(model_id_);
    QString manifestError;
    const auto spec = loadVoiceModelSpec(model, &manifestError);
    if (!spec) {
        if (error) *error = QFileInfo(model).isDir() ? ErrorCode::ModelCorrupt : ErrorCode::ModelMissing;
        return false;
    }
    if (!loadRuntime(error)) return false;

    const auto encoder = spec->encoder.toUtf8();
    const auto decoder = spec->decoder.toUtf8();
    const auto joiner = spec->joiner.toUtf8();
    const auto tokens = spec->tokens.toUtf8();
    const auto modelingUnit = spec->modeling_unit.toUtf8();
    const auto bpeVocab = spec->bpe_vocab.toUtf8();
    const auto hotwordsPath = QString::fromStdString((paths.data / "voice-hotwords.txt").string());
    const auto hotwords = hotwordsPath.toUtf8();
    SherpaOnnxOnlineRecognizerConfig config{};
    config.feat_config.sample_rate = 16'000;
    config.feat_config.feature_dim = 80;
    config.model_config.transducer.encoder = encoder.constData();
    config.model_config.transducer.decoder = decoder.constData();
    config.model_config.transducer.joiner = joiner.constData();
    config.model_config.tokens = tokens.constData();
    config.model_config.provider = "cpu";
    config.model_config.num_threads = std::clamp(QThread::idealThreadCount() / 2, 1, 4);
    config.model_config.modeling_unit = modelingUnit.constData();
    if (!bpeVocab.isEmpty()) config.model_config.bpe_vocab = bpeVocab.constData();
    // Beam search retains plausible alternatives instead of permanently
    // discarding a rare short phrase at its first ambiguous frame. It is also
    // required by sherpa-onnx contextual biasing, which is layered on later.
    config.decoding_method = "modified_beam_search";
    config.max_active_paths = 4;
    hotwords_enabled_ = false;
    if (spec->supports_hotwords && QFileInfo(hotwordsPath).size() > 0) {
        config.hotwords_file = hotwords.constData();
        config.hotwords_score = 2.0F;
        hotwords_enabled_ = true;
    }
    recognizer_ = sherpa_.createRecognizer(&config);
    if (!recognizer_) {
        if (error) *error = ErrorCode::ModelLoadFailed;
        return false;
    }
    return true;
}

bool VoiceWorker::loadRuntime(ErrorCode *error) {
    if (sherpa_.ready()) return true;
    const auto runtime = QString::fromStdString((xdgPaths().data / "runtime" / "sherpa-onnx-v1.13.2" / "lib" / "libsherpa-onnx-c-api.so").string());
    if (!QFileInfo::exists(runtime)) {
        if (error) *error = ErrorCode::ModelMissing;
        return false;
    }
    QString loadingError;
    if (!sherpa_.load(runtime, &loadingError)) {
        if (error) *error = ErrorCode::ModelLoadFailed;
        return false;
    }

    return true;
}

bool VoiceWorker::Warmup() {
    ErrorCode error = ErrorCode::ModelLoadFailed;
    if (!loadRuntime(&error)) return false;
    if (!source_.isEmpty() && !source_.contains(QStringLiteral(".monitor"))) connectStream();
    if (warming_up_.load(std::memory_order_acquire)) return true;
    if (warmup_future_.valid()) warmup_future_.get();
    if (recognizer_ && vad_ && punctuation_) return true;
    warming_up_.store(true, std::memory_order_release);
    warmup_future_ = std::async(std::launch::async, [this] {
        QElapsedTimer timer;
        timer.start();
        ErrorCode modelError = ErrorCode::ModelLoadFailed;
        const bool recognizerReady = openRecognizer(&modelError);
        const bool vadReady = recognizerReady && openVad();
        const bool punctuationReady = recognizerReady && openPunctuation();
        warmup_ms_.store(timer.elapsed(), std::memory_order_relaxed);
        warming_up_.store(false, std::memory_order_release);
        return recognizerReady && vadReady && punctuationReady;
    });
    return true;
}

bool VoiceWorker::finishWarmup() {
    if (!warmup_future_.valid()) return recognizer_ != nullptr;
    const bool result = warmup_future_.get();
    warming_up_.store(false, std::memory_order_release);
    return result;
}

bool VoiceWorker::openVad() {
    if (vad_) return true;
    if (!sherpa_.ready()) return false;
    const auto vadPath = QString::fromStdString((xdgPaths().data / "models" / "silero-vad" / "silero_vad.onnx").string());
    if (!QFileInfo::exists(vadPath)) return false;
    const auto encodedPath = vadPath.toUtf8();
    SherpaOnnxVadModelConfig config{};
    config.silero_vad.model = encodedPath.constData();
    config.silero_vad.threshold = 0.35F;
    config.silero_vad.min_silence_duration = 0.25F;
    config.silero_vad.min_speech_duration = 0.15F;
    config.silero_vad.window_size = 512;
    config.silero_vad.max_speech_duration = 30.0F;
    config.sample_rate = 16'000;
    config.num_threads = 1;
    config.provider = "cpu";
    vad_ = sherpa_.createVoiceActivityDetector(&config, 30.0F);
    return vad_ != nullptr;
}

void VoiceWorker::closeVad() {
    if (vad_ && sherpa_.ready()) sherpa_.destroyVoiceActivityDetector(vad_);
    vad_ = nullptr;
}

bool VoiceWorker::openPunctuation() {
    if (punctuation_) return true;
    if (!sherpa_.ready()) return false;
    const auto modelPath = QString::fromStdString((xdgPaths().data / "models" / kPunctuationModel / "model.int8.onnx").string());
    if (!QFileInfo::exists(modelPath)) return false;
    const auto encodedPath = modelPath.toUtf8();
    SherpaOnnxOfflinePunctuationConfig config{};
    config.model.ct_transformer = encodedPath.constData();
    config.model.num_threads = 1;
    config.model.provider = "cpu";
    punctuation_ = sherpa_.createOfflinePunctuation(&config);
    return punctuation_ != nullptr;
}

void VoiceWorker::closePunctuation() {
    if (punctuation_ && sherpa_.ready()) sherpa_.destroyOfflinePunctuation(punctuation_);
    punctuation_ = nullptr;
}

std::string VoiceWorker::punctuate(const std::string &text) {
    if (text.empty() || !punctuation_) return text;
    const auto *result = sherpa_.addOfflinePunctuation(punctuation_, text.c_str());
    if (!result) return text;
    std::string output(result);
    sherpa_.freeOfflinePunctuationText(result);
    return output.empty() ? text : output;
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
        if (vad_) {
            sherpa_.vadAcceptWaveform(vad_, decode_buffer_.data(), static_cast<int32_t>(count));
            vad_speech_detected_ = vad_speech_detected_ || sherpa_.vadDetected(vad_) != 0 || sherpa_.vadEmpty(vad_) == 0;
        }
        while (sherpa_.isReady(recognizer_, recognition_stream_)) sherpa_.decode(recognizer_, recognition_stream_);
        updateTranscript();
        ++decoded;
    }
}

void VoiceWorker::publish(VoiceState state, std::string text, std::optional<ErrorCode> error) {
    if (state == VoiceState::Review) {
        last_result_chars_.store(QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())).size(), std::memory_order_relaxed);
        last_error_.store(-1, std::memory_order_relaxed);
    } else if (state == VoiceState::Error) {
        last_result_chars_.store(0, std::memory_order_relaxed);
        last_error_.store(static_cast<int>(error.value_or(ErrorCode::InvalidRequest)), std::memory_order_relaxed);
    }
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
    QElapsedTimer startTimer;
    startTimer.start();
    session_id_ = sessionId;
    focus_generation_ = focusGeneration;
    frames_ = 0;
    speech_frames_ = 0;
    level_ = 0.0F;
    peak_ = 0.0F;
    write_sequence_ = 0;
    read_sequence_ = 0;
    audio_overrun_ = false;
    vad_speech_detected_ = false;
    transcript_.clear();
    final_result_.clear();
    publish(VoiceState::Starting);
    if (source_.contains(QStringLiteral(".monitor"))) {
        publish(VoiceState::Error, {}, ErrorCode::AudioDeviceMissing);
        return false;
    }
    // Start capture before any possible cold model load. The PipeWire thread
    // can fill the ring while ONNX Runtime initializes, so speech beginning at
    // the physical key press is not thrown away.
    if (!openStream()) {
        publish(VoiceState::Error, {}, ErrorCode::AudioDeviceMissing);
        return false;
    }
    finishWarmup();
    ErrorCode recognizerError = ErrorCode::ModelLoadFailed;
    if (!openRecognizer(&recognizerError)) {
        setStreamActive(false);
        publish(VoiceState::Error, {}, recognizerError);
        return false;
    }
    openVad();
    if (vad_) sherpa_.vadReset(vad_);
    openPunctuation();
    closeRecognitionStream();
    recognition_stream_ = sherpa_.createStream(recognizer_);
    if (!recognition_stream_) {
        setStreamActive(false);
        closeRecognitionStream();
        publish(VoiceState::Error, {}, ErrorCode::ModelLoadFailed);
        return false;
    }
    listening_ = true;
    meter_timer_.start();
    start_latency_ms_.store(startTimer.elapsed(), std::memory_order_relaxed);
    publish(VoiceState::Listening);
    return true;
}

bool VoiceWorker::Stop(const QString &sessionId) {
    if (!listening_ || sessionId != session_id_) return false;
    QElapsedTimer finalizationTimer;
    finalizationTimer.start();
    publish(VoiceState::Finalizing);
    if (!setStreamActive(false)) closeStream();
    decodeAudio(0);
    sherpa_.inputFinished(recognition_stream_);
    while (sherpa_.isReady(recognizer_, recognition_stream_)) sherpa_.decode(recognizer_, recognition_stream_);
    updateTranscript();
    if (vad_) {
        sherpa_.vadFlush(vad_);
        vad_speech_detected_ = vad_speech_detected_ || sherpa_.vadDetected(vad_) != 0 || sherpa_.vadEmpty(vad_) == 0;
    }
    const bool hasSpeech = vad_ ? vad_speech_detected_ : speech_frames_.load(std::memory_order_relaxed) >= 1'600;
    if (audio_overrun_.load(std::memory_order_relaxed)) {
        publish(VoiceState::Error, {}, ErrorCode::AudioBufferOverrun);
    } else if (!hasSpeech) {
        publish(VoiceState::Error, {}, ErrorCode::EmptySpeech);
    } else if (transcript_.empty()) {
        publish(VoiceState::Error, {}, ErrorCode::RecognizerNoResult);
    } else {
        transcript_ = punctuate(transcript_);
        publish(VoiceState::Review, transcript_);
    }
    closeRecognitionStream();
    meter_timer_.stop();
    listening_ = false;
    finalization_ms_.store(finalizationTimer.elapsed(), std::memory_order_relaxed);
    return true;
}

bool VoiceWorker::Cancel(const QString &sessionId) {
    if (!listening_ || sessionId != session_id_) return false;
    if (!setStreamActive(false)) closeStream();
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
    const bool warmingUp = warming_up_.load(std::memory_order_acquire);
    value.insert(QStringLiteral("source"), source_);
    value.insert(QStringLiteral("model_id"), model_id_);
    value.insert(QStringLiteral("listening"), listening_);
    value.insert(QStringLiteral("frames"), static_cast<qint64>(frames_.load(std::memory_order_relaxed)));
    value.insert(QStringLiteral("audio_seconds"), static_cast<double>(frames_.load(std::memory_order_relaxed)) / 16'000.0);
    value.insert(QStringLiteral("speech_frames"), static_cast<qint64>(speech_frames_.load(std::memory_order_relaxed)));
    value.insert(QStringLiteral("level"), level_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("peak"), peak_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("input_gain"), input_gain_);
    value.insert(QStringLiteral("audio_overrun"), audio_overrun_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("stream_state"), QString::fromLatin1(pw_stream_state_as_string(static_cast<pw_stream_state>(stream_state_.load(std::memory_order_relaxed)))));
    value.insert(QStringLiteral("warming_up"), warmingUp);
    value.insert(QStringLiteral("recognizer_loaded"), !warmingUp && recognizer_ != nullptr);
    value.insert(QStringLiteral("vad_loaded"), !warmingUp && vad_ != nullptr);
    value.insert(QStringLiteral("vad_speech_detected"), vad_speech_detected_);
    value.insert(QStringLiteral("punctuation_loaded"), !warmingUp && punctuation_ != nullptr);
    value.insert(QStringLiteral("hotwords_enabled"), !warmingUp && hotwords_enabled_);
    value.insert(QStringLiteral("warmup_ms"), warmup_ms_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("start_latency_ms"), start_latency_ms_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("finalization_ms"), finalization_ms_.load(std::memory_order_relaxed));
    value.insert(QStringLiteral("last_result_chars"), last_result_chars_.load(std::memory_order_relaxed));
    const int lastError = last_error_.load(std::memory_order_relaxed);
    value.insert(QStringLiteral("last_error"), lastError < 0 ? QString{} : QString::fromStdString(errorCodeName(static_cast<ErrorCode>(lastError))));
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

void VoiceWorker::stateChanged(void *data, pw_stream_state, pw_stream_state state, const char *) {
    static_cast<VoiceWorker *>(data)->stream_state_.store(state, std::memory_order_release);
}
} // namespace modernime
