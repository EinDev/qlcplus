/*
  Q Light Controller Plus
  video.cpp

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
#include <QMediaPlayer>
#include <QDebug>
#include <QFile>

#include "video.h"
#include "doc.h"
#include "show.h"
#include "track.h"
#include "showfunction.h"

#define KXMLQLCVideoSource      QStringLiteral("Source")
#define KXMLQLCVideoScreen      QStringLiteral("Screen")
#define KXMLQLCVideoFullscreen  QStringLiteral("Fullscreen")
#define KXMLQLCVideoGeometry    QStringLiteral("Geometry")
#define KXMLQLCVideoRotation    QStringLiteral("Rotation")
#define KXMLQLCVideoZIndex      QStringLiteral("ZIndex")
#define KXMLQLCVideoOutput      QStringLiteral("Output")
#define KXMLQLCVideoOutputSpout QStringLiteral("spout")
#define KXMLQLCVideoSpoutSize   QStringLiteral("SpoutSize")
#define KXMLQLCVideoVolume      QStringLiteral("Volume")
#define KXMLQLCVideoMuted       QStringLiteral("Muted")

#define KSpoutSenderPrefix      QStringLiteral("QLC+ ")

const QStringList Video::m_defaultVideoCaps =
        QStringList() << "*.avi" << "*.wmv" << "*.mkv" << "*.mp4" << "*.mov" << "*.mpg" << "*.mpeg" << "*.flv" << "*.webm";
const QStringList Video::m_defaultPictureCaps =
        QStringList() << "*.png" << "*.bmp" << "*.jpg" << "*.jpeg" << "*.gif";

/*****************************************************************************
 * Initialization
 *****************************************************************************/

Video::Video(Doc* doc)
  : Function(doc, Function::VideoType)
  , m_doc(doc)
  , m_sourceUrl("")
  , m_isPicture(false)
  , m_videoDuration(0)
  , m_resolution(QSize(0,0))
  , m_customGeometry(QRect())
  , m_rotation(QVector3D(0, 0, 0))
  , m_zIndex(1)
  , m_screen(0)
  , m_outputMode(Windowed)
  , m_spoutSize(QSize(0, 0))
  , m_muted(false)
{
    setName(tr("New Video"));
    setRunOrder(Video::SingleShot);

    registerAttribute(tr("Volume"), Function::LastWins, 0, 100, 100);
    registerAttribute(tr("X Rotation"), Function::LastWins, -360.0, 360.0, 0.0);
    registerAttribute(tr("Y Rotation"), Function::LastWins, -360.0, 360.0, 0.0);
    registerAttribute(tr("Z Rotation"), Function::LastWins, -360.0, 360.0, 0.0);
    registerAttribute(tr("X Position"), Function::LastWins, -100.0, 100.0, 0.0);
    registerAttribute(tr("Y Position"), Function::LastWins, -100.0, 100.0, 0.0);
    registerAttribute(tr("Width scale"), Function::LastWins, 0, 1000.0, 100.0);
    registerAttribute(tr("Height scale"), Function::LastWins, 0, 1000.0, 100.0);

    // Listen to member Function removals
    connect(doc, SIGNAL(functionRemoved(quint32)),
            this, SLOT(slotFunctionRemoved(quint32)));
}

Video::~Video()
{
}

QIcon Video::getIcon() const
{
    return QIcon(":/video.png");
}

/*****************************************************************************
 * Copying
 *****************************************************************************/

