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

#include "ocr.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtMath>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcOcr, "lingmo.screenshot.ocr", QtInfoMsg)

namespace {

const char *STORE_HELPER = "/usr/lib/lingmo-store/lingmo-store-helper";
// Larger images are not upscaled: tesseract is slow enough on them already.
const qint64 UPSCALE_LIMIT = 4000000;
const char *LANGUAGES_TOOL = "/usr/lib/lingmo-screenshot/lingmo-ocr-languages";

struct ScriptInfo {
    const char *osd;              // tesseract's OSD name
    const char *packs;            // '+' separated
    QChar::Script scripts[3];     // Unicode scripts its letters belong to
};

// What OSD can name (tesseract's osd.traineddata) and the packs that read it
const ScriptInfo SCRIPTS[] = {
    {"Hebrew", "heb", {QChar::Script_Hebrew, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Arabic", "ara", {QChar::Script_Arabic, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Cyrillic", "rus+ukr", {QChar::Script_Cyrillic, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Han", "chi_sim+chi_tra", {QChar::Script_Han, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Japanese", "jpn", {QChar::Script_Han, QChar::Script_Hiragana, QChar::Script_Katakana}},
    {"Katakana", "jpn", {QChar::Script_Han, QChar::Script_Hiragana, QChar::Script_Katakana}},
    {"Hiragana", "jpn", {QChar::Script_Han, QChar::Script_Hiragana, QChar::Script_Katakana}},
    {"Hangul", "kor", {QChar::Script_Hangul, QChar::Script_Han, QChar::Script_Unknown}},
    {"Greek", "ell", {QChar::Script_Greek, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Devanagari", "hin", {QChar::Script_Devanagari, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Thai", "tha", {QChar::Script_Thai, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Bengali", "ben", {QChar::Script_Bengali, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Tamil", "tam", {QChar::Script_Tamil, QChar::Script_Unknown, QChar::Script_Unknown}},
    {"Latin", "", {QChar::Script_Latin, QChar::Script_Unknown, QChar::Script_Unknown}},
};

const ScriptInfo *findScript(const QString &osd)
{
    for (const ScriptInfo &info : SCRIPTS) {
        if (osd.compare(QLatin1String(info.osd), Qt::CaseInsensitive) == 0)
            return &info;
    }
    return nullptr;
}

QSet<QChar::Script> unicodeScripts(const QString &osd)
{
    QSet<QChar::Script> set;
    if (const ScriptInfo *info = findScript(osd)) {
        for (QChar::Script s : info->scripts) {
            if (s != QChar::Script_Unknown)
                set.insert(s);
        }
    }
    return set;
}

// Writing systems without spaces between words
bool unspaced(QChar::Script s)
{
    return s == QChar::Script_Han || s == QChar::Script_Hiragana || s == QChar::Script_Katakana
            || s == QChar::Script_Thai;
}

bool isVowel(char32_t c)
{
    // Accented letters: their base letter
    QString base = QString::fromUcs4(&c, 1).normalized(QString::NormalizationForm_D);
    const QChar first = base.isEmpty() ? QChar() : base.at(0).toLower();
    return QStringLiteral("aeiouy").contains(first);
}

enum class Token { Skip, Good, Neutral, Bad };

Token classify(const QString &part, const QSet<QChar::Script> &expected)
{
    const QList<uint> chars = part.toUcs4();
    int letters = 0, digits = 0, lower = 0, upper = 0, vowels = 0, inExpected = 0, latin = 0;
    QSet<QChar::Script> seen;
    int consonantRun = 0, longestRun = 0;
    bool firstUpper = false;
    for (int i = 0; i < chars.size(); ++i) {
        const char32_t c = chars.at(i);
        if (QChar::isDigit(c)) {
            ++digits;
            consonantRun = 0;
            continue;
        }
        if (!QChar::isLetter(c) && QChar::category(c) != QChar::Mark_NonSpacing
                && QChar::category(c) != QChar::Mark_SpacingCombining)
            continue;
        if (!QChar::isLetter(c))
            continue;   // a combining mark belongs to the letter before it
        ++letters;
        const QChar::Script script = QChar::script(c);
        seen.insert(script);
        if (expected.contains(script))
            ++inExpected;
        if (script == QChar::Script_Latin) {
            ++latin;
            if (QChar::isLower(c))
                ++lower;
            if (QChar::isUpper(c)) {
                ++upper;
                if (letters == 1)
                    firstUpper = true;
            }
            if (isVowel(c)) {
                ++vowels;
                consonantRun = 0;
            } else {
                longestRun = qMax(longestRun, ++consonantRun);
            }
        }
    }
    if (letters == 0)
        return Token::Skip;   // numbers, times, prices

    if (digits > 0) {
        // 20h, 10am, mp3, Qt6, H264: fine; 3w, D1, N0, a7va: not words
        static const QRegularExpression unitAfter(QStringLiteral("^\\d{2,}\\p{L}{1,2}$"));
        static const QRegularExpression numberAfter(QStringLiteral("^\\p{L}{2,}\\d+$"));
        if (unitAfter.match(part).hasMatch() || numberAfter.match(part).hasMatch())
            return Token::Neutral;
        return Token::Bad;
    }
    // Letters of two scripts in one word: wrong language (Latin with
    // Cyrillic or Greek look-alikes, mostly)
    if (seen.size() > 1)
        return Token::Bad;

    const QChar::Script script = *seen.cbegin();
    const bool latinExpected = expected.isEmpty() || expected.contains(QChar::Script_Latin);
    if (!expected.isEmpty() && inExpected == 0) {
        // Latin names and code inside Hebrew or Arabic text are normal
        if (script == QChar::Script_Latin && !latinExpected)
            return Token::Neutral;
        return Token::Bad;
    }

    if (script == QChar::Script_Latin || script == QChar::Script_Cyrillic || script == QChar::Script_Greek) {
        // lower, Capitalized or UPPER; "bR", "pONN" are not words
        const bool caseOk = (upper == 0) || (upper == letters) || (upper == 1 && firstUpper);
        if (!caseOk)
            return Token::Bad;
    }
    if (script == QChar::Script_Latin) {
        if (letters == 1) {
            // Words of one letter: a, e, o, y, à, é, I
            return isVowel(chars.constFirst()) ? Token::Good : Token::Bad;
        }
        if (vowels == 0)
            return upper == letters && letters <= 5 ? Token::Neutral   // PDF, SSD
                                                    : Token::Bad;
        if (longestRun > 5)
            return Token::Bad;
    }
    return Token::Good;
}

}

QVector<Ocr::Word> Ocr::parseTsv(const QByteArray &tsv)
{
    QVector<Word> words;
    const QList<QByteArray> lines = tsv.split('\n');

    for (const QByteArray &rawLine : lines) {
        QByteArray line = rawLine;
        if (line.endsWith('\r'))
            line.chop(1);

        const QList<QByteArray> cols = line.split('\t');
        // level page block par line word left top width height conf text
        if (cols.size() < 12)
            continue;

        bool ok = false;
        const int level = cols.at(0).toInt(&ok);
        if (!ok || level != 5)
            continue;

        Word w;
        w.block = cols.at(2).toInt();
        w.paragraph = cols.at(3).toInt();
        w.line = cols.at(4).toInt();
        const int left = cols.at(6).toInt();
        const int top = cols.at(7).toInt();
        const int width = cols.at(8).toInt();
        const int height = cols.at(9).toInt();
        w.confidence = qRound(cols.at(10).toDouble());
        // The text itself may contain tabs only in theory; keep the rest.
        w.text = QString::fromUtf8(cols.mid(11).join('\t')).trimmed();

        if (w.text.isEmpty() || width <= 0 || height <= 0)
            continue;

        w.box = QRect(left, top, width, height);
        words.append(w);
    }

    return words;
}

QVector<QRect> Ocr::redactBoxes(const QVector<Word> &words, int padding, int minConfidence)
{
    // Key: block/paragraph/line, in reading order.
    QMap<qint64, QRect> lines;

    for (const Word &w : words) {
        if (w.confidence < minConfidence)
            continue;
        // Single characters with low confidence are usually icons or noise.
        if (w.text.size() == 1 && !w.text.at(0).isLetterOrNumber())
            continue;

        const qint64 key = (qint64(w.block) << 32) | (qint64(w.paragraph) << 16) | qint64(w.line);
        lines[key] = lines.value(key).united(w.box);
    }

    QVector<QRect> boxes;
    for (const QRect &r : std::as_const(lines))
        boxes.append(r.adjusted(-padding, -padding, padding, padding));

    return boxes;
}

QStringList Ocr::parseLanguages(const QByteArray &output)
{
    QStringList langs;
    const QList<QByteArray> lines = output.split('\n');

    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        // "List of available languages in "/usr/share/tessdata/" (3):"
        if (line.isEmpty() || line.contains(QLatin1Char(' ')) || line.endsWith(QLatin1Char(':')))
            continue;
        if (line == QLatin1String("osd") || line == QLatin1String("equ"))
            continue;
        langs.append(line);
    }

    return langs;
}

QString Ocr::languageArgument(const QString &wanted, const QStringList &installed)
{
    QStringList result;
    const QStringList parts = wanted.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString lang = part.trimmed();
        if (installed.contains(lang) && !result.contains(lang))
            result.append(lang);
    }

    // Nothing that was asked for: use what there is.
    if (result.isEmpty() && !installed.isEmpty())
        result.append(installed.contains(QLatin1String("eng")) ? QStringLiteral("eng") : installed.first());

    return result.join(QLatin1Char('+'));
}

QRect Ocr::scaleBox(const QRect &box, qreal factor)
{
    if (factor <= 0)
        return box;

    const int left = qFloor(box.left() / factor);
    const int top = qFloor(box.top() / factor);
    const int right = qCeil((box.left() + box.width()) / factor);
    const int bottom = qCeil((box.top() + box.height()) / factor);
    return QRect(left, top, right - left, bottom - top);
}

// Chinese and Japanese have no spaces: tesseract's "words" there are single
// characters, and joining them with spaces would change the text
static bool joinsWithoutSpace(const QString &before, const QString &after)
{
    if (before.isEmpty() || after.isEmpty())
        return false;
    const QList<uint> a = before.right(2).toUcs4();
    const QList<uint> b = after.left(2).toUcs4();
    if (a.isEmpty() || b.isEmpty())
        return false;
    // Full-width punctuation (，。「」) carries its own spacing
    auto punctuation = [](uint c) { return (c >= 0x3000 && c <= 0x303F) || (c >= 0xFF00 && c <= 0xFFEF); };
    auto cjk = [&](uint c) {
        const QChar::Script s = QChar::script(c);
        return s == QChar::Script_Han || s == QChar::Script_Hiragana || s == QChar::Script_Katakana || punctuation(c);
    };
    return punctuation(a.constLast()) || punctuation(b.constFirst()) || (cjk(a.constLast()) && cjk(b.constFirst()));
}

QString Ocr::wordsToText(const QVector<Word> &words)
{
    QString text;
    int block = -1, paragraph = -1, line = -1;
    for (const Word &w : words) {
        if (!text.isEmpty()) {
            if (w.block != block)
                text += QStringLiteral("\n\n");
            else if (w.paragraph != paragraph || w.line != line)
                text += QLatin1Char('\n');
            else if (!joinsWithoutSpace(text, w.text))
                text += QLatin1Char(' ');
        }
        text += w.text;
        block = w.block;
        paragraph = w.paragraph;
        line = w.line;
    }
    return text;
}

Ocr::Osd Ocr::parseOsd(const QByteArray &output)
{
    Osd osd;
    const QList<QByteArray> lines = output.split('\n');
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.startsWith(QLatin1String("Script confidence:"))) {
            bool ok = false;
            const double value = line.mid(18).trimmed().toDouble(&ok);
            if (ok)
                osd.confidence = value;
        } else if (line.startsWith(QLatin1String("Script:"))) {
            osd.script = line.mid(7).trimmed();
        }
    }
    // "Script: Katakana" with confidence 0: a guess on two characters
    if (osd.confidence <= 0)
        osd.script.clear();
    return osd;
}

