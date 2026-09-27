/*
  Q Light Controller Plus - Control API
  apivchost.h

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

#ifndef APIVCHOST_H
#define APIVCHOST_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPair>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QtGlobal>
#include <limits>

#include "scenevalue.h"

/**
 * Receiver for live (§4b) Virtual Console state changes - the host side of the vc.*.stateChanged/
 * valueChanged/... event family. ApiVcDomain implements this and registers itself via
 * ApiVcHost::vcSetLiveListener(); the host calls back whenever a widget's live state changes for ANY
 * reason (an API request, the QML UI, an external MIDI/DMX/keyboard input source, a Function
 * stopping on its own, ...). Every callback happens on the host's own (GUI) thread - qmlui's App
 * only ever receives widget signals there (engine-thread emitters reach the widgets through Qt's
 * automatic queued delivery), so the listener may broadcast synchronously.
 *
 * The listener does its own JSON shaping (topic names, field names, enum spellings live in one place,
 * ApiVcDomain), so implementations only pass engine-shaped values:
 *  - button $state is one of "inactive", "active", "monitoring" (VCButton::ButtonState, lowercased)
 *  - xy pad $x/$y are normalized 0.0..1.0 (see ApiVcHost::vcXyPadSetPosition() for the scale)
 *  - speed dial $ms is VCSpeedDial::currentTime()
 */
class ApiVcLiveListener
{
public:
    virtual ~ApiVcLiveListener() {}

    virtual void vcButtonStateChanged(quint32 widgetId, const QString &state) = 0;
    virtual void vcSliderValueChanged(quint32 widgetId, int value) = 0;
    virtual void vcCueListPlaybackChanged(quint32 widgetId, int playbackIndex, bool running, bool paused) = 0;
    virtual void vcXyPadPositionChanged(quint32 widgetId, double x, double y) = 0;
    virtual void vcSpeedDialValueChanged(quint32 widgetId, int ms) = 0;
    virtual void vcFramePageChanged(quint32 widgetId, int page) = 0;

    // --- Cue List side fader / Speed Dial extras (vc.cueList.sideFaderChanged, vc.speedDial.factorChanged,
    // vc.speedDial.tapChanged) ---

    /** VCCueList::sideFaderLevel() changed, or the crossfade bookkeeping it displays did (nextStepIndex /
     *  primaryTop - the step the fader's other end points at, and whether the current step sits at the
     *  top of the fader). $level is 0..100 in Crossfade mode, 0..255 in Steps mode. */
    virtual void vcCueListSideFaderChanged(quint32 widgetId, int level, int nextStepIndex, bool primaryTop) = 0;

    /** VCSpeedDial::currentFactor() changed; $factor is the VcSpeedDialMultiplier wire spelling
     *  ("OneSixteenth".."Sixteen"). */
    virtual void vcSpeedDialFactorChanged(quint32 widgetId, const QString &factor) = 0;

    /** VCSpeedDial::tapTimeValue() changed: $tapTimeValue is the tap interval in ms (0 = no tap series
     *  running), $currentTimeMs the dial's currentTime() at that moment. */
    virtual void vcSpeedDialTapChanged(quint32 widgetId, int tapTimeValue, int currentTimeMs) = 0;
};

/**
 * Virtual Console operations (docs/api-spec/fragments/virtualconsole.yaml's vc.page.* / vc.widget.*
 * plus the live-interaction vc.button, vc.slider, vc.cueList, vc.xyPad, vc.speedDial and vc.frame
 * methods) that ApiVcDomain needs. qmlui's App is the real implementation, driving the live VCPage/VCWidget/
 * VirtualConsole object graph - but controlapi must build and run without qmlui (see apiserver.h),
 * so ApiVcDomain depends on this plain interface instead, obtained via dynamic_cast on ApiServer's
 * parent (see ApiVcDomain::vcHost()), exactly like ApiCoreDomain does for ApiProjectHost.
 *
 * Request-shape validation (revision checks, "does this field make sense", type-string whitelisting,
 * cycle detection on reparent) stays in ApiVcDomain, using the query methods below - this interface's
 * mutation methods assume their caller already validated arguments and just perform the change (see
 * each method's own doc comment for exactly what's assumed). JSON *snapshot* shaping for pages/
 * widgets lives here instead, because only the concrete implementation knows a widget's real
 * type-specific fields (VcButtonConfig/VcSliderConfig/...) - see vcWidgetSnapshot().
 *
 * controlapi/test/apivcdomain's FakeVcHost is the other implementation: a headless in-memory model
 * (the same one that used to live directly inside ApiVcDomain before this interface existed) used to
 * keep that test suite meaningful without linking qmlui into a controlapi-only test binary.
 */
