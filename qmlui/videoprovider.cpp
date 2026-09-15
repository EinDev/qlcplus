/*
  Q Light Controller Plus
  videoprovider.cpp

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

#include <QGuiApplication>
#include <QMediaMetaData>
#include <QQmlContext>
#include <QScreen>

#include "videoprovider.h"
#include "videoframeprobe.h"
#include "track.h"
#include "show.h"
#include "doc.h"

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
#include "spoutvideoplayer.h"
#include "spoutsender.h"
#endif

/** Fade in/out times of a Video, in ms, 0 = none (mirrors VideoContext.qml) */
static void videoFadeTimes(const Video *video, int &fadeIn, int &fadeOut)
{
    uint in  = video->overrideFadeInSpeed()  != Function::defaultSpeed() ? video->overrideFadeInSpeed()  : video->fadeInSpeed();
    uint out = video->overrideFadeOutSpeed() != Function::defaultSpeed() ? video->overrideFadeOutSpeed() : video->fadeOutSpeed();
    if (in == Function::defaultSpeed() || in == Function::infiniteSpeed())
        in = 0;
    if (out == Function::defaultSpeed() || out == Function::infiniteSpeed())
        out = 0;
    fadeIn = int(in);
    fadeOut = int(out);
}

static VideoProvider *s_instance = nullptr;

