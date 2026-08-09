#include "settings/settings_controller.h"
#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
namespace modernime {
namespace { QDBusInterface service() { return {QStringLiteral("org.modernime.Service1"), QStringLiteral("/org/modernime/Service1"), QStringLiteral("org.modernime.Service1")}; } }
SettingsController::SettingsController(QObject *parent) : QObject(parent) {
    voice_model_installer_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&voice_model_installer_, &QProcess::readyReadStandardOutput, this, [this] {
        const auto output = QString::fromUtf8(voice_model_installer_.readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) {
            const auto lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            voice_model_install_status_ = lines.isEmpty() ? output : lines.constLast().trimmed();
            emit voiceModelInstallChanged();
        }
    });
    connect(&voice_model_installer_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        voice_model_install_status_ = QStringLiteral("无法启动语音模型安装程序：%1").arg(voice_model_installer_.errorString());
        emit voiceModelInstallChanged();
    });
    connect(&voice_model_installer_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                const bool success = exitStatus == QProcess::NormalExit && exitCode == 0;
                voice_model_install_status_ = success
                    ? QStringLiteral("语音模型安装完成，可以选择本地语音模型了。")
                    : QStringLiteral("语音模型安装失败（退出码 %1），请检查网络连接后重试。").arg(exitCode);
                refresh();
                emit voiceModelInstallChanged();
                emit notification(voice_model_install_status_);
            });
    refresh();
}
QString SettingsController::configJson() const { return config_json_; } QString SettingsController::diagnostics() const { return diagnostics_; } QVariantList SettingsController::lexemes() const { return lexemes_; } QVariantList SettingsController::microphones() const { return microphones_; } QVariantList SettingsController::voiceModels() const { return voice_models_; }
bool SettingsController::voiceModelInstallRunning() const { return voice_model_installer_.state() != QProcess::NotRunning; }
QString SettingsController::voiceModelInstallStatus() const { return voice_model_install_status_; }
void SettingsController::refresh() { auto api = service(); const auto config = api.call(QStringLiteral("GetConfig")); const auto diagnostics = api.call(QStringLiteral("Diagnostics")); const auto lexemes = api.call(QStringLiteral("ListLexemes"), QString{}); const auto microphones = api.call(QStringLiteral("ListMicrophones")); const auto voiceModels = api.call(QStringLiteral("ListVoiceModels")); if (config.type() == QDBusMessage::ReplyMessage) config_json_ = config.arguments().at(0).toString(); if (diagnostics.type() == QDBusMessage::ReplyMessage) diagnostics_ = diagnostics.arguments().at(0).toString(); lexemes_.clear(); if (lexemes.type() == QDBusMessage::ReplyMessage) { const auto object = QJsonDocument::fromJson(lexemes.arguments().at(0).toString().toUtf8()).object(); for (const auto value : object.value(QStringLiteral("entries")).toArray()) lexemes_.push_back(value.toObject().toVariantMap()); } microphones_.clear(); if (microphones.type() == QDBusMessage::ReplyMessage) for (const auto value : QJsonDocument::fromJson(microphones.arguments().at(0).toString().toUtf8()).array()) microphones_.push_back(value.toObject().toVariantMap()); voice_models_.clear(); if (voiceModels.type() == QDBusMessage::ReplyMessage) for (const auto value : QJsonDocument::fromJson(voiceModels.arguments().at(0).toString().toUtf8()).array()) voice_models_.push_back(value.toObject().toVariantMap()); emit changed(); }
bool SettingsController::saveConfig(const QString &serialized) { auto api = service(); const auto reply = api.call(QStringLiteral("UpdateConfig"), serialized); const bool result = reply.type() == QDBusMessage::ReplyMessage && reply.arguments().at(0).toBool(); if (result) { refresh(); emit notification(QStringLiteral("设置已保存，快捷键已重载")); } else emit notification(QStringLiteral("未保存：服务不可用，或快捷键无效/重复")); return result; }
bool SettingsController::addLexeme(const QString &reading, const QString &output, bool pinned) { const auto input = QJsonDocument(QJsonObject{{QStringLiteral("reading"), reading}, {QStringLiteral("output"), output}, {QStringLiteral("pinned"), pinned}, {QStringLiteral("kind"), QStringLiteral("manual")}}).toJson(QJsonDocument::Compact); auto reply = service().call(QStringLiteral("UpsertLexeme"), QString::fromUtf8(input)); const bool result = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().at(0).toString().isEmpty(); if (result) refresh(); return result; }
bool SettingsController::deleteLexeme(qlonglong id) { auto reply = service().call(QStringLiteral("DeleteLexeme"), id); const bool result = reply.type() == QDBusMessage::ReplyMessage && reply.arguments().at(0).toBool(); if (result) refresh(); return result; }
bool SettingsController::clearLearned() { auto reply = service().call(QStringLiteral("ClearLearned")); const bool result = reply.type() == QDBusMessage::ReplyMessage && reply.arguments().at(0).toBool(); if (result) refresh(); return result; }
QString SettingsController::exportLexicon() { auto reply = service().call(QStringLiteral("ExportLexicon")); return reply.type() == QDBusMessage::ReplyMessage ? reply.arguments().at(0).toString() : QString{}; }
bool SettingsController::importLexicon(const QString &serialized) { auto reply = service().call(QStringLiteral("ImportLexicon"), serialized); const bool result = reply.type() == QDBusMessage::ReplyMessage && reply.arguments().at(0).toBool(); if (result) refresh(); return result; }
bool SettingsController::repairFcitx() { auto reply = service().call(QStringLiteral("RepairFcitx")); const bool result = reply.type() == QDBusMessage::ReplyMessage && reply.arguments().at(0).toBool(); emit notification(result ? QStringLiteral("已将 Modern IME 加回当前 Fcitx 组") : QStringLiteral("修复失败；请在诊断页查看状态")); return result; }
bool SettingsController::installVoiceModel() {
    if (voice_model_installer_.state() != QProcess::NotRunning) return false;
    const auto installer = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                   QStringLiteral("modern-ime/install-voice-model.sh"),
                                                   QStandardPaths::LocateFile);
    if (installer.isEmpty()) {
        voice_model_install_status_ = QStringLiteral("未找到语音模型安装程序，请重新安装 Modern IME。");
        emit voiceModelInstallChanged();
        return false;
    }
    voice_model_install_status_ = QStringLiteral("正在准备下载离线语音运行时和模型…");
    emit voiceModelInstallChanged();
    voice_model_installer_.start(installer, {});
    return true;
}
}
