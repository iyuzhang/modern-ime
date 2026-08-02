#pragma once

#include "contracts/contracts.h"
#include "contracts/xdg.h"
#include "language-core/language_core.h"

#include <QObject>
#include <QProcess>
#include <QHash>

namespace modernime {

class Service final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.modernime.Service1")
public:
    explicit Service(QObject *parent = nullptr);

public slots:
    QString GetConfig();
    bool UpdateConfig(const QString &serialized);
    QString ListLexemes(const QString &query);
    QString UpsertLexeme(const QString &serialized);
    bool DeleteLexeme(qlonglong id);
    bool ClearLearned();
    bool RecordSelection(const QString &reading, const QString &output);
    QString ExportLexicon();
    bool ImportLexicon(const QString &serialized);
    QString Diagnostics();
    QString ListMicrophones();
    bool RepairFcitx();
    bool StartVoice(const QString &sessionId, qulonglong focusGeneration);
    bool StopVoice(const QString &sessionId);
    bool CancelVoice(const QString &sessionId);
    QString GetVoiceResult(const QString &sessionId);
    void Shutdown();

signals:
    void ConfigChanged(qulonglong version, const QString &serialized);
    void VoiceUpdated(const QString &serialized);

private slots:
    void onVoiceWorkerFinished(int exitCode, QProcess::ExitStatus status);
    void onVoiceEvent(const QString &serialized);

private:
    bool loadConfig();
    bool saveConfig();
    bool ensureVoiceWorker();
    bool selectedMicrophoneAvailable() const;
    bool validHotkeys(const ConfigSnapshot &config) const;
    void publishVoiceEvent(const VoiceEvent &event);
    QString lexemeJson(const Lexeme &lexeme) const;
    XdgPaths paths_;
    LanguageCore language_;
    ConfigSnapshot config_;
    QProcess voice_worker_;
    QString voice_worker_source_;
    QString voice_worker_model_;
    QHash<QString, QString> voice_results_;
};

} // namespace modernime
