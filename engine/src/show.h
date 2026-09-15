/*
  Q Light Controller Plus
  show.h

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

#ifndef SHOW_H
#define SHOW_H

#include <QSharedPointer>
#include <QAtomicInteger>
#include <QMutex>
#include <QList>
#include <QSet>

#include "showschedule.h"
#include "function.h"
#include "track.h"

class QXmlStreamReader;
class ShowRunner;

/** @addtogroup engine_functions Functions
 * @{
 */

class Show final : public Function
{
    Q_OBJECT
    Q_DISABLE_COPY(Show)

    /*********************************************************************
     * Initialization
     *********************************************************************/
public:
    Show(Doc* doc);
    virtual ~Show();

    /** @reimp */
    QIcon getIcon() const override;

    /** @reimp */
    quint32 totalDuration() override;

    /*********************************************************************
     * Copying
     *********************************************************************/
public:
    /** @reimp */
    Function* createCopy(Doc* doc, bool addToDoc = true) override;

    /** Copy the contents for this function from another function */
    bool copyFrom(const Function* function) override;

    /*********************************************************************
     * Time division
     *********************************************************************/
public:
    enum TimeDivision
    {
        Time = 0,
        BPM_4_4,
        BPM_3_4,
        BPM_2_4,
        Invalid
    };
    Q_ENUM(TimeDivision)

    /** Set the show time division type (Time, BPM) */
    void setTimeDivision(Show::TimeDivision type, int BPM);

    Show::TimeDivision timeDivisionType() const;
    void setTimeDivisionType(Show::TimeDivision type);
    int beatsDivision() const;

    int timeDivisionBPM() const;
    void setTimeDivisionBPM(int BPM);

    static QString tempoToString(Show::TimeDivision type);
    static Show::TimeDivision stringToTempo(const QString& tempo);

private:
    TimeDivision m_timeDivisionType;
    int m_timeDivisionBPM;

    /*********************************************************************
     * Tracks
     *********************************************************************/
public:
    /**
     * Add a track to this show. If the track is already a
     * member of the show, this call fails.
     *
     * @param id The track to add
     * @return true if successful, otherwise false
     */
    bool addTrack(Track *track, quint32 id = Track::invalidId());

    /**
     * Remove a track from this show. If the track is not a
     * member of the show, this call fails.
     *
     * @param id The track to remove
     * @return true if successful, otherwise false
     */
    bool removeTrack(quint32 id);

    /** Get a track by id */
    Track *track(quint32 id) const;

    /** Get a reference to a Track from the provided Scene ID */
    Track *getTrackFromSceneID(quint32 id) const;

    /** Get a reference to a Track from the provided ShowFunction ID */
    Track *getTrackFromShowFunctionID(quint32 id) const;

    /** Get the number of tracks in the Show */
    int getTracksCount() const;

    /** Move a track ID up or down */
    void moveTrack(Track *track, int direction);

    /** Get a list of available tracks */
    QList <Track*> tracks() const;

private:
    /** Create a new track ID */
    quint32 createTrackId();

protected:
    /** Map of the available tracks coupled by ID */
    QMap <quint32,Track*> m_tracks;

    /** Latest assigned track ID */
    quint32 m_latestTrackId;

    /*********************************************************************
     * Show Functions
     *********************************************************************/
public:
    /** Get a unique ID for the creation of a new ShowFunction */
    quint32 getLatestShowFunctionId();

    /** Get a reference to a ShowFunction from the provided uinique ID */
    ShowFunction *showFunction(quint32 id) const;

protected:
    /** Latest assigned unique ShowFunction ID */
    quint32 m_latestShowFunctionID;

    /*********************************************************************
     * Save & Load
     *********************************************************************/
public:
    /** Save function's contents to an XML document */
    bool saveXML(QXmlStreamWriter *doc) const override;

    /** Load function's contents from an XML document */
    bool loadXML(QXmlStreamReader &root) override;

    /** @reimp */
    void postLoad() override;

public:
    /** @reimp */
    bool contains(quint32 functionId) const override;

    /** @reimp */
    QList<quint32> components() const override;

