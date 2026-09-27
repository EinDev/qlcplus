/*
  Q Light Controller Plus - Control API unit test
  apiioconfigdomain_test.h

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

#ifndef APIIOCONFIGDOMAIN_TEST_H
#define APIIOCONFIGDOMAIN_TEST_H

#include <QObject>
#include <QList>
#include <QJsonObject>

class QSignalSpy;
class QTemporaryDir;
class QWebSocket;

class Doc;
class ApiServer;
class IOPluginStub;

/**
 * End-to-end test of ApiIoConfigDomain (plus io.simpleDesk.sendKeypadCommand,
 * which lives in ApiIoDomain): a real ApiServer on an ephemeral localhost
 * port, driven by real QWebSocket clients, with engine/test/iopluginstub as
 * the patchable plugin. Input profiles are written into a QTemporaryDir via
 * QLCPLUS_USER_INPUTPROFILE_DIR so the developer's real profile folder is
 * never touched; QSettings go to a test-specific organization/application.
 */
class ApiIoConfigDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void pluginGetLinesDescribesStubLines();
    void pluginGetLinesOnUnknownPluginIsNotFound();
    void pluginRescanInvokesStubAndBroadcastsLinesChanged();
    void pluginRescanOnUnknownPluginIsNotFound();
    void pluginConfigureWithoutDialogIsUnsupported();
    void pluginConfigureCallsThroughOnGuiHost();

    void patchSetParametersStoresAndBroadcastsUniverseUpdated();
    void patchSetParametersNullUnsetsKey();
    void patchSetParametersOnUnpatchedUniverseIsNotFound();
    void patchSetParametersWithStaleRevisionConflicts();
    void patchOutputSetStatePausesAndBroadcasts();
    void patchOutputSetStateWithoutFieldsIsInvalidParams();

    void inputProfileSaveWritesFileAndBumpsRevision();
    void inputProfileSaveOverExistingUpdatesInPlace();
    void inputProfileSaveWithStaleRevisionConflicts();
    void inputProfileSaveWithoutModelIsInvalidParams();
    void inputProfileGetOnUnknownIsNotFound();
    void inputProfileDeleteRemovesFileAndClearsPatches();
    void inputProfileDeleteOnUnknownIsNotFound();

    void learnSignalGoesOnlyToRequester();
    void learnStartWithoutInputPatchIsInvalidState();
    void learnStopByOtherClientIsInvalidState();

    void grandMasterSetModeBroadcastsChanged();
    void grandMasterSetModeWithBadValueIsInvalidParams();
    void universeSetMonitorBroadcasts();

    void audioListDevicesStartsWithDefault();
    void audioSetDeviceUnknownIsNotFound();
    void audioSetDefaultDeviceRoundTrips();
    void audioSetConfigWritesSettingsAndBroadcasts();
    void audioInputPreviewValidatesAndStops();

    void keypadCommandSetsChannelsAndHistory();
    void keypadCommandEmptyIsInvalidParams();

private:
    QJsonObject sendAndWaitForReply(QWebSocket *client, const QString &method, const QJsonObject &params);
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params);
    QString hello(QWebSocket *client);
    QWebSocket *connectSecondClient();

    /** Load engine/test/iopluginstub's plugin into m_doc's cache and return it. */
    IOPluginStub *loadStubPlugin();
    /** stub->name() through the plugin vtable (see loadStubPlugin()). */
    QString stubName(IOPluginStub *stub) const;

    /** Patch the stub's output line 0 (universe 0) / input line 0 (universe 0). */
    void patchStubOutput(IOPluginStub *stub, quint32 universeId = 0, quint32 line = 0);
    void patchStubInput(IOPluginStub *stub, quint32 universeId = 0, quint32 line = 0, const QString &profile = QString());

    QList<QJsonObject> eventsWithTopic(QSignalSpy &spy, const QString &topic);
    QJsonObject sampleProfile(const QString &model, const QString &channelName);

private:
    Doc *m_doc;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
    QList<QWebSocket *> m_extraClients;
    QTemporaryDir *m_profileDir;
};

#endif
