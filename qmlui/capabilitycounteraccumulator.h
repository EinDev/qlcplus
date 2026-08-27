/*
  Q Light Controller Plus
  capabilitycounteraccumulator.h

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

#ifndef CAPABILITYCOUNTERACCUMULATOR_H
#define CAPABILITYCOUNTERACCUMULATOR_H

#include <QHash>
#include <QString>

/** Accumulates capability-counter deltas (keyed by capability name, e.g.
 *  "capPosition"/"capBeam") while deferred, instead of letting each one be
 *  applied immediately - used during a mass selection change (e.g.
 *  rectangle-select) so hundreds of fixtures don't each trigger their own
 *  QML property write. Holds no view/QML state itself; the caller is
 *  responsible for actually applying whatever flush() returns. */
class CapabilityCounterAccumulator
{
public:
    bool isDeferred() const { return m_deferred; }

    /** Enable/disable deferral. Turning deferral off does NOT flush any
     *  pending deltas - call flush() explicitly for that. */
    void setDeferred(bool deferred) { m_deferred = deferred; }

    /** If deferred, accumulates $delta under $capName and returns true (the
     *  caller should skip its immediate-apply path). If not deferred,
     *  accumulates nothing and returns false (the caller should apply
     *  $delta immediately, as before this class existed). */
    bool accumulate(const QString &capName, int delta)
    {
        if (m_deferred == false)
            return false;

        m_pending[capName] += delta;
        return true;
    }

    /** Turns deferral off and returns the net, non-zero deltas accumulated
     *  since the last flush() (a capability whose deltas summed to exactly
     *  zero is omitted - applying a zero delta would be a no-op anyway).
     *  Clears all pending state. */
    QHash<QString, int> flush()
    {
        m_deferred = false;

        QHash<QString, int> pending = m_pending;
        m_pending.clear();

        QHash<QString, int> result;
        QHashIterator<QString, int> it(pending);
        while (it.hasNext())
        {
            it.next();
            if (it.value() != 0)
                result.insert(it.key(), it.value());
        }
        return result;
    }

private:
    bool m_deferred = false;
    QHash<QString, int> m_pending;
};

#endif // CAPABILITYCOUNTERACCUMULATOR_H
