/*
  Q Light Controller Plus - Control API unit test
  apifixturegroupdomain_test.h

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

#ifndef APIFIXTUREGROUPDOMAIN_TEST_H
#define APIFIXTUREGROUPDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>

class Doc;
class ApiServer;
class QWebSocket;
class Fixture;

/**
 * End-to-end test for ApiFixtureGroupDomain: a real ApiServer listening on
 * an ephemeral localhost port, driven by a real QWebSocket client - same
 * pattern as controlapi/test/apiiodomain/apiiodomain_test.cpp.
 */
class ApiFixtureGroupDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listIsEmptyInFreshDoc();
    void getOnMissingGroupIsNotFound();

    void createBumpsRevisionAndBroadcastsCreated();
    void createWithStaleRevisionConflicts();
    void createRequiresName();

    void renameBroadcastsRenamedEventNotUpdated();
    void renameToSameNameIsNoopButStillOk();

    void deleteRemovesGroupAndBroadcastsDeleted();

    void setSizeUpdatesSizeAndBroadcastsUpdated();
    void setSizeRejectsZeroColumns();

    void assignFixtureAutoPlacesAllHeads();
    void assignFixtureWithUnknownFixtureIsNotFound();

    void assignHeadAutoPlacesNewHead();
    void assignHeadToOccupiedCellSwapsPositions();
    void assignHeadWithBadHeadIndexIsInvalidParams();

    void unassignHeadClearsCell();
    void unassignHeadOnEmptyCellIsNoopButStillOk();

    void unassignFixtureRemovesAllItsHeads();

    void swapHeadsExchangesPositions();

    void resetClearsAllHeads();

private:
    /** Send a request and wait for exactly one more text message to arrive
     *  on client, returning it parsed as a JSON object. */
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();
    int currentDocRevision();

    /** Create a single-head-per-channel generic fixture with the given
     *  channel count (== head count, per Fixture::heads()'s implementation
     *  for a plain generic-dimmer mode) patched at the next free address,
     *  and return its (auto-assigned) fixture id. */
    quint32 addGenericFixture(int channels);

private:
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
    quint32 m_nextAddress;
};

#endif
