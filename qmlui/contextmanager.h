/*
  Q Light Controller Plus
  contextmanager.h

  Copyright (c) Massimo Callegari

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

#ifndef CONTEXTMANAGER_H
#define CONTEXTMANAGER_H

#include <QHash>
#include <QObject>
#include <QQuickView>
#include <QVector3D>

#include "qlcchannel.h"
#include "scenevalue.h"
#include "genericdmxsource.h"

class Doc;
class Fixture;
class FixtureGroup;
class MainView2D;
class MainView3D;
class MainViewDMX;
class FixtureManager;
class FunctionManager;
class MonitorProperties;
class PreviewContext;
class SimpleDesk;

class ContextManager final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString currentContext READ currentContext NOTIFY currentContextChanged)
    Q_PROPERTY(QString currentSubContext READ currentSubContext WRITE setCurrentSubContext NOTIFY currentSubContextChanged)
    Q_PROPERTY(QVector3D environmentSize READ environmentSize WRITE setEnvironmentSize NOTIFY environmentSizeChanged)
    Q_PROPERTY(quint32 universeFilter READ universeFilter WRITE setUniverseFilter NOTIFY universeFilterChanged)
    Q_PROPERTY(int selectedFixturesCount READ selectedFixturesCount NOTIFY selectedFixturesChanged)
    Q_PROPERTY(int selectedDimmersCount READ selectedDimmersCount NOTIFY selectedDimmersCountChanged)
    Q_PROPERTY(QVector3D fixturesPosition READ fixturesPosition WRITE setFixturesPosition NOTIFY fixturesPositionChanged)
    Q_PROPERTY(QVector3D fixturesRotation READ fixturesRotation WRITE setFixturesRotation NOTIFY fixturesRotationChanged)
    Q_PROPERTY(bool selectedFixtureHasDmxTransform READ selectedFixtureHasDmxTransform NOTIFY selectedFixturesChanged)
    Q_PROPERTY(quint32 fixtureDmxTransformFlags READ fixtureDmxTransformFlags WRITE setFixtureDmxTransformFlags NOTIFY fixtureDmxTransformFlagsChanged)
    Q_PROPERTY(qreal fixtureRotationScale READ fixtureRotationScale WRITE setFixtureRotationScale NOTIFY fixtureRotationScaleChanged)
    Q_PROPERTY(qreal fixturePositionRange READ fixturePositionRange WRITE setFixturePositionRange NOTIFY fixturePositionRangeChanged)
    Q_PROPERTY(int dumpValuesCount READ dumpValuesCount NOTIFY dumpValuesCountChanged)
    Q_PROPERTY(quint32 dumpChannelMask READ dumpChannelMask NOTIFY dumpChannelMaskChanged)
    Q_PROPERTY(bool multipleSelection READ multipleSelection WRITE setMultipleSelection NOTIFY multipleSelectionChanged)
    Q_PROPERTY(bool positionPicking READ positionPicking WRITE setPositionPicking NOTIFY positionPickingChanged)
    Q_PROPERTY(QVector3D lastPickedPoint READ lastPickedPoint NOTIFY lastPickedPointChanged)
    Q_PROPERTY(bool showFixtureGroups READ showFixtureGroups WRITE setShowFixtureGroups NOTIFY showFixtureGroupsChanged)

public:
    explicit ContextManager(QQuickView *view, Doc *doc,
                            FixtureManager *fxMgr, FunctionManager *funcMgr,
                            QObject *parent = 0);
    ~ContextManager();

    /** Register/Unregister a context to the map of known contexts */
    void registerContext(PreviewContext *context);
    void unregisterContext(QString name);

    /** Enable/disable the context with the specified $name.
     *  This sets a flag in the context to know if it is visible
     *  on the screen, so to decide if changes should be applied to it */
    Q_INVOKABLE void enableContext(QString name, bool enable, QQuickItem *item);

    /** Get a reference of a registered PreviewContext by name.
     *  Returns nullptr if not found */
    PreviewContext *contextByName(QString ctxName);

    /** Detach/Reattach a context from/to the application main window */
    Q_INVOKABLE void detachContext(QString name);
    Q_INVOKABLE void reattachContext(QString name);

    /** Switch to the context with the given $name.
     *  Supports both QLC+ 4 and QLC+ 5 context names */
    void switchToContext(QString name);

    /** Return the currently active context */
    QString currentContext() const;

    /** Get/Set the FixturesAndFunctions sub-context */
    QString currentSubContext() const;
    void setCurrentSubContext(QString ctx);

    MainView2D *get2DView();
    MainView3D *get3DView();

    /** Get/Set the environment width/height/depth size */
    QVector3D environmentSize() const;
    void setEnvironmentSize(QVector3D environmentSize);

    /** Get/Set multiple item selection mode */
    bool multipleSelection() const;
    void setMultipleSelection(bool multipleSelection);

    /** Enable/Disable a position picking process */
    bool positionPicking() const;
    void setPositionPicking(bool enable);

    Q_INVOKABLE void setPositionPickPoint(QVector3D point);

    /** Returns the last 3D point picked in the scene (in monitor coordinates, mm) */
    QVector3D lastPickedPoint() const;

    /** Get/Set the last item clicked type */
    int lastClickedType() const;

    /** Get/Set the visibility of the fixture groups bar. This is shared
     *  between the 2D and 3D views and not persisted across sessions */
    bool showFixtureGroups() const;
    void setShowFixtureGroups(bool show);

