/*
  Q Light Controller Plus - Control API unit test
  apipalettedomain_test.h

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

#ifndef APIPALETTEDOMAIN_TEST_H
#define APIPALETTEDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>

#include <QJsonArray>

class Doc;
class ApiServer;
class QWebSocket;
class QLCFixtureDef;
class QLCPalette;

/**
 * End-to-end test: a real ApiServer listening on an ephemeral localhost
 * port, driven by a real QWebSocket client - same shape as
 * controlapi/test/apiiodomain/apiiodomain_test.h.
 */
class ApiPaletteDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createAddsColorPalette();
    void createPanTiltWithOneValueSavesSafely();
    void createWithStaleRevisionConflicts();
    void createWithUnknownTypeIsInvalidParams();
    void getReturnsFullDetail();
    void getMissingPaletteIsNotFound();
    void listReturnsSummaries();
    void updateRenamesAndBroadcasts();
    void updateWithNoActualChangeDoesNotBumpRevision();
    void updateMissingPaletteIsNotFound();
    void deleteRemovesPaletteAndBroadcasts();
    void deleteWithStaleRevisionConflicts();

    void applyEveryTypeMatchesEngine_data();
    void applyEveryTypeMatchesEngine();
    void applyFansDimmerAcrossFixtures();
    void applyWritesGoboWheel();
    void applyValidatesParams();
    void applyOverridesAreReleasable();

private:
    /** Adds a fixture of @def at @address (monitor position @posMm) to m_doc */
    quint32 addFixture(QLCFixtureDef *def, quint32 address, float xMm = 0);
    /** Adds @palette to m_doc and applies it over the API to @ids */
    QJsonObject applyPalette(QLCPalette *palette, const QJsonArray &ids);
    /** The reply's channels must equal QLCPalette::valuesFromFixtures() */
    void verifyMatchesEngine(const QJsonObject &reply, QLCPalette *palette, const QList<quint32> &ids);

    /** Send a request and wait for exactly one more text message to arrive
     *  on client, returning it parsed as a JSON object. */
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId = QStringLiteral("t-1"));
    QString helloAndGetClientId();
    int currentDocRevision();

private:
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
    QLCFixtureDef *m_moverDef;
    QLCFixtureDef *m_barDef;
};

#endif