bool Ocr::osdConfident(const Osd &osd)
{
    if (!osd.isValid() || !findScript(osd.script))
        return false;
    // Han, Japanese and Hangul share characters: OSD's margin between them is
    // small even on clean text, but which of the three is a detail
    static const QStringList cjk = {QStringLiteral("Han"), QStringLiteral("Japanese"), QStringLiteral("Hangul"),
                                    QStringLiteral("Katakana"), QStringLiteral("Hiragana")};
    return osd.confidence >= (cjk.contains(osd.script) ? 0.5 : 2.0);
}

QStringList Ocr::scriptPacks(const QString &script)
{
    const ScriptInfo *info = findScript(script);
    if (!info || !*info->packs)
        return {};
    return QString::fromLatin1(info->packs).split(QLatin1Char('+'));
}

bool Ocr::isLatinLanguage(const QString &code)
{
    static const QSet<QString> latin = {
        QStringLiteral("afr"), QStringLiteral("aze"), QStringLiteral("bos"), QStringLiteral("bre"),
        QStringLiteral("cat"), QStringLiteral("ceb"), QStringLiteral("ces"), QStringLiteral("cos"),
        QStringLiteral("cym"), QStringLiteral("dan"), QStringLiteral("dan_frak"), QStringLiteral("deu"),
        QStringLiteral("deu_frak"), QStringLiteral("eng"), QStringLiteral("enm"), QStringLiteral("epo"),
        QStringLiteral("est"), QStringLiteral("eus"), QStringLiteral("fao"), QStringLiteral("fil"),
        QStringLiteral("fin"), QStringLiteral("fra"), QStringLiteral("frk"), QStringLiteral("frm"),
        QStringLiteral("fry"), QStringLiteral("gla"), QStringLiteral("gle"), QStringLiteral("glg"),
        QStringLiteral("hat"), QStringLiteral("hrv"), QStringLiteral("hun"), QStringLiteral("ind"),
        QStringLiteral("isl"), QStringLiteral("ita"), QStringLiteral("ita_old"), QStringLiteral("jav"),
        QStringLiteral("kmr"), QStringLiteral("lat"), QStringLiteral("lav"), QStringLiteral("lit"),
        QStringLiteral("ltz"), QStringLiteral("mlt"), QStringLiteral("mri"), QStringLiteral("msa"),
        QStringLiteral("nld"), QStringLiteral("nor"), QStringLiteral("oci"), QStringLiteral("pol"),
        QStringLiteral("por"), QStringLiteral("que"), QStringLiteral("ron"), QStringLiteral("slk"),
        QStringLiteral("slk_frak"), QStringLiteral("slv"), QStringLiteral("spa"), QStringLiteral("spa_old"),
        QStringLiteral("sqi"), QStringLiteral("srp_latn"), QStringLiteral("sun"), QStringLiteral("swa"),
        QStringLiteral("swe"), QStringLiteral("tgl"), QStringLiteral("ton"), QStringLiteral("tur"),
        QStringLiteral("uzb"), QStringLiteral("vie"), QStringLiteral("yor"),
    };
    return latin.contains(code);
}

