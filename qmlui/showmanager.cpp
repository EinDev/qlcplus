/*
  Q Light Controller Plus
  showmanager.cpp

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

#include <QQmlContext>
#include <QtMath>
#include <QVector>
#include <algorithm>

#include "waveformimageprovider.h"
#include "videoprovider.h"
#include "showmanager.h"
#include "sequence.h"
#include "tardis.h"
#include "chaser.h"
#include "scene.h"
#include "audio.h"
#include "video.h"
#include "track.h"
#include "show.h"
#include "doc.h"
#include "app.h"

ShowManager::ShowManager(QQuickView *view, Doc *doc, QObject *parent)
    : PreviewContext(view, doc, "SHOWMGR", parent)
    , m_cursorMovedDuringPause(false)
    , m_isPlaying(false)
    , m_isPaused(false)
    , m_previewEnabled(true)
    , m_isPreviewing(false)
    , m_currentShow(nullptr)
    , m_stretchFunctions(false)
    , m_gridEnabled(false)
    , m_snapGuideX(-1.0)
    , m_timeScale(5.0)
    , m_currentTime(0)
    , m_selectedTrackId(-1)
    , m_itemsColor(Qt::gray)
    , m_multipleSelection(false)
{
    view->rootContext()->setContextProperty("showManager", this);
    qmlRegisterUncreatableType<Show>("org.qlcplus.classes", 1, 0, "Show", "Can't create a Show");
    qmlRegisterType<Track>("org.qlcplus.classes", 1, 0, "Track");
    qmlRegisterUncreatableType<ShowFunction>("org.qlcplus.classes", 1, 0, "ShowFunction", "Can't create a ShowFunction");

    /* Create and register a Waveform image provider */
    m_waveformProvider = new WaveformImageProvider(doc);
    view->engine()->addImageProvider(QLatin1String("waveform"), m_waveformProvider);
    view->rootContext()->setContextProperty("waveformProvider", m_waveformProvider);

    /* Relay Function changes to the UI, so Show Items can update
       their preview lines when the referenced Function is edited
       (string-based: Doc lives in the engine DLL, see stagewizard.cpp) */
    connect(m_doc, SIGNAL(functionChanged(quint32)),
            this, SIGNAL(functionChanged(quint32)));

    setContextResource("qrc:/ShowManager.qml");
    setContextTitle(tr("Show Manager"));
}

void ShowManager::initialize()
{
    App *app = qobject_cast<App *>(m_view);
    m_tickSize = app->pixelDensity() * 18;

    if (m_waveformProvider)
        m_waveformProvider->setPixelDensity(app->pixelDensity());

    siComponent = new QQmlComponent(m_view->engine(), QUrl("qrc:/ShowItem.qml"));
    if (siComponent->isError())
        qDebug() << siComponent->errors();
}

int ShowManager::currentShowID() const
{
    if (m_currentShow == nullptr)
        return Function::invalidId();

    return m_currentShow->id();
}

Show *ShowManager::currentShow() const
{
    return m_currentShow;
}

bool ShowManager::isEditing() const
{
    return m_currentShow == nullptr ? false : true;
}

void ShowManager::setCurrentShowID(int currentShowID)
{
    if (m_currentShow != nullptr)
    {
        if (m_currentShow->id() == (quint32)currentShowID)
            return;
        // before the stopped() connection below goes: a frozen Show must
        // not keep holding the output with nothing pointing at it
        stopPreview();
        disconnect(m_currentShow, SIGNAL(timeChanged(quint32)), this, SLOT(slotTimeChanged(quint32)));
        disconnect(m_currentShow, SIGNAL(showFinished()), this, SLOT(slotShowFinished()));
        disconnect(m_currentShow, SIGNAL(stopped(quint32)), this, SLOT(slotShowStopped()));
        disconnect(m_currentShow, SIGNAL(scheduleChanged()), this, SLOT(slotScheduleChanged()));
    }

    m_currentShow = qobject_cast<Show*>(m_doc->function(currentShowID));
    m_cursorMovedDuringPause = false;
    emit currentShowIDChanged(currentShowID);
    emit isEditingChanged();

    if (m_currentShow != nullptr)
    {
        connect(m_currentShow, SIGNAL(timeChanged(quint32)), this, SLOT(slotTimeChanged(quint32)));
        connect(m_currentShow, SIGNAL(showFinished()), this, SLOT(slotShowFinished()));
        connect(m_currentShow, SIGNAL(stopped(quint32)), this, SLOT(slotShowStopped()));
        connect(m_currentShow, SIGNAL(scheduleChanged()), this, SLOT(slotScheduleChanged()));
        emit showDurationChanged(m_currentShow->totalDuration());
        emit showNameChanged(m_currentShow->name());
        emit timeDivisionChanged(timeDivision());
        emit beatsDivisionChanged(beatsDivision());
        emit timeDivisionBPMChanged(timeDivisionBPM());
    }
    else
    {
        emit showDurationChanged(0);
        emit showNameChanged("");
        emit timeDivisionBPMChanged(timeDivisionBPM());
    }

    /* The tick size depends on the Show's time division (see setTimeScale),
       so the previous Show's scale must not carry over to one with a
       different division: force a recompute and notification */
    m_timeScale = 0.0;
    setTimeScale(timeDivision() == Show::Time ? 5.0 : 1.0);

    emit tracksChanged();
    setPlaybackState(m_currentShow != nullptr ? m_currentShow->isRunning() : false,
                     m_currentShow != nullptr ? m_currentShow->isPaused() : false);
}

QString ShowManager::showName() const
{
    if (m_currentShow == nullptr)
        return QString();

    return m_currentShow->name();
}

void ShowManager::setShowName(QString showName)
{
    if (m_currentShow == nullptr || m_currentShow->name() == showName)
        return;

    Tardis::instance()->enqueueAction(Tardis::FunctionSetName, m_currentShow->id(), m_currentShow->name(), showName);

    m_currentShow->setName(showName);
    emit showNameChanged(showName);
}

bool ShowManager::stretchFunctions() const
{
    return m_stretchFunctions;
}

void ShowManager::setStretchFunctions(bool stretchFunctions)
{
    if (m_stretchFunctions == stretchFunctions)
        return;

    m_stretchFunctions = stretchFunctions;
    emit stretchFunctionsChanged(stretchFunctions);
}

bool ShowManager::gridEnabled() const
{
    return m_gridEnabled;
}

void ShowManager::setGridEnabled(bool gridEnabled)
{
    if (m_gridEnabled == gridEnabled)
        return;

    m_gridEnabled = gridEnabled;
    emit gridEnabledChanged(m_gridEnabled);
}

double ShowManager::snapGuideX() const
{
    return m_snapGuideX;
}

void ShowManager::setSnapGuideX(double snapGuideX)
{
    if (qFuzzyCompare(m_snapGuideX, snapGuideX))
        return;

    m_snapGuideX = snapGuideX;
    emit snapGuideXChanged();
}

double ShowManager::msToPx(double ms) const
{
    // ms -> pixel-X conversion shared by snap edges and drag previews.
    // ShowFunction startTime/duration (and beat-marker ms values derived from
    // them) are always real milliseconds, so in Beats mode this must convert
    // ms -> pixels-on-a-beat-ruler (mirrors TimeUtils.timeToBeatSize), not
    // reinterpret the ms value as a beat-pseudo count.
    if (timeDivision() == Show::Time)
        return (ms * m_tickSize) / (m_timeScale * 1000.0);

    if (m_currentShow == nullptr)
        return 0.0;

    int bpmNumber = m_currentShow->timeDivisionBPM();
    double barDuration = bpmNumber > 0 ? (60000.0 / bpmNumber) * m_currentShow->beatsDivision() : 0.0;
    return barDuration > 0.0 ? (m_tickSize * ms) / barDuration : 0.0;
}

QVariantList ShowManager::getSnapEdges(quint32 excludeFuncId,
                                       double viewportLeft, double viewportRight) const
{
    QVariantList edges;

    if (m_currentShow == nullptr)
        return edges;

    bool cull = (viewportLeft >= 0 && viewportRight >= 0);

    for (Track *track : m_currentShow->tracks())
    {
        for (ShowFunction *sf : track->showFunctions())
        {
            if (sf->functionID() == excludeFuncId)
                continue;

            quint32 endTime = sf->startTime() + sf->duration();
            double startX = msToPx((double)sf->startTime());
            double endX = msToPx((double)endTime);

            // filter: skip items entirely outside the visible viewport
            if (cull && (endX < viewportLeft || startX > viewportRight))
                continue;

            edges.append(startX);
            edges.append(endX);

            // Beat-grid snap markers for audio clips with a completed BPM detection.
            // Uses the clip's own detectedBpm/beatPhaseMs - independent of the
            // Show's own Markers-grid BPM (m_currentShow->timeDivisionBPM()), which
            // is only used above (via msToPx) for the axis-scale ms->px conversion.
            Function *f = m_doc->function(sf->functionID());
            if (f != nullptr && f->type() == Function::AudioType)
            {
                Audio *audio = qobject_cast<Audio *>(f);
                if (audio != nullptr && audio->bpmAnalysisState() == Audio::Done && audio->detectedBpm() > 0.0)
                {
                    double periodMs = 60000.0 / audio->detectedBpm();
                    double phaseMs = audio->beatPhaseMs();
                    double clipDuration = (double)sf->duration();

                    for (int k = 0; ; k++)
                    {
                        double t = phaseMs + (double)k * periodMs;
                        if (t > clipDuration)
                            break;

                        double beatX = msToPx((double)sf->startTime() + t);

                        if (cull && beatX < viewportLeft)
                            continue;
                        if (cull && beatX > viewportRight)
                            break;

                        edges.append(beatX);
                    }
                }
            }
        }
    }

    return edges;
}

/*********************************************************************
 * Time
 ********************************************************************/

Show::TimeDivision ShowManager::timeDivision() const
{
    if (m_currentShow == nullptr)
        return Show::Time;

    return m_currentShow->timeDivisionType();
}

void ShowManager::setTimeDivision(Show::TimeDivision division)
{
    if (m_currentShow == nullptr)
        return;

    if (division == m_currentShow->timeDivisionType())
        return;

    /* Set the division type first: setTimeScale needs it to
       calculate the tick size against the new time division */
    m_currentShow->setTimeDivisionType(division);

    /* Notify the new beats division before any geometry-related signal.
       setTimeScale emits tickSizeChanged/timeScaleChanged, which make the
       UI recalculate the items geometry right away. If the beats division
       is still the previous one, beat sizes are computed with a stale
       (possibly zero) divider, messing up the whole timeline preview */
    if (division != Show::Time)
        emit beatsDivisionChanged(m_currentShow->beatsDivision());

    if (division == Show::Time)
    {
        m_currentShow->setTempoType(Function::Time);
        setTimeScale(5.0);
    }
    else
    {
        m_currentShow->setTempoType(Function::Beats);
        setTimeScale(1.0);
    }
    emit timeDivisionChanged(division);
}

