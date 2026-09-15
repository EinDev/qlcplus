/*
  Q Light Controller Plus
  showmanager.h

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

#ifndef SHOWMANAGER_H
#define SHOWMANAGER_H

#include <QObject>
#include <QQuickItem>

#include "previewcontext.h"
#include "show.h"
#include "showmovehelper.h"

class Doc;
class Track;
class Function;
class Chaser;
class ShowFunction;
class WaveformImageProvider;

typedef struct
{
    quint32 m_trackIndex;
    ShowFunction *m_showFunc;
    QQuickItem *m_item;
} SelectedShowItem;

class ShowManager final : public PreviewContext
{
    Q_OBJECT

    Q_PROPERTY(int currentShowID READ currentShowID WRITE setCurrentShowID NOTIFY currentShowIDChanged)
    Q_PROPERTY(bool isEditing READ isEditing NOTIFY isEditingChanged)
    Q_PROPERTY(QString showName READ showName WRITE setShowName NOTIFY showNameChanged)
    Q_PROPERTY(QColor itemsColor READ itemsColor WRITE setItemsColor NOTIFY itemsColorChanged)

    Q_PROPERTY(bool stretchFunctions READ stretchFunctions WRITE setStretchFunctions NOTIFY stretchFunctionsChanged)
    Q_PROPERTY(bool gridEnabled READ gridEnabled WRITE setGridEnabled NOTIFY gridEnabledChanged)
    Q_PROPERTY(double snapGuideX READ snapGuideX WRITE setSnapGuideX NOTIFY snapGuideXChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY isPausedChanged)
    Q_PROPERTY(bool previewEnabled READ previewEnabled WRITE setPreviewEnabled NOTIFY previewEnabledChanged)
    Q_PROPERTY(bool isPreviewing READ isPreviewing NOTIFY isPreviewingChanged)
    Q_PROPERTY(int showDuration READ showDuration NOTIFY showDurationChanged)

    Q_PROPERTY(Show::TimeDivision timeDivision READ timeDivision WRITE setTimeDivision NOTIFY timeDivisionChanged)
    Q_PROPERTY(int beatsDivision READ beatsDivision NOTIFY beatsDivisionChanged)
    Q_PROPERTY(int timeDivisionBPM READ timeDivisionBPM WRITE setTimeDivisionBPM NOTIFY timeDivisionBPMChanged)
    Q_PROPERTY(float timeScale READ timeScale WRITE setTimeScale NOTIFY timeScaleChanged)
    Q_PROPERTY(float tickSize READ tickSize NOTIFY tickSizeChanged)
    Q_PROPERTY(int currentTime READ currentTime WRITE setCurrentTime NOTIFY currentTimeChanged)

    Q_PROPERTY(QVariant tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(int selectedTrackId READ selectedTrackId WRITE setSelectedTrackId NOTIFY selectedTrackIdChanged)
    Q_PROPERTY(int selectedItemsCount READ selectedItemsCount NOTIFY selectedItemsCountChanged)
    Q_PROPERTY(int clipboardItemsCount READ clipboardItemsCount NOTIFY clipboardItemsCountChanged)
    Q_PROPERTY(bool multipleSelection READ multipleSelection WRITE setMultipleSelection NOTIFY multipleSelectionChanged)

public:
    explicit ShowManager(QQuickView *view, Doc *doc, QObject *parent = 0);

    void initialize();

    /** Return the ID of the Show Function being edited */
    int currentShowID() const;

    /** Return a reference of the Show currently being edited */
    Show *currentShow() const;

    /** Flag to indicate if a Show is currently being edited */
    bool isEditing() const;

    /** Set the ID of the Show Function to edit */
    void setCurrentShowID(int currentShowID);

    /** Return the name of the Show Function being edited */
    QString showName() const;

    /** Set the name of the Show Function to edit */
    void setShowName(QString showName);

    /** Reset the Show Manager contents to an initial state */
    Q_INVOKABLE void resetContents();

    /** Clear all the current items in the ShowManager view */
    Q_INVOKABLE void resetView();

    /** Request to render the current Show items on screen */
    Q_INVOKABLE void renderView(QQuickItem *parent);

    Q_INVOKABLE void enableFlicking(bool enable);

    /** Return the current Show total duration in milliseconds */
    int showDuration() const;

    /** Get/Set the Function stretch flag */
    bool stretchFunctions() const;
    void setStretchFunctions(bool stretchFunctions);

    /** Get/Set the grid snapping functionality */
    bool gridEnabled() const;
    void setGridEnabled(bool gridEnabled);

    /** Get/Set the X position of the snap guide line (-1 = hidden) */
    double snapGuideX() const;
    void setSnapGuideX(double snapGuideX);

    /** Play or resume the Show playback */
    Q_INVOKABLE void playShow();

    /** Stop or rewind the Show playback */
    Q_INVOKABLE void stopShow();

    /** Flag that indicates if the Show is currently being played */
    bool isPlaying() const;

    /** Flag that indicates if the Show playback is currently paused */
    bool isPaused() const;

    /**
     * Get/Set whether moving the cursor while the Show is stopped previews
     * the Show's state at the cursor: every clip under it renders on the
     * real output (fixtures via DMX/2D/3D, a Video's frame at the in-clip
     * offset), Audio stays silent. Implemented with Show::setScrubMode.
     */
    bool previewEnabled() const;
    void setPreviewEnabled(bool enable);

    /** True while the Show runs frozen at the cursor for the preview */
    bool isPreviewing() const;

    /** @reimp - leaving the Show Manager stops the preview */
    void enableContext(bool enable) override;

