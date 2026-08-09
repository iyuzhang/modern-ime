#include <QGuiApplication>
#include <QQmlComponent>
#include <QQuickView>
#include <QTimer>

#include <iostream>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QQuickView view;
    QQmlComponent component(view.engine());
    component.setData(R"qml(
        import QtQuick 2.15
        Rectangle {
            id: root
            width: 480
            height: 120
            color: "white"
            property alias enteredText: editor.text
            property int forwardedReturns: 0
            TextInput {
                id: editor
                anchors.fill: parent
                anchors.margins: 20
                focus: true
                font.pixelSize: 28
                color: "black"
                Keys.onReturnPressed: function(event) {
                    root.forwardedReturns += 1
                    event.accepted = true
                }
            }
        }
    )qml", QUrl(QStringLiteral("qrc:/modern-ime-fcitx-smoke.qml")));
    auto *root = component.create();
    if (!root) {
        std::cerr << "could not create the Qt input client\n";
        return 2;
    }
    view.setContent(QUrl(), &component, root);
    view.setTitle(QStringLiteral("Modern IME Fcitx smoke"));
    view.show();
    QTimer::singleShot(5'000, &app, [&] {
        std::cout << "RESULT=" << root->property("enteredText").toString().toStdString() << '\n';
        std::cout << "RETURN_COUNT=" << root->property("forwardedReturns").toInt() << '\n';
        app.quit();
    });
    return app.exec();
}
