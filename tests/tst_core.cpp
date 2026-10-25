/*
 * Copyright (C) 2026 LingmoOS Team.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 */

#include <QtTest>
#include <QPainter>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QUrlQuery>
#include <cmath>

#include "annotationmodel.h"
#include "annotationrenderer.h"
#include "beautify.h"
#include "filenamepattern.h"
#include "imageanalysis.h"
#include "ocr.h"
#include "desktopapps.h"
#include "commandline.h"
#include "screenaskcore.h"

class TestCore : public QObject
{
    Q_OBJECT

private slots:
    // Annotation model / undo
    void drawAndUndoRedo();
    void emptyShapesAreDropped();
    void constrainedShapes();
    void counterNumbering();
    void selectMoveDelete();
    void textEditing();
    void redactEffectsAreOneStep();
    void jsonRoundTrip();
    void hitTesting();

    // Rendering
    void composeCropsAndDraws();
    void effectsReadTheBaseImage();
    void counterContrast();

    // File names
    void patternTokens();
    void patternSanitizes();
    void uniquePaths();

    // Beautify
    void beautifyGeometry();
    void beautifyRender();

    // OCR parsing
    void parseTsv();
    void redactBoxesMergeLines();
    void parseLanguages();
    void languageArgument();
    void scaleBox();
    void parseOsd();
    void scriptLanguages();
    void hebrewUsesHebWithPorEngPreferred();
    void garbageDetection();
    void wordsToText();
    void ocrJobDetectsHebrew();

    // Smart selection and long captures
    void regionDetection();
    void stitching();
    void stitchingWithFooter();

    // Open with
    void execExpansion();

    // Command line
    void commandLine();
    void commandLineErrors();
    void commandLineRoundTrip();

    // Ask about the screen
    void askCommandLine();
    void askStrokeBounds();
    void askEncodeImage();
    void askChipsAndSearch();
};

void TestCore::drawAndUndoRedo()
{
    AnnotationModel m;
    QSignalSpy spy(&m, &AnnotationModel::changed);

    QVERIFY(m.begin("rectangle", QPointF(10, 10), Qt::red, 3));
    QVERIFY(m.drawing());
    m.extend(QPointF(50, 40));
    QVERIFY(m.finish());
    QCOMPARE(m.count(), 1);
    QVERIFY(m.canUndo());
    QVERIFY(!m.canRedo());
    QCOMPARE(m.items().first().rect(), QRectF(10, 10, 40, 30));

    QVERIFY(m.begin("pencil", QPointF(0, 0), Qt::blue, 2));
    m.extend(QPointF(5, 5));
    m.extend(QPointF(10, 5));
    QVERIFY(m.finish());
    QCOMPARE(m.count(), 2);
    QCOMPARE(m.items().last().points.size(), 3);

    m.undo();
    QCOMPARE(m.count(), 1);
    QVERIFY(m.canRedo());
    m.undo();
    QCOMPARE(m.count(), 0);
    QVERIFY(!m.canUndo());
    m.redo();
    m.redo();
    QCOMPARE(m.count(), 2);
    QCOMPARE(m.items().last().type, Annotation::Pencil);

    // A new change drops the redo history.
    m.undo();
    m.addCounter(QPointF(1, 1), Qt::red, 3);
    QVERIFY(!m.canRedo());
    QCOMPARE(m.count(), 2);

    m.clear();
    QCOMPARE(m.count(), 0);
    m.undo();
    QCOMPARE(m.count(), 2);

    QVERIFY(spy.count() > 0);
    QVERIFY(!m.begin("nonsense", QPointF(), Qt::red, 1));
    QVERIFY(!m.begin("text", QPointF(), Qt::red, 1));
}

void TestCore::emptyShapesAreDropped()
{
    AnnotationModel m;
    m.begin("arrow", QPointF(10, 10), Qt::red, 3);
    QVERIFY(!m.finish());
    QCOMPARE(m.count(), 0);
    QVERIFY(!m.canUndo());

    m.begin("blur", QPointF(10, 10), Qt::red, 3);
    m.extend(QPointF(11, 40));
    QVERIFY(!m.finish());

    m.addText(QPointF(1, 1), "   ", Qt::red, 20, false);
    QCOMPARE(m.count(), 0);

    // A click with the pencil leaves a dot.
    m.begin("pencil", QPointF(3, 3), Qt::red, 3);
    QVERIFY(m.finish());
    QCOMPARE(m.count(), 1);

    m.begin("ellipse", QPointF(3, 3), Qt::red, 3);
    m.extend(QPointF(30, 30));
    m.cancel();
    QCOMPARE(m.count(), 1);
}

void TestCore::constrainedShapes()
{
    AnnotationModel m;
    m.begin("rectangle", QPointF(0, 0), Qt::red, 2);
    m.extend(QPointF(40, -10), true);
    m.finish();
    QCOMPARE(m.items().last().rect().size(), QSizeF(40, 40));

    m.begin("line", QPointF(0, 0), Qt::red, 2);
    m.extend(QPointF(100, 3), true);
    m.finish();
    QCOMPARE(m.items().last().points.last().y(), 0.0);
}

void TestCore::counterNumbering()
{
    AnnotationModel m;
    QCOMPARE(m.nextNumber(), 1);
    m.addCounter(QPointF(10, 10), Qt::red, 3);
    m.addCounter(QPointF(50, 10), Qt::red, 3);
    QCOMPARE(m.items().at(1).number, 2);
    m.undo();
    QCOMPARE(m.nextNumber(), 2);
    m.addCounter(QPointF(80, 10), Qt::red, 3);
    QCOMPARE(m.items().last().number, 2);
}

