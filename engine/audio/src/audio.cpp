/*
  Q Light Controller Plus
  audio.cpp

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

#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>

#include "audiobpmanalyzer.h"
#include "audiodecoder.h"
#include "audiorenderer.h"
#include "audioplugincache.h"

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
 #if defined(__APPLE__) || defined(Q_OS_MAC)
   #include "audiorenderer_portaudio.h"
 #elif defined(WIN32) || defined(Q_OS_WIN)
   #include "audiorenderer_waveout.h"
 #else
   #include "audiorenderer_alsa.h"
 #endif
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
 #include "audiorenderer_qt5.h"
#else
 #include "audiorenderer_qt6.h"
#endif

#include "audio.h"
#include "doc.h"

#define KXMLQLCAudioSource QStringLiteral("Source")
#define KXMLQLCAudioDevice QStringLiteral("Device")
#define KXMLQLCAudioVolume QStringLiteral("Volume")
#define KXMLQLCAudioMuted QStringLiteral("Muted")

#define KXMLQLCAudioBpm QStringLiteral("Bpm")
#define KXMLQLCAudioBpmValue QStringLiteral("bpm")
#define KXMLQLCAudioBpmPeriod QStringLiteral("period")
#define KXMLQLCAudioBpmPhase QStringLiteral("phase")
#define KXMLQLCAudioBpmConfidence QStringLiteral("confidence")

/*****************************************************************************
 * Initialization
 *****************************************************************************/

Audio::Audio(Doc* doc)
  : Function(doc, Function::AudioType)
  , m_doc(doc)
  , m_decoder(NULL)
  , m_audio_out(NULL)
  , m_audioDevice(QString())
  , m_sourceFileName("")
  , m_audioDuration(0)
  , m_volume(1.0)
  , m_muted(false)
  , m_bpmState(NotAnalyzed)
  , m_detectedBpm(0.0)
  , m_beatPeriodMs(0.0)
  , m_beatPhaseMs(0.0)
  , m_bpmConfidence(0.0)
{
    setName(tr("New Audio"));
    setRunOrder(Audio::SingleShot);

    // Listen to member Function removals
    connect(doc, SIGNAL(functionRemoved(quint32)),
            this, SLOT(slotFunctionRemoved(quint32)));
}

Audio::~Audio()
{
    if (m_audio_out != NULL)
    {
        m_audio_out->stop();
        delete m_audio_out;
    }
    if (m_decoder != NULL)
        delete m_decoder;
}

QIcon Audio::getIcon() const
{
    return QIcon(":/audio.png");
}

/*****************************************************************************
 * Copying
 *****************************************************************************/

Function* Audio::createCopy(Doc* doc, bool addToDoc)
{
    Q_ASSERT(doc != NULL);

    Function* copy = new Audio(doc);
    if (copy->copyFrom(this) == false)
    {
        delete copy;
        copy = NULL;
    }
    if (addToDoc == true && doc->addFunction(copy) == false)
    {
        delete copy;
        copy = NULL;
    }

    return copy;
}

bool Audio::copyFrom(const Function* function)
{
    const Audio* aud = qobject_cast<const Audio*> (function);
    if (aud == NULL)
        return false;

    setSourceFileName(aud->m_sourceFileName);
    setBpmResult(aud->m_bpmState, aud->m_detectedBpm, aud->m_beatPeriodMs,
                 aud->m_beatPhaseMs, aud->m_bpmConfidence);
    m_audioDuration = aud->m_audioDuration;
    m_volume = aud->m_volume;
    m_muted = aud->m_muted;

    return Function::copyFrom(function);
}

QStringList Audio::getCapabilities() const
{
    return m_doc->audioPluginCache()->getSupportedFormats();
}

/*********************************************************************
 * Properties
 *********************************************************************/
quint32 Audio::totalDuration()
{
    return (quint32)m_audioDuration;
}

void Audio::setTotalDuration(quint32 msec)
{
    qDebug() << "Audio set total duration:" << msec;
    m_audioDuration = msec;

    emit totalDurationChanged();
}

