/*
  Q Light Controller Plus
  audio.h

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

#ifndef AUDIO_H
#define AUDIO_H

#include <QColor>

#include "audiorenderer.h"
#include "audiodecoder.h"
#include "function.h"

class QXmlStreamReader;

/** @addtogroup engine_functions Functions
 * @{
 */

class Audio final : public Function
{
    Q_OBJECT
    Q_DISABLE_COPY(Audio)

    /*********************************************************************
     * Initialization
     *********************************************************************/
public:
    enum BpmAnalysisState { NotAnalyzed = 0, Analyzing, Done, Failed };
    Q_ENUM(BpmAnalysisState)

    Audio(Doc* doc);
    virtual ~Audio();

    /** @reimp */
    QIcon getIcon() const override;

private:
    Doc *m_doc;
    /*********************************************************************
     * Copying
     *********************************************************************/
public:
    /** @reimp */
    Function* createCopy(Doc* doc, bool addToDoc = true) override;

    /** Copy the contents for this function from another function */
    bool copyFrom(const Function* function) override;

public slots:
    /** Catches Doc::functionRemoved() so that destroyed members can be
        removed immediately. */
    void slotFunctionRemoved(quint32 function);

    /*********************************************************************
     * Capabilities
     *********************************************************************/
public:
    QStringList getCapabilities() const;

    /*********************************************************************
     * Properties
     *********************************************************************/
public:
    Q_PROPERTY(double detectedBpm READ detectedBpm NOTIFY bpmChanged)
    Q_PROPERTY(double beatPeriodMs READ beatPeriodMs NOTIFY bpmChanged)
    Q_PROPERTY(double beatPhaseMs READ beatPhaseMs NOTIFY bpmChanged)
    Q_PROPERTY(double bpmConfidence READ bpmConfidence NOTIFY bpmChanged)
    Q_PROPERTY(BpmAnalysisState bpmAnalysisState READ bpmAnalysisState NOTIFY bpmChanged)

    /**
     * Returns the duration of the source audio file loaded
     *
     * @return Duration in milliseconds of the source audio file
     */
    quint32 totalDuration() override;

    /**
     * Set the playback duration of the audio file
     *
     * @param The playback total duration in milliseconds
     */
    void setTotalDuration(quint32 msec) override;

    /**
     * Set the source file name used by this Audio object
     */
    bool setSourceFileName(QString filename);

    /**
     * Retrieve the source file name used by this Audio object
     */
    QString getSourceFileName() const;

    /**
     * Retrieve the currently associated audio decoder
     */
    AudioDecoder* getAudioDecoder() const;

    /**
     * Set a specific audio device for rendering. If empty
     * the QLC+ global device will be used
     */
    void setAudioDevice(QString dev);

    /** Get/Set the audio function startup volume */
    qreal volume() const;
    void setVolume(qreal volume);

    /**
     * Retrieve the audio device set for this function
     */
    QString audioDevice() const;

    int adjustAttribute(qreal fraction, int attributeId) override;

    /** Detected BPM of the source audio file, 0.0 if not (yet) analyzed */
    double detectedBpm() const;

    /** Beat period, in milliseconds, of the source audio file, 0.0 if unknown */
    double beatPeriodMs() const;

    /** Offset, in milliseconds, from file start to the nearest beat-grid
     *  point, -1.0 if unknown */
    double beatPhaseMs() const;

    /** Confidence (~0..1) of the current BPM detection result */
    double bpmConfidence() const;

    /** Current state of the offline BPM analysis */
    BpmAnalysisState bpmAnalysisState() const;

    /** Kick off (or re-kick, if force) offline BPM detection on the current
     *  source file. No-op if already Analyzing, or (when !force) already
     *  Done/Failed. Never called from loadXML(). */
    Q_INVOKABLE void requestBpmDetection(bool force = false);

signals:
    void sourceFilenameChanged();
    void bpmChanged();

protected slots:
    void slotEndOfStream();

private slots:
    void slotBpmAnalysisDone(quint32 functionId, bool success, double bpm,
                              double periodMs, double phaseMs, double confidence);

private:
    void setBpmResult(BpmAnalysisState state, double bpm, double periodMs,
                       double phaseMs, double confidence);
    void resetBpmResult();

private:
    /** Instance of an AudioDecoder to perform actual audio decoding */
    AudioDecoder *m_decoder;
    /** output interface to render audio data got from m_decoder */
    AudioRenderer *m_audio_out;
    /** Audio device to use for rendering */
    QString m_audioDevice;
    /** Name of the source audio file */
    QString m_sourceFileName;
    /** Duration of the media object */
    qint64 m_audioDuration;
    /** Startup volume of the audio file */
    qreal m_volume;

    /** Offline BPM detection state and result */
    BpmAnalysisState m_bpmState;
    double m_detectedBpm;
    double m_beatPeriodMs;
    double m_beatPhaseMs;
    double m_bpmConfidence;

    /*********************************************************************
     * Save & Load
     *********************************************************************/
public:
    /** Save function's contents to an XML document */
    bool saveXML(QXmlStreamWriter *doc) const override;

    /** Load function's contents from an XML document */
    bool loadXML(QXmlStreamReader &root) override;

    /** @reimp */
    void postLoad() override;

    /*********************************************************************
     * Running
     *********************************************************************/
public:
    /** @reimpl */
    void preRun(MasterTimer*) override;

    /** @reimpl */
    void setPause(bool enable) override;

    /** @reimpl */
    void write(MasterTimer* timer, QList<Universe*> universes) override;

    /** @reimpl */
    void postRun(MasterTimer* timer, QList<Universe *> universes) override;
};

/** @} */

#endif