int ShowManager::beatsDivision() const
{
    if (m_currentShow == nullptr)
        return 0;

    return m_currentShow->beatsDivision();
}

int ShowManager::timeDivisionBPM() const
{
    if (m_currentShow == nullptr)
        return 120;

    return m_currentShow->timeDivisionBPM();
}

void ShowManager::setTimeDivisionBPM(int BPM)
{
    if (m_currentShow == nullptr || BPM == m_currentShow->timeDivisionBPM())
        return;

    m_currentShow->setTimeDivisionBPM(BPM);
    m_doc->setModified();
    emit timeDivisionBPMChanged(BPM);
}

float ShowManager::timeScale() const
{
    return m_timeScale;
}

void ShowManager::setTimeScale(float timeScale)
{
    if (m_timeScale == timeScale)
        return;

    m_timeScale = timeScale;
    float tickScale = timeDivision() == Show::Time ? 1.0 : timeScale;

    if (m_detached)
    {
        m_tickSize = pixelDensity() * (18 * tickScale);
    }
    else
    {
        App *app = qobject_cast<App *>(m_view);
        m_tickSize = app->pixelDensity() * (18 * tickScale);
    }

    emit tickSizeChanged(m_tickSize);
    emit timeScaleChanged(timeScale);
}

float ShowManager::tickSize() const
{
    return m_tickSize;
}

int ShowManager::currentTime() const
{
    return m_currentTime;
}

void ShowManager::setCurrentTime(int currentTime)
{
    if (m_currentTime == currentTime)
        return;

    if (m_currentShow != nullptr && m_currentShow->isPaused())
        m_cursorMovedDuringPause = true;

    m_currentTime = currentTime;
    emit currentTimeChanged(currentTime);

    if (m_previewEnabled)
        previewAt(currentTime);
}

/*********************************************************************
 * Tracks
 ********************************************************************/

QVariant ShowManager::tracks() const
{
    if (m_currentShow)
        return QVariant::fromValue(m_currentShow->tracks());

    return QVariant();
}

int ShowManager::selectedTrackId() const
{
    return m_selectedTrackId;
}

void ShowManager::setSelectedTrackId(int id)
{
    if (m_selectedTrackId == id)
        return;

    m_selectedTrackId = id;
    emit selectedTrackIdChanged(id);
    emit itemClicked(App::TrackDragItem);
}

void ShowManager::setTrackSolo(int index, bool solo)
{
    QList<Track*> tracks = m_currentShow->tracks();

    if (index < 0 || index >= tracks.count())
        return;

    for (int i = 0; i < tracks.count(); i++)
    {
        if (i == index)
            tracks.at(i)->setMute(false);
        else
            tracks.at(i)->setMute(solo);
    }
}

void ShowManager::moveTrack(int index, int direction)
{
    QList<Track*> tracks = m_currentShow->tracks();

    if (index < 0 || index >= tracks.count())
        return;

    m_currentShow->moveTrack(tracks.at(index), direction);
    m_doc->setModified();

    emit tracksChanged();
}

void ShowManager::deleteSelectedTrack()
{
    deleteTrack(selectedTrackId());
}

void ShowManager::requestTrackDeletion(int trackId)
{
    if (m_currentShow == nullptr)
        return;

    Track *track = m_currentShow->track(trackId);
    if (track == nullptr)
        return;

    int clipCount = track->showFunctions().count();
    if (clipCount == 0)
        deleteTrack(trackId);
    else
        emit trackDeletionConfirmationRequested(trackId, track->name(), clipCount);
}

void ShowManager::deleteTrack(int trackId)
{
    if (m_currentShow == nullptr)
        return;

    Track *track = m_currentShow->track(trackId);
    if (track == nullptr)
        return;

    qDebug() << "Deleting track" << track->id();

    // serialize the track (and its Functions) before removing it, as
    // the undo action needs to restore it from its XML representation
    Tardis::instance()->enqueueAction(
        Tardis::ShowManagerDeleteTrack, m_currentShow->id(),
        Tardis::instance()->actionToByteArray(Tardis::ShowManagerDeleteTrack, m_currentShow->id(), track->id()),
        QVariant());

    int clipboardCount = m_clipboard.count();

    QList <ShowFunction *> sfList = track->showFunctions();
    for (ShowFunction *sf : sfList)
    {
        // the clipboard must not keep a pointer to an item that goes away
        // with its track (same purge as deleteShowItems())
        for (int i = m_clipboard.count() - 1; i >= 0; i--)
        {
            if (m_clipboard.at(i).m_showFunc == sf)
                m_clipboard.removeAt(i);
        }

        QQuickItem *item = m_itemsMap.take(sf->id());
        delete item;
    }

    m_currentShow->removeTrack(trackId);
    m_doc->setModified();

    if (m_clipboard.count() != clipboardCount)
        emit clipboardItemsCountChanged(m_clipboard.count());

    if (m_selectedTrackId == trackId)
    {
        m_selectedTrackId = -1;
        emit selectedTrackIdChanged(-1);
    }

    // rebuild the view (resetView() also drops the selection, which may
    // have held the deleted items); fine with no tracks left, the timeline
    // then only offers the "create a new track" drop zone
    QQuickItem *itemsArea = qobject_cast<QQuickItem*>(m_view->rootObject()->findChild<QObject *>("showItemsArea"));
    renderView(itemsArea);

    emit tracksChanged();
}

/*********************************************************************
 * Track Spout output size
 ********************************************************************/

/** The size Video $func would publish at if it created the sender: its
 *  SpoutSize override, else its native resolution (empty if not probed
 *  yet). Null for anything that is not a Video in Spout mode. */
static Video *spoutVideo(Function *func)
{
    if (func == nullptr || func->type() != Function::VideoType)
        return nullptr;

    // the type was checked: a static_cast is enough and, unlike
    // qobject_cast, works across the engine DLL boundary
    Video *video = static_cast<Video *>(func);
    return video->outputMode() == Video::Spout ? video : nullptr;
}

static QSize effectiveSpoutSize(const Video *video)
{
    return video->spoutSize().isEmpty() ? video->resolution() : video->spoutSize();
}

void ShowManager::setTrackSpoutSize(int trackIdx, int width, int height)
{
    if (m_currentShow == nullptr || trackIdx < 0 || trackIdx >= m_currentShow->tracks().count())
        return;

    Track *track = m_currentShow->tracks().at(trackIdx);
    QSize size(width, height);
    if (size.isValid() == false || size.isEmpty())
        size = QSize(0, 0);

    if (track->spoutSize() == size)
    {
        // already fixed at this size on the Track, but the live sender may
        // still be elsewhere (e.g. "Keep" chosen earlier, then this picked
        // from the header menu): apply anyway, nothing to undo
        applyTrackSpoutSize(track->id(), size);
        return;
    }

    Tardis::instance()->enqueueAction(Tardis::ShowManagerTrackSetSpoutSize, track->id(),
                                      track->spoutSize(), size);
    applyTrackSpoutSize(track->id(), size);
    m_doc->setModified();
}

void ShowManager::applyTrackSpoutSize(quint32 trackId, QSize size)
{
    if (m_currentShow == nullptr)
        return;

    Track *track = m_currentShow->track(trackId);
    if (track == nullptr)
        return;

    track->setSpoutSize(size);

    if (size.isEmpty())
    {
        qDebug().noquote() << "[Spout] track" << track->name() << "output size unset - its sender keeps"
                           << "its current size until the next document load";
    }
    else
    {
        qDebug().noquote() << "[Spout] track" << track->name() << "output size fixed to"
                           << size.width() << "x" << size.height();
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
        VideoProvider *provider = VideoProvider::instance();
        if (provider != nullptr)
            provider->resizeSpoutSender(Video::spoutSenderNameForTrack(track->name()), size,
                                        QString("user switched track '%1' output").arg(track->name()));
#endif
    }

    emit trackSpoutInfoChanged();
}

QVariantMap ShowManager::trackSpoutInfo(int trackIdx) const
{
    QVariantMap info;
    info.insert("hasSpout", false);
    info.insert("width", 0);
    info.insert("height", 0);
    info.insert("fixed", false);
    info.insert("clips", QVariantList());

    if (m_currentShow == nullptr || trackIdx < 0 || trackIdx >= m_currentShow->tracks().count())
        return info;

    Track *track = m_currentShow->tracks().at(trackIdx);
    QVariantList clips;
    for (ShowFunction *sf : track->showFunctions())
    {
        Video *video = spoutVideo(m_doc->function(sf->functionID()));
        if (video == nullptr)
            continue;

        info["hasSpout"] = true;
        QSize size = effectiveSpoutSize(video);
        if (size.isEmpty())
            continue;

        QVariantMap clip;
        clip.insert("name", video->name());
        clip.insert("width", size.width());
        clip.insert("height", size.height());
        clips.append(clip);
    }
    info["clips"] = clips;

    QSize output = track->spoutSize();
    info["fixed"] = output.isEmpty() == false;
    VideoProvider *provider = VideoProvider::instance();
    if (output.isEmpty() && provider != nullptr)
        output = provider->trackSpoutOutputSize(track);
    if (output.isEmpty() == false)
    {
        info["width"] = output.width();
        info["height"] = output.height();
    }

    return info;
}

void ShowManager::checkSpoutSizeMismatch(Track *track, int trackIdx, Function *func)
{
    Video *video = spoutVideo(func);
    if (video == nullptr || track == nullptr)
        return;

    VideoProvider *provider = VideoProvider::instance();
    QSize clipSize = effectiveSpoutSize(video);
    QSize trackSize = provider != nullptr ? provider->trackSpoutOutputSize(track) : track->spoutSize();

    if (clipSize.isEmpty())
    {
        qDebug().noquote() << "[Spout]" << video->name() << "placed on track" << track->name()
                           << "- its resolution is not known yet, no output size check";
    }
    else if (trackSize.isEmpty())
    {
        qDebug().noquote() << "[Spout]" << video->name() << "placed on track" << track->name()
                           << "- nothing fixed the track's output size yet, it will be"
                           << clipSize.width() << "x" << clipSize.height();
    }
    else if (trackSize != clipSize)
    {
        qDebug().noquote() << "[Spout] output size mismatch: track" << track->name() << "outputs"
                           << trackSize.width() << "x" << trackSize.height() << "but" << video->name()
                           << "is" << clipSize.width() << "x" << clipSize.height()
                           << "- kept, asking the user whether to switch";
        emit spoutSizeMismatch(trackIdx, track->name(), trackSize.width(), trackSize.height(),
                               video->name(), clipSize.width(), clipSize.height());
    }

    // the clip is on the track now, so its default sender is the track's:
    // create it if this is the first clip (never resizes an existing one)
    if (provider != nullptr)
        provider->refreshSpoutSender(video->id());
    emit trackSpoutInfoChanged();
}

