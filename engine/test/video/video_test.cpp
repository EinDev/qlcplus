/*
  Q Light Controller Plus - Unit test
  video_test.cpp

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

#include <QtTest>
#include <QBuffer>
#include <QXmlStreamWriter>
#include <QXmlStreamReader>
#include <QVector3D>
#include <QMetaEnum>
#include <QIcon>
#include <QRect>

#define protected public
#define private public
#include "mastertimer_stub.h"
#include "video_test.h"
#include "video.h"
#include "show.h"
#include "track.h"
#include "showfunction.h"
#include "scene.h"
#include "doc.h"
#undef private
#undef protected

static QString saveToXml(const Video &v)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    v.saveXML(&xmlWriter);
    xmlWriter.setDevice(nullptr);
    buffer.close();
    return QString::fromUtf8(buffer.data());
}

static bool loadFromXml(Video &v, const QString &xml)
{
    QByteArray data = xml.toUtf8();
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    return v.loadXML(xmlReader);
}

void Video_Test::initTestCase()
{
    m_doc = new Doc(this);
}

void Video_Test::cleanupTestCase()
{
    delete m_doc;
}

void Video_Test::cleanup()
{
    m_doc->clearContents();
}

void Video_Test::basic()
{
    Video v(m_doc);
    QCOMPARE(v.type(), Function::VideoType);
    QCOMPARE(v.name(), QString("New Video"));
    QVERIFY(v.sourceUrl().isEmpty());
    QVERIFY(v.isPicture() == false);
    QCOMPARE(v.fullscreen(), false);
    QCOMPARE(v.outputMode(), Video::Windowed);
    QCOMPARE(v.spoutSize(), QSize(0, 0));
    QVERIFY(v.runtimeSenderName().isEmpty());
}

void Video_Test::outputMode()
{
    Video v(m_doc);
    QSignalSpy modeSpy(&v, SIGNAL(outputModeChanged(int)));
    QSignalSpy sizeSpy(&v, SIGNAL(spoutSizeChanged(QSize)));

    // the bool API keeps working and maps onto the enum
    v.setFullscreen(true);
    QCOMPARE(v.outputMode(), Video::Fullscreen);
    QCOMPARE(v.fullscreen(), true);
    QCOMPARE(modeSpy.count(), 1);

    v.setOutputMode(Video::Spout);
    QCOMPARE(v.outputMode(), Video::Spout);
    QCOMPARE(v.fullscreen(), false);
    QCOMPARE(modeSpy.count(), 2);

    // leaving fullscreen from Spout mode means windowed, not "still Spout"
    v.setFullscreen(false);
    QCOMPARE(v.outputMode(), Video::Windowed);
    QCOMPARE(modeSpy.count(), 3);

    // the int overload used by editors/undo, unknown values fall back to Windowed
    v.setOutputMode(2);
    QCOMPARE(v.outputMode(), Video::Spout);
    v.setOutputMode(42);
    QCOMPARE(v.outputMode(), Video::Windowed);

    // no signal when nothing changes
    int count = modeSpy.count();
    v.setOutputMode(Video::Windowed);
    QCOMPARE(modeSpy.count(), count);

    v.setSpoutSize(QSize(1280, 720));
    QCOMPARE(v.spoutSize(), QSize(1280, 720));
    QCOMPARE(sizeSpy.count(), 1);

    // half-set or negative sizes mean "native resolution"
    v.setSpoutSize(QSize(1280, 0));
    QCOMPARE(v.spoutSize(), QSize(0, 0));
    v.setSpoutSize(QSize(-1, 720));
    QCOMPARE(v.spoutSize(), QSize(0, 0));
    QCOMPARE(sizeSpy.count(), 2);

    // copies carry the output settings
    v.setOutputMode(Video::Spout);
    v.setSpoutSize(QSize(640, 360));
    Video copy(m_doc);
    QVERIFY(copy.copyFrom(&v));
    QCOMPARE(copy.outputMode(), Video::Spout);
    QCOMPARE(copy.spoutSize(), QSize(640, 360));
}

void Video_Test::spoutSenderName()
{
    QCOMPARE(Video::spoutSenderNameForTrack("Cam 1"), QString("QLC+ Cam 1"));

    Video *v = new Video(m_doc);
    v->setName("clip.mp4");
    v->setOutputMode(Video::Spout);
    QVERIFY(m_doc->addFunction(v));

    // not on any Show track: named after the Video itself
    QCOMPARE(v->defaultSpoutSenderName(), QString("QLC+ clip.mp4"));
    QCOMPARE(v->spoutSenderName(), QString("QLC+ clip.mp4"));

    // the Show runner's runtime name wins while set
    v->setRuntimeSenderName("QLC+ Track A");
    QCOMPARE(v->runtimeSenderName(), QString("QLC+ Track A"));
    QCOMPARE(v->spoutSenderName(), QString("QLC+ Track A"));
    QCOMPARE(v->defaultSpoutSenderName(), QString("QLC+ clip.mp4"));
    v->setRuntimeSenderName(QString());
    QCOMPARE(v->spoutSenderName(), QString("QLC+ clip.mp4"));

    // preRun hands the resolved name over as the signal argument
    QSignalSpy playSpy(v, SIGNAL(requestPlayback(QString)));
    v->setRuntimeSenderName("QLC+ Track A");
    MasterTimerStub timer(m_doc, QList<Universe *>());
    v->preRun(&timer);
    QCOMPARE(playSpy.count(), 1);
    QCOMPARE(playSpy.at(0).at(0).toString(), QString("QLC+ Track A"));
    // ...and postRun clears it again
    v->postRun(&timer, QList<Universe *>());
    QVERIFY(v->runtimeSenderName().isEmpty());

    // once placed on a Show track, the default name follows the first
    // track (in Show/track ID order) that contains the Video
    Show *show = new Show(m_doc);
    QVERIFY(m_doc->addFunction(show));
    Track *empty = new Track();
    empty->setName("Lights");
    QVERIFY(show->addTrack(empty));
    Track *videoTrack = new Track();
    videoTrack->setName("Screen L");
    QVERIFY(show->addTrack(videoTrack));
    QVERIFY(videoTrack->createShowFunction(v->id()) != nullptr);
    Track *later = new Track();
    later->setName("Screen R");
    QVERIFY(show->addTrack(later));
    QVERIFY(later->createShowFunction(v->id()) != nullptr);

    QCOMPARE(v->defaultSpoutSenderName(), QString("QLC+ Screen L"));
    QCOMPARE(v->spoutSenderName(), QString("QLC+ Screen L"));
}

void Video_Test::properties()
{
    Video v(m_doc);
    v.setSourceUrl("http://example.com/pic.png");
    v.setCustomGeometry(QRect(1,2,3,4));
    v.setRotation(QVector3D(1,2,3));
    v.setZIndex(5);
    v.setScreen(2);
    v.setFullscreen(true);

    QCOMPARE(v.sourceUrl(), QString("http://example.com/pic.png"));
    QVERIFY(v.isPicture());
    QCOMPARE(v.zIndex(), 5);
    QCOMPARE(v.screen(), 2);
    QCOMPARE(v.fullscreen(), true);
    QCOMPARE(v.customGeometry(), QRect(1,2,3,4));
    QCOMPARE(v.rotation(), QVector3D(1,2,3));
}

void Video_Test::saveLoad()
{
    Video v(m_doc);
    v.setSourceUrl("http://example.com/movie.mp4");
    v.setCustomGeometry(QRect(10,20,30,40));
    v.setRotation(QVector3D(4,5,6));
    v.setZIndex(7);
    v.setScreen(3);
    v.setFullscreen(true);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    v.saveXML(&xmlWriter);
    xmlWriter.setDevice(nullptr);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Video v2(m_doc);
    QVERIFY(v2.loadXML(xmlReader));
    QCOMPARE(v2.sourceUrl(), QString("http://example.com/movie.mp4"));
#ifdef QMLUI
    QCOMPARE(v2.customGeometry(), QRect(10,20,30,40));
    QCOMPARE(v2.rotation(), QVector3D(4,5,6));
    QCOMPARE(v2.zIndex(), 7);
#endif
    QCOMPARE(v2.screen(), 3);
    QCOMPARE(v2.fullscreen(), true);
    QCOMPARE(v2.outputMode(), Video::Fullscreen);
    QCOMPARE(v2.spoutSize(), QSize(0, 0));
}

void Video_Test::saveLoadVolumeMuted()
{
    // defaults: neither attribute is written, so older projects stay untouched
    Video def(m_doc);
    QCOMPARE(def.volume(), 100.0);
    QCOMPARE(def.muted(), false);
    QString defXml = saveToXml(def);
    QVERIFY(defXml.contains("Volume=") == false);
    QVERIFY(defXml.contains("Muted=") == false);

    Video v(m_doc);
    v.setSourceUrl("http://example.com/movie.mp4");
    QSignalSpy volumeSpy(&v, SIGNAL(volumeChanged()));
    QSignalSpy mutedSpy(&v, SIGNAL(mutedChanged(bool)));
    v.setVolume(42);
    v.setMuted(true);
    QCOMPARE(v.volume(), 42.0);
    QCOMPARE(v.getAttributeValue(Video::Volume), 42.0);
    QCOMPARE(v.muted(), true);
    QCOMPARE(volumeSpy.count(), 1);
    QCOMPARE(mutedSpy.count(), 1);
    // unchanged values don't re-emit
    v.setVolume(42);
    v.setMuted(true);
    QCOMPARE(volumeSpy.count(), 1);
    QCOMPARE(mutedSpy.count(), 1);

    QString xml = saveToXml(v);
    QVERIFY(xml.contains("Volume=\"42\""));
    QVERIFY(xml.contains("Muted=\"1\""));

    Video v2(m_doc);
    QVERIFY(loadFromXml(v2, xml));
    QCOMPARE(v2.volume(), 42.0);
    QCOMPARE(v2.muted(), true);

    // copies carry both over (Function::copyFrom() doesn't copy attributes)
    Video v3(m_doc);
    QVERIFY(v3.copyFrom(&v));
    QCOMPARE(v3.volume(), 42.0);
    QCOMPARE(v3.muted(), true);
}

void Video_Test::saveLoadSpout()
{
    Video v(m_doc);
    v.setSourceUrl("http://example.com/movie.mp4");
    v.setOutputMode(Video::Spout);
    v.setSpoutSize(QSize(640, 360));

    QString xml = saveToXml(v);
    QVERIFY(xml.contains("Output=\"spout\""));
    QVERIFY(xml.contains("SpoutSize=\"640,360\""));
    // Spout is not "fullscreen": an older build must fall back to windowed
    QVERIFY(xml.contains("Fullscreen=") == false);

    Video v2(m_doc);
    QVERIFY(loadFromXml(v2, xml));
    QCOMPARE(v2.sourceUrl(), QString("http://example.com/movie.mp4"));
    QCOMPARE(v2.outputMode(), Video::Spout);
    QCOMPARE(v2.fullscreen(), false);
    QCOMPARE(v2.spoutSize(), QSize(640, 360));

    // native size is the default and is not written out
    Video v3(m_doc);
    v3.setSourceUrl("http://example.com/movie.mp4");
    v3.setOutputMode(Video::Spout);
    xml = saveToXml(v3);
    QVERIFY(xml.contains("SpoutSize=") == false);
    Video v4(m_doc);
    QVERIFY(loadFromXml(v4, xml));
    QCOMPARE(v4.outputMode(), Video::Spout);
    QCOMPARE(v4.spoutSize(), QSize(0, 0));

    // an unknown Output value is ignored, the file is still loaded
    Video v5(m_doc);
    QVERIFY(loadFromXml(v5, "<Function Type=\"Video\" Name=\"x\"><Source Output=\"hologram\">http://example.com/m.mp4</Source></Function>"));
    QCOMPARE(v5.outputMode(), Video::Windowed);
}

void Video_Test::loadLegacyFullscreen()
{
    // pre-Spout project files only know the Fullscreen flag
    Video v(m_doc);
    QVERIFY(loadFromXml(v, "<Function Type=\"Video\" Name=\"x\"><Source Screen=\"1\" Fullscreen=\"1\">http://example.com/m.mp4</Source></Function>"));
    QCOMPARE(v.outputMode(), Video::Fullscreen);
    QCOMPARE(v.fullscreen(), true);
    QCOMPARE(v.screen(), 1);

    // ...and if both are present (hand-edited file), Spout wins
    Video v2(m_doc);
    QVERIFY(loadFromXml(v2, "<Function Type=\"Video\" Name=\"x\"><Source Fullscreen=\"1\" Output=\"spout\">http://example.com/m.mp4</Source></Function>"));
    QCOMPARE(v2.outputMode(), Video::Spout);
}

void Video_Test::iconAndCapabilities()
{
    Video v(m_doc);
    QIcon icon = v.getIcon();
    Q_UNUSED(icon);

    // Qt 6 has no QMediaPlayer::supportedMimeTypes(): the built-in list is
    // what the file dialogs get
    QStringList caps = Video::getVideoCapabilities();
    QVERIFY(caps.contains("*.mp4"));
    QVERIFY(caps.contains("*.mkv"));
    QVERIFY(caps.contains("*.webm"));
    QCOMPARE(caps, Video::m_defaultVideoCaps);

    QStringList pics = Video::getPictureCapabilities();
    QVERIFY(pics.contains("*.png"));
    QVERIFY(pics.contains("*.jpg"));
    QCOMPARE(pics, Video::m_defaultPictureCaps);

    // the output mode is a registered enum (used by QML/undo through the
    // meta-object system)
    QMetaEnum modes = QMetaEnum::fromType<Video::OutputMode>();
    QCOMPARE(QString(modes.valueToKey(Video::Spout)), QString("Spout"));
    QCOMPARE(modes.keyToValue("Fullscreen"), int(Video::Fullscreen));
}

void Video_Test::createCopy()
{
    Video *v = new Video(m_doc);
    v->setSourceUrl("http://example.com/movie.mp4");
    v->setName("Movie");
    v->setOutputMode(Video::Spout);
    v->setSpoutSize(QSize(320, 240));
    v->setVolume(33);
    v->setMuted(true);
    v->setTotalDuration(5000);
    QVERIFY(m_doc->addFunction(v));
    int before = m_doc->functions().count();

    // added to the Doc: a new id, same content
    Function *copy = v->createCopy(m_doc);
    QVERIFY(copy != NULL);
    QVERIFY(copy != v);
    QCOMPARE(copy->type(), Function::VideoType);
    QVERIFY(copy->id() != v->id());
    QVERIFY(m_doc->function(copy->id()) == copy);
    QCOMPARE(m_doc->functions().count(), before + 1);
    Video *videoCopy = qobject_cast<Video *>(copy);
    QVERIFY(videoCopy != NULL);
    QCOMPARE(videoCopy->sourceUrl(), QString("http://example.com/movie.mp4"));
    QCOMPARE(videoCopy->name(), QString("Movie"));
    QCOMPARE(videoCopy->outputMode(), Video::Spout);
    QCOMPARE(videoCopy->spoutSize(), QSize(320, 240));
    QCOMPARE(videoCopy->volume(), 33.0);
    QCOMPARE(videoCopy->muted(), true);
    QCOMPARE(videoCopy->totalDuration(), 5000u);

    // not added: the caller owns it
    Function *loose = v->createCopy(m_doc, false);
    QVERIFY(loose != NULL);
    QCOMPARE(loose->id(), Function::invalidId());
    QCOMPARE(m_doc->functions().count(), before + 1);
    delete loose;

    // only a Video can be copied into a Video
    Scene scene(m_doc);
    Video other(m_doc);
    QVERIFY(other.copyFrom(&scene) == false);
}

void Video_Test::moreProperties()
{
    Video v(m_doc);
    v.setID(9);

    QSignalSpy durationSpy(&v, SIGNAL(totalTimeChanged(qint64)));
    QSignalSpy metaSpy(&v, SIGNAL(metaDataChanged(QString,QVariant)));
    QSignalSpy geometrySpy(&v, SIGNAL(customGeometryChanged(QRect)));
    QSignalSpy rotationSpy(&v, SIGNAL(rotationChanged(QVector3D)));
    QSignalSpy sourceSpy(&v, SIGNAL(sourceChanged(QString)));
    QSignalSpy brightnessSpy(&v, SIGNAL(requestBrightnessVolumeAdjust(qreal)));
    QSignalSpy intensitySpy(&v, SIGNAL(intensityChanged()));

    QCOMPARE(v.totalDuration(), 0u);
    v.setTotalDuration(0);
    QCOMPARE(durationSpy.count(), 0);
    v.setTotalDuration(12345);
    QCOMPARE(v.totalDuration(), 12345u);
    QCOMPARE(durationSpy.count(), 1);
    QCOMPARE(durationSpy.at(0).at(0).toLongLong(), qint64(12345));

    QCOMPARE(v.resolution(), QSize(0, 0));
    v.setResolution(QSize(1920, 1080));
    QCOMPARE(v.resolution(), QSize(1920, 1080));
    QCOMPARE(metaSpy.count(), 1);
    QCOMPARE(metaSpy.at(0).at(0).toString(), QString("Resolution"));
    QCOMPARE(metaSpy.at(0).at(1).toSize(), QSize(1920, 1080));

    v.setAudioCodec("aac");
    v.setVideoCodec("h264");
    QCOMPARE(v.audioCodec(), QString("aac"));
    QCOMPARE(v.videoCodec(), QString("h264"));
    QCOMPARE(metaSpy.count(), 3);
    QCOMPARE(metaSpy.at(1).at(0).toString(), QString("AudioCodec"));
    QCOMPARE(metaSpy.at(1).at(1).toString(), QString("aac"));
    QCOMPARE(metaSpy.at(2).at(0).toString(), QString("VideoCodec"));
    QCOMPARE(metaSpy.at(2).at(1).toString(), QString("h264"));

    // unchanged geometry/rotation do not re-emit
    v.setCustomGeometry(QRect(1, 2, 3, 4));
    v.setCustomGeometry(QRect(1, 2, 3, 4));
    QCOMPARE(geometrySpy.count(), 1);
    v.setRotation(QVector3D(1, 2, 3));
    v.setRotation(QVector3D(1, 2, 3));
    QCOMPARE(rotationSpy.count(), 1);

    // relinking to the current source is a no-op
    v.setSourceUrl("http://example.com/a.mp4");
    QCOMPARE(sourceSpy.count(), 1);
    v.relinkSource("http://example.com/a.mp4");
    QCOMPARE(sourceSpy.count(), 1);

    // the int overload maps 1 onto Fullscreen
    v.setOutputMode(int(Video::Fullscreen));
    QCOMPARE(v.outputMode(), Video::Fullscreen);
    QCOMPARE(v.fullscreen(), true);

    // intensity is the generic Function attribute, adjusted through the
    // player-facing signals
    QCOMPARE(v.intensity(), 1.0);
    QCOMPARE(v.adjustAttribute(0.5, Video::Intensity), int(Video::Intensity));
    QCOMPARE(v.intensity(), 0.5);
    QCOMPARE(brightnessSpy.count(), 1);
    QCOMPARE(brightnessSpy.at(0).at(0).toReal(), 0.5);
    QCOMPARE(intensitySpy.count(), 1);
}

void Video_Test::runningState()
{
    Video v(m_doc);
    v.setID(11);
    v.setSourceUrl("http://example.com/a.mp4");
    MasterTimerStub timer(m_doc, QList<Universe *>());

    QSignalSpy seekSpy(&v, SIGNAL(requestSeek(qint64)));
    QSignalSpy pauseSpy(&v, SIGNAL(requestPause(bool)));

    // not running: stop/seek/pause requests are ignored
    QVERIFY(v.isRunning() == false);
    v.stopFromUI();
    QVERIFY(v.stopped() == false);
    v.seekTo(500);
    QCOMPARE(seekSpy.count(), 0);
    v.setPause(true);
    QCOMPARE(pauseSpy.count(), 0);
    QVERIFY(v.isPaused() == false);

    // running (the state the MasterTimer thread sets in preRun)
    v.m_running = true;
    v.seekTo(1234);
    QCOMPARE(seekSpy.count(), 1);
    QCOMPARE(seekSpy.at(0).at(0).toLongLong(), qint64(1234));

    v.setPause(true);
    QCOMPARE(pauseSpy.count(), 1);
    QCOMPARE(pauseSpy.at(0).at(0).toBool(), true);
    QVERIFY(v.isPaused());
    v.setPause(false);
    QCOMPARE(pauseSpy.count(), 2);
    QVERIFY(v.isPaused() == false);

    // write() only advances the elapsed time: the player does the work
    QCOMPARE(v.elapsed(), 0u);
    v.write(&timer, QList<Universe *>());
    QCOMPARE(v.elapsed(), quint32(MasterTimer::tick()));
    v.write(&timer, QList<Universe *>());
    QCOMPARE(v.elapsed(), quint32(2 * MasterTimer::tick()));

    // EndOfMedia from the player stops the run
    v.stopFromUI();
    QVERIFY(v.stopped());

    v.postRun(&timer, QList<Universe *>());
    v.m_running = false;
}

void Video_Test::loadInvalid()
{
    Video v1(m_doc);
    QVERIFY(loadFromXml(v1, "<Foo Type=\"Video\"/>") == false);

    Video v2(m_doc);
    QVERIFY(loadFromXml(v2, "<Function Type=\"Scene\" Name=\"x\"/>") == false);

    // Fullscreen="0" is an explicit windowed mode; unknown tags are skipped
    Video v3(m_doc);
    v3.setFullscreen(true);
    QVERIFY(loadFromXml(v3, "<Function Type=\"Video\" Name=\"x\">"
                            "<Bogus><Nested/></Bogus>"
                            "<Source Fullscreen=\"0\">http://example.com/m.mp4</Source>"
                            "</Function>"));
    QCOMPARE(v3.outputMode(), Video::Windowed);
    QCOMPARE(v3.fullscreen(), false);
    QCOMPARE(v3.sourceUrl(), QString("http://example.com/m.mp4"));
}

QTEST_MAIN(Video_Test)
