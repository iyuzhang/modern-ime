#include "candidate-ui/candidate_controller.h"
#include "contracts/contracts.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QQuickView>
#include <QQmlContext>
#include <QScreen>
#include <QTimer>

#include <algorithm>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("modern-ime-candidate-ui"));
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("self-test"), QStringLiteral("Show a sample candidate window for visual verification")});
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Load and render one frame, then exit")});
    parser.process(app);

    modernime::CandidateController controller;
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.modernime.UI1")) ||
        !bus.registerObject(QStringLiteral("/org/modernime/UI1"), &controller, QDBusConnection::ExportAllSlots)) {
        return 1;
    }

    QQuickView view;
    view.setColor(Qt::transparent);
    auto windowFlags = Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool |
                       Qt::WindowDoesNotAcceptFocus;
    // On X11, keep the candidate popup above desktop-shell popups such as the
    // Plasma launcher. WindowStaysOnTopHint alone still leaves it managed by
    // the window manager and therefore below some shell-owned popup layers.
    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        windowFlags |= Qt::X11BypassWindowManagerHint;
    }
    view.setFlags(windowFlags);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/CandidateWindow.qml")));
    if (view.status() == QQuickView::Error || !view.rootObject()) return 2;

    QObject::connect(&controller, &modernime::CandidateController::changed, &view, [&] {
        view.setX(controller.windowX());
        view.setY(controller.windowY());
        if (controller.visible()) {
            if (!view.isVisible()) view.show();
            // A shell popup can be created after the candidate window. Raise
            // again on every snapshot without activating or stealing focus.
            view.raise();
        } else if (view.isVisible()) {
            view.hide();
        }
    });

    const bool visualSelfTest = parser.isSet(QStringLiteral("self-test"));
    const bool smokeTest = parser.isSet(QStringLiteral("smoke-test"));
    if (visualSelfTest || smokeTest) {
        modernime::CandidateSnapshot snapshot;
        snapshot.producer_id = "smoke-producer-before-restart";
        snapshot.sequence = 100;
        snapshot.preedit = "nihao";
        snapshot.candidates = {{"你好", "拼音", false, false}, {"nihao", "原文", false, true}};
        snapshot.cursor_x = 300;
        snapshot.cursor_y = 300;
        snapshot.cursor_height = 20;
        snapshot.visible = true;
        controller.ShowSnapshot(QString::fromUtf8(modernime::serialize(snapshot)));
        if (smokeTest) {
            snapshot.producer_id = "smoke-producer-after-restart";
            snapshot.sequence = 1;
            snapshot.preedit = "restart-ok";
            controller.ShowSnapshot(QString::fromUtf8(modernime::serialize(snapshot)));
        }
    }
    if (smokeTest) {
        QTimer::singleShot(50, &app, [&] {
            const auto scale = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
            const int expectedX = qRound(300 / std::max<qreal>(1.0, scale));
            const auto flags = view.flags();
            const bool x11LayeringOk = QGuiApplication::platformName() != QStringLiteral("xcb") ||
                                       flags.testFlag(Qt::X11BypassWindowManagerHint);
            app.exit(view.isVisible() && view.width() > 0 && view.height() > 0 &&
                             controller.windowX() == expectedX && controller.preedit() == QStringLiteral("restart-ok") &&
                             flags.testFlag(Qt::WindowStaysOnTopHint) && flags.testFlag(Qt::WindowDoesNotAcceptFocus) &&
                             x11LayeringOk
                         ? 0
                         : 3);
        });
    }
    return app.exec();
}