/*********************************************************************
  * Show Items
  ********************************************************************/

void ShowManager::addItems(QQuickItem *parent, int trackIdx, int startTime, QVariantList idsList,
                           ShowFunction *sourceFunc)
{
    if (idsList.count() == 0)
        return;

    // if no show is selected, then create a new one
    if (m_currentShow == nullptr)
    {
        QString defaultName = QString("%1 %2").arg(tr("New Show")).arg(m_doc->nextFunctionID());
        m_currentShow = new Show(m_doc);
        m_currentShow->setName(defaultName);
        Function *f = qobject_cast<Function*>(m_currentShow);
        if (m_doc->addFunction(f) == false)
        {
            qDebug() << "Error in creating a new Show!";
            m_currentShow = nullptr;
            return;
        }

        Tardis::instance()->enqueueAction(Tardis::FunctionCreate, m_currentShow->id(), QVariant(),
                                          Tardis::instance()->actionToByteArray(Tardis::FunctionCreate, m_currentShow->id()));

        connect(m_currentShow, SIGNAL(timeChanged(quint32)), this, SLOT(slotTimeChanged(quint32)));
        connect(m_currentShow, SIGNAL(showFinished()), this, SLOT(slotShowFinished()));
        connect(m_currentShow, SIGNAL(stopped(quint32)), this, SLOT(slotShowStopped()));
        emit currentShowIDChanged(m_currentShow->id());
        emit showNameChanged(m_currentShow->name());
        emit isEditingChanged();
        setPlaybackState(false, false);
    }

    Track *selectedTrack = nullptr;

    // if no Track index is provided, then add a new one
    if (trackIdx == -1)
    {
        selectedTrack = new Track(Function::invalidId(), m_currentShow);
        selectedTrack->setName(tr("Track %1").arg(m_currentShow->tracks().count() + 1));
        m_currentShow->addTrack(selectedTrack);

        Tardis::instance()->enqueueAction(
            Tardis::ShowManagerAddTrack, m_currentShow->id(), QVariant(),
            Tardis::instance()->actionToByteArray(Tardis::ShowManagerAddTrack, m_currentShow->id(), selectedTrack->id()));

        trackIdx = m_currentShow->tracks().count() - 1;
        emit tracksChanged();
    }
    else
    {
        if (trackIdx >= m_currentShow->tracks().count())
        {
            qDebug() << "Track index out of bounds!" << trackIdx;
            return;
        }
        selectedTrack = m_currentShow->tracks().at(trackIdx);
    }

    for (QVariant &vID : idsList) // C++11
    {
        quint32 functionID = vID.toUInt();
        if (functionID == m_currentShow->id())
        {
            /* TODO: a popup displaying the user stupidity would be nice here... */
            continue;
        }

        // and now create the actual ShowFunction and the QML item
        Function *func = m_doc->function(functionID);
        if (func == nullptr)
            continue;

        ShowFunction *showFunc = createShowItem(parent, selectedTrack, trackIdx, func, startTime, sourceFunc);
        startTime += showFunc->duration();
    }

    emit showDurationChanged(m_currentShow->totalDuration());
}

ShowFunction *ShowManager::createShowItem(QQuickItem *parent, Track *track, int trackIdx, Function *func,
                                      int startTime, ShowFunction *sourceFunc)
{
    ShowFunction *showFunc = track->createShowFunction(func->id());

    // tempoType (real-time vs beat-clock playback scheduling) is no longer forced
    // to match the Show's display mode here: Function already defaults to Time,
    // Chaser/Scene/RGBMatrix expose their own user-settable tempoType in their own
    // editors, and Audio/Video must never run on a beat clock regardless of how
    // the Show's ruler happens to be displayed.
    //
    // The old Beats-mode branch also called func->setTotalDuration(func->duration())
    // for Audio/Video here. That is dropped rather than made unconditional: Audio
    // already keeps duration()/totalDuration() in sync from its own decoder at file
    // load, making the call a no-op there, but Video's generic duration() is never
    // populated from its real probed length (only its own totalDuration is, via the
    // media player) - it defaults to 0, so applying this unconditionally would
    // overwrite an already-correct Video totalDuration with 0 on every add. This was
    // only safe before because it only ran in Beats mode, in front of a tempoType
    // force-set whose own Function::setTempoType() conversion this call was likely
    // compensating for - that conversion no longer runs, so this line no longer has
    // a clear purpose here.
    showFunc->setDuration(func->totalDuration() ? func->totalDuration() : 5000);
    showFunc->setStartTime(startTime);
    showFunc->setColor(ShowFunction::defaultColor(func->type()));

    // when pasting, inherit the customized properties of the source item
    if (sourceFunc != nullptr)
    {
        showFunc->setDuration(sourceFunc->duration());
        showFunc->setColor(sourceFunc->color());
        showFunc->setLocked(sourceFunc->isLocked());
    }

    Tardis::instance()->enqueueAction(
        Tardis::ShowManagerAddFunction, m_currentShow->id(), QVariant(),
        Tardis::instance()->actionToByteArray(Tardis::ShowManagerAddFunction, m_currentShow->id(), showFunc->id()));

    QQuickItem *newItem = qobject_cast<QQuickItem*>(siComponent->create());

    newItem->setParentItem(parent);
    newItem->setProperty("trackIndex", trackIdx);
    newItem->setProperty("sfRef", QVariant::fromValue(showFunc));
    newItem->setProperty("funcRef", QVariant::fromValue(func));

    m_itemsMap[showFunc->id()] = newItem;

    checkSpoutSizeMismatch(track, trackIdx, func);

    return showFunc;
}

void ShowManager::addShowItem(ShowFunction *sf, quint32 trackId)
{
    if (m_currentShow == nullptr || sf == nullptr)
        return;

    // items are parented to the same item used by renderView()
    QQuickItem *parent = contextItem();
    if (parent == nullptr)
        return;

    Function *func = m_doc->function(sf->functionID());
    if (func == nullptr)
        return;

    // ShowItem places itself vertically by track *index*, not by track ID
    int trackIndex = m_currentShow->tracks().indexOf(m_currentShow->track(trackId));
    if (trackIndex < 0)
        return;

    QQuickItem *newItem = qobject_cast<QQuickItem*>(siComponent->create());

    newItem->setParentItem(parent);
    newItem->setProperty("trackIndex", trackIndex);
    newItem->setProperty("sfRef", QVariant::fromValue(sf));
    newItem->setProperty("funcRef", QVariant::fromValue(func));
    m_itemsMap[sf->id()] = newItem;

    // a redo of a drop/paste lands the clip on the track again
    checkSpoutSizeMismatch(m_currentShow->track(trackId), trackIndex, func);
}

void ShowManager::deleteShowItems(QVariantList data)
{
    Q_UNUSED(data);

    if (m_currentShow == nullptr)
        return;

    int clipboardCount = m_clipboard.count();

    foreach (SelectedShowItem ssi, m_selectedItems)
    {
        // the guarded pointer went null: the item is already gone
        if (ssi.m_showFunc == nullptr)
            continue;

        // drop any clipboard reference to the item being deleted to
        // avoid dangling pointers when pasting later
        for (int i = m_clipboard.count() - 1; i >= 0; i--)
        {
            if (m_clipboard.at(i).m_showFunc == ssi.m_showFunc)
                m_clipboard.removeAt(i);
        }

        // resolved from the ShowFunction rather than the cached
        // m_trackIndex, which goes stale when tracks are reordered
        Track *track = m_currentShow->getTrackFromShowFunctionID(ssi.m_showFunc->id());
        if (track == nullptr)
            continue;

        quint32 sfId = ssi.m_showFunc->id();

        // serialize the item before removing it, as the undo action
        // needs to restore it from its XML representation
        Tardis::instance()->enqueueAction(
            Tardis::ShowManagerDeleteFunction, m_currentShow->id(),
            Tardis::instance()->actionToByteArray(Tardis::ShowManagerDeleteFunction, m_currentShow->id(), sfId),
            QVariant());

        track->removeShowFunction(ssi.m_showFunc, true);
        m_itemsMap.remove(sfId);
        if (ssi.m_item != nullptr)
            delete ssi.m_item.data();
    }

    m_selectedItems.clear();
    emit selectedItemsCountChanged(0);

    if (m_clipboard.count() != clipboardCount)
        emit clipboardItemsCountChanged(m_clipboard.count());

    // a track header may have lost its last Spout clip
    emit trackSpoutInfoChanged();
}

void ShowManager::refreshView()
{
    if (contextItem() != nullptr)
        renderView(contextItem());

    emit tracksChanged();
    if (m_currentShow != nullptr)
        emit showDurationChanged(m_currentShow->totalDuration());
}

void ShowManager::deleteShowItem(ShowFunction *sf)
{
    if (sf == nullptr)
        return;

    // the caller deletes the ShowFunction right after this, so drop
    // every reference to it before it becomes dangling
    int selectedCount = m_selectedItems.count();
    for (int i = m_selectedItems.count() - 1; i >= 0; i--)
    {
        if (m_selectedItems.at(i).m_showFunc == sf)
            m_selectedItems.removeAt(i);
    }
    if (m_selectedItems.count() != selectedCount)
        emit selectedItemsCountChanged(m_selectedItems.count());

    int clipboardCount = m_clipboard.count();
    for (int i = m_clipboard.count() - 1; i >= 0; i--)
    {
        if (m_clipboard.at(i).m_showFunc == sf)
            m_clipboard.removeAt(i);
    }
    if (m_clipboard.count() != clipboardCount)
        emit clipboardItemsCountChanged(m_clipboard.count());

    quint32 sfId = sf->id();
    QQuickItem *item = m_itemsMap.value(sfId, nullptr);
    if (item != nullptr)
    {
        m_itemsMap.remove(sfId);
        delete item;
    }
}

/** A ShowFunction reference coming back from QML. A JS-built array element
 *  ([ sfRef ]) is stored as a plain QObject*, and QVariant::value<ShowFunction*>()
 *  would resolve it through the same metaobject comparison as qobject_cast,
 *  which fails across the engine DLL boundary - so go through inherits(). */
