/*
  Q Light Controller Plus - Control API unit test
  apimediadomain_test.h

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

#ifndef APIMEDIADOMAIN_TEST_H
#define APIMEDIADOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>
#include <QTemporaryDir>
#include <functional>

class QSignalSpy;
class Doc;
class Scene;
class Script;
class Audio;
class Video;
class ApiServer;
class QWebSocket;

/**
 * End-to-end test of ApiMediaDomain (functions.script.*, functions.audio.*,
 * functions.video.*): a real ApiServer on an ephemeral localhost port driven
 * by a real QWebSocket client, asserting on responses, broadcast events and
 * the engine objects themselves. Same layout as apifunctionsdomain_test.
 */
class ApiMediaDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void scriptListCommandsReportsEngineKeywordsAndSnippets();
    void scriptSetSourceReplacesBodyAndBroadcasts();
    void scriptAppendLineAppendsAndBroadcasts();
    void scriptValidateReportsErrorLinesAndRefs();
    void scriptGetCarriesSourceAndSyntaxErrors();
    void scriptSetSourceOnStaleRevisionIsConflict();
    void scriptMethodsOnWrongTypeAreInvalidParams();

    void audioListCapabilitiesHasDefaultDevice();
    void audioSetVolumeDurationDeviceBumpRevisionAndBroadcast();
    void audioSetVolumeRejectsOutOfRange();
    void audioSetSourceImportsIntoStoreAndBroadcasts();
    void audioSetSourceOnMissingFileIsInvalidParams();
    void audioGetCarriesConfig();

    void videoListCapabilitiesHasExtensions();
    void videoSetGeometryRotationLayer();
    void videoSetScreenTargetKeepsSpoutUnlessModeGiven();
    void videoSetSourceUrlKeepsUrlAndBroadcasts();
    void videoGetCarriesConfig();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();
    QJsonObject waitForEvent(QSignalSpy &spy, const QString &topic,
                             const std::function<bool(const QJsonObject &)> &accept, int timeoutMs = 3000);
    /** params + the current docRevision as baseRevision */
    QJsonObject withRevision(QJsonObject params) const;
    QString writeMediaFile(const QString &name, const QByteArray &content);
    Script *addScript(const QString &source);
    Audio *addAudio(const QString &path);
    Video *addVideo(const QString &url);

private:
    Doc *m_doc;
    Scene *m_scene;
    ApiServer *m_apiServer;
    QTemporaryDir *m_tmp;
    QWebSocket *m_client;
};

#endif
