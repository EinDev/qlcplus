/*
  Q Light Controller Plus
  spoutvideoplayer.h

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

#ifndef SPOUTVIDEOPLAYER_H
#define SPOUTVIDEOPLAYER_H

#include <QtGlobal>

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QVariantAnimation>
#include <QMediaPlayer>

class QAudioOutput;
class QVideoSink;
class QVideoFrame;
class Video;
class VideoProvider;
class SpoutSender;

/**
 * Plays one Video function into a Spout sender instead of a window.
 *
 * This is the C++ counterpart of VideoContext.qml's video/picture items
 * for Video::Spout output mode: a QMediaPlayer decodes the source, its
 * QVideoSink hands every frame over as a QVideoFrame, the frame is painted
 * aspect-fit (transparent letterbox) into an ARGB32_Premultiplied canvas of
 * the sender size with QPainter::setOpacity(intensity * fade), and the
 * canvas is pushed through SpoutSender::sendImage. Audio still plays
 * locally through a QAudioOutput. Pictures are painted once from a QImage
 * and re-sent on every fade/intensity change.
 *
 * Fade in/out mirror VideoContext.qml: the multiplier ramps from 0 to 1
 * over the fade-in time, and from the current value to 0 over the fade-out
 * time with the intensity frozen at the value it had when the fade-out
 * began. End of media loops (Function::Loop) or stops the function via
 * Video::stopFromUI(), exactly like the QML path.
 *
 * The sender itself is owned and pooled by VideoProvider (keyed by name);
 * the player only claims it. Two players on the same sender (a fade-out
 * tail overlapping the next clip on the same track) are resolved by the
 * provider's claim: the latest claimant renders, earlier ones go silent
 * and do not send their final transparent frame either. When the player
 * finishes (after the fade-out) it sends one transparent frame and leaves
 * the sender registered, so receivers keep their source.
 *
 * Everything here runs on the GUI thread (the Video's request signals are
 * queued from the MasterTimer thread), which is also what SpoutSender
 * requires.
 */
class SpoutVideoPlayer final : public QObject
{
    Q_OBJECT

public:
    SpoutVideoPlayer(Video *video, VideoProvider *provider, const QString &senderName,
                     QObject *parent = nullptr);
    ~SpoutVideoPlayer();

    /** The sender name this player publishes under */
    QString senderName() const;

    /** true from start() until the last (transparent) frame was sent */
    bool isActive() const;

    /**
     * Start playing (or showing) the Video's source.
     * @param fadeInMs fade-in time, 0 = none
     * @param fadeOutMs fade-out time used by stop(), 0 = none
     * @param startPositionMs resume position for videos, 0 = from the start
     */
    void start(int fadeInMs, int fadeOutMs, qint64 startPositionMs);

    /** Pause/resume decoding (no-op for pictures) */
    void pause(bool enable);

    /** Jump to $positionMs into the media (no-op for pictures). Before the
     *  media has loaded the position is applied once it has, like the
     *  start position. */
    void seek(qint64 positionMs);

    /** Request a stop: fades out if a fade-out time was given, then sends a
     *  transparent frame and emits finished(). Idempotent. */
    void stop();

    /** Stop right now, no fade: sends a transparent frame if this player
     *  still holds the sender claim. Used on shutdown and when the same
     *  Video is restarted while its previous run is still fading out. */
    void stopImmediately();

signals:
    /** Emitted once the player is done and its last frame has been sent */
    void finished();

private slots:
    void slotFrameChanged(const QVideoFrame &frame);
    void slotMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void slotPlayerError(QMediaPlayer::Error error, const QString &errorString);
    void slotAttributeChanged(int attrIndex, qreal value);
    void slotFadeValueChanged(const QVariant &value);
    void slotFadeFinished();

private:
    /** Paint the last frame with the current opacity and send it */
    void render();
    /** Canvas (= sender) size for a frame of the given native size */
    QSize canvasSize(const QSize &frameSize) const;
    qreal effectiveIntensity() const;
    void applyVolume();
    void finish(bool emitFinished);

    /** The Video function being played (lives on the GUI thread) */
    Video *m_video;
    /** Owner of the sender pool */
    VideoProvider *m_provider;
    QString m_senderName;
    /** Claimed sender, null until the first frame is rendered */
    SpoutSender *m_sender;

    QMediaPlayer *m_player;
    QAudioOutput *m_audioOutput;
    QVideoSink *m_videoSink;

    /** Last decoded frame (or the picture), painted on every render() */
    QImage m_lastFrame;
    /** Reused canvas of the sender size */
    QImage m_canvas;

    /** Function intensity (0..1) as combined by the engine */
    qreal m_intensity;
    /** Volume attribute (0..1) */
    qreal m_volume;
    /** Fade multiplier (0..1) */
    qreal m_fadeMultiplier;
    /** 0 idle, 1 fading in, 2 fading out */
    int m_fadeState;
    /** Intensity frozen at fade-out start, < 0 when not frozen */
    qreal m_frozenIntensity;
    int m_fadeOutMs;
    qint64 m_startPosition;
    /** Frames sent in this run (the first one is logged) */
    quint64 m_framesSent;
    bool m_active;
    bool m_stopRequested;
    QVariantAnimation m_fadeAnim;
};

#endif // Q_OS_WIN && QLC_SPOUT

#endif // SPOUTVIDEOPLAYER_H