static ShowFunction *showFunctionFromRef(const QVariant &ref)
{
    QObject *obj = ref.value<QObject *>();
    if (obj != nullptr && obj->inherits("ShowFunction"))
        return static_cast<ShowFunction *>(obj);
    return nullptr;
}

int ShowManager::snapStartTimeToGrid(int startTime, bool itemSnapped) const
{
    if (m_currentShow == nullptr || !m_gridEnabled || itemSnapped)
        return startTime;

    if (timeDivision() == Show::Time)
    {
        // calculate the X position from time and time scale
        // timescale * 1000 : tickSize = time : x
        float xPos = ((float)startTime * m_tickSize) / (m_timeScale * 1000.0);
        // round to the nearest snap position
        xPos = qRound(xPos / m_tickSize) * m_tickSize;
        // recalculate the time from pixels
        // xPos : time = tickSize : timescale * 1000
        return xPos * (1000 * m_timeScale) / m_tickSize;
    }

    // startTime is real ms here too; in Beats mode tickSize means pixels
    // per bar, not pixels-per-timeScale-second, so snap to the nearest whole
    // bar (in ms, via BPM/beatsDivision) instead of reusing the Time-mode
    // pixel round-trip above.
    int bpmNumber = m_currentShow->timeDivisionBPM();
    int beatsDivision = m_currentShow->beatsDivision();
    if (bpmNumber > 0 && beatsDivision > 0)
    {
        double barDuration = (60000.0 / bpmNumber) * beatsDivision;
        return qRound(startTime / barDuration) * barDuration;
    }

    return startTime;
}

QList<QList<ShowClipSpan>> ShowManager::trackSpans() const
{
    QList<QList<ShowClipSpan>> spans;

    if (m_currentShow == nullptr)
        return spans;

    for (Track *track : m_currentShow->tracks())
    {
        QList<ShowClipSpan> clips;
        for (ShowFunction *sf : track->showFunctions())
        {
            // same rule as checkOverlapping(): an item whose Function is
            // gone does not block anything
            if (m_doc->function(sf->functionID()) == nullptr)
                continue;

            ShowClipSpan clip;
            clip.id = sf->id();
            clip.startTime = sf->startTime();
            clip.duration = sf->duration();
            clips.append(clip);
        }
        spans.append(clips);
    }

    return spans;
}

ShowManager::GroupMovePlan ShowManager::planGroupMove(const QVariantList &sfRefs, ShowFunction *grabbed,
                                                      int newTrackIdx, int newStartTime, bool itemSnapped) const
{
    GroupMovePlan plan;

    if (m_currentShow == nullptr || grabbed == nullptr || grabbed->isLocked() || newTrackIdx < 0)
        return plan;

    QList<Track *> tracks = m_currentShow->tracks();

    // a drop anywhere below the last track lands on the first new one, so the
    // group creates at most as many tracks as it spans below the last one
    newTrackIdx = qMin(newTrackIdx, tracks.count());

    // The source tracks are resolved from the ShowFunctions themselves rather
    // than trusted from the QML items' trackIndex: that index could go stale
    // (or point past the last track), and QList::at() on a bad index is
    // undefined behavior in a release build.
    int grabbedTrackIdx = tracks.indexOf(m_currentShow->getTrackFromShowFunctionID(grabbed->id()));
    if (grabbedTrackIdx < 0)
        return plan;

    // the moving group: the grabbed item plus every other unlocked item of
    // the selection that still belongs to this Show
    plan.items.append(grabbed);
    plan.trackIndices.append(grabbedTrackIdx);

    for (const QVariant &ref : sfRefs)
    {
        ShowFunction *sf = showFunctionFromRef(ref);
        if (sf == nullptr || sf == grabbed || sf->isLocked() || plan.items.contains(sf))
            continue;

        int trackIdx = tracks.indexOf(m_currentShow->getTrackFromShowFunctionID(sf->id()));
        if (trackIdx < 0)
            continue;

        plan.items.append(sf);
        plan.trackIndices.append(trackIdx);
    }

    QSet<quint32> movingIds;
    QList<ShowMoveItem> moveItems;
    for (int i = 0; i < plan.items.count(); i++)
    {
        ShowMoveItem item;
        item.id = plan.items.at(i)->id();
        item.trackIndex = plan.trackIndices.at(i);
        item.startTime = plan.items.at(i)->startTime();
        item.duration = plan.items.at(i)->duration();
        moveItems.append(item);
        movingIds.insert(item.id);
    }

    QList<QList<ShowClipSpan>> spans = trackSpans();

    // 1. grid-snap the grabbed item's requested spot, then
    // 2. resolve a collision on the target track to the nearest free spot
    //    (a track index past the end is a new, empty track: nothing to hit)
    qint64 requested = snapStartTimeToGrid(qMax(0, newStartTime), itemSnapped);
    qint64 resolved = requested;
    if (newTrackIdx < spans.count())
        resolved = ShowMoveHelper::resolveCollision(spans.at(newTrackIdx), requested, grabbed->duration(), movingIds);
    plan.shifted = (resolved != requested);

    // 3. the same delta applies to the whole group; every item must land on
    //    a spot free of clips outside the group. A per-item scatter would
    //    break the group's relative layout, so no second resolution pass:
    //    a remaining collision refuses the drop as a whole.
    ShowGroupMoveResult result = ShowMoveHelper::validateGroupMove(spans, moveItems,
                                                                   newTrackIdx - grabbedTrackIdx,
                                                                   resolved - qint64(grabbed->startTime()));
    plan.ok = result.ok;
    plan.trackDelta = result.trackDelta;
    plan.timeDelta = result.timeDelta;

    if (!result.ok)
    {
        ShowFunction *blocker = m_currentShow->showFunction(result.blockingId);
        Function *func = blocker ? m_doc->function(blocker->functionID()) : nullptr;
        plan.blockingName = func ? func->name() : tr("another item");
    }

    return plan;
}

int ShowManager::checkAndMoveItem(ShowFunction *sf, int newTrackIdx, int newStartTime, bool itemSnapped)
{
    return checkAndMoveItems(QVariantList() << QVariant::fromValue(sf), sf, newTrackIdx, newStartTime, itemSnapped);
}

int ShowManager::checkAndMoveItems(QVariantList sfRefs, ShowFunction *grabbed, int newTrackIdx,
                                   int newStartTime, bool itemSnapped)
{
    GroupMovePlan plan = planGroupMove(sfRefs, grabbed, newTrackIdx, newStartTime, itemSnapped);
    if (!plan.ok)
        return -1;

    // create the tracks the group needs below the last one (at most as many
    // as the group spans past it), each one undoable like addItems()' track
    int maxDstIdx = -1;
    for (int trackIdx : plan.trackIndices)
        maxDstIdx = qMax(maxDstIdx, trackIdx + plan.trackDelta);

    bool tracksAdded = false;
    while (m_currentShow->tracks().count() <= maxDstIdx)
    {
        Track *newTrack = new Track(Function::invalidId(), m_currentShow);
        newTrack->setName(tr("Track %1").arg(m_currentShow->tracks().count() + 1));
        m_currentShow->addTrack(newTrack);

        // enqueued before the item moves below, so that an undo (which walks
        // this batch backwards) empties the track before removing it
        Tardis::instance()->enqueueAction(
            Tardis::ShowManagerAddTrack, m_currentShow->id(), QVariant(),
            Tardis::instance()->actionToByteArray(Tardis::ShowManagerAddTrack, m_currentShow->id(), newTrack->id()));
        tracksAdded = true;
    }

    if (tracksAdded)
        emit tracksChanged();

    QList<Track *> tracks = m_currentShow->tracks();

    // All the actions below are enqueued back-to-back, so Tardis batches them
    // into a single undo/redo step (see TARDIS_ACTION_INTERTIME). Every
    // ShowFunction change marks the Show's schedule dirty, which Show
    // coalesces into one rebuild per event-loop turn.
    for (int i = 0; i < plan.items.count(); i++)
    {
        ShowFunction *sf = plan.items.at(i);
        int srcIdx = plan.trackIndices.at(i);
        int dstIdx = srcIdx + plan.trackDelta;
        quint32 newTime = quint32(qint64(sf->startTime()) + plan.timeDelta);

        if (newTime != sf->startTime())
        {
            Tardis::instance()->enqueueAction(Tardis::ShowManagerItemSetStartTime, sf->id(), sf->startTime(), newTime);
            sf->setStartTime(newTime);
        }

        if (dstIdx != srcIdx)
        {
            Track *srcTrack = tracks.at(srcIdx);
            Track *dstTrack = tracks.at(dstIdx);
            Tardis::instance()->enqueueAction(Tardis::ShowManagerItemSetTrack, sf->id(), srcTrack->id(), dstTrack->id());
            moveShowItemToTrack(sf, dstTrack->id());
        }
    }

    m_doc->setModified();

    return plan.trackIndices.first() + plan.trackDelta;
}

QVariantMap ShowManager::previewItemsMove(QVariantList sfRefs, ShowFunction *grabbed, int newTrackIdx,
                                          int newStartTime, bool itemSnapped)
{
    GroupMovePlan plan = planGroupMove(sfRefs, grabbed, newTrackIdx, newStartTime, itemSnapped);

    QVariantMap map;
    map.insert("ok", plan.ok);
    map.insert("trackDelta", plan.trackDelta);
    map.insert("timeDelta", double(plan.timeDelta));
    map.insert("shifted", plan.shifted);
    map.insert("blockingItem", plan.blockingName);

    // followers: the grabbed item draws its own preview from the returned
    // deltas; every other item of the group gets its landing spot pushed
    // here, in its own coordinate space
    for (ShowFunction *sf : plan.items)
    {
        if (sf == grabbed)
            continue;

        QQuickItem *item = m_itemsMap.value(sf->id(), nullptr);
        if (item == nullptr)
            continue;

        double offsetX = msToPx(double(sf->startTime()) + double(plan.timeDelta)) - msToPx(double(sf->startTime()));
        double offsetY = plan.trackDelta * item->height();
        QMetaObject::invokeMethod(item, "setFollowPreview",
                                  Q_ARG(QVariant, offsetX), Q_ARG(QVariant, offsetY),
                                  Q_ARG(QVariant, !plan.ok), Q_ARG(QVariant, plan.shifted));
    }

    return map;
}

void ShowManager::clearItemsMovePreview(QVariantList sfRefs, ShowFunction *grabbed)
{
    for (const QVariant &ref : sfRefs)
    {
        ShowFunction *sf = showFunctionFromRef(ref);
        if (sf == nullptr || sf == grabbed)
            continue;

        QQuickItem *item = m_itemsMap.value(sf->id(), nullptr);
        if (item != nullptr)
            QMetaObject::invokeMethod(item, "clearFollowPreview");
    }
}