signals:
    void currentShowIDChanged(int currentShowID);
    void isEditingChanged();
    void showNameChanged(QString showName);
    void stretchFunctionsChanged(bool stretchFunction);
    void gridEnabledChanged(bool gridEnabled);
    void snapGuideXChanged();
    void isPlayingChanged(bool playing);
    void isPausedChanged(bool paused);
    void previewEnabledChanged(bool enabled);
    void isPreviewingChanged(bool previewing);
    void showDurationChanged(int showDuration);

private:
    void setPlaybackState(bool playing, bool paused);

    /** Start the preview frozen at $time, or seek a running one to it.
     *  Nothing happens while the Show plays or is paused. */
    void previewAt(int time);

    /** Stop the preview if one is running; the cursor stays put */
    void stopPreview();

    void setPreviewing(bool previewing);

    /** Track if cursor is interactively being moved during pause */
    bool m_cursorMovedDuringPause;

    /** Cached playback state for immediate UI updates */
    bool m_isPlaying;
    bool m_isPaused;

    /** See previewEnabled() / isPreviewing() */
    bool m_previewEnabled;
    bool m_isPreviewing;

    /** A reference to the Show Function being edited */
    Show *m_currentShow;

    /** Flag that indicates if a Function should be stretched
     *  when the corresponding Show Item duration changes */
    bool m_stretchFunctions;

    /** Flag that indicates if the Show items should be
     *  snapped to the closest grid divisor */
    bool m_gridEnabled;

    /** X position of the snap guide line (-1 = hidden) */
    double m_snapGuideX;

    /*********************************************************************
      * Time
      ********************************************************************/
public:
    /** Get/Set the Show time division */
    Show::TimeDivision timeDivision() const;
    void setTimeDivision(Show::TimeDivision division);
    int beatsDivision() const;

    /** Get/Set the current Show's per-Show BPM used for Beats-mode calculations */
    int timeDivisionBPM() const;
    void setTimeDivisionBPM(int BPM);

    /** Get/Set the current time scale of the Show Manager timeline */
    float timeScale() const;
    void setTimeScale(float timeScale);

    /** Get the size in pixels of the Show header time division */
    float tickSize() const;

    /** Get/Set the current time of the Show (aka cursor position) */
    int currentTime() const;
    void setCurrentTime(int currentTime);

signals:
    void timeDivisionChanged(Show::TimeDivision division);
    void beatsDivisionChanged(int beatsDivision);
    void timeDivisionBPMChanged(int BPM);
    void timeScaleChanged(float timeScale);
    void tickSizeChanged(float tickSize);
    void currentTimeChanged(int currentTime);

private:
    /** The current time scale of the Show Manager timeline */
    float m_timeScale;

    /** Size in pixels of the Show Manager time division */
    float m_tickSize;

    /** The current time position of the Show in ms */
    int m_currentTime;

    /*********************************************************************
      * Tracks
      ********************************************************************/
public:
    /** Return a list of Track objects suitable for QML */
    QVariant tracks() const;

    /** Get/Set the selected track id */
    int selectedTrackId() const;
    void setSelectedTrackId(int id);

    Q_INVOKABLE void setTrackSolo(int index, bool solo);

    /** Move the track with the provided index in the provided direction */
    Q_INVOKABLE void moveTrack(int index, int direction);

    /** Delete the currently selected Show Track */
    Q_INVOKABLE void deleteSelectedTrack();

signals:
    void tracksChanged();
    void selectedTrackIdChanged(int id);

