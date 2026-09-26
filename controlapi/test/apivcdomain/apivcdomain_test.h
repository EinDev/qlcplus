/*
  Q Light Controller Plus - Control API unit test
  apivcdomain_test.h

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

#ifndef APIVCDOMAIN_TEST_H
#define APIVCDOMAIN_TEST_H

#include <QObject>
#include <QJsonObject>

class Doc;
class ApiServer;
class FakeVcHost;
class QWebSocket;

/**
 * End-to-end test for ApiVcDomain, same shape as controlapi/test/apiiodomain: a real ApiServer
 * listening on an ephemeral localhost port, driven by a real QWebSocket client.
 */
class ApiVcDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void pageListReturnsSingleDefaultPage();
    void pageCreateBumpsRevisionAndBroadcasts();
    void pageCreateWithStaleRevisionConflicts();
    void pageCreateShiftsWidgetsAndSelection();
    void pageDeleteRefusesLastPage();
    void pageDeleteRenumbersWidgetsOnLaterPages();
    void pageRenameBroadcastsEvent();
    void pageSetPinRequiresCurrentPin();
    void pageValidatePinChecksValue();
    void pageSelectDoesNotBumpDocRevision();

    void widgetCreateAndGet();
    void widgetCreateRejectsUnknownType();
    void widgetUpdateRequiresAtLeastOneField();
    void widgetUpdateAppliesGeometry();
    void widgetUpdateWithInvalidPageAppliesNothing();
    void widgetUpdateRejectsPageChangeOnNestedWidget();
    void widgetSetConfigMergesPatch();
    void widgetDeleteRecursivelyDeletesChildren();
    void widgetReparentMovesWidgetAndAdoptsPage();
    void widgetReparentRejectsCycle();
    void widgetRepositionBulkUpdatesAllOrNothing();

    // Live interaction (vc.button/slider/cueList/xyPad/speedDial/frame) - see the live-interaction
    // messages in docs/api-spec/fragments/virtualconsole.yaml.
    void liveButtonPressTogglesOnDownEdgeAndBroadcasts();
    void liveButtonPressFlashFollowsBothEdges();
    void liveButtonPressRejectsUnknownWidgetWrongTypeAndBadParams();
    void liveButtonPressRefusesDisabledWidgetAndMissingFunction();
    void liveSliderSetValueBroadcastsAndValidates();
    void liveCueListTransportAndGet();
    void liveCueListSetPlaybackIndexValidatesRange();
    void liveXyPadSetPositionBroadcastsAndValidates();
    void liveSpeedDialSetValueAndTap();
    void liveFrameGotoPageAndGet();
    void liveWidgetSnapshotsExposeLiveState();
    void liveEngineDrivenChangeBroadcastsWithNullOrigin();
    void liveMethodsDoNotBumpDocRevision();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId = QStringLiteral("t-1"));
    QString helloAndGetClientId();
    int currentDocRevision();

    /** vc.widget.create shortcut for the live tests: a 1x1 widget of $widgetType on page 0 with
     *  $typeConfig, returning its wire id (empty on failure). Reads the live docRevision itself. */
    QString createWidget(const QString &widgetType, const QJsonObject &typeConfig = QJsonObject());

    /** {"widgetId": $widgetId} plus $extra. */
    static QJsonObject widgetParams(const QString &widgetId, const QJsonObject &extra = QJsonObject());

private:
    Doc *m_doc;
    FakeVcHost *m_vcHost;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