class ApiVcHost
{
public:
    virtual ~ApiVcHost() {}

    /** Sentinel for "no parent" (page root) / "not found", matching VCWidget::invalidId()'s role but
     *  expressed here without needing to include vcwidget.h. */
    static constexpr quint32 InvalidWidgetId = std::numeric_limits<quint32>::max();

    /*********************************************************************
     * Pages
     *********************************************************************/

    virtual int vcPageCount() const = 0;

    /** JSON per the VcPage schema: {index, name, hasPin}. Caller guarantees
     *  0 <= $index < vcPageCount(). */
    virtual QJsonObject vcPageSnapshot(int index) const = 0;

    virtual int vcSelectedPage() const = 0;

    /** Live/runtime (§4b) - must not affect Doc's modified/revision state. */
    virtual void vcSetSelectedPage(int index) = 0;

    /** Inserts a new page at $index (0 <= index <= vcPageCount()), shifting every existing page/
     *  widget at or after $index and the current selection exactly like VirtualConsole::addPage().
     *  Bumps Doc's modified flag. */
    virtual void vcAddPage(int index) = 0;

    /** Deletes the page at $index and every widget on it (recursively), renumbering later pages'
     *  widgets down by one. Returns false without changing anything if $index is out of range or
     *  this is the last remaining page (mirrors VirtualConsole::deletePage()'s own refusal) - the
     *  caller must not call this without checking vcPageCount() > 1 first, since on true it fills
     *  $deletedWidgetIds with the wire-format (string) ids of every widget removed. */
    virtual bool vcDeletePage(int index, QJsonArray &deletedWidgetIds) = 0;

    virtual void vcRenamePage(int index, const QString &name) = 0;

    /** Mirrors VirtualConsole::setPagePIN(): $newPin has already been validated by the caller to be
     *  either empty or exactly 4 digits. Returns false without changing anything if a PIN is
     *  currently set and $currentPin doesn't match it. */
    virtual bool vcSetPagePin(int index, const QString &currentPin, const QString &newPin) = 0;

    virtual bool vcValidatePagePin(int index, const QString &pin) const = 0;

    /*********************************************************************
     * Widgets - queries
     *********************************************************************/

    virtual bool vcWidgetExists(quint32 id) const = 0;

    /** Wire type string ("Button", "Slider", "Frame", ...) or an empty string if $id doesn't exist. */
    virtual QString vcWidgetType(quint32 id) const = 0;

    /** The page index of $id's top-level ancestor (or of $id itself if it has no parent), or -1 if
     *  $id doesn't exist. */
    virtual int vcWidgetPage(quint32 id) const = 0;

    /** $id's immediate parent widget id, or InvalidWidgetId if $id is a page-root widget or doesn't
     *  exist. */
    virtual quint32 vcWidgetParentId(quint32 id) const = 0;

    /** True if $id exists and is a Frame or SoloFrame - the only widget types that can be used as a
     *  vc.widget.create/reparent parent. */
    virtual bool vcIsContainerWidget(quint32 id) const = 0;

    /** Every id that currently exists, in no particular order - used by vc.widget.list, which does
     *  its own page/parent/type filtering and sorting on top of this. */
    virtual QList<quint32> vcWidgetIds() const = 0;

    /** Full VcWidget detail JSON (id, widgetType, page, parentId if any, geometry, zIndex,
     *  allowResize, isDisabled, isVisible, style{caption,backgroundColor,backgroundImage,
     *  foregroundColor,font}, typeConfig{...}, inputSources, keySequences, externalControls) per the
     *  spec's VcWidgetDetail schema. Caller guarantees vcWidgetExists($id). */
    virtual QJsonObject vcWidgetSnapshot(quint32 id) const = 0;

    /** fixtures.remap.apply: rewrites every widget's fixture/channel references (Slider level
     *  channels, XY Pad fixtures, Audio Triggers) from the source (key) to the target (value)
     *  SceneValue, like FixtureRemapManager::applyRemap() does after FixtureRemapper::applyRemap()
     *  - VC widgets live in the UI layer, so the engine's remapper leaves this to the host.
     *  Default no-op so a host without a Virtual Console (tests) needs nothing. */
    virtual void vcRemapChannels(const QMap<SceneValue, SceneValue> &remapMap) { Q_UNUSED(remapMap) }

    /*********************************************************************
     * Widgets - mutations
     *********************************************************************/