private:
    /** The index of the currently selected track */
    int m_selectedTrackId;

    /*********************************************************************
      * Show Items
      ********************************************************************/
public:
    /**
     * This enumeration instructs the UI how to interpret the data
     * stored in what previewData returns. It is a numeric
     * prefix before the time value
     */
    enum PreviewDrawType
    {
        RepeatingDuration = 0,
        FadeIn,
        StepDivider,
        FadeOut,
        AudioData
    };
    Q_ENUM(PreviewDrawType)

    /** Return the currently selected color for Show Items */
    QColor itemsColor() const;

    /** Set the color of the currently selected Show Items */
    void setItemsColor(QColor itemsColor);

    /** Add a new Item to the timeline.
     *  This happens when dragging an existing Function from the Function Manager.
     *  If the current Show is NULL, a new Show is created.
     *  If the provided $trackIdx is not valid, a new Track is created.
     *  If $sourceFunc is not NULL (e.g. when pasting), the created ShowFunction
     *  inherits its duration, color and lock state from it.
     */
    Q_INVOKABLE void addItems(QQuickItem *parent, int trackIdx, int startTime, QVariantList idsList,
                              ShowFunction *sourceFunc = nullptr);

    /** Add a Show item from an existing ShowFunction reference and Track Id */
    void addShowItem(ShowFunction *sf, quint32 trackId);

    /** Delete the currently selected show items */
    Q_INVOKABLE void deleteShowItems(QVariantList data);

    /** Delete the item referencing the provided ShowFunction from the QML view */
    void deleteShowItem(ShowFunction *sf);

    /** Rebuild the whole timeline from the current Show contents.
      * Used when Tracks are added/removed outside of the normal UI flow,
      * for example by an undo/redo action */
    void refreshView();

    /** Method invoked when moving an existing Show Item on the timeline.
     *  Equivalent to checkAndMoveItems() with $sf as the only moving item:
     *  the requested spot is grid-snapped (unless $itemSnapped) and, when it
     *  overlaps another clip, shifted to the nearest free spot on that track
     *  (see ShowMoveHelper::resolveCollision). A $newTrackIdx past the last
     *  track creates a new track and lands the item there.
     *  Returns the index of the track the item actually ended up on, which the
     *  UI must adopt as its row (it can differ from $newTrackIdx), or -1 if
     *  the move was refused.
     */
    Q_INVOKABLE int checkAndMoveItem(ShowFunction *sf, int newTrackIdx,
                                     int newStartTime, bool itemSnapped = false);

    /** Move every ShowFunction in $sfRefs as a group. $grabbed is the item
     *  the user is dragging: its requested landing spot ($newTrackIdx,
     *  $newStartTime) is grid-snapped (unless $itemSnapped) and collision-
     *  resolved on the target track, and the resulting track/time delta is
     *  applied to every item of the group. Locked items are left out of the
     *  group (and keep acting as blockers). All-or-nothing: if any item of
     *  the group would then still overlap a clip outside the group, nothing
     *  moves and -1 is returned. Otherwise the tracks the group needs below
     *  the last one are created, every item is moved (one Tardis undo step
     *  for the whole group) and $grabbed's new track index is returned. */
    Q_INVOKABLE int checkAndMoveItems(QVariantList sfRefs, ShowFunction *grabbed, int newTrackIdx,
                                      int newStartTime, bool itemSnapped = false);

    /** Live drag feedback for checkAndMoveItems(): computes the very same
     *  move the drop would perform, shows a landing preview on every other
     *  item of the group and returns a map with:
     *   - "ok": whether the drop would be accepted
     *   - "trackDelta" / "timeDelta": the effective deltas (ms) for the group
     *   - "shifted": true when collision resolution moved $grabbed away from
     *     the requested spot
     *   - "blockingItem": name of the clip refusing the drop ("" if ok) */
    Q_INVOKABLE QVariantMap previewItemsMove(QVariantList sfRefs, ShowFunction *grabbed, int newTrackIdx,
                                             int newStartTime, bool itemSnapped = false);

    /** Hide the landing previews previewItemsMove() put on the group */
    Q_INVOKABLE void clearItemsMovePreview(QVariantList sfRefs, ShowFunction *grabbed);

    /** Move $sf to the Track with id $trackId, keeping the QML item and the
     *  selection in sync. Used by Tardis to undo/redo cross-track moves. */
    void moveShowItemToTrack(ShowFunction *sf, quint32 trackId);

    /** Set the start time of a ShowFunction item (if not overlapping) */
    Q_INVOKABLE bool setShowItemStartTime(ShowFunction *sf, int startTime);

    /** Set the duration of a ShowFunction item (if not overlapping) */
    Q_INVOKABLE bool setShowItemDuration(ShowFunction *sf, int duration);

    /** Insert a time segment in a ShowFunction item, applying type-specific rules */
    Q_INVOKABLE bool insertShowItemTime(ShowFunction *sf, int length);

    /** Cut a time segment from a ShowFunction item, applying type-specific rules */
    Q_INVOKABLE bool cutShowItemTime(ShowFunction *sf, int length);

    /** Insert time at cursor position for all the items covering that position */
    Q_INVOKABLE bool insertTimeAtCursor(int length, int cursorTime);

    /** Cut time at cursor position for all the items covering that position */
    Q_INVOKABLE bool cutTimeAtCursor(int length, int cursorTime);

    /** Returns pixel X positions of all item edges (start + end) across all tracks,
     *  excluding the item with the given function ID */
    Q_INVOKABLE QVariantList getSnapEdges(quint32 excludeFuncId,
                                          double viewportLeft = -1, double viewportRight = -1) const;

    /** Returns the number of the currently selected Show items */
    int selectedItemsCount() const;

    /** Returns the number of Show items currently in the clipboard */
    int clipboardItemsCount() const;

    /** Get/Set multi selection mode for Show items */
    bool multipleSelection() const;
    void setMultipleSelection(bool multipleSelection);

    /** Add an item to the selection tracking list */
    Q_INVOKABLE void setItemSelection(int trackIdx, ShowFunction *sf, QQuickItem *item, bool selected, int keyModifiers);

    /** Selection change for a click on a Show item, following the usual
     *  desktop rules: a plain click selects only that item, Ctrl toggles it
     *  in the selection, Shift selects every item between the last plain/
     *  Ctrl-clicked item and this one when both are on the same track (else
     *  it just adds this one). */
    Q_INVOKABLE void selectItemByClick(int trackIdx, ShowFunction *sf, QQuickItem *item, int keyModifiers);

    /** Select every item whose geometry (timeline content coordinates)
     *  intersects the given rectangle. With $add false the previous
     *  selection is replaced, otherwise extended. */
    Q_INVOKABLE void selectItemsInRect(qreal x, qreal y, qreal width, qreal height, bool add);

    /** Select every item of the current Show */
    Q_INVOKABLE void selectAllItems();

    /** Deselect all the selected items at once */
    Q_INVOKABLE void resetItemsSelection();

    Q_INVOKABLE QVariantList selectedItemRefs() const;
    Q_INVOKABLE QStringList selectedItemNames() const;

    /** Returns true if at least one of the selected items is locked */
    Q_INVOKABLE bool selectedItemsLocked() const;

    /** Lock/Unlock all the currently selected items */
    Q_INVOKABLE void setSelectedItemsLock(bool lock);

    /**
     * Returns an array of values coupled as: PreviewDrawType, time value
     * The UI will render the lines according to their time value and their type
     */
    Q_INVOKABLE QVariantList previewData(Function *f) const;

    /** Returns beat-marker offsets in ms, measured from the START OF THE AUDIO FILE
     *  (i.e. NOT yet offset by any ShowFunction's startTime - callers combine this
     *  with a specific item's own startTime/duration), for every beat from k=0 up
     *  to f->totalDuration(). Empty list if f is not an Audio Function, or its BPM
     *  analysis is not Done, or detectedBpm() <= 0. */
    Q_INVOKABLE QVariantList beatGridData(Function *f) const;

    Q_INVOKABLE void copyToClipboard();
    Q_INVOKABLE void pasteFromClipboard();

    /*********************************************************************
      * Legacy beat-pseudo-count timeline conversion (ADR 0001 decision 4)
      ********************************************************************/
