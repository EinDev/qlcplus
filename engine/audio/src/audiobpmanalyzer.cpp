/*
  Q Light Controller Plus
  audiobpmanalyzer.cpp

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

#include <vector>

#include "audiobpmanalyzer.h"
#include "audioplugincache.h"
#include "audiodecoder.h"
#include "beattracker.h"
#include "doc.h"

/*********************************************************************
 * AudioBpmAnalyzer
 *********************************************************************/

AudioBpmAnalyzer::AudioBpmAnalyzer(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
    , m_worker(nullptr)
{
}

AudioBpmAnalyzer::~AudioBpmAnalyzer()
{
    if (m_thread.isRunning())
    {
        m_thread.quit();
        m_thread.wait();
    }
}

void AudioBpmAnalyzer::ensureWorker()
{
    if (m_worker)
        return;

    m_worker = new AudioBpmAnalyzerWorker(m_doc);
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::finished,
            m_worker, &QObject::deleteLater);

    connect(this, &AudioBpmAnalyzer::analyze,
            m_worker, &AudioBpmAnalyzerWorker::analyze,
            Qt::QueuedConnection);

    connect(m_worker, &AudioBpmAnalyzerWorker::analysisDone,
            this, &AudioBpmAnalyzer::analysisDone,
            Qt::QueuedConnection);

    m_thread.start();
}

void AudioBpmAnalyzer::requestAnalysis(quint32 functionId, const QString &filePath)
{
    ensureWorker();
    emit analyze(functionId, filePath);
}

/*********************************************************************
 * AudioBpmAnalyzerWorker
 *********************************************************************/

AudioBpmAnalyzerWorker::AudioBpmAnalyzerWorker(Doc *doc, QObject *parent)
    : QObject(parent)
    , m_doc(doc)
{
}

void AudioBpmAnalyzerWorker::analyze(quint32 functionId, QString filePath)
{
    if (m_doc == nullptr)
    {
        emit analysisDone(functionId, false, 0.0, 0.0, 0.0, 0.0);
        return;
    }

    AudioDecoder *ad = m_doc->audioPluginCache()->getDecoderForFile(filePath);
    if (ad == nullptr)
    {
        emit analysisDone(functionId, false, 0.0, 0.0, 0.0, 0.0);
        return;
    }

    ad->seek(0);

    AudioParameters ap = ad->audioParameters();
    int channels = ap.channels();

    if (channels <= 0 || ap.sampleRate() <= 0)
    {
        delete ad;
        emit analysisDone(functionId, false, 0.0, 0.0, 0.0, 0.0);
        return;
    }

    // Decoders default to PCM_S16LE (see AudioDecoder::configure()) - same
    // assumption WaveformWorker::generateWaveform() already makes, which is
    // why the read buffer below is reinterpreted directly as int16 samples
    // with no per-sample-size handling.

    BeatTracker tracker(ap.sampleRate(), channels);

    const qint64 readChunkSize = 16384;
    std::vector<char> buffer(readChunkSize);
    qint64 dataRead = 0;

    while ((dataRead = ad->read(buffer.data(), readChunkSize)) > 0)
    {
        qint64 sampleCount = dataRead / 2; // int16 samples in this chunk
        if (sampleCount > 0)
        {
            tracker.processAudio(reinterpret_cast<const int16_t *>(buffer.data()),
                                  int(sampleCount));
        }
    }

    delete ad;

    double bpm = tracker.bpm();
    double confidence = tracker.confidence();
    double periodMs = tracker.beatPeriodMs();
    double phaseMs = tracker.beatPhaseMs();
    bool success = bpm > 0.0 && phaseMs >= 0.0;

    emit analysisDone(functionId, success,
                       success ? bpm : 0.0,
                       success ? periodMs : 0.0,
                       success ? phaseMs : 0.0,
                       success ? confidence : 0.0);
}
