/*
  Q Light Controller Plus
  app_apivcpage.cpp

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

/*
 * Host side of two small control API slices (added 2026-09-27):
 *  - ApiVcHost::vcSetPageSize (vc.page.setSize, controlapi/src/domains/apivcpagestyledomain.cpp)
 *  - ApiShowHost (functions.show.track.setSpoutSize and the "spout" block of every Show track,
 *    controlapi/src/domains/apishowpreviewdomain.cpp): the live Spout sender lives in qmlui's
 *    VideoProvider, so resizing it and reading its size happen here.
 */

#include <QDebug>
#include <QRect>

#include "app.h"
#include "showmanager.h"
#include "videoprovider.h"
#include "virtualconsole/virtualconsole.h"
#include "virtualconsole/vcpage.h"
#include "track.h"
#include "video.h"
#include "show.h"

void App::vcSetPageSize(int index, int width, int height)
{
    VCPage *page = m_virtualConsole->page(index);
    if (page == nullptr)
        return;

    // what VCPageProperties.qml's Width / Height spin boxes write; saved in
    // the page's WindowState like any other frame geometry
    page->setGeometry(QRect(0, 0, width, height));
}

QSize App::showTrackSpoutOutputSize(const Track *track) const
{
    if (track == nullptr)
        return QSize();

    VideoProvider *provider = VideoProvider::instance();
    return provider != nullptr ? provider->trackSpoutOutputSize(track) : track->spoutSize();
}

void App::showTrackSpoutSizeChanged(Show *show, Track *track)
{
    if (show == nullptr || track == nullptr)
        return;

    // The desktop Show Manager showing this Show refreshes its track header
    // and resizes the sender in one go (ShowManager::applyTrackSpoutSize).
    if (m_showManager != nullptr && m_showManager->currentShow() == show)
    {
        m_showManager->applyTrackSpoutSize(track->id(), track->spoutSize());
        return;
    }

    QSize size = track->spoutSize();
    if (size.isEmpty())
        return; // unset: the sender keeps its size until the next load, like the desktop

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    VideoProvider *provider = VideoProvider::instance();
    if (provider != nullptr)
        provider->resizeSpoutSender(Video::spoutSenderNameForTrack(track->name()), size,
                                    QString("control API switched track '%1' output").arg(track->name()));
#endif
}
