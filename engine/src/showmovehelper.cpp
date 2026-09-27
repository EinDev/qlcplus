/*
  Q Light Controller Plus
  showmovehelper.cpp

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

#include <algorithm>
#include <limits>

#include "showmovehelper.h"

bool ShowMoveHelper::overlaps(qint64 aStart, qint64 aDuration, qint64 bStart, qint64 bDuration)
{
    return aStart < bStart + bDuration && bStart < aStart + aDuration;
}

bool ShowMoveHelper::firstBlocker(const QList<ShowClipSpan> &track, qint64 startTime, qint64 duration,
                                  const QSet<quint32> &movingIds, quint32 *blockingId)
{
    for (const ShowClipSpan &clip : track)
    {
        if (movingIds.contains(clip.id))
            continue;

        if (overlaps(startTime, duration, clip.startTime, clip.duration))
        {
            if (blockingId != nullptr)
                *blockingId = clip.id;
            return true;
        }
    }

    return false;
}

qint64 ShowMoveHelper::resolveCollision(const QList<ShowClipSpan> &track, qint64 startTime, qint64 duration,
                                        const QSet<quint32> &movingIds)
{
    if (startTime < 0)
        startTime = 0;

    if (firstBlocker(track, startTime, duration, movingIds) == false)
        return startTime;

    // Merge the non-moving clips into sorted, disjoint blocked intervals.
    // Clips on a track never overlap in a consistent Show, but touching or
    // (after an external edit) overlapping ones must still form one block.
    QList<ShowClipSpan> blockers;
    for (const ShowClipSpan &clip : track)
    {
        if (!movingIds.contains(clip.id))
            blockers.append(clip);
    }

    std::sort(blockers.begin(), blockers.end(),
              [](const ShowClipSpan &a, const ShowClipSpan &b) { return a.startTime < b.startTime; });

    QList<QPair<qint64, qint64>> blocked; // (start, end)
    for (const ShowClipSpan &clip : blockers)
    {
        qint64 end = clip.startTime + clip.duration;
        if (!blocked.isEmpty() && clip.startTime <= blocked.last().second)
            blocked.last().second = qMax(blocked.last().second, end);
        else
            blocked.append(qMakePair(clip.startTime, end));
    }

    // Free gaps: [0, first.start), between consecutive blocks, and the open
    // tail after the last block. For every gap that fits the clip, the best
    // placement is the request clamped into the gap; the smallest shift wins,
    // earlier position on a tie.
    qint64 best = -1;
    qint64 bestShift = 0;

    auto consider = [&](qint64 gapStart, qint64 gapEnd /* -1 = unbounded */)
    {
        if (gapEnd >= 0 && gapEnd - gapStart < duration)
            return;

        qint64 pos = qMax(startTime, gapStart);
        if (gapEnd >= 0)
            pos = qMin(pos, gapEnd - duration);

        qint64 shift = qAbs(pos - startTime);
        if (best < 0 || shift < bestShift || (shift == bestShift && pos < best))
        {
            best = pos;
            bestShift = shift;
        }
    };

    qint64 gapStart = 0;
    for (const QPair<qint64, qint64> &block : blocked)
    {
        consider(gapStart, block.first);
        gapStart = block.second;
    }
    consider(gapStart, -1);

    return best;
}

ShowGroupMoveResult ShowMoveHelper::validateGroupMove(const QList<QList<ShowClipSpan>> &tracks,
                                                      const QList<ShowMoveItem> &items,
                                                      int trackDelta, qint64 timeDelta)
{
    ShowGroupMoveResult result;

    if (items.isEmpty())
        return result;

    QSet<quint32> movingIds;
    int minTrack = items.first().trackIndex;
    qint64 minStart = items.first().startTime;

    for (const ShowMoveItem &item : items)
    {
        movingIds.insert(item.id);
        minTrack = qMin(minTrack, item.trackIndex);
        minStart = qMin(minStart, item.startTime);
    }

    // clamp so the group never leaves the timeline at the top/left
    result.trackDelta = qMax(trackDelta, -minTrack);
    result.timeDelta = qMax(timeDelta, -minStart);

    for (const ShowMoveItem &item : items)
    {
        int dstTrack = item.trackIndex + result.trackDelta;
        if (dstTrack >= tracks.count())
            continue; // a new, empty track

        quint32 blockingId = 0;
        if (firstBlocker(tracks.at(dstTrack), item.startTime + result.timeDelta, item.duration,
                         movingIds, &blockingId))
        {
            result.blockingId = blockingId;
            return result;
        }
    }

    result.ok = true;
    return result;
}

ShowGroupMoveResult ShowMoveHelper::planPaste(const QList<QList<ShowClipSpan>> &tracks,
                                              const QList<ShowMoveItem> &items, qint64 anchorTime)
{
    ShowGroupMoveResult result;

    if (items.isEmpty())
        return result;

    // the earliest item is the one anchored at the cursor (first one on a tie)
    const ShowMoveItem *earliest = &items.first();
    for (const ShowMoveItem &item : items)
    {
        if (item.startTime < earliest->startTime)
            earliest = &item;
    }

    // no clip is exempt from blocking the copies, not even their sources:
    // validateGroupMove() exempts the ids of the items it moves, so give
    // the copies an id no real clip has
    QList<ShowMoveItem> copies = items;
    for (ShowMoveItem &copy : copies)
        copy.id = std::numeric_limits<quint32>::max();

    qint64 requested = qMax<qint64>(0, anchorTime);
    qint64 resolved = requested;
    if (earliest->trackIndex >= 0 && earliest->trackIndex < tracks.count())
        resolved = resolveCollision(tracks.at(earliest->trackIndex), requested, earliest->duration,
                                    QSet<quint32>());

    return validateGroupMove(tracks, copies, 0, resolved - earliest->startTime);
}
