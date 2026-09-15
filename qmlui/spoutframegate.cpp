/*
  Q Light Controller Plus
  spoutframegate.cpp

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

#include "spoutframegate.h"

SpoutFrameGate::SpoutFrameGate()
    : m_accepted(0)
    , m_dropped(0)
{
}

bool SpoutFrameGate::accept(qint64 frameStartUs, qint64 newestStartUs)
{
    // Without timestamps there is nothing to judge by: render everything
    // rather than risk dropping every frame of a clip.
    // A frame older than the newest one has a newer delivery queued behind
    // it. Equal means this is the newest; a newest one that is *older*
    // (the clip looped back to 0, a backward seek) is not a reason to drop
    // this one either - the next delivery will be the new newest.
    if (frameStartUs >= 0 && newestStartUs >= 0 && frameStartUs < newestStartUs)
    {
        m_dropped++;
        return false;
    }

    m_accepted++;
    return true;
}

quint64 SpoutFrameGate::acceptedCount() const
{
    return m_accepted;
}

quint64 SpoutFrameGate::droppedCount() const
{
    return m_dropped;
}

void SpoutFrameGate::reset()
{
    m_accepted = 0;
    m_dropped = 0;
}