void TestCore::selectMoveDelete()
{
    AnnotationModel m;
    m.begin("filledRectangle", QPointF(10, 10), Qt::red, 3);
    m.extend(QPointF(60, 60));
    m.finish();
    m.begin("line", QPointF(100, 100), Qt::red, 4);
    m.extend(QPointF(200, 100));
    m.finish();

    QCOMPARE(m.itemAt(QPointF(30, 30)), 0);
    QCOMPARE(m.itemAt(QPointF(150, 101)), 1);
    QCOMPARE(m.itemAt(QPointF(150, 130)), -1);

    m.setSelectedIndex(0);
    m.beginMove();
    m.moveSelected(QPointF(5, 0));
    m.moveSelected(QPointF(5, 10));
    m.endMove();
    QCOMPARE(m.items().first().rect(), QRectF(20, 20, 50, 50));

    // The whole drag is one undo step.
    m.undo();
    QCOMPARE(m.items().first().rect(), QRectF(10, 10, 50, 50));
    m.redo();
    QCOMPARE(m.items().first().rect(), QRectF(20, 20, 50, 50));

    m.setSelectedIndex(0);
    m.setSelectedColor(Qt::green);
    QCOMPARE(m.items().first().color, QColor(Qt::green));
    m.undo();
    QCOMPARE(m.items().first().color, QColor(Qt::red));

    m.setSelectedIndex(1);
    QVERIFY(m.removeSelected());
    QCOMPARE(m.count(), 1);
    QCOMPARE(m.selectedIndex(), -1);
    m.undo();
    QCOMPARE(m.count(), 2);
    QVERIFY(!m.removeAt(7));
}

void TestCore::textEditing()
{
    AnnotationModel m;
    m.addText(QPointF(5, 6), "Hello\nworld", Qt::blue, 24, true);
    QCOMPARE(m.count(), 1);
    QVERIFY(m.items().first().textRect().height() > 40);
    QVERIFY(m.items().first().contains(QPointF(10, 12), 2));

    const QVariantMap t = m.takeText(0);
    QCOMPARE(m.count(), 0);
    QCOMPARE(t.value("text").toString(), QString("Hello\nworld"));
    QCOMPARE(t.value("bold").toBool(), true);
    QCOMPARE(t.value("size").toReal(), 24.0);
    m.undo();
    QCOMPARE(m.count(), 1);
    QVERIFY(m.takeText(5).isEmpty());
}

void TestCore::redactEffectsAreOneStep()
{
    AnnotationModel m;
    const int n = m.addEffects("pixelate", { QRectF(0, 0, 20, 10), QRectF(0, 20, 30, 10), QRectF(0, 0, 1, 1) }, 4);
    QCOMPARE(n, 2);
    QCOMPARE(m.count(), 2);
    m.undo();
    QCOMPARE(m.count(), 0);
    QCOMPARE(m.addEffects("pencil", { QRectF(0, 0, 20, 10) }, 4), 0);
}

void TestCore::jsonRoundTrip()
{
    AnnotationModel m;
    m.begin("arrow", QPointF(1, 2), QColor("#80ff0000"), 5);
    m.extend(QPointF(30, 40));
    m.finish();
    m.addText(QPointF(5, 5), "Olá", Qt::black, 18, true);
    m.addCounter(QPointF(9, 9), Qt::yellow, 2);

    const QJsonArray json = m.toJson();
    AnnotationModel other;
    QJsonArray withJunk = json;
    withJunk.append(QJsonObject{ { "type", "bogus" } });
    withJunk.append(QJsonObject{ { "type", "line" }, { "color", "#ff0000" }, { "points", QJsonArray{ 1 } } });
    other.fromJson(withJunk);
    QCOMPARE(other.count(), 3);
    QVERIFY(!other.canUndo());
    for (int i = 0; i < 3; ++i)
        QVERIFY(other.items().at(i) == m.items().at(i));
}

void TestCore::hitTesting()
{
    Annotation ellipse;
    ellipse.type = Annotation::Ellipse;
    ellipse.width = 2;
    ellipse.points = { QPointF(0, 0), QPointF(100, 50) };
    QVERIFY(ellipse.contains(QPointF(50, 0), 3));
    // Inside an outline is not "on" it.
    QVERIFY(!ellipse.contains(QPointF(50, 25), 3));

    Annotation counter;
    counter.type = Annotation::Counter;
    counter.width = 2;
    counter.points = { QPointF(50, 50) };
    QVERIFY(counter.contains(QPointF(55, 55), 0));
    QVERIFY(!counter.contains(QPointF(50 + counter.counterRadius() + 5, 50 + counter.counterRadius() + 5), 0));
    QVERIFY(counter.boundingRect().contains(QPointF(50, 50)));
}

void TestCore::composeCropsAndDraws()
{
    QImage base(200, 100, QImage::Format_RGB32);
    base.fill(Qt::white);

    Annotation rect;
    rect.type = Annotation::FilledRectangle;
    rect.color = Qt::red;
    rect.points = { QPointF(50, 20), QPointF(70, 40) };

    const QImage out = AnnotationRenderer::compose(base, { rect }, QRect(40, 10, 100, 50));
    QCOMPARE(out.size(), QSize(100, 50));
    QCOMPARE(QColor(out.pixel(15, 15)), QColor(Qt::red));
    QCOMPARE(QColor(out.pixel(5, 5)), QColor(Qt::white));
    // The base is untouched.
    QCOMPARE(QColor(base.pixel(55, 25)), QColor(Qt::white));

    QVERIFY(AnnotationRenderer::compose(base, {}, QRect(500, 500, 10, 10)).isNull());
    QCOMPARE(AnnotationRenderer::compose(base, {}, QRect()).size(), base.size());
}