public:
    /** Return { name, bpm, beatsDivision, itemCount } for the given Show,
     *  used to prefill the conversion dialog. bpm/beatsDivision come from
     *  the Show's own current settings, not the global BPM - the ADR is
     *  explicit these are only a starting guess, editable by the user. */
    Q_INVOKABLE QVariantMap legacyShowConversionInfo(int showId) const;

    /** Return a list of { name, oldStart, newStart, oldDuration, newDuration }
     *  for a sample of the Show's items (all of them if there are few),
     *  applying the given $bpmNumber to the legacy beat-pseudo-count formula,
     *  without changing anything - used for the dialog's before/after preview. */
    Q_INVOKABLE QVariantList legacyShowConversionPreview(int showId, int bpmNumber) const;

    /** Convert every ShowFunction in the given Show from the legacy
     *  beat-pseudo-count encoding to real milliseconds, using $bpmNumber.
     *  Goes through Tardis exactly like every other ShowFunction start/duration
     *  change in this class, so it is a normal, undoable operation. */
    Q_INVOKABLE bool convertLegacyBeatShow(int showId, int bpmNumber);

private:
    /** ms represented by one legacy "beat-pseudo-count" unit (the old
     *  encoding stored beatCount * 1000) - the exact inverse of
     *  TimingUtils.qml's msToBeatPseudo(). Returns 0 for bpmNumber <= 0. */
    static double legacyBeatPseudoUnitToMs(int bpmNumber);

