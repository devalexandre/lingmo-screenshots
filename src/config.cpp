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

#include "config.h"
#include "filenamepattern.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QStandardPaths>
#include <QUrl>

namespace {

const int MAX_RECENT_COLORS = 8;

}

Config::Config(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("lingmoos"), QStringLiteral("lingmo-screenshot"))
{
}

QVariant Config::value(const QString &key, const QVariant &def) const
{
    return m_settings.value(key, def);
}

void Config::set(const QString &key, const QVariant &value)
{
    if (m_settings.value(key) == value)
        return;

    m_settings.setValue(key, value);
    m_settings.sync();
    emit changed();
}

// Saving

QString Config::defaultSaveFolder() const
{
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty())
        pictures = QDir::homePath();
    return QDir(pictures).filePath(tr("Screenshots"));
}

QString Config::saveFolder() const
{
    const QString folder = value(QStringLiteral("Save/folder"), QString()).toString();
    return folder.isEmpty() ? defaultSaveFolder() : folder;
}

void Config::setSaveFolder(const QString &value)
{
    QString folder = value;
    if (folder.startsWith(QLatin1String("file://")))
        folder = QUrl(folder).toLocalFile();
    set(QStringLiteral("Save/folder"), folder == defaultSaveFolder() ? QString() : folder);
}

QString Config::defaultFileNamePattern() const
{
    return tr("Screenshot") + QStringLiteral("_%Y-%m-%d_%H-%M-%S");
}

QString Config::fileNamePattern() const
{
    const QString pattern = value(QStringLiteral("Save/pattern"), QString()).toString();
    return pattern.trimmed().isEmpty() ? defaultFileNamePattern() : pattern;
}

void Config::setFileNamePattern(const QString &value)
{
    set(QStringLiteral("Save/pattern"), value == defaultFileNamePattern() ? QString() : value);
}

QString Config::imageFormat() const
{
    const QString format = value(QStringLiteral("Save/format"), QStringLiteral("png")).toString();
    if (format == QLatin1String("jpg") || format == QLatin1String("webp"))
        return format;
    return QStringLiteral("png");
}

void Config::setImageFormat(const QString &value) { set(QStringLiteral("Save/format"), value); }
bool Config::copyAfterSave() const { return value(QStringLiteral("Save/copyAfterSave"), false).toBool(); }
void Config::setCopyAfterSave(bool value) { set(QStringLiteral("Save/copyAfterSave"), value); }

// Capture

bool Config::playSound() const { return value(QStringLiteral("Capture/sound"), true).toBool(); }
void Config::setPlaySound(bool value) { set(QStringLiteral("Capture/sound"), value); }
bool Config::showHelp() const { return value(QStringLiteral("Capture/showHelp"), true).toBool(); }
void Config::setShowHelp(bool value) { set(QStringLiteral("Capture/showHelp"), value); }
bool Config::showMagnifier() const { return value(QStringLiteral("Capture/magnifier"), true).toBool(); }
void Config::setShowMagnifier(bool value) { set(QStringLiteral("Capture/magnifier"), value); }
bool Config::smartSelection() const { return value(QStringLiteral("Capture/smartSelection"), true).toBool(); }
void Config::setSmartSelection(bool value) { set(QStringLiteral("Capture/smartSelection"), value); }

// History

bool Config::historyEnabled() const { return value(QStringLiteral("History/enabled"), true).toBool(); }
void Config::setHistoryEnabled(bool value) { set(QStringLiteral("History/enabled"), value); }
int Config::historySize() const { return qBound(1, value(QStringLiteral("History/size"), 30).toInt(), 500); }
void Config::setHistorySize(int value) { set(QStringLiteral("History/size"), qBound(1, value, 500)); }

// OCR

QString Config::ocrLanguages() const
{
    // Empty: automatic (the script is detected; the installed Latin packs
    // when it can't be). A stored value only orders the Latin packs.
    return value(QStringLiteral("Ocr/languages"), QString()).toString();
}

void Config::setOcrLanguages(const QString &value) { set(QStringLiteral("Ocr/languages"), value.trimmed()); }

// Send to

QString Config::uploadName() const { return value(QStringLiteral("Upload/name"), QString()).toString(); }
void Config::setUploadName(const QString &value) { set(QStringLiteral("Upload/name"), value.trimmed()); }
QString Config::uploadCommand() const { return value(QStringLiteral("Upload/command"), QString()).toString(); }
void Config::setUploadCommand(const QString &value) { set(QStringLiteral("Upload/command"), value.trimmed()); }

bool Config::nearbyShareAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("lingmo-share")).isEmpty();
}

bool Config::uploadEnabled() const
{
    return uploadCommand().contains(QLatin1String("%f"));
}

// Beautify

int Config::beautifyPadding() const { return qBound(0, value(QStringLiteral("Beautify/padding"), 48).toInt(), 200); }
void Config::setBeautifyPadding(int value) { set(QStringLiteral("Beautify/padding"), value); }
int Config::beautifyRadius() const { return qBound(0, value(QStringLiteral("Beautify/radius"), 12).toInt(), 64); }
void Config::setBeautifyRadius(int value) { set(QStringLiteral("Beautify/radius"), value); }
bool Config::beautifyShadow() const { return value(QStringLiteral("Beautify/shadow"), true).toBool(); }
void Config::setBeautifyShadow(bool value) { set(QStringLiteral("Beautify/shadow"), value); }