public slots:
    void setLastClickedType(const int &newLastClickedType);

signals:
    void currentContextChanged();
    void currentSubContextChanged();
    void environmentSizeChanged();
    void positionPickingChanged();
    void lastPickedPointChanged();
    void multipleSelectionChanged();
    void showFixtureGroupsChanged();

public slots:
    /** Resets the data structures and update the currently enabled views */
    void resetContexts();

    /** Re-pushes every DMX-driven fixture's persisted position/rotation
     *  (see MonitorProperties::HasDmxPositionFlag/HasDmxRotationFlag) back
     *  onto its live PositionX/Y/Z and/or RotationX/Y/Z channels, so a
     *  fixture with its own position/rotation channels ends up back where it
     *  was dragged to before the project was saved - including on real
     *  connected hardware, since a project load never otherwise touches
     *  these channels. Call this once, after a project has fully finished
     *  loading (Doc's fixtures AND MonitorProperties both populated) - never
     *  for a brand-new/empty document, since there is nothing to restore and
     *  no fixture will have either flag set. See pushPositionDelta()/
     *  pushRotationDelta() for why the flag (not just a non-default stored
     *  value) is required: MonitorProperties::fixturePosition()/
     *  fixtureRotation() are also written, in an unrelated placement
     *  convention, by ordinary 2D/3D fixture placement code
     *  (MainView2D::createFixtureItems(), MainView3D's equivalent, the
     *  Universe Grid view) - without the flag, restoring from those values
     *  would push an arbitrary, meaningless position/rotation onto a
     *  fixture's real DMX channels every time such a project is opened. */
    void restorePersistedDmxTransforms();

    /** Destroys the items of the currently enabled preview views, without
     *  recreating them. To be called before the Doc contents are cleared,
     *  since the view items reference Doc fixtures */
    void resetViewItems();

    /** Handle a key press from a QQuickView context */
    void handleKeyPress(QKeyEvent *e);

    /** Handle a key release from a QQuickView context */
    void handleKeyRelease(QKeyEvent *e);

    /** Perform mass selection/deselection of fixtures.
     *  While in batch mode, views update and signals are suppressed.
     *  Mass signals are emitted only when $enable is false */
    void setBatchSelection(bool enable);

    /** Returns true if we're currently in a batch selection */
    bool isBatchSelection() const;

private:
    /** Recompute and push out, for every Fixture Group, whether all of its
     *  fixtures are currently selected (see isGroupFullySelected()) - keeps
     *  the group's own tree/list row in sync with the underlying per-fixture
     *  selection, the same way individual fixture rows are already kept in
     *  sync. Called after any fixture selection change (both the plain path
     *  and the batch-completion path). */
    void refreshGroupSelectionRoles();