void TestCore::effectsReadTheBaseImage()
{
    // Black/white stripes: pixelating removes the detail.
    QImage base(64, 64, QImage::Format_RGB32);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            base.setPixel(x, y, (x % 2) ? qRgb(0, 0, 0) : qRgb(255, 255, 255));

    Annotation pix;
    pix.type = Annotation::Pixelate;
    pix.width = 4;
    pix.points = { QPointF(0, 0), QPointF(32, 32) };

    Annotation inv;
    inv.type = Annotation::Invert;
    inv.points = { QPointF(40, 40), QPointF(64, 64) };

    const QImage out = AnnotationRenderer::compose(base, { pix, inv }, QRect());
    const QColor a(out.pixel(4, 4)), b(out.pixel(5, 4));
    QCOMPARE(a, b);
    QVERIFY(qAbs(a.red() - 128) < 40);
    // Outside the effect nothing changes.
    QCOMPARE(QColor(out.pixel(33, 33)), QColor(base.pixel(33, 33)));
    // Inverted.
    QCOMPARE(QColor(out.pixel(41, 41)).red(), 255 - QColor(base.pixel(41, 41)).red());

    const QImage blurred = AnnotationRenderer::blur(base, 6);
    QVERIFY(qAbs(QColor(blurred.pixel(20, 20)).red() - 128) < 40);
}

void TestCore::counterContrast()
{
    QCOMPARE(AnnotationRenderer::counterTextColor(Qt::yellow), QColor(20, 20, 20));
    QCOMPARE(AnnotationRenderer::counterTextColor(QColor(229, 57, 53)), QColor(255, 255, 255));
}

void TestCore::patternTokens()
{
    FileNamePattern::Context c;
    c.time = QDateTime(QDate(2026, 3, 7), QTime(9, 5, 4));
    c.width = 640;
    c.height = 480;
    c.windowTitle = "Terminal";

    QCOMPARE(FileNamePattern::format("Shot %Y-%m-%d %H-%M-%S", c), QString("Shot 2026-03-07 09-05-04"));
    QCOMPARE(FileNamePattern::format("%y%j_%wx%h_%t", c), QString("26066_640x480_Terminal"));
    QCOMPARE(FileNamePattern::format("100%% %q", c), QString("100% %q"));
    QCOMPARE(FileNamePattern::format("end%", c), QString("end%"));
    QCOMPARE(FileNamePattern::format("%s", c), QString::number(c.time.toSecsSinceEpoch()));
}

void TestCore::patternSanitizes()
{
    FileNamePattern::Context c;
    c.time = QDateTime(QDate(2026, 1, 1), QTime(0, 0));
    c.windowTitle = "a/b:c*?";
    QCOMPARE(FileNamePattern::format("../%t", c), QString("_a_b_c__"));
    QVERIFY(!FileNamePattern::format("../../etc/passwd", c).contains('/'));
    QCOMPARE(FileNamePattern::format("", c), QString("screenshot"));
    QCOMPARE(FileNamePattern::format("...", c), QString("screenshot"));
    QCOMPARE(FileNamePattern::format(".hidden", c), QString("hidden"));
    QVERIFY(FileNamePattern::format(QString(500, 'x'), c).size() <= 200);
}

void TestCore::uniquePaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FileNamePattern::Context c;
    c.time = QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5));

    const QString first = FileNamePattern::uniquePath(dir.path(), "cap_%Y", c, "png");
    QCOMPARE(first, dir.filePath("cap_2026.png"));
    QFile f(first);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    QCOMPARE(FileNamePattern::uniquePath(dir.path(), "cap_%Y", c, "png"), dir.filePath("cap_2026 (2).png"));
    QCOMPARE(FileNamePattern::uniquePath(dir.path(), "cap_%Y", c, "jpg"), dir.filePath("cap_2026.jpg"));

    QCOMPARE(FileNamePattern::formatForPath("/x/a.JPEG"), QString("jpg"));
    QCOMPARE(FileNamePattern::formatForPath("/x/a.webp"), QString("webp"));
    QCOMPARE(FileNamePattern::formatForPath("/x/a"), QString("png"));
}

void TestCore::beautifyGeometry()
{
    Beautify::Options o;
    o.padding = 40;
    o.radius = 500;
    o.shadow = true;
    Beautify::Geometry g = Beautify::geometry(QSize(300, 100), o);
    QCOMPARE(g.canvas, QSize(380, 180));
    QCOMPARE(g.image, QRect(40, 40, 300, 100));
    // Radius never more than half the short side.
    QCOMPARE(g.radius, 50);
    QVERIFY(g.shadowBlur > 0 && g.shadowBlur <= 40);
    QVERIFY(!g.shadow.isEmpty());
    // The blurred shadow stays inside the canvas.
    QVERIFY(QRect(QPoint(0, 0), g.canvas).contains(g.shadow.adjusted(-g.shadowBlur / 2, 0, g.shadowBlur / 2, g.shadowBlur / 2)));

    o.padding = 0;
    g = Beautify::geometry(QSize(300, 100), o);
    QCOMPARE(g.canvas, QSize(300, 100));
    QVERIFY(g.shadow.isEmpty());

    o.padding = -5;
    QCOMPARE(Beautify::geometry(QSize(10, 10), o).canvas, QSize(10, 10));
    QVERIFY(Beautify::geometry(QSize(), o).canvas.isEmpty());
    QVERIFY(Beautify::backgrounds().size() >= 4);
}

void TestCore::beautifyRender()
{
    QImage img(100, 60, QImage::Format_RGB32);
    img.fill(Qt::red);
    Beautify::Options o;
    o.padding = 20;
    o.radius = 10;
    o.background = 99; // clamped
    const QImage out = Beautify::render(img, o);
    QCOMPARE(out.size(), QSize(140, 100));
    QCOMPARE(QColor(out.pixel(70, 50)), QColor(Qt::red));
    // Rounded corner: the very corner of the capture is background.
    QVERIFY(QColor(out.pixel(20, 20)) != QColor(Qt::red));
    QCOMPARE(qAlpha(out.pixel(1, 1)), 255);
}

