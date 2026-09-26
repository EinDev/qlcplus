/*
  Q Light Controller Plus - Control API unit test
  fakevchost.h

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

#ifndef FAKEVCHOST_H
#define FAKEVCHOST_H

#include <QHash>
#include <QObject>
#include <QRectF>
#include <QVector>

#include "apivchost.h"

/**
 * Headless stand-in for qmlui's App, used only by this test binary (controlapi must build and run
 * without qmlui - see apivchost.h). Reimplements ApiVcHost with a private in-memory model of pages
 * and widgets that mimics VCWidget/VCPage's field semantics closely enough to exercise ApiVcDomain's
 * request-shape validation end-to-end, without a real QQuickView/VirtualConsole object graph.
 *
 * This is a QObject (not just an ApiVcHost) so a test's ApiServer can be constructed with this as its
 * parent - ApiVcDomain::vcHost() finds it via dynamic_cast<ApiVcHost*>(m_server->parent()), exactly
 * how App is found in production.
 */
class FakeVcHost : public QObject, public ApiVcHost
{
    Q_OBJECT

public:
    explicit FakeVcHost(QObject *parent = nullptr);

    // --- Pages ---
    int vcPageCount() const override;
    QJsonObject vcPageSnapshot(int index) const override;
    int vcSelectedPage() const override;
    void vcSetSelectedPage(int index) override;
    void vcAddPage(int index) override;
    bool vcDeletePage(int index, QJsonArray &deletedWidgetIds) override;
    void vcRenamePage(int index, const QString &name) override;
    bool vcSetPagePin(int index, const QString &currentPin, const QString &newPin) override;
    bool vcValidatePagePin(int index, const QString &pin) const override;

    // --- Widgets: queries ---
    bool vcWidgetExists(quint32 id) const override;
    QString vcWidgetType(quint32 id) const override;
    int vcWidgetPage(quint32 id) const override;
    quint32 vcWidgetParentId(quint32 id) const override;
    bool vcIsContainerWidget(quint32 id) const override;
    QList<quint32> vcWidgetIds() const override;
    QJsonObject vcWidgetSnapshot(quint32 id) const override;

    // --- Widgets: mutations ---
    quint32 vcCreateWidget(const QString &widgetType, int page, quint32 parentId,
                            const QJsonObject &geometry, const QJsonObject &style,
                            const QJsonObject &typeConfig, QString *error) override;
    void vcDeleteWidgets(const QList<quint32> &ids, QJsonArray &deletedIds) override;
    bool vcUpdateWidgetCommon(quint32 id, const QJsonObject &fields, QString *error) override;
    void vcMoveTopLevelWidgetToPage(quint32 id, int newPage) override;
    bool vcSetWidgetConfig(quint32 id, const QJsonObject &configPatch, QString *error) override;
    bool vcReparentWidget(quint32 id, quint32 newParentId, QPointF newTopLeft, QString *error) override;
    void vcRepositionWidgets(const QList<QPair<quint32, QJsonObject> > &updates) override;

    // --- Widgets: live interaction ---
    void vcSetLiveListener(ApiVcLiveListener *listener) override;
    bool vcButtonPress(quint32 id, bool pressed, QString *error) override;
    bool vcSliderSetValue(quint32 id, int value, QString *error) override;
    bool vcCueListAction(quint32 id, CueListAction action, QString *error) override;
    bool vcCueListSetPlaybackIndex(quint32 id, int index, QString *error) override;
    QJsonObject vcCueListSnapshot(quint32 id) const override;
    bool vcXyPadSetPosition(quint32 id, double x, double y, QString *error) override;
    bool vcSpeedDialSetValue(quint32 id, int ms, QString *error) override;
    bool vcSpeedDialTap(quint32 id, QString *error) override;
    bool vcFrameGotoPage(quint32 id, int page, QString *error) override;
    QJsonObject vcFrameSnapshot(quint32 id) const override;

    /** Every fake CueList pretends its Chaser has exactly this many steps (named "Step 1".."Step N"),
     *  so index-range validation in the domain has something real to check against. */
    static const int FakeCueListStepCount;

    // --- Test hooks: engine-side changes nobody requested over the API ---
    // Mimic what the real host does when the QML UI / external input / a Function stopping changes a
    // widget's live state: update the model and notify the listener. The domain must broadcast these
    // with a null originClientId.
    void simulateButtonState(quint32 id, const QString &state);
    void simulateSliderValue(quint32 id, int value);
    void simulateCueListAdvance(quint32 id, int playbackIndex);

private:
    struct VcPageState
    {
        QString name;
        QString pin; // empty = no PIN set
    };

    struct VcWidgetState
    {
        quint32 id = 0;
        QString widgetType;
        int page = 0; // invariant: always equal to the top-level ancestor's page
        quint32 parentId = ApiVcHost::InvalidWidgetId;
        QRectF geometry;
        int zIndex = 0;
        bool allowResize = true;
        bool isDisabled = false;
        bool isVisible = true;
        QString caption;
        QString backgroundColor;
        QString backgroundImage;
        QString foregroundColor;
        QJsonObject font;
        QJsonObject typeConfig;

        // Live (§4b) state, per widget type - only the fields matching widgetType are meaningful.
        QString buttonState = QStringLiteral("inactive"); // Button: "inactive"|"active"|"monitoring"
        int sliderValue = 0;                              // Slider
        int playbackIndex = -1;                           // CueList
        bool running = false;                             // CueList
        bool paused = false;                              // CueList
        double x = 0.0;                                   // XYPad, normalized 0..1
        double y = 0.0;                                   // XYPad, normalized 0..1
        int speedMs = 0;                                  // Speed
        qint64 lastTapMs = 0;                             // Speed: tap-tempo bookkeeping
        int currentPage = 0;                              // Frame/SoloFrame
    };

    static const QStringList ContainerWidgetTypes; // Frame, SoloFrame

    static QRectF geometryFromJson(const QJsonObject &geom);
    static QJsonObject geometryToJson(const QRectF &geom);
    QJsonObject styleToJson(const VcWidgetState &w) const;
    void applyStyleFromJson(VcWidgetState &w, const QJsonObject &style) const;
    QJsonObject widgetSummaryToJson(const VcWidgetState &w) const;
    QJsonObject widgetDetailToJson(const VcWidgetState &w) const;

    /** Every descendant of $id (children, grandchildren, ...), NOT including $id itself. */
    QList<quint32> collectDescendants(quint32 id) const;
    void setWidgetPageRecursive(quint32 id, int newPage);

    /** Adds the widgetType-specific live fields (state/value/playbackIndex/x/y/ms/currentPage/...)
     *  that vc.widget.get/list expose so a UI can seed itself. */
    void appendLiveStateToJson(const VcWidgetState &w, QJsonObject &obj) const;
    void notifyCueListPlayback(const VcWidgetState &w) const;
    static int frameTotalPages(const VcWidgetState &w);

    QVector<VcPageState> m_pages;
    int m_selectedPage;

    QHash<quint32, VcWidgetState> m_widgets;
    quint32 m_nextWidgetId;

    ApiVcLiveListener *m_liveListener;
};

#endif