private:
    /** Reference to the QML view root */
    QQuickView *m_view;
    /** Reference to the project workspace */
    Doc *m_doc;
    /** Reference to the Doc Monitor properties */
    MonitorProperties *m_monProps;

    /** Reference to a simple PreviewContext representing
     *  the universe grid view, since it doesn't have a dedicated class */
    PreviewContext *m_uniGridView;
    /** Reference to the DMX Preview context */
    MainViewDMX *m_DMXView;
    /** Reference to the 2D Preview context */
    MainView2D *m_2DView;
    /** Reference to the 3D Preview context */
    MainView3D *m_3DView;
    /** Reference to the Fixture Manager */
    FixtureManager *m_fixtureManager;
    /** Reference to the Function Manager */
    FunctionManager *m_functionManager;

    QMap <QString, PreviewContext *> m_contextsMap;

    /** Holds the currently selected sub-context of FixturesAndFunctions */
    QString m_currentSubContext;

    /** Flag that indicates if multiple item selection is active */
    bool m_multipleSelection;
    /** Flag that indicates if a position picking is active */
    bool m_positionPicking;
    /** Flag to indicate if we're performing a mass fixture selection */
    bool m_batchSelection;
    /** Flag that indicates if the fixture groups bar is visible in the 2D/3D views */
    bool m_showFixtureGroups;
    /** Last 3D point picked in the scene (in monitor coordinates) */
    QVector3D m_lastPickedPoint;

    /** Keep track of the last item type that was
     *  clicked, to handle the Del keypress */
    int m_lastClickedType;

    /*********************************************************************
     * Universe filtering
     *********************************************************************/
public:
    /** Get/Set the universe displayed by contexts */
    quint32 universeFilter() const;
    void setUniverseFilter(quint32 universeFilter);

signals:
    void universeFilterChanged(quint32 universeFilter);

private:
    /** The currently displayed universe
      * The value Universe::invalid() means "All universes" */
    quint32 m_universeFilter;

    /*********************************************************************
     * Common fixture helpers
     *********************************************************************/
