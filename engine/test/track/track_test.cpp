/*
  Q Light Controller Plus - Unit test
  track_test.cpp

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
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "track_test.h"
#include "sequence.h"
#include "track.h"

void Track_Test::initTestCase()
{
    m_doc = new Doc(this);
    m_showFunc = new ShowFunction(123, this);
}

void Track_Test::cleanupTestCase()
{
    delete m_showFunc;
}

void Track_Test::defaults()
{
    Track t;

    // check defaults
    QVERIFY(t.id() == Track::invalidId());
    QVERIFY(t.getSceneID() == Scene::invalidId());
    QVERIFY(t.showId() == Function::invalidId());
    QCOMPARE(t.name(), "New Track");

    // set & check base params
    t.setId(321);
    t.setShowId(567);
    t.setSceneID(890);
    t.setName("Foo Track");

    QVERIFY(t.id() == 321);
    QVERIFY(t.showId() == 567);
    QVERIFY(t.getSceneID() == 890);
    QCOMPARE(t.name(), "Foo Track");

    Track t2(123);
    QVERIFY(t2.getSceneID() == 123);
}

void Track_Test::mute()
{
    Track t;
    QCOMPARE(t.isMute(), false);

    t.setMute(true);
    QCOMPARE(t.isMute(), true);

    t.setMute(false);
    QCOMPARE(t.isMute(), false);
}

void Track_Test::spoutSize()
{
    Track t;
    t.setId(42);
    QSignalSpy sizeSpy(&t, SIGNAL(spoutSizeChanged(QSize)));
    QSignalSpy changedSpy(&t, SIGNAL(changed(quint32)));

    // not set by default
    QCOMPARE(t.spoutSize(), QSize(0, 0));
    QVERIFY(t.spoutSize().isEmpty());

    t.setSpoutSize(QSize(1920, 1080));
    QCOMPARE(t.spoutSize(), QSize(1920, 1080));
    QCOMPARE(sizeSpy.count(), 1);
    QCOMPARE(sizeSpy.at(0).at(0).toSize(), QSize(1920, 1080));
    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(changedSpy.at(0).at(0).toUInt(), 42u);

    // same size again: no signals
    t.setSpoutSize(QSize(1920, 1080));
    QCOMPARE(sizeSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 1);

    // anything unusable resets to "not set", stored uniformly as 0x0
    t.setSpoutSize(QSize(640, 0));
    QCOMPARE(t.spoutSize(), QSize(0, 0));
    QCOMPARE(sizeSpy.count(), 2);

    t.setSpoutSize(QSize(1280, 720));
    t.setSpoutSize(QSize());
    QCOMPARE(t.spoutSize(), QSize(0, 0));
    QCOMPARE(sizeSpy.count(), 4);

    // already unset: no signal
    t.setSpoutSize(QSize(-1, 5));
    QCOMPARE(sizeSpy.count(), 4);
}

void Track_Test::showFunctions()
{
    Track t;
    QCOMPARE(t.showFunctions().count(), 0);

    QVERIFY(t.createShowFunction(123) != nullptr);
    QCOMPARE(t.showFunctions().count(), 1);

    QVERIFY(t.addShowFunction(nullptr) == false);
    QVERIFY(t.addShowFunction(m_showFunc) == false);
    QCOMPARE(t.showFunctions().count(), 1);

    m_showFunc->setFunctionID(123);
    QVERIFY(t.addShowFunction(m_showFunc) == true);
    QCOMPARE(t.showFunctions().count(), 2);

    QVERIFY(t.removeShowFunction(nullptr) == false);
    QVERIFY(t.removeShowFunction(m_showFunc, true) == true);
    QCOMPARE(t.showFunctions().count(), 1);
}

void Track_Test::load()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Track");
    xmlWriter.writeAttribute("ID", "123");
    xmlWriter.writeAttribute("SceneID", "456");
    xmlWriter.writeAttribute("Name", "Sequence Cue");
    xmlWriter.writeAttribute("isMute", "1");

    xmlWriter.writeStartElement("ShowFunction");
    xmlWriter.writeAttribute("ID", "789");
    xmlWriter.writeAttribute("Duration", "112233");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Track t;
    QVERIFY(t.loadXML(xmlReader) == true);

    QVERIFY(t.id() == 123);
    QVERIFY(t.getSceneID() == 456);
    QCOMPARE(t.name(), "Sequence Cue");
    QCOMPARE(t.isMute(), true);

    QVERIFY(t.showFunctions().count() == 1);
    ShowFunction *sf = t.showFunctions().first();

    QVERIFY(sf->functionID() == 789);
    QVERIFY(sf->duration() == 112233);

    // no SpoutSize attribute: stays unset
    QCOMPARE(t.spoutSize(), QSize(0, 0));
}

void Track_Test::loadSpoutSize()
{
    // a valid "w,h" attribute, plus the malformed variants that must be
    // ignored (leaving the size unset) without failing the whole load
    QStringList attrs;
    attrs << "1280,720" << "1280" << "a,b" << "0,720" << "1280,-1" << "";
    QList<QSize> expected;
    expected << QSize(1280, 720) << QSize(0, 0) << QSize(0, 0) << QSize(0, 0) << QSize(0, 0) << QSize(0, 0);

    for (int i = 0; i < attrs.count(); i++)
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);

        xmlWriter.writeStartElement("Track");
        xmlWriter.writeAttribute("ID", "5");
        xmlWriter.writeAttribute("Name", "Video");
        xmlWriter.writeAttribute("isMute", "0");
        xmlWriter.writeAttribute("SpoutSize", attrs.at(i));
        xmlWriter.writeEndElement();
        xmlWriter.writeEndDocument();
        xmlWriter.setDevice(NULL);
        buffer.close();

        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();

        Track t;
        QVERIFY2(t.loadXML(xmlReader) == true, qPrintable(attrs.at(i)));
        QCOMPARE(t.spoutSize(), expected.at(i));
    }
}

void Track_Test::functions()
{
    Track t;
    t.setId(321);
    t.setShowId(567);
    t.setName("Foo Track");

    Scene *s = new Scene(m_doc);
    m_doc->addFunction(s, 10);

    Sequence *sq = new Sequence(m_doc);
    sq->setBoundSceneID(890);
    m_doc->addFunction(sq, 20);

    // invalid ShowFunction
    ShowFunction *sf1 = new ShowFunction(123);
    sf1->setFunctionID(666);

    // Valid ShowFunction
    ShowFunction *sf2 = new ShowFunction(456);
    sf2->setFunctionID(s->id());
    QVERIFY(sf2->color() == QColor());

    ShowFunction *sf3 = new ShowFunction(789);
    sf3->setFunctionID(sq->id());

    t.addShowFunction(sf1);
    t.addShowFunction(sf2);
    t.addShowFunction(sf3);

    QVERIFY(t.showFunctions().count() == 3);
    QVERIFY(t.showFunction(111) == NULL);
    QVERIFY(t.showFunction(123) == sf1);
    QVERIFY(t.showFunction(456) == sf2);
    QVERIFY(t.showFunction(789) == sf3);
    QVERIFY(t.postLoad(m_doc) == true);

    // invalid ShowFunction has been removed
    QVERIFY(t.showFunctions().count() == 2);
    // invalid color has been fixed
    QVERIFY(sf2->color() == QColor(100, 100, 100));
    // check SceneID set from sequence
    //QVERIFY(t.getSceneID() == 890);

    //QVERIFY(t.contains(m_doc, 890) == true);
    QVERIFY(t.contains(m_doc, 666) == false);
    QVERIFY(t.contains(m_doc, 10) == true);
    QVERIFY(t.contains(m_doc, 20) == true);

    QVERIFY(t.components().count() == 2);
    QVERIFY(t.components().at(0) == 10);
    QVERIFY(t.components().at(1) == 20);
}

void Track_Test::save()
{
    Track t(321);
    t.setId(654);
    t.setName("Audio Cue");
    t.setMute(true);

    m_showFunc = new ShowFunction(123, this);
    m_showFunc->setFunctionID(987);

    t.addShowFunction(m_showFunc);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(t.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Track");

    QVERIFY(xmlReader.attributes().value("ID").toString() == "654");
    QVERIFY(xmlReader.attributes().value("SceneID").toString() == "321");
    QVERIFY(xmlReader.attributes().value("Name").toString() == "Audio Cue");
    QVERIFY(xmlReader.attributes().value("isMute").toString() == "1");
    // unset: the attribute is not written at all
    QVERIFY(xmlReader.attributes().hasAttribute("SpoutSize") == false);

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "ShowFunction");
    QVERIFY(xmlReader.attributes().value("ID").toString() == "987");
}

void Track_Test::saveSpoutSize()
{
    Track t;
    t.setId(7);
    t.setName("Video");
    t.setSpoutSize(QSize(1920, 1080));

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(t.saveXML(&xmlWriter) == true);
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Track");
    QCOMPARE(xmlReader.attributes().value("SpoutSize").toString(), QString("1920,1080"));

    // and it round-trips through loadXML
    Track t2;
    QVERIFY(t2.loadXML(xmlReader) == true);
    QCOMPARE(t2.id(), 7u);
    QCOMPARE(t2.name(), QString("Video"));
    QCOMPARE(t2.spoutSize(), QSize(1920, 1080));
}


QTEST_APPLESS_MAIN(Track_Test)
