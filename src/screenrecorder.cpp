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

#include "screenrecorder.h"
#include "x11windows.h"

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QQuickView>
#include <QQmlContext>
#include <QSurfaceFormat>
#include <QRasterWindow>
#include <QPainter>
#include <QRegion>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QMessageBox>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QLoggingCategory>

#include <signal.h>

Q_LOGGING_CATEGORY(lcRecorder, "lingmo.screenshot.recorder", QtInfoMsg)

namespace {

const char *NOTIFY_SERVICE = "org.freedesktop.Notifications";
const char *NOTIFY_PATH = "/org/freedesktop/Notifications";
const char *NOTIFY_INTERFACE = "org.freedesktop.Notifications";

// How long to wait for ffmpeg to finalize the file before interrupting it.
const int STOP_TIMEOUT = 5000;
// How long to stay alive so that the notification actions keep working.
const int NOTIFICATION_TIMEOUT = 2 * 60 * 1000;

// A small ring drawn around the pointer when a mouse button is pressed.
// It uses a shaped window so that it also works without a compositor.
class ClickRipple : public QRasterWindow
{
public:
    explicit ClickRipple(QScreen *screen, const QPoint &center)
        : m_step(0)
    {
        setFlags(Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint
                 | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput
                 | Qt::WindowDoesNotAcceptFocus);
        setScreen(screen);

        const int size = SIZE;
        setGeometry(center.x() - size / 2, center.y() - size / 2, size, size);
        updateMask();

        QTimer *timer = new QTimer(this);
        timer->setInterval(30);
        connect(timer, &QTimer::timeout, this, [this, timer] {
            if (++m_step >= STEPS) {
                timer->stop();
                close();
                deleteLater();
                return;
            }
            updateMask();
        });
        timer->start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(QRect(QPoint(0, 0), size()), QColor(255, 179, 0));
    }

private:
    void updateMask()
    {
        const int outer = 10 + (SIZE / 2 - 10) * m_step / (STEPS - 1);
        const int inner = qMax(0, outer - 4);
        const QPoint c(SIZE / 2, SIZE / 2);

        QRegion region(QRect(c.x() - outer, c.y() - outer, outer * 2, outer * 2), QRegion::Ellipse);
        region -= QRegion(QRect(c.x() - inner, c.y() - inner, inner * 2, inner * 2), QRegion::Ellipse);
        setMask(region);
    }

    static const int SIZE = 44;
    static const int STEPS = 12;
    int m_step;
};

}

ScreenRecorder::ScreenRecorder(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_stopRequested(false)
    , m_failed(false)
    , m_tickTimer(new QTimer(this))
    , m_killTimer(new QTimer(this))
    , m_showingClicks(false)
    , m_indicator(nullptr)
    , m_trayIcon(nullptr)
    , m_notificationId(0)
{
    m_tickTimer->setInterval(500);
    connect(m_tickTimer, &QTimer::timeout, this, &ScreenRecorder::elapsedChanged);

    m_killTimer->setSingleShot(true);
    m_killTimer->setInterval(STOP_TIMEOUT);
    connect(m_killTimer, &QTimer::timeout, this, [this] {
        if (!m_process || m_process->state() == QProcess::NotRunning)
            return;

        if (m_process->property("interrupted").toBool()) {
            qCWarning(lcRecorder) << "ffmpeg did not exit, killing it";
            m_process->kill();
        } else {
            qCWarning(lcRecorder) << "ffmpeg did not exit, sending SIGINT";
            m_process->setProperty("interrupted", true);
            ::kill(m_process->processId(), SIGINT);
            m_killTimer->start();
        }
    });
}

ScreenRecorder::~ScreenRecorder()
{
    showClicks(false);

    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->write("q");
        if (!m_process->waitForFinished(STOP_TIMEOUT))
            m_process->kill();
    }
}

QString ScreenRecorder::outputDirectory()
{
    QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (movies.isEmpty())
        movies = QDir::homePath();

    return QDir(movies).filePath(tr("Screen Recordings"));
}

QString ScreenRecorder::defaultMonitorSource()
{
    QProcess pactl;
    pactl.start("pactl", QStringList() << "get-default-sink");

    if (pactl.waitForFinished(2000) && pactl.exitStatus() == QProcess::NormalExit && pactl.exitCode() == 0) {
        const QString sink = QString::fromUtf8(pactl.readAllStandardOutput()).trimmed();
        if (!sink.isEmpty())
            return sink + ".monitor";
    }

    // Understood by PulseAudio and pipewire-pulse.
    return QStringLiteral("@DEFAULT_MONITOR@");
}

