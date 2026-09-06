/*
  Q Light Controller Plus
  audiobpmanalyzer.h

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

#ifndef AUDIOBPMANALYZER_H
#define AUDIOBPMANALYZER_H

#include <QObject>
#include <QThread>
#include <QString>

class Doc;

/** @addtogroup engine_audio Audio
 * @{
 */

/*********************************************************************
 * Worker that actually decodes the audio and runs offline BPM detection
 *********************************************************************/
class AudioBpmAnalyzerWorker final : public QObject
{
    Q_OBJECT
public:
    explicit AudioBpmAnalyzerWorker(Doc *doc, QObject *parent = nullptr);

public slots:
    void analyze(quint32 functionId, QString filePath);

signals:
    void analysisDone(quint32 functionId, bool success, double bpm,
                       double periodMs, double phaseMs, double confidence);

private:
    Doc *m_doc;
};

/*********************************************************************
 * Facade: queues offline BPM analysis requests onto a worker thread,
 * shared by all Audio functions of a Doc
 *********************************************************************/
class AudioBpmAnalyzer final : public QObject
{
    Q_OBJECT
public:
    explicit AudioBpmAnalyzer(Doc *doc, QObject *parent = nullptr);
    ~AudioBpmAnalyzer() override;

    /** Queue analysis of functionId's source file at filePath. Safe to call
     *  repeatedly; each call is an independent queued request. */
    void requestAnalysis(quint32 functionId, const QString &filePath);

signals:
    void analysisDone(quint32 functionId, bool success, double bpm,
                       double periodMs, double phaseMs, double confidence);
    void analyze(quint32 functionId, QString filePath); // internal, to worker

private:
    void ensureWorker();

private:
    Doc *m_doc;
    QThread m_thread;
    AudioBpmAnalyzerWorker *m_worker;
};

/** @} */

#endif // AUDIOBPMANALYZER_H
