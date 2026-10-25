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

#ifndef OCR_H
#define OCR_H

#include <QElapsedTimer>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QStringList>
#include <QVector>

#include <functional>

class QProcess;
class QTemporaryDir;

// Text recognition with tesseract, run locally on the cropped capture.
// Nothing leaves the computer.
//
// The language is automatic: tesseract's orientation and script detection
// (OSD, "--psm 0") names the script of the crop (Hebrew, Arabic, Latin…), and
// the installed packs for that script are used. When OSD isn't sure (little
// text), the installed Latin packs are tried, and then the other installed
// packs. Every result carries how much it can be trusted: garbage read with
// the wrong language must never reach the AI as "the text".
namespace Ocr
{
    struct Word {
        QRect box;
        QString text;
        int confidence = -1;
        int block = 0;
        int paragraph = 0;
        int line = 0;
    };

    // tesseract's "tsv" output: one word per level-5 row.
    QVector<Word> parseTsv(const QByteArray &tsv);
    // The words back as text: one line per tesseract line, a blank line
    // between blocks.
    QString wordsToText(const QVector<Word> &words);
    // Boxes to hide for Quick redact: the words of each line merged into one
    // box (so the gaps don't leak word lengths), grown by padding.
    QVector<QRect> redactBoxes(const QVector<Word> &words, int padding, int minConfidence = 30);
    // `tesseract --list-langs` output.
    QStringList parseLanguages(const QByteArray &output);
    // The wanted languages ("por+eng") that are installed, joined with '+'.
    QString languageArgument(const QString &wanted, const QStringList &installed);
    // Scales a box from the (upscaled) image tesseract read back to the capture.
    QRect scaleBox(const QRect &box, qreal factor);

    // ---- script detection

    // `tesseract --psm 0` output ("Script: Hebrew", "Script confidence: 7.22").
    struct Osd {
        QString script;
        double confidence = 0;
        bool isValid() const { return !script.isEmpty(); }
    };
    Osd parseOsd(const QByteArray &output);
    // Whether the script is sure enough to choose the languages by it. Han and
    // Japanese share characters, so OSD's margin between them is always small.
    bool osdConfident(const Osd &osd);
    // The packs that read a script OSD names: Hebrew → heb, Cyrillic →
    // rus+ukr… Empty for Latin (see latinLanguages) and unknown scripts.
    QStringList scriptPacks(const QString &script);
    bool isLatinLanguage(const QString &code);
    // The Latin packs to read with: the preferred ones (the old "por+eng"
    // setting, may be empty) first, then every other installed Latin pack, at
    // most max. Never empty when a Latin pack is installed.
    QStringList latinLanguages(const QString &preferred, const QStringList &installed, int max = 5);

    struct Plan {
        QStringList languages;   // what tesseract gets ("-l a+b")
        QStringList missing;     // packs of the detected script that aren't installed
    };
    // The languages for a script (empty: not detected). A non-Latin script
    // also gets a Latin pack, since names, numbers and code in Latin letters
    // are mixed in almost everywhere (ara+eng). The preferred setting only
    // orders the Latin packs: it never keeps out an installed pack of the
    // detected script.
    Plan plan(const QString &script, const QString &preferred, const QStringList &installed);

    // ---- how much the text can be trusted

    struct Quality {
        double meanConfidence = -1;   // tesseract's, weighted by length; -1: no words
        double wordRatio = 0;         // tokens that look like words of the script
        double scriptRatio = 1;       // letters in the expected script
        bool reliable = false;
    };
    // Text-only checks: tokens like "bR", "N0", "(3w" or letters of another
    // script mean the wrong language was used. script: an OSD name or empty.
    Quality assessText(const QString &text, const QString &script = QString());
    // assessText plus tesseract's own word confidences (unreliable under 60).
    Quality assess(const QVector<Word> &words, const QString &script = QString());
    constexpr double MinConfidence = 60;

    // The result of reading a crop.
    struct Analysis {
        // ok: text can be trusted; unreliable: something was read but it may
        // be wrong; missing: the script needs a pack that isn't installed;
        // empty: no text; failed: tesseract didn't run
        QString status;
        QString text;
        QString script;             // OSD's name, when it was sure
        double scriptConfidence = 0;
        QStringList languages;      // used
        QStringList missing;        // packs to install (status missing)
        Quality quality;
        qint64 osdMs = -1;
        qint64 totalMs = 0;
        bool reliable() const { return status == QLatin1String("ok"); }
    };

    // The script in the user's language ("Hebrew" → "hebraico").
    QString scriptName(const QString &script);
    // Approximate download size of a tesseract-data pack in KiB (0: unknown).
    int packDownloadKiB(const QString &code);
    // The shared tool that lists, installs and removes the packs (also used
    // by Settings › Language › Reading languages).
    QString languagesTool();
}

