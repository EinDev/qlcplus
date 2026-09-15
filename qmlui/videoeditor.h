/*
  Q Light Controller Plus
  videoeditor.h

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

#ifndef VIDEOEDITOR_H
#define VIDEOEDITOR_H

#include <QMediaPlayer>

#include "functioneditor.h"

class Video;

class VideoEditor final : public FunctionEditor
{
    Q_OBJECT

    Q_PROPERTY(QString sourceFileName READ sourceFileName WRITE setSourceFileName NOTIFY sourceFileNameChanged)
    Q_PROPERTY(bool sourceManaged READ sourceManaged NOTIFY sourceFileNameChanged)
    Q_PROPERTY(QString sourceDisplayName READ sourceDisplayName NOTIFY sourceFileNameChanged)
    /* getter/notify named apart from the property: "originChanged" is what
     * Qt would expect as the NOTIFY of a property called "origin" */
    Q_PROPERTY(QString originPath READ originPath NOTIFY originStateChanged)
    Q_PROPERTY(bool originAvailable READ originAvailable NOTIFY originStateChanged)
    Q_PROPERTY(bool originChanged READ isOriginChanged NOTIFY originStateChanged)
    Q_PROPERTY(QString reloadTooltip READ reloadTooltip NOTIFY originStateChanged)
    Q_PROPERTY(QStringList videoExtensions READ videoExtensions CONSTANT)
    Q_PROPERTY(QStringList pictureExtensions READ pictureExtensions CONSTANT)
    Q_PROPERTY(QVariant mediaInfo READ mediaInfo NOTIFY mediaInfoChanged)
    Q_PROPERTY(QStringList screenList READ screenList CONSTANT)
    Q_PROPERTY(int screenIndex READ screenIndex WRITE setScreenIndex NOTIFY screenIndexChanged)
    Q_PROPERTY(bool fullscreen READ isFullscreen WRITE setFullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(int outputMode READ outputMode WRITE setOutputMode NOTIFY outputModeChanged)
    Q_PROPERTY(bool spoutAvailable READ spoutAvailable CONSTANT)
    Q_PROPERTY(QSize spoutSize READ spoutSize WRITE setSpoutSize NOTIFY spoutSizeChanged)
    Q_PROPERTY(QString spoutSenderName READ spoutSenderName NOTIFY spoutSenderNameChanged)
    Q_PROPERTY(bool looped READ isLooped WRITE setLooped NOTIFY loopedChanged)
    Q_PROPERTY(bool hasCustomGeometry READ hasCustomGeometry CONSTANT)
    Q_PROPERTY(QRect customGeometry READ customGeometry WRITE setCustomGeometry NOTIFY customGeometryChanged)
    Q_PROPERTY(QVector3D rotation READ rotation WRITE setRotation NOTIFY rotationChanged)
    Q_PROPERTY(int layer READ layer WRITE setLayer NOTIFY layerChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)

public:
    VideoEditor(QQuickView *view, Doc *doc, QObject *parent = nullptr);
    ~VideoEditor();

    /** @reimp */
    void setFunctionID(quint32 ID) override;

    /** Get/Set the source file name for this Video function. A local file
     *  is copied into the project's media store and the function (and the
     *  undo entry) point at the copy; a URL is kept as is */
    QString sourceFileName() const;
    void setSourceFileName(QString sourceFileName);

    /** True when the source lives in the project's media store */
    bool sourceManaged() const;

    /** What the editor shows for the source: the file name alone for a
     *  managed copy, the full path or URL otherwise */
    QString sourceDisplayName() const;

    /** Absolute path a managed copy was imported from, empty when unknown */
    QString originPath() const;

    /** True when the Reload button can do something: the origin of a
     *  managed copy exists, or the source is an external local file */
    bool originAvailable() const;

    /** True when the origin file differs from the managed copy */
    bool isOriginChanged() const;

    /** Tooltip of the Reload button, explaining what it would do */
    QString reloadTooltip() const;

    /** Reload the file from disk: a managed copy is re-imported from its
     *  origin (undoable, the previous copy stays on disk), an external
     *  reference is re-probed in place (resolution, duration) */
    Q_INVOKABLE void reloadSource();

    /** Get the supported video file types that can be decoded */
    QStringList videoExtensions() const;

    /** Get the supported picture file types that can be rendered */
    QStringList pictureExtensions() const;

    /** Get the information of the currently loaded media source */
    QVariant mediaInfo() const;

    QStringList screenList() const;

    /** Get/Set the screen index of this Video function */
    int screenIndex() const;
    void setScreenIndex(int screenIndex);

    /** Get/Set the fullscreen flag of this Video function */
    bool isFullscreen() const;
    void setFullscreen(bool fullscreen);

    /** Get/Set the output mode (Video::OutputMode) of this Video function */
    int outputMode() const;
    void setOutputMode(int mode);

    /** true if this build can output to Spout (Windows, QLC_SPOUT) */
    bool spoutAvailable() const;

    /** Get/Set the Spout sender size of this Video function (0x0 = native) */
    QSize spoutSize() const;
    void setSpoutSize(QSize size);

    /** The Spout sender name this Video resolves to outside a running Show
     *  (see Video::defaultSpoutSenderName()) */
    QString spoutSenderName() const;

    /** Get/Set looped attribute for this Video function */
    bool isLooped() const;
    void setLooped(bool looped);

    bool hasCustomGeometry() const;

    /** Get/Set the custom geometry for this Video function */
    QRect customGeometry() const;
    void setCustomGeometry(QRect customGeometry);

    /** Get/Set a rotation transformation */
    QVector3D rotation() const;
    void setRotation(QVector3D rotation);

    /** Get/Set the layer of this Video function */
    int layer() const;
    void setLayer(int index);

    /** Get/Set the Video function volume (0-100) */
    qreal volume() const;
    void setVolume(qreal volume);

    /** Get/Set the Video function mute flag */
    bool muted() const;
    void setMuted(bool muted);

private:
    void detectMedia();

protected slots:
    void slotDurationChanged(qint64 duration);
    void slotMetaDataChanged();

    /** The engine repointed the source (background copy done, store
     *  relocated on save): refresh the path shown, nothing else changed */
    void slotSourceRelinked(QString source);

signals:
    void sourceFileNameChanged(QString sourceFileName);
    void originStateChanged();
    void mediaInfoChanged();
    void screenIndexChanged(int screenIndex);
    void fullscreenChanged(bool fullscreen);
    void outputModeChanged(int mode);
    void spoutSizeChanged(QSize size);
    void spoutSenderNameChanged();
    void loopedChanged();
    void customGeometryChanged(QRect customGeometry);
    void rotationChanged(QVector3D rotation);
    void layerChanged(int index);
    void volumeChanged();
    void mutedChanged();

private:
    /** Reference of the Video currently being edited */
    Video *m_video;

    /** A map representing the Video metadata */
    QVariantMap infoMap;

    /** temporary player to retrieve metadata information */
    QMediaPlayer *m_mediaPlayer;
};

#endif // VIDEOEDITOR_H