public:
    /** Select/Deselect a preview item with the provided $itemID */
    Q_INVOKABLE void setItemSelection(quint32 itemID, bool enable, int keyModifiers);

    /** Select/Deselect a fixture with the provided $itemID and $headIndex */
    Q_INVOKABLE void setFixtureSelection(quint32 itemID, int headIndex, bool enable);

    /** Select/Deselect a fixture with the provided $fixtureID */
    Q_INVOKABLE void setFixtureIDSelection(quint32 fixtureID, bool enable);

    /** Deselect all the currently selected fixtures */
    Q_INVOKABLE void resetFixtureSelection();

    /** Toggle between none/all fixture selection */
    Q_INVOKABLE void toggleFixturesSelection();

    /** Select the fixtures that intersects the provided rectangle coordinates in a 2D environment */
    Q_INVOKABLE void setRectangleSelection(qreal x, qreal y, qreal width, qreal height, int keyModifiers);

    /** Select every even/odd fixture of the currently selected ones */
    Q_INVOKABLE void selectEvenOdd(bool even);

    /** Select every Nth fixture of the currently selected ones. Assumes n >= 2 */
    Q_INVOKABLE void selectEveryNth(int n);

    /** Returns a list of the selected fixture addresses */
    Q_INVOKABLE QVariantList selectedFixtureAddress();

    /** Returns a list of the selected Fixture IDs as QVariantList */
    Q_INVOKABLE QVariantList selectedFixtureIDVariantList();

    /** Returns a list of the selected item IDs as QVariantList */
    QVariantList selectedItemIDVariantList();

    /** Returns the number of currently selected fixtures */
    int selectedFixturesCount();

    /** Returns the number of generic dimmers currently selected */
    int selectedDimmersCount();

    /** Returns if the fixture with $fxID is currently selected */
    Q_INVOKABLE bool isFixtureSelected(quint32 itemID);

    /** Sets the position of the Fixture with the provided $itemID */
    Q_INVOKABLE void setFixturePosition(quint32 itemID, qreal x, qreal y, qreal z);

    /** Adds an offset (in mm) to the selected Fixture positions. This is called only by the 2D view */
    Q_INVOKABLE void setFixturesOffset(qreal x, qreal y);

    /** Set/Get the position of the currently selected fixtures */
    QVector3D fixturesPosition() const;
    void setFixturesPosition(QVector3D position);

    /** Set the gelatine color for the selected fixtures */
    Q_INVOKABLE void setFixturesGelColor(QColor color);

    /** Set a fixed zoom value for the selected fixtures */
    Q_INVOKABLE void setFixedZoom(int degrees);

    /** Align the currently selected Fixtures with the provided $alignment */
    Q_INVOKABLE void setFixturesAlignment(int alignment);

    /** Distribute the currently selected Fixtures with the provided $direction */
    Q_INVOKABLE void setFixturesDistribution(int direction);

    /** Arrange the currently selected Fixtures evenly around a circle of the
     *  given $diameter (mm), centered on their current centroid. If
     *  $lookAtCenter is true, each fixture's yaw (the rotation axis
     *  perpendicular to the plane it's being arranged in) is also set so it
     *  faces the centroid. */
    Q_INVOKABLE void arrangeFixturesInCircle(qreal diameter, bool lookAtCenter = false);

    /** Arrange the currently selected Fixtures in a grid spanning $width x $height
     *  (mm), centered on their current centroid and rotated by $angleDegrees.
     *  $columns <= 0 auto-picks a near-square column count from the selection size */
    Q_INVOKABLE void arrangeFixturesInGrid(qreal width, qreal height, int columns, qreal angleDegrees);

    /** Arrange the currently selected Fixtures evenly along a line of the given
     *  $length (mm) and $angleDegrees orientation, centered on their current
     *  centroid. If $lookAtCenter is true, each fixture's yaw (the rotation
     *  axis perpendicular to the plane it's being arranged in) is also set
     *  so it faces the centroid. */
    Q_INVOKABLE void arrangeFixturesInLine(qreal length, qreal angleDegrees, bool lookAtCenter = false);

    /** Returns the diameter (mm) of the circle that best fits the currently
     *  selected Fixtures' current positions: twice their average distance
     *  from the centroid. Returns 0 if fewer than 2 fixtures are selected.
     *  Read-only - used by the Arrange popup's "detect from placement"
     *  toggle to pre-fill arrangeFixturesInCircle()'s $diameter argument. */
    Q_INVOKABLE qreal detectedCircleDiameter() const;

    /** Returns the length (mm) of the line that best fits the currently
     *  selected Fixtures' current positions: the span of their positions
     *  projected onto the detected line direction (see detectedLineAngle()).
     *  Returns 0 if fewer than 2 fixtures are selected, or if they're all on
     *  top of each other. Read-only, for the Arrange popup's "detect from
     *  placement" toggle. */
    Q_INVOKABLE qreal detectedLineLength() const;

    /** Returns the orientation (degrees) of the line that best fits the
     *  currently selected Fixtures' current positions: the principal axis of
     *  their scatter in the plane the user is looking at (found via 2D PCA).
     *  Returns 0 if fewer than 2 fixtures are selected, or if they're all on
     *  top of each other. Read-only, for the Arrange popup's "detect from
     *  placement" toggle. There's no equivalent detectedGrid*() pair - unlike
     *  a circle or a line, an arbitrary scatter has no single well-defined
     *  grid (row/column count, spacing, rotation) to recover. */
    Q_INVOKABLE qreal detectedLineAngle() const;

    /** Add or remove a linked fixture based on the provided $itemID */
    Q_INVOKABLE void setLinkedFixture(quint32 itemID);

    Q_INVOKABLE void updateFixturesCapabilities();

    /** Get the DMX/degrees value of the current fixture selection
     *  for the requested channel type.
     *  Returns -1 in case of mixed values */
    Q_INVOKABLE qreal getCurrentValue(int type, bool degrees);

    /** Get the RGB color of the current fixture selection */
    Q_INVOKABLE void getCurrentColors(QQuickItem *item) const;

    Q_INVOKABLE void createFixtureGroup();

    /** Set/Get the rotation of the currently selected fixtures */
    QVector3D fixturesRotation() const;
    void setFixturesRotation(QVector3D degrees);
    void setFixtureRotation(quint32 itemID, QVector3D degrees);

    /** True when at least one selected fixture has its own PositionX/Y/Z or
     *  RotationX/Y/Z DMX channels - i.e. whether the per-fixture DMX
     *  invert/scale/range properties below are meaningful for the current
     *  selection. False for no selection, or a selection where no fixture
     *  has any of those channels. */
    bool selectedFixtureHasDmxTransform() const;

    /** Get/Set the per-fixture DMX position/rotation invert flags (any
     *  combination of MonitorProperties::InvertedPosition/RotationXYZFlag)
     *  for every currently selected fixture that has a DMX position/rotation
     *  transform (see selectedFixtureHasDmxTransform()). The getter reads
     *  back the first qualifying selected fixture's value (0 if none
     *  qualify); the setter applies to every qualifying fixture, preserving
     *  each fixture's own other flag bits (Hidden/Locked/InvertedPan/
     *  InvertedTilt). No-ops if no selected fixture qualifies. */
    quint32 fixtureDmxTransformFlags() const;
    void setFixtureDmxTransformFlags(quint32 flags);

    /** Get/Set the per-fixture DMX-to-view rotation scale multiplier
     *  (default 1.0) for every currently selected fixture that has a DMX
     *  position/rotation transform. The getter reads back the first
     *  qualifying selected fixture's value (1.0 if none qualify); the setter
     *  applies to every qualifying fixture. Rotation-only - position uses
     *  the independent fixturePositionRange below. */
    qreal fixtureRotationScale() const;
    void setFixtureRotationScale(qreal scale);

    /** Get/Set the per-fixture absolute position range, in meters (default
     *  800.0), for every currently selected fixture that has a DMX
     *  position/rotation transform. The getter reads back the first
     *  qualifying selected fixture's value (800.0 if none qualify); the
     *  setter applies to every qualifying fixture. */
    qreal fixturePositionRange() const;
    void setFixturePositionRange(qreal range);

    /** Select/Deselect all the fixtures of the Group/Universe with the provided $id */
    Q_INVOKABLE void setFixtureGroupSelection(quint32 id, bool enable, bool isUniverse);

    /** Returns true if every fixture belonging to the Group with the provided $id
     *  is currently selected (extra, non-group fixtures also being selected does
     *  not disqualify it). False for an empty or non-existent group. */
    Q_INVOKABLE bool isGroupFullySelected(quint32 id) const;

    /** "Invert Selection in Group(s)": replaces the current fixture selection
     *  with the union, across every Fixture Group that has at least one
     *  currently-selected member ("candidate group"), of that group's
     *  complement (the group's own members that are NOT currently selected).
     *  A selected fixture that belongs to no group contributes nothing and is
     *  simply dropped. A no-op (selection left untouched) when nothing is
     *  currently selected.
     *
     *  When the selection has 0 or 1 candidate group, this applies
     *  immediately, same as always. When it spans more than one candidate
     *  group, this instead emits candidateGroupsForInversionReady() with the
     *  ambiguous candidates and waits for the QML side to ask the user which
     *  one(s) to use (see confirmGroupSelectionInversion()) - the selection
     *  is left untouched until/unless that happens. */
    Q_INVOKABLE void invertGroupSelection();

    /** Applies "Invert Selection in Group(s)", scoped to just the Fixture
     *  Groups named in $groupIds (as returned to QML via
     *  candidateGroupsForInversionReady()'s "mValue" entries), after the user
     *  has picked which of the ambiguous candidate groups to include. Any id
     *  that no longer resolves to a FixtureGroup (e.g. deleted while the
     *  dialog was open) is silently skipped. */
    Q_INVOKABLE void confirmGroupSelectionInversion(QVariantList groupIds);

    /** "Select Fixtures in Function(s)": replaces the current fixture
     *  selection with every fixture referenced by the Function(s) currently
     *  selected in the Function Manager (see FixtureUtils::functionsFixtures()
     *  for exactly which Function types are supported and how a Chaser's
     *  steps are resolved), each one expanded to its own heads/linked
     *  sub-items via $m_monProps - the same expansion setFixtureGroupSelection()
     *  applies to a Fixture Group's members. A no-op when no Function is
     *  currently selected in the Function Manager, or when the selected
     *  Function(s) reference no fixtures. */
    Q_INVOKABLE void selectFixturesInFunctions();

    Q_INVOKABLE void setChannelValueByType(int type, int value, bool isRelative = false, quint32 channel = UINT_MAX,
                                            GenericDMXSource::Feature feature = GenericDMXSource::IntensityTool);

    Q_INVOKABLE void setColorValue(QColor col, QColor wauv);

    /** Set a Pan/Tilt position in degrees */
    Q_INVOKABLE void setPositionValue(int type, float degrees, bool isRelative);

    /** Set Pan/Tilt values at half position */
    Q_INVOKABLE void setPositionCenter();

    /** Set a zoom channel in degrees */
    Q_INVOKABLE void setBeamDegrees(float degrees, bool isRelative);

    Q_INVOKABLE void highlightFixtureSelection();

    void setChannelValues(QList<SceneValue> values);

    /** Select the next available Fixture group, cycling through the
     *  groups defined in the Doc. Deselects everything else.
     *  Registered as a ShortcutManager action by App */
    void selectNextFixtureGroup();

    /** Perform the context-sensitive delete dispatched on m_lastClickedType
     *  (fixtures/functions/folders/show items/tracks). Registered as a
     *  ShortcutManager action by App */
    void deleteSelectedItems();

