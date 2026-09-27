/*
  Q Light Controller Plus - Control API unit test
  apivclivedomain_test.h

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

#ifndef APIVCLIVEDOMAIN_TEST_H
#define APIVCLIVEDOMAIN_TEST_H

#include <QJsonObject>
#include <QObject>

class Doc;
class ApiServer;
class FakeVcHost;
class QWebSocket;

/**
 * End-to-end test for ApiVcLiveDomain (XY Pad fixtures / presets / floor, Clock, Animation, Audio
 * Triggers), same shape as controlapi/test/apivclayoutdomain: a real ApiServer on an ephemeral
 * localhost port driven by a real QWebSocket, the headless FakeVcHost standing in for qmlui's App and
 * a real Doc (so fixture / group / function validation runs against real engine objects).
 */
class ApiVcLiveDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void xyPadFixtureAddRemoveAndRange();
    void xyPadFixtureAddValidates();
    void xyPadGroupAddCreatesPresetAndPresetsMoveRename();
    void xyPadSetFloorPositionBroadcasts();
    void clockPlayPauseResetAndGatedTimeChanged();
    void clockSchedulesAddUpdateRemove();
    void animationFaderLevelAndKnob();
    void animationPresetMove();
    void audioTriggersCaptureAndLevels();
    void audioTriggersSetBarConfig();
    void wrongWidgetTypeIsInvalidParams();
    void structuralMethodsConflictOnStaleRevision();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId = QStringLiteral("t-1"));
    QString helloAndGetClientId();
    int currentDocRevision();

    /** vc.widget.create shortcut: $widgetType on page 0 with $typeConfig, returning its wire id. */
    QString createWidget(const QString &widgetType, const QJsonObject &typeConfig = QJsonObject());

    /** vc.widget.get's typeConfig of $widgetId. */
    QJsonObject typeConfigOf(const QString &widgetId);

    /** A structural call with the current baseRevision folded in. */
    QJsonObject structural(const QString &method, const QString &widgetId, const QJsonObject &extra, const QString &requestId = QStringLiteral("t-s"));

    /** Adds $count 4-channel generic fixtures to m_doc, returning their ids. */
    QList<quint32> addFixtures(int count);

private:
    Doc *m_doc;
    FakeVcHost *m_vcHost;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
