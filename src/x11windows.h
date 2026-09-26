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

#ifndef X11WINDOWS_H
#define X11WINDOWS_H

#include <QList>
#include <QRect>
#include <QPoint>

namespace X11Windows
{
    // Geometries (root window coordinates, native pixels) of the visible
    // top-level windows, including their decorations. Topmost first.
    QList<QRect> visibleWindows();

    // Current pointer position (root coordinates).
    bool pointerPosition(QPoint *pos);

    // Enables XInput2 raw button press events for the whole screen, so that
    // clicks in other applications can be seen by a native event filter.
    bool selectRawButtonPress(bool enable);

    // Whether the xcb event is a raw press of the left, middle or right button.
    bool isRawButtonPress(void *event);
}

#endif // X11WINDOWS_H