QStringList Ocr::latinLanguages(const QString &preferred, const QStringList &installed, int max)
{
    QStringList result;
    const QStringList parts = preferred.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString lang = part.trimmed();
        if (installed.contains(lang) && isLatinLanguage(lang) && !result.contains(lang))
            result.append(lang);
    }
    // The common ones next, then the rest in tesseract's (alphabetical) order
    QStringList rest = {QStringLiteral("eng"), QStringLiteral("por"), QStringLiteral("spa"),
                        QStringLiteral("fra"), QStringLiteral("deu"), QStringLiteral("ita")};
    rest += installed;
    for (const QString &lang : std::as_const(rest)) {
        if (result.size() >= max)
            break;
        if (installed.contains(lang) && isLatinLanguage(lang) && !result.contains(lang))
            result.append(lang);
    }
    return result;
}

Ocr::Plan Ocr::plan(const QString &script, const QString &preferred, const QStringList &installed)
{
    Plan p;
    const QStringList packs = scriptPacks(script);
    if (packs.isEmpty()) {
        // Latin, or not detected
        p.languages = latinLanguages(preferred, installed);
        return p;
    }
    for (const QString &pack : packs) {
        if (installed.contains(pack))
            p.languages.append(pack);
    }
    if (p.languages.isEmpty()) {
        p.missing = packs;
        return p;
    }
    // Latin words inside: ara+eng
    const QStringList latin = latinLanguages(QStringLiteral("eng"), installed, 1);
    p.languages += latin;
    return p;
}