    /** Creates a widget of the given wire type string ($widgetType has already been validated
     *  against the known type list) at $page (already validated in range), optionally under
     *  $parentId (InvalidWidgetId = page root; caller has already validated it exists and is a
     *  container). Returns the new widget's id, or InvalidWidgetId on failure with *$error set. */
    virtual quint32 vcCreateWidget(const QString &widgetType, int page, quint32 parentId,
                                    const QJsonObject &geometry, const QJsonObject &style,
                                    const QJsonObject &typeConfig, QString *error) = 0;

    /** Deletes every widget in $ids plus their descendants (recursively) - unknown ids are silently
     *  ignored. Fills $deletedIds with the wire-format (string) ids of everything actually removed
     *  (may end up larger than $ids due to descendants, or smaller/empty if every id was unknown). */
    virtual void vcDeleteWidgets(const QList<quint32> &ids, QJsonArray &deletedIds) = 0;

    /** Applies whichever of these keys are present in $fields, atomically: geometry, zIndex,
     *  allowResize, isDisabled, isVisible, style. Does NOT touch "page" - see
     *  vcMoveTopLevelWidgetToPage() for that, kept separate because the domain validates/resolves it
     *  differently (rejecting it outright on a nested widget). Caller guarantees the widget exists. */
    virtual bool vcUpdateWidgetCommon(quint32 id, const QJsonObject &fields, QString *error) = 0;

    /** Moves a top-level (parentless) widget - and its whole subtree - onto a different page. Caller
     *  guarantees the widget currently has no parent and $newPage is in range. */
    virtual void vcMoveTopLevelWidgetToPage(quint32 id, int newPage) = 0;

    /** Partial-patch merge onto the widget's type-specific config (VcButtonConfig/VcSliderConfig/...
     *  depending on its type). Returns false with *$error set if $id's widget type has no
     *  setConfig support yet, or $configPatch contains a key that type doesn't recognize. */
    virtual bool vcSetWidgetConfig(quint32 id, const QJsonObject &configPatch, QString *error) = 0;

    /** Moves $id under $newParentId (InvalidWidgetId = page root) and repositions its top-left
     *  corner to $newTopLeft. Caller has already validated $newParentId exists, is a container, and
     *  is neither $id nor one of its own descendants. */
    virtual bool vcReparentWidget(quint32 id, quint32 newParentId, QPointF newTopLeft, QString *error) = 0;

    /** Bulk geometry-only update, all-or-nothing - caller has already validated every id in $updates
     *  exists. Each pair's QJsonObject is a geometry {x,y,width,height}. */
    virtual void vcRepositionWidgets(const QList<QPair<quint32, QJsonObject> > &updates) = 0;

    /*********************************************************************
     * Widgets - live interaction (§4b: no baseRevision, last-write-wins)
     *
     * Every method below assumes the caller already verified the widget exists AND has the matching
     * wire type (vcWidgetType(): "Button", "Slider", "CueList", "XYPad", "Speed", "Frame"/"SoloFrame")
     * - the domain sends NOT_FOUND / INVALID_PARAMS for those cases itself. A false return means the
     * engine refused the action in its current state (e.g. no Function/Chaser attached, page out of
     * range); *$error carries a human-readable reason for an INVALID_STATE/INVALID_PARAMS response.
     * The resulting live-state change (if any) is reported through the ApiVcLiveListener, never as a
     * return value - that keeps "changed by this request" and "changed by anything else" on one path.
     *********************************************************************/

    /** Registers the single listener that receives every live-state change (nullptr detaches). Owned
     *  by the caller; the host must stop calling it once detached or destroyed. */
    virtual void vcSetLiveListener(ApiVcLiveListener *listener) = 0;

    /** vc.button.press - mirrors what VCButtonItem.qml does on a touch: Toggle/Blackout act on the
     *  down-edge only (pressed=true toggles the current state; pressed=false is a no-op), StopAll fires
     *  on the down-edge only, Flash follows both edges (flashing exactly while pressed). */
    virtual bool vcButtonPress(quint32 id, bool pressed, QString *error) = 0;

    /** vc.slider.setValue - VCSlider::setValue() with the slider's own mode semantics (Level/Adjust/
     *  Submaster/GrandMaster). $value has been validated to 0..255 by the caller; the host confines it
     *  to the slider's [rangeLowLimit, rangeHighLimit] the same way the on-screen fader does. */
    virtual bool vcSliderSetValue(quint32 id, int value, QString *error) = 0;

    enum CueListAction { CueListPlay, CueListStop, CueListNext, CueListPrevious };

