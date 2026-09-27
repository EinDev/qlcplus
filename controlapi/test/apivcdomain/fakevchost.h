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
    void vcSetPageSize(int index, int width, int height) override;

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

    // --- Cue list side fader / speed dial extras / widget presets ---
    bool vcCueListSetSideFaderLevel(quint32 id, int level, QString *error) override;
    bool vcSpeedDialSetFactor(quint32 id, const QString &factor, QString *error) override;
    bool vcSpeedDialApply(quint32 id, QString *error) override;
    bool vcSpeedDialResetTap(quint32 id, QString *error) override;
    bool vcWidgetSupportsPresets(quint32 id) const override;
    QJsonArray vcWidgetPresets(quint32 id) const override;
    int vcWidgetPresetAdd(quint32 id, const QJsonObject &preset, QString *error) override;
    bool vcWidgetPresetRemove(quint32 id, int presetId, QString *error) override;
    bool vcWidgetPresetApply(quint32 id, int presetId, QString *error) override;
    bool vcSpeedDialPresetUpdate(quint32 id, int presetId, const QJsonObject &patch, QString *error) override;
    // --- Widgets: layout / configuration slice (ApiVcLayoutDomain) ---
    bool vcFrameSetPin(quint32 id, const QString &currentPin, const QString &newPin) override;
    bool vcFrameValidatePin(quint32 id, const QString &pin) const override;
    bool vcFrameCloneFirstPage(quint32 id, QJsonArray &createdIds, QString *error) override;
    bool vcSliderSetLevelChannels(quint32 id, const QList<QPair<quint32, quint32> > &channels, QString *error) override;
    bool vcSliderFlash(quint32 id, bool on, QString *error) override;
    QList<quint32> vcCreateWidgetsFromFunctions(int page, quint32 parentId, const QList<quint32> &functionIds,
                                                QPointF position, const QString &widgetHint, QString *error) override;
    QList<quint32> vcCreateWidgetMatrix(int page, quint32 parentId, const QString &matrixType, QPointF position,
                                        int columns, int rows, int widgetWidth, int widgetHeight,
                                        bool soloFrame, QString *error) override;
    QList<quint32> vcWidgetsUsingFunction(quint32 functionId) const override;

    // --- External controls slice (ApiVcInputDomain) ---
    QJsonArray vcWidgetExternalControls(quint32 id) const override;
    QJsonArray vcWidgetInputSources(quint32 id) const override;
    QJsonArray vcWidgetKeySequences(quint32 id) const override;
    bool vcWidgetInputSourceSet(quint32 id, quint32 controlId, quint32 universe, quint32 channel,
                                const QJsonObject &feedback, QString *error) override;
    bool vcWidgetInputSourceRemove(quint32 id, quint32 controlId, quint32 universe, quint32 channel, QString *error) override;
    bool vcWidgetKeySequenceSet(quint32 id, quint32 controlId, const QString &keySequence, QString *error) override;
    bool vcWidgetKeySequenceRemove(quint32 id, const QString &keySequence, QString *error) override;
    // --- XY Pad fixtures / presets / floor, Clock, Animation, Audio Triggers (ApiVcLiveDomain) ---
    void vcSetLiveListenerExt(ApiVcLiveListenerExt *listener) override;
    bool vcXyPadSetFloorPosition(quint32 id, double x, double y, double z, QString *error) override;
    bool vcXyPadAddFixtures(quint32 id, XyPadAddKind kind, quint32 refId, int headIndex,
                            int *addedPresetId, QString *error) override;
    bool vcXyPadRemoveHeads(quint32 id, const QJsonArray &heads, QString *error) override;
    bool vcXyPadSetHeadsRange(quint32 id, const QJsonArray &heads, int xMin, int xMax, bool xReverse,
                              int yMin, int yMax, bool yReverse, QString *error) override;
    int vcWidgetPresetMove(quint32 id, int presetId, bool up, QString *error) override;
    bool vcXyPadRenamePreset(quint32 id, int presetId, const QString &name, QString *error) override;
    bool vcClockPlayPause(quint32 id, QString *error) override;
    bool vcClockReset(quint32 id, QString *error) override;
    bool vcClockAddSchedules(quint32 id, const QList<quint32> &functionIds, QString *error) override;
    bool vcClockUpdateSchedule(quint32 id, int index, const QJsonObject &patch, QString *error) override;
    bool vcClockRemoveSchedule(quint32 id, int index, QString *error) override;
    bool vcAnimationSetFaderLevel(quint32 id, int level, QString *error) override;
    bool vcAnimationSetPresetKnobValue(quint32 id, int presetId, int value, QString *error) override;
    bool vcAudioTriggersSetCaptureEnabled(quint32 id, bool enabled, QString *error) override;
    bool vcAudioTriggersSetBarConfig(quint32 id, int index, const QJsonObject &patch, QString *error) override;

    /** Engine-side changes of that slice (a clock ticking, the audio capture delivering levels) - the
     *  domain must broadcast these with a null originClientId. */
    void simulateClockTick(quint32 id, int currentTime, bool running);
    void simulateAudioLevels(quint32 id, const QList<int> &levels);
    /** A Level slider's monitored channels changed (VCSlider::monitorValueChanged). */
    void simulateSliderMonitor(quint32 id, int monitorValue, bool isOverriding);
    /** The heads of an XY pad moved (VCXYPad::fixturePositionsChanged). */
    void simulateXyFixturePositions(quint32 id, const QList<QPointF> &positions);
    bool vcSliderResetOverride(quint32 id, QString *error) override;
    bool sliderOverriding(quint32 id) const { return m_widgets.value(id).sliderOverriding; }

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
        int width = 1920;  // VCPage's default geometry
        int height = 1080;
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
        int sideFaderLevel = 100;                         // CueList (VCCueList's default)
        QString factor = QStringLiteral("One");           // Speed: VcSpeedDialMultiplier wire string
        int tapTimeValue = 0;                             // Speed: last tap interval in ms, 0 = none
        QJsonArray presets;                               // Speed/XYPad/Animation: Vc<Type>Preset entries
        int nextPresetId = 16;                            // first id VCSpeedDial/VCXYPad assign
        QString framePin;                                 // Frame/SoloFrame: empty = no PIN set
        bool flashing = false;                            // Slider (Adjust): vc.slider.flash state
        int monitorValue = 0;                             // Slider (Level): monitored channel value
        bool sliderOverriding = false;                    // Slider (Level): fader overrides the monitor
        QJsonArray inputSources;                          // VcInputSource entries (ApiVcInputDomain)
        QJsonArray keySequences;                          // {keySequence, controlId} entries (ApiVcInputDomain)
        // XY Pad / Clock / Animation / AudioTriggers slice (ApiVcLiveDomain)
        QJsonArray xyFixtures;                            // XYPad: VcXyPadFixtureEntry entries
        double floorX = 5.0, floorY = 0.0, floorZ = 5.0;  // XYPad: floor target (metres)
        QJsonArray schedules;                             // Clock: VcClockSchedule entries (index = position)
        int clockTime = 0;                                // Clock: currentTime seed
        bool clockRunning = false;                        // Clock
        int faderLevel = 0;                               // Animation
        bool captureEnabled = false;                      // AudioTriggers
        QJsonArray bars;                                  // AudioTriggers: VcAudioTriggersBar entries
    };

    /** The external control table VCWidget subclasses register in their constructors, per wire
     *  type (vcbutton.cpp / vcslider.cpp / vccuelist.cpp / vcframe.cpp / vcspeeddial.cpp / ...):
     *  [{controlId, name, allowKeyboard}]. Frames add one "page shortcut" control per page
     *  (INPUT_SHORTCUT_BASE_ID = 20 + page) while in multipage mode. */
    static QJsonArray externalControlsFor(const VcWidgetState &w);

    static const QStringList PresetWidgetTypes; // Speed, XYPad, Animation

    /** A fresh VcWidgetState registered in m_widgets - shared by vcCreateWidget() and the bulk creators. */
    quint32 addWidget(const QString &widgetType, int page, quint32 parentId, const QRectF &geometry,
                      const QString &caption, const QJsonObject &typeConfig);

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
    ApiVcLiveListenerExt *m_liveListenerExt = nullptr;
};

#endif
