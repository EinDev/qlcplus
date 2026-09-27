/*
  Q Light Controller Plus - Control API unit test
  apirgbmatrixdomain_test.h

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

#ifndef APIRGBMATRIXDOMAIN_TEST_H
#define APIRGBMATRIXDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>
#include <functional>

class QSignalSpy;
class Doc;
class RGBMatrix;
class FixtureGroup;
class ApiServer;
class QWebSocket;

/**
 * End-to-end test of the functions.rgbmatrix.* methods: a real ApiServer on
 * an ephemeral localhost port, a real QWebSocket client, a Doc whose
 * RGBScriptsCache is loaded from the source tree's resources/rgbscripts/
 * (RGBSCRIPTS_DIR, a compile definition from this test's CMakeLists.txt, so
 * the binary can be run from any working directory), one 4x2 fixture group
 * and one RGBMatrix bound to it. MasterTimer is not started: nothing here
 * needs a function to actually run.
 */
class ApiRgbMatrixDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listAlgorithmsHasBuiltinsAndScripts();
    void getScriptPropertiesReturnsDefinitions();
    void getScriptPropertiesOnUnknownScriptIsNotFound();
    void getReturnsRgbMatrixTypeDetail();
    void setConfigAppliesEverythingAndBroadcasts();
    void setConfigAppliesPartialConfig();
    void setConfigOnStaleRevisionIsConflict();
    void setConfigWithUnknownScriptIsInvalidParams();
    void setConfigWithUnknownGroupIsNotFound();
    void setConfigTextAlgorithmRoundTrips();
    void setScriptPropertyChangesValueAndBroadcasts();
    void setScriptPropertyOnUnknownPropertyIsInvalidParams();
    void setScriptPropertyOnBuiltinAlgorithmIsInvalidParams();
    void getPreviewRendersPlainColour();
    void getPreviewWrapsStepAndInterpolatesColour();
    void getPreviewWithoutGroupIsEmpty();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();

    /** Wait (up to timeoutMs) for an event frame with the given topic whose
     *  data satisfies accept(); returns that event, or an empty object. */
    QJsonObject waitForEvent(QSignalSpy &spy, const QString &topic,
                             const std::function<bool(const QJsonObject &)> &accept, int timeoutMs = 3000);

    /** functions.get for m_matrix, returning result.typeDetail.config */
    QJsonObject getConfig();

private:
    Doc *m_doc;
    FixtureGroup *m_group;
    RGBMatrix *m_matrix;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
