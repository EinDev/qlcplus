/*
  Q Light Controller Plus - Control API unit test
  apifixturesdomain_test.h

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

#ifndef APIFIXTURESDOMAIN_TEST_H
#define APIFIXTURESDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>

class Doc;
class ApiServer;
class QWebSocket;

/**
 * End-to-end test: a real ApiServer listening on an ephemeral localhost
 * port, driven by a real QWebSocket client - same pattern as
 * controlapi/test/apiiodomain/apiiodomain_test.cpp.
 */
class ApiFixturesDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void patchGenericCreatesFixtureAndBumpsRevision();
    void patchGenericBroadcastsPatchedEvent();
    void patchWithStaleRevisionConflicts();
    void patchRejectsBothGenericAndNamedDefinition();
    void patchRejectsNeitherGenericNorNamedDefinition();
    void patchRejectsOverlappingAddress();
    void patchBulkQuantityAssignsSequentialAddresses();
    void patchNamedDefinitionUsesRealFixtureDef();
    void patchWithUnknownDefinitionIsNotFound();

    void listReturnsPatchedFixtures();
    void listFiltersByUniverse();
    void getReturnsFixtureDetailWithChannelList();
    void getOnMissingFixtureIsNotFound();

    void updateRenameBroadcastsUpdatedEvent();
    void updateMoveAddressRejectsOverlap();
    void updateMoveToOtherUniverseIgnoresOldUniverseOccupant();
    void updateWithNoFieldsIsInvalidParams();
    void updateWithStaleRevisionConflicts();

    void unpatchDeletesFixtureAndBumpsRevision();
    void unpatchBroadcastsUnpatchedEvent();
    void unpatchOnMissingFixtureIsNotFound();

    void findAvailableAddressReturnsRequestedWhenFree();
    void findAvailableAddressScansWhenRequestedTaken();
    void hugeAddressesAndCountsAreRejectedNotOverflowed();

    void defsListManufacturersIncludesRegisteredDefinition();
    void defsListModelsReturnsNamesAndDetails();
    void defsListModelsUnknownManufacturerIsNotFound();
    void defsGetModelReturnsModesWithChannels();
    void defsGetModelUnknownIsNotFound();
    void defsGetModeReturnsChannelDetail();
    void patchAcceptsFlatManufacturerModelMode();

private:
    /** Register a synthetic "Acme" / "TestPar" definition (Dimmer, one
     *  "2-channel" mode: Intensity MSB + Colour) in m_doc's definition
     *  cache, the way patchNamedDefinitionUsesRealFixtureDef() does inline. */
    void addAcmeTestParDefinition();

    /** Send a request and scan every frame received so far for the matching
     *  "response" (a mutation's response/event can arrive in either order -
     *  see apiiodomain_test.cpp's own sendAndWaitForReply() for why). */
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();

    /** Convenience: patch one generic dimmer and return its fixtureId (as
     *  the raw quint32, not the wire string) via a real fixtures.patch
     *  round-trip. */
    quint32 patchGenericFixture(int universeId, int address, int channels = 2,
                                 const QString &name = QStringLiteral("Test Fixture"));

private:
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
