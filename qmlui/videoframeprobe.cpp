/*
  Q Light Controller Plus
  videoframeprobe.cpp

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

#include "videoframeprobe.h"

VideoFrameProbe::VideoFrameProbe(QObject *parent)
    : QObject(parent)
{
}

QObject *VideoFrameProbe::sink() const
{
    return m_sink;
}

void VideoFrameProbe::setSink(QObject *sink)
{
    QVideoSink *videoSink = qobject_cast<QVideoSink *>(sink);
    if (videoSink == m_sink)
        return;

    if (m_sink)
        disconnect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoFrameProbe::slotFrameChanged);

    m_sink = videoSink;

    if (m_sink)
        connect(m_sink, &QVideoSink::videoFrameChanged, this, &VideoFrameProbe::slotFrameChanged);

    emit sinkChanged();
}

void VideoFrameProbe::slotFrameChanged(const QVideoFrame &frame)
{
    if (frame.isValid() == false)
        return;

    qint64 startTimeUs = frame.startTime();
    emit framePresented(startTimeUs < 0 ? -1 : int(startTimeUs / 1000));
}
