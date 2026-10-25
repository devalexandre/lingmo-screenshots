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

#include "screenask.h"
#include "application.h"
#include "assistantengine.h"
#include "capturesession.h"
#include "ocr.h"
#include "screenaskcore.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDesktopServices>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QCryptographicHash>
#include <QProcess>
#include <QScreen>
#include <QUuid>

Q_LOGGING_CATEGORY(lcAsk, "lingmo.screenshot.ask", QtInfoMsg)

ScreenAsk::ScreenAsk(CaptureSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
}

ScreenAsk::~ScreenAsk()
{
    // A running OcrJob is a child: it stops with us
    if (m_engine && busy())
        m_engine->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")}});
    if (listening())
        cancelVoice();
    // The engine process ends with m_engine (EOF on its stdin)
}

bool ScreenAsk::engineInstalled() const
{
    return AssistantEngine::isInstalled();
}

bool ScreenAsk::busy() const
{
    return m_state == QLatin1String("reading") || m_state == QLatin1String("waiting")
            || m_state == QLatin1String("answering");
}

bool ScreenAsk::listening() const
{
    return m_voiceState == QLatin1String("listening") || m_voiceState == QLatin1String("transcribing");
}

QString ScreenAsk::mode() const
{
    if (!m_mode.isEmpty())
        return m_mode;
    if (m_vision.isValid() && !m_vision.isNull())
        return m_vision.toBool() ? QStringLiteral("vision") : QStringLiteral("ocr");
    return {};
}

void ScreenAsk::setState(const QString &state, const QString &error)
{
    if (m_state == state && m_error == error)
        return;
    m_state = state;
    m_error = error;
    // Never the question, the text or the answer
    qCInfo(lcAsk).noquote() << "state" << state;
    emit stateChanged();
}

void ScreenAsk::prepare(const QRect &crop)
{
    if (!m_session)
        return;
    const QRect r = crop.intersected(QRect(QPoint(0, 0), m_session->image().size()));
    if (r.isEmpty() || r == m_rect)
        return;

    if (busy() && m_state != QLatin1String("reading"))
        stop();
    m_rect = r;
    // What is on screen, annotations included (an arrow drawn in the editor
    // is part of the question)
    m_image = m_session->render(r);
    m_windowTitle = m_session->windowTitleAt(r.center());
    // A new area is a new conversation
    m_conversation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_turns = 0;
    m_mode.clear();
    m_question.clear();
    m_answer.clear();
    emit answerChanged();
    setState(QStringLiteral("idle"));
    startOcr();

    // Start the engine now: it reports the model and whether it sees images,
    // and is warm for the first question
    if (!m_engine && engineInstalled()) {
        m_engine = new AssistantEngine(this);
        connect(m_engine, &AssistantEngine::event, this, &ScreenAsk::onEvent);
        connect(m_engine, &AssistantEngine::failed, this, [this](const QString &text) {
            if (busy() && m_state != QLatin1String("reading"))
                setState(QStringLiteral("error"), text);
            if (listening()) {
                m_voiceState = QStringLiteral("idle");
                emit voiceChanged();
            }
        });
        m_engine->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("status")}});
    }
}

void ScreenAsk::startOcr()
{
    OcrEngine *ocr = Application::instance()->ocr();
    delete m_ocrWatch;   // an OcrJob: deleting it stops tesseract
    m_ocrWatch = nullptr;
    m_ocrText.clear();
    m_analysis = Ocr::Analysis();
    m_ocrDone = false;
    m_ocrBusy = false;

    if (!ocr->available() || !ocr->hasLanguageData()) {
        // The vision model can still look at the picture
        m_analysis.status = QStringLiteral("failed");
        m_ocrDone = true;
        emit ocrChanged();
        return;
    }

    // The same pixels with the same packs: the same result
    QCryptographicHash hash(QCryptographicHash::Sha1);
    const QImage image = m_image.convertToFormat(QImage::Format_ARGB32);
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constBits()), image.sizeInBytes()));
    hash.addData(QByteArray::number(image.width()));
    hash.addData(ocr->languages().join(QLatin1Char('+')).toUtf8());
    m_cacheKey = hash.result();
    const auto cached = m_cache.constFind(m_cacheKey);
    if (cached != m_cache.constEnd()) {
        qCInfo(lcAsk) << "OCR from the cache";
        applyAnalysis(cached.value());
        return;
    }

    OcrJob *job = ocr->createJob(this);
    m_ocrWatch = job;
    connect(job, &OcrJob::finished, this, [this, job](const Ocr::Analysis &analysis) {
        if (job != m_ocrWatch)
            return;
        m_ocrWatch = nullptr;
        job->deleteLater();
        if (m_cache.size() > 16)
            m_cache.clear();
        m_cache.insert(m_cacheKey, analysis);
        applyAnalysis(analysis);
    });
    m_ocrBusy = true;
    emit ocrChanged();
    job->start(m_image);
}

