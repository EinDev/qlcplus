/*
  Q Light Controller Plus
  video.h

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

#ifndef VIDEO_H
#define VIDEO_H

#include <QColor>
#include <QRect>
#include <QVector3D>
#include <QVariant>

#include "function.h"

class QXmlStreamReader;

/** @addtogroup engine_functions Functions
 * @{
 */

class Video final : public Function
{
    Q_OBJECT
    Q_DISABLE_COPY(Video)

    Q_PROPERTY(QString sourceUrl READ sourceUrl WRITE setSourceUrl NOTIFY sourceChanged)
    Q_PROPERTY(qreal intensity READ intensity NOTIFY intensityChanged)
    Q_PROPERTY(QRect customGeometry READ customGeometry WRITE setCustomGeometry NOTIFY customGeometryChanged)
    Q_PROPERTY(QVector3D rotation READ rotation WRITE setRotation NOTIFY rotationChanged)
    Q_PROPERTY(int zIndex READ zIndex WRITE setZIndex NOTIFY zIndexChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen WRITE setFullscreen)
    Q_PROPERTY(int outputMode READ outputMode WRITE setOutputMode NOTIFY outputModeChanged)
    Q_PROPERTY(QSize spoutSize READ spoutSize WRITE setSpoutSize NOTIFY spoutSizeChanged)

    /*********************************************************************
     * Initialization
     *********************************************************************/
public:
    enum VideoAttr
    {
        Intensity = Function::Intensity,
        Volume,
        XRotation,
        YRotation,
        ZRotation,
        XPosition,
        YPosition,
        WidthScale,
        HeightScale
    };

    /**
     * Where the rendered content goes when the Video runs.
     *
     * Windowed and Fullscreen render into an on-screen window on screen().
     * Spout renders into a named Spout sender (Windows only, see
     * spoutSenderName()) and opens no window at all; screen(),
     * customGeometry(), rotation() and zIndex() are ignored in that mode.
     */
    enum OutputMode
    {
        Windowed = 0,
        Fullscreen,
        Spout
    };
    Q_ENUM(OutputMode)

    Video(Doc* doc);
    virtual ~Video();

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

    /** Stop the function from the UI (used on EndOfMedia) */
    Q_INVOKABLE void stopFromUI();

    /**
     * Move the playback position of a running Video to $ms into the media
     * without restarting it: the player keeps its window/Spout sender and
     * just seeks. Used by the Show runner while scrubbing, where a restart
     * per cursor move would tear the window down and up again at up to
     * 20 Hz. Safe to call from the MasterTimer thread: it only emits
     * requestSeek(), which the GUI-side player consumes. No-op unless the
     * Video is running.
     */
    void seekTo(quint32 ms);

    /*********************************************************************
     * Capabilities
     *********************************************************************/
public:
    /** Get the list of the extensions supported by the video decoding system */
    static QStringList getVideoCapabilities();

    /** Get the list of the extensions supported for picture rendering */
    static QStringList getPictureCapabilities();

    static const QStringList m_defaultVideoCaps;
    static const QStringList m_defaultPictureCaps;

    /*********************************************************************
     * Properties
     *********************************************************************/
public:
    /** @reimpl */
    void setTotalDuration(quint32 duration) override;

    /** @reimpl */
    quint32 totalDuration() override;

    /** Get/Set the video resolution as a QSize variable */
    QSize resolution() const;
    void setResolution(QSize size);

    /** Get/Set the video custom geometry as a QRect variable */
    QRect customGeometry() const;
    void setCustomGeometry(QRect rect);

    /** Get/Set the video XYZ rotation as a QVector3D variable */
    QVector3D rotation() const;
    void setRotation(QVector3D rotation);

    /** Get/Set the video Z-Index used for layering */
    int zIndex() const;
    void setZIndex(int idx);

    /** Get/Set the audio codec for this Video Function */
    QString audioCodec() const;
    void setAudioCodec(QString codec);

    /** Get/Set the video codec for this Video Function */
    QString videoCodec() const;
    void setVideoCodec(QString codec);

    /** Get/Set the source URL used by this Video object */
    QString sourceUrl() const;
    bool setSourceUrl(QString filename);

