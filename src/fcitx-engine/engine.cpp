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
#include <fcitx-utils/utf8.h>

#include <algorithm>
#include <cctype>

namespace modernime {
namespace {
constexpr size_t kCandidatePageCount = 5;
constexpr uint64_t kVoiceReleaseTailUs = 180'000;

class ModernCandidate final : public fcitx::CandidateWord {
public:
    ModernCandidate(ModernEngine *engine, Candidate candidate) : CandidateWord(fcitx::Text(candidate.text)), engine_(engine), candidate_(std::move(candidate)) {}
    void select(fcitx::InputContext *context) const override { engine_->selectCandidate(context, candidate_); }
    const Candidate &candidate() const { return candidate_; }
private:
    ModernEngine *engine_;
    Candidate candidate_;
};
bool sensitive(const fcitx::InputContext *context) { return context->capabilityFlags().test(fcitx::CapabilityFlag::Password) || context->capabilityFlags().test(fcitx::CapabilityFlag::Sensitive); }
bool isShiftKey(const fcitx::Key &key) { return key.sym() == FcitxKey_Shift_L || key.sym() == FcitxKey_Shift_R; }
bool isControlKey(const fcitx::Key &key) { return key.sym() == FcitxKey_Control_L || key.sym() == FcitxKey_Control_R; }
bool isLiteralWordCharacter(const fcitx::Key &key) {
    const auto text = fcitx::Key::keySymToUTF8(key.sym());
    return key.isLAZ() || key.isUAZ() || (text.size() == 1 && (text.front() == '.' || text.front() == '_' || text.front() == '-' || text.front() == '/' || text.front() == '@' || text.front() == ':' || text.front() == '+' || text.front() == '#' || text.front() == '>'));
}
bool startsLiteralPrefix(const fcitx::Key &key) {
    const auto text = fcitx::Key::keySymToUTF8(key.sym());
    return text.size() == 1 && (text.front() == '/' || text.front() == '-' || text.front() == '@' || text.front() == '$');
}
bool isAsciiPunctuation(const fcitx::Key &key) {
    const auto text = fcitx::Key::keySymToUTF8(key.sym());
    return text.size() == 1 && std::ispunct(static_cast<unsigned char>(text.front()));
}
uint32_t characterBeforeCursor(const fcitx::SurroundingText &surrounding) {
    // Frontends may expose an entire document. Context classification needs
    // only one character and must stay bounded on the key-event path.
    if (!surrounding.isValid() || surrounding.cursor() == 0 || surrounding.text().size() > 16 * 1024) return fcitx::utf8::INVALID_CHAR;
    size_t index = 0;
    uint32_t previous = fcitx::utf8::INVALID_CHAR;
    for (const auto character : fcitx::utf8::MakeUTF8CharRange(surrounding.text())) {
        if (index++ >= surrounding.cursor()) break;
        previous = character;
    }
    return previous;
}
bool isHan(uint32_t character) {
    return (character >= 0x3400 && character <= 0x9FFF) || (character >= 0xF900 && character <= 0xFAFF) || (character >= 0x20000 && character <= 0x323AF);
}
std::string withContextSpacing(fcitx::InputContext *context, std::string text, bool enabled) {
    if (!enabled || text.empty()) return text;
    const auto previous = characterBeforeCursor(context->surroundingText());
    const auto first = fcitx::utf8::getChar(text);
    const bool previousAsciiWord = previous < 128 && std::isalnum(static_cast<unsigned char>(previous));
    const bool firstAsciiWord = first < 128 && std::isalnum(static_cast<unsigned char>(first));
    if ((isHan(previous) && firstAsciiWord) || (previousAsciiWord && isHan(first))) text.insert(text.begin(), ' ');
    return text;
}
void appendToBuffer(std::string *buffer, const fcitx::Key &key, bool uppercaseLetter = false) {
    auto text = fcitx::Key::keySymToUTF8(key.sym());
    if (uppercaseLetter && text.size() == 1) text.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(text.front())));
    *buffer += text;
}
}