void ScreenAsk::applyAnalysis(const Ocr::Analysis &analysis)
{
    m_analysis = analysis;
    // Only text that can be trusted goes anywhere (the model, the clipboard,
    // the web search): garbage read with the wrong language once made the
    // model "translate" a story that wasn't there
    m_ocrText = analysis.reliable() ? analysis.text.trimmed() : QString();
    qCInfo(lcAsk).noquote() << "OCR" << analysis.status << analysis.script << analysis.totalMs << "ms";
    m_ocrBusy = false;
    m_ocrDone = true;
    emit ocrChanged();
    if (!m_pendingQuestion.isEmpty())
        send();
}

QString ScreenAsk::ocrLanguageName() const
{
    return m_analysis.script.isEmpty() ? QString() : Ocr::scriptName(m_analysis.script);
}

int ScreenAsk::missingSizeMb() const
{
    int kib = 0;
    for (const QString &code : m_analysis.missing)
        kib += Ocr::packDownloadKiB(code);
    return kib > 0 ? qMax(1, (kib + 512) / 1024) : 0;
}

bool ScreenAsk::installingPack() const
{
    return Application::instance()->ocr()->installing();
}

QString ScreenAsk::installProgress() const
{
    return Application::instance()->ocr()->installStatus();
}

void ScreenAsk::installReadingPack()
{
    OcrEngine *ocr = Application::instance()->ocr();
    if (m_analysis.missing.isEmpty() || ocr->installing())
        return;
    auto *once = new QObject(this);
    connect(ocr, &OcrEngine::installingChanged, once, [this] { emit installChanged(); });
    connect(ocr, &OcrEngine::installFinished, once, [this, once](bool ok, const QString &text) {
        once->deleteLater();
        emit installChanged();
        if (!ok) {
            if (!text.isEmpty())
                emit message(text, true);
            return;
        }
        emit message(tr("Reading pack installed. Reading the selection again…"), false);
        // Read again at once with the new pack (the cache key has the packs)
        m_cache.clear();
        if (!m_image.isNull())
            startOcr();
    });
    ocr->installLanguages(m_analysis.missing);
    emit installChanged();
}

QString ScreenAsk::chipLabel(const QString &kind) const
{
    if (kind == QLatin1String("explain"))
        return tr("Explain");
    if (kind == QLatin1String("translate")) {
        QString language = QLocale().nativeLanguageName();
        if (!language.isEmpty())
            language[0] = language.at(0).toUpper();
        return tr("Translate to %1").arg(language);
    }
    if (kind == QLatin1String("summarize"))
        return tr("Summarize");
    if (kind == QLatin1String("copy"))
        return tr("Copy text");
    if (kind == QLatin1String("what"))
        return tr("What is this?");
    if (kind == QLatin1String("web"))
        return tr("Search the web");
    return kind;
}

void ScreenAsk::askChip(const QString &kind)
{
    if (kind == QLatin1String("web")) {
        searchWeb();
        return;
    }
    if (kind == QLatin1String("copy")) {
        copyText();
        return;
    }
    askQuestion(ScreenAskCore::chipQuestion(kind), kind);
}

void ScreenAsk::ask(const QString &text)
{
    askQuestion(text, QString());
}

void ScreenAsk::askQuestion(const QString &text, const QString &intent)
{
    const QString question = text.trimmed();
    if (question.isEmpty() || m_image.isNull())
        return;
    if (!engineInstalled()) {
        setState(QStringLiteral("error"), tr("Install Lingmo Assistant to ask the AI about the screen."));
        return;
    }
    if (busy() && m_state != QLatin1String("reading"))
        return;
    m_question = question;
    m_pendingQuestion = question;
    m_intent = intent;
    m_answer.clear();
    emit answerChanged();
    if (!m_ocrDone) {
        // Sent as soon as the text is read (a second or so)
        setState(QStringLiteral("reading"));
        return;
    }
    send();
}