void ShowManager::moveShowItemToTrack(ShowFunction *sf, quint32 trackId)
{
    if (m_currentShow == nullptr || sf == nullptr)
        return;

    Track *srcTrack = m_currentShow->getTrackFromShowFunctionID(sf->id());
    Track *dstTrack = m_currentShow->track(trackId);
    if (srcTrack == nullptr || dstTrack == nullptr || srcTrack == dstTrack)
        return;

    srcTrack->removeShowFunction(sf, false);
    dstTrack->addShowFunction(sf);

    int dstIdx = m_currentShow->tracks().indexOf(dstTrack);

    // a Spout clip landing on another track (drag, group drag, undo/redo
    // of either) may not match that track's fixed output size
    checkSpoutSizeMismatch(dstTrack, dstIdx, m_doc->function(sf->functionID()));

    QQuickItem *item = m_itemsMap.value(sf->id(), nullptr);
    if (item != nullptr)
        item->setProperty("trackIndex", dstIdx);

    for (int i = 0; i < m_selectedItems.count(); i++)
    {
        if (m_selectedItems.at(i).m_showFunc == sf)
            m_selectedItems[i].m_trackIndex = dstIdx;
    }
}

bool ShowManager::setShowItemStartTime(ShowFunction *sf, int startTime)
{
    if (sf == nullptr)
        return false;

    Track *track = m_currentShow->getTrackFromShowFunctionID(sf->id());
    if (track == nullptr)
        return false;

    bool overlapping = checkOverlapping(track, sf, startTime, sf->duration());
    if (overlapping)
        return false;

    Tardis::instance()->enqueueAction(Tardis::ShowManagerItemSetStartTime, sf->id(), sf->startTime(), startTime);
    sf->setStartTime(startTime);

    return true;
}

bool ShowManager::setShowItemDuration(ShowFunction *sf, int duration)
{
    if (sf == nullptr)
        return false;

    Track *track = m_currentShow->getTrackFromShowFunctionID(sf->id());
    if (track == nullptr)
        return false;

    bool overlapping = checkOverlapping(track, sf, sf->startTime(), duration);
    if (overlapping)
        return false;

    Tardis::instance()->enqueueAction(Tardis::ShowManagerItemSetDuration, sf->id(), sf->duration(), duration);
    sf->setDuration(duration);

    return true;
}

int ShowManager::minimumTimelineDuration(Show::TimeDivision division) const
{
    return division == Show::Time ? 1 : 125;
}

quint32 ShowManager::itemRelativeTimeFromCursor(const ShowFunction *sf, int cursorTime) const
{
    if (sf == nullptr)
        return 0;

    const quint32 currentTimeValue = quint32(qMax(0, cursorTime));
    if (currentTimeValue <= sf->startTime())
        return 0;

    return qMin(sf->duration(), currentTimeValue - sf->startTime());
}

quint32 ShowManager::mapCursorToChaserTime(const ShowFunction *sf, Chaser *chaser, int cursorTime) const
{
    if (sf == nullptr || chaser == nullptr)
        return 0;

    quint32 itemRelativeTime = itemRelativeTimeFromCursor(sf, cursorTime);
    quint32 chaserRelativeTime = itemRelativeTime;
    quint32 chaserTotal = chaser->totalDuration();
    if (sf->duration() > 0 && chaserTotal > 0)
    {
        chaserRelativeTime = quint32(qRound((double(itemRelativeTime) * double(chaserTotal))
                                            / double(sf->duration())));
    }

    return chaserRelativeTime;
}

quint32 ShowManager::chaserStepDuration(Chaser *chaser, int index) const
{
    if (chaser == nullptr || index < 0 || index >= chaser->stepsCount())
        return 0;

    if (chaser->durationMode() == Chaser::Common)
        return chaser->duration();

    ChaserStep *step = chaser->stepAt(index);
    return step ? step->duration : 0;
}

int ShowManager::chaserStepIndexFromTime(Chaser *chaser, quint32 timeValue) const
{
    if (chaser == nullptr || chaser->stepsCount() == 0)
        return -1;

    quint32 elapsed = 0;
    for (int i = 0; i < chaser->stepsCount(); ++i)
    {
        quint32 stepDuration = chaserStepDuration(chaser, i);
        if (stepDuration == 0)
            stepDuration = 1;

        if (timeValue < elapsed + stepDuration)
            return i;

        elapsed += stepDuration;
    }

    return chaser->stepsCount() - 1;
}

bool ShowManager::setChaserStepDurationWithUndo(Chaser *chaser, int stepIndex, quint32 newDuration)
{
    if (chaser == nullptr || stepIndex < 0 || stepIndex >= chaser->stepsCount())
        return false;

    ChaserStep *stepRef = chaser->stepAt(stepIndex);
    if (stepRef == nullptr)
        return false;

    ChaserStep step = *stepRef;
    newDuration = qMax(quint32(1), newDuration);
    if (step.duration == newDuration)
        return true;

    UIntPair oldDuration(stepIndex, step.duration);
    UIntPair oldHold(stepIndex, step.hold);

    step.duration = newDuration;
    step.hold = Function::speedSubtract(step.duration, step.fadeIn);

    Tardis::instance()->enqueueAction(Tardis::ChaserSetStepDuration, chaser->id(),
                                      QVariant::fromValue(oldDuration),
                                      QVariant::fromValue(UIntPair(stepIndex, step.duration)));
    Tardis::instance()->enqueueAction(Tardis::ChaserSetStepHold, chaser->id(),
                                      QVariant::fromValue(oldHold),
                                      QVariant::fromValue(UIntPair(stepIndex, step.hold)));
    chaser->replaceStep(step, stepIndex);
    return true;
}

void ShowManager::convertChaserCommonToPerStep(Chaser *chaser)
{
    if (chaser == nullptr || chaser->durationMode() != Chaser::Common)
        return;

    quint32 commonDuration = qMax(quint32(1), chaser->duration());
    chaser->setDurationMode(Chaser::PerStep);
    for (int i = 0; i < chaser->stepsCount(); ++i)
    {
        ChaserStep *stepRef = chaser->stepAt(i);
        if (stepRef == nullptr)
            continue;

        setChaserStepDurationWithUndo(chaser, i, commonDuration);
    }
}

void ShowManager::setShowItemDurationWithUndo(ShowFunction *sf, int newDuration)
{
    if (sf == nullptr)
        return;

    Tardis::instance()->enqueueAction(Tardis::ShowManagerItemSetDuration, sf->id(), sf->duration(), newDuration);
    sf->setDuration(newDuration);
}

bool ShowManager::moveAllItemsAfterCursor(int cursorTime, int delta)
{
    if (m_currentShow == nullptr || delta == 0)
        return true;

    QList<ShowFunction *> itemsToMove;
    foreach (Track *track, m_currentShow->tracks())
    {
        foreach (ShowFunction *sf, track->showFunctions())
        {
            if (sf == nullptr)
                continue;

            if (int(sf->startTime()) <= cursorTime)
                continue;

            itemsToMove.append(sf);
        }
    }

    std::sort(itemsToMove.begin(), itemsToMove.end(),
              [delta](ShowFunction *a, ShowFunction *b)
              {
                  if (delta > 0)
                      return a->startTime() > b->startTime();
                  return a->startTime() < b->startTime();
              });

    for (ShowFunction *sf : itemsToMove)
    {
        int newStart = int(sf->startTime()) + delta;
        if (newStart < 0)
            newStart = 0;

        if (setShowItemStartTime(sf, newStart) == false)
            return false;
    }

    return true;
}

bool ShowManager::insertShowItemTime(ShowFunction *sf, int length)
{
    return insertShowItemTimeAt(sf, length, m_currentTime);
}

bool ShowManager::insertShowItemTimeAt(ShowFunction *sf, int length, int cursorTime)
{
    if (m_currentShow == nullptr || sf == nullptr || length <= 0)
        return false;

    Function *func = m_doc->function(sf->functionID());
    if (func == nullptr)
        return false;

    Track *track = m_currentShow->getTrackFromShowFunctionID(sf->id());
    if (track == nullptr)
        return false;

    int minDuration = minimumTimelineDuration(timeDivision());

    switch (func->type())
    {
        case Function::AudioType:
        case Function::VideoType:
        {
            if (func->runOrder() != Function::Loop)
                return false;
        }
        Q_FALLTHROUGH();
        case Function::SceneType:
        case Function::CollectionType:
        case Function::EFXType:
        case Function::RGBMatrixType:
        {
            int newDuration = sf->duration() + length;
            if (newDuration < minDuration)
                newDuration = minDuration;

            if (checkOverlapping(track, sf, sf->startTime(), newDuration))
                return false;

            setShowItemDurationWithUndo(sf, newDuration);
            m_doc->setModified();
            return true;
        }
        case Function::ChaserType:
        case Function::SequenceType:
        {
            Chaser *chaser = qobject_cast<Chaser *>(func);
            if (chaser == nullptr)
                return false;

            int stepsCount = chaser->stepsCount();
            if (stepsCount == 0 && func->type() != Function::SequenceType)
                return false;

            int newItemDuration = int(sf->duration()) + length;
            if (newItemDuration < minDuration)
                newItemDuration = minDuration;
            if (checkOverlapping(track, sf, sf->startTime(), newItemDuration))
                return false;

            quint32 chaserRelativeTime = mapCursorToChaserTime(sf, chaser, cursorTime);

            int insertIndex = chaserStepIndexFromTime(chaser, chaserRelativeTime);
            if (insertIndex < 0)
                return false;

            // In Common mode, all steps share one duration: switch to PerStep first
            // so we can stretch only the step covering the cursor.
            convertChaserCommonToPerStep(chaser);

            ChaserStep *targetStepRef = chaser->stepAt(insertIndex);
            if (targetStepRef == nullptr)
                return false;

            quint32 targetDuration = targetStepRef->duration + quint32(length);
            if (setChaserStepDurationWithUndo(chaser, insertIndex, targetDuration) == false)
                return false;

            setShowItemDurationWithUndo(sf, newItemDuration);
            m_doc->setModified();
            return true;
        }
        default:
        break;
    }

    return false;
}

bool ShowManager::cutShowItemTime(ShowFunction *sf, int length)
{
    return cutShowItemTimeAt(sf, length, m_currentTime);
}