protected slots:
    void slotNewFixtureCreated(quint32 fxID, qreal x, qreal y, qreal z = 0);
    void slotFixtureDeleted(quint32 itemID);
    void slotFixtureFlagsChanged(quint32 itemID, quint32 flags);

    void slotChannelValueChanged(quint32 fxID, quint32 channel, quint8 value);
    void slotPresetChanged(const QLCChannel *channel, quint8 value);

    void slotSimpleDeskValueChanged(quint32 fxID, quint32 channel, quint8 value);

    /** Invoked by the QLC+ engine to inform the UI that the
     *  Universe at $idx has changed */
    void slotUniverseWritten(quint32 idx, const QByteArray& ua);

    /** Invoked when Function editing begins or ends in the Function Manager.
     *  Context Manager doesn't care much about Functions, it just needs
     *  to know if it has to set channel values on the GenericDMXSource or
     *  forward them to the Function Manager */
    void slotFunctionEditingChanged(bool status);

signals:
    void selectedFixturesChanged();
    void selectedDimmersCountChanged();
    void fixturesPositionChanged();
    void fixturesRotationChanged();
    void fixtureDmxTransformFlagsChanged();
    void fixtureRotationScaleChanged();
    void fixturePositionRangeChanged();

    /** Emitted by invertGroupSelection() instead of applying immediately,
     *  whenever the current selection spans more than one candidate Fixture
     *  Group - the QML side is expected to let the user choose a subset and
     *  then call confirmGroupSelectionInversion(). $groups is a QVariantList
     *  of QVariantMaps, one per candidate group, each with "mLabel" (the
     *  group's name()) and "mValue" (its id, to pass back in
     *  confirmGroupSelectionInversion()). */
    void candidateGroupsForInversionReady(QVariantList groups);