void ScreenAsk::send()
{
    if (m_pendingQuestion.isEmpty() || !m_engine)
        return;
    QByteArray format;
    const QByteArray data = ScreenAskCore::encodeImage(m_image, 1280, 3 * 1024 * 1024, &format);
    QJsonObject request{
        {QStringLiteral("type"), QStringLiteral("ask_image")},
        {QStringLiteral("conversation_id"), m_conversation},
        {QStringLiteral("message"), m_pendingQuestion},
        {QStringLiteral("image"), QString::fromLatin1(data.toBase64())},
        // Only text that can be trusted; otherwise how the reading went
        {QStringLiteral("ocr_text"), m_ocrText},
        {QStringLiteral("ocr_status"), m_analysis.status},
        {QStringLiteral("ocr_script"), m_analysis.script},
        {QStringLiteral("ocr_confidence"), qRound(m_analysis.quality.meanConfidence)},
        {QStringLiteral("intent"), m_intent},
        // The answer's language: the system's
        {QStringLiteral("language"), ScreenAskCore::languageName()},
        {QStringLiteral("source"), QJsonObject{{QStringLiteral("window"), m_windowTitle},
                                               {QStringLiteral("width"), m_image.width()},
                                               {QStringLiteral("height"), m_image.height()}}},
    };
    m_pendingQuestion.clear();
    setState(QStringLiteral("waiting"));
    m_engine->send(request);
}

void ScreenAsk::onEvent(const QVariantMap &event)
{
    const QString type = event.value(QStringLiteral("type")).toString();
    const QString conv = event.value(QStringLiteral("conversation_id")).toString();

    if (type == QLatin1String("ready") || type == QLatin1String("config")) {
        m_model = event.value(QStringLiteral("model")).toString();
        m_vision = event.value(QStringLiteral("vision"));
        emit configChanged();
        return;
    }
    if (type == QLatin1String("voice_state")) {
        const QString state = event.value(QStringLiteral("state")).toString();
        m_voiceState = state;
        if (state != QLatin1String("listening"))
            m_voiceLevel = 0;
        emit voiceChanged();
        if (state == QLatin1String("error"))
            emit message(event.value(QStringLiteral("message")).toString(), true);
        return;
    }
    if (type == QLatin1String("voice_level")) {
        m_voiceLevel = event.value(QStringLiteral("level")).toReal();
        emit voiceChanged();
        return;
    }
    if (type == QLatin1String("voice_text")) {
        const QString text = event.value(QStringLiteral("text")).toString().trimmed();
        if (!text.isEmpty()) {
            emit questionHeard(text);
            ask(text);
        }
        return;
    }
    if (!conv.isEmpty() && conv != m_conversation)
        return;

    if (type == QLatin1String("image_mode")) {
        m_mode = event.value(QStringLiteral("mode")).toString();
        m_model = event.value(QStringLiteral("model")).toString();
        emit configChanged();
    } else if (type == QLatin1String("started")) {
        setState(QStringLiteral("waiting"));
    } else if (type == QLatin1String("token")) {
        m_answer += event.value(QStringLiteral("text")).toString();
        emit answerChanged();
        if (m_state != QLatin1String("answering"))
            setState(QStringLiteral("answering"));
    } else if (type == QLatin1String("confirmation_required")) {
        // Permissions are answered in the Assistant, never in this panel
        setState(QStringLiteral("error"),
                 tr("The assistant wants to do something that needs your permission. Continue in the Assistant to answer."));
    } else if (type == QLatin1String("error")) {
        if (m_state == QLatin1String("idle") && conv.isEmpty())
            return;   // e.g. a voice request while busy; nothing of ours
        setState(QStringLiteral("error"), event.value(QStringLiteral("message")).toString());
    } else if (type == QLatin1String("done")) {
        ++m_turns;
        setState(QStringLiteral("done"));
        if (m_copyAfter) {
            m_copyAfter = false;
            if (!m_answer.trimmed().isEmpty()) {
                Application::instance()->copyText(m_answer.trimmed());
                emit message(tr("Text copied."), false);
            }
        }
    }
}