    /** vc.cueList.play/stop/next/previous - VCCueList::playClicked()/stopClicked()/nextClicked()/
     *  previousClicked(). Returns false (INVALID_STATE) when no Chaser is attached. */
    virtual bool vcCueListAction(quint32 id, CueListAction action, QString *error) = 0;

    /** vc.cueList.setPlaybackIndex - VCCueList::setPlaybackIndex() followed by playCurrentStep(), i.e.
     *  "jump to this step" (start the Chaser there if stopped, or switch step if running). $index has
     *  already been validated against vcCueListSnapshot()'s step count by the caller (-1 = none). */
    virtual bool vcCueListSetPlaybackIndex(quint32 id, int index, QString *error) = 0;

    /** vc.cueList.get - {steps:[{index,name,functionId,fadeIn,fadeOut,hold,notes}], playbackIndex,
     *  running, paused}. Empty steps when no Chaser is attached. */
    virtual QJsonObject vcCueListSnapshot(quint32 id) const = 0;

    /** vc.xyPad.setPosition - $x/$y are normalized 0.0..1.0 (validated by the caller); the host scales
     *  them onto VCXYPad's native 0..(255 + 255/256) position domain so 1.0 is the full 16-bit span. */
    virtual bool vcXyPadSetPosition(quint32 id, double x, double y, QString *error) = 0;

    /** vc.speedDial.setValue - VCSpeedDial::setCurrentTime($ms) (>= 0, validated by the caller). */
    virtual bool vcSpeedDialSetValue(quint32 id, int ms, QString *error) = 0;

    /** vc.speedDial.tap - VCSpeedDial::tap(). The first tap of a series only arms the timer and changes
     *  nothing observable; from the second tap within 1.5 s on, currentTime follows the tap interval. */
    virtual bool vcSpeedDialTap(quint32 id, QString *error) = 0;

    /** vc.frame.gotoPage - VCFrame::gotoPage(). Returns false with *$error set when $page is outside
     *  0..totalPagesNumber-1. Must be a no-op (no change, no event) when $page is already current. */
    virtual bool vcFrameGotoPage(quint32 id, int page, QString *error) = 0;

    /** vc.frame.get - {pages, currentPage, multipage}. */
    virtual QJsonObject vcFrameSnapshot(quint32 id) const = 0;

    /*********************************************************************
     * Cue List side fader / Speed Dial extras (live, §4b)
     *
     * Same caller contract as the live methods above: the widget exists and has the matching wire type
     * ("CueList" / "Speed"); changes are reported through the ApiVcLiveListener.
     *********************************************************************/

    /** vc.cueList.setSideFaderLevel - VCCueList::setSideFaderLevel(). $level has been validated to
     *  0..255 by the caller; the host confines it to the mode's own range (0..100 in Crossfade mode)
     *  the way the on-screen fader does, and returns false (INVALID_STATE) while sideFaderMode is
     *  None (the fader is hidden then). */
    virtual bool vcCueListSetSideFaderLevel(quint32 id, int level, QString *error) = 0;

    /** vc.speedDial.setFactor - VCSpeedDial::setCurrentFactor(). $factor is a VcSpeedDialMultiplier
     *  wire string already validated by the caller to be one of OneSixteenth..Sixteen. */
    virtual bool vcSpeedDialSetFactor(quint32 id, const QString &factor, QString *error) = 0;

    /** vc.speedDial.apply - VCSpeedDial::applyFunctionsTime(enqueue=true), the "Apply" button. */
    virtual bool vcSpeedDialApply(quint32 id, QString *error) = 0;

    /** vc.speedDial.resetTap - VCSpeedDial::resetTap(). */
    virtual bool vcSpeedDialResetTap(quint32 id, QString *error) = 0;

    /*********************************************************************
     * Widget presets (vc.widget.preset.add/remove are §4a, vc.widget.preset.apply is §4b)
     *
     * Presets exist on Speed, XYPad and Animation widgets only (VCWidget::supportsPresets()); the
     * host dispatches on the widget's type and the preset payload shape follows the spec's
     * VcSpeedDialPreset(Data) / VcXyPadPreset(Data) / VcAnimationPreset(Data). The caller checks
     * vcWidgetSupportsPresets() first and resolves "no such preset" itself via vcWidgetPresets().
     *********************************************************************/

    /** True if $id exists and its type keeps a preset list. */
    virtual bool vcWidgetSupportsPresets(quint32 id) const = 0;