void TestCore::parseTsv()
{
    const QByteArray tsv =
        "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n"
        "1\t1\t0\t0\t0\t0\t0\t0\t400\t200\t-1\t\n"
        "4\t1\t1\t1\t1\t0\t10\t10\t200\t20\t-1\t\n"
        "5\t1\t1\t1\t1\t1\t10\t10\t50\t20\t96.5\tOlá\n"
        "5\t1\t1\t1\t1\t2\t70\t12\t60\t18\t91\tmundo\r\n"
        "5\t1\t1\t1\t2\t1\t10\t40\t80\t20\t12\t~\n"
        "5\t1\t1\t1\t2\t2\t10\t40\t0\t20\t90\tzero\n"
        "garbage line\n";
    const QVector<Ocr::Word> words = Ocr::parseTsv(tsv);
    QCOMPARE(words.size(), 3);
    QCOMPARE(words.at(0).text, QString::fromUtf8("Olá"));
    QCOMPARE(words.at(0).confidence, 97);
    QCOMPARE(words.at(1).box, QRect(70, 12, 60, 18));
    QCOMPARE(words.at(1).text, QString("mundo"));
    QCOMPARE(words.at(2).line, 2);
}

void TestCore::redactBoxesMergeLines()
{
    QVector<Ocr::Word> words(4);
    words[0].box = QRect(10, 10, 50, 20); words[0].text = "one"; words[0].confidence = 90; words[0].line = 1;
    words[1].box = QRect(70, 12, 40, 20); words[1].text = "two"; words[1].confidence = 80; words[1].line = 1;
    words[2].box = QRect(10, 50, 40, 20); words[2].text = "three"; words[2].confidence = 10; words[2].line = 2;
    words[3].box = QRect(10, 80, 10, 10); words[3].text = "|"; words[3].confidence = 95; words[3].line = 3;

    const QVector<QRect> boxes = Ocr::redactBoxes(words, 2);
    QCOMPARE(boxes.size(), 1);
    QCOMPARE(boxes.first(), QRect(8, 8, 104, 26));
    QCOMPARE(Ocr::redactBoxes(words, 0, 0).size(), 2);
}

void TestCore::parseLanguages()
{
    const QByteArray out = "List of available languages in \"/usr/share/tessdata/\" (3):\neng\nosd\npor\n";
    QCOMPARE(Ocr::parseLanguages(out), QStringList({ "eng", "por" }));
    QVERIFY(Ocr::parseLanguages("").isEmpty());
}

void TestCore::languageArgument()
{
    QCOMPARE(Ocr::languageArgument("por+eng", { "eng", "por" }), QString("por+eng"));
    QCOMPARE(Ocr::languageArgument("por+eng", { "eng" }), QString("eng"));
    QCOMPARE(Ocr::languageArgument("deu", { "fra", "spa" }), QString("fra"));
    QCOMPARE(Ocr::languageArgument("por", {}), QString());
}

void TestCore::scaleBox()
{
    QCOMPARE(Ocr::scaleBox(QRect(20, 10, 41, 21), 2), QRect(10, 5, 21, 11));
    QCOMPARE(Ocr::scaleBox(QRect(20, 10, 40, 20), 1), QRect(20, 10, 40, 20));
}

void TestCore::parseOsd()
{
    const QByteArray out = "Page number: 0\nOrientation in degrees: 0\nRotate: 0\nOrientation confidence: 6.61\n"
                           "Script: Hebrew\nScript confidence: 7.22\n";
    const Ocr::Osd osd = Ocr::parseOsd(out);
    QCOMPARE(osd.script, QStringLiteral("Hebrew"));
    QCOMPARE(osd.confidence, 7.22);
    QVERIFY(Ocr::osdConfident(osd));
    // A tiny crop: tesseract gives up, or guesses with no confidence
    QVERIFY(!Ocr::parseOsd("Too few characters. Skipping this page\nError during processing.\n").isValid());
    QVERIFY(!Ocr::parseOsd("Script: Katakana\nScript confidence: 0.00\n").isValid());
    QVERIFY(!Ocr::osdConfident(Ocr::parseOsd("Script: Cyrillic\nScript confidence: 1.10\n")));
    // Han vs Japanese: a small margin is normal
    QVERIFY(Ocr::osdConfident(Ocr::parseOsd("Script: Han\nScript confidence: 0.96\n")));
    QVERIFY(!Ocr::osdConfident(Ocr::parseOsd("Script: Fraktur\nScript confidence: 9\n")));
}