bool ShowManager::cutShowItemTimeAt(ShowFunction *sf, int length, int cursorTime)
{
    if (m_currentShow == nullptr || sf == nullptr || length <= 0)
        return false;

    Function *func = m_doc->function(sf->functionID());
    if (func == nullptr)
        return false;

    int minDuration = minimumTimelineDuration(timeDivision());
    int maxCutDuration = int(sf->duration()) - minDuration;
    if (maxCutDuration <= 0)
        return false;

    int targetCutDuration = qMin(length, maxCutDuration);

    switch (func->type())
    {
        case Function::AudioType:
        case Function::VideoType:
        {
            if (func->runOrder() != Function::Loop)
                return false;
        }
        Q_FALLTHROUGH();
        case Function::SceneType:
        case Function::CollectionType:
        case Function::EFXType:
        case Function::RGBMatrixType:
        {
            int newDuration = int(sf->duration()) - targetCutDuration;
            if (newDuration < minDuration)
                newDuration = minDuration;

            setShowItemDurationWithUndo(sf, newDuration);
            m_doc->setModified();
            return true;
        }
        case Function::ChaserType:
        case Function::SequenceType:
        {
            Chaser *chaser = qobject_cast<Chaser *>(func);
            if (chaser == nullptr || chaser->stepsCount() == 0)
                return false;

            convertChaserCommonToPerStep(chaser);

            quint32 chaserRelativeTime = mapCursorToChaserTime(sf, chaser, cursorTime);

            int cutStartIndex = chaserStepIndexFromTime(chaser, chaserRelativeTime);
            if (cutStartIndex < 0)
                return false;

            quint32 stepStartTime = 0;
            for (int i = 0; i < cutStartIndex; ++i)
                stepStartTime += qMax(quint32(1), chaserStepDuration(chaser, i));

            int cutRemaining = targetCutDuration;
            int cutDuration = 0;
            int stepIndex = cutStartIndex;
            quint32 cursorOffset = chaserRelativeTime > stepStartTime ? (chaserRelativeTime - stepStartTime) : 0;

            while (cutRemaining > 0 && stepIndex < chaser->stepsCount())
            {
                ChaserStep *stepRef = chaser->stepAt(stepIndex);
                if (stepRef == nullptr)
                    break;

                ChaserStep step = *stepRef;
                quint32 stepDuration = qMax(quint32(1), step.duration);
                quint32 offset = qMin(cursorOffset, stepDuration);
                int removable = (stepIndex == cutStartIndex) ? int(stepDuration - offset) : int(stepDuration);
                if (removable <= 0)
                {
                    cursorOffset = 0;
                    stepIndex++;
                    continue;
                }

                int consume = qMin(cutRemaining, removable);

                if (stepIndex == cutStartIndex && offset > 0)
                {
                    quint32 newStepDuration = stepDuration;
                    if (consume < removable)
                        newStepDuration = qMax(quint32(1), quint32(int(stepDuration) - consume));
                    else
                        newStepDuration = qMax(quint32(1), quint32(offset));
                    setChaserStepDurationWithUndo(chaser, stepIndex, newStepDuration);

                    cutDuration += consume;
                    cutRemaining -= consume;
                    cursorOffset = 0;
                    if (consume < removable)
                        break;
                    stepIndex++;
                    continue;
                }

                if (consume < removable)
                {
                    quint32 newStepDuration = qMax(quint32(1), quint32(int(stepDuration) - consume));
                    setChaserStepDurationWithUndo(chaser, stepIndex, newStepDuration);

                    cutDuration += consume;
                    cutRemaining = 0;
                    break;
                }

                if (chaser->stepsCount() <= 1)
                {
                    quint32 newStepDuration = 1;
                    int actualConsume = int(stepDuration - newStepDuration);
                    if (actualConsume <= 0)
                        break;

                    setChaserStepDurationWithUndo(chaser, stepIndex, newStepDuration);

                    cutDuration += actualConsume;
                    cutRemaining -= actualConsume;
                    break;
                }

                Tardis::instance()->enqueueAction(Tardis::ChaserRemoveStep, chaser->id(),
                                                  Tardis::instance()->actionToByteArray(Tardis::ChaserRemoveStep,
                                                                                        chaser->id(), stepIndex),
                                                  QVariant());
                if (chaser->removeStep(stepIndex) == false)
                    break;

                cutDuration += consume;
                cutRemaining -= consume;
            }

            if (cutDuration <= 0)
                return false;

            int newDuration = int(sf->duration()) - cutDuration;
            if (newDuration < minDuration)
                newDuration = minDuration;

            setShowItemDurationWithUndo(sf, newDuration);
            m_doc->setModified();
            return true;
        }
        default:
        break;
    }

    return false;
}

bool ShowManager::insertTimeAtCursor(int length, int cursorTime)
{
    if (m_currentShow == nullptr || length <= 0)
        return false;

    bool hasTarget = false;
    bool hasItemsAfterCursor = false;
    foreach (Track *track, m_currentShow->tracks())
    {
        foreach (ShowFunction *sf, track->showFunctions())
        {
            if (sf == nullptr || sf->isLocked())
                continue;

            int startTime = int(sf->startTime());
            if (startTime > cursorTime)
                hasItemsAfterCursor = true;

            int endTime = startTime + int(sf->duration());
            if (cursorTime < startTime || cursorTime > endTime)
                continue;

            hasTarget = true;
            if (hasItemsAfterCursor)
                break;
        }

        if (hasTarget && hasItemsAfterCursor)
            break;
    }

    if (!hasTarget && !hasItemsAfterCursor)
        return false;

    if (moveAllItemsAfterCursor(cursorTime, length) == false)
        return false;

    bool changed = false;
    foreach (Track *track, m_currentShow->tracks())
    {
        foreach (ShowFunction *sf, track->showFunctions())
        {
            if (sf == nullptr || sf->isLocked())
                continue;

            int startTime = int(sf->startTime());
            int endTime = startTime + int(sf->duration());
            if (cursorTime < startTime || cursorTime > endTime)
                continue;

            changed |= insertShowItemTimeAt(sf, length, cursorTime);
        }
    }

    if (hasTarget && !hasItemsAfterCursor && !changed)
        moveAllItemsAfterCursor(cursorTime, -length);

    return changed || hasItemsAfterCursor;
}

bool ShowManager::cutTimeAtCursor(int length, int cursorTime)
{
    if (m_currentShow == nullptr || length <= 0)
        return false;

    bool changed = false;
    foreach (Track *track, m_currentShow->tracks())
    {
        foreach (ShowFunction *sf, track->showFunctions())
        {
            if (sf == nullptr || sf->isLocked())
                continue;

            int startTime = int(sf->startTime());
            int endTime = startTime + int(sf->duration());
            if (cursorTime < startTime || cursorTime > endTime)
                continue;

            changed |= cutShowItemTimeAt(sf, length, cursorTime);
        }
    }

    if (!changed)
        return false;

    moveAllItemsAfterCursor(cursorTime, -length);

    return changed;
}

void ShowManager::resetContents()
{
    stopPreview();
    resetView();
    m_currentTime = 0;
    emit currentTimeChanged(m_currentTime);

    m_selectedTrackId = -1;
    m_cursorMovedDuringPause = false;

    if (m_currentShow != nullptr)
    {
        disconnect(m_currentShow, SIGNAL(timeChanged(quint32)), this, SLOT(slotTimeChanged(quint32)));
        disconnect(m_currentShow, SIGNAL(showFinished()), this, SLOT(slotShowFinished()));
        disconnect(m_currentShow, SIGNAL(stopped(quint32)), this, SLOT(slotShowStopped()));
    }

    m_currentShow = nullptr;

    // the clipboard holds ShowFunction pointers belonging to the show
    // being closed, so drop them to avoid dangling references
    if (m_clipboard.isEmpty() == false)
    {
        m_clipboard.clear();
        emit clipboardItemsCountChanged(0);
    }

    emit tracksChanged();
    emit isEditingChanged();
    setPlaybackState(false, false);
}

void ShowManager::resetView()
{
    // the selection holds raw pointers to the items deleted below
    // (refreshView() after an undo goes through here too)
    if (m_selectedItems.isEmpty() == false)
    {
        m_selectedItems.clear();
        emit selectedItemsCountChanged(0);
    }

    QMapIterator<quint32, QQuickItem*> it(m_itemsMap);
    while (it.hasNext())
    {
        it.next();
        delete it.value();
    }
    m_itemsMap.clear();
}

void ShowManager::renderView(QQuickItem *parent)
{
    resetView();

    if (m_currentShow == nullptr)
        return;

    setContextItem(parent);

    int trkIdx = 0;

    foreach (Track *track, m_currentShow->tracks())
    {
        foreach (ShowFunction *sf, track->showFunctions())
        {
            Function *func = m_doc->function(sf->functionID());
            if (func == nullptr)
                continue;

            QQuickItem *newItem = qobject_cast<QQuickItem*>(siComponent->create());

            newItem->setParentItem(parent);
            newItem->setProperty("trackIndex", trkIdx);
            newItem->setProperty("sfRef", QVariant::fromValue(sf));
            newItem->setProperty("funcRef", QVariant::fromValue(func));

            m_itemsMap[sf->id()] = newItem;
        }

        trkIdx++;
    }
}

void ShowManager::enableFlicking(bool enable)
{
    QQuickItem *flickable = qobject_cast<QQuickItem*>(m_view->rootObject()->findChild<QObject *>("showItemsArea"));
    flickable->setProperty("interactive", enable);
}

int ShowManager::showDuration() const
{
    if (m_currentShow == nullptr)
        return 0;

    return m_currentShow->totalDuration();
}

void ShowManager::playShow()
{
    if (m_currentShow == nullptr)
        return;

    if (m_isPreviewing)
    {
        // The frozen runner already sits at the cursor with its clips
        // started: leaving scrub mode lets it play on from there (it is
        // running and not paused, so the branches below do not apply).
        m_currentShow->setScrubMode(false);
        setPreviewing(false);
        setPlaybackState(true, false);
        return;
    }

    if (m_currentShow->isRunning() == false)
    {
        m_cursorMovedDuringPause = false;
        // Edits queue their schedule rebuild on the event loop; make sure the
        // runner's very first tick already plays the timeline as it is now.
        m_currentShow->rebuildSchedule();
        m_currentShow->start(m_doc->masterTimer(), FunctionParent::master(FunctionParent::ShowManagerPlayback), m_currentTime);
        setPlaybackState(true, false);
        return;
    }

    if (m_currentShow->isPaused())
    {
        if (m_cursorMovedDuringPause)
        {
            m_currentShow->stop(FunctionParent::master(FunctionParent::ShowManagerPlayback));
            m_currentShow->stopAndWait(FunctionParent::master(FunctionParent::ShowManagerPlayback));
            m_cursorMovedDuringPause = false;
            m_currentShow->rebuildSchedule();
            m_currentShow->start(m_doc->masterTimer(), FunctionParent::master(FunctionParent::ShowManagerPlayback), m_currentTime);
        }
        else
        {
            m_currentShow->setPause(false);
        }

        setPlaybackState(true, false);
        return;
    }

    m_currentShow->setPause(true);
    setPlaybackState(true, true);
}

