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

import QtQuick 2.12
import QtQuick.Window 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Rectangle {
    id: control

    width: layout.implicitWidth + LingmoUI.Units.largeSpacing * 2
    height: 40
    radius: height / 2
    color: Qt.rgba(0.12, 0.12, 0.12, 0.92)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.15)

    function formatTime(seconds) {
        var h = Math.floor(seconds / 3600)
        var m = Math.floor((seconds % 3600) / 60)
        var s = seconds % 60
        var text = (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s
        return h > 0 ? h + ":" + text : text
    }

    // Drag the indicator around.
    MouseArea {
        property point pressPos

        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: function(mouse) { pressPos = Qt.point(mouse.x, mouse.y) }
        onPositionChanged: function(mouse) {
            if (!pressed)
                return

            control.Window.window.x += mouse.x - pressPos.x
            control.Window.window.y += mouse.y - pressPos.y
        }
    }

    RowLayout {
        id: layout
        anchors.centerIn: parent
        spacing: LingmoUI.Units.largeSpacing

        Rectangle {
            width: 12
            height: 12
            radius: 6
            color: "#E53935"

            SequentialAnimation on opacity {
                loops: Animation.Infinite
                running: recorder.recording
                NumberAnimation { to: 0.25; duration: 700 }
                NumberAnimation { to: 1.0; duration: 700 }
            }
        }

        Label {
            text: control.formatTime(recorder.elapsed)
            color: "white"
            font.family: "monospace"
        }

        Rectangle {
            id: stopButton

            implicitWidth: stopRow.implicitWidth + LingmoUI.Units.largeSpacing * 2
            implicitHeight: 28
            radius: height / 2
            color: stopArea.pressed ? "#b71c1c" : stopArea.containsMouse ? "#d32f2f" : "#E53935"

            Row {
                id: stopRow
                anchors.centerIn: parent
                spacing: LingmoUI.Units.smallSpacing

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    height: 10
                    radius: 2
                    color: "white"
                }

                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Stop")
                    color: "white"
                }
            }

            MouseArea {
                id: stopArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: recorder.stop()
            }
        }
    }
}