int Config::beautifyBackground() const
{
    return qBound(0, value(QStringLiteral("Beautify/background"), 0).toInt(), int(Beautify::backgrounds().size()) - 1);
}

void Config::setBeautifyBackground(int value) { set(QStringLiteral("Beautify/background"), value); }

QVariantList Config::beautifyBackgrounds() const
{
    QVariantList list;
    for (const auto &pair : Beautify::backgrounds())
        list.append(QVariant::fromValue(QVariantList { pair.first, pair.second }));
    return list;
}

Beautify::Options Config::beautifyOptions() const
{
    Beautify::Options o;
    o.padding = beautifyPadding();
    o.radius = beautifyRadius();
    o.shadow = beautifyShadow();
    o.background = beautifyBackground();
    return o;
}

// Tools

QColor Config::toolColor() const
{
    const QColor c(value(QStringLiteral("Tools/color"), QStringLiteral("#e53935")).toString());
    return c.isValid() ? c : QColor(0xe5, 0x39, 0x35);
}

void Config::setToolColor(const QColor &value) { set(QStringLiteral("Tools/color"), value.name(QColor::HexArgb)); }
int Config::toolWidth() const { return qBound(1, value(QStringLiteral("Tools/width"), 4).toInt(), 60); }
void Config::setToolWidth(int value) { set(QStringLiteral("Tools/width"), qBound(1, value, 60)); }
int Config::textSize() const { return qBound(8, value(QStringLiteral("Tools/textSize"), 24).toInt(), 200); }
void Config::setTextSize(int value) { set(QStringLiteral("Tools/textSize"), qBound(8, value, 200)); }
bool Config::textBold() const { return value(QStringLiteral("Tools/textBold"), false).toBool(); }
void Config::setTextBold(bool value) { set(QStringLiteral("Tools/textBold"), value); }

QStringList Config::recentColors() const
{
    return value(QStringLiteral("Tools/recentColors"), QStringList()).toStringList();
}

QStringList Config::palette() const
{
    // Annotation colours (part of the image, not of the interface).
    return {
        QStringLiteral("#e53935"), QStringLiteral("#fb8c00"), QStringLiteral("#fdd835"), QStringLiteral("#43a047"),
        QStringLiteral("#00897b"), QStringLiteral("#1e88e5"), QStringLiteral("#3949ab"), QStringLiteral("#8e24aa"),
        QStringLiteral("#d81b60"), QStringLiteral("#6d4c41"), QStringLiteral("#000000"), QStringLiteral("#616161"),
        QStringLiteral("#bdbdbd"), QStringLiteral("#ffffff"),
    };
}

void Config::addRecentColor(const QColor &color)
{
    if (!color.isValid())
        return;

    QStringList colors = recentColors();
    const QString name = color.name(QColor::HexArgb);
    colors.removeAll(name);
    colors.prepend(name);
    while (colors.size() > MAX_RECENT_COLORS)
        colors.removeLast();
    set(QStringLiteral("Tools/recentColors"), colors);
}

// Recording (the keys of the first version are kept)

bool Config::recordMicrophone() const { return value(QStringLiteral("Recording/microphone"), false).toBool(); }
void Config::setRecordMicrophone(bool value) { set(QStringLiteral("Recording/microphone"), value); }
bool Config::recordSystemAudio() const { return value(QStringLiteral("Recording/systemAudio"), false).toBool(); }
void Config::setRecordSystemAudio(bool value) { set(QStringLiteral("Recording/systemAudio"), value); }
bool Config::recordShowClicks() const { return value(QStringLiteral("Recording/showClicks"), false).toBool(); }
void Config::setRecordShowClicks(bool value) { set(QStringLiteral("Recording/showClicks"), value); }

QString Config::recordFormat() const
{
    return value(QStringLiteral("Recording/format"), QStringLiteral("mp4")).toString() == QLatin1String("gif")
            ? QStringLiteral("gif") : QStringLiteral("mp4");
}

void Config::setRecordFormat(const QString &value) { set(QStringLiteral("Recording/format"), value); }

int Config::recordFrameRate() const
{
    return qBound(5, value(QStringLiteral("Recording/frameRate"), 30).toInt(), 60);
}

void Config::setRecordFrameRate(int value) { set(QStringLiteral("Recording/frameRate"), qBound(5, value, 60)); }

QString Config::previewFileName(const QString &pattern) const
{
    FileNamePattern::Context context;
    context.time = QDateTime::currentDateTime();
    context.width = 1280;
    context.height = 720;
    context.windowTitle = tr("Window");
    return FileNamePattern::format(pattern.isEmpty() ? defaultFileNamePattern() : pattern, context)
            + QLatin1Char('.') + imageFormat();
}

void Config::chooseSaveFolder()
{
    QDir().mkpath(saveFolder());
    const QString dir = QFileDialog::getExistingDirectory(nullptr, tr("Choose the folder for screenshots"), saveFolder());
    if (!dir.isEmpty())
        setSaveFolder(dir);
}

void Config::openSaveFolder()
{
    QDir().mkpath(saveFolder());
    QDesktopServices::openUrl(QUrl::fromLocalFile(saveFolder()));
}
