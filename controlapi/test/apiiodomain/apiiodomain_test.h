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
#include <QList>
#include <QJsonObject>

class QSignalSpy;

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

    void simpleDeskSetChannelIsReflectedInGet();
    void simpleDeskSetChannelBroadcastsOverriddenTrue();
    void simpleDeskSetChannelsIsReflectedInGetAndDmxUniverse();
    void simpleDeskSetChannelsBroadcastsOneEventPerEntry();
    void simpleDeskSetChannelsRejectsMalformedEntryWithoutPartialApply();
    void simpleDeskResetChannelBroadcastsOverriddenFalse();
    void simpleDeskResetUniverseClearsHeldValues();
    void simpleDeskSetUniverseFilterBroadcastsEvent();
    void simpleDeskGetOnMissingUniverseIsNotFound();

    void simpleDeskDumpCreatesNewSceneAndBumpsRevision();
    void simpleDeskDumpWithStaleRevisionConflicts();
    void simpleDeskDumpBroadcastsFunctionsCreatedEvent();
    void simpleDeskDumpMergeIntoExistingSceneBroadcastsFunctionsUpdated();
    void simpleDeskDumpOnMissingTargetSceneIsNotFound();
    void simpleDeskDumpFixtureIdsLimitsToThoseFixtures();
    void simpleDeskDumpUnknownFixtureIdIsNotFound();
    void simpleDeskOverrideOnUniverse1FixtureHitsItsChannel();
    void simpleDeskOverrideSurvivesProjectUniverseReload();

    void pluginListDescribesStubPluginLines();
    void patchSetOutputBumpsRevisionAndBroadcastsUniverseUpdated();
    void patchSetInputWithProfileThenRemoveInput();
    void patchSetUnknownPluginIsNotFound();
    void patchSetFeedbackOnPluginWithoutFeedbackIsUnsupported();
    void patchRemoveWhenNothingPatchedIsNotFound();
    void universeUpdateRenamesAndSetsPassthrough();
    void universeUpdateWithNoFieldsIsInvalidParams();
    void universeUpdateWithStaleRevisionConflicts();
    void universeDeleteRemovesTrailingUniverseAndBroadcasts();
    void universeDeleteNonTrailingIsInvalidParams();
    void universeDeleteWithPatchedFixturesRequiresForce();
    void universeDeleteLastUniverseIsInvalidState();
    void inputProfileListReturnsLoadedProfiles();

private:
    /** Send a request and wait for exactly one more text message to arrive
     *  on client, returning it parsed as a JSON object. */
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString helloAndGetClientId();

    /** Load engine/test/iopluginstub's I/O plugin (4 input + 4 output
     *  lines, Input|Output capabilities, no Feedback) into m_doc's plugin
     *  cache - the only way a bare Doc gets a patchable plugin. Returns its
     *  name (empty if the stub DLL wasn't found next to this build). */
    QString loadStubPlugin();

    /** Every event frame with the given topic received by spy so far */
    QList<QJsonObject> eventsWithTopic(QSignalSpy &spy, const QString &topic);

private:
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
