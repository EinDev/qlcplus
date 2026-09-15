/*
  Q Light Controller Plus
  showmovehelper.h

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

#ifndef SHOWMOVEHELPER_H
#define SHOWMOVEHELPER_H

#include <QList>
#include <QSet>
#include <QtGlobal>

/**
 * Pure, engine-free placement logic for Show Manager drag & drop.
 *
 * ShowManager (which needs a live QQuickView and a Doc) converts its Tracks
 * into plain ShowClipSpan lists and delegates every "where does this land /
 * is this drop legal" decision to the static methods here, so the rules can
 * be unit tested without a view (see qmlui/test/showgroupmove).
 *
 * All times are milliseconds. Intervals are half-open: two clips whose edges
 * merely touch do NOT overlap (snapping deliberately places clips
 * back-to-back).
 */

/** One clip on a track, identified by its ShowFunction id */
struct ShowClipSpan
{
    quint32 id;
    qint64 startTime;
    qint64 duration;
};

/** One clip taking part in a (group) move, with its current position */
struct ShowMoveItem
{
    quint32 id;
    int trackIndex;
    qint64 startTime;
    qint64 duration;
};

struct ShowGroupMoveResult
{
    /** true when every item lands on a free spot */
    bool ok = false;
    /** effective deltas: the requested ones, clamped so no item lands on a
     *  negative track or at a negative time */
    int trackDelta = 0;
    qint64 timeDelta = 0;
    /** ShowFunction id of the first clip blocking the move (ok == false) */
    quint32 blockingId = 0;
};

class ShowMoveHelper
{
public:
    /** Half-open interval overlap test */
    static bool overlaps(qint64 aStart, qint64 aDuration, qint64 bStart, qint64 bDuration);

    /** Id of the first clip on $track (skipping $movingIds) overlapping
     *  [$startTime, $startTime + $duration). Returns false if the spot is free. */
    static bool firstBlocker(const QList<ShowClipSpan> &track, qint64 startTime, qint64 duration,
                             const QSet<quint32> &movingIds, quint32 *blockingId = nullptr);

    /**
     * Nearest free start time for a clip of $duration requested at $startTime
     * on $track, ignoring every clip whose id is in $movingIds (the clips
     * being dragged must not block themselves).
     *
     * A negative request is clamped to 0 first. If the spot is free it is
     * returned unchanged. Otherwise the candidates are: immediately left of
     * the blocking clip, immediately right of it, and - when those spots are
     * themselves too small - every further gap outward in both directions,
     * down to t = 0 on the left and unbounded on the right. The candidate
     * with the smallest absolute shift from the request wins; on an exact
     * tie the earlier (left) position is chosen.
     */
    static qint64 resolveCollision(const QList<ShowClipSpan> &track, qint64 startTime, qint64 duration,
                                   const QSet<quint32> &movingIds);

    /**
     * Validate moving every item in $items by the same ($trackDelta,
     * $timeDelta). $tracks holds the clips of every existing track by index;
     * a target index past the end is a new, empty track. Moving items never
     * block each other (they keep their relative layout), only clips that
     * are not part of $items do. The deltas are clamped so that the earliest
     * item lands at t >= 0 and the topmost one on track >= 0; the clamped
     * values are returned and must be what the caller applies.
     */
    static ShowGroupMoveResult validateGroupMove(const QList<QList<ShowClipSpan>> &tracks,
                                                 const QList<ShowMoveItem> &items,
                                                 int trackDelta, qint64 timeDelta);

    /**
     * Where copies of $items (the clipboard, with their current positions)
     * land when pasted so that the earliest one starts at $anchorTime (the
     * cursor), every copy staying on its source's track at the same
     * relative time offset. Like a drop, the earliest copy's spot is first
     * collision-resolved on its track and the resulting delta applied to
     * the whole group, which must then be free of any blocker (ok == false
     * otherwise, with the first blocker's id).
     *
     * The copies do not exist yet, so nothing is exempt from blocking - in
     * particular the sources themselves: a copy pasted right onto its
     * source is shifted next to it, not laid over it. The ids in $items
     * are ignored for that reason. trackDelta is always 0.
     */
    static ShowGroupMoveResult planPaste(const QList<QList<ShowClipSpan>> &tracks,
                                         const QList<ShowMoveItem> &items, qint64 anchorTime);
};

#endif // SHOWMOVEHELPER_H
