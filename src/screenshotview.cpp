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

#include "screenshotview.h"
#include "screenrecorder.h"
#include "x11windows.h"

#include <QClipboard>
#include <QEventLoop>
#include <QTimer>

#include <QGuiApplication>
#include <QQmlContext>
#include <QScreen>
#include <QPixmap>
#include <QStandardPaths>
#include <QDateTime>
#include <QProcess>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QCursor>

ScreenshotView::ScreenshotView(ScreenRecorder *recorder, QQuickView *parent)
    : QQuickView(parent)
    , m_recorder(recorder)
{
    rootContext()->setContextProperty("view", this);
    QString filePath = "/usr/bin/lingmo-ocr";
    QFile file(filePath);
    if(file.exists())
    {
        m_ocrEnabled=true;
    }
    else
    {
        m_ocrEnabled=false;
    }
    emit ocrEnabledChanged();
    setFlags(Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
    setScreen(qGuiApp->primaryScreen());
    setResizeMode(QQuickView::SizeRootObjectToView);
    setSource(QUrl("qrc:/qml/main.qml"));
    setGeometry(screen()->geometry());
}

bool ScreenshotView::ocrEnabled() const
{
    return m_ocrEnabled;
}

QVariantList ScreenshotView::windowRects() const
{
    return m_windowRects;
}

void ScreenshotView::start()
{
    // Use the screen under the mouse pointer.
    QScreen *s = QGuiApplication::screenAt(QCursor::pos());
    if (!s)
        s = qGuiApp->primaryScreen();

    setScreen(s);
    setGeometry(s->geometry());

    // 保存图片
    QPixmap p = s->grabWindow(0);
    p.save("/tmp/lingmo-screenshot.png");

    // Windows on this screen, in logical coordinates relative to the view.
    const QPoint origin = s->geometry().topLeft();
    const qreal dpr = s->devicePixelRatio();
    const QRect bounds(QPoint(0, 0), s->geometry().size());
    m_windowRects.clear();
    for (const QRect &rect : X11Windows::visibleWindows()) {
        QRect logical(QPointF((rect.topLeft() - origin) / dpr).toPoint(), (QSizeF(rect.size()) / dpr).toSize());
        logical = logical.intersected(bounds);
        if (!logical.isEmpty())
            m_windowRects.append(logical);
    }
    emit windowRectsChanged();

    setVisible(true);
    setKeyboardGrabEnabled(true);

    emit refresh();
}

void ScreenshotView::delay(int value)
{
    QEventLoop waitLoop;
    QTimer::singleShot(value, &waitLoop, SLOT(quit()));
    waitLoop.exec();

    start();
}

void ScreenshotView::quit()
{
    qGuiApp->quit();
}

void ScreenshotView::ocr(QRect rect)
{
    setVisible(false);
    QDir dir;
    dir.mkpath("/tmp/lingmo-screenshot/");
    QString fileName = QString("%1/Screenshot_%2.png")
                              .arg("/tmp/lingmo-screenshot")
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    QImage image("/tmp/lingmo-screenshot.png");
    QImage cropped = image.copy(rect);
    bool saved = cropped.save(fileName);

    if (saved) {
        QProcess::startDetached("lingmo-ocr", QStringList() << fileName);
    }

    removeTmpFile();
    this->quit();
}

void ScreenshotView::saveFile(QRect rect)
{
    setVisible(false);

    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString fileName = QString("%1/Screenshot_%2.png")
                              .arg(desktopPath)
                              .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    QImage image("/tmp/lingmo-screenshot.png");
    QImage cropped = image.copy(rect);
    bool saved = cropped.save(fileName);

    if (saved) {
        QDBusInterface iface("org.freedesktop.Notifications",
                             "/org/freedesktop/Notifications",
                             "org.freedesktop.Notifications",
                             QDBusConnection::sessionBus());
        if (iface.isValid()) {
            QList<QVariant> args;
            args << "lingmo-screenshot";
            args << ((unsigned int) 0);
            args << "lingmo-screenshot";
            args << "";
            args << tr("The picture has been saved to %1").arg(fileName);
            args << QStringList();
            args << QVariantMap();
            args << (int) 10;
            iface.asyncCallWithArgumentList("Notify", args);
        }
    }

    removeTmpFile();
    this->quit();
}

void ScreenshotView::copyToClipboard(QRect rect)
{
    setVisible(false);

    QImage image("/tmp/lingmo-screenshot.png");
    QImage cropped = image.copy(rect);
    QClipboard *clipboard = qGuiApp->clipboard();
    clipboard->setImage(cropped);

    QDBusInterface iface("org.freedesktop.Notifications",
                         "/org/freedesktop/Notifications",
                         "org.freedesktop.Notifications",
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        QList<QVariant> args;
        args << "lingmo-screenshot";
        args << ((unsigned int) 0);
        args << "lingmo-screenshot";
        args << "";
        args << tr("The picture has been saved to the clipboard");
        args << QStringList();
        args << QVariantMap();
        args << (int) 10;
        iface.asyncCallWithArgumentList("Notify", args);
    }

    removeTmpFile();

    QTimer::singleShot(100, qGuiApp, &QGuiApplication::quit);
}

void ScreenshotView::startRecording(QRect rect, bool microphone, bool systemAudio, bool showClicks)
{
    setKeyboardGrabEnabled(false);
    setVisible(false);
    removeTmpFile();

    // rect is in physical pixels, relative to the screen.
    QScreen *s = screen();
    const QRect nativeRect = rect.translated(s->geometry().topLeft());

    ScreenRecorder::Options options;
    options.microphone = microphone;
    options.systemAudio = systemAudio;
    options.showClicks = showClicks;

    // Give the X server some time to remove the overlay from the screen.
    QTimer::singleShot(300, m_recorder, [this, s, nativeRect, options] {
        m_recorder->start(s, nativeRect, options);
    });
}

void ScreenshotView::removeTmpFile()
{
    QFile("/tmp/lingmo-screenshot.png").remove();
}
