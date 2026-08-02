import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#111318"
    focus: true
    property int page: 0
    property var config: ({})
    property string captureField: ""
    property string captureMessage: ""
    property var candidateThemes: [
        { id: "midnight", name: "午夜蓝", subtitle: "当前默认", background: "#1a1f29", border: "#51627b", accent: "#8ab4f8", preedit: "#b8c2d1", text: "#ffffff", muted: "#91a4ba" },
        { id: "aurora", name: "极光绿", subtitle: "清透、低调", background: "#102728", border: "#5c9ee8", accent: "#78e7c0", preedit: "#c7eee1", text: "#ffffff", muted: "#93bdb1" },
        { id: "cloud", name: "云雾白", subtitle: "明亮、专注", background: "#f7f9fc", border: "#cbd5e1", accent: "#2563a9", preedit: "#475569", text: "#0f172a", muted: "#64748b" },
        { id: "ink", name: "墨韵", subtitle: "温暖、沉静", background: "#261f1d", border: "#73594d", accent: "#d9aa6c", preedit: "#e2c7a7", text: "#fff8ed", muted: "#b49b86" }
    ]

    component DarkButton: Button {
        id: control
        implicitHeight: 36
        contentItem: Text {
            text: control.text
            color: control.enabled ? "#f4f7ff" : "#71809a"
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: control.down ? "#213a66" : (control.highlighted ? "#476fae" : (control.hovered ? "#2e4365" : "#242f43"))
            border.color: control.activeFocus ? "#9dc0ff" : "#455a7d"
            border.width: control.activeFocus ? 2 : 1
        }
    }
    component DarkSwitch: Switch {
        id: control
        implicitHeight: 34
        contentItem: Text {
            text: control.text
            color: "#f4f7ff"
            font: control.font
            leftPadding: control.indicator.width + 10
            verticalAlignment: Text.AlignVCenter
        }
        indicator: Rectangle {
            implicitWidth: 42
            implicitHeight: 24
            x: control.leftPadding
            y: parent.height / 2 - height / 2
            radius: height / 2
            color: control.checked ? "#5d8fe5" : "#4b566b"
            border.color: control.activeFocus ? "#b7d0ff" : "#65738c"
            Rectangle { width: 18; height: 18; radius: 9; anchors.verticalCenter: parent.verticalCenter; x: control.checked ? parent.width - width - 3 : 3; color: "#f4f7ff" }
        }
    }
    component DarkCheckBox: CheckBox {
        id: control
        implicitHeight: 32
        contentItem: Text {
            text: control.text
            color: "#f4f7ff"
            font: control.font
            leftPadding: control.indicator.width + 9
            verticalAlignment: Text.AlignVCenter
        }
        indicator: Rectangle {
            implicitWidth: 20
            implicitHeight: 20
            x: control.leftPadding
            y: parent.height / 2 - height / 2
            radius: 4
            color: control.checked ? "#5d8fe5" : "#202838"
            border.color: control.activeFocus ? "#b7d0ff" : "#71809a"
            Text { anchors.centerIn: parent; text: control.checked ? "✓" : ""; color: "white"; font.pixelSize: 16; font.bold: true }
        }
    }

    function microphoneIndex() {
        const selected = config.microphone || ""
        for (let i = 0; i < settings.microphones.length; ++i)
            if (settings.microphones[i].name === selected) return i
        return -1
    }
    function hotkeyLabel(value) {
        return (value || "未设置").replace("Control", "Ctrl").replace("bracketleft", "[").replace("bracketright", "]").replace("Page_Up", "PgUp").replace("Page_Down", "PgDn")
    }
    function selectedTheme() { return config.theme || "midnight" }
    function saveAppearance(field, value) {
        const next = JSON.parse(JSON.stringify(config))
        next[field] = value
        if (settings.saveConfig(JSON.stringify(next))) config = next
    }
    function capturedKey(event) {
        if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F35) return "F" + (event.key - Qt.Key_F1 + 1)
        const names = {}
        names[Qt.Key_Escape] = "Escape"; names[Qt.Key_Return] = "Return"; names[Qt.Key_Enter] = "KP_Enter"; names[Qt.Key_Space] = "space"
        names[Qt.Key_Left] = "Left"; names[Qt.Key_Right] = "Right"; names[Qt.Key_Up] = "Up"; names[Qt.Key_Down] = "Down"
        names[Qt.Key_PageUp] = "Page_Up"; names[Qt.Key_PageDown] = "Page_Down"; names[Qt.Key_Home] = "Home"; names[Qt.Key_End] = "End"
        names[Qt.Key_Backspace] = "BackSpace"; names[Qt.Key_Delete] = "Delete"; names[Qt.Key_Tab] = "Tab"; names[Qt.Key_BracketLeft] = "bracketleft"; names[Qt.Key_BracketRight] = "bracketright"
        if (names[event.key]) return names[event.key]
        if (event.text && /^[a-zA-Z0-9]$/.test(event.text)) return event.text.toLowerCase()
        return ""
    }
    function setCapturedHotkey(event) {
        if (!captureField) return
        const name = capturedKey(event)
        if (!name || event.key === Qt.Key_Control || event.key === Qt.Key_Alt || event.key === Qt.Key_Shift || event.key === Qt.Key_Meta) {
            captureMessage = "请按一个非修饰键，例如 F8、Ctrl+Enter 或 PageDown。"
            event.accepted = true
            return
        }
        const modifiers = []
        if (event.modifiers & Qt.ControlModifier) modifiers.push("Control")
        if (event.modifiers & Qt.AltModifier) modifiers.push("Alt")
        if (event.modifiers & Qt.MetaModifier) modifiers.push("Super")
        if ((event.modifiers & Qt.ShiftModifier) && !/^[a-z]$/.test(name)) modifiers.push("Shift")
        const binding = modifiers.concat([name]).join("+")
        const next = JSON.parse(JSON.stringify(config))
        next[captureField] = binding
        if (settings.saveConfig(JSON.stringify(next))) {
            config = next
            captureMessage = "已将此操作绑定为 " + hotkeyLabel(binding)
            captureField = ""
        } else {
            captureMessage = "未保存：快捷键必须有效且不能与其它操作重复。"
        }
        event.accepted = true
    }
    Keys.onPressed: function(event) { root.setCapturedHotkey(event) }

    Component.onCompleted: {
        config = JSON.parse(settings.configJson || "{}")
        settings.refresh()
    }
    Connections {
        target: settings
        function onChanged() { root.config = JSON.parse(settings.configJson || "{}") }
        function onNotification(message) { root.captureMessage = message }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 18

        Rectangle {
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            radius: 16
            color: "#1a1d25"
            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                Text { text: "Modern IME"; color: "#f4f7ff"; font.pixelSize: 24; font.bold: true }
                Text { text: "本地、私密的中英文与语音输入"; color: "#95a0b5"; width: 170; wrapMode: Text.Wrap }
                Repeater {
                    model: ["概览", "输入", "词库", "语音", "外观", "快捷键", "诊断"]
                    delegate: DarkButton {
                        width: 178
                        text: modelData
                        flat: true
                        highlighted: index === root.page
                        onClicked: root.page = index
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 16
            color: "#1a1d25"
            StackLayout {
                anchors.fill: parent
                anchors.margins: 32
                currentIndex: root.page

                Column {
                    spacing: 18
                    Text { text: "概览"; color: "white"; font.pixelSize: 28; font.bold: true }
                    Text { text: "服务状态与首次使用检查"; color: "#aeb8ca"; font.pixelSize: 16 }
                    Rectangle {
                        width: 620
                        height: 132
                        radius: 12
                        color: "#222733"
                        Column {
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 8
                            Text { text: "Modern IME 服务"; color: "#eaf0ff"; font.pixelSize: 18 }
                            Text { text: settings.diagnostics.length > 0 ? "已连接。可在下方测试键盘输入和麦克风。" : "未连接服务；请运行 systemctl --user start modern-ime-service"; color: "#99e2b1"; wrapMode: Text.Wrap }
                        }
                    }
                    DarkButton {
                        text: "修复 Fcitx 配置"
                        onClicked: settings.repairFcitx()
                    }
                    Text { text: "当在 Fcitx 配置中误删 Modern IME，或改动其它拼音引擎后无法切回时使用。它只会在当前组缺失时补回 Modern IME，不会重置、删除或重排其它输入法。"; color: "#b8c5dc"; width: 620; wrapMode: Text.Wrap }
                    TextArea {
                        width: 620
                        height: 160
                        placeholderText: "在这里试用输入法；启用后输入 nihao 并按 Space"
                        color: "white"
                        placeholderTextColor: "#9aa8c2"
                        background: Rectangle { radius: 10; color: "#111318"; border.color: "#35405a" }
                    }
                }

                Column {
                    spacing: 18
                    Text { text: "输入"; color: "white"; font.pixelSize: 28; font.bold: true }
                    DarkSwitch { text: "默认中文模式"; checked: root.config.chinese_mode === undefined || root.config.chinese_mode; onToggled: { root.config.chinese_mode = checked; settings.saveConfig(JSON.stringify(root.config)) } }
                    DarkSwitch { text: "中文与英文、数字之间插入空格"; checked: root.config.insert_spacing; onToggled: { root.config.insert_spacing = checked; settings.saveConfig(JSON.stringify(root.config)) } }
                    SpinBox { from: 3; to: 9; value: root.config.candidate_count || 7; editable: true; onValueModified: { root.config.candidate_count = value; settings.saveConfig(JSON.stringify(root.config)) } }
                }

                Column {
                    spacing: 14
                    Text { text: "词库"; color: "white"; font.pixelSize: 28; font.bold: true }
                    Row {
                        spacing: 8
                        TextField { id: reading; width: 190; placeholderText: "读音或触发词"; color: "#f4f7ff"; placeholderTextColor: "#9aa8c2"; background: Rectangle { radius: 7; color: "#10141d"; border.color: reading.activeFocus ? "#7fa9ff" : "#4a5875" } }
                        TextField { id: output; width: 240; placeholderText: "输出文本，例如 GitHub Actions"; color: "#f4f7ff"; placeholderTextColor: "#9aa8c2"; background: Rectangle { radius: 7; color: "#10141d"; border.color: output.activeFocus ? "#7fa9ff" : "#4a5875" } }
                        DarkCheckBox { id: pinned; text: "固定" }
                        DarkButton {
                            text: "添加"
                            onClicked: {
                                if (settings.addLexeme(reading.text, output.text, pinned.checked)) {
                                    reading.text = ""
                                    output.text = ""
                                }
                            }
                        }
                    }
                    ListView {
                        width: 700
                        height: 390
                        clip: true
                        model: settings.lexemes
                        delegate: Rectangle {
                            width: 680
                            height: 48
                            color: index % 2 ? "#202530" : "#1a1d25"
                            Row {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 16
                                Text { text: modelData.reading; width: 180; color: "#cbd6e8" }
                                Text { text: modelData.output; width: 270; color: "white" }
                                Text { text: modelData.pinned ? "固定" : modelData.kind; width: 80; color: "#8ab4f8" }
                                DarkButton { text: "删除"; onClicked: settings.deleteLexeme(modelData.id) }
                            }
                        }
                    }
                    DarkButton { text: "清空自动学习"; onClicked: settings.clearLearned() }
                }

                Column {
                    spacing: 16
                    Text { text: "语音"; color: "white"; font.pixelSize: 28; font.bold: true }
                    Text { text: "默认使用真实麦克风，绝不自动选取扬声器 monitor。语音模型必须本地安装并校验。"; color: "#aeb8ca"; width: 620; wrapMode: Text.Wrap }
                    Row {
                        spacing: 10
                        ComboBox {
                            id: microphone
                            width: 600
                            model: settings.microphones
                            textRole: "label"
                            valueRole: "name"
                            currentIndex: root.microphoneIndex()
                            displayText: currentIndex >= 0 ? (currentText + "  ·  " + currentValue) : "未选择麦克风"
                            onActivated: {
                                root.config.microphone = currentValue
                                settings.saveConfig(JSON.stringify(root.config))
                            }
                            contentItem: Text {
                                leftPadding: 12
                                rightPadding: 30
                                text: microphone.displayText
                                color: "#f4f7ff"
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle { radius: 8; color: "#10141d"; border.color: microphone.activeFocus ? "#7fa9ff" : "#4a5875"; border.width: microphone.activeFocus ? 2 : 1 }
                            popup: Popup {
                                y: microphone.height + 4
                                width: microphone.width
                                implicitHeight: contentItem.implicitHeight + 12
                                padding: 6
                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: Math.min(contentHeight, 240)
                                    model: microphone.popup.visible ? microphone.delegateModel : null
                                    currentIndex: microphone.highlightedIndex
                                }
                                background: Rectangle { color: "#202838"; radius: 8; border.color: "#5a6a8a" }
                            }
                            delegate: ItemDelegate {
                                width: microphone.width - 12
                                contentItem: Text { text: modelData.label + "  ·  " + modelData.name; color: "#f4f7ff"; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                                highlighted: microphone.highlightedIndex === index
                                background: Rectangle { radius: 5; color: parent.highlighted ? "#3c527a" : (parent.hovered ? "#2b3850" : "transparent") }
                            }
                        }
                        DarkButton { text: "刷新设备"; onClicked: settings.refresh() }
                    }
                    Text { visible: settings.microphones.length === 0; text: "没有发现可用的 PipeWire/PulseAudio 输入设备。请检查麦克风权限或在系统声音设置中启用设备。"; color: "#ffb4ab"; width: 620; wrapMode: Text.Wrap }
                    Text { text: "蓝牙耳机需先在系统声音设置中切换到 HFP/HSP（通话）模式，再选择出现的 bluez_input source。"; color: "#aeb8ca"; width: 620; wrapMode: Text.Wrap }
                    ComboBox {
                        model: ["按住说话", "点击切换"]
                        currentIndex: root.config.voice_trigger === "toggle" ? 1 : 0
                        onActivated: { root.config.voice_trigger = currentIndex === 1 ? "toggle" : "push_to_talk"; settings.saveConfig(JSON.stringify(root.config)) }
                    }
                    DarkCheckBox { text: "焦点未变化时自动提交"; checked: root.config.voice_auto_commit; onToggled: { root.config.voice_auto_commit = checked; settings.saveConfig(JSON.stringify(root.config)) } }
                }

                Column {
                    spacing: 16
                    Text { text: "外观"; color: "white"; font.pixelSize: 28; font.bold: true }
                    Text { text: "主题会立即应用到候选窗；“午夜蓝”保留了你现在使用的外观。"; color: "#aeb8ca"; width: 680; wrapMode: Text.Wrap }
                    Grid {
                        width: 640
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 12
                        Repeater {
                            model: root.candidateThemes
                            delegate: Rectangle {
                                id: themeCard
                                property var themeData: modelData
                                width: 314
                                height: 132
                                radius: 10
                                color: root.selectedTheme() === themeData.id ? "#263a5d" : "#202631"
                                border.width: root.selectedTheme() === themeData.id ? 2 : 1
                                border.color: root.selectedTheme() === themeData.id ? "#9dc0ff" : "#46556e"
                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 12
                                    spacing: 8
                                    Row {
                                        width: parent.width
                                        spacing: 8
                                        Text { text: themeCard.themeData.name; width: 70; color: "#f4f7ff"; font.pixelSize: 16; font.bold: true }
                                        Text { text: themeCard.themeData.subtitle; width: 150; color: "#aeb8ca"; font.pixelSize: 12; elide: Text.ElideRight }
                                        Text { text: root.selectedTheme() === themeCard.themeData.id ? "已选" : ""; color: "#a9c7ff"; font.pixelSize: 12; font.bold: true }
                                    }
                                    Rectangle {
                                        width: parent.width
                                        height: 52
                                        radius: 8
                                        color: themeCard.themeData.background
                                        border.width: 1
                                        border.color: themeCard.themeData.border
                                        Row {
                                            anchors.fill: parent
                                            anchors.margins: 10
                                            spacing: 9
                                            Text { text: "zh"; color: themeCard.themeData.accent; font.pixelSize: 12; font.bold: true }
                                            Text { text: "nihao"; color: themeCard.themeData.preedit; font.pixelSize: 12 }
                                            Text { text: "1"; color: themeCard.themeData.muted; font.pixelSize: 12 }
                                            Text { text: "你好"; color: themeCard.themeData.text; font.pixelSize: 15; font.bold: true }
                                        }
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.saveAppearance("theme", themeCard.themeData.id)
                                }
                            }
                        }
                    }
                    ComboBox { model: ["横向候选", "纵向候选"]; currentIndex: root.config.layout === "vertical" ? 1 : 0; onActivated: { root.config.layout = currentIndex ? "vertical" : "horizontal"; settings.saveConfig(JSON.stringify(root.config)) } }
                    Row {
                        spacing: 18
                        Text { text: "候选字号"; color: "#d7deea"; width: 72; anchors.verticalCenter: parent.verticalCenter }
                        SpinBox { from: 12; to: 32; value: root.config.font_size || 15; onValueModified: root.saveAppearance("font_size", value) }
                        Text { text: "圆角"; color: "#d7deea"; width: 40; anchors.verticalCenter: parent.verticalCenter }
                        SpinBox { from: 0; to: 32; value: root.config.corner_radius === undefined ? 10 : root.config.corner_radius; onValueModified: root.saveAppearance("corner_radius", value) }
                        Text { text: "背景不透明度"; color: "#d7deea"; width: 92; anchors.verticalCenter: parent.verticalCenter }
                        SpinBox { from: 65; to: 100; value: root.config.opacity || 96; onValueModified: root.saveAppearance("opacity", value) }
                    }
                }

                Column {
                    spacing: 16
                    Text { text: "快捷键"; color: "white"; font.pixelSize: 28; font.bold: true }
                    Text { text: "点击右侧按钮后直接按下想要的组合键。修改会立即写入配置并让 Fcitx 引擎重载；不接受重复或只有修饰键的绑定。"; color: "#b8c5dc"; width: 680; wrapMode: Text.Wrap }
                    Repeater {
                        model: [
                            { field: "voice_hotkey", title: "语音输入", hint: "按住说话；默认 F8" },
                            { field: "cancel_hotkey", title: "取消组合/语音", hint: "默认 Esc" },
                            { field: "commit_raw_hotkey", title: "提交原文", hint: "默认 Enter" },
                            { field: "previous_page_hotkey", title: "上一页候选", hint: "默认 PgUp" },
                            { field: "next_page_hotkey", title: "下一页候选", hint: "默认 PgDn" },
                            { field: "previous_candidate_hotkey", title: "上一个候选", hint: "默认 ←" },
                            { field: "next_candidate_hotkey", title: "下一个候选", hint: "默认 →" }
                        ]
                        delegate: Rectangle {
                            width: 680
                            height: 52
                            radius: 8
                            color: root.captureField === modelData.field ? "#263a5d" : "#202631"
                            Row {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 12
                                Column {
                                    width: 390
                                    Text { text: modelData.title; color: "#f4f7ff"; font.pixelSize: 15 }
                                    Text { text: modelData.hint; color: "#adbbd3"; font.pixelSize: 12 }
                                }
                                DarkButton {
                                    width: 240
                                    text: root.captureField === modelData.field ? "请按键…" : root.hotkeyLabel(root.config[modelData.field])
                                    onClicked: { root.captureField = modelData.field; root.captureMessage = "正在录入“" + modelData.title + "”。按 Esc 也会被记录；如需取消请点击其它页面。"; root.forceActiveFocus() }
                                }
                            }
                        }
                    }
                    Text { text: root.captureMessage; color: root.captureField ? "#a9c7ff" : "#9fe1b6"; width: 680; wrapMode: Text.Wrap }
                    Rectangle { width: 680; height: 1; color: "#3b465d" }
                    Text { text: "Space 选择首选候选、1–9 选择候选以及 Shift + 字母直接提交大写字母仍保持固定，避免与普通输入冲突。切换输入法的 Ctrl+Space 属于 Fcitx 全局设置，可在 fcitx5-configtool 的“全局选项”中修改。"; color: "#b8c5dc"; width: 680; wrapMode: Text.Wrap }
                }

                Column {
                    spacing: 16
                    Text { text: "诊断"; color: "white"; font.pixelSize: 28; font.bold: true }
                    DarkButton { text: "刷新"; onClicked: settings.refresh() }
                    TextArea {
                        width: 700
                        height: 440
                        text: settings.diagnostics
                        readOnly: true
                        color: "#d7deea"
                        wrapMode: Text.WrapAnywhere
                        background: Rectangle { color: "#10141d"; radius: 10 }
                    }
                }
            }
        }
    }
}