bool Audio::setSourceFileName(QString filename)
{
    resetBpmResult();

    if (m_sourceFileName.isEmpty() == false)
    {
        // unload previous source
        if (m_decoder != NULL)
        {
            delete m_decoder;
            m_decoder = NULL;
        }
    }

    m_sourceFileName = filename;

    if (QFile(m_sourceFileName).exists())
    {
        setName(QFileInfo(m_sourceFileName).fileName());
    }
    else
    {
        doc()->appendToErrorLog(tr("Audio file <b>%1</b> not found").arg(m_sourceFileName));
        setName(tr("File not found"));
        //m_audioDuration = 0;
        emit changed(id());
        return true;
    }
    emit sourceFilenameChanged();

    m_decoder = m_doc->audioPluginCache()->getDecoderForFile(m_sourceFileName);

    if (m_decoder == NULL)
        return false;

    setDuration(m_decoder->totalTime());
    setTotalDuration(m_decoder->totalTime());

    emit changed(id());

    return true;
}

void Audio::relinkSource(const QString &filename)
{
    if (m_sourceFileName == filename)
        return;

    m_sourceFileName = filename;

    emit sourceFilenameChanged();
    emit changed(id());
}

QString Audio::getSourceFileName() const
{
    return m_sourceFileName;
}

AudioDecoder *Audio::getAudioDecoder() const
{
    return m_decoder;
}

void Audio::setAudioDevice(QString dev)
{
    m_audioDevice = dev;
}

qreal Audio::volume() const
{
    return m_volume;
}

void Audio::setVolume(qreal volume)
{
    m_volume = volume;

    // take effect immediately on a running clip
    if (m_audio_out != NULL)
        m_audio_out->adjustIntensity(effectiveVolume() * getAttributeValue(Function::Intensity));
}

bool Audio::muted() const
{
    return m_muted;
}

void Audio::setMuted(bool muted)
{
    m_muted = muted;

    if (m_audio_out != NULL)
        m_audio_out->adjustIntensity(effectiveVolume() * getAttributeValue(Function::Intensity));
}

qreal Audio::effectiveVolume() const
{
    return m_muted ? 0.0 : m_volume;
}

QString Audio::audioDevice() const
{
    return m_audioDevice;
}

int Audio::adjustAttribute(qreal fraction, int attributeId)
{
    int attrIndex = Function::adjustAttribute(fraction, attributeId);

    if (m_audio_out != NULL && attrIndex == Intensity)
        m_audio_out->adjustIntensity(effectiveVolume() * getAttributeValue(Function::Intensity));

    return attrIndex;
}

double Audio::detectedBpm() const
{
    return m_detectedBpm;
}

double Audio::beatPeriodMs() const
{
    return m_beatPeriodMs;
}

double Audio::beatPhaseMs() const
{
    return m_beatPhaseMs;
}

double Audio::bpmConfidence() const
{
    return m_bpmConfidence;
}

Audio::BpmAnalysisState Audio::bpmAnalysisState() const
{
    return m_bpmState;
}

void Audio::setBpmResult(BpmAnalysisState state, double bpm, double periodMs,
                          double phaseMs, double confidence)
{
    m_bpmState = state;
    m_detectedBpm = bpm;
    m_beatPeriodMs = periodMs;
    m_beatPhaseMs = phaseMs;
    m_bpmConfidence = confidence;

    emit bpmChanged();
}

void Audio::resetBpmResult()
{
    setBpmResult(NotAnalyzed, 0.0, 0.0, 0.0, 0.0);
}

void Audio::requestBpmDetection(bool force)
{
    if (m_sourceFileName.isEmpty() || !QFile(m_sourceFileName).exists())
        return;
    if (m_bpmState == Analyzing)
        return;
    if (!force && (m_bpmState == Done || m_bpmState == Failed))
        return;

    m_bpmState = Analyzing;
    emit bpmChanged();

    AudioBpmAnalyzer *analyzer = m_doc->audioBpmAnalyzer();
    connect(analyzer, &AudioBpmAnalyzer::analysisDone,
            this, &Audio::slotBpmAnalysisDone, Qt::UniqueConnection);
    analyzer->requestAnalysis(id(), m_sourceFileName);
}

