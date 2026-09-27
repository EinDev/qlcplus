/*
  Q Light Controller Plus - Control API unit test
  apifunctionsmiscdomain_test.h

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

#ifndef APIFUNCTIONSMISCDOMAIN_TEST_H
#define APIFUNCTIONSMISCDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>
#include <QTemporaryDir>
#include <functional>

class QSignalSpy;
class Doc;
class ApiServer;
class FakeVcHost;
class QWebSocket;
class Scene;
class Chaser;
class Sequence;

/**
 * End-to-end tests for the "function-side leftovers" slice: a real
 * ApiServer (parented to a FakeVcHost, so functions.usage sees widgets) on
 * an ephemeral port and a real QWebSocket client.
 *
 * Covers ApiFunctionsMiscDomain (chaser speed modes / actions, sequence
 * bound scene / dump values, adjustAttribute, tap, clone, usage, startup
 * function) plus the methods this slice appended to ApiMediaDomain (media
 * store status / collect / removeUnused, audio mute + BPM detection, video
 * volume / mute / Spout size), ApiRgbMatrixDomain (saveToSequence) and
 * ApiPaletteDomain (fanning).
 */
class ApiFunctionsMiscDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void chaserSetSpeedModesChangesAndBroadcasts();
    void chaserSetSpeedModesRejectsBadInput();
    void chaserSetActionValidates();
    void sequenceSetBoundSceneRebindsSteps();
    void sequenceApplyDumpValuesExplicit();
    void sequenceApplyDumpValuesCaptureLive();
    void adjustAttributeClampsAndBroadcasts();
    void tapKnownAndUnknown();
    void cloneSceneAndSequence();
    void usageListsFunctionsAndWidgets();
    void startupFunctionSetGetUnset();

    void mediaStatusCollectAndRemoveUnused();
    void audioSetMutedAndDetectBpm();
    void videoVolumeMuteSpoutSize();
    void rgbMatrixSaveToSequence();
    void paletteFanningRoundTrip();

private:
    QJsonObject call(const QString &method, const QJsonObject &params);
    QJsonObject callRev(const QString &method, QJsonObject params);
    int revision();
    QJsonObject waitForEvent(QSignalSpy &spy, const QString &topic,
                             const std::function<bool(const QJsonObject &)> &accept = nullptr, int timeoutMs = 3000);

    Scene *addScene(const QString &name);
    QString sid(quint32 id) const { return QString::number(id); }

private:
    Doc *m_doc;
    FakeVcHost *m_host;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
    int m_seq;
};

#endif