VideoProvider::VideoProvider(QQuickView *view, Doc *doc, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_doc(doc)
    , m_fullscreenContext(nullptr)
{
    Q_ASSERT(doc != nullptr);

    qmlRegisterUncreatableType<Video>("org.qlcplus.classes", 1, 0, "VideoFunction", "Can't create a Video!");
    qmlRegisterType<VideoFrameProbe>("org.qlcplus.classes", 1, 0, "VideoFrameProbe");

    // registered before the contents are built: their eager sender
    // creation is what the rest of the UI wants to observe
    s_instance = this;

    for (Function *f : m_doc->functionsByType(Function::VideoType))
        slotFunctionAdded(f->id());

    connect(m_doc, SIGNAL(functionAdded(quint32)), this, SLOT(slotFunctionAdded(quint32)));
    connect(m_doc, SIGNAL(functionRemoved(quint32)), this, SLOT(slotFunctionRemoved(quint32)));
}

VideoProvider *VideoProvider::instance()
{
    return s_instance;
}

VideoProvider::~VideoProvider()
{
    if (s_instance == this)
        s_instance = nullptr;

    // Contents first (their Spout players blank the senders they own),
    // then the senders themselves: this is the only place senders are
    // released, i.e. on document clear/close
    for (VideoContent *vc : std::as_const(m_videoMap))
    {
        if (vc)
        {
            vc->destroyContext();
            delete vc;
        }
    }
    m_videoMap.clear();

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    for (auto it = m_spoutSenders.constBegin(); it != m_spoutSenders.constEnd(); ++it)
    {
        if (it.value() != nullptr)
            qDebug().noquote() << "[Spout] releasing sender" << it.key();
        delete it.value();
    }
    m_spoutSenders.clear();
    m_spoutOwners.clear();
#endif
}

void VideoProvider::shutdown()
{
    for (VideoContent *vc : std::as_const(m_videoMap))
    {
        if (vc)
        {
            vc->stopContent();
            vc->destroyContext();
        }
    }
    if (m_fullscreenContext)
    {
        m_fullscreenContext->close();
        m_fullscreenContext->deleteLater();
        m_fullscreenContext = nullptr;
    }
}

QQuickView *VideoProvider::view() const
{
    return m_view;
}

QQuickView *VideoProvider::fullscreenContext() const
{
    return m_fullscreenContext;
}

void VideoProvider::setFullscreenContext(QQuickView *context)
{
    if (context == nullptr && m_fullscreenContext)
        m_fullscreenContext->deleteLater();

    m_fullscreenContext = context;
}

void VideoProvider::slotFunctionAdded(quint32 id)
{
    Function *func = m_doc->function(id);
    if (func == nullptr || func->type() != Function::VideoType)
        return;

    if (m_videoMap.contains(id))
        return;

    Video *video = qobject_cast<Video *>(func);
    m_videoMap[id] = new VideoContent(video, this);

    connect(video, SIGNAL(requestPlayback(QString)), this, SLOT(slotRequestPlayback(QString)));
    connect(video, SIGNAL(requestPause(bool)), this, SLOT(slotRequestPause(bool)));
    connect(video, SIGNAL(requestSeek(qint64)), this, SLOT(slotRequestSeek(qint64)));
    connect(video, SIGNAL(requestStop()), this, SLOT(slotRequestStop()));
}

void VideoProvider::slotFunctionRemoved(quint32 id)
{
    if (m_videoMap.contains(id))
    {
        VideoContent *vc = m_videoMap.take(id);
        delete vc;
    }
}

void VideoProvider::slotRequestPlayback(QString spoutSenderName)
{
    Video *video = qobject_cast<Video *>(sender());
    if (video == nullptr)
        return;

    if (m_videoMap.contains(video->id()))
        m_videoMap[video->id()]->playContent(spoutSenderName);
}

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
SpoutSender *VideoProvider::spoutSender(const QString &name, const QSize &size, const QString &sizedBy)
{
    if (name.isEmpty())
        return nullptr;

    if (m_spoutSenders.contains(name))
        return m_spoutSenders.value(name);

    // a track with a fixed output size wins over whatever clip is asking
    // (both creation paths - eager and first frame - go through here)
    QSize createSize = size;
    QString sizedByNote = sizedBy;
    Track *track = trackForSpoutSender(name);
    if (track != nullptr && track->spoutSize().isEmpty() == false)
    {
        createSize = track->spoutSize();
        sizedByNote = QString("track '%1' fixed size").arg(track->name());
    }

    if (createSize.isEmpty())
        return nullptr;

    SpoutSender *sender = new SpoutSender();
    if (sender->create(name, createSize.width(), createSize.height()) == false)
    {
        qWarning().noquote() << "[Spout] could not create sender" << name
                             << createSize.width() << "x" << createSize.height() << "- Spout output disabled for it";
        delete sender;
        // remember the failure so it isn't retried on every frame
        m_spoutSenders.insert(name, nullptr);
        return nullptr;
    }

    m_spoutSenders.insert(name, sender);
    qDebug().noquote() << "[Spout] created sender" << sender->name()
                       << "at" << createSize.width() << "x" << createSize.height()
                       << "(requested name" << name << ", sized by" << sizedByNote << ") - sent initial transparent frame";
    emit spoutSendersChanged();
    return sender;
}

void VideoProvider::resizeSpoutSender(const QString &name, const QSize &size, const QString &reason)
{
    SpoutSender *sender = m_spoutSenders.value(name, nullptr);
    if (sender == nullptr || size.isEmpty() || sender->size() == size)
        return;

    QSize previous = sender->size();
    sender->resize(size);
    qDebug().noquote() << "[Spout] sender" << name << "resized from" << previous.width() << "x" << previous.height()
                       << "to" << size.width() << "x" << size.height() << "-" << reason;
    emit spoutSendersChanged();
}

Track *VideoProvider::trackForSpoutSender(const QString &name) const
{
    for (Function *f : m_doc->functionsByType(Function::ShowType))
    {
        // functionsByType() already filtered on the type: a static_cast is
        // enough and, unlike qobject_cast, works across the engine DLL
        Show *show = static_cast<Show *>(f);

        for (Track *track : show->tracks())
        {
            if (Video::spoutSenderNameForTrack(track->name()) == name)
                return track;
        }
    }

    return nullptr;
}

void VideoProvider::claimSpoutSender(const QString &name, QObject *owner)
{
    m_spoutOwners.insert(name, owner);
}

bool VideoProvider::ownsSpoutSender(const QString &name, const QObject *owner) const
{
    return m_spoutOwners.value(name, nullptr) == owner;
}

void VideoProvider::releaseSpoutSender(const QString &name, QObject *owner)
{
    if (ownsSpoutSender(name, owner))
        m_spoutOwners.remove(name);
}

bool VideoProvider::isSpoutSenderIdle(const QString &name) const
{
    return m_spoutOwners.value(name, nullptr) == nullptr;
}

QSize VideoProvider::spoutSenderSize(const QString &name) const
{
    SpoutSender *sender = m_spoutSenders.value(name, nullptr);
    return sender == nullptr ? QSize() : sender->size();
}
#endif

QSize VideoProvider::trackSpoutOutputSize(const Track *track) const
{
    if (track == nullptr)
        return QSize();

    if (track->spoutSize().isEmpty() == false)
        return track->spoutSize();

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    return spoutSenderSize(Video::spoutSenderNameForTrack(track->name()));
#else
    return QSize();
#endif
}

void VideoProvider::refreshSpoutSender(quint32 videoId)
{
    VideoContent *vc = m_videoMap.value(videoId, nullptr);
    if (vc != nullptr)
        vc->ensureSpoutSender();
}

void VideoProvider::slotRequestPause(bool enable)
{
    Video *video = qobject_cast<Video *>(sender());
    if (video == nullptr)
        return;

    if (m_videoMap.contains(video->id()))
        m_videoMap[video->id()]->pauseContent(enable);
}

void VideoProvider::slotRequestSeek(qint64 ms)
{
    Video *video = qobject_cast<Video *>(sender());
    if (video == nullptr)
        return;

    if (m_videoMap.contains(video->id()))
        m_videoMap[video->id()]->seekContent(ms);
}

void VideoProvider::slotRequestStop()
{
    Video *video = qobject_cast<Video *>(sender());
    if (video == nullptr)
        return;

    if (m_videoMap.contains(video->id()))
        m_videoMap[video->id()]->stopContent();
}

/*********************************************************************
 * VideoContent class implementation
 *********************************************************************/

VideoContent::VideoContent(Video *video, VideoProvider *parent)
    : QObject(parent)
    , m_provider(parent)
    , m_video(video)
    , m_mediaPlayer(nullptr)
    , m_viewContext(nullptr)
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    , m_spoutPlayer(nullptr)
#endif
{
    Q_ASSERT(video != nullptr);

    slotDetectResolution();

    // All connects to the Video are string-based on purpose: Video lives in
    // the engine DLL and this MinGW build has no dllimport declarations, so
    // a pointer-to-member such as &Video::outputModeChanged taken here is
    // the import thunk, not the address moc registered inside the DLL -
    // the pointer form fails at runtime with "signal not found".
    connect(m_video, SIGNAL(sourceChanged(QString)),
            this, SLOT(slotDetectResolution()));
    connect(m_video, SIGNAL(attributeChanged(int,qreal)),
            this, SLOT(slotAttributeChanged(int,qreal)));

    // eager Spout sender creation (document load, or the editor switching
    // this video to Spout mode / changing the sender size / probing the
    // resolution itself)
    connect(m_video, SIGNAL(outputModeChanged(int)),
            this, SLOT(slotOutputModeChanged(int)));
    connect(m_video, SIGNAL(spoutSizeChanged(QSize)),
            this, SLOT(ensureSpoutSender()));
    connect(m_video, SIGNAL(metaDataChanged(QString,QVariant)),
            this, SLOT(slotVideoMetaDataChanged(QString,QVariant)));
    ensureSpoutSender();
}

VideoContent::~VideoContent()
{
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    if (m_spoutPlayer)
        m_spoutPlayer->stopImmediately();
#endif
}

quint32 VideoContent::id() const
{
    return m_video->id();
}

void VideoContent::destroyContext()
{
    // close() can emit closing() synchronously and invalidate m_viewContext.
    QPointer<QQuickView> context = m_viewContext;
    m_viewContext = nullptr;

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    if (m_spoutPlayer)
        m_spoutPlayer->stopImmediately();
#endif

    if (m_video->fullscreen())
    {
        m_provider->setFullscreenContext(nullptr);
    }
    else if (context)
    {
        context->close();
        if (context)
            context->deleteLater();
    }
}

void VideoContent::playContent(const QString &spoutSenderName)
{
    if (m_video->outputMode() == Video::Spout)
    {
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
        playSpoutContent(spoutSenderName);
        return;
#else
        Q_UNUSED(spoutSenderName)
        qWarning() << "[Spout] Spout output is not available on this platform, playing"
                   << m_video->name() << "in a window instead";
#endif
    }

    QScreen *vScreen = nullptr;

    if (m_video->fullscreen())
        m_viewContext = m_provider->fullscreenContext();

    QList<QScreen *> screens = QGuiApplication::screens();
    if (m_video->screen() < screens.count())
        vScreen = screens.at(m_video->screen());

    if (m_video->isPicture())
    {
        m_geometry.setSize(m_video->resolution());
    }
    else if (!m_video->customGeometry().isNull())
    {
        m_geometry = m_video->customGeometry();
    }

    qDebug() << "Video screen:" << m_video->screen() << ", geometry:" << m_geometry;

    if (m_viewContext == nullptr)
    {
        m_viewContext = new QQuickView(QUrl("qrc:/VideoContext.qml"));
        m_viewContext->rootContext()->setContextProperty("videoContent", this);

        if (m_video->customGeometry().isNull())
        {
            m_viewContext->setGeometry(m_geometry);
            if (vScreen)
                m_viewContext->setPosition(vScreen->geometry().topLeft());
        }
        else
        {
            QPoint topLeft = vScreen ? vScreen->geometry().topLeft() : QPoint(0, 0);
            topLeft.setX(topLeft.x() + m_video->customGeometry().x());
            topLeft.setY(topLeft.y() + m_video->customGeometry().y());
            m_viewContext->setGeometry(m_video->customGeometry());
            m_viewContext->setPosition(topLeft);
        }

        connect(m_viewContext, SIGNAL(closing(QQuickCloseEvent*)), this, SLOT(slotWindowClosing()));
    }
    else
    {
        m_viewContext->rootContext()->setContextProperty("videoContent", this);
    }

    int fadeIn = 0, fadeOut = 0;
    videoFadeTimes(m_video, fadeIn, fadeOut);

    QQuickItem *root = m_viewContext->rootObject();
    if (root == nullptr)
        return;

    if (m_video->isPicture())
    {
        QMetaObject::invokeMethod(root, "addPicture",
                                  Q_ARG(QVariant, QVariant::fromValue(m_video)),
                                  Q_ARG(QVariant, fadeIn),
                                  Q_ARG(QVariant, fadeOut));
    }
    else
    {
        QMetaObject::invokeMethod(root, "addVideo",
                                  Q_ARG(QVariant, QVariant::fromValue(m_video)),
                                  Q_ARG(QVariant, fadeIn),
                                  Q_ARG(QVariant, fadeOut),
                                  Q_ARG(QVariant, (int)m_video->elapsed()));
    }

    m_viewContext->setFlags(m_viewContext->flags() | Qt::WindowStaysOnTopHint);

    if (m_video->fullscreen())
    {
        m_provider->setFullscreenContext(m_viewContext);
        if (vScreen)
            m_viewContext->setScreen(vScreen);
        m_viewContext->showFullScreen();
    }
    else
    {
        m_viewContext->show();
        // Restore focus to the main QLC+ window so that VC interactions
        // (e.g. a slider that started this video) remain active.
        if (m_provider->view() != nullptr)
            m_provider->view()->requestActivate();
    }
}

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
void VideoContent::playSpoutContent(const QString &spoutSenderName)
{
    // A previous run of this same Video still fading out: cut it short,
    // the new run takes the sender over
    if (m_spoutPlayer)
    {
        m_spoutPlayer->stopImmediately();
        m_spoutPlayer = nullptr;
    }

    QString name = spoutSenderName.isEmpty() ? m_video->defaultSpoutSenderName() : spoutSenderName;

    int fadeIn = 0, fadeOut = 0;
    videoFadeTimes(m_video, fadeIn, fadeOut);

    m_spoutPlayer = new SpoutVideoPlayer(m_video, m_provider, name, this);
    connect(m_spoutPlayer, &SpoutVideoPlayer::finished, this, &VideoContent::slotSpoutPlayerFinished);
    m_spoutPlayer->start(fadeIn, fadeOut, m_video->isPicture() ? 0 : qint64(m_video->elapsed()));
}
#endif

void VideoContent::slotSpoutPlayerFinished()
{
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    SpoutVideoPlayer *player = qobject_cast<SpoutVideoPlayer *>(sender());
    if (player == nullptr)
        return;

    if (player == m_spoutPlayer)
        m_spoutPlayer = nullptr;
    player->deleteLater();
#endif
}

void VideoContent::ensureSpoutSender()
{
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    if (m_video->outputMode() != Video::Spout)
        return;

    QSize size = m_video->spoutSize().isEmpty() ? m_video->resolution() : m_video->spoutSize();
    if (size.isEmpty())
    {
        // a track with a fixed output size doesn't need the clip's
        // resolution at all (the pool applies the track size anyway)
        Track *track = m_video->spoutTrack();
        if (track != nullptr && track->spoutSize().isEmpty() == false)
        {
            size = track->spoutSize();
        }
        else
        {
            qDebug().noquote() << "[Spout] sender for" << m_video->name()
                               << "deferred until its resolution is known";
            return;
        }
    }

    // clips on the same Show track share one sender: whichever clip's
    // resolution becomes known first creates it and fixes its size, later
    // ones are aspect-fit into it at playback - never resized (see the
    // sender pool notes in videoprovider.h)
    QString name = m_video->defaultSpoutSenderName();
    SpoutSender *existing = m_provider->spoutSender(name, QSize());
    if (existing != nullptr)
    {
        QSize current = existing->size();
        if (current == size)
            qDebug().noquote() << "[Spout]" << m_video->name() << "shares sender" << name
                               << "at" << current.width() << "x" << current.height();
        else
            qDebug().noquote() << "[Spout]" << m_video->name() << "is" << size.width() << "x" << size.height()
                               << "- sender" << name << "stays at" << current.width() << "x" << current.height()
                               << "(clip is aspect-fit into it, not resized)";
        return;
    }

    m_provider->spoutSender(name, size, m_video->name());
#endif
}

void VideoContent::slotVideoMetaDataChanged(QString key, QVariant data)
{
    Q_UNUSED(data)
    if (key == "Resolution")
        ensureSpoutSender();
}

void VideoContent::slotOutputModeChanged(int mode)
{
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    if (mode == Video::Spout)
    {
        qDebug().noquote() << "[Spout]" << m_video->name() << "switched to Spout output";
        ensureSpoutSender();
        // deferred and nothing probing: (re)probe, the sender follows the
        // resolution via slotVideoMetaDataChanged
        if (m_video->spoutSize().isEmpty() && m_video->resolution().isEmpty()
            && m_mediaPlayer == nullptr && m_video->isPicture() == false)
        {
            slotDetectResolution();
        }
        return;
    }

    // Switched away from Spout: stop a running Spout playback (it blanks
    // the sender as it finishes) or blank the idle sender ourselves. The
    // sender stays registered until document close, receivers such as OBS
    // reset their source when a sender disappears.
    if (m_spoutPlayer)
    {
        m_spoutPlayer->stopImmediately();
        m_spoutPlayer = nullptr;
    }
    QString name = m_video->defaultSpoutSenderName();
    SpoutSender *sender = m_provider->spoutSender(name, QSize());
    if (sender != nullptr && m_provider->isSpoutSenderIdle(name))
    {
        sender->sendTransparent();
        qDebug().noquote() << "[Spout]" << m_video->name() << "left Spout output - blanked sender" << name;
    }
#else
    Q_UNUSED(mode)
#endif
}

void VideoContent::pauseContent(bool enable)
{
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    if (m_spoutPlayer)
    {
        m_spoutPlayer->pause(enable);
        return;
    }
#endif

    if (m_viewContext == nullptr)
        return;

    QQuickItem *root = m_viewContext->rootObject();
    if (root == nullptr)
        return;

    QMetaObject::invokeMethod(root, "pauseContent",
                              Q_ARG(QVariant, m_video->id()),
                              Q_ARG(QVariant, enable));
}

void VideoContent::seekContent(qint64 ms)
{
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    if (m_spoutPlayer)
    {
        m_spoutPlayer->seek(ms);
        return;
    }
#endif

    if (m_viewContext == nullptr)
        return;

    QQuickItem *root = m_viewContext->rootObject();
    if (root == nullptr)
        return;

    QMetaObject::invokeMethod(root, "seekContent",
                              Q_ARG(QVariant, m_video->id()),
                              Q_ARG(QVariant, (int)ms));
}

void VideoContent::stopContent()
{
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    if (m_spoutPlayer)
    {
        m_spoutPlayer->stop();
        return;
    }
#endif

    if (m_viewContext == nullptr)
        return;

    QQuickItem *root = m_viewContext->rootObject();
    if (root == nullptr)
        return;

    QMetaObject::invokeMethod(root, "removeContent",
                              Q_ARG(QVariant, m_video->id()));
}

void VideoContent::slotDetectResolution()
{
    QString sourceURL = m_video->sourceUrl();

    if (m_video->isPicture())
    {
        QString localPath = sourceURL;
        if (sourceURL.contains("://"))
            localPath = QUrl(sourceURL).toLocalFile();

        QPixmap img(localPath);
        if (!img.isNull())
        {
            m_video->setResolution(img.size());
            m_video->setDuration(3000);
            m_video->setTotalDuration(3000);
        }
    }
    else
    {
        // a previous probe still in flight (source changed twice in a
        // row): drop it, its late metaDataChanged must not reach us
        if (m_mediaPlayer != nullptr)
        {
            m_mediaPlayer->disconnect(this);
            m_mediaPlayer->deleteLater();
            m_mediaPlayer = nullptr;
        }

        m_mediaPlayer = new QMediaPlayer();

        // a failed probe (missing file, no multimedia backend) leaves the
        // resolution unknown and a Spout sender deferred forever: say so,
        // and drop the player so a later switch to Spout probes again
        QMediaPlayer *player = m_mediaPlayer;
        connect(player, &QMediaPlayer::errorOccurred, this,
                [this, player](QMediaPlayer::Error, const QString &message)
        {
            qWarning().noquote() << "[Video] resolution probe of" << m_video->name() << "failed:" << message;
            if (m_mediaPlayer == player)
            {
                player->disconnect(this);
                player->deleteLater();
                m_mediaPlayer = nullptr;
            }
        });
        connect(m_mediaPlayer, SIGNAL(durationChanged(qint64)),
                this, SLOT(slotDurationChanged(qint64)));
        connect(m_mediaPlayer, SIGNAL(metaDataChanged()),
                this, SLOT(slotMetaDataChanged()));

        if (sourceURL.contains("://"))
            m_mediaPlayer->setSource(QUrl(sourceURL));
        else
            m_mediaPlayer->setSource(QUrl::fromLocalFile(sourceURL));
    }
}

QVariant VideoContent::getAttribute(quint32 id, const char *propName) const
{
    if (m_viewContext == nullptr)
        return QVariant();

    QQuickItem *item = qobject_cast<QQuickItem*>(m_viewContext->findChild<QQuickItem*>(QString("media-%1").arg(id)));
    if (item)
        return item->property(propName);

    return QVariant();
}

void VideoContent::updateAttribute(quint32 id, const char *propName, QVariant value)
{
    if (m_viewContext == nullptr)
        return;

    QQuickItem *item = qobject_cast<QQuickItem*>(m_viewContext->findChild<QQuickItem*>(QString("media-%1").arg(id)));
    if (item)
        item->setProperty(propName, value);
}

void VideoContent::slotAttributeChanged(int attrIndex, qreal value)
{
    switch (attrIndex)
    {
        case Video::Volume:
        {
            updateAttribute(m_video->id(), "volume", float(value / 100.0));
        }
        break;
        case Video::XRotation:
        {
            QVector3D rot = m_video->rotation();
            rot.setX(float(value));
            updateAttribute(m_video->id(), "rotation", rot);
        }
        break;
        case Video::YRotation:
        {
            QVector3D rot = m_video->rotation();
            rot.setY(float(value));
            updateAttribute(m_video->id(), "rotation", rot);
        }
        break;
        case Video::ZRotation:
        {
            QVector3D rot = m_video->rotation();
            rot.setZ(float(value));
            updateAttribute(m_video->id(), "rotation", rot);
        }
        break;
        case Video::XPosition:
        {
            if (m_viewContext == nullptr)
                break;
            qreal xDelta = qreal(m_viewContext->width()) * (value / 100.0);
            QVariant var = getAttribute(m_video->id(), "geometry");
            QRect currGeom = var.isNull() ? m_geometry : var.toRect();
            QRect geom(m_geometry.x() + int(xDelta), currGeom.y(),
                       currGeom.width(), currGeom.height());
            updateAttribute(m_video->id(), "geometry", geom);
        }
        break;
        case Video::YPosition:
        {
            if (m_viewContext == nullptr)
                break;
            qreal yDelta = qreal(m_viewContext->height()) * (value / 100.0);
            QVariant var = getAttribute(m_video->id(), "geometry");
            QRect currGeom = var.isNull() ? m_geometry : var.toRect();
            QRect geom(currGeom.x(), m_geometry.y() + int(yDelta),
                       currGeom.width(), currGeom.height());
            updateAttribute(m_video->id(), "geometry", geom);
        }
        break;
        case Video::WidthScale:
        {
            QVariant var = getAttribute(m_video->id(), "geometry");
            QRect geom = var.isNull() ? m_geometry : var.toRect();
            qreal newWidth = qreal(m_geometry.width()) * (value / 100.0);
            geom.setWidth(int(newWidth));
            updateAttribute(m_video->id(), "geometry", geom);
        }
        break;
        case Video::HeightScale:
        {
            QVariant var = getAttribute(m_video->id(), "geometry");
            QRect geom = var.isNull() ? m_geometry : var.toRect();
            qreal newHeight = qreal(m_geometry.height()) * (value / 100.0);
            geom.setHeight(int(newHeight));
            updateAttribute(m_video->id(), "geometry", geom);
        }
        break;
        default:
        break;
    }
}

void VideoContent::slotDurationChanged(qint64 duration)
{
    m_video->setTotalDuration(duration);
}

void VideoContent::slotMetaDataChanged()
{
    if (m_mediaPlayer == nullptr)
        return;

    QMediaMetaData md = m_mediaPlayer->metaData();
    foreach (QMediaMetaData::Key k, md.keys())
    {
        if (k == QMediaMetaData::Resolution)
        {
            QSize size = md.value(k).toSize();
            if (m_video->outputMode() == Video::Spout)
                qDebug().noquote() << "[Spout] probed" << m_video->name() << "at" << size.width() << "x" << size.height();

            m_geometry.setSize(size);

            disconnect(m_mediaPlayer, SIGNAL(metaDataChanged()),
                       this, SLOT(slotMetaDataChanged()));
            m_mediaPlayer->deleteLater();
            m_mediaPlayer = nullptr;

            // the editor also does this, but only once the Video is edited:
            // the Spout sender size ("native" default) needs it at load.
            // Emits Video::metaDataChanged -> slotVideoMetaDataChanged ->
            // ensureSpoutSender()
            m_video->setResolution(size);
            break;
        }
    }
}

void VideoContent::slotWindowClosing()
{
    // The window is being closed manually (e.g. window manager close button).
    // The QQuickView only hides on close(), it does not tear down the QML
    // scene graph, so the MediaPlayer would keep playing in the background.
    // Stop the QML content explicitly while the root object is still valid.
    if (m_viewContext)
    {
        QQuickItem *root = m_viewContext->rootObject();
        if (root)
            QMetaObject::invokeMethod(root, "stopAllPlayback");
    }

    m_viewContext = nullptr;

    // Window teardown can race with scene graph/root object destruction.
    // Request function stop through the engine path instead of touching QML.
    if (m_video && m_video->isRunning())
        m_video->stopFromUI();
}
