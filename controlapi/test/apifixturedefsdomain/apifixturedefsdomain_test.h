/*
  Q Light Controller Plus - Control API unit test
  apifixturedefsdomain_test.h

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

#ifndef APIFIXTUREDEFSDOMAIN_TEST_H
#define APIFIXTUREDEFSDOMAIN_TEST_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTemporaryDir>

class Doc;
class ApiServer;
class QWebSocket;

/**
 * End-to-end test of the fixturedefs.* domain: a real ApiServer on an
 * ephemeral localhost port driven by a real QWebSocket (same shape as
 * controlapi/test/apipalettedomain). The user fixture directory is redirected
 * to a QTemporaryDir through QLCFixtureDefCache::setUserDefinitionDirectoryOverride()
 * so no test ever touches the real user profile; a "system" (isUser=false)
 * definition is seeded straight into the Doc's cache in memory.
 */
class ApiFixtureDefsDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listIncludesSeededDefinition();
    void getReturnsDefinitionWithIds();
    void getMissingIsNotFound();
    void sessionCreateIsBlankUserSession();
    void sessionOpenClonesLibraryDefinition();
    void sessionListAndClose();
    void sessionGetReturnsSnapshot();
    void sessionUpdateBumpsRevisionAndConflicts();
    void sessionSetPhysicalMergesPartially();
    void channelAddUpdateRemove();
    void channelRemoveCascadesToModesAndAliases();
    void capabilityAddUpdateRemove();
    void capabilityWizardCreatesRangesAndRejectsOverlap();
    void channelWizardCreatesCompoundChannels();
    void autoPatchColorsDetectsNamedColors();
    void aliasAddUpdateRemoveApplyToAllModes();
    void modeAddRenameSetChannelsRemove();
    void modeSetChannelsRejectsActsOnSelf();
    void modeSetPhysicalOverrideAndReset();
    void headsSurviveChannelReorder();
    void saveOnSystemSessionIsReadOnlyUntilForked();
    void deleteSystemIsReadOnly();
    void deleteInUseIsRejected();
    void deleteUserCopyRestoresBundledDefinition();
    void importCreatesUserSession();
    void fullRoundTrip();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId);
    QJsonObject call(const QString &method, const QJsonObject &params);
    QJsonObject callOk(const QString &method, const QJsonObject &params);
    QString callError(const QString &method, const QJsonObject &params, QJsonObject *details = nullptr);
    QString hello();
    /** Wait until an event with this topic has been received on m_client
     *  since the spy was armed; returns its data (empty object on timeout). */
    QJsonObject waitForEvent(class QSignalSpy &spy, const QString &topic);

    QJsonObject openSession(const QString &manufacturer, const QString &model);
    QJsonObject createSession();
    /** The definition snapshot carried by the most recent
     *  fixturedefs.session.updated event recorded by spy - the way a real
     *  client is meant to learn the post-mutation state (00-conventions §3). */
    QJsonObject lastUpdatedDefinition(class QSignalSpy &spy);
    QJsonObject params(const QString &sessionId, int baseRevision);

private:
    QTemporaryDir *m_userDir;
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
