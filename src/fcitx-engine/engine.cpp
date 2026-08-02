#include "fcitx-engine/engine.h"

#include "contracts/contracts.h"
#include "contracts/xdg.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#ifdef emit
#undef emit
#endif
#ifdef foreach
#undef foreach
#endif
#ifdef Q_FOREACH
#undef Q_FOREACH
#endif

#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>

#include <algorithm>
#include <cctype>

namespace modernime {
namespace {
constexpr size_t kCandidatePageCount = 5;
constexpr uint64_t kVoiceReleaseTailUs = 180'000;

class ModernCandidate final : public fcitx::CandidateWord {
public:
    ModernCandidate(ModernEngine *engine, std::string text, std::string annotation) : CandidateWord(fcitx::Text(text)), engine_(engine), text_(std::move(text)) { (void)annotation; }
    void select(fcitx::InputContext *context) const override { engine_->selectCandidate(context, text_); }
private:
    ModernEngine *engine_;
    std::string text_;
};
bool sensitive(const fcitx::InputContext *context) { return context->capabilityFlags().test(fcitx::CapabilityFlag::Password) || context->capabilityFlags().test(fcitx::CapabilityFlag::Sensitive); }
bool isShiftKey(const fcitx::Key &key) { return key.sym() == FcitxKey_Shift_L || key.sym() == FcitxKey_Shift_R; }
bool isLiteralWordCharacter(const fcitx::Key &key) {
    const auto text = fcitx::Key::keySymToUTF8(key.sym());
    return key.isLAZ() || key.isUAZ() || (text.size() == 1 && (text.front() == '.' || text.front() == '_' || text.front() == '-' || text.front() == '/' || text.front() == '@'));
}
void appendToBuffer(std::string *buffer, const fcitx::Key &key) {
    *buffer += fcitx::Key::keySymToUTF8(key.sym());
    std::transform(buffer->begin(), buffer->end(), buffer->begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
}
}

ModernEngine::ModernEngine(fcitx::Instance *instance) : instance_(instance), factory_([](fcitx::InputContext &) { return new CompositionState; }), language_(xdgPaths().data / "user.db") {
    producer_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    instance_->inputContextManager().registerProperty("modern-ime-state", &factory_);
    // Cursor geometry can arrive after the key event that opened the
    // candidate list. Publish again when the frontend has supplied the fresh
    // rectangle, matching the lifecycle used by Fcitx's built-in UIs.
    cursor_rect_watcher_ = instance_->watchEvent(fcitx::EventType::InputContextCursorRectChanged, fcitx::EventWatcherPhase::Default, [this](fcitx::Event &event) {
        auto *context = static_cast<fcitx::InputContextEvent &>(event).inputContext();
        if (!context || !context->hasFocus() || state(context)->buffer.empty()) return;
        publish(context, true);
    });
    reloadConfig();
}
CompositionState *ModernEngine::state(fcitx::InputContext *context) { return context->propertyFor(&factory_); }
void ModernEngine::activate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) { auto *context = event.inputContext(); auto *current = state(context); current->focus_generation++; applyConfig(current); installInputPanelCallback(context); }
void ModernEngine::reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) { auto *current = state(event.inputContext()); current->f8_pressed = false; current->shift_pressed = false; current->pending_voice_release.reset(); current->pending_voice_commit.reset(); cancelVoice(event.inputContext()); clear(event.inputContext()); }
std::string ModernEngine::subMode(const fcitx::InputMethodEntry &, fcitx::InputContext &) { return "中"; }