Q_DECLARE_METATYPE(Ocr::Analysis)

// One reading of a crop: OSD and a first scan run side by side, then the scan
// with the detected script's packs when that's different. Deleting the job
// stops it (and deletes the private copy of the image).
class OcrJob : public QObject
{
    Q_OBJECT

public:
    // forced: exactly these languages, no detection ("por+eng")
    OcrJob(const QString &tesseract, const QStringList &installed, const QString &preferred,
           const QString &forced = QString(), QObject *parent = nullptr);
    ~OcrJob() override;

    void start(const QImage &image);
    Ocr::Analysis result() const { return m_result; }

signals:
    void finished(const Ocr::Analysis &analysis);

private:
    QProcess *launch(const QStringList &args, std::function<void(bool ok, const QByteArray &out)> done);
    QProcess *scan(const QStringList &languages, std::function<void(bool ok, const QVector<Ocr::Word> &words)> done);
    void onOsd(bool ok, const QByteArray &out);
    void onFirstScan(bool ok, const QVector<Ocr::Word> &words);
    void evaluateFirst();
    void tryOtherPacks();
    void weakGuessOrUnreliable();
    void afterGuess();
    void finish(const QString &status, const QVector<Ocr::Word> &words = {}, const QString &script = QString());
    void stopAll();

    QString m_tesseract;
    QStringList m_installed;
    QString m_preferred;
    QString m_forced;
    QTemporaryDir *m_dir = nullptr;
    QString m_file;
    QList<QPointer<QProcess>> m_processes;
    QElapsedTimer m_clock;
    Ocr::Osd m_osd;
    bool m_osdDone = false;
    bool m_firstDone = false;
    bool m_firstOk = false;
    bool m_done = false;
    QStringList m_firstLanguages;
    QVector<Ocr::Word> m_firstWords;
    Ocr::Analysis m_result;
    int m_candidatesLeft = 0;
    Ocr::Analysis m_best;
    bool m_guessRunning = false;
    bool m_waitingForGuess = false;
    QStringList m_guessLanguages;
};

class OcrEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(bool hasLanguageData READ hasLanguageData NOTIFY stateChanged)
    Q_PROPERTY(QStringList languages READ languages NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool installing READ installing NOTIFY installingChanged)
    Q_PROPERTY(QString installStatus READ installStatus NOTIFY installingChanged)
    Q_PROPERTY(QString wantedLanguages READ wantedLanguages WRITE setWantedLanguages NOTIFY stateChanged)
    Q_PROPERTY(QStringList missingPackages READ missingPackages NOTIFY stateChanged)

public:
    explicit OcrEngine(QObject *parent = nullptr);
    ~OcrEngine() override;

    bool available() const { return !m_tesseract.isEmpty(); }
    bool hasLanguageData() const;
    QStringList languages() const { return m_languages; }
    bool busy() const { return m_process != nullptr || m_job != nullptr; }
    bool installing() const { return m_installer != nullptr; }
    // The last progress line of an install ("2/3")
    QString installStatus() const { return m_installStatus; }
    // Preferred languages when the script can't be detected; empty: every
    // installed Latin pack
    QString wantedLanguages() const { return m_wanted; }
    void setWantedLanguages(const QString &languages);
    // Arch packages needed before anything can be read at all.
    QStringList missingPackages() const;

    Q_INVOKABLE void refresh();
    // Installs tesseract and the language data through polkit.
    Q_INVOKABLE void install();
    // Installs language packs (tesseract codes: "heb") through the shared
    // tool, in one transaction. Ends with installFinished.
    Q_INVOKABLE void installLanguages(const QStringList &codes);
    // Settings › Language › Reading languages
    Q_INVOKABLE void openLanguageSettings();

    // A detection + reading job, owned by parent; the caller starts it.
    OcrJob *createJob(QObject *parent, const QString &forced = QString()) const;

    // Results come back through textReady / boxesReady / failed.
    // languages: exactly these (the screenshot.ocr intent), else automatic.
    void recognizeText(const QImage &image, const QString &languages = QString());
    void recognizeBoxes(const QImage &image);
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void busyChanged();
    void installingChanged();
    void installFinished(bool ok, const QString &message);
    void textReady(const QString &text);
    void boxesReady(const QVector<QRect> &boxes);
    void failed(const QString &message);

private:
    void run(const QImage &image);
    void startInstaller(const QString &program, const QStringList &args, const QStringList &packages);

    QString m_tesseract;
    QStringList m_languages;
    QString m_wanted;
    QString m_installStatus;
    QPointer<QProcess> m_process;
    QPointer<QProcess> m_installer;
    QPointer<OcrJob> m_job;
    QTemporaryDir *m_tempDir;
    qreal m_scale;
};

#endif // OCR_H
