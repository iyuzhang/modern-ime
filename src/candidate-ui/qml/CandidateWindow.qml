import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root
    width: controller.windowWidth
    height: controller.vertical
            ? Math.min(420, Math.max(controller.windowHeight, mascotTopInset + verticalHeader.implicitHeight + candidateList.contentHeight + 32))
            : controller.windowHeight

    function mascotSourceFor(theme) {
        if (theme === "starlight") return "qrc:/themes/anime-mascot.png"
        if (theme === "moon-rabbit") return "qrc:/themes/lunar-rabbit-mascot.png"
        if (theme === "mint-cat") return "qrc:/themes/mint-cat-mascot.png"
        if (theme === "berry-bear") return "qrc:/themes/strawberry-bear-mascot.png"
        return ""
    }
    function usesClassicSway(theme) {
        return theme === "starlight" || theme === "moon-rabbit" || theme === "berry-bear"
    }
    readonly property string mascotSource: mascotSourceFor(controller.theme)
    readonly property rect mascotClipRect: controller.theme === "starlight"
                                             ? Qt.rect(300, 70, 650, 1100)
                                             : Qt.rect(0, 0, 1254, 1254)
    readonly property bool showMascot: mascotSource.length > 0
    readonly property real mascotAspectRatio: controller.theme === "starlight" ? 650 / 1100 : 1
    readonly property int mascotTopInset: showMascot ? 66 : 0

    function paletteFor(theme) {
        const palettes = {
            midnight: { background: "#1a1f29", border: "#51627b", accent: "#8ab4f8", preedit: "#b8c2d1", muted: "#91a4ba", candidate: "#d7deea", selected: "#ffffff" },
            aurora: { background: "#102728", border: "#5c9ee8", accent: "#78e7c0", preedit: "#c7eee1", muted: "#93bdb1", candidate: "#e1f7ef", selected: "#ffffff" },
            cloud: { background: "#f7f9fc", border: "#cbd5e1", accent: "#2563a9", preedit: "#475569", muted: "#64748b", candidate: "#1e293b", selected: "#0f172a" },
            ink: { background: "#261f1d", border: "#73594d", accent: "#d9aa6c", preedit: "#e2c7a7", muted: "#b49b86", candidate: "#ecdcca", selected: "#fff8ed" },
            starlight: { background: "#fff7f4", border: "#e8a0b3", accent: "#c65373", preedit: "#765161", muted: "#ad7d8c", candidate: "#49333e", selected: "#961f49" },
            sakura: { background: "#fff5f8", border: "#efb6c8", accent: "#d85e83", preedit: "#9f6277", muted: "#b87b92", candidate: "#5a3544", selected: "#c83f68" },
            matcha: { background: "#f4f9ed", border: "#b8d597", accent: "#5f9258", preedit: "#668060", muted: "#8ca984", candidate: "#31553a", selected: "#356a45" },
            lavender: { background: "#f8f5ff", border: "#cbbcf3", accent: "#8169d1", preedit: "#766b9d", muted: "#998cbe", candidate: "#3e346a", selected: "#6047aa" },
            "peach-soda": { background: "#f1fbff", border: "#a9ddeb", accent: "#319bc6", preedit: "#537d91", muted: "#79a6b9", candidate: "#2d5467", selected: "#176f99" },
            "moon-rabbit": { background: "#f9f6ff", border: "#d9c8f5", accent: "#8d6fca", preedit: "#76658e", muted: "#a398b7", candidate: "#3e345a", selected: "#5e4798" },
            "mint-cat": { background: "#f2fcf5", border: "#b5e1c4", accent: "#4d9d72", preedit: "#5d806c", muted: "#7eae8f", candidate: "#2f624b", selected: "#256d49" },
            "berry-bear": { background: "#fff5f1", border: "#f2bda8", accent: "#db7757", preedit: "#966759", muted: "#be8b7e", candidate: "#613e37", selected: "#b94d40" }
        }
        return palettes[theme] || palettes.midnight
    }
    function motifFor(theme) {
        if (theme === "sakura") return "✿"
        if (theme === "matcha") return "● ● ●"
        if (theme === "lavender") return "✦"
        if (theme === "peach-soda") return "♡"
        return ""
    }
    function translucent(color, opacity) {
        const source = Qt.color(color)
        return Qt.rgba(source.r, source.g, source.b, opacity)
    }

    readonly property var palette: paletteFor(controller.theme)
    readonly property string motif: motifFor(controller.theme)
    readonly property color backgroundColor: palette.background
    readonly property color borderColor: palette.border
    readonly property color accentColor: palette.accent
    readonly property color preeditColor: palette.preedit
    readonly property color mutedColor: palette.muted
    readonly property color candidateColor: palette.candidate
    readonly property color selectedColor: palette.selected

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

        Text {
            visible: root.motif.length > 0
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: root.motif
            color: root.translucent(root.accentColor, 0.13)
            font.pixelSize: controller.theme === "matcha" ? 14 : 36
            font.bold: true
            z: 0
        }

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
        width: 112
        height: parent.height
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        z: 1

        Image {
            id: mascot
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: height * root.mascotAspectRatio
            height: Math.min(parent.height - 4, 126)
            source: root.mascotSource
            sourceClipRect: root.mascotClipRect
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
            transformOrigin: Item.Bottom
            property real idleOffsetX: 0
            property real idleOffsetY: 0
            property string animationTheme: controller.theme
            property bool listening: controller.mode.indexOf("聆听") !== -1
            property bool idle: mascot.visible && !mascot.listening
            transform: Translate { x: mascot.idleOffsetX; y: mascot.idleOffsetY }

            ParallelAnimation {
                running: mascot.idle && root.usesClassicSway(mascot.animationTheme)
                loops: Animation.Infinite
                SequentialAnimation {
                    NumberAnimation { target: mascot; property: "idleOffsetY"; from: 0; to: -4; duration: 1200; easing.type: Easing.InOutSine }
                    NumberAnimation { target: mascot; property: "idleOffsetY"; from: -4; to: 0; duration: 1200; easing.type: Easing.InOutSine }
                }
                SequentialAnimation {
                    NumberAnimation { target: mascot; property: "rotation"; from: -2; to: 2; duration: 1600; easing.type: Easing.InOutSine }
                    NumberAnimation { target: mascot; property: "rotation"; from: 2; to: -2; duration: 1600; easing.type: Easing.InOutSine }
                }
            }

            ParallelAnimation {
                running: mascot.idle && mascot.animationTheme === "mint-cat"
                loops: Animation.Infinite
                SequentialAnimation {
                    NumberAnimation { target: mascot; property: "idleOffsetX"; from: 0; to: -4; duration: 520; easing.type: Easing.InOutSine }
                    PauseAnimation { duration: 260 }
                    NumberAnimation { target: mascot; property: "idleOffsetX"; from: -4; to: 3; duration: 650; easing.type: Easing.InOutSine }
                    NumberAnimation { target: mascot; property: "idleOffsetX"; from: 3; to: 0; duration: 420; easing.type: Easing.InOutSine }
                    PauseAnimation { duration: 520 }
                }
                SequentialAnimation {
                    NumberAnimation { target: mascot; property: "rotation"; from: 0; to: -4; duration: 520; easing.type: Easing.InOutSine }
                    PauseAnimation { duration: 260 }
                    NumberAnimation { target: mascot; property: "rotation"; from: -4; to: 3; duration: 650; easing.type: Easing.InOutSine }
                    NumberAnimation { target: mascot; property: "rotation"; from: 3; to: 0; duration: 420; easing.type: Easing.InOutSine }
                    PauseAnimation { duration: 520 }
                }
            }

            SequentialAnimation {
                running: mascot.visible && mascot.listening
                loops: Animation.Infinite
                NumberAnimation { target: mascot; property: "scale"; from: 1; to: 1.06; duration: 680; easing.type: Easing.InOutSine }
                NumberAnimation { target: mascot; property: "scale"; from: 1.06; to: 1; duration: 680; easing.type: Easing.InOutSine }
            }
            function resetIdlePose() {
                idleOffsetX = 0
                idleOffsetY = 0
                rotation = 0
                scale = 1
            }
            onVisibleChanged: {
                if (!visible) resetIdlePose()
            }
            onAnimationThemeChanged: resetIdlePose()
            onListeningChanged: resetIdlePose()
        }
    }
}