Ocr::Quality Ocr::assessText(const QString &text, const QString &script)
{
    Quality q;
    QSet<QChar::Script> expected = unicodeScripts(script);

    // Letters in each script
    QHash<QChar::Script, int> counts;
    int letters = 0;
    const QList<uint> chars = text.toUcs4();
    for (uint c : chars) {
        if (QChar::isLetter(c)) {
            ++letters;
            ++counts[QChar::script(c)];
        }
    }
    if (letters == 0) {
        // Numbers only (a time, a price) are fine; nothing at all isn't text
        q.wordRatio = 1;
        q.reliable = !text.trimmed().isEmpty();
        return q;
    }
    if (expected.isEmpty()) {
        // Not detected: the most common script is the one to check against
        QChar::Script top = QChar::Script_Unknown;
        int best = -1;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            if (it.value() > best) {
                best = it.value();
                top = it.key();
            }
        }
        expected.insert(top);
        if (top == QChar::Script_Han || top == QChar::Script_Hiragana || top == QChar::Script_Katakana)
            expected += {QChar::Script_Han, QChar::Script_Hiragana, QChar::Script_Katakana};
    }
    int inExpected = 0;
    for (QChar::Script s : std::as_const(expected))
        inExpected += counts.value(s);
    q.scriptRatio = double(inExpected) / letters;

    // Words: split on spaces, then on the joiners of compounds and times
    static const QRegularExpression space(QStringLiteral("\\s+"));
    static const QRegularExpression joiners(QStringLiteral("[-‐–—/:.,;!?()\\[\\]{}\"'«»“”‘’…|]+"));
    int good = 0, bad = 0;
    bool unspacedText = false;
    for (QChar::Script s : std::as_const(expected))
        unspacedText |= unspaced(s);
    const QStringList tokens = text.split(space, Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const QStringList parts = token.split(joiners, Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            switch (classify(part, expected)) {
            case Token::Good: ++good; break;
            case Token::Bad: ++bad; break;
            default: break;
            }
        }
    }
    q.wordRatio = good + bad > 0 ? double(good) / (good + bad) : 1.0;
    Q_UNUSED(unspacedText)

    const bool latinExpected = expected.contains(QChar::Script_Latin);
    q.reliable = q.wordRatio >= 0.6 && (latinExpected ? q.scriptRatio >= 0.5 : q.scriptRatio >= 0.5);
    return q;
}

Ocr::Quality Ocr::assess(const QVector<Word> &words, const QString &script)
{
    Quality q = assessText(wordsToText(words), script);
    double sum = 0;
    int weight = 0;
    for (const Word &w : words) {
        if (w.confidence < 0)
            continue;
        const int n = qMax(1, int(w.text.size()));
        sum += double(w.confidence) * n;
        weight += n;
    }
    q.meanConfidence = weight > 0 ? sum / weight : -1;
    if (q.meanConfidence >= 0 && q.meanConfidence < MinConfidence)
        q.reliable = false;
    return q;
}

QString Ocr::scriptName(const QString &script)
{
    // The language that script stands for, as it reads after "To read …"
    const QString s = script.toLower();
    if (s == QLatin1String("hebrew")) return QCoreApplication::translate("Ocr", "Hebrew");
    if (s == QLatin1String("arabic")) return QCoreApplication::translate("Ocr", "Arabic");
    if (s == QLatin1String("cyrillic")) return QCoreApplication::translate("Ocr", "Russian and Ukrainian");
    if (s == QLatin1String("han")) return QCoreApplication::translate("Ocr", "Chinese");
    if (s == QLatin1String("japanese") || s == QLatin1String("katakana") || s == QLatin1String("hiragana"))
        return QCoreApplication::translate("Ocr", "Japanese");
    if (s == QLatin1String("hangul")) return QCoreApplication::translate("Ocr", "Korean");
    if (s == QLatin1String("greek")) return QCoreApplication::translate("Ocr", "Greek");
    if (s == QLatin1String("devanagari")) return QCoreApplication::translate("Ocr", "Hindi");
    if (s == QLatin1String("thai")) return QCoreApplication::translate("Ocr", "Thai");
    if (s == QLatin1String("bengali")) return QCoreApplication::translate("Ocr", "Bengali");
    if (s == QLatin1String("tamil")) return QCoreApplication::translate("Ocr", "Tamil");
    if (s == QLatin1String("latin")) return QCoreApplication::translate("Ocr", "Latin script");
    return script;
}

int Ocr::packDownloadKiB(const QString &code)
{
    // pacman -Si tesseract-data-<code>, Download Size (October 2026)
    static const QHash<QString, int> sizes = {
        {QStringLiteral("heb"), 1938}, {QStringLiteral("ara"), 1606}, {QStringLiteral("rus"), 7496},
        {QStringLiteral("ukr"), 4557}, {QStringLiteral("chi_sim"), 17869}, {QStringLiteral("chi_tra"), 24013},
        {QStringLiteral("jpn"), 14295}, {QStringLiteral("kor"), 6205}, {QStringLiteral("ell"), 3072},
        {QStringLiteral("hin"), 1369}, {QStringLiteral("tha"), 885}, {QStringLiteral("ben"), 1342},
        {QStringLiteral("tam"), 1290}, {QStringLiteral("spa"), 7209}, {QStringLiteral("fra"), 5284},
        {QStringLiteral("deu"), 6083}, {QStringLiteral("ita"), 5980},
    };
    return sizes.value(code);
}

QString Ocr::languagesTool()
{
    const QString override = qEnvironmentVariable("LINGMO_OCR_LANGUAGES_TOOL");
    return override.isEmpty() ? QString::fromLatin1(LANGUAGES_TOOL) : override;
}

// ------------------------------------------------------------------ OcrJob

OcrJob::OcrJob(const QString &tesseract, const QStringList &installed, const QString &preferred,
               const QString &forced, QObject *parent)
    : QObject(parent)
    , m_tesseract(tesseract)
    , m_installed(installed)
    , m_preferred(preferred)
    , m_forced(forced)
{
}

OcrJob::~OcrJob()
{
    stopAll();
    delete m_dir;
}

void OcrJob::stopAll()
{
    for (const QPointer<QProcess> &p : std::as_const(m_processes)) {
        if (!p)
            continue;
        p->disconnect(this);
        p->kill();
        p->waitForFinished(500);
        p->deleteLater();
    }
    m_processes.clear();
}