void TestCore::scriptLanguages()
{
    QCOMPARE(Ocr::scriptPacks("Hebrew"), QStringList({"heb"}));
    QCOMPARE(Ocr::scriptPacks("Arabic"), QStringList({"ara"}));
    QCOMPARE(Ocr::scriptPacks("Cyrillic"), QStringList({"rus", "ukr"}));
    QCOMPARE(Ocr::scriptPacks("Han"), QStringList({"chi_sim", "chi_tra"}));
    QCOMPARE(Ocr::scriptPacks("Japanese"), QStringList({"jpn"}));
    QCOMPARE(Ocr::scriptPacks("Hangul"), QStringList({"kor"}));
    QCOMPARE(Ocr::scriptPacks("Greek"), QStringList({"ell"}));
    QCOMPARE(Ocr::scriptPacks("Devanagari"), QStringList({"hin"}));
    QCOMPARE(Ocr::scriptPacks("Thai"), QStringList({"tha"}));
    QCOMPARE(Ocr::scriptPacks("Bengali"), QStringList({"ben"}));
    QCOMPARE(Ocr::scriptPacks("Tamil"), QStringList({"tam"}));
    QVERIFY(Ocr::scriptPacks("Latin").isEmpty());

    // Latin: the preferred ones first, then every other installed Latin pack
    const QStringList installed = {"afr", "deu", "eng", "heb", "ita", "por", "spa"};
    QCOMPARE(Ocr::latinLanguages("por+eng", installed), QStringList({"por", "eng", "spa", "deu", "ita"}));
    QCOMPARE(Ocr::latinLanguages("", installed), QStringList({"eng", "por", "spa", "deu", "ita"}));
    QCOMPARE(Ocr::latinLanguages("", installed, 7), QStringList({"eng", "por", "spa", "deu", "ita", "afr"}));
    QCOMPARE(Ocr::latinLanguages("xyz", {"por"}), QStringList({"por"}));

    // A script whose pack is missing
    Ocr::Plan plan = Ocr::plan("Arabic", "por+eng", {"eng", "por"});
    QVERIFY(plan.languages.isEmpty());
    QCOMPARE(plan.missing, QStringList({"ara"}));
    // ... and with it: mixed with Latin (ara+eng)
    plan = Ocr::plan("Arabic", "por+eng", {"ara", "eng", "por"});
    QCOMPARE(plan.languages, QStringList({"ara", "eng"}));
    QVERIFY(plan.missing.isEmpty());
    // One of two packs is enough
    plan = Ocr::plan("Han", "", {"chi_tra", "por"});
    QCOMPARE(plan.languages, QStringList({"chi_tra", "por"}));
    plan = Ocr::plan("Cyrillic", "", {"eng"});
    QCOMPARE(plan.missing, QStringList({"rus", "ukr"}));
    // Latin and undetected: the Latin packs
    QCOMPARE(Ocr::plan("Latin", "por+eng", installed).languages, QStringList({"por", "eng", "spa", "deu", "ita"}));
    QCOMPARE(Ocr::plan(QString(), "", {"por"}).languages, QStringList({"por"}));

    QVERIFY(Ocr::packDownloadKiB("heb") > 1000);
    QVERIFY(!Ocr::scriptName("Hebrew").isEmpty());
}

// The old fixed "por+eng" setting must never keep an installed pack of the
// detected script out (it did: Hebrew was read as Latin garbage)
void TestCore::hebrewUsesHebWithPorEngPreferred()
{
    const Ocr::Plan plan = Ocr::plan("Hebrew", "por+eng", {"eng", "heb", "osd", "por"});
    QVERIFY(plan.missing.isEmpty());
    QCOMPARE(plan.languages.first(), QStringLiteral("heb"));
    QVERIFY(!plan.languages.contains("por"));
}

void TestCore::garbageDetection()
{
    // What por+eng read from a WhatsApp message in Hebrew
    const QString garbage = QStringLiteral("bR P 3993 20:30-5 (3w D1) am N0 SD PONN");
    QVERIFY(!Ocr::assessText(garbage).reliable);
    QVERIFY(!Ocr::assessText(garbage, "Latin").reliable);
    // Expected Hebrew, got Latin letters
    QVERIFY(!Ocr::assessText(QStringLiteral("DRI YW amh ANRXIT AMUA Navae"), "Hebrew").reliable);

    const QString hebrew = QString::fromUtf8("מחר (יום שני) ב-20:30 בערב שעון ישראל\nאני מעביר הרצאה אונליין על יצירת\n"
                                             "סביבת פיתוח בטוחה לסוכנים בתוך VM");
    QVERIFY(Ocr::assessText(hebrew, "Hebrew").reliable);
    QVERIFY(Ocr::assessText(hebrew).reliable);
    const QString portuguese = QString::fromUtf8("Amanhã às 20:30 vou fazer uma palestra online\n"
                                                 "sobre como criar um ambiente seguro para agentes (VM, SSD, mp3).");
    QVERIFY(Ocr::assessText(portuguese).reliable);
    QVERIFY(Ocr::assessText(portuguese, "Latin").reliable);
    QVERIFY(Ocr::assessText(QString::fromUtf8("Завтра в 20:30 я проведу онлайн-лекцию"), "Cyrillic").reliable);
    QVERIFY(Ocr::assessText(QString::fromUtf8("明天晚上八点半我会做一个在线讲座。"), "Han").reliable);
    QVERIFY(Ocr::assessText(QString::fromUtf8("غداً الساعة الثامنة مساءً سأقدم محاضرة عبر VM"), "Arabic").reliable);
    QVERIFY(Ocr::assessText(QStringLiteral("20:30")).reliable);
    QVERIFY(!Ocr::assessText(QString()).reliable);

    // tesseract's own confidences: the same words read at 22% aren't text
    QVector<Ocr::Word> words;
    for (const QString &w : {QStringLiteral("Amanhã"), QStringLiteral("vou"), QStringLiteral("sair")}) {
        Ocr::Word word;
        word.text = w;
        word.confidence = 92;
        words.append(word);
    }
    QVERIFY(Ocr::assess(words).reliable);
    QVERIFY(Ocr::assess(words).meanConfidence > 90);
    for (Ocr::Word &w : words)
        w.confidence = 22;
    QVERIFY(!Ocr::assess(words).reliable);
}

void TestCore::wordsToText()
{
    const QByteArray tsv =
        "5\t1\t1\t1\t1\t1\t10\t10\t30\t12\t95\tHello\n"
        "5\t1\t1\t1\t1\t2\t50\t10\t30\t12\t95\tworld\n"
        "5\t1\t1\t1\t2\t1\t10\t30\t30\t12\t95\tsecond\n"
        "5\t1\t2\t1\t1\t1\t10\t60\t30\t12\t95\tblock\n";
    QCOMPARE(Ocr::wordsToText(Ocr::parseTsv(tsv)), QStringLiteral("Hello world\nsecond\n\nblock"));
    // Chinese: one character per "word", no spaces between them
    QVector<Ocr::Word> chinese;
    for (const char *c : {"明", "天", "，", "VM", "里"}) {
        Ocr::Word w;
        w.text = QString::fromUtf8(c);
        chinese.append(w);
    }
    QCOMPARE(Ocr::wordsToText(chinese), QString::fromUtf8("明天，VM 里"));
}

