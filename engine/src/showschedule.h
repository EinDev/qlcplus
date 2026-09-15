/*
  Q Light Controller Plus
  showschedule.h

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

#ifndef SHOWSCHEDULE_H
#define SHOWSCHEDULE_H

#include <QVector>
#include <QHash>
#include <QSet>

#include "function.h"

/** @addtogroup engine_functions Functions
 * @{
 */

/**
 * One Show timeline item, resolved into plain values.
 *
 * A ShowSchedule is an immutable snapshot of a Show's timeline. It is built on
 * the thread owning the Show (the GUI thread, see Show::rebuildSchedule) and
 * consumed by ShowRunner on the MasterTimer thread. Everything the runner needs
 * is copied here by value, so the runner never dereferences a Track or a
 * ShowFunction that the GUI thread may be editing or deleting at the same time.
 */
struct ScheduledClip
{
    /** ShowFunction::id() - unique within a Show, preserved across undo/redo */
    quint32 sfId = 0;
    /** ID of the Function the clip plays */
    quint32 functionId = Function::invalidId();
    /** ID of the Track the clip sits on */
    quint32 trackId = 0;
    /** Start time on the timeline, in milliseconds */
    quint32 start = 0;
    /** End time on the timeline (start + resolved duration), in milliseconds */
    quint32 end = 0;
    /** Type of the clip's Function when the schedule was built */
    Function::Type type = Function::Undefined;

    /** True when the clip should be playing at timeline position $now */
    bool isActiveAt(quint32 now) const { return start <= now && now < end; }
};

struct ShowSchedule
{
    /** Every clip of the Show, sorted by start time */
    QVector<ScheduledClip> clips;
    /** Latest clip end over the whole Show, in milliseconds */
    quint32 totalRunTime = 0;
    /** Track intensity (the Show's attribute values), keyed by track ID */
    QHash<quint32, qreal> intensity;
    /** IDs of every Function referenced by a clip (for cheap change filtering) */
    QSet<quint32> functionIds;
    /**
     * The Show's own tempo type when the schedule was built. It selects the
     * clock every clip is started and stopped on: real elapsed time in a Time
     * Show, the beat-quantised clock in a Beats Show (see ShowRunner::now()).
     * A clip's Function tempo only governs how that Function steps internally,
     * never when it comes and goes on the timeline.
     */
    Function::TempoType showTempo = Function::Time;

    /** Find a clip by its ShowFunction ID, or nullptr if it is not scheduled */
    const ScheduledClip *clip(quint32 sfId) const
    {
        for (const ScheduledClip &c : clips)
            if (c.sfId == sfId)
                return &c;
        return nullptr;
    }
};

/** @} */

#endif // SHOWSCHEDULE_H
