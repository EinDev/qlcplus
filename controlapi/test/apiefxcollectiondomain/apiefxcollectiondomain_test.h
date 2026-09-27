/*
  Q Light Controller Plus - Control API unit test
  apiefxcollectiondomain_test.h

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

#ifndef APIEFXCOLLECTIONDOMAIN_TEST_H
#define APIEFXCOLLECTIONDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>
#include <functional>

class QSignalSpy;
class Doc;
class Scene;
class Collection;
class EFX;
class Fixture;
class ApiServer;
class QWebSocket;

/**
 * End-to-end test of functions.collection.* / functions.efx.*: a real
 * ApiServer on an ephemeral localhost port driven by a real QWebSocket.
 * MasterTimer is never started - nothing here needs a running function, and
 * a bare Doc keeps every assertion synchronous.
 */
class ApiEfxCollectionDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void collectionTypeDetailListsMembers();
    void collectionAddFunctionAppendsInsertsAndBroadcasts();
    void collectionAddRejectsSelfDuplicateLoopAndStaleRevision();
    void collectionAddWhileRunningThenAdjustIntensity();
    void collectionRemoveFunction();
    void collectionSetMembersReplacesInOrder();

    void efxTypeDetailCarriesParametersAlgorithmsAndFixtures();
    void efxSetParametersAppliesClampsAndBroadcasts();
    void efxSetParametersRejectsBadEnumWithoutApplying();
    void efxFixturesAddRemoveReorderAndParameters();
    void efxAddFixtureAllHeads();
    void efxSetFixturesOffset();
    void efxGetPreviewMatchesEditorMath();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();
    QJsonObject waitForEvent(QSignalSpy &spy, const QString &topic,
                             const std::function<bool(const QJsonObject &)> &accept, int timeoutMs = 3000);
    /** params + the current docRevision as baseRevision */
    QJsonObject withRevision(QJsonObject params) const;
    QString errorCode(const QJsonObject &reply) const;
    /** A generic dimmer (one head per channel) added to the Doc */
    Fixture *addDimmer(const QString &name, quint32 channels);

private:
    Doc *m_doc;
    Scene *m_sceneA;
    Scene *m_sceneB;
    Collection *m_collection;
    EFX *m_efx;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