QProcess *OcrJob::launch(const QStringList &args, std::function<void(bool, const QByteArray &)> done)
{
    auto *p = new QProcess(this);
    p->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Several threads make small images slower, not faster; and two run at once
    env.insert(QStringLiteral("OMP_THREAD_LIMIT"), QStringLiteral("1"));
    p->setProcessEnvironment(env);
    auto called = std::make_shared<bool>(false);
    connect(p, &QProcess::finished, this, [p, done, called](int exitCode, QProcess::ExitStatus status) {
        if (*called)
            return;
        *called = true;
        const QByteArray out = p->readAllStandardOutput();
        const QByteArray err = p->readAllStandardError();
        p->deleteLater();
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        if (!ok)
            qCDebug(lcOcr).noquote() << "tesseract:" << exitCode << err.right(300);
        done(ok, ok ? out : out + err);
    });
    connect(p, &QProcess::errorOccurred, this, [p, done, called](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || *called)
            return;
        *called = true;
        p->deleteLater();
        done(false, {});
    });
    m_processes.append(p);
    p->start(m_tesseract, args);
    return p;
}

QProcess *OcrJob::scan(const QStringList &languages, std::function<void(bool, const QVector<Ocr::Word> &)> done)
{
    qCInfo(lcOcr).noquote() << "Reading with" << languages.join(QLatin1Char('+'));
    return launch({m_file, QStringLiteral("stdout"), QStringLiteral("-l"), languages.join(QLatin1Char('+')),
                   QStringLiteral("--psm"), QStringLiteral("3"),
                   QStringLiteral("-c"), QStringLiteral("tessedit_create_txt=0"),
                   QStringLiteral("-c"), QStringLiteral("tessedit_create_tsv=1")},
                  [done](bool ok, const QByteArray &out) { done(ok, ok ? Ocr::parseTsv(out) : QVector<Ocr::Word>()); });
}