// With real tesseract: osd and heb from TESSDATA_PREFIX (skipped without them)
void TestCore::ocrJobDetectsHebrew()
{
    const QString tesseract = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    if (tesseract.isEmpty())
        QSKIP("tesseract is not installed");
    QProcess list;
    list.start(tesseract, {QStringLiteral("--list-langs")});
    list.waitForFinished(5000);
    const QByteArray langs = list.readAllStandardOutput() + list.readAllStandardError();
    const QStringList installed = Ocr::parseLanguages(langs);
    if (!langs.contains("\nosd") || !installed.contains("heb") || !installed.contains("eng"))
        QSKIP("needs osd, heb and eng (set TESSDATA_PREFIX)");

    // A WhatsApp-like bubble: light Hebrew text on a dark background
    QImage image(520, 150, QImage::Format_RGB32);
    image.fill(QColor("#202c33"));
    {
        QPainter p(&image);
        p.setPen(QColor("#e9edef"));
        QFont font(QStringLiteral("Noto Sans Hebrew"));
        font.setPixelSize(20);
        p.setFont(font);
        p.drawText(QRect(10, 10, 500, 130), Qt::AlignRight | Qt::AlignTop,
                   QString::fromUtf8("מחר (יום שני) ב-20:30 בערב שעון ישראל\nאני מעביר הרצאה אונליין על יצירת\n"
                                     "סביבת פיתוח בטוחה לסוכנים בתוך המחשב"));
    }

    // heb installed and the old "por+eng" setting: heb is used
    {
        OcrJob job(tesseract, installed, QStringLiteral("por+eng"));
        QSignalSpy spy(&job, &OcrJob::finished);
        job.start(image);
        QVERIFY(spy.wait(30000));
        const Ocr::Analysis a = job.result();
        QCOMPARE(a.status, QStringLiteral("ok"));
        QCOMPARE(a.script, QStringLiteral("Hebrew"));
        QCOMPARE(a.languages.first(), QStringLiteral("heb"));
        QVERIFY2(a.text.contains(QString::fromUtf8("ישראל")), qPrintable(a.text));
        QVERIFY(a.quality.meanConfidence >= Ocr::MinConfidence);
    }
    // Without heb: the pack is missing, and no garbage text comes back
    {
        OcrJob job(tesseract, {QStringLiteral("eng"), QStringLiteral("por")}, QStringLiteral("por+eng"));
        QSignalSpy spy(&job, &OcrJob::finished);
        job.start(image);
        QVERIFY(spy.wait(30000));
        const Ocr::Analysis a = job.result();
        QCOMPARE(a.status, QStringLiteral("missing"));
        QCOMPARE(a.missing, QStringList({"heb"}));
        QVERIFY(a.text.isEmpty());
    }
}

void TestCore::regionDetection()
{
    // A light window with a bordered panel and a text field inside it.
    QImage img(400, 300, QImage::Format_RGB32);
    img.fill(QColor(240, 240, 240));
    QPainter p(&img);
    p.setPen(QColor(120, 120, 120));
    p.setBrush(Qt::white);
    p.drawRect(QRect(50, 40, 300, 200));       // panel incl. 1px border
    p.setBrush(QColor(200, 220, 255));
    p.setPen(Qt::NoPen);
    p.drawRect(QRect(80, 100, 120, 30));       // field
    p.setPen(Qt::black);
    p.drawText(QRect(85, 105, 100, 20), "Text");
    p.end();

    const QVector<QRect> regions = RegionDetector::regionsAt(img, QPoint(150, 118));
    QVERIFY(regions.size() >= 2);
    QCOMPARE(regions.at(0), QRect(80, 100, 120, 30));
    QCOMPARE(regions.at(1), QRect(50, 40, 301, 201));

    // Next to the text (letters look like short lines): still the field.
    p.begin(&img);
    p.setPen(Qt::black);
    p.fillRect(QRect(90, 160, 200, 40), QColor(225, 240, 255));
    QFont big = p.font();
    big.setPixelSize(22);
    p.setFont(big);
    p.drawText(QRect(95, 165, 190, 30), Qt::AlignLeft | Qt::AlignVCenter, "Total: 1|1 529");
    p.end();
    const QVector<QRect> nearText = RegionDetector::regionsAt(img, QPoint(250, 180));
    QVERIFY(!nearText.isEmpty());
    QCOMPARE(nearText.first(), QRect(90, 160, 200, 40));

    // Nothing around: no region.
    QImage flat(100, 100, QImage::Format_RGB32);
    flat.fill(Qt::white);
    QVERIFY(RegionDetector::regionsAt(flat, QPoint(50, 50)).isEmpty());
    QVERIFY(RegionDetector::regionsAt(flat, QPoint(-5, 50)).isEmpty());
}

static QImage page(int height)
{
    // A tall document with distinct rows.
    QImage img(200, height, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    for (int y = 0; y < height; y += 20) {
        p.fillRect(10, y + 4, 20 + (y * 7) % 150, 10, QColor((y * 3) % 256, (y * 5) % 256, 120));
    }
    p.end();
    return img;
}

void TestCore::stitching()
{
    const QImage doc = page(1000);
    const QImage a = doc.copy(0, 0, 200, 300);
    const QImage b = doc.copy(0, 120, 200, 300);
    const QImage c = doc.copy(0, 260, 200, 300);

    Stitcher::Match m = Stitcher::match(a, b);
    QCOMPARE(m.offset, 120);
    QImage s = Stitcher::append(a, b, m);
    QCOMPARE(s.height(), 420);

    m = Stitcher::match(b, c);
    QCOMPARE(m.offset, 140);
    s = Stitcher::append(s, c, m);
    QCOMPARE(s.height(), 560);
    QCOMPARE(s.convertToFormat(QImage::Format_RGB32), doc.copy(0, 0, 200, 560).convertToFormat(QImage::Format_RGB32));

    QCOMPARE(Stitcher::match(a, a).offset, 0);

    // Text drawn at fractional positions comes out slightly different:
    // small differences must not break the match.
    QImage jittered = b.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < jittered.height(); ++y)
        for (int x = 0; x < jittered.width(); x += 3) {
            const QColor c = jittered.pixelColor(x, y);
            const int d = ((x + y) % 2) ? 3 : -3;
            jittered.setPixelColor(x, y, QColor(qBound(0, c.red() + d, 255), qBound(0, c.green() + d, 255),
                                                qBound(0, c.blue() + d, 255)));
        }
    QCOMPARE(Stitcher::match(a, jittered).offset, 120);
    // Unrelated content: no match.
    QImage noise(200, 300, QImage::Format_RGB32);
    for (int y = 0; y < 300; ++y)
        for (int x = 0; x < 200; ++x)
            noise.setPixel(x, y, qRgb((x * 13 + y * 7) % 256, (x * y) % 256, (y * 31) % 256));
    QCOMPARE(Stitcher::match(a, noise).offset, -1);
    QCOMPARE(Stitcher::match(a, doc).offset, -1);
}

