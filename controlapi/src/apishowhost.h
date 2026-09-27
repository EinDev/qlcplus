/*
  Q Light Controller Plus - Control API
  apishowhost.h

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

#ifndef APISHOWHOST_H
#define APISHOWHOST_H

#include <QSize>

class Show;
class Track;

/**
 * The few Show Manager things only the desktop front end knows (ApiShowPreviewDomain, added
 * 2026-09-27): the live Spout sender a track's Spout-mode Video clips publish into lives in
 * qmlui's VideoProvider, not in the engine. qmlui's App implements this; controlapi finds it via
 * dynamic_cast on ApiServer's parent, like ApiVcHost / ApiProjectHost. Without a host (tests, a
 * headless build) the Track's stored size is all there is and nothing is resized live.
 * Every call happens on the main thread.
 */
class ApiShowHost
{
public:
    virtual ~ApiShowHost() {}

    /** The size $track's shared Spout sender outputs right now: the Track's fixed size when set,
     *  otherwise the live sender's (created by its first clip), 0x0 when there is none. */
    virtual QSize showTrackSpoutOutputSize(const Track *track) const = 0;

    /** functions.show.track.setSpoutSize just stored a new fixed size on $track (0x0 = unset):
     *  resize the live sender like ShowManager::applyTrackSpoutSize() does and refresh the desktop
     *  Show Manager's track header if it shows $show. */
    virtual void showTrackSpoutSizeChanged(Show *show, Track *track) = 0;
};

#endif
