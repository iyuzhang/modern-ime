import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    width: controller.windowWidth
    height: controller.vertical
            ? Math.min(420, Math.max(controller.windowHeight, mascotTopInset + verticalHeader.implicitHeight + candidateList.contentHeight + 32))
            : controller.windowHeight

    readonly property bool showMascot: controller.theme === "starlight"
    readonly property int mascotTopInset: showMascot ? 36 : 0

    function themeColor(midnight, aurora, cloud, ink, starlight) {
        if (controller.theme === "aurora") return aurora
        if (controller.theme === "cloud") return cloud
        if (controller.theme === "ink") return ink
        if (controller.theme === "starlight") return starlight
        return midnight
    }
    function translucent(color, opacity) {
        const source = Qt.color(color)
        return Qt.rgba(source.r, source.g, source.b, opacity)
    }

    readonly property color backgroundColor: themeColor("#1a1f29", "#102728", "#f7f9fc", "#261f1d", "#fff7f4")
    readonly property color borderColor: themeColor("#1affffff", "#5c9ee8", "#cbd5e1", "#73594d", "#e8a0b3")
    readonly property color accentColor: themeColor("#8ab4f8", "#78e7c0", "#2563a9", "#d9aa6c", "#c65373")
    readonly property color preeditColor: themeColor("#b8c2d1", "#c7eee1", "#475569", "#e2c7a7", "#765161")
    readonly property color mutedColor: themeColor("#91a4ba", "#93bdb1", "#64748b", "#b49b86", "#ad7d8c")
    readonly property color candidateColor: themeColor("#d7deea", "#e1f7ef", "#1e293b", "#ecdcca", "#49333e")
    readonly property color selectedColor: themeColor("#ffffff", "#ffffff", "#0f172a", "#fff8ed", "#961f49")

    Rectangle {
        id: candidateSurface
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: controller.vertical ? root.height - root.mascotTopInset : controller.candidateHeight
        radius: controller.cornerRadius
        color: root.translucent(root.backgroundColor, controller.backgroundOpacity / 100)
        border.width: 1
        border.color: root.borderColor

    Row {
        id: horizontalContent
        visible: !controller.vertical
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 12
        anchors.rightMargin: root.showMascot ? mascotSlot.width + 8 : 12
        spacing: 12

        Text {
            text: controller.mode
            visible: text.length > 0
            color: root.accentColor
            font.pixelSize: Math.max(12, controller.fontSize - 1)
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            text: controller.preedit
            color: root.preeditColor
            font.pixelSize: Math.max(12, controller.fontSize - 1)
            verticalAlignment: Text.AlignVCenter
            width: Math.min(140, implicitWidth)
            elide: Text.ElideRight
        }
        Text {
            text: controller.pageIndicator
            visible: text.length > 0
            color: root.mutedColor
            font.pixelSize: Math.max(11, controller.fontSize - 3)
            verticalAlignment: Text.AlignVCenter
        }
        Repeater {
            model: controller.candidates
            delegate: Row {
                spacing: 5
                Text {
                    text: modelData.number
                    color: root.mutedColor
                    font.pixelSize: Math.max(11, controller.fontSize - 2)
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    text: modelData.text
                    color: index === controller.selected ? root.selectedColor : root.candidateColor
                    font.pixelSize: controller.fontSize + 1
                    font.bold: index === controller.selected
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    text: modelData.annotation
                    color: root.mutedColor
                    font.pixelSize: Math.max(11, controller.fontSize - 3)
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    Column {
        visible: controller.vertical
        anchors.fill: parent
        anchors.margins: 12
        anchors.rightMargin: root.showMascot ? mascotSlot.width + 8 : 12
        spacing: 8

        Row {
            id: verticalHeader
            spacing: 12
            Text {
                text: controller.mode
                visible: text.length > 0
                color: root.accentColor
                font.pixelSize: Math.max(12, controller.fontSize - 1)
                font.bold: true
            }
            Text {
                text: controller.preedit
                color: root.preeditColor
                font.pixelSize: Math.max(12, controller.fontSize - 1)
            }
            Text {
                text: controller.pageIndicator
                visible: text.length > 0
                color: root.mutedColor
                font.pixelSize: Math.max(11, controller.fontSize - 3)
            }
        }
        ListView {
            id: candidateList
            width: parent.width
            height: Math.min(contentHeight, Math.max(120, 372 - verticalHeader.implicitHeight))
            clip: true
            model: controller.candidates
            delegate: Item {
                width: parent.width
                height: Math.max(30, controller.fontSize + 9)
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Text {
                        text: modelData.number
                        color: root.mutedColor
                        font.pixelSize: Math.max(11, controller.fontSize - 2)
                    }
                    Text {
                        text: modelData.text
                        color: index === controller.selected ? root.selectedColor : root.candidateColor
                        font.pixelSize: controller.fontSize + 1
                        font.bold: index === controller.selected
                    }
                    Text {
                        text: modelData.annotation
                        color: root.mutedColor
                        font.pixelSize: Math.max(11, controller.fontSize - 3)
                    }
                }
            }
        }
    }

    }

    Item {
        id: mascotSlot
        visible: root.showMascot
        width: 62
        height: parent.height
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        z: 1

        Image {
            id: mascot
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: height * 0.59
            height: Math.min(parent.height + 6, 88)
            source: "qrc:/themes/anime-mascot.png"
            sourceClipRect: Qt.rect(300, 70, 650, 1100)
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            transformOrigin: Item.Bottom
            property real floatOffset: 0
            property bool listening: controller.mode.indexOf("聆听") !== -1
            transform: Translate { y: mascot.floatOffset }

            SequentialAnimation on floatOffset {
                running: mascot.visible && !mascot.listening
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: -4; duration: 1200; easing.type: Easing.InOutSine }
                NumberAnimation { from: -4; to: 0; duration: 1200; easing.type: Easing.InOutSine }
            }
            SequentialAnimation on rotation {
                running: mascot.visible && !mascot.listening
                loops: Animation.Infinite
                NumberAnimation { from: -2; to: 2; duration: 1600; easing.type: Easing.InOutSine }
                NumberAnimation { from: 2; to: -2; duration: 1600; easing.type: Easing.InOutSine }
            }
            SequentialAnimation on scale {
                running: mascot.visible && mascot.listening
                loops: Animation.Infinite
                NumberAnimation { from: 1; to: 1.06; duration: 680; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1.06; to: 1; duration: 680; easing.type: Easing.InOutSine }
            }
            onVisibleChanged: {
                if (!visible) {
                    floatOffset = 0
                    rotation = 0
                    scale = 1
                }
            }
            onListeningChanged: {
                if (!listening) scale = 1
            }
        }
    }
}