void TestCore::stitchingWithFooter()
{
    const QImage doc = page(1000);
    auto frame = [&doc](int top) {
        QImage f(200, 300, QImage::Format_RGB32);
        QPainter p(&f);
        p.drawImage(0, 0, doc, 0, top, 200, 260);
        p.fillRect(0, 260, 200, 40, QColor(30, 30, 30));
        p.setPen(Qt::white);
        p.drawText(10, 285, "status bar");
        p.end();
        return f;
    };

    const QImage a = frame(0), b = frame(100);
    const Stitcher::Match m = Stitcher::match(a, b);
    QCOMPARE(m.offset, 100);
    // The blank content rows right above the bar look the same in both frames.
    QVERIFY(m.footer >= 40 && m.footer <= 46);
    const QImage s = Stitcher::append(a, b, m);
    QCOMPARE(s.height(), 400);
    QCOMPARE(s.copy(0, 0, 200, 360), doc.copy(0, 0, 200, 360).convertToFormat(QImage::Format_RGB32));
    QCOMPARE(QColor(s.pixel(5, 395)), QColor(30, 30, 30));
}

void TestCore::execExpansion()
{
    QStringList args;
    QString program;
    QVERIFY(DesktopApps::expandExec("gimp-2.10 %U", "/tmp/a b.png", &program, &args));
    QCOMPARE(program, QString("gimp-2.10"));
    QCOMPARE(args, QStringList({ "file:///tmp/a%20b.png" }));

    QVERIFY(DesktopApps::expandExec("\"/opt/My App/bin/app\" --open %f --name %c %i", "/x.png", &program, &args));
    QCOMPARE(program, QString("/opt/My App/bin/app"));
    QCOMPARE(args, QStringList({ "--open", "/x.png", "--name" }));

    QVERIFY(DesktopApps::expandExec("viewer", "/x.png", &program, &args));
    QCOMPARE(args, QStringList({ "/x.png" }));

    QVERIFY(DesktopApps::expandExec("app --uri=%u", "/x y.png", &program, &args));
    QCOMPARE(args, QStringList({ "--uri=file:///x%20y.png" }));

    QVERIFY(!DesktopApps::expandExec("", "/x.png", &program, &args));
}

void TestCore::commandLine()
{
    using namespace CommandLine;
    Request r;
    QVERIFY(parse({}, &r, nullptr));
    QCOMPARE(r.command, Gui);
    QVERIFY(r.interactive());

    QVERIFY(parse({ "full", "--delay", "2", "-p", "/tmp/x.png", "-c" }, &r, nullptr));
    QCOMPARE(r.command, Full);
    QCOMPARE(r.delayMs, 2000);
    QCOMPARE(r.path, QString("/tmp/x.png"));
    QVERIFY(r.clipboard);
    QVERIFY(!r.interactive());

    QVERIFY(parse({ "screen", "--number=1", "--raw", "-d", "500ms" }, &r, nullptr));
    QCOMPARE(r.command, Screen);
    QCOMPARE(r.screen, 1);
    QVERIFY(r.raw);
    QCOMPARE(r.delayMs, 500);

    QVERIFY(parse({ "window", "--active" }, &r, nullptr));
    QVERIFY(!r.interactive());
    QVERIFY(parse({ "window" }, &r, nullptr));
    QVERIFY(r.interactive());

    QVERIFY(parse({ "gui", "--region", "300x200+10+20" }, &r, nullptr));
    QCOMPARE(r.region, QRect(10, 20, 300, 200));
    QVERIFY(parse({ "--stop-recording" }, &r, nullptr));
    QCOMPARE(r.command, StopRecording);
    QVERIFY(parse({ "config" }, &r, nullptr));
    QCOMPARE(r.command, Settings);

    QCOMPARE(parseDelay("1,5"), 1500);
    QCOMPARE(parseDelay("3s"), 3000);
    QCOMPARE(parseDelay("-1"), -1);
    QCOMPARE(parseDelay("abc"), -1);
    QCOMPARE(parseRegion("10,20,30,40"), QRect(10, 20, 30, 40));
    QVERIFY(parseRegion("0x10+1+1").isNull());
    QVERIFY(!helpText().isEmpty());
}

void TestCore::commandLineErrors()
{
    using namespace CommandLine;
    Request r;
    QString error;
    QVERIFY(!parse({ "frobnicate" }, &r, &error));
    QVERIFY(error.contains("frobnicate"));
    QVERIFY(!parse({ "--delay" }, &r, &error));
    QVERIFY(!parse({ "--delay", "soon" }, &r, &error));
    QVERIFY(!parse({ "--bogus" }, &r, &error));
    QVERIFY(!parse({ "gui", "--raw" }, &r, &error));
    QVERIFY(!parse({ "screen", "-n", "x" }, &r, &error));
    QVERIFY(!parse({ "gui", "--region", "big" }, &r, &error));
}

