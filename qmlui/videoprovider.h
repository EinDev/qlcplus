/*
  Q Light Controller Plus
  videoprovider.h

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

#ifndef VIDEOPROVIDER_H
#define VIDEOPROVIDER_H

#include <QQuickView>
#include <QQuickItem>
#include <QMediaPlayer>
#include <QPointer>
#include <QHash>

#include "video.h"

class Doc;
class Track;
class VideoContent;
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
class SpoutSender;
class SpoutVideoPlayer;
#endif

class VideoProvider final : public QObject
{
    Q_OBJECT
public:
    VideoProvider(QQuickView *view, Doc *doc, QObject *parent = 0);
    ~VideoProvider();

    /** The provider of the current document, null between documents.
     *  App creates exactly one per document and deletes it on document
     *  clear, so this is a lookup (never cached by callers) for the parts
     *  of the UI that need the live sender pool: the Show Manager's
     *  track output size and the Video editor's sender label. */
    static VideoProvider *instance();

    /** Get the main QML view */
    QQuickView *view() const;
    /** Get/Set the shared fullscreen context */
    QQuickView *fullscreenContext() const;
    void setFullscreenContext(QQuickView *context);
    /** Force close any video windows and contexts, and blank every Spout
     *  sender. Senders stay registered (see the sender pool below). */
    void shutdown();

#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    /*********************************************************************
     * Spout sender pool
     *
     * One SpoutSender per sender name, shared by every content that
     * resolves to that name (all clips on the same Show track), created
     * on demand and kept alive - blank/transparent while idle - until the
     * provider (i.e. the document) goes away, because receivers such as
     * OBS re-initialize their source whenever a sender disappears.
     * Senders for every Video in Spout mode are created eagerly at
     * document load by VideoContent::ensureSpoutSender(), as soon as the
     * video's resolution is known (immediately for pictures and when
     * SpoutSize is set, after the metadata probe for videos).
     *
     * A sender's size is fixed once created: the first clip whose size
     * becomes known sizes it (its SpoutSize override or native
     * resolution) and every later clip on the same track is aspect-fit
     * into that size at playback (SpoutVideoPlayer::canvasSize). Nothing
     * here resizes an existing sender on its own: receivers such as OBS
     * re-initialize their source on a size change, so a resize is only
     * ever an explicit user choice (ShowManager::setTrackSpoutSize).
     *
     * Only one player renders into a sender at a time: the latest one to
     * claim it. An earlier player still fading out on the same sender
     * (a fade tail overlapping the next clip on a track) goes silent.
     *********************************************************************/
public:
    /** Get the sender registered under $name, creating it at $size if it
     *  doesn't exist yet. Returns null if creation failed (not retried
     *  until the next document load) or $size is empty. */
    SpoutSender *spoutSender(const QString &name, const QSize &size, const QString &sizedBy = QString());

    /** Make $owner the one and only current writer of sender $name */
    void claimSpoutSender(const QString &name, QObject *owner);

    /** true if $owner is the current writer of sender $name */
    bool ownsSpoutSender(const QString &name, const QObject *owner) const;

    /** Drop $owner's claim on sender $name (no-op if it's not the owner) */
    void releaseSpoutSender(const QString &name, QObject *owner);

    /** true if nobody currently writes into sender $name */
    bool isSpoutSenderIdle(const QString &name) const;

    /** The current size of sender $name, empty if it doesn't exist */
    QSize spoutSenderSize(const QString &name) const;

    /** Explicitly change the size of sender $name (the user's choice in
     *  the Show Manager). Receivers re-initialize. A clip rendering into
     *  it follows on its next frame. No-op if the sender doesn't exist,
     *  $size is empty or already the current size. $reason is for the log. */
    void resizeSpoutSender(const QString &name, const QSize &size, const QString &reason);

    /** The Show track whose sender name is $name (see
     *  Video::spoutSenderNameForTrack()), null if there is none */
    Track *trackForSpoutSender(const QString &name) const;
