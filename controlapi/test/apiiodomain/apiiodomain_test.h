/*
  Q Light Controller Plus - Control API unit test
  apiiodomain_test.h

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

#ifndef APIIODOMAIN_TEST_H
#define APIIODOMAIN_TEST_H

#include <QObject>

class Doc;
class ApiServer;
class QWebSocket;

/**
 * End-to-end test: a real ApiServer listening on an ephemeral localhost
 * port, driven by a real QWebSocket client - exercises the actual
 * transport (not just the dispatch logic in isolation), while still being
 * a fast, hermetic, single-process QTest (no external process/tooling
 * needed, unlike a manual wscat-based smoke test).
 */
class ApiIoDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void helloReturnsWelcome();
    void requestBeforeHelloIsUnauthorized();
    void universeCreateBumpsRevision();
    void universeCreateWithStaleRevisionConflicts();
    void grandMasterSetValueBroadcastsLiveEvent();
    void blackoutToggleBroadcastsLiveEvent();
    void dmxEventOnlyDeliveredAfterSubscribe();

private:
    /** Send a request and wait for exactly one more text message to arrive
     *  on client, returning it parsed as a JSON object. */
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();

private:
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
