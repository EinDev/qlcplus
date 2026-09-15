/*
  Q Light Controller Plus - Unit test
  showgroupmove_test.cpp

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

#include <QtTest>

#include "showgroupmove_test.h"
#include "showmovehelper.h"

static ShowClipSpan clip(quint32 id, qint64 start, qint64 duration)
{
    ShowClipSpan c;
    c.id = id;
    c.startTime = start;
    c.duration = duration;
    return c;
}

static ShowMoveItem item(quint32 id, int track, qint64 start, qint64 duration)
{
    ShowMoveItem i;
    i.id = id;
    i.trackIndex = track;
    i.startTime = start;
    i.duration = duration;
    return i;
}

void ShowGroupMove_Test::overlapsHalfOpen()
{
    QVERIFY(ShowMoveHelper::overlaps(0, 1000, 500, 1000));
    QVERIFY(ShowMoveHelper::overlaps(500, 1000, 0, 1000));
    QVERIFY(ShowMoveHelper::overlaps(100, 100, 0, 1000));   // contained
    // touching edges are legal
    QVERIFY(!ShowMoveHelper::overlaps(0, 1000, 1000, 1000));
    QVERIFY(!ShowMoveHelper::overlaps(1000, 1000, 0, 1000));
    QVERIFY(!ShowMoveHelper::overlaps(0, 1000, 5000, 1000));
}

void ShowGroupMove_Test::firstBlockerSkipsMoving()
{
    QList<ShowClipSpan> track;
    track << clip(1, 0, 1000) << clip(2, 2000, 1000);

    quint32 blocker = 0;
    QVERIFY(ShowMoveHelper::firstBlocker(track, 500, 200, QSet<quint32>(), &blocker));
    QCOMPARE(blocker, 1u);

    QVERIFY(!ShowMoveHelper::firstBlocker(track, 500, 200, QSet<quint32>() << 1, &blocker));
    QVERIFY(!ShowMoveHelper::firstBlocker(track, 1000, 1000, QSet<quint32>(), &blocker));
    QVERIFY(ShowMoveHelper::firstBlocker(track, 1000, 1001, QSet<quint32>(), &blocker));
    QCOMPARE(blocker, 2u);
}

void ShowGroupMove_Test::resolveFreeSpotUnchanged()
{
    QList<ShowClipSpan> track;
    track << clip(1, 0, 1000) << clip(2, 5000, 1000);

    QCOMPARE(ShowMoveHelper::resolveCollision(track, 2000, 1000, QSet<quint32>()), qint64(2000));
    // back-to-back with a neighbour is free too
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 1000, 4000, QSet<quint32>()), qint64(1000));
    // empty track
    QCOMPARE(ShowMoveHelper::resolveCollision(QList<ShowClipSpan>(), 1234, 10, QSet<quint32>()), qint64(1234));
}

void ShowGroupMove_Test::resolveNegativeClampedToZero()
{
    QCOMPARE(ShowMoveHelper::resolveCollision(QList<ShowClipSpan>(), -500, 1000, QSet<quint32>()), qint64(0));

    QList<ShowClipSpan> track;
    track << clip(1, 200, 1000);
    // clamped to 0, then overlaps the clip starting at 200: right of it (1200)
    // is the only option since there is no 1000ms gap left of it
    QCOMPARE(ShowMoveHelper::resolveCollision(track, -500, 1000, QSet<quint32>()), qint64(1200));
}

void ShowGroupMove_Test::resolvePrefersSmallerShift()
{
    QList<ShowClipSpan> track;
    track << clip(1, 5000, 2000);   // blocker [5000, 7000)

    // request [4500, 5500): left needs a 500 shift (to 4000), right needs 2500
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 4500, 1000, QSet<quint32>()), qint64(4000));
    // request [6500, 7500): right needs a 500 shift (to 7000), left needs 2500
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 6500, 1000, QSet<quint32>()), qint64(7000));
}

void ShowGroupMove_Test::resolveTieGoesLeft()
{
    QList<ShowClipSpan> track;
    track << clip(1, 5000, 2000);   // blocker [5000, 7000)

    // request [5500, 6500): left -> 4000 (shift 1500), right -> 7000 (shift 1500)
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 5500, 1000, QSet<quint32>()), qint64(4000));
}

void ShowGroupMove_Test::resolveWalksPastSeveralClips()
{
    // three clips with gaps too small for a 1000ms clip between them, a large
    // gap before the first one and free space after the last one
    QList<ShowClipSpan> track;
    track << clip(1, 3000, 1000)    // [3000, 4000)
          << clip(2, 4500, 1000)    // [4500, 5500)  gap 500
          << clip(3, 6000, 1000);   // [6000, 7000)  gap 500

    // request [4700, 5700) overlapping clip 2: the small gaps on both sides are
    // skipped; left gap [0, 3000) -> 2000 (shift 2700), right -> 7000 (shift 2300)
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 4700, 1000, QSet<quint32>()), qint64(7000));

    // request [3200, 4200) overlapping clip 1: left -> 2000 (shift 1200),
    // right -> 7000 (shift 3800)
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 3200, 1000, QSet<quint32>()), qint64(2000));

    // a 400ms clip does fit into the small gaps: immediately left of clip 2,
    // ending where it starts (4100, shift 600) beats right of it (5500, shift 800)
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 4700, 400, QSet<quint32>()), qint64(4100));
}

void ShowGroupMove_Test::resolveLeftGapAtZero()
{
    QList<ShowClipSpan> track;
    track << clip(1, 1000, 1000);   // [1000, 2000)

    // request [800, 1800): the left gap is exactly [0, 1000) -> lands at 0
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 800, 1000, QSet<quint32>()), qint64(0));

    // a 1200ms clip cannot go left (gap only 1000) -> right of the blocker
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 800, 1200, QSet<quint32>()), qint64(2000));
}

void ShowGroupMove_Test::resolveIgnoresMovingIds()
{
    QList<ShowClipSpan> track;
    track << clip(1, 0, 1000) << clip(2, 1000, 1000) << clip(3, 5000, 1000);

    // clip 2 is being moved: its old spot is not a blocker for anybody
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 1200, 500, QSet<quint32>() << 2), qint64(1200));
    // ...but clip 1 still is
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 800, 500, QSet<quint32>() << 2), qint64(1000));
    // several moving ids
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 100, 500, QSet<quint32>() << 1 << 2), qint64(100));
}

void ShowGroupMove_Test::resolveMergesTouchingBlockers()
{
    // two back-to-back clips form one block: no 0-length "gap" between them
    // may be reported as a landing spot
    QList<ShowClipSpan> track;
    track << clip(2, 2000, 1000) << clip(1, 1000, 1000);   // unsorted on purpose

    // request [1500, 1600): left -> 900 (shift 600), right -> 3000 (shift 1500)
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 1500, 100, QSet<quint32>()), qint64(900));
    // request [2500, 2600): right -> 3000 (shift 500)
    QCOMPARE(ShowMoveHelper::resolveCollision(track, 2500, 100, QSet<quint32>()), qint64(3000));
}

void ShowGroupMove_Test::groupMoveFree()
{
    QList<QList<ShowClipSpan>> tracks;
    tracks << (QList<ShowClipSpan>() << clip(1, 0, 1000) << clip(2, 3000, 1000))
           << (QList<ShowClipSpan>() << clip(3, 0, 1000) << clip(4, 6000, 1000));

    QList<ShowMoveItem> items;
    items << item(1, 0, 0, 1000) << item(3, 1, 0, 1000);

    ShowGroupMoveResult r = ShowMoveHelper::validateGroupMove(tracks, items, 0, 1500);
    QVERIFY(r.ok);
    QCOMPARE(r.trackDelta, 0);
    QCOMPARE(r.timeDelta, qint64(1500));
}

void ShowGroupMove_Test::groupMoveBlocked()
{
    QList<QList<ShowClipSpan>> tracks;
    tracks << (QList<ShowClipSpan>() << clip(1, 0, 1000) << clip(2, 3000, 1000))
           << (QList<ShowClipSpan>() << clip(3, 0, 1000) << clip(4, 2000, 1000));

    QList<ShowMoveItem> items;
    items << item(1, 0, 0, 1000) << item(3, 1, 0, 1000);

    // item 1 lands free at 1500 on track 0, item 3 hits clip 4 on track 1
    ShowGroupMoveResult r = ShowMoveHelper::validateGroupMove(tracks, items, 0, 1500);
    QVERIFY(!r.ok);
    QCOMPARE(r.blockingId, 4u);

    // moving one track down: item 1 -> track 1 at 1500 hits clip 4 too
    r = ShowMoveHelper::validateGroupMove(tracks, items, 1, 1500);
    QVERIFY(!r.ok);
    QCOMPARE(r.blockingId, 4u);
}

void ShowGroupMove_Test::groupMoveMovingItemsDoNotBlockEachOther()
{
    QList<QList<ShowClipSpan>> tracks;
    tracks << (QList<ShowClipSpan>() << clip(1, 0, 1000) << clip(2, 1000, 1000) << clip(3, 2000, 1000));

    // shifting the whole back-to-back row by half a clip: every item lands on
    // a spot currently occupied by a moving neighbour, which is legal
    QList<ShowMoveItem> items;
    items << item(1, 0, 0, 1000) << item(2, 0, 1000, 1000) << item(3, 0, 2000, 1000);

    ShowGroupMoveResult r = ShowMoveHelper::validateGroupMove(tracks, items, 0, 500);
    QVERIFY(r.ok);

    // but a clip that stays put still blocks
    tracks[0] << clip(4, 3200, 100);
    r = ShowMoveHelper::validateGroupMove(tracks, items, 0, 500);
    QVERIFY(!r.ok);
    QCOMPARE(r.blockingId, 4u);
}

void ShowGroupMove_Test::groupMoveClampsDeltas()
{
    QList<QList<ShowClipSpan>> tracks;
    tracks << (QList<ShowClipSpan>() << clip(1, 500, 1000))
           << (QList<ShowClipSpan>() << clip(2, 2000, 1000));

    QList<ShowMoveItem> items;
    items << item(1, 0, 500, 1000) << item(2, 1, 2000, 1000);

    // requesting -3 tracks / -5000 ms: the topmost item is on track 0 and the
    // earliest starts at 500, so the effective deltas are 0 / -500
    ShowGroupMoveResult r = ShowMoveHelper::validateGroupMove(tracks, items, -3, -5000);
    QVERIFY(r.ok);
    QCOMPARE(r.trackDelta, 0);
    QCOMPARE(r.timeDelta, qint64(-500));
}

void ShowGroupMove_Test::groupMoveNewTracks()
{
    QList<QList<ShowClipSpan>> tracks;
    tracks << (QList<ShowClipSpan>() << clip(1, 0, 1000) << clip(9, 4000, 1000))
           << (QList<ShowClipSpan>() << clip(2, 0, 1000));

    QList<ShowMoveItem> items;
    items << item(1, 0, 0, 1000) << item(2, 1, 0, 1000);

    // two tracks down: item 1 -> track 2, item 2 -> track 3, both new and empty
    ShowGroupMoveResult r = ShowMoveHelper::validateGroupMove(tracks, items, 2, 4000);
    QVERIFY(r.ok);
    QCOMPARE(r.trackDelta, 2);

    // one track down: item 1 -> track 1 (free at 4000), item 2 -> new track 2
    r = ShowMoveHelper::validateGroupMove(tracks, items, 1, 4000);
    QVERIFY(r.ok);

    // no track change: item 1 collides with clip 9 at 4000
    r = ShowMoveHelper::validateGroupMove(tracks, items, 0, 4000);
    QVERIFY(!r.ok);
    QCOMPARE(r.blockingId, 9u);
}

void ShowGroupMove_Test::groupMoveEmpty()
{
    ShowGroupMoveResult r = ShowMoveHelper::validateGroupMove(QList<QList<ShowClipSpan>>(),
                                                              QList<ShowMoveItem>(), 1, 1);
    QVERIFY(!r.ok);
}

QTEST_APPLESS_MAIN(ShowGroupMove_Test)