ModernEngine::ModernEngine(fcitx::Instance *instance) : instance_(instance), factory_([](fcitx::InputContext &) { return new CompositionState; }), language_(xdgPaths().data / "user.db"), lexicon_bus_(fcitx::dbus::BusType::Session) {
    producer_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    instance_->inputContextManager().registerProperty("modern-ime-state", &factory_);
    // Cursor geometry can arrive after the key event that opened the
    // candidate list. Publish again when the frontend has supplied the fresh
    // rectangle, matching the lifecycle used by Fcitx's built-in UIs.
    cursor_rect_watcher_ = instance_->watchEvent(fcitx::EventType::InputContextCursorRectChanged, fcitx::EventWatcherPhase::Default, [this](fcitx::Event &event) {
        auto *context = static_cast<fcitx::InputContextEvent &>(event).inputContext();
        if (!context || !context->hasFocus() || (state(context)->buffer.empty() && state(context)->selected_text.empty())) return;
        publish(context, true);
    });
    lexicon_bus_.attachEventLoop(&instance_->eventLoop());
    lexicon_changed_slot_ = lexicon_bus_.addMatch(
        fcitx::dbus::MatchRule(std::string{}, "/org/modernime/Service1", "org.modernime.Service1", "LexiconChanged"),
        [this](fcitx::dbus::Message &) {
            language_.refreshUserLexicon();
            return true;
        });
    reloadConfig();
}
CompositionState *ModernEngine::state(fcitx::InputContext *context) { return context->propertyFor(&factory_); }
void ModernEngine::activate(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) { auto *context = event.inputContext(); auto *current = state(context); current->focus_generation++; if (config_.input_mode != InputMode::Auto) current->manual_chinese = false; applyConfig(current); installInputPanelCallback(context); }
void ModernEngine::reset(const fcitx::InputMethodEntry &, fcitx::InputContextEvent &event) { auto *current = state(event.inputContext()); current->f8_pressed = false; current->shift_pressed = false; current->control_pressed = false; current->control_used = false; current->manual_chinese = false; current->pending_voice_release.reset(); current->pending_voice_commit.reset(); cancelVoice(event.inputContext()); clear(event.inputContext()); }
std::string ModernEngine::subMode(const fcitx::InputMethodEntry &, fcitx::InputContext &context) {
    if (effectiveInputMode(state(&context)) == InputMode::English) return "英";
    if (effectiveInputMode(state(&context)) == InputMode::Chinese) return "中";
    return "智";
}