void Audio::slotBpmAnalysisDone(quint32 functionId, bool success, double bpm,
                                 double periodMs, double phaseMs, double confidence)
{
    if (functionId != id())
        return;
    if (m_bpmState != Analyzing)
        return;

    setBpmResult(success ? Done : Failed,
                 success ? bpm : 0.0, success ? periodMs : 0.0,
                 success ? phaseMs : 0.0, success ? confidence : 0.0);
    emit changed(id());
}

void Audio::slotEndOfStream()
{
    if (!stopped())
        stop(FunctionParent::master(FunctionParent::EngineSelfStop));

    if (m_audio_out != NULL)
    {
        m_audio_out->stop();
        m_audio_out->deleteLater();
        m_audio_out = NULL;
    }
    if (m_decoder != NULL)
        m_decoder->seek(0);
}

void Audio::slotFunctionRemoved(quint32 fid)
{
    Q_UNUSED(fid)
}

/*********************************************************************
 * Save & Load
 *********************************************************************/

bool Audio::saveXML(QXmlStreamWriter *doc) const
{
    Q_ASSERT(doc != NULL);

    /* Function tag */
    doc->writeStartElement(KXMLQLCFunction);

    /* Common attributes */
    saveXMLCommon(doc);

    /* Speed */
    saveXMLSpeed(doc);

    /* Playback mode */
    saveXMLRunOrder(doc);

    doc->writeStartElement(KXMLQLCAudioSource);

    if (m_audioDevice.isEmpty() == false)
        doc->writeAttribute(KXMLQLCAudioDevice, m_audioDevice);

    if (m_volume != 1.0)
        doc->writeAttribute(KXMLQLCAudioVolume, QString::number(m_volume));

    if (m_muted)
        doc->writeAttribute(KXMLQLCAudioMuted, "1");

    doc->writeCharacters(m_doc->normalizeComponentPath(m_sourceFileName));

    doc->writeEndElement();

    if (m_bpmState == Done)
    {
        doc->writeStartElement(KXMLQLCAudioBpm);
        doc->writeAttribute(KXMLQLCAudioBpmValue, QString::number(m_detectedBpm, 'f', 2));
        doc->writeAttribute(KXMLQLCAudioBpmPeriod, QString::number(m_beatPeriodMs, 'f', 2));
        doc->writeAttribute(KXMLQLCAudioBpmPhase, QString::number(m_beatPhaseMs, 'f', 2));
        doc->writeAttribute(KXMLQLCAudioBpmConfidence, QString::number(m_bpmConfidence, 'f', 3));
        doc->writeEndElement();
    }

    /* End the <Function> tag */
    doc->writeEndElement();

    return true;
}

bool Audio::loadXML(QXmlStreamReader &root)
{
    if (root.name() != KXMLQLCFunction)
    {
        qWarning() << Q_FUNC_INFO << "Function node not found";
        return false;
    }

    if (root.attributes().value(KXMLQLCFunctionType).toString() != typeToString(Function::AudioType))
    {
        qWarning() << Q_FUNC_INFO << root.attributes().value(KXMLQLCFunctionType).toString()
                   << "is not Audio";
        return false;
    }

    QString fname = name();

    while (root.readNextStartElement())
    {
        if (root.name() == KXMLQLCAudioSource)
        {
            QXmlStreamAttributes attrs = root.attributes();

            if (attrs.hasAttribute(KXMLQLCAudioDevice))
                setAudioDevice(attrs.value(KXMLQLCAudioDevice).toString());
            if (attrs.hasAttribute(KXMLQLCAudioVolume))
                setVolume(attrs.value(KXMLQLCAudioVolume).toString().toDouble());
            if (attrs.hasAttribute(KXMLQLCAudioMuted))
                setMuted(attrs.value(KXMLQLCAudioMuted).toString() == "1");

            setSourceFileName(m_doc->denormalizeComponentPath(root.readElementText()));
        }
        else if (root.name() == KXMLQLCFunctionSpeed)
        {
            loadXMLSpeed(root);
        }
        else if (root.name() == KXMLQLCFunctionRunOrder)
        {
            loadXMLRunOrder(root);
        }
        else if (root.name() == KXMLQLCAudioBpm)
        {
            QXmlStreamAttributes attrs = root.attributes();
            double bpm = attrs.value(KXMLQLCAudioBpmValue).toString().toDouble();
            double period = attrs.value(KXMLQLCAudioBpmPeriod).toString().toDouble();
            double phase = attrs.value(KXMLQLCAudioBpmPhase).toString().toDouble();
            double confidence = attrs.hasAttribute(KXMLQLCAudioBpmConfidence)
                                 ? attrs.value(KXMLQLCAudioBpmConfidence).toString().toDouble() : 0.0;
            setBpmResult(Done, bpm, period, phase, confidence);
            root.skipCurrentElement();
        }
        else
        {
            qWarning() << Q_FUNC_INFO << "Unknown Audio tag:" << root.name();
            root.skipCurrentElement();
        }
    }

    setName(fname);

    return true;
}

