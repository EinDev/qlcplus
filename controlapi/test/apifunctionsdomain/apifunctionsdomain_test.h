/*
  Q Light Controller Plus - Control API unit test
  apifunctionsdomain_test.h

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

#ifndef APIFUNCTIONSDOMAIN_TEST_H
#define APIFUNCTIONSDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>

class Doc;
class Scene;
class ApiServer;
class QWebSocket;

/**
 * End-to-end test: a real ApiServer listening on an ephemeral localhost
 * port, driven by a real QWebSocket client, with a real MasterTimer thread
 * actually running (unlike apiiodomain_test, which deliberately avoids that
 * for its DMX-tick coverage) - functions.start/stop only take effect once
 * MasterTimer's timerTickFunctions() processes the start/stop queue on its
 * own thread, so isRunning()/isPaused() are polled rather than asserted
 * synchronously.
 */
class ApiFunctionsDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startRunsFunction();
    void startOnMissingFunctionIsNotFound();
    void stopStopsFunction();
    void setPausePausesRunningFunction();

    void createSceneAddsFunctionAndBumpsRevision();
    void createBroadcastsFunctionsCreatedEvent();
    void createOnStaleRevisionIsConflict();
    void createSequenceAutoCreatesHiddenBoundScene();
    void getReturnsGenericAndSceneTypeDetail();
    void listFiltersByType();
    void deleteRemovesFunction();
    void renameChangesName();
    void moveChangesPath();
    void updateChangesGenericProperties();

    void sceneSetValuesReplacesValueList();
    void sceneSetValueAndUnsetValueEmitSinglePatchOps();
    void sceneSetMembersReplacesFixtureList();

    void chaserStepsAddReplaceRemoveMove();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();

    /** functions.create helper for the new-method tests below - returns the
     *  new function's id (as a string, matching the wire convention) or an
     *  empty string on failure. */
    QString createFunctionViaApi(const QString &type, const QJsonObject &extraParams = QJsonObject());

private:
    Doc *m_doc;
    Scene *m_scene;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