    /*********************************************************************
     * Schedule
     *********************************************************************/
public:
    /**
     * Rebuild the immutable timeline snapshot (ShowSchedule) synchronously.
     *
     * Must be called on the thread owning this Show (the GUI thread): it walks
     * the live tracks and clips. Timeline edits (Track::changed) normally queue
     * one rebuild per event-loop turn; call this directly when a fresh snapshot
     * is needed right now, e.g. before starting playback or from tests.
     * Emits scheduleChanged().
     */
    void rebuildSchedule();

    /**
     * The most recently built snapshot, building one first if none exists
     * (or if the timeline is dirty and this is the owner thread). Consumed by
     * ShowRunner when it is created; also clears any pending snapshot.
     */
    QSharedPointer<const ShowSchedule> currentSchedule();

    /**
     * Hand out a snapshot rebuilt since the runner last looked, or null.
     * Called by ShowRunner on the MasterTimer thread once per tick.
     */
    QSharedPointer<const ShowSchedule> takePendingSchedule();

    /** True when the timeline changed since the last rebuildSchedule() */
    bool isScheduleDirty() const;

signals:
    /** Emitted after every rebuildSchedule() */
    void scheduleChanged();

private slots:
    void slotTrackChanged(quint32 trackId);
    void slotFunctionChanged(quint32 fid);
    void slotRebuildScheduleIfDirty();

private:
    /** Flag the schedule as stale and queue one rebuild on the event loop */
    void markScheduleDirty();

    /** Walk the live tracks/clips into a new snapshot (owner thread only) */
    QSharedPointer<const ShowSchedule> buildSchedule() const;

    /** Guards the four members below; shared by the GUI and MasterTimer threads */
    mutable QMutex m_scheduleMutex;
    QSharedPointer<const ShowSchedule> m_currentSchedule;
    QSharedPointer<const ShowSchedule> m_pendingSchedule;
    bool m_scheduleDirty;
    bool m_rebuildQueued;

    /*********************************************************************
     * Scrubbing
     *********************************************************************/
public:
    /**
     * Enter/leave scrub mode. While the Show runs in scrub mode its runner
     * is frozen: the playhead does not advance, every clip under it is
     * started (with no fade-in) and then held paused, Audio clips are
     * skipped, and the Show never ends on its own. The Show Manager uses
     * this to preview the state at the cursor while the Show is stopped:
     * start() the Show at the cursor with scrub mode on, requestSeek() on
     * every cursor move, and leave scrub mode to play on seamlessly.
     *
     * Thread-safe: the flag is consumed by the runner on the MasterTimer
     * thread at its next tick. Cleared automatically when the Show stops.
     */
    void setScrubMode(bool enable);
    bool isScrubMode() const;

    /**
     * Ask the runner to move the playhead to $ms. Requests posted between
     * two ticks coalesce into the last one. Thread-safe.
     */
    void requestSeek(quint32 ms);

    /** Runner side: take the pending seek request, if any, into $ms */
    bool takeSeekRequest(quint32 &ms);

private:
    QAtomicInteger<int> m_scrubMode;
    /** Pending seek position in ms, or -1 for none */
    QAtomicInteger<qint64> m_seekRequest;

    /*********************************************************************
     * Running
     *********************************************************************/
public:
    /** @reimp */
    void preRun(MasterTimer* timer) override;

    /** @reimp */
    void setPause(bool enable) override;

    /** @reimp */
    void write(MasterTimer* timer, QList<Universe*> universes) override;

    /** @reimp */
    void postRun(MasterTimer* timer, QList<Universe*> universes) override;

protected slots:
    /** Called whenever one of this function's child functions stops */
    void slotChildStopped(quint32 fid);

signals:
    void timeChanged(quint32);
    void showFinished();

protected:
    ShowRunner *m_runner;
    /** Number of currently running children */
    QSet <quint32> m_runningChildren;

    /*************************************************************************
     * Attributes
     *************************************************************************/
public:
    /** @reimp */
    int adjustAttribute(qreal fraction, int attributeId = 0) override;
};

/** @} */

#endif
