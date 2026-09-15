/*
  Q Light Controller Plus
  audioeditor.h

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

#ifndef AUDIOEDITOR_H
#define AUDIOEDITOR_H

#include "functioneditor.h"

class Audio;
class ListModel;

class AudioEditor final : public FunctionEditor
{
    Q_OBJECT

    Q_PROPERTY(QString sourceFileName READ sourceFileName WRITE setSourceFileName NOTIFY sourceFileNameChanged)
    Q_PROPERTY(bool sourceManaged READ sourceManaged NOTIFY sourceFileNameChanged)
    Q_PROPERTY(QString sourceDisplayName READ sourceDisplayName NOTIFY sourceFileNameChanged)
    Q_PROPERTY(QStringList audioExtensions READ audioExtensions CONSTANT)
    Q_PROPERTY(QVariant mediaInfo READ mediaInfo NOTIFY mediaInfoChanged)
    Q_PROPERTY(bool looped READ isLooped WRITE setLooped NOTIFY loopedChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    Q_PROPERTY(int cardLineIndex READ cardLineIndex WRITE setCardLineIndex NOTIFY cardLineIndexChanged)
    Q_PROPERTY(double bpm READ bpm NOTIFY bpmChanged)
    Q_PROPERTY(double bpmConfidence READ bpmConfidence NOTIFY bpmChanged)
    Q_PROPERTY(int bpmState READ bpmState NOTIFY bpmChanged)

public:
    AudioEditor(QQuickView *view, Doc *doc, QObject *parent = 0);

    /** Set the ID of the Audio being edited */
    void setFunctionID(quint32 ID) override;

    /** Get/Set the source file name for this Audio function. The setter
     *  copies the picked file into the project's media store and points the
     *  function (and the undo entry) at the copy */
    QString sourceFileName() const;
    void setSourceFileName(QString sourceFileName);

    /** True when the source lives in the project's media store */
    bool sourceManaged() const;

    /** What the editor shows for the source: the file name alone for a
     *  managed copy, the full path for an external reference */
    QString sourceDisplayName() const;

    /** Get the supported file types that can be decoded */
    QStringList audioExtensions() const;

    /** Get the information of the currently loaded media source */
    QVariant mediaInfo() const;

    /** Get/Set looped attribute for this Audio function */
    bool isLooped();
    void setLooped(bool looped);

    /** Get/Set the Audio function volume */
    qreal volume();
    void setVolume(qreal volume);

    /** Get/Set the Audio function mute flag */
    bool muted() const;
    void setMuted(bool muted);

    /** Get/Set the audio card line used to play this Audio function */
    int cardLineIndex() const;
    void setCardLineIndex(int cardLineIndex);

    /** Get the detected BPM, confidence and analysis state of this Audio function */
    double bpm() const;
    double bpmConfidence() const;
    int bpmState() const;

    /** Manually (re-)trigger offline BPM detection */
    Q_INVOKABLE void detectBpm();

protected slots:
    /** The engine repointed the source (background copy done, store
     *  relocated on save): refresh the path shown, nothing else changed */
    void slotSourceRelinked();

signals:
    void sourceFileNameChanged(QString sourceFileName);
    void mediaInfoChanged();
    void loopedChanged();
    void volumeChanged();
    void mutedChanged();
    void cardLineIndexChanged(int cardLineIndex);
    void bpmChanged();

private:
    /** Reference of the Audio currently being edited */
    Audio *m_audio;
};

#endif // AUDIOEDITOR_H
