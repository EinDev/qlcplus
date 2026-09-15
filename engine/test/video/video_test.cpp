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
#include <QRect>

#define protected public
#define private public
#include "mastertimer_stub.h"
#include "video_test.h"
#include "video.h"
#include "show.h"
#include "track.h"
#include "showfunction.h"
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

QTEST_MAIN(Video_Test)