private:
    /** Shared tail end of invertGroupSelection()/confirmGroupSelectionInversion():
     *  replaces the current fixture selection with
     *  FixtureUtils::invertGroupSelection()'s result for $groups. */
    void applyGroupSelectionInversion(const QList<FixtureGroup *> &groups);

    /** Returns the distinct Fixture IDs (deduplicated - a multi-head/linked
     *  fixture contributes several itemIDs to m_selectedFixtures but must
     *  only be processed once) among the current selection that have their
     *  own DMX position/rotation transform - i.e. the same per-fixture
     *  channel check selectedFixtureHasDmxTransform() does, applied across
     *  the whole selection instead of just the first fixture. Shared by
     *  fixtureDmxTransformFlags()/setFixtureDmxTransformFlags(),
     *  fixtureRotationScale()/setFixtureRotationScale() and
     *  fixturePositionRange()/setFixturePositionRange() so they don't each
     *  duplicate this loop. Order follows m_selectedFixtures. */
    QList<quint32> qualifyingDmxTransformFixtures() const;

    /** Returns the world axis indices (0=X, 1=Y, 2=Z) that represent the
     *  horizontal ($hAxis) and vertical ($vAxis) directions on screen for
     *  $pointOfView, plus the remaining depth axis ($dAxis). Used by the
     *  arrangeFixturesIn*() methods to lay fixtures out in the plane the
     *  user is currently looking at. */
    void fixturePlaneAxes(int pointOfView, int &hAxis, int &vAxis, int &dAxis) const;

    /** Returns the average position (mm) of the currently selected fixtures */
    QVector3D selectedFixturesCentroid() const;

    /** Returns $itemID's actual visual position (mm), matching whichever
     *  storage is authoritative for it: gridCenter + cachedPositionDelta()
     *  for a fixture with its own PositionX/Y/Z DMX channels (see
     *  pushPositionDelta()'s doc comment), or plain MonitorProperties
     *  fixturePosition() otherwise. Same branch fixturesPosition()'s
     *  single-selection case and setFixturesOffset()/setFixturesPosition()
     *  already use - selectedFixturesCentroid() and the arrangeFixturesIn*()
     *  methods must read through this rather than MonitorProperties
     *  directly, or they silently operate on stale/meaningless data for a
     *  DMX-position-driven fixture (root cause of a user report: arranging a
     *  group of such fixtures looked correct, but the next group move
     *  snapped them back near their pre-arrange positions, since nothing
     *  had told the DMX delta cache about the arrangement). */
    QVector3D effectiveFixturePosition(quint32 itemID) const;

    /** Writes $newPos (mm) as $itemID's new visual position through the same
     *  storage effectiveFixturePosition() reads from - i.e. pushPositionDelta()
     *  for a DMX-position-driven fixture, or MonitorProperties::setFixturePosition()
     *  otherwise - and refreshes whichever of the 2D/3D views are enabled.
     *  Used by the arrangeFixturesIn*() methods so they commit through the
     *  same path setFixturesOffset()/setFixturesPosition() use instead of
     *  writing MonitorProperties directly (see effectiveFixturePosition()'s
     *  doc comment for why that went wrong before). Tardis undo is only
     *  recorded for the non-DMX branch, matching setFixturesOffset()/
     *  setFixturesPosition()'s own DMX branches, which don't record it either. */
    void applyArrangedFixturePosition(quint32 itemID, const QVector3D &newPos);

    /** Computes the principal-axis orientation ($angleRadians) and matching
     *  span ($length, mm) of the currently selected fixtures' current
     *  positions, in the plane the user is looking at. Shared by
     *  detectedLineAngle()/detectedLineLength() so the fit - a 2D PCA over
     *  the position scatter - is only computed once. Both outputs are left
     *  at 0 if fewer than 2 fixtures are selected, or if they coincide. */
    void detectedLineFit(qreal &angleRadians, qreal &length) const;

    /** Rotates the yaw of the Fixture with the given $itemID (the rotation
     *  axis perpendicular to the $hAxis/$vAxis plane, i.e. $dAxis) so it
     *  faces $centroid from $newPos - both in world space. Used by
     *  arrangeFixturesInCircle()/arrangeFixturesInLine() when their
     *  $lookAtCenter argument is true. Assumes 0 degrees of yaw faces along
     *  +$hAxis, same as the placement math's own angle convention - this is
     *  an assumption about the fixture model's un-rotated facing direction
     *  that hasn't been visually confirmed against the 3D mesh, so a 180 or
     *  90 degree offset may need correcting here if fixtures turn out to
     *  face the wrong way in practice. */
    void faceFixtureTowards(quint32 itemID, const QVector3D &newPos, const QVector3D &centroid,
                             int hAxis, int vAxis, int dAxis);

    /** Returns the currently selected Fixture item IDs sorted by DMX order
     *  (universe/address, then head/linked index), rather than the order
     *  they were selected in. Used by the arrangeFixturesIn*() methods so
     *  a shape follows patch order rather than click order. */
    QList<quint32> sortedSelectedFixtures() const;

    /** Returns the currently selected Fixture item IDs ordered by their
     *  Fixture Group's own grid assignment (row-major, top-left to
     *  bottom-right) when every selected fixture belongs to the same single
     *  FixtureGroup - this is the order RGB Matrix effects actually use, and
     *  is independent of both DMX order and 2D/3D monitor position. Falls
     *  back to sortedSelectedFixtures() (DMX order) when the selection spans
     *  multiple groups, or none. */
    QList<quint32> groupOrSortedSelectedFixtures() const;

    /** Pushes $deltaMeters as a live DMX value onto $fixture's PositionX/Y/Z
     *  channels (whichever of the three it actually defines - the others are
     *  no-ops), following the same dump-value/scene-value routing decision
     *  setPositionValue() uses for Pan/Tilt. $feature defaults to the normal
     *  2D/3D drag case; restorePersistedDmxTransforms() overrides it since
     *  it calls this to replay a persisted delta on project load rather than
     *  in response to a live drag. */
    void pushPositionDelta(Fixture *fixture, QVector3D deltaMeters,
                            GenericDMXSource::Feature feature = GenericDMXSource::DragPositionPush);

    /** Same as pushPositionDelta(), for $fixture's RotationX/Y/Z channels
     *  ($deltaDegrees). */
    void pushRotationDelta(Fixture *fixture, QVector3D deltaDegrees,
                            GenericDMXSource::Feature feature = GenericDMXSource::DragPositionPush);

    /** The position/rotation delta we last told $fixture's PositionX/Y/Z or
     *  RotationX/Y/Z channels to have, tracked in m_fixturePositionDeltaCache/
     *  m_fixtureRotationDeltaCache (keyed by fixture ID) instead of re-derived
     *  from Fixture::channelValueAt() on every call. Needed because
     *  channelValueAt() only reflects a pushPositionDelta()/pushRotationDelta()
     *  write once Doc's regular processing tick has run - a fast drag calls
     *  this far more often than that tick fires, so re-deriving "current
     *  delta" from the fixture on every increment reads a stale value and
     *  silently discards most of the increments that haven't been processed
     *  yet (reproduced as: dragging the fixture a visible distance only
     *  actually moves it a small fraction of that distance). Seeded from the
     *  fixture's actual current value the first time it's asked about; after
     *  that it's updated purely from what we ourselves pushed, making it the
     *  authoritative record of "what we told this fixture to be" rather than
     *  a read of the DMX output. */
    QVector3D cachedPositionDelta(quint32 fxID, Fixture *fixture) const;
    QVector3D cachedRotationDelta(quint32 fxID, Fixture *fixture) const;

    /** Drops $fxID's cached position/rotation delta (see cachedPositionDelta()/
     *  cachedRotationDelta() above) and re-renders its 2D/3D preview from a
     *  freshly re-derived one - called after changing this fixture's own
     *  invert/scale/range settings (setFixtureDmxTransformFlags()/
     *  setFixtureRotationScale()/setFixturePositionRange()),
     *  since those settings change what a given DMX value *means* without the
     *  DMX value itself changing, so the cache and the on-screen position/
     *  rotation would otherwise silently keep showing the pre-change result
     *  until the next unrelated universe write happened to invalidate it. */
    void refreshFixtureDmxTransform(quint32 fxID);

