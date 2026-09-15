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

#include <exception>

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
    , m_muted(false)
    , m_fadeMultiplier(1.0)
    , m_fadeState(0)
    , m_frozenIntensity(-1.0)
    , m_fadeOutMs(0)
    , m_startPosition(0)
    , m_framesSent(0)
    , m_conversionFailures(0)
    , m_holdRequested(false)
    , m_holdArmed(false)
    , m_holdTarget(-1)
    , m_holdFrames(0)
    , m_holdPending(false)
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

    m_holdSettle.setSingleShot(true);
    m_holdSettle.setInterval(250);
    connect(&m_holdSettle, &QTimer::timeout, this, &SpoutVideoPlayer::holdNow);

    // Intensity (incl. Show track overrides) and Volume, as combined by the
    // engine. Emitted from the MasterTimer thread, queued to us.
    connect(m_video, SIGNAL(attributeChanged(int,qreal)),
            this, SLOT(slotAttributeChanged(int,qreal)));
    connect(m_video, SIGNAL(mutedChanged(bool)),
            this, SLOT(slotMutedChanged(bool)));

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
    m_frameGate.reset();
    m_conversionFailures = 0;
    // Show tracks apply their intensity override before start(), so read
    // the combined value now instead of assuming 1.0
    m_intensity = m_video->intensity();
    m_volume = m_video->getAttributeValue(Video::Volume) / 100.0;
    m_muted = m_video->muted();
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

// A QMediaPlayer paused before it has delivered a frame delivers none,
// and a seek while paused delivers none either (verified with the Show
// Manager's scrub preview, which pauses a clip ~40ms after starting it).
// So a pause is deferred until a frame at the wanted position has been
// delivered: the player keeps playing for a frame or two and is paused
// from slotFrameChanged() on the first one after the seek/start.
void SpoutVideoPlayer::armHold(qint64 target)
{
    m_holdTarget = target;
    m_holdArmed = true;
    m_holdPending = false;
    m_holdFrames = 0;
    m_holdSettle.stop();
    if (m_player->playbackState() != QMediaPlayer::PlayingState)
        m_player->play();
}

void SpoutVideoPlayer::holdNow()
{
    m_holdArmed = false;
    m_holdPending = false;
    m_holdSettle.stop();
    if (m_holdRequested)
        m_player->pause();
}

void SpoutVideoPlayer::pause(bool enable)
{
    if (m_active == false || m_video->isPicture())
        return;

    m_holdRequested = enable;
    if (enable)
    {
        armHold(-1);
    }
    else
    {
        m_holdArmed = false;
        m_holdPending = false;
        m_holdSettle.stop();
        m_player->play();
    }
}

void SpoutVideoPlayer::seek(qint64 positionMs)
{
    if (m_active == false || m_video->isPicture())
        return;

    QMediaPlayer::MediaStatus status = m_player->mediaStatus();
    if (status == QMediaPlayer::NoMedia || status == QMediaPlayer::LoadingMedia)
    {
        m_startPosition = positionMs;   // slotMediaStatusChanged applies it
        return;
    }

    m_player->setPosition(positionMs);
    if (m_holdRequested)
        armHold(positionMs);
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

    // The sink emits from the decoder's renderer thread, so every frame
    // reaches this GUI-thread slot as its own queued event, each pinning a
    // decoded QVideoFrame. Once conversion + painting + sending takes longer
    // than the clip's frame interval (a 1080p60 clip does) that queue grows
    // without bound - measured 7 GB within 40 s - until QVideoFrame::toImage()
    // throws std::bad_alloc. Only the frame the sink currently holds is
    // worth converting; older deliveries are dropped unread, which keeps
    // the queue drained at the delivery rate.
    if (m_frameGate.accept(frame.startTime(), m_videoSink->videoFrame().startTime()) == false)
        return;

    // Qt's converter throws std::bad_alloc (QByteArray::resize in the RHI
    // readback) when memory is exhausted; a lost frame is acceptable, an
    // uncaught exception in an event handler is not - it aborts the app
    try
    {
        m_lastFrame = frame.toImage();
        render();
    }
    catch (const std::exception &e)
    {
        m_lastFrame = QImage();
        if (m_conversionFailures++ == 0)
            qWarning().noquote() << "[Spout] frame conversion failed:" << e.what()
                                 << "- dropping the frame, playback of" << m_video->name() << "continues";
        return;
    }
    catch (...)
    {
        m_lastFrame = QImage();
        if (m_conversionFailures++ == 0)
            qWarning().noquote() << "[Spout] frame conversion failed: unknown exception"
                                 << "- dropping the frame, playback of" << m_video->name() << "continues";
        return;
    }

    if (m_holdRequested == false)
        return;

    if (m_holdPending)
    {
        holdNow();
        return;
    }

    if (m_holdArmed)
    {
        // Judge the frame by its own timestamp: the player reports the seek
        // target as its position at once, while the frames delivered can
        // still be the ones decoded before the seek for a while (backward
        // seeks especially). Without timestamps the position has to do.
        m_holdFrames++;
        qint64 frameMs = frame.startTime() < 0 ? -1 : frame.startTime() / 1000;
        bool atTarget = true;
        if (m_holdTarget >= 0)
        {
            if (frameMs >= 0)
                atTarget = frameMs >= m_holdTarget - 100 && frameMs <= m_holdTarget + 1000;
            else
                atTarget = m_holdFrames >= 2 && qAbs(m_player->position() - m_holdTarget) <= 500;
        }

        // Pausing inside this frame's own delivery makes the backend
        // re-present the frame before it (the pre-seek one after a backward
        // seek): pause on the next delivery, or after a moment if none comes.
        if (atTarget)
        {
            m_holdArmed = false;
            m_holdPending = true;
            m_holdSettle.start();
        }
    }
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
        if (m_holdRequested)
            armHold(m_startPosition);
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

void SpoutVideoPlayer::slotMutedChanged(bool muted)
{
    m_muted = muted;

    if (m_active)
        applyVolume();
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
        m_sender = m_provider->spoutSender(m_senderName, preferred, m_video->name());
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
    // 1. the size the (possibly shared, eagerly created) sender already
    //    has. SpoutSender::sendImage() resizes the shared texture when a
    //    frame of another size is sent and every receiver re-initializes,
    //    so an existing sender's size always wins - even over this Video's
    //    own SpoutSize, which only sizes a sender it creates itself
    if (m_sender != nullptr && m_sender->size().isValid() && m_sender->size().isEmpty() == false)
        return m_sender->size();

    // 2. the size configured on the Video function
    if (m_video->spoutSize().isEmpty() == false)
        return m_video->spoutSize();

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
    if (m_muted)
        m_audioOutput->setVolume(0.0f);
    else
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
                               << "frames - sent transparent frame on" << m_senderName
                               << (m_frameGate.droppedCount() > 0
                                       ? QString("(%1 queued frames dropped)").arg(m_frameGate.droppedCount())
                                       : QString())
                               << (m_conversionFailures > 0
                                       ? QString("(%1 frames failed to convert)").arg(m_conversionFailures)
                                       : QString());
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