void OcrJob::start(const QImage &image)
{
    m_clock.start();
    if (m_tesseract.isEmpty() || m_installed.isEmpty() || image.isNull()) {
        QMetaObject::invokeMethod(this, [this] { finish(QStringLiteral("failed")); }, Qt::QueuedConnection);
        return;
    }

    // Private folder: the capture may contain anything
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_dir = new QTemporaryDir(base + QStringLiteral("/lingmo-screenshot-ocr-XXXXXX"));
    // Screen text is small for tesseract (made for ~300 dpi scans): doubling
    // the size and dropping the colours helps a lot
    QImage input = image.convertToFormat(QImage::Format_Grayscale8);
    if (qint64(image.width()) * image.height() <= UPSCALE_LIMIT)
        input = input.scaled(image.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    m_file = m_dir->filePath(QStringLiteral("input.png"));
    if (!m_dir->isValid() || !input.save(m_file, "PNG")) {
        QMetaObject::invokeMethod(this, [this] { finish(QStringLiteral("failed")); }, Qt::QueuedConnection);
        return;
    }

    if (!m_forced.isEmpty()) {
        const QStringList forced = Ocr::languageArgument(m_forced, m_installed).split(QLatin1Char('+'), Qt::SkipEmptyParts);
        m_firstLanguages = forced;
        scan(forced, [this](bool ok, const QVector<Ocr::Word> &words) {
            if (!ok)
                return finish(QStringLiteral("failed"));
            const Ocr::Quality q = Ocr::assess(words);
            finish(words.isEmpty() ? QStringLiteral("empty") : q.reliable ? QStringLiteral("ok") : QStringLiteral("unreliable"), words);
        });
        return;
    }

    // Both at once: OSD takes a fraction of a second, and most text is Latin
    launch({m_file, QStringLiteral("stdout"), QStringLiteral("--psm"), QStringLiteral("0"),
            QStringLiteral("-c"), QStringLiteral("min_characters_to_try=10")},
           [this](bool ok, const QByteArray &out) { onOsd(ok, out); });
    m_firstLanguages = Ocr::latinLanguages(m_preferred, m_installed);
    if (m_firstLanguages.isEmpty())
        m_firstLanguages = m_installed.mid(0, 3);
    scan(m_firstLanguages, [this](bool ok, const QVector<Ocr::Word> &words) { onFirstScan(ok, words); });
}

void OcrJob::onOsd(bool ok, const QByteArray &out)
{
    if (m_done)
        return;
    m_osdDone = true;
    m_result.osdMs = m_clock.elapsed();
    m_osd = ok ? Ocr::parseOsd(out) : Ocr::Osd();
    qCInfo(lcOcr).noquote() << "OSD:" << (m_osd.isValid() ? m_osd.script : QStringLiteral("-"))
                            << m_osd.confidence << m_result.osdMs << "ms";

    if (Ocr::osdConfident(m_osd) && m_osd.script != QLatin1String("Latin")) {
        const Ocr::Plan plan = Ocr::plan(m_osd.script, m_preferred, m_installed);
        // The Latin scan can't read this: stop it
        stopAll();
        m_firstDone = true;
        if (!plan.missing.isEmpty()) {
            m_result.missing = plan.missing;
            return finish(QStringLiteral("missing"), {}, m_osd.script);
        }
        const QString script = m_osd.script;
        m_firstLanguages = plan.languages;
        scan(plan.languages, [this, script](bool ok, const QVector<Ocr::Word> &words) {
            if (!ok)
                return finish(QStringLiteral("failed"), {}, script);
            const Ocr::Quality q = Ocr::assess(words, script);
            finish(words.isEmpty() ? QStringLiteral("empty") : q.reliable ? QStringLiteral("ok") : QStringLiteral("unreliable"),
                   words, script);
        });
        return;
    }

    // Not sure, but it guessed a script whose pack is installed: read with it
    // too, right now, so that when the Latin packs fail nothing has to wait
    // for another scan
    if (m_osd.isValid() && m_osd.script != QLatin1String("Latin") && findScript(m_osd.script)) {
        const Ocr::Plan plan = Ocr::plan(m_osd.script, m_preferred, m_installed);
        if (!plan.languages.isEmpty() && plan.missing.isEmpty()) {
            m_guessRunning = true;
            m_guessLanguages = plan.languages;
            const QString script = m_osd.script;
            scan(plan.languages, [this, script](bool ok, const QVector<Ocr::Word> &words) {
                if (m_done)
                    return;
                m_guessRunning = false;
                if (ok && !words.isEmpty()) {
                    const Ocr::Quality q = Ocr::assess(words, script);
                    if (q.reliable) {
                        m_best.quality = q;
                        m_best.script = script;
                        m_best.languages = m_guessLanguages;
                        m_best.text = Ocr::wordsToText(words);
                    }
                }
                if (m_waitingForGuess) {
                    m_waitingForGuess = false;
                    afterGuess();
                }
            });
        }
    }
    if (m_firstDone)
        evaluateFirst();
}

void OcrJob::afterGuess()
{
    if (!m_best.text.isEmpty()) {
        m_firstLanguages = m_best.languages;
        m_result.quality = m_best.quality;
        m_result.text = m_best.text;
        m_result.script = m_best.script;
        return finish(QStringLiteral("ok-best"));
    }
    tryOtherPacks();
}

void OcrJob::onFirstScan(bool ok, const QVector<Ocr::Word> &words)
{
    if (m_done)
        return;
    m_firstDone = true;
    m_firstOk = ok;
    m_firstWords = words;
    if (m_osdDone)
        evaluateFirst();
}

void OcrJob::evaluateFirst()
{
    if (!m_firstOk)
        return finish(QStringLiteral("failed"));
    const bool latin = Ocr::osdConfident(m_osd);   // and so Latin
    const QString script = latin ? m_osd.script : QString();
    bool anyLetter = false;
    for (const Ocr::Word &w : std::as_const(m_firstWords)) {
        for (const QChar c : w.text)
            anyLetter |= c.isLetter();
    }
    if (m_firstWords.isEmpty())
        return finish(QStringLiteral("empty"), {}, script);
    const Ocr::Quality q = Ocr::assess(m_firstWords, script);
    if (q.reliable)
        return finish(QStringLiteral("ok"), m_firstWords, script);
    if (!latin && anyLetter) {
        if (m_guessRunning) {
            m_waitingForGuess = true;   // afterGuess() when it's done
            return;
        }
        return afterGuess();
    }
    finish(QStringLiteral("unreliable"), m_firstWords, script);
}

// OSD wasn't sure and the Latin packs read nonsense: maybe one of the other
// installed packs reads it (Hebrew on two words is too little for OSD)
void OcrJob::tryOtherPacks()
{
    QList<QPair<QString, QStringList>> candidates;   // script, languages
    // OSD's guess first, when it had one
    QList<const ScriptInfo *> order;
    if (const ScriptInfo *guess = m_osd.isValid() ? findScript(m_osd.script) : nullptr)
        order.append(guess);
    for (const ScriptInfo &info : SCRIPTS)
        order.append(&info);
    for (const ScriptInfo *entry : std::as_const(order)) {
        const ScriptInfo &info = *entry;
        if (!*info.packs || candidates.size() >= 3)
            continue;
        const Ocr::Plan p = Ocr::plan(QString::fromLatin1(info.osd), m_preferred, m_installed);
        if (p.languages.isEmpty() || !p.missing.isEmpty())
            continue;
        bool duplicate = p.languages == m_guessLanguages;   // read already
        for (const auto &c : std::as_const(candidates))
            duplicate |= c.second == p.languages;
        if (!duplicate)
            candidates.append({QString::fromLatin1(info.osd), p.languages});
    }
    if (candidates.isEmpty())
        return weakGuessOrUnreliable();

    m_candidatesLeft = int(candidates.size());
    for (const auto &c : std::as_const(candidates)) {
        const QString script = c.first;
        const QStringList languages = c.second;
        scan(languages, [this, script, languages](bool ok, const QVector<Ocr::Word> &words) {
            if (m_done)
                return;
            if (ok && !words.isEmpty()) {
                const Ocr::Quality q = Ocr::assess(words, script);
                if (q.reliable && q.meanConfidence > m_best.quality.meanConfidence) {
                    m_best.quality = q;
                    m_best.script = script;
                    m_best.languages = languages;
                    m_best.text = Ocr::wordsToText(words);
                }
            }
            if (--m_candidatesLeft > 0)
                return;
            if (!m_best.text.isEmpty()) {
                m_firstLanguages = m_best.languages;
                m_result.quality = m_best.quality;
                m_result.text = m_best.text;
                m_result.script = m_best.script;
                return finish(QStringLiteral("ok-best"));
            }
            weakGuessOrUnreliable();
        });
    }
}

// Nothing read it well. OSD's guess wasn't sure enough to choose by it, but
// the Latin packs failing backs it up: when its pack isn't installed, offer it
void OcrJob::weakGuessOrUnreliable()
{
    if (m_osd.isValid() && m_osd.confidence >= 0.3 && m_osd.script != QLatin1String("Latin")) {
        const Ocr::Plan plan = Ocr::plan(m_osd.script, m_preferred, m_installed);
        if (!plan.missing.isEmpty()) {
            m_result.missing = plan.missing;
            return finish(QStringLiteral("missing"), {}, m_osd.script);
        }
    }
    finish(QStringLiteral("unreliable"), m_firstWords);
}

void OcrJob::finish(const QString &status, const QVector<Ocr::Word> &words, const QString &script)
{
    if (m_done)
        return;
    m_done = true;
    stopAll();
    delete m_dir;
    m_dir = nullptr;

    if (status == QLatin1String("ok-best")) {
        m_result.status = QStringLiteral("ok");   // text, script and quality already set
    } else {
        m_result.status = status;
        m_result.text = Ocr::wordsToText(words);
        m_result.script = script;
        m_result.quality = Ocr::assess(words, script);
    }
    m_result.scriptConfidence = m_osd.confidence;
    if (m_result.script.isEmpty() && m_osd.isValid() && Ocr::osdConfident(m_osd))
        m_result.script = m_osd.script;
    m_result.languages = status == QLatin1String("missing") ? QStringList() : m_firstLanguages;
    m_result.totalMs = m_clock.elapsed();
    qCInfo(lcOcr).noquote() << "OCR" << m_result.status << "script" << m_result.script
                            << "languages" << m_result.languages.join(QLatin1Char('+'))
                            << "confidence" << qRound(m_result.quality.meanConfidence)
                            << "words" << m_result.quality.wordRatio << m_result.totalMs << "ms";
    emit finished(m_result);
}

// ------------------------------------------------------------------ OcrEngine

OcrEngine::OcrEngine(QObject *parent)
    : QObject(parent)
    , m_tempDir(nullptr)
    , m_scale(1)
{
    qRegisterMetaType<Ocr::Analysis>();
    refresh();
}

OcrEngine::~OcrEngine()
{
    if (m_process) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    delete m_job;
    delete m_tempDir;
}

bool OcrEngine::hasLanguageData() const
{
    return !m_languages.isEmpty();
}

void OcrEngine::setWantedLanguages(const QString &languages)
{
    const QString value = languages.trimmed();
    if (value == m_wanted)
        return;
    m_wanted = value;
    emit stateChanged();
}

QStringList OcrEngine::missingPackages() const
{
    QStringList packages;
    if (!available())
        packages << QStringLiteral("tesseract");
    if (hasLanguageData())
        return packages;

    // Nothing to read with at all: the preferred languages, else Portuguese
    // and English (the packs the screenshot package depends on)
    QString wanted = m_wanted.isEmpty() ? QStringLiteral("por+eng") : m_wanted;
    const QStringList parts = wanted.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString lang = part.trimmed();
        // Only plain language codes become package names.
        bool valid = !lang.isEmpty();
        for (const QChar c : lang)
            valid &= c.isLetterOrNumber() || c == QLatin1Char('_');
        if (valid)
            packages << QStringLiteral("tesseract-data-") + lang.toLower();
    }

    return packages;
}

