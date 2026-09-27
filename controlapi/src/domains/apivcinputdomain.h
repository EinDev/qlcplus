/*
  Q Light Controller Plus - Control API
  apivcinputdomain.h

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0.txt

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef APIVCINPUTDOMAIN_H
#define APIVCINPUTDOMAIN_H

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>

#include "apivchost.h"

class ApiDispatcher;
class ApiServer;
class ApiSession;
class Doc;

/**
 * The Virtual Console "external controls" slice of the vc.* domain
 * (docs/api-spec/fragments/virtualconsole.yaml), kept in its own class next to ApiVcDomain /
 * ApiVcLayoutDomain:
 *  - vc.widget.inputSource.set / vc.widget.inputSource.remove (§4a) - bind an external controller
 *    input (universe + channel of an input patch: MIDI, OSC, DMX-in, ...) to one of a widget's named
 *    controls, with the custom feedback values / MIDI feedback routing PopupCustomFeedback.qml edits;
 *    broadcast on vc.widget.inputSourcesChanged.
 *  - vc.widget.keySequence.set / vc.widget.keySequence.remove (§4a) - keyboard bindings
 *    (KeyboardSequenceDelegate.qml); broadcast on vc.widget.keySequencesChanged.
 *  - vc.widget.inputDetect.start / vc.widget.inputDetect.stop (§4b) - the "learn" wizard: the next
 *    signal on ANY input line is bound to the armed widget control.
 *
 * Auto-detection lives here, not in VirtualConsole's own autodetect slot: this class listens to
 * InputOutputMap::inputValueChanged() exactly like ApiIoConfigDomain's io.inputProfile.learn.* does
 * and, on the first signal, calls ApiVcHost::vcWidgetInputSourceSet() with what it heard. That keeps
 * the whole path testable against the headless FakeVcHost + engine/test/iopluginstub (VirtualConsole
 * needs a QQuickView) and avoids the engine path's side effect of parking an *invalid* placeholder
 * source on the widget (which stays behind - and is saved - when detection is aborted). What differs
 * from the QML learn: while armed, the signal is still dispatched normally to whatever is already
 * mapped to it (the engine's detection mode swallows it). One slot server-wide, like the engine's:
 * a second client's start is refused with INVALID_STATE (details.clientId names the holder) instead
 * of silently stealing the slot, the same client may re-arm another control, stop from anyone cancels
 * (the spec's wording), and the holder disconnecting releases it.
 *
 * Same host seam as the other two VC domains: the widget graph is reached through ApiVcHost (qmlui's
 * App in production, FakeVcHost in controlapi/test), obtained via dynamic_cast on ApiServer's parent.
 */
class ApiVcInputDomain : public QObject
{
    Q_OBJECT

public:
    ApiVcInputDomain(Doc *doc, ApiServer *server, QObject *parent = nullptr);
    ~ApiVcInputDomain() override;

private slots:
    /** InputOutputMap::inputValueChanged while a detection is armed -> bind and disarm. */
    void slotInputValueChanged(quint32 universe, quint32 channel, uchar value, const QString &key);
    void slotDetectSessionDisconnected(ApiSession *session);

private:
    void registerInputSourceMethods(ApiDispatcher *d);
    void registerKeySequenceMethods(ApiDispatcher *d);
    void registerDetectMethods(ApiDispatcher *d);

    ApiVcHost *vcHost() const;

    /** Sends ErrInternal and returns nullptr when no host is attached. */
    ApiVcHost *requireHost(ApiSession *session, const QString &id);

    /** §4a front half: compares params.baseRevision with Doc::docRevision(), answering CONFLICT
     *  (with the current revision in details) and returning false on a mismatch. */
    bool checkRevision(ApiSession *session, const QString &id, const QJsonObject &params);

    /** Resolves params.widgetId to an existing widget, answering NOT_FOUND itself. */
    bool resolveWidget(ApiSession *session, const QString &id, const QJsonObject &params, ApiVcHost *host, quint32 *outId);

    /** Resolves params.controlId against the widget's externalControls, answering INVALID_PARAMS
     *  (with the control table in details) itself. $needKeyboard also requires allowKeyboard. */
    bool resolveControl(ApiSession *session, const QString &id, const QJsonObject &params, ApiVcHost *host,
                        quint32 widgetId, bool needKeyboard, quint32 *outControlId);

    /** {docRevision} ok response after Doc::setModified(). */
    void replyRevision(ApiSession *session, const QString &id);

    void broadcastInputSourcesChanged(ApiVcHost *host, quint32 widgetId, const QString &originClientId);
    void broadcastKeySequencesChanged(ApiVcHost *host, quint32 widgetId, const QString &originClientId);

    /** Disarms the detection slot (idempotent). */
    void stopDetection();

    static bool parseWidgetId(const QString &s, quint32 &outId);

    Doc *m_doc;
    ApiServer *m_server;

    // The single detection slot.
    QPointer<ApiSession> m_detectSession;
    quint32 m_detectWidgetId;
    quint32 m_detectControlId;
};

#endif