void ScreenAsk::stop()
{
    m_pendingQuestion.clear();
    m_copyAfter = false;
    if (m_engine && (m_state == QLatin1String("waiting") || m_state == QLatin1String("answering")))
        m_engine->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("stop")}});
    if (busy())
        setState(m_answer.isEmpty() ? QStringLiteral("idle") : QStringLiteral("done"));
}

bool ScreenAsk::copyText()
{
    if (!m_ocrText.isEmpty()) {
        Application::instance()->copyText(m_ocrText);
        emit message(tr("Text copied."), false);
        return true;
    }
    if (m_ocrBusy) {
        // Copied once tesseract is done
        auto *once = new QObject(this);
        connect(this, &ScreenAsk::ocrChanged, once, [this, once] {
            if (m_ocrBusy)
                return;
            once->deleteLater();
            copyText();
        });
        return true;
    }
    if (mode() == QLatin1String("vision")) {
        m_copyAfter = true;
        askQuestion(ScreenAskCore::chipQuestion(QStringLiteral("copy")), QStringLiteral("copy"));
        return false;
    }
    if (m_analysis.status == QLatin1String("missing"))
        emit message(tr("To read %1, install the reading pack first.").arg(ocrLanguageName()), true);
    else if (m_analysis.status == QLatin1String("unreliable"))
        emit message(tr("The text in the selection couldn't be read reliably."), true);
    else
        emit message(tr("No text was found in the selection."), true);
    return false;
}

void ScreenAsk::copyAnswer()
{
    if (m_answer.trimmed().isEmpty())
        return;
    Application::instance()->copyText(m_answer.trimmed());
    emit message(tr("Answer copied."), false);
}

void ScreenAsk::searchWeb()
{
    const QUrl url = ScreenAskCore::webSearchUrl(m_ocrText);
    if (url.isEmpty()) {
        emit message(m_ocrBusy ? tr("Still reading the text, try again in a moment.")
                               : tr("No text was found in the selection to search for."), true);
        return;
    }
    QDesktopServices::openUrl(url);
    emit closeRequested();
}

void ScreenAsk::continueInAssistant()
{
    if (m_image.isNull())
        return;
    if (m_engine && busy())
        stop();

    // The picture goes over D-Bus, in memory: the Assistant keeps it for the
    // follow-up questions of this conversation and never writes it to disk
    QByteArray format;
    const QByteArray data = ScreenAskCore::encodeImage(m_image, 1280, 3 * 1024 * 1024, &format);
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("com.lingmo.Assistant"), QStringLiteral("/Assistant"),
                                                       QStringLiteral("com.lingmo.Assistant"),
                                                       QStringLiteral("continueWithImage"));
    call << m_conversation << data << m_ocrText << m_windowTitle;
    const QString conversation = m_conversation;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 20000), Application::instance());
    connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [watcher, conversation] {
        watcher->deleteLater();
        if (watcher->isError()) {
            // An older Assistant: at least the conversation
            qCWarning(lcAsk).noquote() << "continueWithImage failed:" << watcher->error().message();
            QProcess::startDetached(QStringLiteral("lingmo-assistant"), {QStringLiteral("--open"), conversation});
        }
    });
    emit closeRequested();
}

void ScreenAsk::pin()
{
    if (m_answer.trimmed().isEmpty())
        return;
    Application::instance()->pinAnswer(m_question, m_answer.trimmed(), m_model);
    emit closeRequested();
}

void ScreenAsk::toggleVoice()
{
    if (!engineInstalled())
        return;
    if (!m_engine) {
        // prepare() wasn't called yet: nothing to ask about
        return;
    }
    // A second voice_start while listening stops the recording (push-to-talk)
    m_engine->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("voice_start")},
                               {QStringLiteral("commands"), false}});
    if (!listening()) {
        m_voiceState = QStringLiteral("listening");
        emit voiceChanged();
    }
}

QRectF ScreenAsk::strokeBounds(const QVariantList &points, qreal padding, const QRectF &bounds) const
{
    QVector<QPointF> list;
    list.reserve(points.size());
    for (const QVariant &v : points)
        list.append(v.toPointF());
    return ScreenAskCore::strokeBounds(list, padding, bounds);
}

void ScreenAsk::cancelVoice()
{
    if (m_engine && listening())
        m_engine->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("voice_cancel")}});
    m_voiceState = QStringLiteral("idle");
    m_voiceLevel = 0;
    emit voiceChanged();
}
