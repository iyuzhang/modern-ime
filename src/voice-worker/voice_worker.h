#pragma once

#include "contracts/contracts.h"
#include "voice-worker/sherpa_api.h"

#include <QObject>
#include <QTimer>

#include <pipewire/pipewire.h>

#include <atomic>
#include <array>
#include <future>
#include <memory>
#include <string>

struct spa_hook;

namespace modernime {

class VoiceWorker final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.modernime.VoiceWorker1")
public:
    explicit VoiceWorker(QString source, QString modelId, QObject *parent = nullptr);
    ~VoiceWorker() override;
    static void process(void *data);
    static void stateChanged(void *data, pw_stream_state oldState, pw_stream_state state, const char *error);
public slots:
    bool Warmup();
    bool Start(const QString &sessionId, qulonglong focusGeneration);
    bool Stop(const QString &sessionId);
    bool Cancel(const QString &sessionId);
    QString GetResult(const QString &sessionId) const;
    QString Status();
signals:
    void Event(const QString &serialized);
private slots:
    void publishMeter();
private:
    bool openStream();
    bool connectStream();
    bool setStreamActive(bool active);
    void closeStream();
    bool openRecognizer(ErrorCode *error);
    bool loadRuntime(ErrorCode *error);
    bool finishWarmup();
    bool openVad();
    void closeVad();
    bool openPunctuation();
    void closePunctuation();
    std::string punctuate(const std::string &text);
    void closeRecognitionStream();
    void closeRecognizer();
    void decodeAudio(size_t maximumChunks);
    void updateTranscript();
    void publish(VoiceState state, std::string text = {}, std::optional<ErrorCode> error = std::nullopt);
    static constexpr size_t kAudioRingFrames = 16'000 * 30;
    static constexpr size_t kDecodeChunkFrames = 3'200;
    QString source_;
    QString model_id_;
    QString session_id_;
    qulonglong focus_generation_ = 0;
    QTimer meter_timer_;
    float input_gain_ = 1.0F;
    std::atomic<float> level_{0.0F};
    std::atomic<float> peak_{0.0F};
    std::atomic<uint64_t> frames_{0};
    std::atomic<uint64_t> speech_frames_{0};
    std::atomic<uint64_t> write_sequence_{0};
    std::atomic<uint64_t> read_sequence_{0};
    std::atomic<bool> audio_overrun_{false};
    std::atomic<int> stream_state_{PW_STREAM_STATE_UNCONNECTED};
    std::atomic<bool> warming_up_{false};
    std::atomic<qint64> warmup_ms_{-1};
    std::atomic<qint64> start_latency_ms_{-1};
    std::atomic<qint64> finalization_ms_{-1};
    std::atomic<int> last_result_chars_{0};
    std::atomic<int> last_error_{-1};
    bool vad_speech_detected_ = false;
    bool hotwords_enabled_ = false;
    std::array<float, kAudioRingFrames> audio_ring_{};
    std::array<float, kDecodeChunkFrames> decode_buffer_{};
    std::string transcript_;
    QString final_result_;
    bool listening_ = false;
    pw_thread_loop *loop_ = nullptr;
    pw_context *context_ = nullptr;
    pw_core *core_ = nullptr;
    pw_stream *stream_ = nullptr;
    ::spa_hook *stream_listener_opaque_ = nullptr;
    SherpaApi sherpa_;
    const SherpaOnnxOnlineRecognizer *recognizer_ = nullptr;
    const SherpaOnnxOnlineStream *recognition_stream_ = nullptr;
    const SherpaOnnxVoiceActivityDetector *vad_ = nullptr;
    const SherpaOnnxOfflinePunctuation *punctuation_ = nullptr;
    std::future<bool> warmup_future_;
};

} // namespace modernime