    /** The widget's full preset list in its type's Vc<Type>Preset shape (each entry carries a stable
     *  integer presetId), in display order. Empty for a widget without presets. */
    virtual QJsonArray vcWidgetPresets(quint32 id) const = 0;

    /** Adds a preset built from $preset (the type's Vc<Type>PresetData) and returns its new presetId,
     *  or -1 with *$error set when the payload is invalid for this widget type. */
    virtual int vcWidgetPresetAdd(quint32 id, const QJsonObject &preset, QString *error) = 0;

    /** Removes the preset $presetId (already known to exist). */
    virtual bool vcWidgetPresetRemove(quint32 id, int presetId, QString *error) = 0;

    /** Live activation of preset $presetId (already known to exist): Speed sets currentTime to the
     *  preset's value (reported as vc.speedDial.valueChanged), XYPad/Animation call their applyPreset(). */
    virtual bool vcWidgetPresetApply(quint32 id, int presetId, QString *error) = 0;

    /** vc.speedDial.preset.update - applies whichever of "name" / "valueMs" are present in $patch to
     *  the Speed widget's preset $presetId (already known to exist). */
    virtual bool vcSpeedDialPresetUpdate(quint32 id, int presetId, const QJsonObject &patch, QString *error) = 0;
    /*********************************************************************
     * Widgets - layout / configuration slice (ApiVcLayoutDomain,
     * controlapi/src/domains/apivclayoutdomain.cpp)
     *
     * Same assumptions as above: the domain has already verified the widget exists and has the
     * matching wire type; every id in a list is known. Structural (§4a) methods do NOT bump Doc
     * themselves - the domain calls Doc::setModified() and broadcasts after a true return.
     *********************************************************************/

    /** vc.frame.setPin - VCFrame::setPIN() with vc.page.setPin's semantics: $newPin is already
     *  validated (empty or exactly 4 digits); returns false without changing anything when a PIN is
     *  set and $currentPin doesn't match it. */
    virtual bool vcFrameSetPin(quint32 id, const QString &currentPin, const QString &newPin) = 0;

    /** vc.frame.validatePin - stateless check, true when the frame has no PIN or $pin matches. */
    virtual bool vcFrameValidatePin(quint32 id, const QString &pin) const = 0;

    /** vc.frame.cloneFirstPage - VCFrame::cloneFirstPage(). Returns false with *$error set when the
     *  frame has a single page (the engine silently no-ops then). Fills $createdIds with the wire
     *  ids of every widget the clone created. */
    virtual bool vcFrameCloneFirstPage(quint32 id, QJsonArray &createdIds, QString *error) = 0;

    /** vc.slider.setLevelChannels - bulk replace of VCSlider's Level-mode channel list; each entry
     *  is {fixtureId (already validated to exist), channel (already validated < fixture channels)}. */
    virtual bool vcSliderSetLevelChannels(quint32 id, const QList<QPair<quint32, quint32> > &channels, QString *error) = 0;

    /** vc.slider.flash - VCSlider::flashFunction($on). Returns false (INVALID_STATE) when the slider
     *  is not in Adjust mode with a controlled Function and adjustFlashEnabled. */
    virtual bool vcSliderFlash(quint32 id, bool on, QString *error) = 0;

    /** vc.widget.createFromFunctions - VCFrame::addFunctions(): one Button (hint "button"), one
     *  Adjust-mode Slider ("adjustSlider") per Function, or one Cue List ("cueList", every id already
     *  validated to be a Chaser). $functionIds have already been validated to exist. Returns the
     *  new widgets' ids (empty with *$error set on failure). */
    virtual QList<quint32> vcCreateWidgetsFromFunctions(int page, quint32 parentId, const QList<quint32> &functionIds,
                                                        QPointF position, const QString &widgetHint, QString *error) = 0;

    /** vc.widget.createMatrix - VCFrame::addWidgetMatrix(): a container Frame (or SoloFrame when
     *  $soloFrame) holding $columns x $rows Buttons ($matrixType "Button") or Sliders ("Slider"), each
     *  $widgetSize big. Returns every new id, the container first. */
    virtual QList<quint32> vcCreateWidgetMatrix(int page, quint32 parentId, const QString &matrixType, QPointF position,
                                                int columns, int rows, int widgetWidth, int widgetHeight,
                                                bool soloFrame, QString *error) = 0;

    /** vc.widget.usage - VirtualConsole::usageList($functionId): every widget id referencing that
     *  Function (Button's functionID, Slider's controlledFunction, Cue List's chaser, Clock schedules). */
    virtual QList<quint32> vcWidgetsUsingFunction(quint32 functionId) const = 0;
};

#endif
