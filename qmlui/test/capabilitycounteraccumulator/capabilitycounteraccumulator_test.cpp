/*
  Q Light Controller - Unit test
  capabilitycounteraccumulator_test.cpp

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

#include <QtTest/QtTest>

#include "capabilitycounteraccumulator.h"
#include "capabilitycounteraccumulator_test.h"

void CapabilityCounterAccumulator_Test::notDeferredPassesThroughImmediately()
{
    CapabilityCounterAccumulator acc;
    QCOMPARE(acc.isDeferred(), false);

    // not deferred: accumulate() must say "I didn't take this, apply it
    // yourself" so FixtureManager::updateCapabilityCounter()'s existing
    // immediate-apply path still runs, exactly as before this class existed.
    QCOMPARE(acc.accumulate("capPosition", 5), false);

    // nothing was ever deferred, so a flush() has nothing to report.
    QCOMPARE(acc.flush().isEmpty(), true);
}

void CapabilityCounterAccumulator_Test::deferredAccumulatesSameCapability()
{
    CapabilityCounterAccumulator acc;
    acc.setDeferred(true);

    QCOMPARE(acc.accumulate("capPosition", 1), true);
    QCOMPARE(acc.accumulate("capPosition", 1), true);
    QCOMPARE(acc.accumulate("capPosition", -1), true);

    // +1 +1 -1 nets to +1 - one entry, not three separate applications.
    QHash<QString, int> result = acc.flush();
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.value("capPosition"), 1);
}

void CapabilityCounterAccumulator_Test::netZeroDeltaIsOmittedFromFlush()
{
    CapabilityCounterAccumulator acc;
    acc.setDeferred(true);

    acc.accumulate("capBeam", 1);
    acc.accumulate("capBeam", -1);

    // selecting then immediately deselecting the same fixture during one
    // batch nets to zero - flush() must not even mention the capability,
    // since applying a zero delta would be a pointless QML write.
    QHash<QString, int> result = acc.flush();
    QCOMPARE(result.contains("capBeam"), false);
    QCOMPARE(result.isEmpty(), true);
}

void CapabilityCounterAccumulator_Test::distinctCapabilitiesAccumulateIndependently()
{
    CapabilityCounterAccumulator acc;
    acc.setDeferred(true);

    acc.accumulate("capPosition", 2);
    acc.accumulate("capColour", -3);
    acc.accumulate("capPosition", 1);

    QHash<QString, int> result = acc.flush();
    QCOMPARE(result.size(), 2);
    QCOMPARE(result.value("capPosition"), 3);
    QCOMPARE(result.value("capColour"), -3);
}

void CapabilityCounterAccumulator_Test::flushClearsStateAndTurnsDeferralOff()
{
    CapabilityCounterAccumulator acc;
    acc.setDeferred(true);
    acc.accumulate("capGobo", 4);

    QHash<QString, int> first = acc.flush();
    QCOMPARE(first.value("capGobo"), 4);

    // flush() must turn deferral off...
    QCOMPARE(acc.isDeferred(), false);

    // ...and a second flush() right after must have nothing left to give -
    // the pending map was actually cleared, not just copied.
    QCOMPARE(acc.flush().isEmpty(), true);

    // with deferral now off, a fresh accumulate() call goes straight back to
    // "apply it yourself" (false), not silently still deferring old state.
    QCOMPARE(acc.accumulate("capGobo", 1), false);
}

void CapabilityCounterAccumulator_Test::deferredBatchMatchesImmediateApplication()
{
    // the actual correctness property that matters: whatever the final
    // applied totals are after a deferred batch, they must equal what
    // applying every delta immediately, one at a time, would have produced.
    // Reference model: a plain hash summing deltas as they arrive, with no
    // deferral concept at all - simulates the pre-existing non-batched path.
    const QList<QPair<QString, int>> events = {
        { "capPosition", 1 }, { "capColour", 1 }, { "capPosition", 1 },
        { "capBeam", -1 },    { "capColour", -1 }, { "capPosition", -1 },
        { "capShutter", 2 },  { "capPosition", 1 }, { "capBeam", 1 },
    };

    QHash<QString, int> immediate;
    for (const auto &e : events)
        immediate[e.first] += e.second;

    CapabilityCounterAccumulator acc;
    acc.setDeferred(true);
    for (const auto &e : events)
        acc.accumulate(e.first, e.second);
    QHash<QString, int> deferred = acc.flush();

    // immediate's zero-net entries (capColour: +1-1=0) are a no-op either
    // way, but flush() actively omits them - strip them before comparing so
    // both sides represent the same real end state.
    QHash<QString, int> immediateNonZero;
    QHashIterator<QString, int> it(immediate);
    while (it.hasNext())
    {
        it.next();
        if (it.value() != 0)
            immediateNonZero.insert(it.key(), it.value());
    }

    QCOMPARE(deferred, immediateNonZero);
}

QTEST_APPLESS_MAIN(CapabilityCounterAccumulator_Test)
