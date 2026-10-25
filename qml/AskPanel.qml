/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts
import LingmoUI.CompatibleModule 3.0 as LingmoUI

// "Ask about the screen": the glass panel next to the selection. Quick chips,
// a question field (typed or spoken), the answer streaming in, and what to do
// with it. The answer comes from the local AI (Lingmo Assistant engine).
Item {
    id: panel

    // The capture under the panel, blurred behind the glass (overlay only)
    property Item backdrop: null
    property real maxHeight: 600

    readonly property bool hasAnswer: screenAsk.answer.length > 0
    readonly property bool asked: screenAsk.question.length > 0
    readonly property real padding: LingmoUI.Units.largeSpacing

    signal closeRequested()

    function focusField() {
        field.forceActiveFocus()
    }

    function continueInAssistant() {
        if (screenAsk.engineInstalled)
            screenAsk.continueInAssistant()
    }

    function submit() {
        var text = field.text.trim()
        if (text.length === 0 || screenAsk.busy && screenAsk.state !== "reading")
            return
        field.text = ""
        screenAsk.ask(text)
    }

    implicitWidth: 420
    implicitHeight: Math.min(maxHeight, column.implicitHeight + padding * 2)

    // Keeps the pointer from reaching the capture below.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // The capture behind the panel, blurred: the glass of a window that is
    // itself the whole screen
    Item {
        id: blurred
        anchors.fill: parent
        visible: panel.backdrop !== null && glass.glass
        layer.enabled: visible
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        ShaderEffectSource {
            id: behind
            anchors.fill: parent
            visible: false
            sourceItem: panel.backdrop
            live: false
            sourceRect: {
                if (!panel.backdrop)
                    return Qt.rect(0, 0, 0, 0)
                var p = panel.mapToItem(panel.backdrop, 0, 0)
                return Qt.rect(p.x, p.y, panel.width, panel.height)
            }
        }

        MultiEffect {
            anchors.fill: parent
            source: behind
            blurEnabled: true
            blur: 1.0
            blurMax: 48
            saturation: 0.2
        }
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: glass.radius
        visible: false
        layer.enabled: true
    }

    LingmoUI.GlassSurface {
        id: glass
        anchors.fill: parent
        radius: LingmoUI.Theme.hugeRadius
        elevated: true
        // The blur is ours (above); never ask KWin for one
        backdropBlurred: panel.backdrop !== null
        blurBehind: false

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: panel.padding
            spacing: LingmoUI.Units.largeSpacing

            // ------------------------------------------------ header
            RowLayout {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.smallSpacing

                LingmoUI.IconItem {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    source: "lingmo-assistant"
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        color: LingmoUI.Theme.textColor
                        text: qsTr("Ask about the screen")
                        font.pointSize: LingmoUI.Units.fontBody
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        id: modeLabel
                        text: {
                            if (!screenAsk.engineInstalled)
                                return qsTr("Lingmo Assistant is not installed")
                            if (screenAsk.mode === "vision")
                                return qsTr("Looks at the picture · %1").arg(screenAsk.model)
                            if (screenAsk.mode === "ocr")
                                return qsTr("Reads the text on the screen · %1").arg(screenAsk.model)
                            return screenAsk.model.length > 0 ? screenAsk.model : qsTr("Starting the local AI…")
                        }
                        color: LingmoUI.Theme.secondaryTextColor
                        font.pointSize: LingmoUI.Units.fontCaption
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                LingmoUI.IconButton {
                    iconColor: LingmoUI.Theme.textColor
                    size: 28; iconSize: 16
                    iconName: "window-close"
                    tipText: qsTr("Close (Esc)")
                    onClicked: panel.closeRequested()
                }
            }

            // ------------------------------------------------ quick chips
            Flow {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.smallSpacing

                Repeater {
                    model: [
                        { kind: "explain", icon: "documentinfo" },
                        { kind: "translate", icon: "preferences-desktop-locale-symbolic" },
                        { kind: "summarize", icon: "view-list-text" },
                        { kind: "copy", icon: "edit-copy" },
                        { kind: "what", icon: "edit-find-symbolic" },
                        { kind: "web", icon: "globe" }
                    ]

                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool enabledChip: modelData.kind === "web" || modelData.kind === "copy"
                                                            || !screenAsk.busy || screenAsk.state === "reading"

                        implicitWidth: chipRow.implicitWidth + LingmoUI.Units.largeSpacing * 1.5
                        implicitHeight: 32
                        radius: height / 2
                        opacity: enabledChip ? 1 : 0.5
                        color: chipArea.pressed ? Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                                                          LingmoUI.Theme.highlightColor.b, 0.28)
                             : chipArea.containsMouse ? Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                                                                LingmoUI.Theme.highlightColor.b, 0.16)
                             : Qt.rgba(LingmoUI.Theme.textColor.r, LingmoUI.Theme.textColor.g, LingmoUI.Theme.textColor.b, 0.07)
                        border.width: 1
                        border.color: Qt.rgba(LingmoUI.Theme.textColor.r, LingmoUI.Theme.textColor.g, LingmoUI.Theme.textColor.b, 0.08)

                        Behavior on color { ColorAnimation { duration: LingmoUI.Units.shortDuration } }

                        RowLayout {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: LingmoUI.Units.smallSpacing

                            LingmoUI.IconItem {
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                source: chip.modelData.icon
                                color: LingmoUI.Theme.textColor
                            }
                            Label {
                                color: LingmoUI.Theme.textColor
                                text: screenAsk.chipLabel(chip.modelData.kind)
                                font.pointSize: LingmoUI.Units.fontCaption + 1
                            }
                        }

                        MouseArea {
                            id: chipArea
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: chip.enabledChip
                            cursorShape: Qt.PointingHandCursor
                            onClicked: screenAsk.askChip(chip.modelData.kind)
                        }
                    }
                }
            }

            // ------------------------------------------------ reading
            // The text is in a script whose pack isn't installed, or was read
            // badly: never passed off as the text (the model would translate
            // nonsense); the question can still go, with the picture only
            LingmoUI.Banner {
                id: readingBanner
                Layout.fillWidth: true
                visible: screenAsk.ocrStatus === "missing" || screenAsk.ocrStatus === "unreliable"
                closable: false
                type: LingmoUI.Banner.Warning
                title: screenAsk.ocrStatus === "missing"
                       ? (screenAsk.missingSizeMb > 0
                          ? qsTr("To read %1, install the reading pack (≈ %2 MB)").arg(screenAsk.ocrLanguageName).arg(screenAsk.missingSizeMb)
                          : qsTr("To read %1, install the reading pack").arg(screenAsk.ocrLanguageName))
                       : qsTr("Uncertain reading")
                text: screenAsk.ocrStatus === "missing"
                      ? qsTr("You can still ask: the answer will come from the picture only, and the reading is uncertain.")
                      : qsTr("The text in the selection wasn't recognized reliably, so it isn't used as the text. The answer comes from the picture and may be uncertain.")

                LingmoUI.ActionButton {
                    visible: screenAsk.ocrStatus === "missing"
                    primary: true
                    busy: screenAsk.installingPack
                    enabled: !screenAsk.installingPack
                    text: screenAsk.installingPack
                          ? (screenAsk.installProgress.length > 0 ? qsTr("Installing… %1").arg(screenAsk.installProgress)
                                                                  : qsTr("Installing…"))
                          : qsTr("Install")
                    onClicked: screenAsk.installReadingPack()
                }
            }

            // ------------------------------------------------ the answer
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: panel.asked || screenAsk.state === "error"
                spacing: LingmoUI.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    visible: panel.asked
                    text: screenAsk.question
                    color: LingmoUI.Theme.secondaryTextColor
                    font.pointSize: LingmoUI.Units.fontCaption + 1
                    font.italic: true
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: screenAsk.state === "reading" || screenAsk.state === "waiting"
                    spacing: LingmoUI.Units.smallSpacing

                    LingmoUI.Spinner {
                        size: 18
                        running: parent.visible
                    }
                    Label {
                        text: screenAsk.state === "reading" ? qsTr("Reading the text in the selection…")
                            : screenAsk.mode === "vision" ? qsTr("Looking at the selection…")
                            : qsTr("Thinking…")
                        color: LingmoUI.Theme.secondaryTextColor
                    }
                }

                ScrollView {
                    id: answerScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: Math.min(answerText.implicitHeight, 380)
                    Layout.minimumHeight: Math.min(answerText.implicitHeight, 80)
                    visible: panel.hasAnswer
                    clip: true
                    contentWidth: availableWidth

                    TextEdit {
                        id: answerText
                        width: answerScroll.availableWidth
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.MarkdownText
                        text: screenAsk.answer
                        color: LingmoUI.Theme.textColor
                        font.pointSize: LingmoUI.Units.fontBody
                        selectionColor: LingmoUI.Theme.highlightColor
                        selectedTextColor: LingmoUI.Theme.highlightedTextColor
                        onLinkActivated: (link) => {
                            if (/^https?:\/\//.test(link))
                                Qt.openUrlExternally(link)
                        }
                        // Follows the answer while it streams in
                        onImplicitHeightChanged: if (screenAsk.state === "answering")
                                                     answerScroll.ScrollBar.vertical.position = 1.0 - answerScroll.ScrollBar.vertical.size
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: screenAsk.state === "error"
                    text: screenAsk.error
                    color: LingmoUI.Theme.redColor
                    wrapMode: Text.WordWrap
                }

                // What to do with the answer
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.hasAnswer || screenAsk.state === "error"
                    spacing: LingmoUI.Units.smallSpacing

                    LingmoUI.ActionButton {
                        text: qsTr("Continue in Assistant")
                        tipText: qsTr("Ctrl+Enter")
                        iconName: "lingmo-assistant"
                        primary: true
                        enabled: screenAsk.engineInstalled
                        onClicked: screenAsk.continueInAssistant()
                    }
                    Item { Layout.fillWidth: true }
                    LingmoUI.IconButton {
                        iconColor: LingmoUI.Theme.textColor
                        size: 32; iconSize: 18
                        visible: panel.hasAnswer
                        iconName: "edit-copy"
                        tipText: qsTr("Copy answer")
                        onClicked: screenAsk.copyAnswer()
                    }
                    LingmoUI.IconButton {
                        iconColor: LingmoUI.Theme.textColor
                        size: 32; iconSize: 18
                        visible: panel.hasAnswer
                        enabled: screenAsk.state === "done"
                        iconName: "window-pin"
                        tipText: qsTr("Pin the answer on the screen (Ctrl+P)")
                        onClicked: screenAsk.pin()
                    }
                }
            }

            // ------------------------------------------------ question
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 40
                radius: height / 2
                color: Qt.rgba(LingmoUI.Theme.textColor.r, LingmoUI.Theme.textColor.g, LingmoUI.Theme.textColor.b, 0.06)
                border.width: field.activeFocus ? 2 : 1
                border.color: field.activeFocus ? LingmoUI.Theme.highlightColor
                                                : Qt.rgba(LingmoUI.Theme.textColor.r, LingmoUI.Theme.textColor.g, LingmoUI.Theme.textColor.b, 0.12)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: LingmoUI.Units.largeSpacing
                    anchors.rightMargin: 4
                    spacing: 2

                    TextInput {
                        id: field
                        Layout.fillWidth: true
                        verticalAlignment: TextInput.AlignVCenter
                        color: LingmoUI.Theme.textColor
                        font.pointSize: LingmoUI.Units.fontBody
                        selectByMouse: true
                        clip: true
                        maximumLength: 2000
                        selectionColor: LingmoUI.Theme.highlightColor
                        selectedTextColor: LingmoUI.Theme.highlightedTextColor
                        enabled: screenAsk.engineInstalled

                        // Ctrl+Enter: "Continue in Assistant"
                        Keys.onReturnPressed: (event) => (event.modifiers & Qt.ControlModifier) ? panel.continueInAssistant()
                                                                                                 : panel.submit()
                        Keys.onEnterPressed: (event) => (event.modifiers & Qt.ControlModifier) ? panel.continueInAssistant()
                                                                                                : panel.submit()

                        Label {
                            anchors.fill: parent
                            verticalAlignment: Text.AlignVCenter
                            visible: field.text.length === 0 && !field.inputMethodComposing
                            text: screenAsk.listening ? (screenAsk.voiceState === "transcribing" ? qsTr("Recognizing…")
                                                                                                 : qsTr("Listening…"))
                                : panel.hasAnswer ? qsTr("Ask a follow-up question…")
                                : qsTr("Ask anything about this area…")
                            color: LingmoUI.Theme.secondaryTextColor
                            elide: Text.ElideRight
                        }
                    }

                    // Push-to-talk, the Assistant's voice input
                    Item {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32

                        Rectangle {
                            anchors.centerIn: parent
                            width: 32 + screenAsk.voiceLevel * 14
                            height: width
                            radius: width / 2
                            visible: screenAsk.listening
                            color: Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                                           LingmoUI.Theme.highlightColor.b, 0.25)
                            Behavior on width { NumberAnimation { duration: 80 } }
                        }
                        LingmoUI.IconButton {
                            iconColor: LingmoUI.Theme.textColor
                            anchors.centerIn: parent
                            size: 32; iconSize: 18
                            iconName: "audio-input-microphone"
                            checkable: true
                            checked: screenAsk.listening
                            enabled: screenAsk.engineInstalled
                            tipText: screenAsk.listening ? qsTr("Stop listening") : qsTr("Ask by voice")
                            onClicked: screenAsk.toggleVoice()
                        }
                    }

                    LingmoUI.IconButton {
                        iconColor: LingmoUI.Theme.textColor
                        size: 32; iconSize: 18
                        iconName: screenAsk.busy && screenAsk.state !== "reading" ? "media-playback-stop" : "go-up"
                        enabled: screenAsk.busy || field.text.trim().length > 0
                        tipText: screenAsk.busy && screenAsk.state !== "reading" ? qsTr("Stop") : qsTr("Ask (Enter)")
                        onClicked: screenAsk.busy && screenAsk.state !== "reading" ? screenAsk.stop() : panel.submit()
                    }
                }
            }

            // ------------------------------------------------ privacy
            RowLayout {
                Layout.fillWidth: true
                spacing: LingmoUI.Units.smallSpacing

                LingmoUI.IconItem {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    source: "object-locked"
                    color: LingmoUI.Theme.secondaryTextColor
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Analyzed on your computer · nothing is uploaded")
                    color: LingmoUI.Theme.secondaryTextColor
                    font.pointSize: LingmoUI.Units.fontCaption
                    elide: Text.ElideRight
                }
            }
        }
    }

    Connections {
        target: screenAsk
        function onQuestionHeard(text) { field.text = "" }
    }
}
