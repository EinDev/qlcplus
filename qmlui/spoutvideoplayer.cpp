/*
  Q Light Controller Plus
  spoutvideoplayer.cpp

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

#include "spoutvideoplayer.h"

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)

#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>
#include <QPainter>
#include <QDebug>
#include <QUrl>

#include "videoprovider.h"
#include "spoutsender.h"
#include "video.h"

SpoutVideoPlayer::SpoutVideoPlayer(Video *video, VideoProvider *provider, const QString &senderName,
                                   QObject *parent)
    : QObject(parent)
    , m_video(video)
    , m_provider(provider)
    , m_senderName(senderName)
    , m_sender(nullptr)
    , m_player(new QMediaPlayer(this))
    , m_audioOutput(new QAudioOutput(this))
    , m_videoSink(new QVideoSink(this))
    , m_intensity(1.0)
    , m_volume(1.0)
    , m_fadeMultiplier(1.0)
    , m_fadeState(0)
    , m_frozenIntensity(-1.0)
    , m_fadeOutMs(0)
    , m_startPosition(0)
    , m_framesSent(0)
    , m_active(false)
    , m_stopRequested(false)
{
    Q_ASSERT(video != nullptr);
    Q_ASSERT(provider != nullptr);

    m_player->setAudioOutput(m_audioOutput);
    m_player->setVideoSink(m_videoSink);

    connect(m_videoSink, &QVideoSink::videoFrameChanged,
            this, &SpoutVideoPlayer::slotFrameChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, &SpoutVideoPlayer::slotMediaStatusChanged);
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, &SpoutVideoPlayer::slotPlayerError);

    // Intensity (incl. Show track overrides) and Volume, as combined by the
    // engine. Emitted from the MasterTimer thread, queued to us.
    connect(m_video, SIGNAL(attributeChanged(int,qreal)),
            this, SLOT(slotAttributeChanged(int,qreal)));

    m_fadeAnim.setStartValue(0.0);
    m_fadeAnim.setEndValue(1.0);
    connect(&m_fadeAnim, &QVariantAnimation::valueChanged,
            this, &SpoutVideoPlayer::slotFadeValueChanged);
    connect(&m_fadeAnim, &QVariantAnimation::finished,
            this, &SpoutVideoPlayer::slotFadeFinished);
}

SpoutVideoPlayer::~SpoutVideoPlayer()
{
    if (m_active)
        finish(false);
    else
        m_provider->releaseSpoutSender(m_senderName, this);
}

QString SpoutVideoPlayer::senderName() const
{
    return m_senderName;
}

bool SpoutVideoPlayer::isActive() const
{
    return m_active;
}

void SpoutVideoPlayer::start(int fadeInMs, int fadeOutMs, qint64 startPositionMs)
{
    if (m_active)
        return;

    m_active = true;
    m_stopRequested = false;
    m_fadeOutMs = fadeOutMs;
    m_startPosition = startPositionMs;
    m_fadeState = 0;
    m_fadeMultiplier = 1.0;
    m_frozenIntensity = -1.0;
    m_framesSent = 0;
    // Show tracks apply their intensity override before start(), so read
    // the combined value now instead of assuming 1.0
    m_intensity = m_video->intensity();
    m_volume = m_video->getAttributeValue(Video::Volume) / 100.0;
    m_lastFrame = QImage();

    // Latest claimant wins: an earlier player still fading out on this
    // sender goes silent from now on
    m_provider->claimSpoutSender(m_senderName, this);

    if (fadeInMs > 0)
    {
        m_fadeState = 1;
        m_fadeMultiplier = 0.0;
        // the setters recompute and emit valueChanged() on their own, only
        // start() should push a value
        m_fadeAnim.blockSignals(true);
        m_fadeAnim.setStartValue(0.0);
        m_fadeAnim.setEndValue(1.0);
        m_fadeAnim.setDuration(fadeInMs);
        m_fadeAnim.blockSignals(false);
        m_fadeAnim.start();
    }

    applyVolume();

    QString sourceUrl = m_video->sourceUrl();
    QUrl url = sourceUrl.contains("://") ? QUrl(sourceUrl) : QUrl::fromLocalFile(sourceUrl);

    qDebug().noquote() << "[Spout] playing" << m_video->name() << "into sender" << m_senderName
                       << "fadeIn" << fadeInMs << "fadeOut" << fadeOutMs << "from" << startPositionMs;

    if (m_video->isPicture())
    {
        QString localPath = url.isLocalFile() ? url.toLocalFile() : sourceUrl;
        m_lastFrame = QImage(localPath);
        if (m_lastFrame.isNull())
            qWarning() << "[Spout] cannot load picture" << localPath << "for" << m_video->name();
        // one frame now, then only on fade/intensity changes
        render();
    }
    else
    {
        m_player->setSource(url);
        m_player->play();
    }
}

void SpoutVideoPlayer::pause(bool enable)
{
    if (m_active == false || m_video->isPicture())
        return;

    if (enable)
        m_player->pause();
    else
        m_player->play();
}

void SpoutVideoPlayer::stop()
{
    if (m_active == false || m_stopRequested)
        return;

    m_stopRequested = true;

    if (m_fadeOutMs > 0)
    {
        // freeze the intensity at its current value for the whole fade,
        // like VideoContext.qml does
        m_frozenIntensity = effectiveIntensity();
        m_fadeState = 2;
        // fade out from wherever the fade-in got to; block the setters'
        // own valueChanged() emissions (stale intermediate values)
        qreal from = m_fadeMultiplier;
        m_fadeAnim.blockSignals(true);
        m_fadeAnim.stop();
        m_fadeAnim.setStartValue(from);
        m_fadeAnim.setEndValue(0.0);
        m_fadeAnim.setDuration(m_fadeOutMs);
        m_fadeAnim.blockSignals(false);
        m_fadeAnim.start();
    }
    else
    {
        finish(true);
    }
}

void SpoutVideoPlayer::stopImmediately()
{
    if (m_active == false)
        return;

    finish(true);
}

void SpoutVideoPlayer::slotFrameChanged(const QVideoFrame &frame)
{
    if (m_active == false || frame.isValid() == false)
        return;

    m_lastFrame = frame.toImage();
    render();
}

void SpoutVideoPlayer::slotMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (m_active == false)
        return;

    // Seek to the resume position once the media is loaded (e.g. the Show
    // Manager resuming a clip at a non-zero position)
    if (m_startPosition > 0 &&
        (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia))
    {
        m_player->setPosition(m_startPosition);
        m_startPosition = 0;
    }

    if (status == QMediaPlayer::EndOfMedia)
    {
        if (m_video->runOrder() == Function::Loop)
            m_player->play();
        else
            m_video->stopFromUI();
    }
}

void SpoutVideoPlayer::slotPlayerError(QMediaPlayer::Error error, const QString &errorString)
{
    qWarning() << "[Spout] media error" << error << errorString << "playing" << m_video->name();
}

void SpoutVideoPlayer::slotAttributeChanged(int attrIndex, qreal value)
{
    switch (attrIndex)
    {
        case Function::Intensity:
            m_intensity = value;
        break;
        case Video::Volume:
            m_volume = value / 100.0;
        break;
        default:
            return;
    }

    if (m_active == false)
        return;

    applyVolume();
    // videos would pick it up on their next frame anyway, but pictures and
    // paused videos only change if we re-send now
    render();
}

void SpoutVideoPlayer::slotFadeValueChanged(const QVariant &value)
{
    m_fadeMultiplier = value.toReal();
    applyVolume();
    render();
}

void SpoutVideoPlayer::slotFadeFinished()
{
    // QVariantAnimation::stop() midway (a fade-out cutting a fade-in short)
    // is not the end of a fade
    if (m_fadeAnim.currentTime() < m_fadeAnim.duration())
        return;

    int state = m_fadeState;
    m_fadeState = 0;
    m_frozenIntensity = -1.0;

    if (state == 2)
        finish(true);
}

void SpoutVideoPlayer::render()
{
    if (m_active == false)
        return;

    // an earlier run still fading out must not paint over the new one
    if (m_provider->ownsSpoutSender(m_senderName, this) == false)
        return;

    // nothing decoded yet (fade ticks before the first frame): the sender
    // is already transparent from its creation or the previous finish()
    if (m_lastFrame.isNull())
        return;

    if (m_sender == nullptr)
    {
        QSize preferred = m_video->spoutSize().isEmpty() ? m_lastFrame.size() : m_video->spoutSize();
        m_sender = m_provider->spoutSender(m_senderName, preferred);
        if (m_sender == nullptr)
            return;
    }

    QSize size = canvasSize(m_lastFrame.size());
    if (size.isEmpty())
        return;

    if (m_canvas.size() != size || m_canvas.format() != QImage::Format_ARGB32_Premultiplied)
        m_canvas = QImage(size, QImage::Format_ARGB32_Premultiplied);

    // transparent letterbox, premultiplied: the receiver has to composite
    // this as premultiplied alpha (see spoutsender.h)
    m_canvas.fill(Qt::transparent);

    QSize fit = m_lastFrame.size().scaled(size, Qt::KeepAspectRatio);
    QRect target(QPoint((size.width() - fit.width()) / 2, (size.height() - fit.height()) / 2), fit);

    QPainter painter(&m_canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, fit != m_lastFrame.size());
    painter.setOpacity(qBound(0.0, effectiveIntensity() * m_fadeMultiplier, 1.0));
    painter.drawImage(target, m_lastFrame);
    painter.end();

    m_sender->sendImage(m_canvas);

    if (++m_framesSent == 1)
        qDebug().noquote() << "[Spout] first frame of" << m_video->name() << "sent on" << m_senderName
                           << "canvas" << size.width() << "x" << size.height()
                           << "source" << m_lastFrame.width() << "x" << m_lastFrame.height();
}

QSize SpoutVideoPlayer::canvasSize(const QSize &frameSize) const
{
    // 1. the size configured on the Video function
    if (m_video->spoutSize().isEmpty() == false)
        return m_video->spoutSize();

    // 2. the size the (possibly shared, eagerly created) sender already
    //    has: resizing it would make every receiver re-initialize
    if (m_sender != nullptr && m_sender->size().isValid() && m_sender->size().isEmpty() == false)
        return m_sender->size();

    // 3. native
    return frameSize;
}

qreal SpoutVideoPlayer::effectiveIntensity() const
{
    if (m_fadeState == 2 && m_frozenIntensity >= 0.0)
        return m_frozenIntensity;

    return m_intensity;
}

void SpoutVideoPlayer::applyVolume()
{
    m_audioOutput->setVolume(float(qBound(0.0, m_volume * effectiveIntensity() * m_fadeMultiplier, 1.0)));
}

void SpoutVideoPlayer::finish(bool emitFinished)
{
    if (m_active == false)
        return;

    m_active = false;
    m_stopRequested = true;
    m_fadeState = 0;
    m_frozenIntensity = -1.0;
    m_fadeAnim.stop();
    m_player->stop();
    m_lastFrame = QImage();

    // Leave the sender registered (receivers keep their source), just blank
    // it - unless a newer run already took it over
    if (m_provider->ownsSpoutSender(m_senderName, this))
    {
        if (m_sender != nullptr)
        {
            m_sender->sendTransparent();
            qDebug().noquote() << "[Spout] stopped" << m_video->name() << "after" << m_framesSent
                               << "frames - sent transparent frame on" << m_senderName;
        }
        m_provider->releaseSpoutSender(m_senderName, this);
    }
    else
    {
        qDebug().noquote() << "[Spout] stopped" << m_video->name() << "- sender" << m_senderName << "already taken over";
    }

    if (emitFinished)
        emit finished();
}

#endif // Q_OS_WIN && QLC_SPOUT
