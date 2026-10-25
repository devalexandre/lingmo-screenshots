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

#include "screenaskcore.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QPainter>
#include <QUrlQuery>

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ScreenAskCore", text);
}

QByteArray encode(const QImage &image, const char *format, int quality)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format, quality);
    return data;
}

}

QByteArray ScreenAskCore::encodeImage(const QImage &image, int maxSide, int maxBytes, QByteArray *format)
{
    if (image.isNull())
        return {};
    QImage out = image;
    if (qMax(out.width(), out.height()) > maxSide)
        out = out.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // No transparency for the model: it would turn black in some decoders
    if (out.hasAlphaChannel()) {
        QImage flat(out.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, out);
        p.end();
        out = flat;
    }
    QByteArray data = encode(out, "PNG", -1);
    if (data.size() <= maxBytes) {
        if (format)
            *format = "png";
        return data;
    }
    for (int quality : {90, 80, 65}) {
        data = encode(out, "JPEG", quality);
        if (data.size() <= maxBytes)
            break;
    }
    if (format)
        *format = "jpeg";
    return data;
}

QRectF ScreenAskCore::strokeBounds(const QVector<QPointF> &points, qreal padding, const QRectF &bounds)
{
    if (points.size() < 2)
        return {};
    qreal x1 = points.first().x(), x2 = x1, y1 = points.first().y(), y2 = y1;
    for (const QPointF &p : points) {
        x1 = qMin(x1, p.x());
        x2 = qMax(x2, p.x());
        y1 = qMin(y1, p.y());
        y2 = qMax(y2, p.y());
    }
    // A stroke that is almost a line (an underline, a slip): nothing selected
    if (x2 - x1 < 4 || y2 - y1 < 4)
        return {};
    const QRectF r = QRectF(QPointF(x1, y1), QPointF(x2, y2)).adjusted(-padding, -padding, padding, padding);
    return bounds.isValid() ? r.intersected(bounds) : r;
}

QString ScreenAskCore::languageName(const QLocale &locale)
{
    QString language = locale.nativeLanguageName();
    if (language.isEmpty())
        language = QLocale::languageToString(locale.language());
    const QString territory = locale.nativeTerritoryName();
    if (!territory.isEmpty() && locale.language() != QLocale::C)
        return QStringLiteral("%1 (%2)").arg(language, territory);
    return language;
}

QString ScreenAskCore::chipQuestion(const QString &kind, const QLocale &locale)
{
    if (kind == QLatin1String("explain"))
        return tr("Explain what is in this area of the screen.");
    if (kind == QLatin1String("translate"))
        return tr("Translate the text in this area into %1. Reply with only the translation.").arg(languageName(locale));
    if (kind == QLatin1String("summarize"))
        return tr("Summarize what this area shows.");
    if (kind == QLatin1String("what"))
        return tr("What is this?");
    if (kind == QLatin1String("copy"))
        return tr("Write out exactly all the text in this area, keeping its lines, and nothing else.");
    return {};
}

QUrl ScreenAskCore::webSearchUrl(const QString &text, int maxLength)
{
    QString query = text.simplified();
    if (query.isEmpty())
        return {};
    if (query.size() > maxLength) {
        query.truncate(maxLength);
        const int space = query.lastIndexOf(QLatin1Char(' '));
        if (space > maxLength / 2)
            query.truncate(space);
    }
    QUrl url(QStringLiteral("https://duckduckgo.com/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(q);
    return url;
}
