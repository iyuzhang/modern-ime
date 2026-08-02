#pragma once
#include "contracts/contracts.h"
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QVariantList>
class QScreen;
namespace modernime {
class CandidateController final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.modernime.UI1")
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QString preedit READ preedit NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
    Q_PROPERTY(int selected READ selected NOTIFY changed)
    Q_PROPERTY(QString pageIndicator READ pageIndicator NOTIFY changed)
    Q_PROPERTY(int windowWidth READ windowWidth NOTIFY changed)
    Q_PROPERTY(int windowHeight READ windowHeight NOTIFY changed)
    Q_PROPERTY(int candidateHeight READ candidateHeight NOTIFY changed)
    Q_PROPERTY(bool vertical READ vertical NOTIFY changed)
    Q_PROPERTY(QString theme READ theme NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY changed)
    Q_PROPERTY(int cornerRadius READ cornerRadius NOTIFY changed)
    Q_PROPERTY(int backgroundOpacity READ backgroundOpacity NOTIFY changed)
    Q_PROPERTY(int windowX READ windowX NOTIFY changed)
    Q_PROPERTY(int windowY READ windowY NOTIFY changed)
public:
    explicit CandidateController(QObject *parent = nullptr);
    bool visible() const; QString preedit() const; QString mode() const; QVariantList candidates() const; int selected() const; QString pageIndicator() const; int windowWidth() const; int windowHeight() const; int candidateHeight() const; bool vertical() const; QString theme() const; int fontSize() const; int cornerRadius() const; int backgroundOpacity() const; int windowX() const; int windowY() const;
public slots:
    void ShowSnapshot(const QString &serialized);
    void Hide();
    void HideSession(const QString &sessionId);
    void ShowVoiceEvent(const QString &serialized);
    void ApplyConfig(qulonglong version, const QString &serialized);
    QString Status() const;
signals:
    void changed();
private:
    struct LogicalCursor {
        QPoint position{-1, -1};
        int height = 0;
        qreal scale = 1.0;
        QScreen *screen = nullptr;
    };
    LogicalCursor logicalCursor() const;
    void position();
    void loadAppearance();
    QString voiceErrorText() const;
    QString voiceErrorHint() const;
    CandidateSnapshot snapshot_;
    ConfigSnapshot appearance_;
    VoiceEvent voice_;
    QString hidden_voice_session_;
    QTimer error_timer_;
    bool visible_ = false;
    int x_ = 0;
    int y_ = 0;
};
}