#endif

public:
    /** The Spout output size of Show track $track as it stands right now:
     *  its fixed Track::spoutSize() if set, else the size of its live
     *  sender if one exists, else empty (nothing has fixed it yet). */
    QSize trackSpoutOutputSize(const Track *track) const;

    /** Re-run the eager sender creation for Video $videoId, e.g. after it
     *  was placed on a Show track (its default sender name changed) */
    void refreshSpoutSender(quint32 videoId);

signals:
    /** A sender was created or resized: anything showing sender sizes
     *  (track headers, the Video editor) should re-read them */
    void spoutSendersChanged();

protected slots:
    void slotFunctionAdded(quint32 id);
    void slotFunctionRemoved(quint32 id);

    void slotRequestPlayback(QString spoutSenderName);
    void slotRequestPause(bool enable);
    void slotRequestSeek(qint64 ms);
    void slotRequestStop();

private:
    /** Reference of the QML view */
    QQuickView *m_view;
    /** Reference of the project workspace */
    Doc *m_doc;
    /** Map of the currently available Video functions */
    QMap<quint32, VideoContent *> m_videoMap;
    /** A single instance for fullscreen rendering shared between videos */
    QPointer<QQuickView> m_fullscreenContext;
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    /** Spout senders by requested name (null = creation failed) */
    QHash<QString, SpoutSender *> m_spoutSenders;
    /** Current writer of each sender, by requested name */
    QHash<QString, QObject *> m_spoutOwners;
#endif
};

class VideoContent final : public QObject
{
    Q_OBJECT

    Q_PROPERTY(quint32 id READ id CONSTANT)

public:
    VideoContent(Video *video, VideoProvider *parent = nullptr);
    ~VideoContent();

    quint32 id() const;
    Q_INVOKABLE void destroyContext();

    /** Start rendering this content. $spoutSenderName is the sender to
     *  publish under in Spout mode (resolved by the engine, see
     *  Video::spoutSenderName()), ignored otherwise. */
    void playContent(const QString &spoutSenderName);
    void pauseContent(bool enable);
    /** Jump to $ms into the media without restarting it (Video::seekTo) */
    void seekContent(qint64 ms);
    void stopContent();

protected:
    QVariant getAttribute(quint32 id, const char *propName) const;
    void updateAttribute(quint32 id, const char *propName, QVariant value);

public slots:
    void slotDetectResolution();
    void slotAttributeChanged(int attrIndex, qreal value);

    /** Create this content's Spout sender now if it is in Spout mode and
     *  its size is known (see VideoProvider's sender pool). An existing
     *  sender is never resized. No-op on platforms without Spout. */
    void ensureSpoutSender();

protected slots:
    void slotDurationChanged(qint64 duration);
    void slotMetaDataChanged();
    /** Video::metaDataChanged: a "Resolution" from any prober (this content,
     *  the editor) lets a deferred Spout sender be created */
    void slotVideoMetaDataChanged(QString key, QVariant data);
    /** Video::outputModeChanged: create the sender when switching to Spout,
     *  blank it when switching away */
    void slotOutputModeChanged(int mode);
    void slotWindowClosing();
    void slotSpoutPlayerFinished();

private:
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    void playSpoutContent(const QString &spoutSenderName);
#endif

protected:
    /** Reference to the parent video provider */
    VideoProvider *m_provider;
    /** reference to the actual Video Function */
    Video *m_video;
    /** temporary media player to retrieve the video resolution */
    QMediaPlayer *m_mediaPlayer;
    /** the video position considering its resolution and the target screen */
    QRect m_geometry;
    /** Quick context for windowed video playback */
    QPointer<QQuickView> m_viewContext;
#if defined(Q_OS_WIN) && defined(QLC_SPOUT)
    /** Player for Spout output mode, one per run, null when idle */
    SpoutVideoPlayer *m_spoutPlayer;
#endif
};

#endif // VIDEOPROVIDER_H