QStringList ScreenRecorder::ffmpegArguments(const QString &display, const QRect &rect,
                                            const Options &options, const QString &monitorSource,
                                            const QString &fileName)
{
    QStringList args;
    args << "-hide_banner" << "-loglevel" << "error" << "-nostats" << "-y";

    // Video input
    args << "-thread_queue_size" << "512"
         << "-f" << "x11grab"
         << "-framerate" << QString::number(options.frameRate)
         << "-draw_mouse" << "1"
         << "-video_size" << QString("%1x%2").arg(rect.width()).arg(rect.height())
         << "-i" << QString("%1+%2,%3").arg(display).arg(rect.x()).arg(rect.y());

    // Audio inputs
    int audioInputs = 0;
    if (options.microphone) {
        args << "-thread_queue_size" << "1024" << "-f" << "pulse" << "-i" << "default";
        ++audioInputs;
    }
    if (options.systemAudio) {
        args << "-thread_queue_size" << "1024" << "-f" << "pulse" << "-i" << monitorSource;
        ++audioInputs;
    }

    if (audioInputs == 2) {
        args << "-filter_complex" << "[1:a][2:a]amix=inputs=2:duration=longest:normalize=0[aout]"
             << "-map" << "0:v" << "-map" << "[aout]";
    } else if (audioInputs == 1) {
        args << "-map" << "0:v" << "-map" << "1:a";
    }

    args << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << "23"
         << "-pix_fmt" << "yuv420p";

    if (audioInputs > 0)
        args << "-c:a" << "aac" << "-b:a" << "160k";

    args << "-movflags" << "+faststart" << fileName;

    return args;
}

bool ScreenRecorder::stopRunningRecording()
{
    QDBusMessage msg = QDBusMessage::createMethodCall(RECORDER_SERVICE, RECORDER_PATH,
                                                      RECORDER_INTERFACE, "StopRecording");
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 5000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

void ScreenRecorder::start(QScreen *screen, const QRect &rect, const Options &options)
{
    if (m_process)
        return;

    m_screen = screen;
    m_options = options;
    m_stopRequested = false;
    m_failed = false;
    m_errorOutput.clear();

    const QString ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) {
        showError(tr("FFmpeg was not found. Install the ffmpeg package to record the screen."));
        return;
    }

    // yuv420p needs even dimensions.
    m_rect = rect;
    if (screen) {
        const QRect nativeScreen(screen->geometry().topLeft(), screen->geometry().size() * screen->devicePixelRatio());
        m_rect = m_rect.intersected(nativeScreen);
    }
    m_rect.setWidth(m_rect.width() & ~1);
    m_rect.setHeight(m_rect.height() & ~1);

    if (m_rect.width() < 2 || m_rect.height() < 2) {
        showError(tr("The selected area is too small to be recorded."));
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(RECORDER_SERVICE)) {
        showError(tr("Another screen recording is already in progress."));
        return;
    }
    bus.registerObject(RECORDER_PATH, this, QDBusConnection::ExportScriptableSlots);
    // Allow new screenshots to be taken while recording.
    bus.unregisterService("com.lingmo.Screenshot");

    const QString dirPath = outputDirectory();
    if (!QDir().mkpath(dirPath)) {
        showError(tr("Unable to create the folder %1.").arg(dirPath));
        return;
    }

    const QString baseName = tr("Screen Recording %1")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh-mm-ss"));
    m_fileName = QDir(dirPath).filePath(baseName + ".mp4");
    for (int i = 2; QFile::exists(m_fileName); ++i)
        m_fileName = QDir(dirPath).filePath(QString("%1 (%2).mp4").arg(baseName).arg(i));

    QString display = qEnvironmentVariable("DISPLAY");
    if (display.isEmpty())
        display = ":0";

    const QString monitor = options.systemAudio ? defaultMonitorSource() : QString();
    const QStringList args = ffmpegArguments(display, m_rect, options, monitor, m_fileName);

    qCInfo(lcRecorder).noquote() << "Running:" << ffmpeg << args.join(' ');

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::started, this, &ScreenRecorder::onStarted);
    connect(m_process, &QProcess::finished, this, &ScreenRecorder::onFinished);
    connect(m_process, &QProcess::errorOccurred, this, &ScreenRecorder::onErrorOccurred);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errorOutput += m_process->readAllStandardError();
        m_errorOutput = m_errorOutput.right(4096);
    });
    m_process->start(ffmpeg, args);
}

