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
import QtQuick.Controls 2.12
import LingmoUI.CompatibleModule 3.0 as LingmoUI

Item {
    id: control

    property alias text: _label.text
    property bool checked: false
    // Toggles show a check box, choices are highlighted when selected.
    property bool toggle: false
    signal clicked()

    implicitWidth: _row.implicitWidth + LingmoUI.Units.largeSpacing * 2
    implicitHeight: 32

    Rectangle {
        anchors.fill: parent
        radius: LingmoUI.Theme.smallRadius
        color: control.checked && !control.toggle ? LingmoUI.Theme.highlightColor
                                                  : _mouseArea.pressed ? "#d9d9d9"
                                                  : _mouseArea.containsMouse ? "#ececec" : "transparent"
    }

    Row {
        id: _row
        anchors.centerIn: parent
        spacing: LingmoUI.Units.smallSpacing

        Rectangle {
            visible: control.toggle
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            radius: 3
            color: control.checked ? LingmoUI.Theme.highlightColor : "transparent"
            border.width: control.checked ? 0 : 1
            border.color: "#888"

            Label {
                anchors.centerIn: parent
                visible: control.checked
                text: "✓"
                color: "white"
                font.pointSize: 8
            }
        }

        Label {
            id: _label
            anchors.verticalCenter: parent.verticalCenter
            color: control.checked && !control.toggle ? LingmoUI.Theme.highlightedTextColor : "#333"
        }
    }

    MouseArea {
        id: _mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: control.clicked()
    }
}