protected slots:
    void slotTimeChanged(quint32 msec_time);
    void slotShowFinished();
    void slotShowStopped();
    /** The Show rebuilt its timeline snapshot: its total duration may have changed */
    void slotScheduleChanged();

private:
    // Timeline mapping helpers
    int minimumTimelineDuration(Show::TimeDivision division) const;
    quint32 itemRelativeTimeFromCursor(const ShowFunction *sf, int cursorTime) const;
    quint32 mapCursorToChaserTime(const ShowFunction *sf, Chaser *chaser, int cursorTime) const;

    // Chaser-specific helpers
    quint32 chaserStepDuration(Chaser *chaser, int index) const;
    int chaserStepIndexFromTime(Chaser *chaser, quint32 timeValue) const;
    bool setChaserStepDurationWithUndo(Chaser *chaser, int stepIndex, quint32 newDuration);
    void convertChaserCommonToPerStep(Chaser *chaser);

    // Timeline mutation helpers
    void setShowItemDurationWithUndo(ShowFunction *sf, int newDuration);
    bool moveAllItemsAfterCursor(int cursorTime, int delta);

    bool insertShowItemTimeAt(ShowFunction *sf, int length, int cursorTime);
    bool cutShowItemTimeAt(ShowFunction *sf, int length, int cursorTime);

    /** Check items overlapping for the given track, ShowFunction,
     *  start time and duration. Returns true if overlapping is
     *  detected, otherwise false */
    bool checkOverlapping(Track *track, ShowFunction *sourceFunc,
                          quint32 startTime, quint32 duration) const;

signals:
    void itemsColorChanged(QColor itemsColor);
    void selectedItemsCountChanged(int count);
    void clipboardItemsCountChanged(int count);
    void multipleSelectionChanged();

private:
    /** Everything checkAndMoveItems()/previewItemsMove() need to agree on */
    struct GroupMovePlan
    {
        bool ok = false;
        int trackDelta = 0;
        qint64 timeDelta = 0;
        bool shifted = false;
        QString blockingName;
        /** the (unlocked) items taking part, with their current track index */
        QList<ShowFunction *> items;
        QList<int> trackIndices;
    };

    /** Shared by preview and drop: grid-snap and collision-resolve $grabbed's
     *  requested spot, derive the group delta and validate the whole group */
    GroupMovePlan planGroupMove(const QVariantList &sfRefs, ShowFunction *grabbed,
                                int newTrackIdx, int newStartTime, bool itemSnapped) const;

    /** Round $startTime to the grid, unless disabled or the item is already
     *  snapped to another item's edge */
    int snapStartTimeToGrid(int startTime, bool itemSnapped) const;

    /** Every track's clips as plain spans, indexed like Show::tracks() */
    QList<QList<ShowClipSpan>> trackSpans() const;

    /** Milliseconds to timeline pixels, in the current time division */
    double msToPx(double ms) const;

    /** Selection bookkeeping without notifications; return true if changed */
    bool addToSelection(int trackIdx, ShowFunction *sf, QQuickItem *item);
    bool removeFromSelection(ShowFunction *sf);
    bool clearSelection();
    bool isSelected(ShowFunction *sf) const;

    /** The background color for Show Items */
    QColor m_itemsColor;

    /** Pre-cached QML component for quick item creation */
    QQmlComponent *siComponent;

    /** Holds the currently selected Show items */
    QList<SelectedShowItem> m_selectedItems;

    /** ShowFunction id of the last plain/Ctrl-clicked item: the anchor of a
     *  Shift-click range selection (an id, not a pointer, since the item may
     *  have been deleted since) */
    quint32 m_selectionAnchorId;

    /** Flag to enable multi selection in Show items */
    bool m_multipleSelection;

    /** Holds the item currently ready for pasting */
    QList<SelectedShowItem> m_clipboard;

    WaveformImageProvider *m_waveformProvider;
};

#endif // SHOWMANAGER_H
