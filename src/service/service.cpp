#include "service/service.h"

#include "contracts/logging.h"
#include "contracts/xdg.h"
#include "language-core/lexicon_store.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <fcitx-utils/key.h>

#include <algorithm>
#include <array>
#include <set>

namespace modernime {
namespace {
QString toQString(std::string_view value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
std::string toString(const QString &value) { return value.toUtf8().toStdString(); }
QString pathString(const std::filesystem::path &path) { return QString::fromStdString(path.string()); }
QJsonArray microphoneSources() {
    QProcess probe;
    probe.start(QStringLiteral("pactl"), {QStringLiteral("-f"), QStringLiteral("json"), QStringLiteral("list"), QStringLiteral("sources")});
    if (!probe.waitForStarted(250) || !probe.waitForFinished(1'000) || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) return {};
    const auto document = QJsonDocument::fromJson(probe.readAllStandardOutput());
    if (!document.isArray()) return {};
    QJsonArray sources;
    for (const auto value : document.array()) {
        const auto source = value.toObject();
        const auto name = source.value(QStringLiteral("name")).toString();
        if (name.isEmpty() || name.endsWith(QStringLiteral(".monitor"))) continue;
        auto description = source.value(QStringLiteral("description")).toString();
        if (description.isEmpty() || description == QStringLiteral("(null)")) {
            const auto properties = source.value(QStringLiteral("properties")).toObject();
            description = properties.value(QStringLiteral("device.description")).toString();
            if (description.isEmpty()) description = properties.value(QStringLiteral("node.nick")).toString();
        }
        if (description.isEmpty() || description == QStringLiteral("(null)")) description = name;
        sources.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("label"), description}, {QStringLiteral("state"), source.value(QStringLiteral("state")).toString()}});
    }
    return sources;
}
struct FcitxProfileInfo {
    struct Item { int index = 0; QString name; QString layout; };
    QStringList group_order;
    QHash<QString, QString> group_names;
    QHash<QString, QString> group_layouts;
    QHash<QString, int> next_item_index;
    QHash<QString, bool> group_contains_modernime;
    QHash<QString, QList<Item>> group_items;
    bool contains_modernime = false;
};
FcitxProfileInfo readFcitxProfile(const QString &path) {
    QFile file(path);
    FcitxProfileInfo result;
    if (!file.open(QIODevice::ReadOnly)) return result;
    const QRegularExpression groupHeader(QStringLiteral("^\\[Groups/([^/\\]]+)\\]$"));
    const QRegularExpression itemHeader(QStringLiteral("^\\[Groups/([^/\\]]+)/Items/([0-9]+)\\]$"));
    QString group;
    int item = -1;
    QString itemName;
    QString itemLayout;
    const auto flushItem = [&] {
        if (group.isEmpty() || item < 0 || itemName.isEmpty()) return;
        result.group_items[group].push_back({item, itemName, itemLayout});
        if (itemName == QStringLiteral("modernime")) { result.contains_modernime = true; result.group_contains_modernime.insert(group, true); }
    };
    for (const auto &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
        if (const auto match = itemHeader.match(line); match.hasMatch()) {
            flushItem();
            group = match.captured(1);
            item = match.captured(2).toInt();
            itemName.clear();
            itemLayout.clear();
            if (!result.group_order.contains(group)) result.group_order.push_back(group);
            result.next_item_index[group] = std::max(result.next_item_index.value(group), item + 1);
            continue;
        }
        if (const auto match = groupHeader.match(line); match.hasMatch()) {
            flushItem();
            group = match.captured(1);
            item = -1;
            itemName.clear();
            itemLayout.clear();
            if (!result.group_order.contains(group)) result.group_order.push_back(group);
            continue;
        }
        if (line.startsWith(QLatin1String("["))) { flushItem(); group.clear(); item = -1; itemName.clear(); itemLayout.clear(); continue; }
        if (group.isEmpty()) continue;
        if (line.startsWith(QLatin1String("Name="))) { if (item < 0) result.group_names.insert(group, line.sliced(5)); else itemName = line.sliced(5); }
        else if (line.startsWith(QLatin1String("Default Layout=")) && item < 0) result.group_layouts.insert(group, line.sliced(15));
        else if (line.startsWith(QLatin1String("Layout=")) && item >= 0) itemLayout = line.sliced(7);
    }
    flushItem();
    for (auto &items : result.group_items) std::sort(items.begin(), items.end(), [](const auto &left, const auto &right) { return left.index < right.index; });
    return result;
}
bool addModernImeToProfile(const QString &path, const QString &activeGroup) {
    const auto info = readFcitxProfile(path);
    QString target;
    for (const auto &group : info.group_order) if (info.group_names.value(group) == activeGroup) { target = group; break; }
    if (target.isEmpty() && !info.group_order.empty()) target = info.group_order.front();
    QString content;
    if (target.isEmpty()) {
        content = QStringLiteral("[Groups/0]\nName=Default\nDefault Layout=us\nDefaultIM=modernime\n\n[Groups/0/Items/0]\nName=keyboard-us\nLayout=\n\n[Groups/0/Items/1]\nName=modernime\nLayout=\n\n[GroupOrder]\n0=Default\n");
    } else {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return false;
        content = QString::fromUtf8(input.readAll());
        if (!info.group_contains_modernime.value(target)) {
            if (!content.endsWith(QLatin1Char('\n'))) content.append(QLatin1Char('\n'));
            content += QStringLiteral("\n[Groups/") + target + QStringLiteral("/Items/") + QString::number(info.next_item_index.value(target)) + QStringLiteral("]\nName=modernime\nLayout=\n");
        }
    }
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return false;
    output.write(content.toUtf8());
    return output.commit();
}
QString gvariantString(QString value) { return QLatin1Char('\'') + value.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('\''), QStringLiteral("\\'")) + QLatin1Char('\''); }
QString gvariantItems(const QList<FcitxProfileInfo::Item> &items) {
    QStringList values;
    for (const auto &item : items) if (!item.name.isEmpty()) values.push_back(QLatin1Char('(') + gvariantString(item.name) + QLatin1Char(',') + gvariantString(item.layout) + QLatin1Char(')'));
    return QLatin1Char('[') + values.join(QLatin1Char(',')) + QLatin1Char(']');
}
bool sourceAppearsAvailable(const QString &source) {
    if (source.isEmpty() || source.endsWith(QStringLiteral(".monitor"))) return false;

    QProcess probe;
    probe.start(QStringLiteral("pactl"), {QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("sources")});
    if (!probe.waitForStarted(250) || !probe.waitForFinished(1'000) || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        // PipeWire's PulseAudio compatibility server can be unavailable while
        // PipeWire itself is still starting. Let the worker provide the final
        // error in that transient case instead of rejecting a valid source.
        return true;
    }

    const auto lines = QString::fromUtf8(probe.readAllStandardOutput()).split(QLatin1Char('\n'));
    for (const auto &line : lines) {
        const auto columns = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (columns.size() >= 2 && columns.at(1) == source) return true;
    }
    return false;
}
}

Service::Service(QObject *parent) : QObject(parent), paths_(xdgPaths()), language_(paths_.data / "user.db") {
    ensureXdgDirectories(paths_); loadConfig();
    connect(&voice_worker_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &Service::onVoiceWorkerFinished);
    QDBusConnection::sessionBus().connect(QStringLiteral("org.modernime.VoiceWorker1"), QStringLiteral("/org/modernime/VoiceWorker1"), QStringLiteral("org.modernime.VoiceWorker1"), QStringLiteral("Event"), this, SLOT(onVoiceEvent(QString)));
}

bool Service::loadConfig() { QFile file(pathString(paths_.config / "config.json")); if (!file.exists()) return saveConfig(); if (!file.open(QIODevice::ReadOnly)) return false; const auto parsed = parseConfigSnapshot(file.readAll().toStdString()); if (!parsed) return false; config_ = *parsed; return true; }
bool Service::saveConfig() { config_.version++; QSaveFile file(pathString(paths_.config / "config.json")); if (!file.open(QIODevice::WriteOnly)) return false; file.write(QByteArray::fromStdString(serialize(config_))); return file.commit(); }
QString Service::GetConfig() { return toQString(serialize(config_)); }
bool Service::validHotkeys(const ConfigSnapshot &config) const {
    const std::array<std::string, 7> bindings{config.voice_hotkey, config.cancel_hotkey, config.commit_raw_hotkey, config.previous_page_hotkey, config.next_page_hotkey, config.previous_candidate_hotkey, config.next_candidate_hotkey};
    std::set<std::string> seen;
    for (const auto &binding : bindings) {
        const fcitx::Key key(binding);
        if (!key.isValid() || key.isModifier() || !seen.insert(key.normalize().toString()).second) return false;
    }
    return true;
}
bool Service::UpdateConfig(const QString &serialized) { const auto parsed = parseConfigSnapshot(toString(serialized)); if (!parsed || !validHotkeys(*parsed)) return false; auto updated = *parsed; updated.version = config_.version; config_ = std::move(updated); if (!saveConfig()) return false; QDBusInterface controller(QStringLiteral("org.fcitx.Fcitx5"), QStringLiteral("/controller"), QStringLiteral("org.fcitx.Fcitx.Controller1")); controller.asyncCall(QStringLiteral("ReloadAddonConfig"), QStringLiteral("modernime")); emit ConfigChanged(config_.version, toQString(serialize(config_))); return true; }
QString Service::lexemeJson(const Lexeme &lexeme) const { QJsonObject object; object.insert(QStringLiteral("id"), static_cast<qint64>(lexeme.id)); object.insert(QStringLiteral("reading"), toQString(lexeme.reading)); object.insert(QStringLiteral("output"), toQString(lexeme.output)); object.insert(QStringLiteral("language_tag"), toQString(lexeme.language_tag)); object.insert(QStringLiteral("kind"), toQString(lexeme.kind)); object.insert(QStringLiteral("pinned"), lexeme.pinned); object.insert(QStringLiteral("blocked"), lexeme.blocked); object.insert(QStringLiteral("base_weight"), lexeme.base_weight); object.insert(QStringLiteral("select_count"), static_cast<qint64>(lexeme.select_count)); object.insert(QStringLiteral("last_selected_at"), static_cast<qint64>(lexeme.last_selected_at)); return QJsonDocument(object).toJson(QJsonDocument::Compact); }
QString Service::ListLexemes(const QString &query) { QJsonArray entries; for (const auto &lexeme : language_.lexicon().list(toString(query))) entries.append(QJsonDocument::fromJson(lexemeJson(lexeme).toUtf8()).object()); return QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("modern-ime-lexeme-list")}, {QStringLiteral("version"), 1}, {QStringLiteral("entries"), entries}}).toJson(QJsonDocument::Compact); }
QString Service::UpsertLexeme(const QString &serialized) { const auto object = QJsonDocument::fromJson(serialized.toUtf8()).object(); Lexeme lexeme; lexeme.reading = toString(object.value(QStringLiteral("reading")).toString()); lexeme.output = toString(object.value(QStringLiteral("output")).toString()); lexeme.language_tag = toString(object.value(QStringLiteral("language_tag")).toString(QStringLiteral("und"))); lexeme.kind = toString(object.value(QStringLiteral("kind")).toString(QStringLiteral("manual"))); lexeme.pinned = object.value(QStringLiteral("pinned")).toBool(); lexeme.blocked = object.value(QStringLiteral("blocked")).toBool(); lexeme.base_weight = object.value(QStringLiteral("base_weight")).toDouble(); const auto persisted = language_.lexicon().upsert(std::move(lexeme)); if (persisted) language_.refreshUserLexicon(); return persisted ? lexemeJson(*persisted) : QString{}; }
bool Service::DeleteLexeme(qlonglong id) { const bool result = language_.lexicon().remove(id); if (result) language_.refreshUserLexicon(); return result; }
bool Service::ClearLearned() { const bool result = language_.lexicon().clearLearned(); if (result) language_.refreshUserLexicon(); return result; }
bool Service::RecordSelection(const QString &reading, const QString &output) { const bool result = language_.lexicon().recordSelection(toString(reading), toString(output)); if (result) language_.refreshUserLexicon(); return result; }
QString Service::ExportLexicon() { return toQString(language_.lexicon().exportJson()); }
bool Service::ImportLexicon(const QString &serialized) { std::string reason; const bool result = language_.lexicon().importJson(toString(serialized), &reason); if (result) language_.refreshUserLexicon(); else logError("service", reason); return result; }
QString Service::Diagnostics() {
    const auto config = paths_.config; const auto data = paths_.data; const auto state = paths_.state;
    QDBusInterface controller(QStringLiteral("org.fcitx.Fcitx5"), QStringLiteral("/controller"), QStringLiteral("org.fcitx.Fcitx.Controller1"));
    const auto addon = controller.call(QStringLiteral("AddonForIM"), QStringLiteral("modernime"));
    const auto profile = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/fcitx5/profile");
    const bool profileContainsModernIme = readFcitxProfile(profile).contains_modernime;
    QJsonObject result; result.insert(QStringLiteral("protocol_version"), static_cast<int>(kProtocolVersion)); result.insert(QStringLiteral("service_pid"), QCoreApplication::applicationPid()); result.insert(QStringLiteral("config_path"), pathString(config)); result.insert(QStringLiteral("data_path"), pathString(data)); result.insert(QStringLiteral("state_path"), pathString(state)); result.insert(QStringLiteral("lexicon_healthy"), language_.lexicon().healthy()); result.insert(QStringLiteral("lexicon_error"), toQString(language_.lexicon().lastError())); result.insert(QStringLiteral("voice_worker_running"), voice_worker_.state() != QProcess::NotRunning); result.insert(QStringLiteral("selected_microphone"), toQString(config_.microphone)); result.insert(QStringLiteral("selected_microphone_available"), selectedMicrophoneAvailable()); result.insert(QStringLiteral("model_id"), toQString(config_.model_id)); result.insert(QStringLiteral("fcitx_registers_modernime"), addon.type() == QDBusMessage::ReplyMessage && !addon.arguments().empty() && addon.arguments().front().toString() == QStringLiteral("modernime")); result.insert(QStringLiteral("profile_contains_modernime"), profileContainsModernIme);
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}
QString Service::ListMicrophones() { return QJsonDocument(microphoneSources()).toJson(QJsonDocument::Compact); }
bool Service::RepairFcitx() {
    const auto profilePath = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/fcitx5/profile");
    QDBusInterface controller(QStringLiteral("org.fcitx.Fcitx5"), QStringLiteral("/controller"), QStringLiteral("org.fcitx.Fcitx.Controller1"));
    const auto currentGroupReply = controller.call(QStringLiteral("CurrentInputMethodGroup"));
    const auto currentGroup = currentGroupReply.type() == QDBusMessage::ReplyMessage && !currentGroupReply.arguments().empty() ? currentGroupReply.arguments().front().toString() : QString{};
    const auto profile = readFcitxProfile(profilePath);
    if (!controller.isValid()) return addModernImeToProfile(profilePath, currentGroup);
    QString target;
    for (const auto &group : profile.group_order) if (profile.group_names.value(group) == currentGroup) { target = group; break; }
    if (target.isEmpty() && !profile.group_order.empty()) target = profile.group_order.front();
    if (target.isEmpty()) return false;
    auto items = profile.group_items.value(target);
    if (std::none_of(items.cbegin(), items.cend(), [](const auto &item) { return item.name == QStringLiteral("modernime"); })) items.push_back({profile.next_item_index.value(target), QStringLiteral("modernime"), {}});
    const auto command = QStandardPaths::findExecutable(QStringLiteral("gdbus"));
    if (command.isEmpty()) return false;
    QProcess process;
    process.start(command, {QStringLiteral("call"), QStringLiteral("--session"), QStringLiteral("--dest"), QStringLiteral("org.fcitx.Fcitx5"), QStringLiteral("--object-path"), QStringLiteral("/controller"), QStringLiteral("--method"), QStringLiteral("org.fcitx.Fcitx.Controller1.SetInputMethodGroupInfo"), currentGroup, profile.group_layouts.value(target, QStringLiteral("us")), gvariantItems(items)});
    if (!process.waitForStarted(500) || !process.waitForFinished(2'000) || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return false;
    controller.call(QStringLiteral("Save"));
    return true;
}
bool Service::ensureVoiceWorker() {
    if (voice_worker_.state() != QProcess::NotRunning && (voice_worker_source_ != toQString(config_.microphone) || voice_worker_model_ != toQString(config_.model_id))) {
        voice_worker_.terminate();
        if (!voice_worker_.waitForFinished(750)) {
            voice_worker_.kill();
            voice_worker_.waitForFinished(750);
        }
    }
    if (voice_worker_.state() == QProcess::NotRunning) {
        const auto worker = QStandardPaths::findExecutable(QStringLiteral("modern-ime-voice-worker"));
        const QString command = worker.isEmpty() ? QCoreApplication::applicationDirPath() + QStringLiteral("/modern-ime-voice-worker") : worker;
        voice_worker_.start(command, {QStringLiteral("--source"), toQString(config_.microphone), QStringLiteral("--model"), toQString(config_.model_id)});
        if (!voice_worker_.waitForStarted(500)) {
            logError("service", "voice worker did not start");
            return false;
        }
        voice_worker_source_ = toQString(config_.microphone);
        voice_worker_model_ = toQString(config_.model_id);
    }
    auto *busInterface = QDBusConnection::sessionBus().interface();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 2'000) {
        const auto registered = busInterface->isServiceRegistered(QStringLiteral("org.modernime.VoiceWorker1"));
        if (registered.isValid() && registered.value()) return true;
        QThread::msleep(25);
    }
    logError("service", "voice worker did not acquire its D-Bus name");
    return false;
}
bool Service::selectedMicrophoneAvailable() const { return sourceAppearsAvailable(toQString(config_.microphone)); }
void Service::publishVoiceEvent(const VoiceEvent &event) {
    const auto serialized = toQString(serialize(event));
    if (event.state == VoiceState::Review || event.state == VoiceState::Error) voice_results_.insert(QString::fromUtf8(event.session_id), serialized);
    QDBusInterface ui(QStringLiteral("org.modernime.UI1"), QStringLiteral("/org/modernime/UI1"), QStringLiteral("org.modernime.UI1"), QDBusConnection::sessionBus());
    ui.asyncCall(QStringLiteral("ShowVoiceEvent"), serialized);
    emit VoiceUpdated(serialized);
}
bool Service::StartVoice(const QString &sessionId, qulonglong focusGeneration) {
    voice_results_.remove(sessionId);
    const auto microphoneError = [this, &sessionId, focusGeneration] {
        VoiceEvent event;
        event.session_id = toString(sessionId);
        event.focus_generation = static_cast<uint64_t>(focusGeneration);
        event.state = VoiceState::Error;
        event.error = ErrorCode::AudioDeviceMissing;
        publishVoiceEvent(event);
    };
    if (!selectedMicrophoneAvailable()) {
        microphoneError();
        return false;
    }
    if (!ensureVoiceWorker()) {
        microphoneError();
        return false;
    }
    QDBusInterface worker(QStringLiteral("org.modernime.VoiceWorker1"), QStringLiteral("/org/modernime/VoiceWorker1"), QStringLiteral("org.modernime.VoiceWorker1"));
    const auto reply = worker.call(QStringLiteral("Start"), sessionId, focusGeneration);
    const bool started = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().empty() && reply.arguments().front().toBool();
    if (!started) microphoneError();
    return started;
}
bool Service::StopVoice(const QString &sessionId) { QDBusInterface worker(QStringLiteral("org.modernime.VoiceWorker1"), QStringLiteral("/org/modernime/VoiceWorker1"), QStringLiteral("org.modernime.VoiceWorker1")); const auto reply = worker.call(QStringLiteral("Stop"), sessionId); return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().empty() && reply.arguments().front().toBool(); }
bool Service::CancelVoice(const QString &sessionId) { QDBusInterface worker(QStringLiteral("org.modernime.VoiceWorker1"), QStringLiteral("/org/modernime/VoiceWorker1"), QStringLiteral("org.modernime.VoiceWorker1")); const auto reply = worker.call(QStringLiteral("Cancel"), sessionId); return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().empty() && reply.arguments().front().toBool(); }
QString Service::GetVoiceResult(const QString &sessionId) { return voice_results_.value(sessionId); }
void Service::Shutdown() { QCoreApplication::quit(); }
void Service::onVoiceWorkerFinished(int exitCode, QProcess::ExitStatus status) { logError("service", "voice worker exited: " + std::to_string(exitCode) + (status == QProcess::CrashExit ? " crash" : "")); }
void Service::onVoiceEvent(const QString &serialized) {
    const auto event = parseVoiceEvent(toString(serialized));
    if (!event) return;
    if (event->state == VoiceState::Error) logError("voice", errorCodeName(event->error.value_or(ErrorCode::InvalidRequest)));
    else if (event->state == VoiceState::Review) logInfo("voice", "review");
    publishVoiceEvent(*event);
}
} // namespace modernime
