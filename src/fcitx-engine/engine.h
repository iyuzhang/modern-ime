#pragma once

#include "contracts/contracts.h"
#include "language-core/language_core.h"

#include <fcitx/inputcontextproperty.h>
#include <fcitx/addonfactory.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx-utils/event.h>

#include <memory>
#include <chrono>
#include <vector>

namespace fcitx { class InputContext; }
namespace modernime {

class CompositionState final : public fcitx::InputContextProperty {
public:
    std::string buffer;
    std::vector<Candidate> candidates;
    uint64_t focus_generation = 0;
    std::string voice_session_id;
    bool voice_recording = false;
    bool f8_pressed = false;
    bool shift_pressed = false;
    std::unique_ptr<fcitx::EventSource> pending_voice_release;
    std::unique_ptr<fcitx::EventSource> pending_voice_commit;
    VoiceTriggerMode voice_trigger = VoiceTriggerMode::PushToTalk;
    bool voice_auto_commit = false;
    int candidate_page_size = 7;
    CandidateLayout candidate_layout = CandidateLayout::Horizontal;
};

class ModernEngine final : public fcitx::InputMethodEngineV2 {
public:
    explicit ModernEngine(fcitx::Instance *instance);
    void keyEvent(const fcitx::InputMethodEntry &entry, fcitx::KeyEvent &event) override;
    void activate(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) override;
    void reset(const fcitx::InputMethodEntry &entry, fcitx::InputContextEvent &event) override;
    void reloadConfig() override;
    std::string subMode(const fcitx::InputMethodEntry &entry, fcitx::InputContext &context) override;
    void selectCandidate(fcitx::InputContext *context, std::string value);
private:
    void installInputPanelCallback(fcitx::InputContext *context);
    void update(fcitx::InputContext *context);
    void clear(fcitx::InputContext *context);
    void publish(fcitx::InputContext *context, bool visible);
    void requestVoice(fcitx::InputContext *context);
    void stopVoice(fcitx::InputContext *context);
    void cancelVoice(fcitx::InputContext *context);
    bool commitVoiceResult(fcitx::InputContext *context);
    void applyConfig(CompositionState *current) const;
    bool matchesHotkey(const fcitx::KeyEvent &event, const std::string &hotkey) const;
    void ensureCandidateUi();
    CompositionState *state(fcitx::InputContext *context);
    fcitx::Instance *instance_;
    fcitx::FactoryFor<CompositionState> factory_;
    LanguageCore language_;
    ConfigSnapshot config_;
    std::string producer_id_;
    uint64_t next_sequence_ = 0;
    std::unique_ptr<fcitx::HandlerTableEntry<fcitx::EventHandler>> cursor_rect_watcher_;
    std::chrono::steady_clock::time_point last_candidate_ui_start_{};
};

class ModernEngineFactory final : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance *create(fcitx::AddonManager *manager) override;
};

} // namespace modernime