private:
    mutable QHash<quint32, QVector3D> m_fixturePositionDeltaCache;
    mutable QHash<quint32, QVector3D> m_fixtureRotationDeltaCache;

    /** The list of the currently selected Fixture item IDs */
    QList<quint32> m_selectedFixtures;

    /** The ID of the Fixture group currently selected via CTRL+Tab,
     *  or Function::invalidId() if none */
    quint32 m_currentFixtureGroupID;

    /** A flag indicating if a Function is currently being edited */
    bool m_editingEnabled;

    /** The number of generic dimmers currently selected */
    int m_selectedDimmersCount;

    /** A multihash containing the selected fixtures' capabilities by channel type */
    /** The hash is: int (channel type) , SceneValue (Fixture ID and channel) */
    QMultiHash<int, SceneValue> m_channelsMap;

    /*********************************************************************
     * DMX channels dump
     *********************************************************************/
public:
    /** Store a channel value for Scene dumping. $feature identifies the
     *  calling UI feature (see GenericDMXSource::Feature) for debugging
     *  purposes only - it has no effect when $output is false, since no
     *  GenericDMXSource::set() call happens in that case. */
    Q_INVOKABLE void setDumpValue(quint32 fxID, quint32 channel, uchar value, bool output = true,
                                   GenericDMXSource::Feature feature = GenericDMXSource::Unspecified);

    /** Remove a channel from the Scene dumping list */
    Q_INVOKABLE void unsetDumpValue(quint32 fxID, quint32 channel);

    /** Return the number of DMX channels currently available for dumping */
    int dumpValuesCount() const;

    /** Return the current DMX dump channel type mask */
    int dumpChannelMask() const;

    /** Dump the cached DMX channel to a new or an existing Scene,
     *  considering the options flagged in the DMX Dump popup */
    Q_INVOKABLE void dumpDmxChannels(quint32 channelMask, QString sceneName, int sceneID,
                                     bool allChannels, bool nonZeroOnly);

    /** Resets the current values used for dumping or preview */
    Q_INVOKABLE void resetDumpValues();

    /** Return a list only of the fixture IDs from the selected preview items */
    QList<quint32> selectedFixtureIDList() const;

signals:
    void dumpValuesCountChanged();
    void dumpChannelMaskChanged();

private:
    /** List of the values available for dumping to a Scene */
    QList <SceneValue> m_dumpValues;

    /** Bitmask representing the available channel types for
     *  the DMX channels ready for dumping */
    quint32 m_dumpChannelMask;

    /** Reference to a Generic DMX source used to handle Scenes dump */
    GenericDMXSource* m_source;
};

#endif // CONTEXTMANAGER_H