int ScreenRecorder::elapsed() const
{
    return m_elapsedTimer.isValid() ? int(m_elapsedTimer.elapsed() / 1000) : 0;
}

void ScreenRecorder::StopRecording()
{
    stop();
}

bool ScreenRecorder::IsRecording() const
{
    return isRecording();
}

bool ScreenRecorder::isRecording() const
{
    return m_process && m_process->state() == QProcess::Running;
}

void ScreenRecorder::stop()
{
    if (!isRecording() || m_stopRequested)
        return;

    qCInfo(lcRecorder) << "Stopping the recording";

    m_stopRequested = true;
    showClicks(false);
    hideIndicator();

    // Ask ffmpeg to quit so that the mp4 gets finalized.
    m_process->write("q");
    m_killTimer->start();
}

void ScreenRecorder::onStarted()
{
    m_elapsedTimer.start();
    m_tickTimer->start();

    if (m_options.showClicks)
        showClicks(true);

    showIndicator();

    emit recordingChanged();
    emit elapsedChanged();
}

void ScreenRecorder::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_errorOutput += m_process->readAllStandardError();
    m_killTimer->stop();
    m_tickTimer->stop();
    showClicks(false);
    hideIndicator();

    qCInfo(lcRecorder) << "ffmpeg finished, exit code" << exitCode << exitStatus;

    const QFileInfo file(m_fileName);

    if (m_stopRequested && file.exists() && file.size() > 0) {
        notifySaved();
    } else {
        if (file.exists() && file.size() == 0)
            QFile::remove(m_fileName);

        QString message = m_elapsedTimer.isValid() && m_elapsedTimer.elapsed() > 3000
                ? tr("The screen recording stopped unexpectedly.")
                : tr("Unable to start the screen recording.");

        const QString details = QString::fromLocal8Bit(m_errorOutput).trimmed();
        if (!details.isEmpty())
            message += "\n\n" + details.split('\n').mid(-6).join('\n');

        showError(message);
    }

    emit recordingChanged();
}

void ScreenRecorder::onErrorOccurred(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;

    showError(tr("Unable to start FFmpeg: %1").arg(m_process->errorString()));
}

void ScreenRecorder::showClicks(bool enable)
{
    if (enable == m_showingClicks)
        return;

    if (enable) {
        if (!X11Windows::selectRawButtonPress(true)) {
            qCWarning(lcRecorder) << "XInput2 is not available, clicks will not be shown";
            return;
        }
        qGuiApp->installNativeEventFilter(this);
    } else {
        qGuiApp->removeNativeEventFilter(this);
        X11Windows::selectRawButtonPress(false);
    }

    m_showingClicks = enable;
}

bool ScreenRecorder::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result)

    if (eventType != "xcb_generic_event_t" || !X11Windows::isRawButtonPress(message))
        return false;

    QPoint pos;
    if (m_screen && X11Windows::pointerPosition(&pos)) {
        // Root coordinates to logical coordinates.
        const QPoint origin = m_screen->geometry().topLeft();
        const QPoint logical = origin + (pos - origin) / m_screen->devicePixelRatio();
        ClickRipple *ripple = new ClickRipple(m_screen, logical);
        ripple->show();
    }

    return true;
}

void ScreenRecorder::showIndicator()
{
    QScreen *screen = m_screen ? m_screen.data() : qGuiApp->primaryScreen();

    if (!m_indicator) {
        m_indicator = new QQuickView;
        m_indicator->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                              | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
        m_indicator->setTitle(tr("Screen recording"));
        QSurfaceFormat format = m_indicator->format();
        format.setAlphaBufferSize(8);
        m_indicator->setFormat(format);
        m_indicator->setColor(Qt::transparent);
        m_indicator->setResizeMode(QQuickView::SizeViewToRootObject);
        m_indicator->rootContext()->setContextProperty("recorder", this);
        m_indicator->setSource(QUrl("qrc:/qml/RecordingIndicator.qml"));
    }

    m_indicator->setScreen(screen);

    // Keep the indicator out of the recorded area when possible.
    const QRect available = screen->availableGeometry();
    const qreal dpr = screen->devicePixelRatio();
    const QPoint origin = screen->geometry().topLeft();
    const QRect recorded(origin + (m_rect.topLeft() - origin) / dpr, m_rect.size() / dpr);
    const QSize size = m_indicator->size();
    const int margin = 16;

    const QList<QPoint> candidates {
        QPoint(available.right() - size.width() - margin, available.top() + margin),
        QPoint(available.right() - size.width() - margin, available.bottom() - size.height() - margin),
        QPoint(available.left() + margin, available.top() + margin),
        QPoint(available.left() + margin, available.bottom() - size.height() - margin),
    };

    QPoint pos = candidates.first();
    for (const QPoint &candidate : candidates) {
        if (!recorded.intersects(QRect(candidate, size))) {
            pos = candidate;
            break;
        }
    }

    m_indicator->setPosition(pos);
    m_indicator->show();

    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        if (!m_trayIcon) {
            m_trayIcon = new QSystemTrayIcon(QIcon::fromTheme("media-record", QIcon(":/images/record.svg")), this);
            m_trayIcon->setToolTip(tr("Screen recording in progress. Click to stop."));

            QMenu *menu = new QMenu;
            menu->addAction(QIcon::fromTheme("media-playback-stop"), tr("Stop recording"), this, &ScreenRecorder::stop);
            m_trayIcon->setContextMenu(menu);
            connect(m_trayIcon, &QObject::destroyed, menu, &QObject::deleteLater);

            connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                    stop();
            });
        }
        m_trayIcon->show();
    }
}