void OcrEngine::refresh()
{
    m_tesseract = QStandardPaths::findExecutable(QStringLiteral("tesseract"));
    m_languages.clear();

    if (!m_tesseract.isEmpty()) {
        QProcess p;
        p.start(m_tesseract, { QStringLiteral("--list-langs") });
        if (p.waitForFinished(5000) && p.exitStatus() == QProcess::NormalExit)
            m_languages = Ocr::parseLanguages(p.readAllStandardOutput() + p.readAllStandardError());
    }

    emit stateChanged();
}

void OcrEngine::install()
{
    if (m_installer)
        return;

    const QStringList packages = missingPackages();
    if (packages.isEmpty()) {
        emit installFinished(true, QString());
        return;
    }

    QStringList args;
    if (QFile::exists(QString::fromLatin1(STORE_HELPER)))
        args << QString::fromLatin1(STORE_HELPER) << QStringLiteral("install") << packages;
    else
        args << QStringLiteral("pacman") << QStringLiteral("-S") << QStringLiteral("--needed")
             << QStringLiteral("--noconfirm") << packages;
    startInstaller(QStringLiteral("pkexec"), args, packages);
}

void OcrEngine::installLanguages(const QStringList &codes)
{
    if (m_installer)
        return;
    QStringList clean;
    static const QRegularExpression valid(QStringLiteral("^[a-z][a-z0-9_]*$"));
    for (const QString &code : codes) {
        if (valid.match(code).hasMatch() && !clean.contains(code))
            clean << code;
    }
    if (clean.isEmpty()) {
        emit installFinished(true, QString());
        return;
    }
    QStringList packages;
    for (const QString &code : std::as_const(clean))
        packages << QStringLiteral("tesseract-data-") + code;

    // The same tool as Settings › Language › Reading languages
    const QString tool = Ocr::languagesTool();
    if (QFileInfo(tool).isExecutable()) {
        startInstaller(tool, QStringList{QStringLiteral("install")} + clean, packages);
        return;
    }
    QStringList args;
    if (QFile::exists(QString::fromLatin1(STORE_HELPER)))
        args << QString::fromLatin1(STORE_HELPER) << QStringLiteral("install") << packages;
    else
        args << QStringLiteral("pacman") << QStringLiteral("-S") << QStringLiteral("--needed")
             << QStringLiteral("--noconfirm") << packages;
    startInstaller(QStringLiteral("pkexec"), args, packages);
}

void OcrEngine::startInstaller(const QString &program, const QStringList &args, const QStringList &packages)
{
    m_installer = new QProcess(this);
    m_installer->setProcessChannelMode(QProcess::MergedChannels);
    m_installStatus.clear();

    qCInfo(lcOcr) << "Installing" << packages;

    connect(m_installer, &QProcess::readyRead, this, [this] {
        if (!m_installer)
            return;
        // "(2/3) installing tesseract-data-heb", "@@STEP install"
        static const QRegularExpression step(QStringLiteral("\\((\\d+)/(\\d+)\\) (downloading|installing|removing)"));
        while (m_installer->canReadLine()) {
            const QString line = QString::fromUtf8(m_installer->readLine()).trimmed();
            const QRegularExpressionMatch m = step.match(line);
            if (m.hasMatch()) {
                m_installStatus = m.captured(1) + QLatin1Char('/') + m.captured(2);
                emit installingChanged();
            }
        }
    });
    connect(m_installer, &QProcess::finished, this, [this, packages](int exitCode, QProcess::ExitStatus status) {
        const QByteArray output = m_installer->readAll();
        m_installer->deleteLater();
        m_installer = nullptr;
        m_installStatus.clear();
        emit installingChanged();
        refresh();

        QStringList installed;
        for (const QString &package : packages) {
            if (package.startsWith(QLatin1String("tesseract-data-")))
                installed << package.mid(15);
        }
        bool present = true;
        for (const QString &code : std::as_const(installed))
            present &= m_languages.contains(code);
        const bool ok = status == QProcess::NormalExit && exitCode == 0 && available() && hasLanguageData() && present;
        if (!ok)
            qCWarning(lcOcr).noquote() << "Install failed:" << exitCode << output.right(2000);

        // pkexec: 126 = dialog dismissed, 127 = not authorized.
        QString message;
        if (exitCode == 126 || exitCode == 127)
            message = tr("The installation was cancelled.");
        else if (!ok)
            message = tr("The text recognition packages could not be installed.");
        emit installFinished(ok, message);
    });
    connect(m_installer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_installer)
            return;
        m_installer->deleteLater();
        m_installer = nullptr;
        emit installingChanged();
        emit installFinished(false, tr("Could not start pkexec. Make sure polkit is installed."));
    });

    m_installer->start(program, args);
    emit installingChanged();
}

