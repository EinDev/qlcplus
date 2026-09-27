/*
  Q Light Controller Plus - Control API
  apiioconfigdomain.h

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

#ifndef APIIOCONFIGDOMAIN_H
#define APIIOCONFIGDOMAIN_H

#include <QObject>
#include <QPointer>
#include <QString>
#include <QList>
#include <QSharedPointer>
#include <QElapsedTimer>

class AudioCapture;
class ApiIoDomain;
class ApiServer;
class ApiSession;
class Doc;
class QLCInputProfile;

/**
 * The configuration half of the io domain (docs/api-spec/fragments/io.yaml),
 * split out of ApiIoDomain so the live/DMX code there stays focused:
 *
 *  - io.plugin.getLines / rescan / configure (+ io.plugin.linesChanged)
 *  - io.patch.setParameters (generic per-line plugin parameters - the
 *    remote-friendly replacement for a plugin's native dialog)
 *  - io.patch.output.setState (+ io.patch.output.stateChanged)
 *  - io.inputProfile.get / save / delete (+ changed / deleted, §4c
 *    profilesRevision, counter owned by ApiIoDomain because its list
 *    method reports it) and io.inputProfile.learn.start / stop with the
 *    learn.signal event delivered ONLY to the client that started the
 *    session (io-notes.md flagged the broadcast form as a UX problem)
 *  - io.grandMaster.setMode, io.universe.setMonitor
 *  - io.audio.listDevices / setDevice / setConfig (the host's own audio
 *    devices and format, the same QSettings keys qmlui's InputOutputManager
 *    writes) and io.audio.inputPreview.set (+ io.audio.inputLevel, sent only
 *    to the previewing clients)
 *
 * Engine-only (engine/src + engine/audio/src), no qmlui dependency - the
 * input profile editor is reimplemented on top of QLCInputProfile directly
 * rather than going through qmlui's InputProfileEditor.
 */
class ApiIoConfigDomain : public QObject
{
    Q_OBJECT

public:
    ApiIoConfigDomain(Doc *doc, ApiServer *server, ApiIoDomain *ioDomain, QObject *parent = nullptr);
    ~ApiIoConfigDomain() override;

private:
    void registerMethods();
    void registerPluginMethods();
    void registerPatchMethods();
    void registerProfileMethods();
    void registerLiveMethods();
    void registerAudioMethods();

    /** Learn session bookkeeping (io.inputProfile.learn.*). */
    void stopLearning();

private slots:
    /** InputOutputMap::pluginConfigurationChanged relay -> io.plugin.linesChanged. */
    void slotPluginConfigurationChanged(const QString &pluginName, bool success);
    /** InputOutputMap::inputValueChanged while a learn session is active ->
     *  io.inputProfile.learn.signal, sent to the learning client only. */
    void slotInputValueChanged(quint32 universe, quint32 channel, uchar value, const QString &key);
    /** The learning client went away: stop listening. */
    void slotLearnSessionDisconnected(ApiSession *session);

    /** AudioCapture::dataProcessed while an input level preview is on ->
     *  io.audio.inputLevel to the previewing clients (throttled). */
    void slotAudioPreviewData(double *spectrumBands, int size, double maxMagnitude, quint32 power);
    /** A previewing client went away. */
    void slotPreviewSessionDisconnected(ApiSession *session);

private:
    /** io.audio.inputPreview.set: the input level check of PopupAudioConfiguration.qml
     *  (InputOutputManager::enableAudioInputPreview), shared by every client that asked. */
    void attachAudioPreview();
    void detachAudioPreview();
    /** The input device / format changed (the capture was destroyed): re-open the preview. */
    void restartAudioPreview();

    QList<QPointer<ApiSession>> m_previewSessions;
    QSharedPointer<AudioCapture> m_previewCapture;
    QElapsedTimer m_previewThrottle;

private:
    Doc *m_doc;
    ApiServer *m_server;
    ApiIoDomain *m_ioDomain;

    /** Null when no learn session is active. Set by io.inputProfile.learn.start,
     *  cleared by learn.stop or the session disconnecting. */
    QPointer<ApiSession> m_learnSession;
    quint32 m_learnUniverse = 0;
    /** Profile whose channel map "alreadyMapped" is checked against
     *  (learn.start's optional profileName), empty = the universe's own. */
    QString m_learnProfileName;
};

#endif