void ModernEngine::installInputPanelCallback(fcitx::InputContext *context) {
    context->inputPanel().setCustomInputPanelCallback([this](fcitx::InputContext *input) {
        publish(input, !state(input)->buffer.empty());
    });
}
void ModernEngine::clear(fcitx::InputContext *context) { auto *current = state(context); current->buffer.clear(); current->selected_text.clear(); current->selected_reading.clear(); current->candidates.clear(); current->literal_prefix = false; current->literal_passthrough = false; context->inputPanel().reset(); installInputPanelCallback(context); context->updatePreedit(); context->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel); publish(context, false); }
void ModernEngine::update(fcitx::InputContext *context, bool allowAutoCommit) {
    installInputPanelCallback(context);
    auto *current = state(context);
    const auto features = inputFeatures(context, current);
    current->candidates = language_.candidates(current->buffer, static_cast<size_t>(current->candidate_page_size) * kCandidatePageCount, features);
    if (allowAutoCommit && current->selected_text.empty() && language_.shouldAutoCommitLiteral(current->buffer, features, current->candidates)) {
        const auto text = withContextSpacing(context, language_.normalizeForCommit(current->buffer), config_.insert_spacing);
        context->commitString(text);
        clear(context);
        state(context)->literal_passthrough = true;
        return;
    }
    auto list = std::make_unique<fcitx::CommonCandidateList>(); list->setPageSize(current->candidate_page_size); list->setCursorKeepInSamePage(true); list->setLayoutHint(current->candidate_layout == CandidateLayout::Vertical ? fcitx::CandidateLayoutHint::Vertical : fcitx::CandidateLayoutHint::Horizontal); for (const auto &candidate : current->candidates) list->append<ModernCandidate>(this, candidate); const auto preedit = current->selected_text + current->buffer; context->inputPanel().setPreedit(fcitx::Text(preedit)); context->inputPanel().setClientPreedit(fcitx::Text(preedit)); context->inputPanel().setCandidateList(std::move(list)); context->updatePreedit(); context->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel); publish(context, true);
}
void ModernEngine::publish(fcitx::InputContext *context, bool visible) {
    auto *current = state(context);
    CandidateSnapshot snapshot;
    snapshot.producer_id = producer_id_;
    // Candidate UI is shared by every input context, so its ordering key must
    // also be global. A per-context sequence restarts at one after focus moves
    // to another application and makes the UI reject every new snapshot as
    // stale compared with the previous context.
    snapshot.sequence = ++next_sequence_;
    snapshot.preedit = current->selected_text + current->buffer;
    const auto mode = effectiveInputMode(current);
    snapshot.mode = mode == InputMode::Auto ? "智能" : mode == InputMode::English ? "英文" : current->manual_chinese ? "临时中文" : "中文";
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
            const auto *modern = dynamic_cast<const ModernCandidate *>(&list->candidate(index));
            snapshot.candidates.push_back({list->candidate(index).text().toString(), annotation, modern && modern->candidate().pinned, modern && modern->candidate().literal});
        }
    }
    const auto rect = context->cursorRect();
    snapshot.cursor_x = rect.left(); snapshot.cursor_y = rect.top(); snapshot.cursor_height = rect.height();
    ensureCandidateUi();
    QDBusInterface ui(QStringLiteral("org.modernime.UI1"), QStringLiteral("/org/modernime/UI1"), QStringLiteral("org.modernime.UI1"), QDBusConnection::sessionBus());
    ui.asyncCall(QStringLiteral("ShowSnapshot"), QString::fromUtf8(serialize(snapshot)));
}
void ModernEngine::selectCandidate(fcitx::InputContext *context, Candidate candidate) {
    auto *current = state(context);
    const auto advance = advanceComposition(current->selected_text, current->buffer, candidate);
    if (!advance) return;
    QDBusInterface service(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"), QStringLiteral("org.modernime.Service1"), QDBusConnection::sessionBus());
    service.asyncCall(QStringLiteral("RecordInputPreference"), QString::fromUtf8(advance->selected_reading), QString::fromUtf8(inputFeatures(context, current).scope), candidate.literal);
    if (!candidate.literal) service.asyncCall(QStringLiteral("RecordSelection"), QString::fromUtf8(advance->selected_reading), QString::fromUtf8(candidate.text));
    if (!advance->complete) {
        current->selected_text = advance->composed_text;
        current->selected_reading += advance->selected_reading;
        current->buffer = advance->remaining_input;
        update(context);
        return;
    }
    if (!current->selected_text.empty()) {
        service.asyncCall(QStringLiteral("RecordSelection"), QString::fromUtf8(current->selected_reading + advance->selected_reading), QString::fromUtf8(advance->composed_text));
    }
    context->commitString(withContextSpacing(context, language_.normalizeForCommit(advance->composed_text), config_.insert_spacing));
    clear(context);
}
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
InputMode ModernEngine::effectiveInputMode(const CompositionState *current) const {
    return config_.input_mode == InputMode::Auto && current && current->manual_chinese ? InputMode::Chinese : config_.input_mode;
}
InputContextFeatures ModernEngine::inputFeatures(fcitx::InputContext *context, const CompositionState *current) const {
    InputContextFeatures result;
    result.mode = effectiveInputMode(current);
    result.scope = context->program();
    std::transform(result.scope.begin(), result.scope.end(), result.scope.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    const auto flags = context->capabilityFlags();
    result.terminal = flags.test(fcitx::CapabilityFlag::Terminal);
    result.url = flags.test(fcitx::CapabilityFlag::Url);
    result.email = flags.test(fcitx::CapabilityFlag::Email);
    result.code_context = current && current->literal_prefix;
    const auto &surrounding = context->surroundingText();
    if (!result.code_context && surrounding.isValid()) {
        const auto previous = characterBeforeCursor(surrounding);
        result.code_context = previous < 128 && std::string_view("/._-@:$=>`([{\\").find(static_cast<char>(previous)) != std::string_view::npos;
    }
    return result;
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
    if (isControlKey(event.rawKey()) || isControlKey(key)) {
        if (!event.isRelease()) {
            current->control_pressed = true;
            current->control_used = false;
            return;
        }
        const bool shortPress = current->control_pressed && !current->control_used;
        current->control_pressed = false;
        current->control_used = false;
        if (shortPress && config_.control_mode_toggle && config_.input_mode == InputMode::Auto) {
            current->manual_chinese = !current->manual_chinese;
            current->literal_passthrough = false;
            if (!current->voice_session_id.empty()) cancelVoice(context);
            if (!current->buffer.empty() || !current->selected_text.empty()) update(context);
            else clear(context);
            context->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
        }
        return;
    }
    if (!event.isRelease() && current->control_pressed) current->control_used = true;
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
    if (current->literal_passthrough) {
        const auto text = fcitx::Key::keySymToUTF8(key.sym());
        const bool wordCharacter = key.isLAZ() || key.isUAZ() || key.digitSelection() >= 0 || key.check(FcitxKey_BackSpace) ||
                                   (text.size() == 1 && std::string_view("._-/@:+#>\\").find(text.front()) != std::string_view::npos);
        if (!wordCharacter) current->literal_passthrough = false;
        return;
    }
    auto candidateList = context->inputPanel().candidateList();
    const auto candidateData = [](const std::shared_ptr<fcitx::CandidateList> &list, int index) -> const Candidate * {
        if (!list || index < 0 || index >= list->size()) return nullptr;
        const auto *candidate = dynamic_cast<const ModernCandidate *>(&list->candidate(index));
        return candidate ? &candidate->candidate() : nullptr;
    };
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
        if (!current->buffer.empty()) {
            const bool fluentLiteral = effectiveInputMode(current) == InputMode::Auto && !current->candidates.empty() && current->candidates.front().literal;
            QDBusInterface service(QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"), QStringLiteral("org.modernime.Service1"), QDBusConnection::sessionBus());
            service.asyncCall(QStringLiteral("RecordInputPreference"), QString::fromUtf8(current->buffer), QString::fromUtf8(inputFeatures(context, current).scope), true);
            context->commitString(withContextSpacing(context, language_.normalizeForCommit(current->selected_text + current->buffer), config_.insert_spacing));
            clear(context);
            if (fluentLiteral) context->forwardKey(event.rawKey(), false, event.time());
            event.filterAndAccept();
            return;
        }
        if (!current->voice_session_id.empty()) {
            if (commitVoiceResult(context)) clear(context);
            // A stale review cannot be allowed to submit the surrounding
            // application (for example, Codex's send shortcut).
            event.filterAndAccept();
        }
        return;
    }
    if (effectiveInputMode(current) == InputMode::English) {
        if (!current->buffer.empty()) clear(context);
        return;
    }
    // Letters always join the current token. Developer punctuation joins only
    // after a token has started; a leading path/option marker is forwarded to
    // the application and makes the next token literal.
    if (isLiteralWordCharacter(key)) {
        const bool letter = key.isLAZ() || key.isUAZ();
        if (current->buffer.empty() && !letter) {
            if (effectiveInputMode(current) == InputMode::Auto && startsLiteralPrefix(key)) current->literal_prefix = true;
            return;
        }
        const auto text = fcitx::Key::keySymToUTF8(key.sym());
        const auto *recommended = candidateData(candidateList, 0);
        const bool strongJoin = text.size() == 1 && std::string_view("_@+#>").find(text.front()) != std::string_view::npos;
        const bool joinsToken = letter || strongJoin || (recommended && recommended->literal);
        if (joinsToken) {
            if (!current->voice_session_id.empty()) cancelVoice(context);
            appendToBuffer(&current->buffer, key, shiftedLetter);
            update(context, true);
            event.filterAndAccept();
            return;
        }
    }
    if (current->buffer.empty()) {
        if (key.digitSelection() >= 0 && effectiveInputMode(current) == InputMode::Auto) current->literal_prefix = true;
        else if (key.check(FcitxKey_BackSpace) || key.check(FcitxKey_space)) current->literal_prefix = false;
        return;
    }
    if (key.check(FcitxKey_BackSpace)) { current->buffer.pop_back(); if (current->buffer.empty()) clear(context); else update(context); event.filterAndAccept(); return; }
    if (key.check(FcitxKey_space) && candidateList && candidateList->size()) {
        const int selected = std::max(0, candidateList->cursorIndex());
        const auto *choice = candidateData(candidateList, selected);
        const bool literal = choice && choice->literal;
        candidateList->candidate(selected).select(context);
        if (literal && effectiveInputMode(current) == InputMode::Auto) context->commitString(" ");
        event.filterAndAccept();
        return;
    }
    const int selection = key.digitSelection();
    if (selection >= 0 && language_.shouldAppendDigit(current->buffer, inputFeatures(context, current))) {
        appendToBuffer(&current->buffer, key);
        update(context, true);
        event.filterAndAccept();
        return;
    }
    if (selection >= 0 && candidateList && selection < candidateList->size()) { candidateList->candidate(selection).select(context); event.filterAndAccept(); return; }
    if (key.check(FcitxKey_apostrophe)) { appendToBuffer(&current->buffer, key); update(context, true); event.filterAndAccept(); return; }
    if (isAsciiPunctuation(key) && candidateList && candidateList->size()) {
        const int selected = std::max(0, candidateList->cursorIndex());
        candidateList->candidate(selected).select(context);
        context->forwardKey(event.rawKey(), false, event.time());
        event.filterAndAccept();
        return;
    }
    if (key.hasModifier()) return;
}
} // namespace modernime