Function* Video::createCopy(Doc* doc, bool addToDoc)
{
    Q_ASSERT(doc != NULL);

    Function* copy = new Video(doc);
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

bool Video::copyFrom(const Function* function)
{
    const Video* vid = qobject_cast<const Video*> (function);
    if (vid == NULL)
        return false;

    setSourceUrl(vid->m_sourceUrl);
    m_videoDuration = vid->m_videoDuration;
    m_outputMode = vid->m_outputMode;
    m_spoutSize = vid->m_spoutSize;
    // Function::copyFrom() doesn't copy attribute values
    setVolume(vid->volume());
    setMuted(vid->m_muted);

    return Function::copyFrom(function);
}

QStringList Video::getVideoCapabilities()
{
    QStringList caps;
    QStringList mimeTypes;
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    mimeTypes = QMediaPlayer::supportedMimeTypes();
#endif

    if (mimeTypes.isEmpty())
    {
        return m_defaultVideoCaps;
    }
    else
    {
        qDebug() << "Supported video types:" << mimeTypes;

        foreach (QString mime, mimeTypes)
        {
            if (mime.startsWith("video/"))
            {
                if (mime.endsWith("/3gpp")) caps << "*.3gp";
                else if (mime.endsWith("/mp4")) caps << "*.mp4";
                else if (mime.endsWith("/avi")) caps << "*.avi";
                else if (mime.endsWith("/m2ts")) caps << "*.m2ts";
                else if (mime.endsWith("m4v")) caps << "*.m4v";
                else if (mime.endsWith("/mpeg")) caps << "*.mpeg";
                else if (mime.endsWith("/mpg")) caps << "*.mpg";
                else if (mime.endsWith("/quicktime")) caps << "*.mov";
                else if (mime.endsWith("/webm")) caps << "*.webm";
                else if (mime.endsWith("matroska")) caps << "*.mkv";
            }
        }
    }
    return caps;
}

QStringList Video::getPictureCapabilities()
{
    return m_defaultPictureCaps;
}

/*********************************************************************
 * Properties
 *********************************************************************/
void Video::setTotalDuration(quint32 duration)
{
    if (m_videoDuration == (qint64)duration)
        return;

    m_videoDuration = (qint64)duration;
    emit totalTimeChanged(m_videoDuration);
}

quint32 Video::totalDuration()
{
    return (quint32)m_videoDuration;
}

QSize Video::resolution() const
{
    return m_resolution;
}

void Video::setResolution(QSize size)
{
    m_resolution = size;
    emit metaDataChanged("Resolution", QVariant(m_resolution));
}

QRect Video::customGeometry() const
{
    return m_customGeometry;
}

void Video::setCustomGeometry(QRect rect)
{
    if (rect == m_customGeometry)
        return;

    m_customGeometry = rect;
    emit customGeometryChanged(rect);
}

QVector3D Video::rotation() const
{
    return m_rotation;
}

void Video::setRotation(QVector3D rotation)
{
    if (m_rotation == rotation)
        return;

    m_rotation = rotation;
    emit rotationChanged(m_rotation);
}

int Video::zIndex() const
{
    return m_zIndex;
}

void Video::setZIndex(int idx)
{
    if (m_zIndex == idx)
        return;

    m_zIndex = idx;
    emit zIndexChanged(m_zIndex);
}

void Video::setAudioCodec(QString codec)
{
    m_audioCodec = codec;
    emit metaDataChanged("AudioCodec", QVariant(m_audioCodec));
}

QString Video::audioCodec() const
{
    return m_audioCodec;
}

void Video::setVideoCodec(QString codec)
{
    m_videoCodec = codec;
    emit metaDataChanged("VideoCodec", QVariant(m_videoCodec));
}

QString Video::videoCodec() const
{
    return m_videoCodec;
}

bool Video::setSourceUrl(QString filename)
{
    m_sourceUrl = filename;
    qDebug() << Q_FUNC_INFO << "Source name set:" << m_sourceUrl;

    QString fileExt = "*" + filename.mid(filename.lastIndexOf('.'));

    if (m_defaultPictureCaps.contains(fileExt))
    {
        m_isPicture = true;
    }

    if (m_sourceUrl.contains("://"))
    {
        QUrl url(m_sourceUrl);
        setName(url.fileName());
    }
    else
    {
        if (QFile(m_sourceUrl).exists())
        {
            setName(QFileInfo(m_sourceUrl).fileName());
        }
        else
        {
            doc()->appendToErrorLog(tr("Video file <b>%1</b> not found").arg(m_sourceUrl));
            setName(tr("File not found"));
        }
    }

    emit sourceChanged(m_sourceUrl);

    return true;
}

void Video::relinkSource(const QString &filename)
{
    if (m_sourceUrl == filename)
        return;

    m_sourceUrl = filename;

    emit sourceChanged(m_sourceUrl);
    emit changed(id());
}

bool Video::isPicture() const
{
    return m_isPicture;
}

QString Video::sourceUrl() const
{
    return m_sourceUrl;
}

void Video::setScreen(int index)
{
    m_screen = index;
    emit changed(id());
}

int Video::screen() const
{
    return m_screen;
}

void Video::setFullscreen(bool enable)
{
    setOutputMode(enable ? Fullscreen : Windowed);
}

Video::OutputMode Video::outputMode() const
{
    return m_outputMode;
}

void Video::setOutputMode(OutputMode mode)
{
    if (m_outputMode == mode)
        return;

    m_outputMode = mode;
    emit outputModeChanged(int(m_outputMode));
    emit changed(id());
}

void Video::setOutputMode(int mode)
{
    switch (mode)
    {
        case Fullscreen: setOutputMode(Fullscreen); break;
        case Spout: setOutputMode(Spout); break;
        default: setOutputMode(Windowed); break;
    }
}

QSize Video::spoutSize() const
{
    return m_spoutSize;
}

void Video::setSpoutSize(QSize size)
{
    // a negative or half-set size makes no sense for a sender: treat it
    // as "native resolution"
    if (size.width() <= 0 || size.height() <= 0)
        size = QSize(0, 0);

    if (m_spoutSize == size)
        return;

    m_spoutSize = size;
    emit spoutSizeChanged(m_spoutSize);
    emit changed(id());
}

QString Video::runtimeSenderName() const
{
    return m_runtimeSenderName;
}

void Video::setRuntimeSenderName(const QString &name)
{
    m_runtimeSenderName = name;
}

QString Video::spoutSenderName() const
{
    if (m_runtimeSenderName.isEmpty() == false)
        return m_runtimeSenderName;

    return defaultSpoutSenderName();
}

QString Video::defaultSpoutSenderName() const
{
    Track *track = spoutTrack();
    if (track != nullptr)
        return spoutSenderNameForTrack(track->name());

    return KSpoutSenderPrefix + name();
}

Track *Video::spoutTrack() const
{
    // First Show track (Shows, then tracks, in ID order) containing this
    // Video: the Show runner names the sender after the track it plays
    // from, and this rule picks the same name for the track a user is
    // most likely to have put the clip on, so the sender created at
    // document load is the one the Show will actually feed.
    for (Function *f : m_doc->functionsByType(Function::ShowType))
    {
        Show *show = qobject_cast<Show *>(f);
        if (show == nullptr)
            continue;

        for (Track *track : show->tracks())
        {
            for (ShowFunction *sf : track->showFunctions())
            {
                if (sf->functionID() == id())
                    return track;
            }
        }
    }

    return nullptr;
}

QString Video::spoutSenderNameForTrack(const QString &trackName)
{
    return KSpoutSenderPrefix + trackName;
}

qreal Video::intensity() const
{
    return getAttributeValue(Intensity);
}

bool Video::fullscreen() const
{
    return m_outputMode == Fullscreen;
}

qreal Video::volume() const
{
    return getAttributeValue(Volume);
}

void Video::setVolume(qreal volume)
{
    // adjustAttribute() clamps, emits attributeChanged() (which the players
    // listen to) and volumeChanged(), and is a no-op when unchanged
    adjustAttribute(volume, Volume);
}

bool Video::muted() const
{
    return m_muted;
}

void Video::setMuted(bool muted)
{
    if (m_muted == muted)
        return;

    m_muted = muted;
    emit mutedChanged(muted);
}

int Video::adjustAttribute(qreal fraction, int attributeId)
{
    int attrIndex = Function::adjustAttribute(fraction, attributeId);

    switch (attrIndex)
    {
        case Intensity:
        {
            emit requestBrightnessVolumeAdjust(getAttributeValue(Intensity));
            emit intensityChanged();
        }
        break;
        case Volume:
            emit volumeChanged();
        break;
        default:
        break;
    }

    return attrIndex;
}

void Video::slotFunctionRemoved(quint32 fid)
{
    Q_UNUSED(fid)
}

void Video::stopFromUI()
{
    if (isRunning())
        stop(FunctionParent::master(FunctionParent::VideoWindowClosed));
}

void Video::seekTo(quint32 ms)
{
    if (isRunning() == false)
        return;

    emit requestSeek(qint64(ms));
}

/*********************************************************************
 * Save & Load
 *********************************************************************/

bool Video::saveXML(QXmlStreamWriter *doc) const
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

    doc->writeStartElement(KXMLQLCVideoSource);
    if (m_screen > 0)
        doc->writeAttribute(KXMLQLCVideoScreen, QString::number(m_screen));
    // Fullscreen keeps its pre-Spout shape so older builds load it unchanged.
    // Spout is a separate attribute: an older build that doesn't know it
    // silently falls back to Windowed instead of misreading the project.
    if (m_outputMode == Fullscreen)
        doc->writeAttribute(KXMLQLCVideoFullscreen, "1");
    else if (m_outputMode == Spout)
        doc->writeAttribute(KXMLQLCVideoOutput, KXMLQLCVideoOutputSpout);
    if (m_spoutSize.isEmpty() == false)
    {
        QString size = QString("%1,%2").arg(m_spoutSize.width()).arg(m_spoutSize.height());
        doc->writeAttribute(KXMLQLCVideoSpoutSize, size);
    }
    // Only written when non-default so projects of older builds stay byte-identical
    if (volume() != 100.0)
        doc->writeAttribute(KXMLQLCVideoVolume, QString::number(volume()));
    if (m_muted)
        doc->writeAttribute(KXMLQLCVideoMuted, "1");
#ifdef QMLUI
    if (m_customGeometry.isNull() == false)
    {
        QString rect = QString("%1,%2,%3,%4")
                .arg(m_customGeometry.x()).arg(m_customGeometry.y())
                .arg(m_customGeometry.width()).arg(m_customGeometry.height());
        doc->writeAttribute(KXMLQLCVideoGeometry, rect);
    }
    if (m_rotation.isNull() == false)
    {
        QString rot = QString("%1,%2,%3").arg(m_rotation.x()).arg(m_rotation.y()).arg(m_rotation.z());
        doc->writeAttribute(KXMLQLCVideoRotation, rot);
    }
    doc->writeAttribute(KXMLQLCVideoZIndex, QString::number(m_zIndex));
#endif
    if (m_sourceUrl.contains("://"))
        doc->writeCharacters(m_sourceUrl);
    else
        doc->writeCharacters(m_doc->normalizeComponentPath(m_sourceUrl));

    doc->writeEndElement();

    /* End the <Function> tag */
    doc->writeEndElement();

    return true;
}

