#pragma once
#include <QObject>
#include <QVariantList>
namespace modernime {
class SettingsController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString configJson READ configJson NOTIFY changed)
    Q_PROPERTY(QString diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(QVariantList lexemes READ lexemes NOTIFY changed)
    Q_PROPERTY(QVariantList microphones READ microphones NOTIFY changed)
    Q_PROPERTY(QVariantList voiceModels READ voiceModels NOTIFY changed)
public:
    explicit SettingsController(QObject *parent = nullptr);
    QString configJson() const; QString diagnostics() const; QVariantList lexemes() const; QVariantList microphones() const; QVariantList voiceModels() const;
public slots:
    void refresh();
    bool saveConfig(const QString &serialized);
    bool addLexeme(const QString &reading, const QString &output, bool pinned);
    bool deleteLexeme(qlonglong id);
    bool clearLearned();
    QString exportLexicon();
    bool importLexicon(const QString &serialized);
    bool repairFcitx();
signals:
    void changed();
    void notification(const QString &message);
private:
    QString config_json_;
    QString diagnostics_;
    QVariantList lexemes_;
    QVariantList microphones_;
    QVariantList voice_models_;
};
}
