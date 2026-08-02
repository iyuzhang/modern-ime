#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modernime {

inline constexpr uint32_t kProtocolVersion = 1;
inline constexpr uint32_t kLexiconFormatVersion = 1;

enum class ErrorCode {
    AudioDeviceMissing,
    AudioPermissionDenied,
    AudioBufferOverrun,
    ModelMissing,
    ModelCorrupt,
    ModelLoadFailed,
    RecognizerTooSlow,
    FocusChanged,
    EmptySpeech,
    InvalidRequest,
    StorageFailure,
};

enum class VoiceState { Idle, Starting, Listening, Finalizing, Review, Committing, Error };
enum class VoiceTriggerMode { PushToTalk, Toggle };
enum class CandidateLayout { Horizontal, Vertical };
enum class CandidateTheme { Midnight, Aurora, Cloud, Ink, Starlight };

struct Candidate {
    std::string text;
    std::string annotation;
    bool pinned = false;
    bool literal = false;
};

struct CandidateSnapshot {
    uint32_t protocol_version = kProtocolVersion;
    // Monotonic sequence numbers are scoped to one engine process. The
    // producer id lets a long-running candidate UI accept sequence 1 again
    // after Fcitx restarts.
    std::string producer_id;
    uint64_t sequence = 0;
    std::string preedit;
    std::vector<Candidate> candidates;
    int selected = 0;
    int page = 1;
    int pages = 1;
    CandidateLayout layout = CandidateLayout::Horizontal;
    std::string mode = "中文";
    int cursor_x = -1;
    int cursor_y = -1;
    int cursor_height = 0;
    bool visible = false;
};

struct ConfigSnapshot {
    uint32_t protocol_version = kProtocolVersion;
    uint64_t version = 1;
    bool chinese_mode = true;
    bool insert_spacing = true;
    int candidate_count = 7;
    CandidateLayout layout = CandidateLayout::Horizontal;
    int font_size = 15;
    int corner_radius = 10;
    int opacity = 96;
    // Keep the established midnight appearance as the default so existing
    // installations do not change visual style when the new theme setting is
    // introduced.
    CandidateTheme theme = CandidateTheme::Midnight;
    VoiceTriggerMode voice_trigger = VoiceTriggerMode::PushToTalk;
    bool voice_auto_commit = false;
    // These are portable Fcitx key names (for example "F8" or
    // "Control+Return"). Keeping them in the shared snapshot lets both the
    // settings application and the engine use exactly the same bindings.
    std::string voice_hotkey = "F8";
    std::string cancel_hotkey = "Escape";
    std::string commit_raw_hotkey = "Return";
    std::string previous_page_hotkey = "Page_Up";
    std::string next_page_hotkey = "Page_Down";
    std::string previous_candidate_hotkey = "Left";
    std::string next_candidate_hotkey = "Right";
    // Audio capture is opt-in. A machine-specific source must be selected;
    // falling back to a guessed source can silently capture the wrong device.
    std::string microphone;
    std::string model_id = "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20";
};

struct VoiceEvent {
    uint32_t protocol_version = kProtocolVersion;
    std::string session_id;
    uint64_t focus_generation = 0;
    VoiceState state = VoiceState::Idle;
    std::string text;
    float level = 0.0F;
    float stability = 0.0F;
    std::optional<ErrorCode> error;
};

struct Lexeme {
    int64_t id = 0;
    std::string reading;
    std::string output;
    std::string language_tag = "und";
    std::string kind = "manual";
    bool pinned = false;
    bool blocked = false;
    double base_weight = 0.0;
    uint64_t select_count = 0;
    int64_t last_selected_at = 0;
};

std::string serialize(const CandidateSnapshot &snapshot);
std::optional<CandidateSnapshot> parseCandidateSnapshot(std::string_view value);
std::string serialize(const ConfigSnapshot &snapshot);
std::optional<ConfigSnapshot> parseConfigSnapshot(std::string_view value);
std::string serialize(const VoiceEvent &event);
std::optional<VoiceEvent> parseVoiceEvent(std::string_view value);
std::string errorCodeName(ErrorCode code);
std::string voiceStateName(VoiceState state);
std::string escapeJson(std::string_view value);

} // namespace modernime
