/*
 * Copyright (C) 2021 LingmoOS Team.
 *
 * Author:     Reion Wong <reionwong@gmail.com>
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

import QtQuick 2.12
import QtQuick.Window 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtCore
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Item {
    id: control

    focus: true

    property rect cropRect
    property bool cropping: false

    // Photo or video capture
    property bool videoMode: false
    // "screen", "area" or "window"
    property string target: "area"
    property rect hoveredWindow: Qt.rect(0, 0, 0, 0)

    Keys.enabled: true
    Keys.onEscapePressed: view.quit()
    Keys.onReturnPressed: control.accept()
    Keys.onEnterPressed: control.accept()

    Keys.onLeftPressed: {
        if (selectLayer.visible) {
            var newX =  selectLayer.x -= 10
            if (newX < control.x)
                newX = control.x

            selectLayer.x = newX
        }
    }

    Keys.onRightPressed: {
        if (selectLayer.visible) {
            var newX =  selectLayer.x += 10
            if (newX > control.width - selectLayer.width)
                newX = control.width - selectLayer.width

            selectLayer.x = newX
        }
    }

    Keys.onUpPressed: {
        if (selectLayer.visible) {
            var newY = selectLayer.y -= 10
            if (newY < control.y)
                newY = control.y

            selectLayer.y = newY
        }
    }

    Keys.onDownPressed: {
        if (selectLayer.visible) {
            var newY = selectLayer.y += 10
            if (newY > control.height - selectLayer.height)
                newY = control.height - selectLayer.height

            selectLayer.y = newY
        }
    }

    Settings {
        id: recordSettings
        category: "Recording"
        property bool microphone: false
        property bool systemAudio: false
        property bool showClicks: false
    }

    function selectionRect() {
        return Qt.rect(selectLayer.x * Screen.devicePixelRatio,
                       selectLayer.y * Screen.devicePixelRatio,
                       selectLayer.width * Screen.devicePixelRatio,
                       selectLayer.height * Screen.devicePixelRatio)
    }

    function hasSelection() {
        return selectLayer.visible && selectLayer.width > 1 && selectLayer.height > 1
    }

    function accept() {
        if (!hasSelection())
            return

        if (control.videoMode)
            control.record()
        else
            control.copyToClipboard()
    }

    function record() {
        view.startRecording(selectionRect(),
                            recordSettings.microphone,
                            recordSettings.systemAudio,
                            recordSettings.showClicks)
    }

    function select(rect) {
        selectLayer.x = rect.x
        selectLayer.y = rect.y
        selectLayer.newX = rect.x
        selectLayer.newY = rect.y
        selectLayer.width = rect.width
        selectLayer.height = rect.height
        selectLayer.visible = true
    }

    function setTarget(value) {
        control.target = value
        control.hoveredWindow = Qt.rect(0, 0, 0, 0)

        if (value === "screen")
            control.select(Qt.rect(0, 0, control.width, control.height))
        else
            selectLayer.reset()
    }

    function windowAt(x, y) {
        var rects = view.windowRects
        for (var i = 0; i < rects.length; ++i) {
            var r = rects[i]
            if (x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height)
                return r
        }
        return Qt.rect(0, 0, 0, 0)
    }

    function refreshImage() {
        image.source = ""
        image.source = "file:///tmp/lingmo-screenshot.png"

        selectImage.source = ""
        selectImage.source = "file:///tmp/lingmo-screenshot.png"
    }

    function save() {
        view.saveFile(Qt.rect(selectLayer.x * Screen.devicePixelRatio,
                              selectLayer.y * Screen.devicePixelRatio,
                              selectLayer.width * Screen.devicePixelRatio,
                              selectLayer.height * Screen.devicePixelRatio))
    }

    function ocr() {
        view.ocr(Qt.rect(selectLayer.x * Screen.devicePixelRatio,
                              selectLayer.y * Screen.devicePixelRatio,
                              selectLayer.width * Screen.devicePixelRatio,
                              selectLayer.height * Screen.devicePixelRatio))
    }

    function copyToClipboard() {
        view.copyToClipboard(Qt.rect(selectLayer.x * Screen.devicePixelRatio,
                                     selectLayer.y * Screen.devicePixelRatio,
                                     selectLayer.width * Screen.devicePixelRatio,
                                     selectLayer.height * Screen.devicePixelRatio))
    }

    Connections {
        target: view

        function onRefresh() {
            control.refreshImage()
        }
    }

    Image {
        id: image
        anchors.fill: parent
        asynchronous: true

        Rectangle {
            id: dimRect
            anchors.fill: parent
            color: "#000"
            opacity: 0.5
        }
    }

    Rectangle {
        id: selectLayer

        property int newX: 0
        property int newY: 0
        property var control: parent
        z: 999
        height: 0
        width: 0
        x: 0
        y: 0
        visible: false
        clip: true
        function reset() {
            selectLayer.x = 0
            selectLayer.y = 0
            selectLayer.newX = 0
            selectLayer.newY = 0
            selectLayer.visible = false
            selectLayer.width = 0
            selectLayer.height = 0
        }

        Image {
            id: selectImage
            width: control.width
            height: control.height
            asynchronous: true
            x: -selectLayer.x
            y: -selectLayer.y
        }

        MoveArea {
            anchors.fill: parent
            control: parent
            enabled: control.target === "area"
        }

        ResizeBorder {
            control: parent
            anchors.fill: parent
            enabled: control.target === "area"
            visible: enabled
        }
    }

    // Window under the pointer in window selection mode
    Rectangle {
        id: windowHighlight
        z: 998
        visible: control.target === "window" && hoveredWindow.width > 0
        x: hoveredWindow.x
        y: hoveredWindow.y
        width: hoveredWindow.width
        height: hoveredWindow.height
        color: Qt.rgba(LingmoUI.Theme.highlightColor.r,
                       LingmoUI.Theme.highlightColor.g,
                       LingmoUI.Theme.highlightColor.b, 0.15)
        border.width: 2
        border.color: LingmoUI.Theme.highlightColor
    }

    // Capture mode
    Rectangle {
        id: modeBar

        z: 1000
        anchors.horizontalCenter: parent.horizontalCenter
        y: LingmoUI.Units.largeSpacing * 2
        width: modeLayout.implicitWidth + LingmoUI.Units.smallSpacing * 2
        height: modeLayout.implicitHeight + LingmoUI.Units.smallSpacing * 2
        radius: LingmoUI.Theme.smallRadius
        color: "white"

        MouseArea {
            anchors.fill: parent
        }

        RowLayout {
            id: modeLayout
            anchors.centerIn: parent
            spacing: 2

            ModeButton {
                text: qsTr("Photo")
                checked: !control.videoMode
                onClicked: control.videoMode = false
            }

            ModeButton {
                text: qsTr("Video")
                checked: control.videoMode
                onClicked: control.videoMode = true
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 20
                Layout.leftMargin: LingmoUI.Units.smallSpacing
                Layout.rightMargin: LingmoUI.Units.smallSpacing
                color: "#d0d0d0"
            }

            ModeButton {
                text: qsTr("Screen")
                checked: control.target === "screen"
                onClicked: control.setTarget("screen")
            }

            ModeButton {
                text: qsTr("Area")
                checked: control.target === "area"
                onClicked: control.setTarget("area")
            }

            ModeButton {
                text: qsTr("Window")
                checked: control.target === "window"
                onClicked: control.setTarget("window")
            }
        }
    }

    Rectangle {
        id: sizeToolTip
        visible: selectLayer.visible && selectLayer.width > 1 && selectLayer.height > 1

        width: sizeLabel.implicitWidth + LingmoUI.Units.largeSpacing
        height: sizeLabel.implicitHeight + LingmoUI.Units.largeSpacing

        z: 999
        x: selectLayer.x
        y: {
            var newY = selectLayer.y - sizeToolTip.height - LingmoUI.Units.smallSpacing

            if (newY < control.y)
                newY = control.y

            return newY
        }

        radius: LingmoUI.Theme.smallRadius

        color: Qt.rgba(LingmoUI.Theme.backgroundColor.r,
                       LingmoUI.Theme.backgroundColor.g,
                       LingmoUI.Theme.backgroundColor.b, 0.9)
        border.width: 1
        border.color: Qt.rgba(LingmoUI.Theme.textColor.r,
                               LingmoUI.Theme.textColor.g,
                               LingmoUI.Theme.textColor.b, 0.15)

        Label {
            id: sizeLabel
            anchors.centerIn: parent
            text: "%1 * %2".arg(parseInt(selectLayer.width)).arg(parseInt(selectLayer.height))
        }
    }

    Rectangle {
        id: tools

        width: toolsLayout.implicitWidth + LingmoUI.Units.largeSpacing
        height: 40 + LingmoUI.Units.smallSpacing

        visible: selectLayer.visible && selectLayer.width > 1 && selectLayer.height > 1
        z: 999

        // 放在右侧
        x: {
            var newX = selectLayer.x + selectLayer.width - tools.width

            if (newX < control.x) {
                return control.x
            }

            return newX
        }

        y: {
            var newY = 0

//            if (selectLayer.y <= control.y
//                    && selectLayer.height + tools.height >= control.height)
//                newY = control.height - tools.height

            // 选中区域与工具栏高度大于总高度
            if (selectLayer.y + selectLayer.height + tools.height + LingmoUI.Units.smallSpacing >= control.height) {
                newY = selectLayer.y - tools.height - LingmoUI.Units.smallSpacing
            } else {
                newY = selectLayer.y + selectLayer.height + LingmoUI.Units.smallSpacing
            }

            if (newY < control.y || newY > control.y + control.height)
                newY = control.height - tools.height

            return newY
        }

        radius: LingmoUI.Theme.smallRadius
        color: "white"

        MouseArea {
            anchors.fill: parent
        }

        RowLayout {
            id: toolsLayout
            anchors.fill: parent

            anchors.leftMargin: LingmoUI.Units.smallSpacing
            anchors.rightMargin: LingmoUI.Units.smallSpacing
            anchors.topMargin: LingmoUI.Units.smallSpacing / 2
            anchors.bottomMargin: LingmoUI.Units.smallSpacing / 2

            ImageButton {
                iconMargins: LingmoUI.Units.largeSpacing
                size: 40
                source: "qrc:/images/ocr.svg"
                visible: view.ocrEnabled && !control.videoMode
                onClicked: control.ocr()
            }

            ImageButton {
                iconMargins: LingmoUI.Units.largeSpacing
                size: 40
                source: "qrc:/images/save.svg"
                visible: !control.videoMode
                onClicked: control.save()
            }

            ModeButton {
                visible: control.videoMode
                toggle: true
                text: qsTr("Microphone")
                checked: recordSettings.microphone
                onClicked: recordSettings.microphone = !recordSettings.microphone
            }

            ModeButton {
                visible: control.videoMode
                toggle: true
                text: qsTr("System audio")
                checked: recordSettings.systemAudio
                onClicked: recordSettings.systemAudio = !recordSettings.systemAudio
            }

            ModeButton {
                visible: control.videoMode
                toggle: true
                text: qsTr("Show clicks")
                checked: recordSettings.showClicks
                onClicked: recordSettings.showClicks = !recordSettings.showClicks
            }

            Rectangle {
                visible: control.videoMode
                Layout.preferredWidth: 1
                Layout.preferredHeight: 20
                Layout.leftMargin: LingmoUI.Units.smallSpacing
                Layout.rightMargin: LingmoUI.Units.smallSpacing
                color: "#d0d0d0"
            }

            ImageButton {
                iconMargins: LingmoUI.Units.largeSpacing
                size: 40
                source: "qrc:/images/cancel.svg"
                onClicked: view.quit()
            }

            ImageButton {
                iconMargins: LingmoUI.Units.largeSpacing
                size: 40
                source: "qrc:/images/ok.svg"
                visible: !control.videoMode
                onClicked: control.copyToClipboard()
            }

            ImageButton {
                id: recordButton
                iconMargins: LingmoUI.Units.largeSpacing - 2
                size: 40
                source: "qrc:/images/record.svg"
                visible: control.videoMode
                onClicked: control.record()
            }

            Label {
                visible: control.videoMode
                text: qsTr("Record")
                color: "#333"
                rightPadding: LingmoUI.Units.smallSpacing

                MouseArea {
                    anchors.fill: parent
                    onClicked: control.record()
                }
            }
        }
    }

    // Global
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        cursorShape: control.target === "window" ? Qt.PointingHandCursor
                                                 : control.target === "screen" ? Qt.ArrowCursor : Qt.CrossCursor
        hoverEnabled: control.target === "window"

        onClicked: {
            if (control.target !== "window")
                return

            var rect = control.windowAt(mouseX, mouseY)
            if (rect.width > 0)
                control.select(rect)
        }

        onPressed: {
            if (control.target !== "area")
                return

            selectLayer.visible = true
            selectLayer.x = mouseX
            selectLayer.y = mouseY
            selectLayer.newX = mouseX
            selectLayer.newY = mouseY
            selectLayer.width = 0
            selectLayer.height = 0
        }

        onPositionChanged: {
            if (control.target === "window") {
                control.hoveredWindow = control.windowAt(mouseX, mouseY)
                return
            }

            if (!mouseArea.pressed || control.target !== "area")
                return

            if (mouseX >= selectLayer.newX) {
                selectLayer.width = mouseX < (control.x + control.width) ? (mouseX - selectLayer.x) : selectLayer.width
            } else {
                selectLayer.x = mouseX < control.x ? control.x : mouseX
                selectLayer.width = selectLayer.newX - selectLayer.x
            }

            if (mouseY >= selectLayer.newY) {
                selectLayer.height = mouseY < (control.y + control.height) ? (mouseY - selectLayer.y) : selectLayer.height
            } else {
                selectLayer.y = mouseY < control.y ? control.y : mouseY
                selectLayer.height = selectLayer.newY - selectLayer.y
            }
        }
    }
}