    /** Return if the loaded source is a picture */
    bool isPicture() const;

    /** Get/Set the screen index where to render the video */
    int screen() const;
    void setScreen(int index);

    /** Get/Set the video to be rendered in windowed or fullscreen mode.
     *  Kept for API compatibility: fullscreen() is true only in Fullscreen
     *  mode, setFullscreen(true/false) selects Fullscreen/Windowed. */
    bool fullscreen() const;
    void setFullscreen(bool enable);

    /** Get/Set the output mode (see OutputMode) */
    OutputMode outputMode() const;
    void setOutputMode(OutputMode mode);
    void setOutputMode(int mode);

    /** Get/Set the Spout sender size. An empty size (the default, 0x0)
     *  means "use the native resolution of the source". */
    QSize spoutSize() const;
    void setSpoutSize(QSize size);

    /** Get/Set the runtime Spout sender name. Set by the Show runner right
     *  before start() to spoutSenderNameForTrack(track name) and cleared
     *  again in postRun(). Empty when the Video is not running from a Show. */
    QString runtimeSenderName() const;
    void setRuntimeSenderName(const QString &name);

    /** The Spout sender name to publish under when this Video is started
     *  right now: runtimeSenderName() if set, defaultSpoutSenderName()
     *  otherwise. Emitted as the argument of requestPlayback(), so the
     *  GUI thread never has to read it across threads. */
    QString spoutSenderName() const;

    /**
     * The Spout sender name this Video resolves to when no Show is
     * running it. This is the name used to create the sender at document
     * load (so receivers like OBS can pick it before anything plays) and
     * when the Video is started from the Function Manager or the Virtual
     * Console. The rule is:
     *   - "QLC+ <track name>" of the first Show track (Shows and tracks in
     *     ID order) that contains this Video, so it matches the name the
     *     Show runner will use for that track, else
     *   - "QLC+ <Video name>" if no Show track contains it.
     */
    QString defaultSpoutSenderName() const;

    /** The sender name used for a Video played from a Show track */
    static QString spoutSenderNameForTrack(const QString &trackName);

    /** Get the current Video intensity */
    qreal intensity() const;

    /** @reimp */
    int adjustAttribute(qreal fraction, int attributeId) override;

signals:
    void sourceChanged(QString url);
    void intensityChanged();
    void customGeometryChanged(QRect rect);
    void rotationChanged(QVector3D rotation);
    void zIndexChanged(int index);
    void totalTimeChanged(qint64);
    void metaDataChanged(QString key, QVariant data);
    void outputModeChanged(int mode);
    void spoutSizeChanged(QSize size);
    /** Emitted from preRun() (MasterTimer thread) with the sender name to
     *  use for this run (see spoutSenderName()). The default argument keeps
     *  parameterless SIGNAL(requestPlayback()) connections valid. */
    void requestPlayback(QString spoutSenderName = QString());
    void requestPause(bool enable);
    /** Emitted by seekTo(): the player should jump to $ms into the media */
    void requestSeek(qint64 ms);
    void requestStop();
    void requestBrightnessVolumeAdjust(qreal value);

private:
    /** URL of the video media source */
    QString m_sourceUrl;
    /** Flag that indicates if the loaded source is a picture (or a video) */
    bool m_isPicture;
    /** Duration of the video content */
    qint64 m_videoDuration;
    /** The audio and video codec as strings */
    QString m_audioCodec, m_videoCodec;
    /** Resolution of the video content */
    QSize m_resolution;
    /** If set, specifies the custom geometry (position and size)
     *  to be used when rendering the video */
    QRect m_customGeometry;
    /** The video XYZ rotation as a 3D vector */
    QVector3D m_rotation;
    /** The video Z-Index */
    int m_zIndex;
    /** Index of the screen where to render the video */
    int m_screen;
    /** Where the content is rendered (window, fullscreen or Spout) */
    OutputMode m_outputMode;
    /** Spout sender size, 0x0 = native resolution */
    QSize m_spoutSize;
    /** Spout sender name set by the Show runner for the current run */
    QString m_runtimeSenderName;

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