void ScreenRecorder::hideIndicator()
{
    if (m_indicator)
        m_indicator->hide();

    if (m_trayIcon)
        m_trayIcon->hide();
}

void ScreenRecorder::showError(const QString &message)
{
    if (m_failed)
        return;

    m_failed = true;
    hideIndicator();
    qCWarning(lcRecorder).noquote() << message;

    QMessageBox *box = new QMessageBox(QMessageBox::Critical, tr("Screen recording"), message, QMessageBox::Ok);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setWindowFlag(Qt::WindowStaysOnTopHint);
    connect(box, &QMessageBox::finished, this, &ScreenRecorder::finish);
    box->show();
}

void ScreenRecorder::notifySaved()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface(NOTIFY_SERVICE, NOTIFY_PATH, NOTIFY_INTERFACE, bus);

    if (!iface.isValid()) {
        finish();
        return;
    }

    QVariantMap hints;
    hints.insert("desktop-entry", "lingmo-screenshot");

    QList<QVariant> args;
    args << "lingmo-screenshot";
    args << ((unsigned int) 0);
    args << "lingmo-screenshot";
    args << tr("Recording saved");
    args << tr("The recording has been saved to %1").arg(m_fileName);
    args << (QStringList() << "default" << tr("Open")
                           << "open-folder" << tr("Open folder"));
    args << hints;
    args << (int) -1;

    QDBusReply<uint> reply = iface.callWithArgumentList(QDBus::Block, "Notify", args);
    if (!reply.isValid()) {
        finish();
        return;
    }

    m_notificationId = reply.value();

    bus.connect(NOTIFY_SERVICE, NOTIFY_PATH, NOTIFY_INTERFACE, "ActionInvoked",
                this, SLOT(onNotificationActionInvoked(uint, QString)));
    bus.connect(NOTIFY_SERVICE, NOTIFY_PATH, NOTIFY_INTERFACE, "NotificationClosed",
                this, SLOT(onNotificationClosed(uint, uint)));

    // Give up the D-Bus names while waiting for the notification actions.
    bus.unregisterObject(RECORDER_PATH);
    bus.unregisterService(RECORDER_SERVICE);

    QTimer::singleShot(NOTIFICATION_TIMEOUT, this, &ScreenRecorder::finish);
}

void ScreenRecorder::onNotificationActionInvoked(uint id, const QString &action)
{
    if (id != m_notificationId)
        return;

    if (action == "default")
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_fileName));
    else if (action == "open-folder")
        openFolder();

    QTimer::singleShot(500, this, &ScreenRecorder::finish);
}

void ScreenRecorder::onNotificationClosed(uint id, uint reason)
{
    Q_UNUSED(reason)

    if (id == m_notificationId)
        QTimer::singleShot(500, this, &ScreenRecorder::finish);
}

void ScreenRecorder::openFolder()
{
    QDBusMessage msg = QDBusMessage::createMethodCall("org.freedesktop.FileManager1",
                                                      "/org/freedesktop/FileManager1",
                                                      "org.freedesktop.FileManager1",
                                                      "ShowItems");
    msg << QStringList(QUrl::fromLocalFile(m_fileName).toString()) << QString();
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 3000);

    if (reply.type() != QDBusMessage::ReplyMessage)
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_fileName).absolutePath()));
}

void ScreenRecorder::finish()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterObject(RECORDER_PATH);
    bus.unregisterService(RECORDER_SERVICE);

    emit done();
}
