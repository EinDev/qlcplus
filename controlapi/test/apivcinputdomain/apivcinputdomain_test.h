/*
  Q Light Controller Plus - Control API unit test
  apivcinputdomain_test.h

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

#ifndef APIVCINPUTDOMAIN_TEST_H
#define APIVCINPUTDOMAIN_TEST_H

#include <QJsonObject>
#include <QList>
#include <QObject>

class Doc;
class ApiServer;
class FakeVcHost;
class IOPluginStub;
class QWebSocket;

/**
 * End-to-end test for ApiVcInputDomain, same shape as controlapi/test/apivclayoutdomain: a real
 * ApiServer on an ephemeral localhost port driven by real QWebSockets, with the headless FakeVcHost
 * standing in for qmlui's App. The Doc is real (MasterTimer + universes running) so the auto-detect
 * cases can feed an input signal through engine/test/iopluginstub -> InputPatch -> InputOutputMap.
 */
class ApiVcInputDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void snapshotExposesExternalControls();
    void inputSourceSetCreatesAndBroadcasts();
    void inputSourceSetUpdatesExistingUniverseChannel();
    void inputSourceSetValidates();
    void inputSourceRemove();
    void keySequenceSetAndRemove();
    void keySequenceSetValidates();
    void keySequenceRebindLeavesOneEntry();
    void structuralMethodsConflictOnStaleRevision();
    void inputDetectBindsNextSignal();
    void inputDetectSlotIsGlobal();
    void inputDetectStopAndDisconnectRelease();

private:
    QJsonObject sendAndWaitForReply(QWebSocket *client, const QString &method, const QJsonObject &params, const QString &requestId = QStringLiteral("t-1"));
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId = QStringLiteral("t-1"));
    QString hello(QWebSocket *client);
    QWebSocket *connectSecondClient();
    int currentDocRevision();

    /** vc.widget.create shortcut on page 0, returning the wire id. */
    QString createWidget(const QString &widgetType, const QJsonObject &typeConfig = QJsonObject());
    QJsonObject widgetDetail(const QString &widgetId);

    IOPluginStub *loadStubPlugin();
    QString stubName(IOPluginStub *stub) const;

private:
    Doc *m_doc;
    FakeVcHost *m_vcHost;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
    QList<QWebSocket *> m_extraClients;
};

#endif
