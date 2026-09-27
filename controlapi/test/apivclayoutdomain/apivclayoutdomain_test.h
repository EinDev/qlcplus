/*
  Q Light Controller Plus - Control API unit test
  apivclayoutdomain_test.h

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

#ifndef APIVCLAYOUTDOMAIN_TEST_H
#define APIVCLAYOUTDOMAIN_TEST_H

#include <QJsonObject>
#include <QObject>

class Doc;
class ApiServer;
class FakeVcHost;
class QWebSocket;

/**
 * End-to-end test for ApiVcLayoutDomain, same shape as controlapi/test/apivcdomain: a real
 * ApiServer on an ephemeral localhost port driven by a real QWebSocket, with the headless
 * FakeVcHost (shared with that suite) standing in for qmlui's App. The Doc is real, so the
 * function / fixture validation the domain does itself runs against real engine objects.
 */
class ApiVcLayoutDomain_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void pageSetPinBroadcastsPageUpdated();
    void frameSetPinRequiresCurrentPinAndBroadcasts();
    void frameValidatePinChecksValue();
    void frameCloneFirstPageCreatesCopies();
    void sliderSetLevelChannelsValidatesFixturesAndChannels();
    void sliderFlashRequiresAdjustModeWithFlashButton();
    void widgetAlignMovesToReference();
    void widgetAlignRejectsMixedParentsAndBadAlignment();
    void widgetDistributeSpreadsEvenly();
    void widgetBulkStyleAppliesToEveryWidget();
    void createFromFunctionsCreatesOnePerFunction();
    void createFromFunctionsValidates();
    void createMatrixCreatesContainerAndCells();
    void createMatrixValidates();
    void usageListsReferencingWidgets();
    void structuralMethodsConflictOnStaleRevision();

private:
    QJsonObject sendAndWaitForReply(const QString &method, const QJsonObject &params, const QString &requestId = QStringLiteral("t-1"));
    QString helloAndGetClientId();
    int currentDocRevision();

    /** vc.widget.create shortcut: $widgetType at $geometry on page 0 under $parentId (empty = page
     *  root) with $typeConfig, returning its wire id (empty on failure). */
    QString createWidget(const QString &widgetType, const QJsonObject &typeConfig = QJsonObject(),
                         const QJsonObject &geometry = QJsonObject(), const QString &parentId = QString());

    /** vc.widget.get's geometry of $widgetId. */
    QJsonObject geometryOf(const QString &widgetId);

    static QJsonObject geometry(double x, double y, double width, double height);
    static QJsonObject widgetParams(const QString &widgetId, const QJsonObject &extra = QJsonObject());

private:
    Doc *m_doc;
    FakeVcHost *m_vcHost;
    ApiServer *m_apiServer;
    QWebSocket *m_client;
};

#endif