bool Video::loadXML(QXmlStreamReader &root)
{
    if (root.name() != KXMLQLCFunction)
    {
        qWarning() << Q_FUNC_INFO << "Function node not found";
        return false;
    }

    if (root.attributes().value(KXMLQLCFunctionType).toString() != typeToString(Function::VideoType))
    {
        qWarning() << Q_FUNC_INFO << root.attributes().value(KXMLQLCFunctionType).toString()
                   << "is not Video";
        return false;
    }

    QString fname = name();

    while (root.readNextStartElement())
    {
        if (root.name() == KXMLQLCVideoSource)
        {
            QXmlStreamAttributes attrs = root.attributes();
            if (attrs.hasAttribute(KXMLQLCVideoScreen))
                setScreen(attrs.value(KXMLQLCVideoScreen).toString().toInt());

            if (attrs.hasAttribute(KXMLQLCVideoFullscreen))
            {
                if (attrs.value(KXMLQLCVideoFullscreen).toString() == "1")
                    setFullscreen(true);
                else
                    setFullscreen(false);
            }

            // read after Fullscreen so that Spout wins if both are present
            if (attrs.hasAttribute(KXMLQLCVideoOutput))
            {
                if (attrs.value(KXMLQLCVideoOutput).toString() == KXMLQLCVideoOutputSpout)
                    setOutputMode(Spout);
            }

            if (attrs.hasAttribute(KXMLQLCVideoSpoutSize))
            {
                QStringList slist = attrs.value(KXMLQLCVideoSpoutSize).toString().split(",");
                if (slist.count() == 2)
                    setSpoutSize(QSize(slist.at(0).toInt(), slist.at(1).toInt()));
            }
            if (attrs.hasAttribute(KXMLQLCVideoVolume))
                setVolume(attrs.value(KXMLQLCVideoVolume).toString().toDouble());
            if (attrs.hasAttribute(KXMLQLCVideoMuted))
                setMuted(attrs.value(KXMLQLCVideoMuted).toString() == "1");
#ifdef QMLUI
            if (attrs.hasAttribute(KXMLQLCVideoGeometry))
            {
                QStringList slist = attrs.value(KXMLQLCVideoGeometry).toString().split(",");
                if (slist.count() == 4)
                {
                    QRect r;
                    r.setX(slist.at(0).toInt());
                    r.setY(slist.at(1).toInt());
                    r.setWidth(slist.at(2).toInt());
                    r.setHeight(slist.at(3).toInt());

                    setCustomGeometry(r);
                }
            }
            if (attrs.hasAttribute(KXMLQLCVideoRotation))
            {
                QStringList slist = attrs.value(KXMLQLCVideoRotation).toString().split(",");
                if (slist.count() == 3)
                {
                    QVector3D v;
                    v.setX(slist.at(0).toInt());
                    v.setY(slist.at(1).toInt());
                    v.setZ(slist.at(2).toInt());
                    setRotation(v);
                }
            }
            if (attrs.hasAttribute(KXMLQLCVideoZIndex))
            {
                setZIndex(attrs.value(KXMLQLCVideoZIndex).toInt());
            }
#endif

            QString path = root.readElementText();
            if (path.contains("://") == true)
                setSourceUrl(path);
            else
                setSourceUrl(m_doc->denormalizeComponentPath(path));
        }
        else if (root.name() == KXMLQLCFunctionSpeed)
        {
            loadXMLSpeed(root);
        }
        else if (root.name() == KXMLQLCFunctionRunOrder)
        {
            loadXMLRunOrder(root);
        }
        else
        {
            qWarning() << Q_FUNC_INFO << "Unknown Video tag:" << root.name();
            root.skipCurrentElement();
        }
    }

    setName(fname);

    return true;
}

void Video::postLoad()
{
}

/*********************************************************************
 * Running
 *********************************************************************/
void Video::preRun(MasterTimer* timer)
{
    // The sender name is resolved here, on the MasterTimer thread, and
    // handed over as a signal argument: the GUI-side player must never
    // read m_runtimeSenderName itself.
    emit requestPlayback(spoutSenderName());
    Function::preRun(timer);
}

void Video::setPause(bool enable)
{
    if (isRunning())
    {
        emit requestPause(enable);
        Function::setPause(enable);
    }
}

void Video::write(MasterTimer* timer, QList<Universe *> universes)
{
    Q_UNUSED(timer)
    Q_UNUSED(universes)

    incrementElapsed();
}

void Video::postRun(MasterTimer* timer, QList<Universe*> universes)
{
    emit requestStop();
    // The name only applies to the run a Show started; the next start
    // (from a Show or otherwise) sets or resolves its own.
    m_runtimeSenderName.clear();
    Function::postRun(timer, universes);
}
