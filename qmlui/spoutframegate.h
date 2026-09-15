/*
  Q Light Controller Plus
  spoutframegate.h

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

#ifndef SPOUTFRAMEGATE_H
#define SPOUTFRAMEGATE_H

#include <QtGlobal>

/**
 * Back-pressure for video frames delivered through a queued connection.
 *
 * QVideoSink::videoFrameChanged is emitted on the decoder's renderer
 * thread, so a GUI-thread slot receives every frame as its own queued
 * event, each retaining a decoded QVideoFrame. When the slot's work
 * (conversion, painting, sending) takes longer than the clip's frame
 * interval, that queue - and the memory it pins - grows without bound
 * (measured: 7 GB within 40 s for a 1080p60 clip, ending in std::bad_alloc).
 *
 * The gate keeps the slot's cost bounded: a delivered frame is only worth
 * rendering if it is the newest one the sink holds. Anything older has a
 * newer delivery queued right behind it and is dropped unread, so a burst
 * of N queued frames collapses into one conversion. Judged by the frames'
 * own start timestamps; frames without timestamps are always rendered.
 * Plain logic, no Qt Multimedia dependency, so it is unit-testable.
 */
class SpoutFrameGate
{
public:
    SpoutFrameGate();

    /**
     * Decide whether the delivered frame should be rendered.
     * @param frameStartUs start time of the delivered frame (µs, < 0 = unknown)
     * @param newestStartUs start time of the newest frame the sink holds
     *        (µs, < 0 = unknown)
     * @return true to render it, false to drop it (a newer frame follows)
     */
    bool accept(qint64 frameStartUs, qint64 newestStartUs);

    /** Frames accepted since the last reset() */
    quint64 acceptedCount() const;
    /** Frames dropped since the last reset() */
    quint64 droppedCount() const;

    /** Forget the counters (a new run) */
    void reset();

private:
    quint64 m_accepted;
    quint64 m_dropped;
};

#endif // SPOUTFRAMEGATE_H
