/*
 * Copyright (C) 2021 - 2026 LingmoOS Team.
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
import QtQuick.Layouts
import QtQuick.Shapes
import LingmoUI.CompatibleModule 3.0 as LingmoUI
import LingmoScreenshot 1.0

// Select an area of the capture, draw on it and do something with it.
// Used full screen (overlay) and inside the editor window (editorMode).
Item {
    id: editor

    focus: true

    readonly property bool editorMode: capture.editor
    readonly property var annotations: capture.annotations
    // Image pixels per item unit.
    readonly property real s: imageArea.width > 0 ? capture.imageWidth / imageArea.width : 1

    // Selection, in imageArea coordinates.
    property real selX: 0
    property real selY: 0
    property real selW: 0
    property real selH: 0
    readonly property bool hasSelection: selW >= 2 && selH >= 2

    property bool videoMode: capture.video
    readonly property bool scrollMode: capture.scroll
    // "area", "window" or "screen"
    property string target: capture.target
    property string tool: "select"
    property bool beautify: view.beautify
    // "Ask about the screen": select (or circle) an area and ask the local AI
    property bool askMode: capture.ask
    // The circle or scribble being drawn (imageArea coordinates)
    property var strokePoints: []
    property bool askedInitial: false

    // What the left button is doing: "", "draw", "erase", "moveObject",
    // "moveSelection", "newSelection", "pickWindow", "resize".
    property string mode: ""
    property bool dragStarted: false
    property real pressX: 0
    property real pressY: 0
    property real pressSelX: 0
    property real pressSelY: 0
    property point lastImagePoint: Qt.point(0, 0)

    // Pointer, for the magnifier and the smart selection.
    property real pointerX: -1
    property real pointerY: -1
    property bool pointerInside: false
    property var hoverRegions: []
    property int hoverLevel: 0
    readonly property var hoverRect: hoverRegions.length > 0 ? hoverRegions[Math.min(hoverLevel, hoverRegions.length - 1)] : null

    property bool eyedropper: false
    property bool textEditing: false
    property bool helpVisible: config.showHelp && !editorMode && !videoMode && !scrollMode && target === "area"
                               && capture.initialRegion.width <= 0 && !askMode
    property real wheelHintValue: -1

    readonly property bool interacting: mode === "newSelection" && dragStarted || mode === "resize" || mode === "moveSelection"
    readonly property bool barsVisible: hasSelection && !interacting && !textCard.visible
    readonly property bool askPanelVisible: askMode && hasSelection && !interacting && mode !== "scribble"
                                            && !textCard.visible

    readonly property var tools: [
        { name: "select", icon: "tool-pointer", tip: qsTr("Select and move objects (V)") },
        { name: "pencil", icon: "draw-freehand", tip: qsTr("Pencil (P)") },
        { name: "line", icon: "draw-line", tip: qsTr("Line (L)") },
        { name: "arrow", icon: "draw-arrow", tip: qsTr("Arrow (A)") },
        { name: "rectangle", icon: "draw-rectangle", tip: qsTr("Rectangle (R)") },
        { name: "filledRectangle", icon: "rectangle-shape", tip: qsTr("Filled rectangle (F)") },
        { name: "ellipse", icon: "draw-ellipse", tip: qsTr("Ellipse (O)") },
        { name: "marker", icon: "draw-highlight", tip: qsTr("Highlighter (M)") },
        { name: "text", icon: "draw-text", tip: qsTr("Text (T)") },
        { name: "counter", icon: "format-list-ordered", tip: qsTr("Numbered step (N)") },
        { name: "pixelate", icon: "pixelart-trace", tip: qsTr("Pixelate (X)") },
        { name: "blur", icon: "blurimage", tip: qsTr("Blur (B)") },
        { name: "invert", icon: "invertimage", tip: qsTr("Invert colours (I)") },
        { name: "eraser", icon: "draw-eraser-delete-objects", tip: qsTr("Eraser: click an object to delete it (E)") }
    ]

    readonly property var toolKeys: ({
        [Qt.Key_V]: "select", [Qt.Key_P]: "pencil", [Qt.Key_L]: "line", [Qt.Key_A]: "arrow",
        [Qt.Key_R]: "rectangle", [Qt.Key_F]: "filledRectangle", [Qt.Key_O]: "ellipse",
        [Qt.Key_M]: "marker", [Qt.Key_T]: "text", [Qt.Key_N]: "counter", [Qt.Key_X]: "pixelate",
        [Qt.Key_B]: "blur", [Qt.Key_I]: "invert", [Qt.Key_E]: "eraser"
    })

    // ---------------------------------------------------------------- helpers

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }

    function imagePoint(x, y) { return Qt.point(x * s, y * s) }

    function selectionImageRect() {
        var x1 = Math.round(selX * s), y1 = Math.round(selY * s)
        var x2 = Math.round((selX + selW) * s), y2 = Math.round((selY + selH) * s)
        return Qt.rect(x1, y1, Math.max(1, x2 - x1), Math.max(1, y2 - y1))
    }

    function insideSelection(x, y) {
        return hasSelection && x >= selX && x <= selX + selW && y >= selY && y <= selY + selH
    }

    function setSelection(x, y, w, h) {
        var x1 = clamp(Math.min(x, x + w), 0, imageArea.width)
        var y1 = clamp(Math.min(y, y + h), 0, imageArea.height)
        var x2 = clamp(Math.max(x, x + w), 0, imageArea.width)
        var y2 = clamp(Math.max(y, y + h), 0, imageArea.height)
        selX = x1; selY = y1; selW = x2 - x1; selH = y2 - y1
    }

    function selectImageRect(r) {
        setSelection(r.x / s, r.y / s, r.width / s, r.height / s)
    }

    function selectAll() {
        setSelection(0, 0, imageArea.width, imageArea.height)
    }

    function clearSelection() {
        selX = 0; selY = 0; selW = 0; selH = 0
        annotations.selectedIndex = -1
    }

    function setTarget(value) {
        target = value
        hoverRegions = []
        if (value === "screen")
            selectAll()
        else if (!editorMode)
            clearSelection()
    }

    function setTool(name) {
        commitText()
        tool = name
        eyedropper = false
        if (name !== "select")
            annotations.selectedIndex = -1
        editor.forceActiveFocus()
    }

    function currentWidth() {
        return tool === "text" ? config.textSize : config.toolWidth
    }

    function setColor(c) {
        if (!c || c.a === undefined)
            return
        config.toolColor = c
        config.addRecentColor(c)
        if (annotations.selectedIndex >= 0)
            annotations.setSelectedColor(c)
    }

    function changeWidth(delta) {
        var index = annotations.selectedIndex
        if (index >= 0 && tool === "select") {
            // Restyle the selected object.
            var isText = annotations.typeAt(index) === "text"
            var value = Math.max(1, annotations.widthAt(index) + delta * (isText ? 2 : 1))
            annotations.setSelectedWidth(value)
            wheelHintValue = Math.round(value)
        } else if (tool === "text" || textEditing) {
            config.textSize = clamp(config.textSize + delta * 2, 8, 200)
            if (textEditing)
                textInput.pixelSize = config.textSize / s
            wheelHintValue = config.textSize
        } else {
            config.toolWidth = clamp(config.toolWidth + delta, 1, 60)
            wheelHintValue = config.toolWidth
        }
        wheelHintTimer.restart()
    }

    function openColorPopup(x, y) {
        colorPopup.openAt(x, y)
    }

    function updateHover(x, y) {
        if (editorMode || hasSelection || videoMode && target === "screen") {
            hoverRegions = []
            return
        }
        var ix = Math.round(x * s), iy = Math.round(y * s)
        var regions = []
        if (target === "window")
            regions = capture.windowAt(ix, iy)
        else if (target === "area" && config.smartSelection)
            regions = capture.regionsAt(ix, iy)
        hoverRegions = regions
        if (hoverLevel >= regions.length)
            hoverLevel = Math.max(0, regions.length - 1)
    }

    // ---------------------------------------------------------------- text

    function startText(x, y, text, color, size, bold) {
        textInput.x = x
        textInput.y = y
        textInput.text = text || ""
        textInput.color = color !== undefined ? color : config.toolColor
        textInput.pixelSize = (size !== undefined ? size : config.textSize) / s
        textInput.bold = bold !== undefined ? bold : config.textBold
        textEditing = true
        textInput.forceActiveFocus()
        textInput.cursorPosition = textInput.text.length
    }

    function commitText() {
        if (!textEditing)
            return
        textEditing = false
        if (textInput.text.trim().length > 0)
            annotations.addText(imagePoint(textInput.x, textInput.y), textInput.text,
                                textInput.color, textInput.pixelSize * s, textInput.bold)
        textInput.text = ""
        editor.forceActiveFocus()
    }

    function cancelText() {
        textEditing = false
        textInput.text = ""
        editor.forceActiveFocus()
    }

    // ---------------------------------------------------------------- ask

    function startAsk() {
        commitText()
        if (!hasSelection)
            return
        tool = "select"
        eyedropper = false
        annotations.selectedIndex = -1
        askMode = true
        askPrepare.restart()
    }

    // Esc in ask mode: stop listening, stop the answer, leave
    function askEscape() {
        if (screenAsk.listening)
            screenAsk.cancelVoice()
        else if (screenAsk.busy)
            screenAsk.stop()
        else if (editorMode && !capture.ask)
            askMode = false
        else
            view.cancel()
    }

    // ---------------------------------------------------------------- actions

    function act(name, argument) {
        commitText()
        if (!hasSelection)
            return
        var r = selectionImageRect()
        switch (name) {
        case "accept": view.accept(r); break
        case "copy": view.copy(r); break
        case "save": view.save(r); break
        case "saveAs": view.saveAs(r); break
        case "pin": view.pin(r); break
        case "openWith": view.openWith(r, argument); break
        case "upload": view.upload(r); break
        case "sendNearby": view.sendNearby(r); break
        case "record": view.record(r); break
        case "scroll": view.scrollCapture(r); break
        case "ocr":
            if (!ocr.available || !ocr.hasLanguageData)
                ocrMissing.visible = true
            else
                view.recognizeText(r)
            break
        case "redact":
            if (!ocr.available || !ocr.hasLanguageData)
                ocrMissing.visible = true
            else
                view.redact(r)
            break
        }
    }

    // ---------------------------------------------------------------- mouse

    function press(mouse) {
        if (mouse.button === Qt.RightButton) {
            colorPopup.openAt(mouse.x + imageArea.x, mouse.y + imageArea.y)
            return
        }

        if (textEditing && !textInput.contains(textInput.mapFromItem(imageArea, mouse.x, mouse.y))) {
            commitText()
            // The click that ends the text doesn't start a new one.
            if (tool === "text") {
                mode = ""
                return
            }
        }

        helpVisible = false
        editor.forceActiveFocus()
        pressX = mouse.x
        pressY = mouse.y
        dragStarted = false
        var p = imagePoint(mouse.x, mouse.y)

        if (eyedropper) {
            setColor(capture.pixelColor(Math.floor(p.x), Math.floor(p.y)))
            eyedropper = false
            mode = ""
            return
        }

        // Ask: drag a box, draw a circle or scribble over something; a
        // click takes the smart selection under the pointer
        if (askMode && !editorMode) {
            mode = "scribble"
            strokeFade.stop()
            strokeShape.opacity = 1
            strokePoints = [Qt.point(mouse.x, mouse.y)]
            return
        }

        if (editorMode || insideSelection(mouse.x, mouse.y)) {
            if (videoMode || scrollMode) {
                if (!editorMode)
                    beginMoveSelection()
                return
            }

            switch (tool) {
            case "select": {
                var index = annotations.itemAt(p, 4 * s)
                if (index >= 0) {
                    annotations.selectedIndex = index
                    annotations.beginMove()
                    lastImagePoint = p
                    mode = "moveObject"
                } else {
                    annotations.selectedIndex = -1
                    if (!editorMode)
                        beginMoveSelection()
                }
                return
            }
            case "text":
                startText(mouse.x, mouse.y)
                mode = ""
                return
            case "counter":
                annotations.addCounter(p, config.toolColor, config.toolWidth)
                mode = ""
                return
            case "eraser":
                annotations.removeAt(annotations.itemAt(p, 4 * s))
                mode = "erase"
                return
            default:
                annotations.begin(tool, p, config.toolColor, config.toolWidth)
                mode = "draw"
                return
            }
        }

        if (target === "window") {
            mode = "pickWindow"
            return
        }
        if (target === "screen")
            return

        mode = "newSelection"
    }

    function beginMoveSelection() {
        mode = "moveSelection"
        pressSelX = selX
        pressSelY = selY
    }

    function move(mouse) {
        pointerX = mouse.x
        pointerY = mouse.y
        pointerInside = true

        if (!(mouse.buttons & Qt.LeftButton)) {
            updateHover(mouse.x, mouse.y)
            return
        }

        var p = imagePoint(mouse.x, mouse.y)
        switch (mode) {
        case "draw":
            annotations.extend(p, (mouse.modifiers & Qt.ShiftModifier) !== 0)
            break
        case "erase":
            annotations.removeAt(annotations.itemAt(p, 4 * s))
            break
        case "moveObject":
            annotations.moveSelected(Qt.point(p.x - lastImagePoint.x, p.y - lastImagePoint.y))
            lastImagePoint = p
            break
        case "moveSelection":
            selX = clamp(pressSelX + mouse.x - pressX, 0, imageArea.width - selW)
            selY = clamp(pressSelY + mouse.y - pressY, 0, imageArea.height - selH)
            break
        case "scribble": {
            if (!dragStarted && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 3) {
                dragStarted = true
                hoverRegions = []
            }
            if (!dragStarted)
                break
            var points = strokePoints
            var last = points[points.length - 1]
            if (Math.abs(mouse.x - last.x) + Math.abs(mouse.y - last.y) >= 2) {
                points.push(Qt.point(mouse.x, mouse.y))
                strokePoints = points
            }
            var live = screenAsk.strokeBounds(strokePoints, 0, Qt.rect(0, 0, imageArea.width, imageArea.height))
            if (live.width > 0)
                setSelection(live.x, live.y, live.width, live.height)
            break
        }
        case "newSelection":
            if (!dragStarted && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) > 3) {
                dragStarted = true
                hoverRegions = []
                annotations.selectedIndex = -1
            }
            if (dragStarted)
                setSelection(pressX, pressY, mouse.x - pressX, mouse.y - pressY)
            break
        }
    }

    function release(mouse) {
        if (mouse.button !== Qt.LeftButton)
            return

        switch (mode) {
        case "draw":
            annotations.finish()
            break
        case "moveObject":
            annotations.endMove()
            break
        case "newSelection":
            if (!dragStarted && hoverRect)
                selectImageRect(hoverRect)
            hoverRegions = []
            break
        case "scribble": {
            if (dragStarted) {
                // The box around the stroke, a little larger than the line
                var b = screenAsk.strokeBounds(strokePoints, 6, Qt.rect(0, 0, imageArea.width, imageArea.height))
                if (b.width > 0)
                    setSelection(b.x, b.y, b.width, b.height)
            } else if (hoverRect) {
                strokePoints = []
                selectImageRect(hoverRect)
            }
            hoverRegions = []
            strokeFade.restart()
            break
        }
        case "pickWindow": {
            var r = capture.windowAt(Math.round(mouse.x * s), Math.round(mouse.y * s))
            if (r.length > 0)
                selectImageRect(r[0])
            hoverRegions = []
            break
        }
        }
        mode = ""
        dragStarted = false
    }

    function doubleClick(mouse) {
        if (askMode)
            return
        if (tool !== "select" || !insideSelection(mouse.x, mouse.y) && !editorMode)
            return
        var index = annotations.itemAt(imagePoint(mouse.x, mouse.y), 4 * s)
        if (index < 0) {
            // Double click on the capture: copy it, like Flameshot.
            if (!editorMode && annotations.count === 0)
                act("accept")
            return
        }
        if (annotations.typeAt(index) !== "text")
            return
        var t = annotations.takeText(index)
        startText(t.x / s, t.y / s, t.text, t.color, t.size, t.bold)
    }

    function wheel(event) {
        var up = event.angleDelta.y > 0
        if (!hasSelection && hoverRegions.length > 1) {
            hoverLevel = clamp(hoverLevel + (up ? 1 : -1), 0, hoverRegions.length - 1)
            return
        }
        if (hasSelection && !videoMode && !scrollMode && !askMode)
            changeWidth(up ? 1 : -1)
    }

    // ---------------------------------------------------------------- keyboard

    function nudge(dx, dy, resize, step) {
        if (annotations.selectedIndex >= 0 && !resize) {
            annotations.moveSelected(Qt.point(dx * step * s, dy * step * s))
            return
        }
        if (!hasSelection || editorMode)
            return
        if (resize) {
            selW = clamp(selW + dx * step, 2, imageArea.width - selX)
            selH = clamp(selH + dy * step, 2, imageArea.height - selY)
        } else {
            selX = clamp(selX + dx * step, 0, imageArea.width - selW)
            selY = clamp(selY + dy * step, 0, imageArea.height - selH)
        }
    }

    Keys.onPressed: (event) => {
        if (textEditing)
            return

        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        var shift = (event.modifiers & Qt.ShiftModifier) !== 0
        var step = ctrl ? 10 : 1
        event.accepted = true

        switch (event.key) {
        case Qt.Key_Escape:
            if (askMode && !colorPopup.opened && !textCard.visible && !ocrMissing.visible) {
                askEscape()
                return
            }
            if (colorPopup.opened) colorPopup.close()
            else if (eyedropper) eyedropper = false
            else if (textCard.visible) textCard.visible = false
            else if (ocrMissing.visible) ocrMissing.visible = false
            else if (editorMode && annotations.selectedIndex >= 0) annotations.selectedIndex = -1
            else view.cancel()
            return
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (askMode) {
                if (askPanelVisible)
                    askPanel.focusField()
                return
            }
            if (videoMode) act("record")
            else if (scrollMode) act("scroll")
            else act("accept")
            return
        case Qt.Key_Left: nudge(-1, 0, shift, step); return
        case Qt.Key_Right: nudge(1, 0, shift, step); return
        case Qt.Key_Up: nudge(0, -1, shift, step); return
        case Qt.Key_Down: nudge(0, 1, shift, step); return
        case Qt.Key_Delete:
        case Qt.Key_Backspace:
            annotations.removeSelected()
            return
        case Qt.Key_F1:
        case Qt.Key_Question:
            helpVisible = !helpVisible
            return
        }

        if (ctrl) {
            switch (event.key) {
            case Qt.Key_C: act("copy"); return
            case Qt.Key_S: act(shift ? "saveAs" : "save"); return
            case Qt.Key_P:
                // Ask: pin the answer, not the picture
                if (askMode) { if (screenAsk.state === "done") screenAsk.pin(); return }
                act("pin"); return
            case Qt.Key_T: act("ocr"); return
            case Qt.Key_I: startAsk(); return
            case Qt.Key_H: if (shift) act("redact"); return
            case Qt.Key_Z: if (shift) annotations.redo(); else annotations.undo(); return
            case Qt.Key_Y: annotations.redo(); return
            case Qt.Key_A: if (!editorMode) selectAll(); return
            }
            event.accepted = false
            return
        }

        if (event.modifiers === Qt.NoModifier && toolKeys[event.key] !== undefined && hasSelection
                && !videoMode && !scrollMode && !askMode) {
            setTool(toolKeys[event.key])
            return
        }
        if (event.key === Qt.Key_H) {
            helpVisible = !helpVisible
            return
        }
        event.accepted = false
    }

    property bool initialized: false

    // Once the view has its size (the scale depends on it).
    function initialize() {
        if (initialized || imageArea.width <= 1 || imageArea.height <= 1)
            return
        initialized = true
        if (editorMode || target === "screen")
            selectAll()
        else if (capture.initialRegion.width > 0)
            selectImageRect(capture.initialRegion)
    }

    Component.onCompleted: {
        initialize()
        editor.forceActiveFocus()
    }

    // ---------------------------------------------------------------- scene

    Rectangle {
        anchors.fill: parent
        visible: editorMode
        color: LingmoUI.Theme.backgroundColor
    }

    Item {
        id: imageArea

        readonly property real fitScale: Math.min(1, Math.min(availableWidth / capture.imageWidth * Screen.devicePixelRatio,
                                                              availableHeight / capture.imageHeight * Screen.devicePixelRatio))
        readonly property real availableWidth: editor.width - LingmoUI.Units.largeSpacing * 4
        readonly property real availableHeight: editor.height - dock.height - LingmoUI.Units.largeSpacing * 4

        // The editor always works on the whole image.
        onWidthChanged: if (editorMode) editor.selectAll(); else editor.initialize()
        onHeightChanged: if (editorMode) editor.selectAll(); else editor.initialize()

        x: editorMode ? Math.round((editor.width - width) / 2) : 0
        y: editorMode ? Math.round(LingmoUI.Units.largeSpacing * 1.5 + (availableHeight - height) / 2) : 0
        width: editorMode ? Math.max(1, Math.round(capture.imageWidth / Screen.devicePixelRatio * fitScale)) : editor.width
        height: editorMode ? Math.max(1, Math.round(capture.imageHeight / Screen.devicePixelRatio * fitScale)) : editor.height

        // Frame around the image in the editor window.
        Rectangle {
            visible: editorMode
            anchors.fill: parent
            anchors.margins: -1
            color: "transparent"
            border.width: 1
            border.color: LingmoUI.Theme.borderColor
        }

        Image {
            id: background
            anchors.fill: parent
            source: capture.imageUrl
            cache: true
            smooth: true
            visible: !editorMode
        }

        // Dims everything outside the selection.
        Rectangle {
            anchors.fill: parent
            visible: !editorMode
            color: LingmoUI.Theme.shadowColor
            opacity: 0.45
        }

        AnnotationCanvas {
            id: canvas
            x: selX
            y: selY
            width: Math.max(1, selW)
            height: Math.max(1, selH)
            visible: hasSelection
            session: capture
            sourceRect: Qt.rect(selX * s, selY * s, selW * s, selH * s)
            selectionColor: LingmoUI.Theme.highlightColor
        }

        // Smart selection / window under the pointer.
        Rectangle {
            id: hoverHighlight
            visible: hoverRect !== null && !hasSelection && mode === ""
            x: hoverRect ? hoverRect.x / s : 0
            y: hoverRect ? hoverRect.y / s : 0
            width: hoverRect ? hoverRect.width / s : 0
            height: hoverRect ? hoverRect.height / s : 0
            color: Qt.rgba(LingmoUI.Theme.highlightColor.r, LingmoUI.Theme.highlightColor.g,
                           LingmoUI.Theme.highlightColor.b, 0.12)
            border.width: 2
            border.color: LingmoUI.Theme.highlightColor

            Behavior on x { NumberAnimation { duration: LingmoUI.Units.veryShortDuration; easing.type: Easing.OutCubic } }
            Behavior on y { NumberAnimation { duration: LingmoUI.Units.veryShortDuration; easing.type: Easing.OutCubic } }
            Behavior on width { NumberAnimation { duration: LingmoUI.Units.veryShortDuration; easing.type: Easing.OutCubic } }
            Behavior on height { NumberAnimation { duration: LingmoUI.Units.veryShortDuration; easing.type: Easing.OutCubic } }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: {
                if (eyedropper)
                    return Qt.CrossCursor
                if (askMode && !editorMode)
                    return Qt.CrossCursor
                if (mode === "moveSelection" || mode === "moveObject")
                    return Qt.ClosedHandCursor
                if (insideSelection(pointerX, pointerY) || editorMode) {
                    if (videoMode || scrollMode)
                        return editorMode ? Qt.ArrowCursor : Qt.SizeAllCursor
                    if (tool === "select")
                        return editorMode ? Qt.ArrowCursor : Qt.SizeAllCursor
                    if (tool === "text")
                        return Qt.IBeamCursor
                    if (tool === "eraser" || tool === "counter")
                        return Qt.PointingHandCursor
                    return Qt.CrossCursor
                }
                return target === "window" ? Qt.PointingHandCursor : Qt.CrossCursor
            }

            onPressed: (mouse) => editor.press(mouse)
            onPositionChanged: (mouse) => editor.move(mouse)
            onReleased: (mouse) => editor.release(mouse)
            onDoubleClicked: (mouse) => editor.doubleClick(mouse)
            onWheel: (wheel) => editor.wheel(wheel)
            onExited: editor.pointerInside = false
            onEntered: editor.pointerInside = true
        }

        SelectionFrame {
            editor: editor
            visible: hasSelection && !editorMode
        }

        // The circle or scribble around what to ask about; fades once drawn
        Shape {
            id: strokeShape
            anchors.fill: parent
            visible: askMode && strokePoints.length > 1 && opacity > 0

            ShapePath {
                strokeColor: Qt.rgba(1, 1, 1, 0.9)
                strokeWidth: 9
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: editor.strokePoints }
            }
            ShapePath {
                strokeColor: LingmoUI.Theme.highlightColor
                strokeWidth: 5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: editor.strokePoints }
            }

            NumberAnimation on opacity {
                id: strokeFade
                running: false
                from: 1; to: 0
                duration: 700
                easing.type: Easing.InQuad
            }
        }

        // Text being typed.
        TextEdit {
            id: textInput

            property real pixelSize: 24
            property bool bold: false

            visible: textEditing
            font.pixelSize: Math.max(4, pixelSize)
            font.bold: bold
            selectByMouse: true
            persistentSelection: false
            textMargin: 0
            width: Math.max(contentWidth + 2, 24)

            Keys.onEscapePressed: editor.cancelText()
            Keys.onReturnPressed: (event) => {
                if (event.modifiers & Qt.ShiftModifier)
                    event.accepted = false
                else
                    editor.commitText()
            }
            Keys.onEnterPressed: editor.commitText()

            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                color: "transparent"
                radius: LingmoUI.Theme.tinyRadius
                border.width: 1
                border.color: LingmoUI.Theme.highlightColor
            }
        }

        // Size of the selection.
        Rectangle {
            id: sizeLabel
            visible: hasSelection && !editorMode && (interacting || !barsVisible || mode === "")
            x: clamp(selX, 0, imageArea.width - width)
            y: selY - height - LingmoUI.Units.smallSpacing >= 0 ? selY - height - LingmoUI.Units.smallSpacing
                                                                 : selY + LingmoUI.Units.smallSpacing
            width: sizeText.implicitWidth + LingmoUI.Units.largeSpacing
            height: sizeText.implicitHeight + LingmoUI.Units.smallSpacing
            radius: LingmoUI.Theme.tinyRadius
            color: LingmoUI.Theme.secondBackgroundColor
            border.width: 1
            border.color: LingmoUI.Theme.borderColor

            Label {
                id: sizeText
                anchors.centerIn: parent
                font.pointSize: LingmoUI.Units.fontCaption
                text: {
                    var r = selectionImageRect()
                    return "%1 × %2".arg(r.width).arg(r.height)
                }
            }
        }

        Magnifier {
            id: magnifier
            editor: editor
            visible: config.showMagnifier && !editorMode && pointerInside && pointerX >= 0
                     && (!hasSelection || mode === "newSelection" || mode === "resize" || eyedropper)
        }

        // Wheel feedback: the new width.
        Rectangle {
            visible: wheelHintTimer.running && wheelHintValue > 0
            x: clamp(pointerX + 18, 0, imageArea.width - width)
            y: clamp(pointerY + 18, 0, imageArea.height - height)
            width: wheelRow.implicitWidth + LingmoUI.Units.largeSpacing
            height: wheelRow.implicitHeight + LingmoUI.Units.smallSpacing * 2
            radius: LingmoUI.Theme.smallRadius
            color: LingmoUI.Theme.secondBackgroundColor
            border.width: 1
            border.color: LingmoUI.Theme.borderColor

            RowLayout {
                id: wheelRow
                anchors.centerIn: parent
                spacing: LingmoUI.Units.smallSpacing

                Rectangle {
                    Layout.preferredWidth: Math.min(24, Math.max(2, (tool === "text" ? 8 : wheelHintValue) / s))
                    Layout.preferredHeight: Layout.preferredWidth
                    radius: width / 2
                    color: config.toolColor
                    visible: tool !== "text"
                }

                Label {
                    text: tool === "text" || textEditing ? qsTr("Text size: %1").arg(wheelHintValue)
                                                         : qsTr("Thickness: %1").arg(wheelHintValue)
                    font.pointSize: LingmoUI.Units.fontCaption
                }
            }
        }
    }

    Timer {
        id: wheelHintTimer
        interval: 900
    }

    // ---------------------------------------------------------------- bars

    // Overlay: photo/video and area/window/screen.
    ModeBar {
        id: modeBar
        editor: editor
        visible: !editorMode && !interacting && mode !== "draw" && !askMode
        z: 10
        anchors.horizontalCenter: parent.horizontalCenter
        y: {
            var top = LingmoUI.Units.largeSpacing * 2
            var overlaps = hasSelection && selY < top + height + LingmoUI.Units.largeSpacing
                    && selX < (editor.width + width) / 2 && selX + selW > (editor.width - width) / 2
            return overlaps ? editor.height - height - LingmoUI.Units.largeSpacing * 2 : top
        }
    }

    // Tools and actions, under the selection (overlay) or docked (editor).
    Flow {
        id: bars

        readonly property real naturalWidth: toolsCard.implicitWidth + actionsCard.implicitWidth + spacing
        readonly property real margin: LingmoUI.Units.largeSpacing
        readonly property real gap: LingmoUI.Units.smallSpacing + 2

        z: 10
        visible: barsVisible && !editorMode && !askMode
        spacing: LingmoUI.Units.smallSpacing
        width: Math.min(naturalWidth, editor.width - margin * 2)
        layoutDirection: Qt.LeftToRight

        x: clamp(selX + selW / 2 - width / 2, margin, editor.width - width - margin)
        y: {
            var below = selY + selH + gap
            if (below + height <= editor.height - margin)
                return below
            var above = selY - gap - height
            if (above >= margin)
                return above
            return clamp(selY + selH - height - gap, margin, editor.height - height - margin)
        }

        ToolsCard { id: toolsCard; editor: editor; visible: !videoMode && !scrollMode }
        ActionsCard { id: actionsCard; editor: editor }
    }

    // The editor window: bars at the bottom.
    Item {
        id: dock
        visible: editorMode
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: editorMode ? dockFlow.height + LingmoUI.Units.largeSpacing * 2 : 0

        Flow {
            id: dockFlow
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: LingmoUI.Units.largeSpacing
            width: Math.min(dockTools.implicitWidth + dockActions.implicitWidth + spacing,
                            parent.width - LingmoUI.Units.largeSpacing * 2)
            spacing: LingmoUI.Units.smallSpacing

            ToolsCard { id: dockTools; editor: editor; visible: editorMode }
            ActionsCard { id: dockActions; editor: editor; visible: editorMode }
        }
    }

    // Ask: what to do, before anything is selected
    Rectangle {
        id: askHint
        z: 10
        visible: askMode && !editorMode && !hasSelection && mode === ""
        anchors.horizontalCenter: parent.horizontalCenter
        y: LingmoUI.Units.largeSpacing * 2
        width: askHintRow.implicitWidth + LingmoUI.Units.largeSpacing * 2
        height: askHintRow.implicitHeight + LingmoUI.Units.largeSpacing
        radius: height / 2
        color: LingmoUI.Theme.secondBackgroundColor
        border.width: 1
        border.color: LingmoUI.Theme.borderColor

        RowLayout {
            id: askHintRow
            anchors.centerIn: parent
            spacing: LingmoUI.Units.smallSpacing

            LingmoUI.IconItem {
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
                source: "lingmo-assistant"
            }
            Label {
                text: qsTr("Circle, scribble over or select what you want to ask about")
                color: LingmoUI.Theme.textColor
                font.bold: true
            }
            Label {
                text: qsTr("Esc to close")
                color: LingmoUI.Theme.secondaryTextColor
                font.pointSize: LingmoUI.Units.fontCaption
            }
        }
    }

    AskPanel {
        id: askPanel
        z: 15
        visible: askPanelVisible
        backdrop: editorMode ? null : imageArea
        maxHeight: Math.min(620, editor.height - LingmoUI.Units.largeSpacing * 4)
        width: Math.min(implicitWidth, editor.width - LingmoUI.Units.largeSpacing * 4)
        height: implicitHeight

        readonly property real margin: LingmoUI.Units.largeSpacing * 2
        readonly property real gap: LingmoUI.Units.largeSpacing

        // Beside the selection (right, then left), else below or above it,
        // else over its bottom-right corner
        x: {
            var right = imageArea.x + selX + selW + gap
            if (right + width <= editor.width - margin)
                return right
            var left = imageArea.x + selX - gap - width
            if (left >= margin)
                return left
            return clamp(imageArea.x + selX + selW - width, margin, editor.width - width - margin)
        }
        y: {
            var right = imageArea.x + selX + selW + gap
            var left = imageArea.x + selX - gap - width
            if (right + width <= editor.width - margin || left >= margin)
                return clamp(imageArea.y + selY, margin, editor.height - height - margin)
            var below = imageArea.y + selY + selH + gap
            if (below + height <= editor.height - margin)
                return below
            var above = imageArea.y + selY - gap - height
            if (above >= margin)
                return above
            return clamp(editor.height - height - margin, margin, editor.height - height - margin)
        }

        onCloseRequested: editor.askEscape()
        onVisibleChanged: if (visible) focusTimer.restart()

        Timer {
            id: focusTimer
            interval: 50
            onTriggered: askPanel.focusField()
        }
    }

    // The area changed: crop it and read its text before the question
    Timer {
        id: askPrepare
        interval: 120
        onTriggered: {
            if (!askMode || !hasSelection || interacting || mode === "scribble")
                return
            screenAsk.prepare(selectionImageRect())
            if (capture.askQuestion.length > 0 && !askedInitial) {
                askedInitial = true
                screenAsk.ask(capture.askQuestion)
            }
        }
    }
    onSelXChanged: if (askMode) askPrepare.restart()
    onSelYChanged: if (askMode) askPrepare.restart()
    onSelWChanged: if (askMode) askPrepare.restart()
    onSelHChanged: if (askMode) askPrepare.restart()
    onModeChanged: if (askMode && mode === "") askPrepare.restart()

    ColorPopup {
        id: colorPopup
        editor: editor
    }

    HelpCard {
        id: helpCard
        editor: editor
        z: 20
        visible: helpVisible && !hasSelection
        anchors.centerIn: parent
    }

    TextResultCard {
        id: textCard
        z: 20
        visible: false
        anchors.centerIn: parent
        onCopyRequested: (text) => view.copyText(text)
    }

    // OCR needs tesseract.
    LingmoUI.Card {
        id: ocrMissing
        z: 20
        visible: false
        elevation: 3
        anchors.centerIn: parent
        width: Math.min(460, editor.width - LingmoUI.Units.largeSpacing * 4)
        implicitHeight: missingColumn.implicitHeight + LingmoUI.Units.largeSpacing * 3

        // Keeps the pointer from reaching the capture below.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            onWheel: (wheel) => wheel.accepted = true
        }

        ColumnLayout {
            id: missingColumn
            anchors.fill: parent
            anchors.margins: LingmoUI.Units.largeSpacing * 1.5
            spacing: LingmoUI.Units.largeSpacing

            Label {
                text: qsTr("Text recognition is not installed")
                font.pointSize: LingmoUI.Units.fontSubheading
                font.bold: true
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }

            Label {
                text: !ocr.available
                      ? qsTr("Copying text and Quick redact use tesseract, which runs on this computer: nothing is sent anywhere. Install it from the settings.")
                      : qsTr("Tesseract is installed, but no language data is. Install it from the settings.")
                color: LingmoUI.Theme.secondaryTextColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: LingmoUI.Units.smallSpacing

                LingmoUI.ActionButton {
                    text: qsTr("Close")
                    onClicked: ocrMissing.visible = false
                }
                LingmoUI.ActionButton {
                    text: qsTr("Open settings")
                    primary: true
                    onClicked: view.openSettings("settings")
                }
            }
        }
    }

    // OCR / redact in progress.
    LingmoUI.Card {
        z: 20
        visible: view.busy
        elevation: 2
        anchors.centerIn: parent
        implicitWidth: busyRow.implicitWidth + LingmoUI.Units.largeSpacing * 3
        implicitHeight: busyRow.implicitHeight + LingmoUI.Units.largeSpacing * 2

        RowLayout {
            id: busyRow
            anchors.centerIn: parent
            spacing: LingmoUI.Units.largeSpacing

            LingmoUI.Spinner {
                running: view.busy
            }
            Label {
                text: view.busyText
            }
        }
    }

    LingmoUI.Toast {
        id: toast
    }

    Connections {
        target: view

        function onMessage(text, error) {
            toast.showNotification(text, error ? "long" : "short")
        }

        function onTextRecognized(text) {
            textCard.text = text
            textCard.visible = true
        }
    }
}
