#pragma once

#include "contracts/contracts.h"
#include "voice-worker/sherpa_api.h"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <array>
#include <memory>
#include <string>

struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;
struct spa_hook;

namespace modernime {

class VoiceWorker final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.modernime.VoiceWorker1")
public:
    explicit VoiceWorker(QString source, QString modelId, QObject *parent = nullptr);
    ~VoiceWorker() override;
    static void process(void *data);
public slots:
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
    void closeStream();
    bool openRecognizer(ErrorCode *error);
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
};

} // namespace modernime
