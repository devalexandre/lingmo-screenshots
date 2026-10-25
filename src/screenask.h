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

#ifndef SCREENASK_H
#define SCREENASK_H

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>

#include "ocr.h"

class AssistantEngine;
class CaptureSession;

// "Ask about the screen" ("Perguntar sobre a tela") for one capture: the
// selected area is recognized with tesseract and sent, with the question, to
// the Lingmo Assistant engine (request ask_image), which uses the model of
// ai.conf: a vision model gets the picture, any other model the recognized
// text. Exposed to QML as "screenAsk".
//
// Privacy: the crop only lives in memory (and in tesseract's private
// temporary file, deleted when it finishes); nothing is uploaded.
class ScreenAsk : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool engineInstalled READ engineInstalled CONSTANT)
    // idle, reading (OCR), waiting (sent), answering, done, error
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString question READ question NOTIFY stateChanged)
    Q_PROPERTY(QString answer READ answer NOTIFY answerChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString ocrText READ ocrText NOTIFY ocrChanged)
    Q_PROPERTY(bool ocrBusy READ ocrBusy NOTIFY ocrChanged)
    // How the text was read: "", ok, unreliable, missing, empty, failed
    Q_PROPERTY(QString ocrStatus READ ocrStatus NOTIFY ocrChanged)
    // "Hebrew" in the UI language, when a script was detected
    Q_PROPERTY(QString ocrLanguageName READ ocrLanguageName NOTIFY ocrChanged)
    // Download size of the missing packs, in MB (0: unknown)
    Q_PROPERTY(int missingSizeMb READ missingSizeMb NOTIFY ocrChanged)
    Q_PROPERTY(bool installingPack READ installingPack NOTIFY installChanged)
    Q_PROPERTY(QString installProgress READ installProgress NOTIFY installChanged)
    // "vision" or "ocr" once the engine answered; before that what it will be
    Q_PROPERTY(QString mode READ mode NOTIFY configChanged)
    Q_PROPERTY(QString model READ model NOTIFY configChanged)
    Q_PROPERTY(int turns READ turns NOTIFY stateChanged)
    Q_PROPERTY(bool listening READ listening NOTIFY voiceChanged)
    Q_PROPERTY(QString voiceState READ voiceState NOTIFY voiceChanged)
    Q_PROPERTY(qreal voiceLevel READ voiceLevel NOTIFY voiceChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY ocrChanged)

public:
    explicit ScreenAsk(CaptureSession *session, QObject *parent = nullptr);
    ~ScreenAsk() override;

    bool engineInstalled() const;
    QString state() const { return m_state; }
    bool busy() const;
    QString question() const { return m_question; }
    QString answer() const { return m_answer; }
    QString error() const { return m_error; }
    QString ocrText() const { return m_ocrText; }
    bool ocrBusy() const { return m_ocrBusy; }
    QString ocrStatus() const { return m_analysis.status; }
    QString ocrLanguageName() const;
    int missingSizeMb() const;
    bool installingPack() const;
    QString installProgress() const;
    QString mode() const;
    QString model() const { return m_model; }
    int turns() const { return m_turns; }
    bool listening() const;
    QString voiceState() const { return m_voiceState; }
    qreal voiceLevel() const { return m_voiceLevel; }
    QString windowTitle() const { return m_windowTitle; }

    // The selection (image pixels) changed: crop it and read its text now,
    // so the first question doesn't wait. A new area starts a new conversation.
    Q_INVOKABLE void prepare(const QRect &crop);
    Q_INVOKABLE void ask(const QString &text);
    // explain, translate, summarize, what (to the model); copy (the text)
    Q_INVOKABLE void askChip(const QString &kind);
    Q_INVOKABLE QString chipLabel(const QString &kind) const;
    Q_INVOKABLE void stop();
    // Installs the reading pack of the detected script (tesseract-data-heb)
    // and reads the selection again with it
    Q_INVOKABLE void installReadingPack();
    // "Copy text": the recognized text, or the model reads it when OCR found
    // none (vision models). False when it had to ask the model.
    Q_INVOKABLE bool copyText();
    Q_INVOKABLE void copyAnswer();
    // Opens the browser with the recognized text (only when clicked)
    Q_INVOKABLE void searchWeb();
    // Hands the conversation, the picture and the text to the Assistant app
    Q_INVOKABLE void continueInAssistant();
    // A floating card with the answer
    Q_INVOKABLE void pin();
    // Push-to-talk with the Assistant's voice input; a second call stops
    Q_INVOKABLE void toggleVoice();
    Q_INVOKABLE void cancelVoice();
    // The box around a circle or scribble (points: QML points), see
    // ScreenAskCore::strokeBounds
    Q_INVOKABLE QRectF strokeBounds(const QVariantList &points, qreal padding, const QRectF &bounds) const;

signals:
    void stateChanged();
    void answerChanged();
    void ocrChanged();
    void configChanged();
    void voiceChanged();
    void installChanged();
    // Short feedback for a toast
    void message(const QString &text, bool error);
    // The overlay should close (the answer went to the Assistant or a card)
    void closeRequested();
    // The question field gets this text (voice)
    void questionHeard(const QString &text);

private:
    void onEvent(const QVariantMap &event);
    void setState(const QString &state, const QString &error = QString());
    void startOcr();
    void applyAnalysis(const Ocr::Analysis &analysis);
    void send();
    void askQuestion(const QString &text, const QString &intent);
    QImage crop() const { return m_image; }

    QPointer<CaptureSession> m_session;
    AssistantEngine *m_engine = nullptr;
    QRect m_rect;
    QImage m_image;
    QString m_windowTitle;
    QString m_conversation;
    QString m_state = QStringLiteral("idle");
    QString m_question;
    QString m_pendingQuestion;
    QString m_answer;
    QString m_error;
    QString m_ocrText;        // only text that can be trusted
    Ocr::Analysis m_analysis;
    // Per crop (hash of the pixels and the installed packs): selecting the
    // same area again doesn't read it again. Memory only.
    QHash<QByteArray, Ocr::Analysis> m_cache;
    QByteArray m_cacheKey;
    QString m_intent;         // the chip of the pending question, or empty
    bool m_ocrBusy = false;
    bool m_ocrDone = false;
    QObject *m_ocrWatch = nullptr;
    QString m_mode;          // from image_mode
    QString m_model;
    QVariant m_vision;       // from the engine's config: true, false or null (unknown)
    int m_turns = 0;
    bool m_copyAfter = false;
    QString m_voiceState = QStringLiteral("idle");
    qreal m_voiceLevel = 0;
};

#endif // SCREENASK_H