void Audio::postLoad()
{
}

/*********************************************************************
 * Running
 *********************************************************************/
void Audio::preRun(MasterTimer* timer)
{
    if (m_decoder != NULL)
    {
        uint fadeIn = overrideFadeInSpeed() == defaultSpeed() ? fadeInSpeed() : overrideFadeInSpeed();

        if (m_audio_out != NULL && m_audio_out->isRunning())
        {
            delete m_audio_out;
            m_audio_out = NULL;
        }

        m_decoder->seek(elapsed());
        AudioParameters ap = m_decoder->audioParameters();
#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
 #if defined(__APPLE__) || defined(Q_OS_MAC)
        //m_audio_out = new AudioRendererCoreAudio();
        m_audio_out = new AudioRendererPortAudio(m_audioDevice);
 #elif defined(WIN32) || defined(Q_OS_WIN)
        m_audio_out = new AudioRendererWaveOut(m_audioDevice);
 #else
        m_audio_out = new AudioRendererAlsa(m_audioDevice);
 #endif
        m_audio_out->moveToThread(QCoreApplication::instance()->thread());
#elif QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        m_audio_out = new AudioRendererQt5(m_audioDevice, doc());
#else
        m_audio_out = new AudioRendererQt6(m_audioDevice, doc());
#endif
        m_audio_out->setDecoder(m_decoder);
        m_audio_out->initialize(ap.sampleRate(), ap.channels(), ap.format());
        m_audio_out->adjustIntensity(effectiveVolume() * getAttributeValue(Intensity));
        m_audio_out->setFadeIn(elapsed() ? 0 : fadeIn);
        m_audio_out->setLooped(runOrder() == Audio::Loop);
        m_audio_out->setUserStop(false);
        m_audio_out->start();
    }

    Function::preRun(timer);
}

void Audio::setPause(bool enable)
{
    if (isRunning())
    {
        if (m_audio_out != NULL)
        {
            if (enable)
                m_audio_out->suspend();
            else
                m_audio_out->resume();
        }

        Function::setPause(enable);
    }
}

void Audio::write(MasterTimer* timer, QList<Universe *> universes)
{
    Q_UNUSED(timer)
    Q_UNUSED(universes)

    if (isPaused())
        return;

    incrementElapsed();

    if (m_audio_out && !m_audio_out->isLooped())
    {
        uint fadeout = overrideFadeOutSpeed() == defaultSpeed() ? fadeOutSpeed() : overrideFadeOutSpeed();

        if (fadeout)
        {
            if (m_audio_out != NULL && totalDuration() - elapsed() <= fadeOutSpeed())
                m_audio_out->setFadeOut(fadeOutSpeed());
        }
        if (m_audio_out->isEos())
            slotEndOfStream();
    }
}

void Audio::postRun(MasterTimer* timer, QList<Universe*> universes)
{
    // Check whether a fade out is needed "outside" of the natural playback
    // This is the case of a Chaser step
    uint fadeout = overrideFadeOutSpeed() == defaultSpeed() ? fadeOutSpeed() : overrideFadeOutSpeed();

    if (fadeout == 0)
    {
        slotEndOfStream();
    }
    else
    {
        if (m_audio_out != NULL)
            m_audio_out->setFadeOut(fadeout);
    }

    Function::postRun(timer, universes);
}