void TestCore::commandLineRoundTrip()
{
    using namespace CommandLine;
    Request r;
    QVERIFY(parse({ "record", "-d", "1.25", "--path", "/a b/", "--region", "5,6,7,8", "--pin" }, &r, nullptr));
    Request back;
    QVERIFY(parse(r.toArguments(), &back, nullptr));
    QCOMPARE(back.command, Record);
    QCOMPARE(back.delayMs, 1250);
    QCOMPARE(back.path, QString("/a b/"));
    QCOMPARE(back.region, QRect(5, 6, 7, 8));
    QVERIFY(back.pin);
}

void TestCore::askCommandLine()
{
    CommandLine::Request r;
    QString error;
    QVERIFY(CommandLine::parse({QStringLiteral("ask"), QStringLiteral("--question"), QStringLiteral("  O que é isso?  ")},
                               &r, &error));
    QCOMPARE(r.command, CommandLine::Ask);
    QCOMPARE(r.question, QStringLiteral("O que é isso?"));
    QVERIFY(r.interactive());   // handed to the running instance
    CommandLine::Request back;
    QVERIFY(CommandLine::parse(r.toArguments(), &back, &error));
    QCOMPARE(back.command, CommandLine::Ask);
    QCOMPARE(back.question, r.question);
    QVERIFY(!CommandLine::parse({QStringLiteral("ask"), QStringLiteral("--question")}, &r, &error));
    QVERIFY(!CommandLine::parse({QStringLiteral("ask"), QStringLiteral("--raw")}, &r, &error));
}

void TestCore::askStrokeBounds()
{
    const QRectF bounds(0, 0, 1000, 800);
    // A diagonal drag is its rectangle
    QCOMPARE(ScreenAskCore::strokeBounds({QPointF(100, 100), QPointF(300, 200)}, 0, bounds), QRectF(100, 100, 200, 100));
    // A rough circle around something: its box, grown by the padding
    QVector<QPointF> circle;
    for (int i = 0; i < 36; ++i) {
        const qreal a = i * M_PI / 18;
        circle << QPointF(500 + 80 * std::cos(a), 400 + 50 * std::sin(a));
    }
    const QRectF c = ScreenAskCore::strokeBounds(circle, 6, bounds);
    QVERIFY(qAbs(c.left() - 414) < 1 && qAbs(c.right() - 586) < 1);
    QVERIFY(qAbs(c.top() - 344) < 1 && qAbs(c.bottom() - 456) < 1);
    // Kept inside the screen
    QCOMPARE(ScreenAskCore::strokeBounds({QPointF(2, 3), QPointF(50, 60)}, 10, bounds).topLeft(), QPointF(0, 0));
    // A line or a click is no area
    QVERIFY(ScreenAskCore::strokeBounds({QPointF(10, 10), QPointF(300, 12)}, 6, bounds).isNull());
    QVERIFY(ScreenAskCore::strokeBounds({QPointF(10, 10)}, 6, bounds).isNull());
}

void TestCore::askEncodeImage()
{
    QImage big(3000, 1500, QImage::Format_ARGB32);
    big.fill(Qt::transparent);
    QPainter p(&big);
    p.fillRect(100, 100, 400, 300, Qt::red);
    p.end();
    QByteArray format;
    const QByteArray data = ScreenAskCore::encodeImage(big, 1280, 3 * 1024 * 1024, &format);
    QCOMPARE(format, QByteArray("png"));
    const QImage back = QImage::fromData(data);
    QCOMPARE(back.size(), QSize(1280, 640));
    QVERIFY(!back.hasAlphaChannel());
    QCOMPARE(QColor(back.pixel(5, 5)), QColor(Qt::white));   // transparency on white

    // Noise doesn't compress as PNG: JPEG under the limit
    QImage noise(1000, 1000, QImage::Format_RGB32);
    quint32 seed = 1;
    for (int y = 0; y < noise.height(); ++y)
        for (int x = 0; x < noise.width(); ++x)
            noise.setPixel(x, y, (seed = seed * 1664525u + 1013904223u) | 0xff000000u);
    const QByteArray small = ScreenAskCore::encodeImage(noise, 1280, 400 * 1024, &format);
    QCOMPARE(format, QByteArray("jpeg"));
    QVERIFY(small.size() > 0);
    QVERIFY(ScreenAskCore::encodeImage(QImage()).isEmpty());
}

void TestCore::askChipsAndSearch()
{
    const QLocale br(QLocale::Portuguese, QLocale::Brazil);
    QCOMPARE(ScreenAskCore::languageName(br), QStringLiteral("português (Brasil)"));
    QVERIFY(ScreenAskCore::chipQuestion(QStringLiteral("translate"), br).contains(QStringLiteral("português (Brasil)")));
    for (const char *kind : {"explain", "summarize", "what", "copy"})
        QVERIFY(!ScreenAskCore::chipQuestion(QString::fromLatin1(kind)).isEmpty());
    QVERIFY(ScreenAskCore::chipQuestion(QStringLiteral("nope")).isEmpty());

    QVERIFY(ScreenAskCore::webSearchUrl(QStringLiteral("  \n ")).isEmpty());
    const QUrl url = ScreenAskCore::webSearchUrl(QStringLiteral("Quarterly\nreport  2026 & more"));
    QCOMPARE(url.host(), QStringLiteral("duckduckgo.com"));
    QCOMPARE(QUrlQuery(url).queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded),
             QStringLiteral("Quarterly report 2026 & more"));
    const QString longText = QStringLiteral("word ").repeated(100);
    QVERIFY(QUrlQuery(ScreenAskCore::webSearchUrl(longText)).queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded).size() <= 200);
}

QTEST_MAIN(TestCore)
#include "tst_core.moc"
