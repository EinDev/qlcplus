/*
  Q Light Controller Plus
  videoframeprobe.h

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

#ifndef VIDEOFRAMEPROBE_H
#define VIDEOFRAMEPROBE_H

#include <QObject>
#include <QPointer>
#include <QVideoSink>
#include <QVideoFrame>

/**
 * Reports every frame a QML VideoOutput presents, with the frame's own
 * timestamp in the media.
 *
 * QML only sees the MediaPlayer's position, which the ffmpeg backend updates
 * to the seek target at once while the frames actually presented can still be
 * the ones decoded before the seek for a while (backward seeks especially).
 * VideoContext.qml uses this to pause a player only once a frame at the
 * wanted position is really on screen (see its seekPlayback/pausePlayback).
 *
 * Usage: VideoFrameProbe { sink: someVideoOutput.videoSink; onFramePresented: ... }
 */
class VideoFrameProbe : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *sink READ sink WRITE setSink NOTIFY sinkChanged)

public:
    explicit VideoFrameProbe(QObject *parent = nullptr);

    /** The QVideoSink to watch (a VideoOutput's videoSink) */
    QObject *sink() const;
    void setSink(QObject *sink);

signals:
    void sinkChanged();

    /** A frame was presented; $startTimeMs is its timestamp in the media,
     *  or -1 when the frame carries none */
    void framePresented(int startTimeMs);

private slots:
    void slotFrameChanged(const QVideoFrame &frame);

private:
    QPointer<QVideoSink> m_sink;
};

#endif
