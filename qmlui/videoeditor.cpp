/*
  Q Light Controller Plus
  videoeditor.cpp

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
#include <QScreen>

#include <QFileInfo>

#include "videoprovider.h"
#include "videoeditor.h"
#include "tardis.h"
#include "video.h"
#include "track.h"
#include "mediaassets.h"
#include "doc.h"

VideoEditor::VideoEditor(QQuickView *view, Doc *doc, QObject *parent)
    : FunctionEditor(view, doc, parent)
    , m_video(nullptr)
    , m_mediaPlayer(nullptr)
{
    m_view->rootContext()->setContextProperty("videoEditor", this);

    // the default sender name follows the function name
    connect(this, &FunctionEditor::functionNameChanged, this, &VideoEditor::spoutSenderNameChanged);

    // ... and its "@ WxH" suffix follows the track sender (created after
    // a probe, or resized by the user in the Show Manager). The provider
    // outlives every editor of its document, and a new document replaces
    // the editors too, so a plain connection is safe here.
    if (VideoProvider::instance() != nullptr)
        connect(VideoProvider::instance(), &VideoProvider::spoutSendersChanged,
                this, &VideoEditor::spoutSenderNameChanged);
}

VideoEditor::~VideoEditor()
{
    if (m_mediaPlayer)
        delete m_mediaPlayer;
}

void VideoEditor::detectMedia()
{
    if (m_video == nullptr)
        return;

    infoMap.clear();

    if (m_video->isPicture())
    {
        infoMap.insert("Resolution", m_video->resolution());
        infoMap.insert("Duration", Function::speedToString(m_video->duration()));
    }
    else
    {
        QString sourceURL = m_video->sourceUrl();
        m_mediaPlayer = new QMediaPlayer(this);

        connect(m_mediaPlayer, SIGNAL(metaDataChanged()),
                this, SLOT(slotMetaDataChanged()));
        connect(m_mediaPlayer, SIGNAL(durationChanged(qint64)),
                this, SLOT(slotDurationChanged(qint64)));

        if (sourceURL.contains("://"))
            m_mediaPlayer->setSource(QUrl(sourceURL));
        else
            m_mediaPlayer->setSource(QUrl::fromLocalFile(sourceURL));
    }
}

void VideoEditor::setFunctionID(quint32 ID)
{
    m_video = qobject_cast<Video *>(m_doc->function(ID));
    FunctionEditor::setFunctionID(ID);

    if (m_video != nullptr)
        connect(m_video, SIGNAL(sourceChanged(QString)), this, SLOT(slotSourceRelinked(QString)));

    detectMedia();

    emit outputModeChanged(outputMode());
    emit spoutSizeChanged(spoutSize());
    emit spoutSenderNameChanged();
}

QString VideoEditor::sourceFileName() const
{
    if (m_video == nullptr)
        return "";

    return m_video->sourceUrl();
}

void VideoEditor::setSourceFileName(QString sourceFileName)
{
    if (sourceFileName.startsWith("file:"))
        sourceFileName = QUrl(sourceFileName).toLocalFile();

    if (m_video == nullptr)
        return;

    // Local files are copied into the project's media store (function and
    // undo history both point at the managed copy); a URL typed into the
    // text box is a stream and bypasses the store
    if (sourceFileName.contains("://") == false)
        sourceFileName = m_doc->assets()->importOrKeep(sourceFileName);

    if (m_video->sourceUrl() == sourceFileName)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetSource, m_video->id(), m_video->sourceUrl(), sourceFileName);
    m_video->setSourceUrl(sourceFileName);

    detectMedia();

    emit sourceFileNameChanged(sourceFileName);
    emit functionNameChanged(m_video->name());
    emit loopedChanged();
}

bool VideoEditor::sourceManaged() const
{
    if (m_video == nullptr)
        return false;

    return m_doc->assets()->isManaged(m_video->sourceUrl());
}

QString VideoEditor::sourceDisplayName() const
{
    if (m_video == nullptr)
        return QString();

    QString source = m_video->sourceUrl();
    if (m_doc->assets()->isManaged(source))
        return QFileInfo(source).fileName();

    return source;
}

void VideoEditor::slotSourceRelinked(QString source)
{
    emit sourceFileNameChanged(source);
}

QStringList VideoEditor::videoExtensions() const
{
    return Video::getVideoCapabilities();
}

QStringList VideoEditor::pictureExtensions() const
{
    return Video::getPictureCapabilities();
}

QVariant VideoEditor::mediaInfo() const
{
    return QVariant::fromValue(infoMap);
}

void VideoEditor::slotDurationChanged(qint64 duration)
{
    infoMap.insert("Duration", Function::speedToString(duration));
    m_video->setTotalDuration(duration);
    emit mediaInfoChanged();
}

void VideoEditor::slotMetaDataChanged()
{
    if (m_video == NULL)
        return;

    QMediaMetaData md = m_mediaPlayer->metaData();
    foreach (QMediaMetaData::Key k, md.keys())
    {
        QString mdKeyName = md.metaDataKeyToString(k);
        QVariant mdValue = md.stringValue(k);
        qDebug() << "[Metadata]" << mdKeyName << ":" << mdValue;

        switch (k)
        {
            case QMediaMetaData::Resolution:
                m_video->setResolution(md.value(k).toSize());
                mdValue = md.value(k).toSize();
            break;
            case QMediaMetaData::VideoCodec:
                m_video->setVideoCodec(md.stringValue(k));
                mdKeyName = "VideoCodec";
            break;
            case QMediaMetaData::AudioCodec:
                m_video->setAudioCodec(md.stringValue(k));
                mdKeyName = "AudioCodec";
            break;
            case QMediaMetaData::Duration:
                continue;
            break;
            default:
            break;
        }
        infoMap.insert(mdKeyName, mdValue);
    }
    emit mediaInfoChanged();
}

QStringList VideoEditor::screenList() const
{
    QStringList list;
    int i = 1;

    for (QScreen *screen : QGuiApplication::screens())
        list.append(QString(QString("Screen %1 - (%2)").arg(i++).arg(screen->name())));

    return list;
}

int VideoEditor::screenIndex() const
{
    if (m_video != nullptr)
        return m_video->screen();

    return 0;
}

void VideoEditor::setScreenIndex(int screenIndex)
{
    if (m_video == nullptr || m_video->screen() == screenIndex)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetScreenIndex, m_video->id(), m_video->screen(), screenIndex);
    m_video->setScreen(screenIndex);
    emit screenIndexChanged(screenIndex);
}

bool VideoEditor::isFullscreen() const
{
    if (m_video != nullptr)
        return m_video->fullscreen();

    return false;
}

void VideoEditor::setFullscreen(bool fullscreen)
{
    if (m_video == nullptr || m_video->fullscreen() == fullscreen)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetFullscreen, m_video->id(), m_video->fullscreen(), fullscreen);
    m_video->setFullscreen(fullscreen);
    emit fullscreenChanged(fullscreen);
    emit outputModeChanged(outputMode());
}

int VideoEditor::outputMode() const
{
    if (m_video != nullptr)
        return int(m_video->outputMode());

    return int(Video::Windowed);
}

void VideoEditor::setOutputMode(int mode)
{
    if (m_video == nullptr || int(m_video->outputMode()) == mode)
        return;

    if (mode == Video::Spout && spoutAvailable() == false)
    {
        qWarning() << "Spout output is not available in this build";
        return;
    }

    bool wasFullscreen = m_video->fullscreen();
    Tardis::instance()->enqueueAction(Tardis::VideoSetOutputMode, m_video->id(), int(m_video->outputMode()), mode);
    m_video->setOutputMode(mode);
    emit outputModeChanged(outputMode());
    if (wasFullscreen != m_video->fullscreen())
        emit fullscreenChanged(m_video->fullscreen());
}

bool VideoEditor::spoutAvailable() const
{
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    return true;
#else
    return false;
#endif
}

QSize VideoEditor::spoutSize() const
{
    if (m_video != nullptr)
        return m_video->spoutSize();

    return QSize(0, 0);
}

void VideoEditor::setSpoutSize(QSize size)
{
    if (m_video == nullptr)
        return;

    if (size.width() <= 0 || size.height() <= 0)
        size = QSize(0, 0);

    if (m_video->spoutSize() == size)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetSpoutSize, m_video->id(), m_video->spoutSize(), size);
    m_video->setSpoutSize(size);
    emit spoutSizeChanged(m_video->spoutSize());
}

QString VideoEditor::spoutSenderName() const
{
    if (m_video == nullptr)
        return QString();

    QString name = m_video->defaultSpoutSenderName();

    // on a Show track the sender's size is the track's fixed output size,
    // not this Video's own: say so next to the name
    Track *track = m_video->spoutTrack();
    if (track != nullptr)
    {
        VideoProvider *provider = VideoProvider::instance();
        QSize size = provider != nullptr ? provider->trackSpoutOutputSize(track) : track->spoutSize();
        if (size.isEmpty() == false)
            name += QString(" @ %1x%2").arg(size.width()).arg(size.height());
    }

    return name;
}

bool VideoEditor::isLooped() const
{
    if (m_video != nullptr)
        return m_video->runOrder() == Video::Loop;

    return false;
}

void VideoEditor::setLooped(bool looped)
{
    if (m_video != nullptr)
    {
        Tardis::instance()->enqueueAction(Tardis::FunctionSetRunOrder, m_video->id(),
                                          m_video->runOrder(), looped ? Video::Loop : Video::SingleShot);
        if (looped)
            m_video->setRunOrder(Video::Loop);
        else
            m_video->setRunOrder(Video::SingleShot);
    }
}

bool VideoEditor::hasCustomGeometry() const
{
    if (m_video != nullptr && m_video->customGeometry().isNull() == false)
        return true;

    return false;
}

QRect VideoEditor::customGeometry() const
{
    if (m_video != nullptr)
        return m_video->customGeometry();

    return QRect();
}

void VideoEditor::setCustomGeometry(QRect customGeometry)
{
    if (m_video == nullptr || m_video->customGeometry() == customGeometry)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetGeometry, m_video->id(), m_video->customGeometry(), customGeometry);
    m_video->setCustomGeometry(customGeometry);
    emit customGeometryChanged(customGeometry);
}

QVector3D VideoEditor::rotation() const
{
    if (m_video != nullptr)
        return m_video->rotation();

    return QVector3D();
}

void VideoEditor::setRotation(QVector3D rotation)
{
    if (m_video == nullptr || m_video->rotation() == rotation)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetRotation, m_video->id(), m_video->rotation(), rotation);
    m_video->setRotation(rotation);
    emit rotationChanged(rotation);
}

int VideoEditor::layer() const
{
    if (m_video != nullptr)
        return m_video->zIndex();

    return 1;
}

void VideoEditor::setLayer(int index)
{
    if (m_video == nullptr || m_video->zIndex() == index)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetLayer, m_video->id(), m_video->zIndex(), index);
    m_video->setZIndex(index);
    emit layerChanged(index);
}

qreal VideoEditor::volume() const
{
    if (m_video != nullptr)
        return m_video->volume();

    return 100;
}

void VideoEditor::setVolume(qreal volume)
{
    if (m_video == nullptr || m_video->volume() == volume)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetVolume, m_video->id(), m_video->volume(), volume);
    m_video->setVolume(volume);
    emit volumeChanged();
}

bool VideoEditor::muted() const
{
    if (m_video != nullptr)
        return m_video->muted();

    return false;
}

void VideoEditor::setMuted(bool muted)
{
    if (m_video == nullptr || m_video->muted() == muted)
        return;

    Tardis::instance()->enqueueAction(Tardis::VideoSetMuted, m_video->id(), m_video->muted(), muted);
    m_video->setMuted(muted);
    emit mutedChanged();
}