void OcrEngine::openLanguageSettings()
{
    QProcess::startDetached(QStringLiteral("lingmo-settings"), {QStringLiteral("-m"), QStringLiteral("language/ocr")});
}

OcrJob *OcrEngine::createJob(QObject *parent, const QString &forced) const
{
    return new OcrJob(m_tesseract, m_languages, m_wanted, forced, parent);
}

void OcrEngine::recognizeText(const QImage &image, const QString &languages)
{
    if (busy()) {
        emit failed(tr("Text recognition is already running."));
        return;
    }
    if (!available() || !hasLanguageData()) {
        emit failed(tr("Text recognition needs tesseract and its language data."));
        return;
    }
    if (image.isNull()) {
        emit failed(tr("There is nothing to recognize."));
        return;
    }

    m_job = createJob(this, languages);
    OcrJob *job = m_job;
    connect(job, &OcrJob::finished, this, [this, job](const Ocr::Analysis &a) {
        job->deleteLater();
        if (m_job == job)
            m_job = nullptr;
        emit busyChanged();
        if (a.status == QLatin1String("missing")) {
            emit failed(tr("This text is in %1. To read it, install its reading pack in Settings › Language › Reading languages.")
                                .arg(Ocr::scriptName(a.script)));
        } else if (a.status == QLatin1String("failed")) {
            emit failed(tr("Text recognition failed."));
        } else {
            emit textReady(a.text.trimmed());
        }
    });
    job->start(image);
    emit busyChanged();
}

void OcrEngine::recognizeBoxes(const QImage &image)
{
    run(image);
}

void OcrEngine::cancel()
{
    if (m_job) {
        m_job->disconnect(this);
        m_job->deleteLater();
        m_job = nullptr;
        emit busyChanged();
    }
    if (!m_process)
        return;

    m_process->disconnect(this);
    m_process->kill();
    m_process->deleteLater();
    m_process = nullptr;
    // The copy of the capture goes away now, not at the next recognition
    delete m_tempDir;
    m_tempDir = nullptr;
    emit busyChanged();
}

// Quick redact: word boxes in any language, so no detection
void OcrEngine::run(const QImage &image)
{
    const bool boxes = true;
    if (busy()) {
        emit failed(tr("Text recognition is already running."));
        return;
    }

    if (!available() || !hasLanguageData()) {
        emit failed(tr("Text recognition needs tesseract and its language data."));
        return;
    }

    if (image.isNull()) {
        emit failed(tr("There is nothing to recognize."));
        return;
    }

    // Private folder: the capture may contain anything.
    delete m_tempDir;
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    m_tempDir = new QTemporaryDir(base + QStringLiteral("/lingmo-screenshot-ocr-XXXXXX"));
    if (!m_tempDir->isValid()) {
        emit failed(tr("Unable to create a temporary file for text recognition."));
        return;
    }

    // Screen text is small for tesseract (made for ~300 dpi scans): doubling
    // the size and dropping the colours helps a lot.
    QImage input = image.convertToFormat(QImage::Format_Grayscale8);
    m_scale = 1;
    if (qint64(image.width()) * image.height() <= UPSCALE_LIMIT) {
        m_scale = 2;
        input = input.scaled(image.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    const QString file = m_tempDir->filePath(QStringLiteral("input.png"));
    if (!input.save(file, "PNG")) {
        emit failed(tr("Unable to create a temporary file for text recognition."));
        return;
    }

    QStringList args;
    args << file << QStringLiteral("stdout")
         << QStringLiteral("-l") << Ocr::languageArgument(Ocr::latinLanguages(m_wanted, m_languages).join(QLatin1Char('+')), m_languages)
         << QStringLiteral("--psm") << QStringLiteral("3");
    if (boxes)
        args << QStringLiteral("tsv");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // Several threads make small images slower, not faster.
    env.insert(QStringLiteral("OMP_THREAD_LIMIT"), QStringLiteral("1"));
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::finished, this, [this, boxes](int exitCode, QProcess::ExitStatus status) {
        QProcess *p = m_process;
        m_process = nullptr;
        const QByteArray out = p->readAllStandardOutput();
        const QByteArray err = p->readAllStandardError();
        p->deleteLater();
        delete m_tempDir;
        m_tempDir = nullptr;
        emit busyChanged();

        if (status != QProcess::NormalExit || exitCode != 0) {
            qCWarning(lcOcr).noquote() << "tesseract failed:" << err.right(1000);
            emit failed(tr("Text recognition failed."));
            return;
        }

        if (boxes) {
            QVector<QRect> result;
            const QVector<QRect> raw = Ocr::redactBoxes(Ocr::parseTsv(out), qRound(3 * m_scale));
            for (const QRect &r : raw)
                result.append(Ocr::scaleBox(r, m_scale));
            emit boxesReady(result);
        } else {
            QString text = QString::fromUtf8(out);
            // tesseract ends pages with a form feed.
            text.remove(QChar(0x0C));
            emit textReady(text.trimmed());
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process)
            return;
        m_process->deleteLater();
        m_process = nullptr;
        emit busyChanged();
        emit failed(tr("Unable to start tesseract."));
    });

    qCInfo(lcOcr).noquote() << "Running tesseract" << args.mid(1).join(QLatin1Char(' '));
    m_process->start(m_tesseract, args);
    emit busyChanged();
}
