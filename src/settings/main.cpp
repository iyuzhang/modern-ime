#include "settings/settings_controller.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlContext>
#include <QQuickView>
#include <QTimer>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Modern IME 设置"));
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("smoke-test"), QStringLiteral("Load and render the settings window, then exit")});
    parser.addOption({QStringLiteral("smoke-test-model-installer"),
                      QStringLiteral("Run the configured model installer and verify asynchronous completion")});
    parser.process(app);

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#111318")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#f4f7ff")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#10141d")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#1a1d25")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#f4f7ff")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#293247")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#f4f7ff")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#5a8dee")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#9aa8c2")));
    app.setPalette(palette);

    modernime::SettingsController controller;
    QQuickView view;
    view.rootContext()->setContextProperty(QStringLiteral("settings"), &controller);
    view.setSource(QUrl(QStringLiteral("qrc:/qml/SettingsWindow.qml")));
    if (view.status() == QQuickView::Error || !view.rootObject()) return 2;
    view.setTitle(QStringLiteral("Modern IME"));
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1120, 720);
    view.show();

    if (parser.isSet(QStringLiteral("smoke-test-model-installer"))) {
        QObject::connect(&controller, &modernime::SettingsController::voiceModelInstallChanged, &app, [&] {
            if (!controller.voiceModelInstallRunning() &&
                controller.voiceModelInstallStatus().contains(QStringLiteral("安装完成"))) {
                app.exit(0);
            }
        });
        if (!controller.installVoiceModel()) return 4;
        QTimer::singleShot(3'000, &app, [&] { app.exit(5); });
    } else if (parser.isSet(QStringLiteral("smoke-test"))) {
        QTimer::singleShot(50, &app, [&] {
            app.exit(view.isVisible() && view.width() == 1120 && view.height() == 720 &&
                             !controller.voiceModelInstallRunning()
                         ? 0
                         : 3);
        });
    }
    return app.exec();
}