void ShowManager::stopShow()
{
    if (m_isPreviewing)
    {
        // the cursor stays where it is; a second stop rewinds it below
        stopPreview();
        setPlaybackState(false, false);
        return;
    }

    if (m_currentShow != nullptr && m_currentShow->isRunning())
    {
        m_cursorMovedDuringPause = false;
        m_currentShow->stop(FunctionParent::master(FunctionParent::ShowManagerPlayback));
        setPlaybackState(false, false);
        return;
    }

    setPlaybackState(false, false);

    if (m_currentTime != 0)
    {
        m_currentTime = 0;
        emit currentTimeChanged(m_currentTime);
    }
}

bool ShowManager::isPlaying() const
{
    return m_isPlaying;
}

bool ShowManager::isPaused() const
{
    return m_isPaused;
}

bool ShowManager::previewEnabled() const
{
    return m_previewEnabled;
}

void ShowManager::setPreviewEnabled(bool enable)
{
    if (m_previewEnabled == enable)
        return;

    m_previewEnabled = enable;
    emit previewEnabledChanged(enable);

    if (enable == false)
        stopPreview();
}

bool ShowManager::isPreviewing() const
{
    return m_isPreviewing;
}

void ShowManager::enableContext(bool enable)
{
    PreviewContext::enableContext(enable);

    if (enable == false)
        stopPreview();
}

void ShowManager::previewAt(int time)
{
    if (m_currentShow == nullptr)
        return;

    quint32 position = quint32(qMax(0, time));

    if (m_isPreviewing)
    {
        // the runner coalesces requests posted between two ticks
        m_currentShow->requestSeek(position);
        return;
    }

    // A playing Show keeps its cursor. Note isRunning() stays true for one
    // tick after a stop, so a click right after stopping the Show does not
    // preview yet.
    if (m_currentShow->isRunning())
    {
        if (m_currentShow->isPaused() == false)
            return;

        // Paused: hand the runner over to the frozen scrub state at the new
        // cursor. Scrub mode goes on first so that the runner keeps its
        // clips held through the unpause (see ShowRunner::setPause) and its
        // first tick seeks them; from here on this is a preview like the
        // stopped case - play continues from the cursor.
        m_cursorMovedDuringPause = false;
        m_currentShow->setScrubMode(true);
        m_currentShow->requestSeek(position);
        m_currentShow->setPause(false);
        setPreviewing(true);
        setPlaybackState(false, false);
        return;
    }

    m_currentShow->rebuildSchedule();
    m_currentShow->setScrubMode(true);
    m_currentShow->start(m_doc->masterTimer(), FunctionParent::master(FunctionParent::ShowManagerPlayback), position);
    setPreviewing(true);
}

void ShowManager::stopPreview()
{
    if (m_isPreviewing == false)
        return;

    if (m_currentShow != nullptr)
        m_currentShow->stop(FunctionParent::master(FunctionParent::ShowManagerPlayback));

    setPreviewing(false);
}

void ShowManager::setPreviewing(bool previewing)
{
    if (m_isPreviewing == previewing)
        return;

    m_isPreviewing = previewing;
    emit isPreviewingChanged(previewing);
}

QColor ShowManager::itemsColor() const
{
    return m_itemsColor;
}

void ShowManager::setItemsColor(QColor itemsColor)
{
    if (m_itemsColor == itemsColor)
        return;

    m_itemsColor = itemsColor;
    emit itemsColorChanged(itemsColor);
}

int ShowManager::selectedItemsCount() const
{
    return m_selectedItems.count();
}

int ShowManager::clipboardItemsCount() const
{
    return m_clipboard.count();
}

bool ShowManager::multipleSelection() const
{
    return m_multipleSelection;
}

void ShowManager::setMultipleSelection(bool multipleSelection)
{
    if (m_multipleSelection == multipleSelection)
        return;

    m_multipleSelection = multipleSelection;
    emit multipleSelectionChanged();
}

bool ShowManager::isSelected(ShowFunction *sf) const
{
    for (const SelectedShowItem &si : m_selectedItems)
    {
        if (si.m_showFunc == sf)
            return true;
    }
    return false;
}

bool ShowManager::addToSelection(int trackIdx, ShowFunction *sf, QQuickItem *item)
{
    if (sf == nullptr || isSelected(sf))
        return false;

    SelectedShowItem selection;
    selection.m_trackIndex = trackIdx;
    selection.m_showFunc = sf;
    selection.m_item = item;
    m_selectedItems.append(selection);

    if (item != nullptr)
        item->setProperty("isSelected", true);

    return true;
}

bool ShowManager::removeFromSelection(ShowFunction *sf)
{
    for (int i = 0; i < m_selectedItems.count(); i++)
    {
        SelectedShowItem si = m_selectedItems.at(i);
        if (si.m_showFunc == sf)
        {
            if (si.m_item != nullptr)
                si.m_item->setProperty("isSelected", false);
            m_selectedItems.removeAt(i);
            return true;
        }
    }
    return false;
}

bool ShowManager::clearSelection()
{
    if (m_selectedItems.isEmpty())
        return false;

    foreach (SelectedShowItem ssi, m_selectedItems)
    {
        if (ssi.m_item != nullptr)
            ssi.m_item->setProperty("isSelected", false);
    }
    m_selectedItems.clear();
    return true;
}

void ShowManager::setItemSelection(int trackIdx, ShowFunction *sf, QQuickItem *item, bool selected)
{
    bool allowMulti = m_multipleSelection;
    bool changed = false;

    if (selected == true)
    {
        if (!allowMulti)
        {
            for (int i = m_selectedItems.count() - 1; i >= 0; --i)
            {
                if (m_selectedItems.at(i).m_showFunc == sf)
                    continue;
                changed |= removeFromSelection(m_selectedItems.at(i).m_showFunc);
            }
        }

        changed |= addToSelection(trackIdx, sf, item);
    }
    else
    {
        changed |= removeFromSelection(sf);
    }
    if (changed)
        emit selectedItemsCountChanged(m_selectedItems.count());
    emit itemClicked(App::ShowDragItem);
}

void ShowManager::selectItemByClick(int trackIdx, ShowFunction *sf, QQuickItem *item)
{
    if (m_currentShow == nullptr || sf == nullptr)
        return;

    bool changed = false;

    if (m_multipleSelection)
    {
        if (isSelected(sf))
            changed |= removeFromSelection(sf);
        else
            changed |= addToSelection(trackIdx, sf, item);
    }
    else
    {
        for (int i = m_selectedItems.count() - 1; i >= 0; --i)
        {
            if (m_selectedItems.at(i).m_showFunc == sf)
                continue;
            changed |= removeFromSelection(m_selectedItems.at(i).m_showFunc);
        }
        changed |= addToSelection(trackIdx, sf, item);
    }

    if (changed)
        emit selectedItemsCountChanged(m_selectedItems.count());
    emit itemClicked(App::ShowDragItem);
}

void ShowManager::selectItemsInRect(qreal x, qreal y, qreal width, qreal height)
{
    if (m_currentShow == nullptr)
        return;

    QRectF band = QRectF(x, y, width, height).normalized();
    bool changed = clearSelection();

    QMapIterator<quint32, QQuickItem *> it(m_itemsMap);
    while (it.hasNext())
    {
        it.next();
        QQuickItem *item = it.value();
        QRectF geometry(item->x(), item->y(), item->width(), item->height());
        if (!band.intersects(geometry))
            continue;

        ShowFunction *sf = m_currentShow->showFunction(it.key());
        if (sf == nullptr)
            continue;

        changed |= addToSelection(item->property("trackIndex").toInt(), sf, item);
    }

    if (changed)
        emit selectedItemsCountChanged(m_selectedItems.count());
    // so that the Delete key (ContextManager::deleteSelectedItems) targets
    // Show items, exactly as after a click on one
    emit itemClicked(App::ShowDragItem);
}

void ShowManager::selectAllItems()
{
    if (m_currentShow == nullptr)
        return;

    bool changed = false;
    QMapIterator<quint32, QQuickItem *> it(m_itemsMap);
    while (it.hasNext())
    {
        it.next();
        ShowFunction *sf = m_currentShow->showFunction(it.key());
        if (sf == nullptr)
            continue;
        changed |= addToSelection(it.value()->property("trackIndex").toInt(), sf, it.value());
    }

    if (changed)
        emit selectedItemsCountChanged(m_selectedItems.count());
    emit itemClicked(App::ShowDragItem);
}

void ShowManager::resetItemsSelection()
{
    clearSelection();
    emit selectedItemsCountChanged(m_selectedItems.count());
}

QVariantList ShowManager::selectedItemRefs() const
{
    QVariantList list;
    foreach (SelectedShowItem si, m_selectedItems)
    {
        if (si.m_showFunc != nullptr)
            list.append(QVariant::fromValue(si.m_showFunc.data()));
    }
    return list;
}

QStringList ShowManager::selectedItemNames() const
{
    QStringList names;
    foreach (SelectedShowItem si, m_selectedItems)
    {
        if (si.m_showFunc == nullptr)
            continue;

        Function *func = m_doc->function(si.m_showFunc->functionID());
        if (func != nullptr)
            names.append(func->name());
    }

    return names;
}

bool ShowManager::selectedItemsLocked() const
{
    foreach (SelectedShowItem si, m_selectedItems)
    {
        if (si.m_showFunc != nullptr && si.m_showFunc->isLocked())
            return true;
    }
    return false;
}

void ShowManager::setSelectedItemsLock(bool lock)
{
    foreach (SelectedShowItem si, m_selectedItems)
    {
        if (si.m_showFunc != nullptr)
            si.m_showFunc->setLocked(lock);
    }
}

void ShowManager::slotTimeChanged(quint32 msec_time)
{
    m_currentTime = (int)msec_time;
    emit currentTimeChanged(m_currentTime);
}

void ShowManager::slotShowFinished()
{
    stopShow();
}

void ShowManager::slotShowStopped()
{
    setPlaybackState(false, false);
    // also a preview stopped from elsewhere (e.g. "stop all functions")
    setPreviewing(false);
}

void ShowManager::slotScheduleChanged()
{
    if (m_currentShow != nullptr)
        emit showDurationChanged(m_currentShow->totalDuration());
}

