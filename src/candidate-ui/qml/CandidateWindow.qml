import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    width: controller.windowWidth
    height: controller.vertical ? Math.min(420, 48 + candidateList.contentHeight) : 52
    radius: 10
    color: Qt.rgba(0.10, 0.12, 0.16, 0.96)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.10)

    Row {
        id: horizontalContent
        visible: !controller.vertical
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 12
        spacing: 12

        Text {
            text: controller.mode
            visible: text.length > 0
            color: "#8ab4f8"
            font.pixelSize: 14
            font.bold: true
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            text: controller.preedit
            color: "#b8c2d1"
            font.pixelSize: 14
            verticalAlignment: Text.AlignVCenter
            width: Math.min(140, implicitWidth)
            elide: Text.ElideRight
        }
        Text {
            text: controller.pageIndicator
            visible: text.length > 0
            color: "#91a4ba"
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
        }
        Repeater {
            model: controller.candidates
            delegate: Row {
                spacing: 5
                Text { text: modelData.number; color: "#91a4ba"; font.pixelSize: 13 }
                Text { text: modelData.text; color: index === controller.selected ? "#ffffff" : "#d7deea"; font.pixelSize: 16; font.bold: index === controller.selected }
                Text { text: modelData.annotation; color: "#91a4ba"; font.pixelSize: 12 }
            }
        }
    }

    Column {
        visible: controller.vertical
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Row {
            spacing: 12
            Text { text: controller.mode; visible: text.length > 0; color: "#8ab4f8"; font.bold: true }
            Text { text: controller.preedit; color: "#b8c2d1" }
            Text { text: controller.pageIndicator; visible: text.length > 0; color: "#91a4ba" }
        }
        ListView {
            id: candidateList
            width: parent.width
            height: Math.min(contentHeight, 360)
            clip: true
            model: controller.candidates
            delegate: Row {
                width: parent.width
                spacing: 8
                Text { text: modelData.number; color: "#91a4ba" }
                Text { text: modelData.text; color: index === controller.selected ? "white" : "#d7deea"; font.pixelSize: 16 }
                Text { text: modelData.annotation; color: "#91a4ba" }
            }
        }
    }
}
