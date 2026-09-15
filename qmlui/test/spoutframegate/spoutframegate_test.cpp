/*
  Q Light Controller Plus - Unit test
  spoutframegate_test.cpp

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

#include <QtTest>

#include "spoutframegate_test.h"
#include "spoutframegate.h"

// Timestamps are QVideoFrame::startTime() values, i.e. microseconds.
static const qint64 kFrameUs = 33333; // ~30 fps

void SpoutFrameGate_Test::newestFrameIsAccepted()
{
    SpoutFrameGate gate;
    // the delivered frame is the one the sink currently holds
    QVERIFY(gate.accept(0, 0));
    QVERIFY(gate.accept(kFrameUs, kFrameUs));
    QVERIFY(gate.accept(5 * kFrameUs, 5 * kFrameUs));
    QCOMPARE(gate.acceptedCount(), quint64(3));
    QCOMPARE(gate.droppedCount(), quint64(0));
}

void SpoutFrameGate_Test::staleFrameIsDropped()
{
    SpoutFrameGate gate;
    // the sink already moved on to a newer frame while this delivery sat
    // in the event queue
    QVERIFY(gate.accept(kFrameUs, 2 * kFrameUs) == false);
    QVERIFY(gate.accept(0, kFrameUs) == false);
    QCOMPARE(gate.acceptedCount(), quint64(0));
    QCOMPARE(gate.droppedCount(), quint64(2));
}

void SpoutFrameGate_Test::queuedBurstCollapsesToTheNewestFrame()
{
    SpoutFrameGate gate;
    // Six frames were queued while the GUI thread was busy; by the time the
    // events are processed, the sink holds the sixth. Only that one costs a
    // conversion, the five older deliveries are dropped unread.
    const qint64 newest = 5 * kFrameUs;
    int rendered = 0;
    for (int i = 0; i <= 5; i++)
    {
        if (gate.accept(i * kFrameUs, newest))
            rendered++;
    }
    QCOMPARE(rendered, 1);
    QCOMPARE(gate.acceptedCount(), quint64(1));
    QCOMPARE(gate.droppedCount(), quint64(5));
}

void SpoutFrameGate_Test::framesWithoutTimestampsAreAlwaysAccepted()
{
    SpoutFrameGate gate;
    // no timestamp on the delivered frame, or none on the sink's: nothing to
    // judge by, never drop (a stream could otherwise lose every frame)
    QVERIFY(gate.accept(-1, -1));
    QVERIFY(gate.accept(-1, 3 * kFrameUs));
    QVERIFY(gate.accept(3 * kFrameUs, -1));
    QCOMPARE(gate.acceptedCount(), quint64(3));
    QCOMPARE(gate.droppedCount(), quint64(0));
}

void SpoutFrameGate_Test::loopRestartAndBackwardSeekAreNotDrops()
{
    SpoutFrameGate gate;
    // Loop: the last frame of the previous pass is delivered while the sink
    // already holds frame 0 of the next pass. Backward seek: a pre-seek
    // frame arrives while the sink holds the (earlier) seek target. Neither
    // frame is stale in the queue sense - the newest one is simply earlier
    // in the clip - so both are rendered; the next delivery is the newest.
    QVERIFY(gate.accept(900 * kFrameUs, 0));
    QVERIFY(gate.accept(600 * kFrameUs, 100 * kFrameUs));
    QCOMPARE(gate.droppedCount(), quint64(0));
}

void SpoutFrameGate_Test::countersFollowDecisionsAndReset()
{
    SpoutFrameGate gate;
    QCOMPARE(gate.acceptedCount(), quint64(0));
    QCOMPARE(gate.droppedCount(), quint64(0));

    gate.accept(0, 0);
    gate.accept(0, kFrameUs);
    gate.accept(kFrameUs, kFrameUs);
    QCOMPARE(gate.acceptedCount(), quint64(2));
    QCOMPARE(gate.droppedCount(), quint64(1));

    gate.reset();
    QCOMPARE(gate.acceptedCount(), quint64(0));
    QCOMPARE(gate.droppedCount(), quint64(0));

    // still decides the same way after a reset
    QVERIFY(gate.accept(2 * kFrameUs, 2 * kFrameUs));
    QVERIFY(gate.accept(kFrameUs, 2 * kFrameUs) == false);
}

QTEST_APPLESS_MAIN(SpoutFrameGate_Test)