void ShowManager::setPlaybackState(bool playing, bool paused)
{
    if (playing == false)
        paused = false;

    if (m_isPlaying != playing)
    {
        m_isPlaying = playing;
        emit isPlayingChanged(m_isPlaying);
    }

    if (m_isPaused != paused)
    {
        m_isPaused = paused;
        emit isPausedChanged(m_isPaused);
    }
}

bool ShowManager::checkOverlapping(Track *track, ShowFunction *sourceFunc,
                                   quint32 startTime, quint32 duration) const
{
    if (track == nullptr)
        return false;

    foreach (ShowFunction *sf, track->showFunctions())
    {
        if (sf == sourceFunc)
            continue;

        Function *func = m_doc->function(sf->functionID());
        if (func != nullptr)
        {
            quint32 fst = sf->startTime();
            // half-open intervals: touching edges are legal, since snapping (getSnapEdges()
            // includes item end edges) deliberately places clips back-to-back
            if (startTime < fst + sf->duration() && fst < startTime + duration)
            {
                return true;
            }
        }
    }

    return false;
}

QVariantList ShowManager::previewData(Function *f) const
{
    QVariantList data;
    if (f == nullptr)
        return data;

    switch (f->type())
    {
        case Function::ChaserType:
        case Function::SequenceType:
        {
            Chaser *chaser = qobject_cast<Chaser *>(f);
            quint32 stepsTimeCounter = 0;

            foreach (ChaserStep step, chaser->steps())
            {
                uint stepFadeIn = step.fadeIn;
                uint stepFadeOut = step.fadeOut;
                uint stepDuration = step.duration;
                if (chaser->fadeInMode() == Chaser::Common)
                    stepFadeIn = chaser->fadeInSpeed();
                if (chaser->fadeOutMode() == Chaser::Common)
                    stepFadeOut = chaser->fadeOutSpeed();
                if (chaser->durationMode() == Chaser::Common)
                    stepDuration = chaser->duration();

                stepsTimeCounter += stepDuration;

                if (stepFadeIn > 0)
                {
                    data.append(FadeIn);
                    data.append(stepFadeIn);
                }
                data.append(StepDivider);
                data.append(stepsTimeCounter);

                if (stepFadeOut > 0)
                {
                    data.append(FadeOut);
                    data.append(stepFadeOut);
                }
            }
        }
        break;

        /* All the other Function types */
        case Function::AudioType:
        case Function::VideoType:
        {
            data.append(RepeatingDuration);
            data.append(f->totalDuration());
            data.append(FadeIn);
            data.append(f->fadeInSpeed());
            data.append(FadeOut);
            data.append(f->fadeOutSpeed());
        }
        break;
        default:
        {
            data.append(RepeatingDuration);
            data.append(f->totalDuration());
        }
        break;
    }

    return data;
}

QVariantList ShowManager::beatGridData(Function *f) const
{
    QVariantList data;
    if (f == nullptr || f->type() != Function::AudioType)
        return data;

    Audio *audio = qobject_cast<Audio *>(f);
    if (audio == nullptr || audio->bpmAnalysisState() != Audio::Done)
        return data;

    double bpm = audio->detectedBpm();
    if (bpm <= 0.0)
        return data;

    double periodMs = 60000.0 / bpm;
    double phaseMs = audio->beatPhaseMs();
    double totalMs = static_cast<double>(f->totalDuration());

    for (int k = 0; ; k++)
    {
        double t = phaseMs + (double)k * periodMs;
        if (t > totalMs)
            break;
        data.append(t);
    }

    return data;
}

void ShowManager::copyToClipboard()
{
    m_clipboard.clear();

    for (SelectedShowItem item : m_selectedItems)
        m_clipboard.append(item);

    emit clipboardItemsCountChanged(m_clipboard.count());
}

void ShowManager::pasteFromClipboard()
{
    if (m_currentShow == nullptr || m_clipboard.isEmpty())
        return;

    QList<Track *> tracks = m_currentShow->tracks();

    // The copies to make. Each source's track is resolved from the
    // ShowFunction itself: the index stored at selection time may have gone
    // stale since (tracks moved or deleted), see planGroupMove().
    struct PasteSource
    {
        ShowFunction *sf;
        Function *func;
        int trackIdx;
    };
    QList<PasteSource> sources;
    QList<ShowMoveItem> items;

    for (const SelectedShowItem &ssi : m_clipboard)
    {
        ShowFunction *sf = ssi.m_showFunc;
        if (sf == nullptr)
            continue;

        int trackIdx = tracks.indexOf(m_currentShow->getTrackFromShowFunctionID(sf->id()));
        Function *func = m_doc->function(sf->functionID());
        if (trackIdx < 0 || func == nullptr)
            continue;

        // a Sequence whose bound Scene is gone cannot run, so skip it
        if (func->type() == Function::SequenceType)
        {
            Sequence *sequence = qobject_cast<Sequence*>(func);
            if (m_doc->function(sequence->boundSceneID()) == nullptr)
                continue;
        }

        sources.append({ sf, func, trackIdx });

        ShowMoveItem item;
        item.id = sf->id();
        item.trackIndex = trackIdx;
        item.startTime = sf->startTime();
        item.duration = sf->duration();
        items.append(item);
    }

    if (sources.isEmpty())
        return;

    // the earliest copy goes to the cursor, the others keep their relative
    // time offsets and stay on their sources' tracks; collisions are
    // resolved like a drop, and a group that still does not fit is refused
    // as a whole (a per-item scatter would break its layout)
    ShowGroupMoveResult plan = ShowMoveHelper::planPaste(trackSpans(), items, m_currentTime);
    if (!plan.ok)
    {
        ShowFunction *blocker = m_currentShow->showFunction(plan.blockingId);
        Function *blockingFunc = blocker ? m_doc->function(blocker->functionID()) : nullptr;
        emit pasteRefused(blockingFunc ? blockingFunc->name() : tr("another item"));
        return;
    }

    // All the copies are created back-to-back so Tardis batches their
    // ShowManagerAddFunction actions into one undo step. The pasted clips
    // become the selection, as they are what the user works on next.
    clearSelection();
    QQuickItem *parent = contextItem();

    for (const PasteSource &src : sources)
    {
        ShowFunction *copy = createShowItem(parent, tracks.at(src.trackIdx), src.trackIdx, src.func,
                                            int(src.sf->startTime() + plan.timeDelta), src.sf);
        addToSelection(src.trackIdx, copy, m_itemsMap.value(copy->id(), nullptr));
    }

    emit showDurationChanged(m_currentShow->totalDuration());
    emit selectedItemsCountChanged(m_selectedItems.count());
    emit itemClicked(App::ShowDragItem);
}

double ShowManager::legacyBeatPseudoUnitToMs(int bpmNumber)
{
    if (bpmNumber <= 0)
        return 0.0;

    // Old encoding (TimeUtils.js posToBeat(), pre-e008dd107) stored
    // beatCount * 1000. Inverse of TimingUtils.qml's msToBeatPseudo()
    // (round((ms / (60000 / bpm)) * 1000)): realMs = pseudo * (60000/bpm) / 1000
    return (60000.0 / bpmNumber) / 1000.0;
}

QVariantMap ShowManager::legacyShowConversionInfo(int showId) const
{
    QVariantMap info;

    Show *show = qobject_cast<Show *>(m_doc->function(quint32(showId)));
    if (show == nullptr)
        return info;

    int itemCount = 0;
    foreach (Track *track, show->tracks())
        itemCount += track->showFunctions().count();

    info["name"] = show->name();
    info["bpm"] = show->timeDivisionBPM();
    info["beatsDivision"] = show->beatsDivision();
    info["itemCount"] = itemCount;

    return info;
}

QVariantList ShowManager::legacyShowConversionPreview(int showId, int bpmNumber) const
{
    QVariantList preview;

    Show *show = qobject_cast<Show *>(m_doc->function(quint32(showId)));
    double msPerUnit = legacyBeatPseudoUnitToMs(bpmNumber);
    if (show == nullptr || msPerUnit <= 0.0)
        return preview;

    QList<ShowFunction *> items;
    foreach (Track *track, show->tracks())
        items << track->showFunctions();

    std::sort(items.begin(), items.end(), [](ShowFunction *a, ShowFunction *b) {
        return a->startTime() < b->startTime();
    });

    // first/last 3 items (all of them if there are 6 or fewer), per the ADR
    const int sampleEdge = 3;
    for (int i = 0; i < items.count(); i++)
    {
        if (items.count() > sampleEdge * 2 && i >= sampleEdge && i < items.count() - sampleEdge)
            continue;

        ShowFunction *sf = items.at(i);
        Function *target = m_doc->function(sf->functionID());

        QVariantMap row;
        row["name"] = target != nullptr ? target->name() : tr("Unknown function");
        row["oldStart"] = sf->startTime();
        row["newStart"] = quint32(qRound(sf->startTime() * msPerUnit));
        row["oldDuration"] = sf->duration();
        row["newDuration"] = quint32(qRound(sf->duration() * msPerUnit));
        preview << row;
    }

    return preview;
}

bool ShowManager::convertLegacyBeatShow(int showId, int bpmNumber)
{
    Show *show = qobject_cast<Show *>(m_doc->function(quint32(showId)));
    double msPerUnit = legacyBeatPseudoUnitToMs(bpmNumber);
    if (show == nullptr || msPerUnit <= 0.0)
        return false;

    // Tardis's undo/redo for ShowManagerItemSetStartTime/Duration resolves
    // the ShowFunction id against ShowManager::currentShow() (ids are only
    // unique within a Show, not globally - see showfunction.h), exactly
    // like every other ShowFunction mutator in this class. So this Show
    // must be made current for the duration of the conversion.
    int previousShowID = currentShowID();
    if (previousShowID != showId)
        setCurrentShowID(showId);

    foreach (Track *track, show->tracks())
    {
        foreach (ShowFunction *sf, track->showFunctions())
        {
            quint32 newStart = quint32(qRound(sf->startTime() * msPerUnit));
            if (newStart != sf->startTime())
            {
                Tardis::instance()->enqueueAction(Tardis::ShowManagerItemSetStartTime, sf->id(),
                                                   sf->startTime(), newStart);
                sf->setStartTime(newStart);
            }

            quint32 newDuration = quint32(qRound(sf->duration() * msPerUnit));
            if (newDuration != sf->duration())
            {
                Tardis::instance()->enqueueAction(Tardis::ShowManagerItemSetDuration, sf->id(),
                                                   sf->duration(), newDuration);
                sf->setDuration(newDuration);
            }
        }
    }

    m_doc->setModified();
    refreshView();

    if (previousShowID != showId)
        setCurrentShowID(previousShowID);

    return true;
}