void ModernEngine::installInputPanelCallback(fcitx::InputContext *context) {
    context->inputPanel().setCustomInputPanelCallback([this](fcitx::InputContext *input) {
        publish(input, !state(input)->buffer.empty());
    });
}
void ModernEngine::clear(fcitx::InputContext *context) { auto *current = state(context); current->buffer.clear(); current->candidates.clear(); context->inputPanel().reset(); installInputPanelCallback(context); context->updatePreedit(); context->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel); publish(context, false); }
void ModernEngine::update(fcitx::InputContext *context) { installInputPanelCallback(context); auto *current = state(context); current->candidates = language_.candidates(current->buffer, static_cast<size_t>(current->candidate_page_size) * kCandidatePageCount); auto list = std::make_unique<fcitx::CommonCandidateList>(); list->setPageSize(current->candidate_page_size); list->setCursorKeepInSamePage(true); list->setLayoutHint(current->candidate_layout == CandidateLayout::Vertical ? fcitx::CandidateLayoutHint::Vertical : fcitx::CandidateLayoutHint::Horizontal); for (const auto &candidate : current->candidates) list->append<ModernCandidate>(this, candidate.text, candidate.annotation); context->inputPanel().setPreedit(fcitx::Text(current->buffer)); context->inputPanel().setClientPreedit(fcitx::Text(current->buffer)); context->inputPanel().setCandidateList(std::move(list)); context->updatePreedit(); context->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel); publish(context, true); }
void ModernEngine::publish(fcitx::InputContext *context, bool visible) {
    auto *current = state(context);
    CandidateSnapshot snapshot;
    snapshot.producer_id = producer_id_;
    // Candidate UI is shared by every input context, so its ordering key must
    // also be global. A per-context sequence restarts at one after focus moves
    // to another application and makes the UI reject every new snapshot as
    // stale compared with the previous context.
    snapshot.sequence = ++next_sequence_;
    snapshot.preedit = current->buffer;
    snapshot.layout = current->candidate_layout;
    snapshot.visible = visible;
    const auto list = context->inputPanel().candidateList();
    if (list) {
        snapshot.selected = std::max(0, list->cursorIndex());
        if (const auto *pageable = list->toPageable()) {
            snapshot.page = pageable->currentPage() + 1;
            snapshot.pages = std::max(1, pageable->totalPages());
        }
    }
    if (list) {
        // CandidateList::candidate() is page-local. Reading it directly keeps
        // the custom window in lockstep with Fcitx's actual current page.
        for (int index = 0; index < list->size(); ++index) {
            const auto global = static_cast<size_t>(std::max(0, snapshot.page - 1)) * static_cast<size_t>(current->candidate_page_size) + static_cast<size_t>(index);
            const auto annotation = global < current->candidates.size() ? current->candidates[global].annotation : std::string{};
            snapshot.candidates.push_back({list->candidate(index).text().toString(), annotation, false, false});
        }
    }
    const auto rect = context->cursorRect();
    snapshot.cursor_x = rect.left(); snapshot.cursor_y = rect.top(); snapshot.cursor_height = rect.height();
    ensureCandidateUi();
    QDBusInterface ui(QStringLiteral("org.modernime.UI1"), QStringLiteral("/org/modernime/UI1"), QStringLiteral("org.modernime.UI1"), QDBusConnection::sessionBus());
    ui.asyncCall(QStringLiteral("ShowSnapshot"), QString::fromUtf8(serialize(snapshot)));
}
void ModernEngine::selectCandidate(fcitx::InputContext *context, std::string value) { auto *current = state(context); context->commitString(language_.normalizeForCommit(value)); QDBusInterface service(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"), QStringLiteral("org.modernime.Service1"), QDBusConnection::sessionBus()); service.asyncCall(QStringLiteral("RecordSelection"), QString::fromUtf8(current->buffer), QString::fromUtf8(value)); clear(context); }
void ModernEngine::reloadConfig() {
    QFile file(QString::fromStdString((xdgPaths().config / "config.json").string()));
    if (!file.open(QIODevice::ReadOnly)) return;
    if (const auto parsed = parseConfigSnapshot(file.readAll().toStdString())) config_ = *parsed;
}
void ModernEngine::applyConfig(CompositionState *current) const {
    current->voice_trigger = config_.voice_trigger;
    current->voice_auto_commit = config_.voice_auto_commit;
    current->candidate_page_size = config_.candidate_count;
    current->candidate_layout = config_.layout;
}
bool ModernEngine::matchesHotkey(const fcitx::KeyEvent &event, const std::string &hotkey) const {
    const fcitx::Key configured(hotkey);
    if (!configured.isValid() || configured.isModifier()) return false;
    const auto normalized = configured.normalize();
    return event.rawKey().normalize().check(normalized) || event.key().normalize().check(normalized);
}
void ModernEngine::ensureCandidateUi() {
    auto *interface = QDBusConnection::sessionBus().interface();
    if (interface) {
        const auto registered = interface->isServiceRegistered(QStringLiteral("org.modernime.UI1"));
        if (registered.isValid() && registered.value()) return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (last_candidate_ui_start_.time_since_epoch().count() != 0 && now - last_candidate_ui_start_ < std::chrono::seconds(2)) return;
    last_candidate_ui_start_ = now;
    auto command = QStandardPaths::findExecutable(QStringLiteral("modern-ime-candidate-ui"));
    if (command.isEmpty()) {
        const auto userInstall = QDir::homePath() + QStringLiteral("/.local/bin/modern-ime-candidate-ui");
        if (QFileInfo(userInstall).isExecutable()) command = userInstall;
    }
    QProcess::startDetached(command.isEmpty() ? QStringLiteral("modern-ime-candidate-ui") : command);
}
void ModernEngine::requestVoice(fcitx::InputContext *context) {
    if (sensitive(context)) return;
    auto *current = state(context);
    if (!current->voice_session_id.empty()) {
        if (current->voice_recording) {
            stopVoice(context);
            return;
        }
        // A finished or failed session must not consume the next F8 press.
        // Cancel is deliberately best-effort: the worker may already have
        // released its stream after reporting a final result or an error.
        cancelVoice(context);
    }
    clear(context);
    const auto id = QString::number(reinterpret_cast<quintptr>(context), 16) + QStringLiteral("-") + QString::number(current->focus_generation) + QStringLiteral("-") + QString::number(++next_sequence_);
    current->voice_session_id = id.toUtf8().toStdString();
    current->voice_recording = true;
    QDBusInterface service(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"), QStringLiteral("org.modernime.Service1"), QDBusConnection::sessionBus());
    service.setTimeout(6'000);
    // Do not leave a key-held session marked as recording when the service
    // cannot start it. A blocking call is short after the worker's initial
    // model load and gives the F8 release a definite session state.
    const auto reply = service.call(QStringLiteral("StartVoice"), id, static_cast<qulonglong>(current->focus_generation));
    const bool started = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().empty() && reply.arguments().front().toBool();
    if (!started) {
        current->voice_session_id.clear();
        current->voice_recording = false;
    }
}
void ModernEngine::stopVoice(fcitx::InputContext *context) {
    auto *current = state(context);
    if (current->voice_session_id.empty() || !current->voice_recording) return;
    QDBusInterface service(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"), QStringLiteral("org.modernime.Service1"), QDBusConnection::sessionBus());
    service.setTimeout(6'000);
    const auto reply = service.call(QStringLiteral("StopVoice"), QString::fromUtf8(current->voice_session_id));
    current->voice_recording = false;
    const bool stopped = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().empty() && reply.arguments().front().toBool();
    // Push-to-talk has exactly one final transcript. Committing at the F8
    // release edge matches normal dictation controls and never leaves Enter
    // to be interpreted by the host application.
    if (stopped && (current->voice_trigger == VoiceTriggerMode::PushToTalk || current->voice_auto_commit) && commitVoiceResult(context)) clear(context);
}
void ModernEngine::cancelVoice(fcitx::InputContext *context) { auto *current = state(context); if (current->voice_session_id.empty()) return; QDBusInterface service(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"), QStringLiteral("org.modernime.Service1"), QDBusConnection::sessionBus()); service.asyncCall(QStringLiteral("CancelVoice"), QString::fromUtf8(current->voice_session_id)); current->voice_session_id.clear(); current->voice_recording = false; }
bool ModernEngine::commitVoiceResult(fcitx::InputContext *context) {
    auto *current = state(context);
    if (current->voice_session_id.empty() || current->voice_recording) return false;
    const auto session = QString::fromUtf8(current->voice_session_id);
    const auto decode = [](const QDBusMessage &reply) -> std::optional<VoiceEvent> {
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().empty()) return std::nullopt;
        return parseVoiceEvent(reply.arguments().front().toString().toUtf8().toStdString());
    };
    // Get the result directly from the worker first. Its final signal is
    // delivered asynchronously through the service, so reading only the
    // service cache races the F8 release and used to make Enter reach Codex.
    QDBusInterface worker(QStringLiteral("org.modernime.VoiceWorker1"), QStringLiteral("/org/modernime/VoiceWorker1"), QStringLiteral("org.modernime.VoiceWorker1"), QDBusConnection::sessionBus());
    worker.setTimeout(500);
    auto result = decode(worker.call(QStringLiteral("GetResult"), session));
    if (!result) {
        QDBusInterface service(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"), QStringLiteral("org.modernime.Service1"), QDBusConnection::sessionBus());
        service.setTimeout(500);
        result = decode(service.call(QStringLiteral("GetVoiceResult"), session));
    }
    if (!result || result->state != VoiceState::Review || result->focus_generation != current->focus_generation || result->text.empty()) return false;
    const auto text = language_.normalizeForCommit(result->text);
    const auto generation = current->focus_generation;
    current->voice_session_id.clear();
    // Some XIM clients discard commits made while processing a key-release
    // event. Post it to Fcitx's next loop turn, after the F8 release has
    // completed, while retaining the original focus-generation guard.
    current->pending_voice_commit.reset();
    current->pending_voice_commit = instance_->eventLoop().addDeferEvent(
        [this, context, generation, text](fcitx::EventSource *) {
            auto *latest = state(context);
            if (latest->focus_generation == generation) context->commitString(text);
            return false;
        });
    QDBusInterface ui(QStringLiteral("org.modernime.UI1"), QStringLiteral("/org/modernime/UI1"), QStringLiteral("org.modernime.UI1"), QDBusConnection::sessionBus());
    ui.asyncCall(QStringLiteral("HideSession"), session);
    return true;
}

void ModernEngine::keyEvent(const fcitx::InputMethodEntry &, fcitx::KeyEvent &event) {
    auto *context = event.inputContext(); auto *current = state(context); const auto key = event.key();
    applyConfig(current);
    if (isShiftKey(event.rawKey()) || isShiftKey(key)) {
        current->shift_pressed = !event.isRelease();
        return;
    }
    if (matchesHotkey(event, config_.voice_hotkey)) {
        const bool repeated = event.rawKey().states().test(fcitx::KeyState::Repeat) ||
                              event.origKey().states().test(fcitx::KeyState::Repeat) ||
                              key.states().test(fcitx::KeyState::Repeat);
        if (repeated) {
            // Qt/X11 generates synthetic release/press pairs while a key is
            // held and marks both halves as Repeat. Neither half is a physical
            // edge. In particular, stopping on the synthetic release used to
            // create hundreds of ~20 ms recordings and EmptySpeech flashes.
            current->pending_voice_release.reset();
            event.filterAndAccept();
            return;
        }
        if (event.isRelease()) {
            current->pending_voice_release.reset();
            if (current->f8_pressed && current->voice_trigger == VoiceTriggerMode::PushToTalk) {
                // Some X11 frontends do not preserve KeyState::Repeat. Delay a
                // release briefly so an immediately following synthetic press
                // can cancel it. A real release has no matching press and
                // stops recording after this small debounce interval.
                const auto generation = current->focus_generation;
                auto release = instance_->eventLoop().addTimeEvent(
                    CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + kVoiceReleaseTailUs, 0,
                    [this, context, generation](fcitx::EventSourceTime *, uint64_t) {
                        auto *latest = state(context);
                        if (latest->focus_generation == generation && latest->f8_pressed) {
                            latest->f8_pressed = false;
                            stopVoice(context);
                        }
                        return true;
                    });
                release->setOneShot();
                current->pending_voice_release = std::move(release);
            } else {
                current->f8_pressed = false;
            }
        } else {
            current->pending_voice_release.reset();
            if (!current->f8_pressed) {
                current->f8_pressed = true;
                requestVoice(context);
            }
        }
        event.filterAndAccept(); return;
    }
    if (event.isRelease()) return;
    const bool shiftedLetter = (current->shift_pressed || event.rawKey().states().test(fcitx::KeyState::Shift) || event.origKey().states().test(fcitx::KeyState::Shift) || key.isUAZ()) && (key.isLAZ() || key.isUAZ());
    if (shiftedLetter) {
        // Commit the uppercase character through Fcitx. Some XIM frontends
        // consume an unhandled key instead of forwarding it to the client.
        if (!current->buffer.empty()) clear(context);
        auto uppercase = fcitx::Key::keySymToUTF8(key.sym());
        std::transform(uppercase.begin(), uppercase.end(), uppercase.begin(), [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
        context->commitString(uppercase);
        event.filterAndAccept();
        return;
    }
    auto candidateList = context->inputPanel().candidateList();
    // Retain the original navigation vocabulary in addition to the rebinding
    // configured in Settings. Existing muscle memory must not disappear when
    // a new configurable binding is introduced.
    const bool previousPage = matchesHotkey(event, config_.previous_page_hotkey) || key.check(FcitxKey_Page_Up) || key.check(FcitxKey_bracketleft) || key.check(FcitxKey_Up);
    const bool nextPage = matchesHotkey(event, config_.next_page_hotkey) || key.check(FcitxKey_Page_Down) || key.check(FcitxKey_bracketright) || key.check(FcitxKey_Down);
    if ((previousPage || nextPage) && candidateList) { if (auto *pageable = candidateList->toPageable()) { previousPage ? pageable->prev() : pageable->next(); publish(context, true); event.filterAndAccept(); return; } }
    const bool previousCandidate = matchesHotkey(event, config_.previous_candidate_hotkey) || key.check(FcitxKey_Left);
    const bool nextCandidate = matchesHotkey(event, config_.next_candidate_hotkey) || key.check(FcitxKey_Right);
    if ((previousCandidate || nextCandidate) && candidateList) {
        if (auto *movable = candidateList->toCursorMovable()) {
            previousCandidate ? movable->prevCandidate() : movable->nextCandidate();
            publish(context, true);
            event.filterAndAccept();
            return;
        }
    }
    if (matchesHotkey(event, config_.cancel_hotkey)) {
        if (!current->buffer.empty() || !current->voice_session_id.empty()) { cancelVoice(context); clear(context); event.filterAndAccept(); }
        return;
    }
    if (matchesHotkey(event, config_.commit_raw_hotkey)) {
        if (!current->buffer.empty()) { context->commitString(current->buffer); clear(context); event.filterAndAccept(); return; }
        if (!current->voice_session_id.empty()) {
            if (commitVoiceResult(context)) clear(context);
            // A stale review cannot be allowed to submit the surrounding
            // application (for example, Codex's send shortcut).
            event.filterAndAccept();
        }
        return;
    }
    // Punctuation such as '_' is part of a literal English token. Handle it
    // before the generic modifier check because '_' and '@' require Shift on
    // common keyboard layouts.
    if (isLiteralWordCharacter(key)) {
        // A bare punctuation key belongs to the application. Only keep it in
        // a literal English token after that token has already started.
        if (current->buffer.empty() && !key.isLAZ() && !key.isUAZ()) return;
        if (!current->voice_session_id.empty()) cancelVoice(context);
        appendToBuffer(&current->buffer, key);
        update(context);
        event.filterAndAccept();
        return;
    }
    if (key.hasModifier()) return;
    if (current->buffer.empty()) return;
    if (key.check(FcitxKey_BackSpace)) { current->buffer.pop_back(); if (current->buffer.empty()) clear(context); else update(context); event.filterAndAccept(); return; }
    if (key.check(FcitxKey_space) && candidateList && candidateList->size()) { candidateList->candidate(std::max(0, candidateList->cursorIndex())).select(context); event.filterAndAccept(); return; }
    const int selection = key.digitSelection(); if (selection >= 0 && candidateList && selection < candidateList->size()) { candidateList->candidate(selection).select(context); event.filterAndAccept(); return; }
    if (key.check(FcitxKey_apostrophe)) { appendToBuffer(&current->buffer, key); update(context); event.filterAndAccept(); }
}
} // namespace modernime
