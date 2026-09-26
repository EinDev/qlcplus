/*
  Q Light Controller Plus - Unit test
  scene_test.cpp

  Copyright (c) Heikki Junnila
                Massimo Callegari

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

#define protected public
#define private public
#include "mastertimer_stub.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "genericfader.h"
#include "fixturegroup.h"
#include "scene_test.h"
#include "qlcchannel.h"
#include "qlcpalette.h"
#include "universe.h"
#include "function.h"
#include "fixture.h"
#include "qlcfile.h"
#include "scene.h"
#include "chaser.h"
#include "doc.h"
#include "bus.h"
#undef private
#undef protected

#include "../common/resource_paths.h"

void Scene_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(m_doc->fixtureDefCache()->loadMap(dir) == true);
}

void Scene_Test::cleanupTestCase()
{
    delete m_doc;
}

void Scene_Test::init()
{
}

void Scene_Test::cleanup()
{
    m_doc->clearContents();
}

void Scene_Test::initial()
{
    Scene s(m_doc);
    QVERIFY(s.type() == Function::SceneType);
    QVERIFY(s.name() == "New Scene");
    QVERIFY(s.values().size() == 0);
    QVERIFY(s.id() == Function::invalidId());
    QCOMPARE(s.m_legacyFadeBus, Bus::invalid());
}

void Scene_Test::values()
{
    Scene s(m_doc);
    QVERIFY(s.values().size() == 0);

    /* Value 3 to fixture 1's channel number 2 */
    s.setValue(1, 2, 3);
    QVERIFY(s.values().size() == 1);
    QVERIFY(s.values().at(0).fxi == 1);
    QVERIFY(s.values().at(0).channel == 2);
    QVERIFY(s.values().at(0).value == 3);

    /* Value 6 to fixture 4's channel number 5 */
    SceneValue scv(4, 5, 6);
    s.setValue(scv);
    QVERIFY(s.values().size() == 2);
    QVERIFY(s.values().at(0).fxi == 1);
    QVERIFY(s.values().at(0).channel == 2);
    QVERIFY(s.values().at(0).value == 3);
    QVERIFY(s.values().at(1).fxi == 4);
    QVERIFY(s.values().at(1).channel == 5);
    QVERIFY(s.values().at(1).value == 6);

    /* Replace previous value 3 with 15 for fixture 1's channel number 2 */
    s.setValue(1, 2, 15);
    QVERIFY(s.values().size() == 2);
    QVERIFY(s.values().at(0).fxi == 1);
    QVERIFY(s.values().at(0).channel == 2);
    QVERIFY(s.values().at(0).value == 15);
    QVERIFY(s.values().at(1).fxi == 4);
    QVERIFY(s.values().at(1).channel == 5);
    QVERIFY(s.values().at(1).value == 6);

    QVERIFY(s.value(1, 2) == 15);
    QVERIFY(s.value(3, 2) == 0); // No such channel
    QVERIFY(s.value(4, 5) == 6);

    /* No channel 5 for fixture 1 in the scene, unset shouldn't happen */
    s.unsetValue(1, 5);
    QVERIFY(s.values().size() == 2);
    QVERIFY(s.values().at(0).fxi == 1);
    QVERIFY(s.values().at(0).channel == 2);
    QVERIFY(s.values().at(0).value == 15);
    QVERIFY(s.values().at(1).fxi == 4);
    QVERIFY(s.values().at(1).channel == 5);
    QVERIFY(s.values().at(1).value == 6);

    /* Remove fixture 1's channel 2 from the scene */
    s.unsetValue(1, 2);
    QVERIFY(s.values().size() == 1);
    QVERIFY(s.values().at(0).fxi == 4);
    QVERIFY(s.values().at(0).channel == 5);
    QVERIFY(s.values().at(0).value == 6);

    /* No fixture 1 anymore */
    s.unsetValue(1, 2);
    QVERIFY(s.values().size() == 1);
    QVERIFY(s.values().at(0).fxi == 4);
    QVERIFY(s.values().at(0).channel == 5);
    QVERIFY(s.values().at(0).value == 6);

    /* Remove fixture 4's channel 5 from the scene */
    s.unsetValue(4, 5);
    QVERIFY(s.values().size() == 0);

    /* Unset unknown fixture value */
    s.unsetValue(42, 123);
    QVERIFY(s.values().size() == 0);

    /* set 3 fixtures, 4 values */
    s.setValue(1, 1, 255);
    s.setValue(2, 2, 255);
    s.setValue(4, 3, 255);
    s.setValue(1, 4, 255);
    QVERIFY(s.values().size() == 4);

    /* check 3 fixture IDs are set */
    QVERIFY(s.components().size() == 3);

    QVERIFY(s.checkValue(SceneValue(1, 1)) == true);
    QVERIFY(s.checkValue(SceneValue(7, 8)) == false);

    s.clear();
    QVERIFY(s.values().size() == 0);
}

void Scene_Test::colorValue()
{
    Doc* doc = new Doc(this);
    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));

    QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Generic", "Generic RGB");
    QVERIFY(def != NULL);

    QLCFixtureMode* mode = def->mode("Dimmer RGB");
    QVERIFY(mode != NULL);

    Fixture* fxi1 = new Fixture(doc);
    fxi1->setFixtureDefinition(def, mode);
    QCOMPARE(fxi1->channels(), quint32(4));
    fxi1->setAddress(0);
    fxi1->setUniverse(0);
    doc->addFixture(fxi1);

    QLCFixtureDef *def2 = m_doc->fixtureDefCache()->fixtureDef("Futurelight", "PCC-250CMY");
    QVERIFY(def2 != NULL);

    QLCFixtureMode *mode2 = def2->mode("Mode 1");
    QVERIFY(mode2 != NULL);

    Fixture *fxi2 = new Fixture(doc);
    fxi2->setFixtureDefinition(def2, mode2);
    QCOMPARE(fxi2->channels(), quint32(12));
    fxi2->setAddress(50);
    fxi2->setUniverse(0);
    doc->addFixture(fxi2);

    QLCFixtureDef *def3 = m_doc->fixtureDefCache()->fixtureDef("Showtec", "Phantom 95 LED Spot");
    QVERIFY(def3 != NULL);

    QLCFixtureMode *mode3 = def3->mode("15 Channels");
    QVERIFY(mode3 != NULL);

    Fixture *fxi3 = new Fixture(doc);
    fxi3->setFixtureDefinition(def3, mode3);
    QCOMPARE(fxi3->channels(), quint32(15));
    fxi3->setAddress(80);
    fxi3->setUniverse(0);
    doc->addFixture(fxi3);

    Scene* s1 = new Scene(doc);
    QVERIFY(s1->values().size() == 0);
    doc->addFunction(s1);

    /* set color of RGB fixture */
    s1->setValue(fxi1->id(), 0, 255);
    s1->setValue(fxi1->id(), 1, 50);
    s1->setValue(fxi1->id(), 2, 100);
    s1->setValue(fxi1->id(), 3, 200);

    /* set color of CMY fixture */
    s1->setValue(fxi2->id(), 1, 200);
    s1->setValue(fxi2->id(), 2, 100);
    s1->setValue(fxi2->id(), 3, 50);

    /* set color of a color wheel (light green) */
    s1->setValue(fxi3->id(), 5, 45);

    QVERIFY(s1->values().size() == 8);

    QColor cmyCol;
    cmyCol.setCmyk(200, 100, 50, 0);
    QVERIFY(s1->colorValue(fxi1->id()) == QColor(50, 100, 200));
    QVERIFY(s1->colorValue(fxi2->id()) == QColor(cmyCol.red(), cmyCol.green(), cmyCol.blue()));
    QVERIFY(s1->colorValue(fxi3->id()) == QColor(85, 255, 0));
}

void Scene_Test::channelGroup()
{
    Scene s(m_doc);
    QVERIFY(s.channelGroups().count() == 0);
    QVERIFY(s.channelGroupsLevels().count() == 0);

    ChannelsGroup *cg = new ChannelsGroup(m_doc);
    cg->addChannel(0, 1);
    cg->addChannel(0, 2);
    cg->addChannel(0, 3);
    m_doc->addChannelsGroup(cg);

    /* add a channel group */
    s.addChannelGroup(cg->id());
    QVERIFY(s.channelGroups().count() == 1);
    QVERIFY(s.channelGroupsLevels().count() == 1);

    /* do not allow adding same group twice */
    s.addChannelGroup(cg->id());
    QVERIFY(s.channelGroups().count() == 1);
    QVERIFY(s.channelGroupsLevels().count() == 1);

    s.setChannelGroupLevel(cg->id(), 142);
    QVERIFY(s.channelGroups().count() == 1);
    QVERIFY(s.channelGroupsLevels().count() == 1);
    QVERIFY(s.channelGroupsLevels().at(0) == 142);

    /* remove an invalid group */
    s.removeChannelGroup(42);
    QVERIFY(s.channelGroups().count() == 1);

    /* remove a valid group */
    s.removeChannelGroup(cg->id());
    QVERIFY(s.channelGroups().count() == 0);
    QVERIFY(s.channelGroupsLevels().count() == 0);
}

void Scene_Test::fixtureRemoval()
{
    Scene s(m_doc);
    QVERIFY(s.values().size() == 0);

    s.setValue(1, 2, 3);
    s.setValue(4, 5, 6);
    QVERIFY(s.values().size() == 2);

    /* Simulate fixture removal signal with an uninteresting fixture id */
    s.slotFixtureRemoved(6);
    QVERIFY(s.values().size() == 2);

    /* Simulate fixture removal signal with a fixture in the scene */
    s.slotFixtureRemoved(4);
    QVERIFY(s.values().size() == 1);
    QVERIFY(s.values().at(0).fxi == 1);
    QVERIFY(s.values().at(0).channel == 2);
    QVERIFY(s.values().at(0).value == 3);

    /* Simulate fixture removal signal with an invalid fixture id */
    s.slotFixtureRemoved(Fixture::invalidId());
    QVERIFY(s.values().size() == 1);
    QVERIFY(s.values().at(0).fxi == 1);
    QVERIFY(s.values().at(0).channel == 2);
    QVERIFY(s.values().at(0).value == 3);

    /* Simulate fixture removal signal with a fixture in the scene */
    s.slotFixtureRemoved(1);
    QVERIFY(s.values().size() == 0);
}

void Scene_Test::loadSuccess()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Function");
    xmlWriter.writeAttribute("Type", "Scene");

    xmlWriter.writeStartElement("Bus");
    xmlWriter.writeAttribute("Role", "Fade");
    xmlWriter.writeCharacters("5");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Speed");
    xmlWriter.writeAttribute("FadeIn", "500");
    xmlWriter.writeAttribute("FadeOut", "5000");
    xmlWriter.writeAttribute("Duration", "50000");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Value");
    xmlWriter.writeAttribute("Fixture", "5");
    xmlWriter.writeAttribute("Channel", "60");
    xmlWriter.writeCharacters("100");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Value");
    xmlWriter.writeAttribute("Fixture", "133");
    xmlWriter.writeAttribute("Channel", "4");
    xmlWriter.writeCharacters("59");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Foo");
    xmlWriter.writeAttribute("Fixture", "133");
    xmlWriter.writeAttribute("Channel", "4");
    xmlWriter.writeCharacters("59");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Scene s(m_doc);
    QVERIFY(s.loadXML(xmlReader) == true);
    QVERIFY(s.fadeInSpeed() == 500);
    QVERIFY(s.fadeOutSpeed() == 5000);
    QVERIFY(s.duration() == 50000);
    QVERIFY(s.values().size() == 2);
    QVERIFY(s.value(5, 60) == 100);
    QVERIFY(s.value(133, 4) == 59);
}

void Scene_Test::loadWrongType()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Function");
    xmlWriter.writeAttribute("Type", "Chaser");

    xmlWriter.writeStartElement("Bus");
    xmlWriter.writeAttribute("Role", "Fade");
    xmlWriter.writeCharacters("5");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Value");
    xmlWriter.writeAttribute("Fixture", "5");
    xmlWriter.writeAttribute("Channel", "60");
    xmlWriter.writeCharacters("100");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Value");
    xmlWriter.writeAttribute("Fixture", "133");
    xmlWriter.writeAttribute("Channel", "4");
    xmlWriter.writeCharacters("59");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Scene s(m_doc);
    QVERIFY(s.loadXML(xmlReader) == false);
}

void Scene_Test::loadWrongRoot()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Scene");
    xmlWriter.writeAttribute("Type", "Scene");

    xmlWriter.writeStartElement("Bus");
    xmlWriter.writeAttribute("Role", "Fade");
    xmlWriter.writeCharacters("5");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Value");
    xmlWriter.writeAttribute("Fixture", "5");
    xmlWriter.writeAttribute("Channel", "60");
    xmlWriter.writeCharacters("100");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Value");
    xmlWriter.writeAttribute("Fixture", "133");
    xmlWriter.writeAttribute("Channel", "4");
    xmlWriter.writeCharacters("59");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Scene s(m_doc);
    QVERIFY(s.loadXML(xmlReader) == false);
}

void Scene_Test::save()
{
    Scene s(m_doc);
    s.setFadeInSpeed(100);
    s.setFadeOutSpeed(1000);
    s.setDuration(10000);

    QVERIFY(s.fixtures().count() == 0);
    s.setValue(0, 0, 100);
    s.setValue(3, 0, 150);
    s.setValue(3, 3, 10);
    s.setValue(3, 5, 100);
    /* verify that 2 fixture IDs have been added */
    QVERIFY(s.fixtures().count() == 2);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(s.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(xmlReader.name().toString() == "Function");
    QVERIFY(xmlReader.attributes().value("Type").toString() == "Scene");

    xmlReader.readNextStartElement();

    QVERIFY(xmlReader.name().toString() == "Speed");
    QVERIFY(xmlReader.attributes().value("FadeIn").toString() == "100");
    QVERIFY(xmlReader.attributes().value("FadeOut").toString() == "1000");
    QVERIFY(xmlReader.attributes().value("Duration").toString() == "10000");

    xmlReader.skipCurrentElement();
    xmlReader.readNextStartElement();

    QVERIFY(xmlReader.name().toString() == "FixtureVal");
    QVERIFY(xmlReader.attributes().value("ID").toString() == "0");
    QVERIFY(xmlReader.readElementText() == "0,100");

    xmlReader.readNextStartElement();

    QVERIFY(xmlReader.name().toString() == "FixtureVal");
    QVERIFY(xmlReader.attributes().value("ID").toString() == "3");
    QVERIFY(xmlReader.readElementText() == "0,150,3,10,5,100");
}

void Scene_Test::copyFrom()
{
    Scene s1(m_doc);
    s1.setName("First");
    s1.setFadeInSpeed(100);
    s1.setFadeOutSpeed(1000);
    s1.setDuration(10000);
    s1.setValue(1, 2, 3);
    s1.setValue(4, 5, 6);
    s1.setValue(7, 8, 9);

    /* Verify that scene contents are copied */
    Scene s2(m_doc);
    QSignalSpy spy(&s2, SIGNAL(changed(quint32)));
    QVERIFY(s2.copyFrom(&s1) == true);
    QCOMPARE(spy.size(), 1);
    QVERIFY(s2.name() == s1.name());
    QVERIFY(s2.fadeInSpeed() == 100);
    QVERIFY(s2.fadeOutSpeed() == 1000);
    QVERIFY(s2.duration() == 10000);
    QVERIFY(s2.value(1, 2) == 3);
    QVERIFY(s2.value(4, 5) == 6);
    QVERIFY(s2.value(7, 8) == 9);

    /* Verify that a Scene gets a copy only from another Scene */
    Chaser c(m_doc);
    QVERIFY(s2.copyFrom(&c) == false);

    /* Make a third Scene */
    Scene s3(m_doc);
    s3.setName("Third");
    s3.setFadeInSpeed(200);
    s3.setFadeOutSpeed(2000);
    s3.setDuration(20000);
    s3.setValue(3, 1, 2);
    s3.setValue(6, 4, 5);
    s3.setValue(9, 7, 8);

    /* Verify that copying TO the same Scene a second time succeeds */
    QVERIFY(s2.copyFrom(&s3) == true);
    QVERIFY(s2.name() == s3.name());
    QVERIFY(s2.fadeInSpeed() == 200);
    QVERIFY(s2.fadeOutSpeed() == 2000);
    QVERIFY(s2.duration() == 20000);
    QVERIFY(s2.value(3, 1) == 2);
    QVERIFY(s2.value(6, 4) == 5);
    QVERIFY(s2.value(9, 7) == 8);
}

void Scene_Test::createCopy()
{
    Doc doc(this);

    Scene* s1 = new Scene(m_doc);
    s1->setName("First");
    s1->setFadeInSpeed(200);
    s1->setFadeOutSpeed(2000);
    s1->setDuration(20000);
    s1->setValue(1, 2, 3);
    s1->setValue(4, 5, 6);
    s1->setValue(7, 8, 9);

    doc.addFunction(s1);
    QVERIFY(s1->id() != Function::invalidId());

    Function* f = s1->createCopy(&doc);
    QVERIFY(f != NULL);
    QVERIFY(f != s1);
    QVERIFY(f->id() != s1->id());

    Scene* copy = qobject_cast<Scene*> (f);
    QVERIFY(copy != NULL);
    QVERIFY(copy->fadeInSpeed() == 200);
    QVERIFY(copy->fadeOutSpeed() == 2000);
    QVERIFY(copy->duration() == 20000);
    QVERIFY(copy->values().size() == 3);
    QVERIFY(copy->value(1, 2) == 3);
    QVERIFY(copy->value(4, 5) == 6);
    QVERIFY(copy->value(7, 8) == 9);
}

void Scene_Test::preRunPostRun()
{
    Doc* doc = new Doc(this);
    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));
    MasterTimerStub timer(m_doc, ua);

    Fixture* fxi = new Fixture(doc);
    fxi->setName("Test Fixture");
    fxi->setAddress(15);
    fxi->setUniverse(0);
    fxi->setChannels(10);
    doc->addFixture(fxi);

    Scene* s1 = new Scene(doc);
    s1->setName("First");
    s1->setValue(fxi->id(), 0, 123);
    s1->setValue(fxi->id(), 7, 45);
    s1->setValue(fxi->id(), 3, 67);
    doc->addFunction(s1);

    QVERIFY(s1->m_fadersMap.isEmpty());
    s1->preRun(&timer);
    QVERIFY(s1->m_fadersMap.isEmpty());

    s1->write(&timer, ua);
    QVERIFY(s1->m_fadersMap.count() == 1);

    s1->postRun(&timer, ua);
    QVERIFY(s1->m_fadersMap.isEmpty());

    delete doc;
}

void Scene_Test::flashUnflash()
{
    Doc *doc = new Doc(this);
    QList<Universe*> ua;
    MasterTimer timer(doc);

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(10);
    doc->addFixture(fxi);

    Scene *s1 = new Scene(doc);
    s1->setName("First");
    s1->setValue(fxi->id(), 0, 123);
    s1->setValue(fxi->id(), 1, 45);
    s1->setValue(fxi->id(), 2, 67);
    doc->addFunction(s1);

    QVERIFY(timer.m_dmxSourceList.size() == 0);

    s1->flash(&timer, false, false);
    QVERIFY(timer.m_dmxSourceList.size() == 1);
    QVERIFY(s1->stopped() == true);
    QVERIFY(s1->flashing() == true);

    ua = doc->inputOutputMap()->claimUniverses();
    s1->writeDMX(&timer, ua);
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[0] == char(123));
    QVERIFY(ua[0]->preGMValues()[1] == char(45));
    QVERIFY(ua[0]->preGMValues()[2] == char(67));
    doc->inputOutputMap()->releaseUniverses(false);

    s1->flash(&timer, false, false);
    QVERIFY(timer.m_dmxSourceList.size() == 1);
    QVERIFY(s1->stopped() == true);
    QVERIFY(s1->flashing() == true);

    ua = doc->inputOutputMap()->claimUniverses();
    s1->writeDMX(&timer, ua);
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[0] == char(123));
    QVERIFY(ua[0]->preGMValues()[1] == char(45));
    QVERIFY(ua[0]->preGMValues()[2] == char(67));
    doc->inputOutputMap()->releaseUniverses(false);

    s1->unFlash(&timer);
    QVERIFY(timer.m_dmxSourceList.size() == 1);
    QVERIFY(s1->stopped() == true);
    QVERIFY(s1->flashing() == false);

    ua = doc->inputOutputMap()->claimUniverses();
    s1->writeDMX(&timer, ua);
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[0] == char(0));
    QVERIFY(ua[0]->preGMValues()[1] == char(0));
    QVERIFY(ua[0]->preGMValues()[2] == char(0));
    doc->inputOutputMap()->releaseUniverses(false);

    QVERIFY(timer.m_dmxSourceList.size() == 0);
}

void Scene_Test::writeHTPZeroTicks()
{
    Doc* doc = new Doc(this);
    MasterTimer timer(doc);
    QList<Universe*> ua;

    Fixture* fxi = new Fixture(doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(10);
    doc->addFixture(fxi);

    Scene* s1 = new Scene(doc);
    s1->setFadeInSpeed(0);
    s1->setFadeOutSpeed(0);
    s1->setName("First");
    s1->setValue(fxi->id(), 0, 255);
    s1->setValue(fxi->id(), 1, 127);
    s1->setValue(fxi->id(), 2, 0);
    doc->addFunction(s1);

    s1->start(&timer, FunctionParent::master());
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[0] == (char) 255);
    QVERIFY(ua[0]->preGMValues()[1] == (char) 127);
    QVERIFY(ua[0]->preGMValues()[2] == (char) 0);
    QVERIFY(s1->stopped() == false);
    doc->inputOutputMap()->releaseUniverses(false);

    s1->stop(FunctionParent::master());
    QVERIFY(s1->stopped() == true);
    QVERIFY(s1->isRunning() == true); // postRun has not been run yet, but..
    timer.timerTick();                // ..now it has.
    QVERIFY(s1->isRunning() == false);
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[0] == (char) 0);
    QVERIFY(ua[0]->preGMValues()[1] == (char) 0);
    QVERIFY(ua[0]->preGMValues()[2] == (char) 0);
    doc->inputOutputMap()->releaseUniverses(false);
}

void Scene_Test::writeHTPTwoTicks()
{
    Doc* doc = new Doc(this);
    MasterTimer timer(doc);
    QList<Universe*> ua;

    QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Futurelight", "DJScan250");
    QVERIFY(def != NULL);

    QLCFixtureMode* mode = def->mode("Mode 1");
    QVERIFY(mode != NULL);

    Fixture* fxi = new Fixture(doc);
    fxi->setFixtureDefinition(def, mode);
    QCOMPARE(fxi->channels(), quint32(6));
    fxi->setAddress(0);
    fxi->setUniverse(0);
    doc->addFixture(fxi);

    Scene* s1 = new Scene(doc);
    s1->setName("First");
    s1->setFadeInSpeed(MasterTimer::tick() * 2);
    s1->setFadeOutSpeed(MasterTimer::tick() * 2);
    s1->setValue(fxi->id(), 5, 250); // HTP
    s1->setValue(fxi->id(), 0, 100); // LTP
    doc->addFunction(s1);

    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    QVERIFY(ua[0]->preGMValues()[5] == (char) 0);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 0);
    doc->inputOutputMap()->releaseUniverses(false);

    QVERIFY(s1->stopped() == true);
    s1->start(&timer, FunctionParent::master());
    timer.timerTick();

    QVERIFY(s1->stopped() == false);
    QVERIFY(s1->isRunning() == true);
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 125);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 50);
    doc->inputOutputMap()->releaseUniverses(false);

    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 250);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);
    QVERIFY(s1->stopped() == false);
    doc->inputOutputMap()->releaseUniverses(false);

    // Values stay up
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 250);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);
    QVERIFY(s1->stopped() == false);
    ua[0]->write(5, 255); // Overridden in the next round
    ua[0]->write(0, 42);  // Not overridden in the next round
    doc->inputOutputMap()->releaseUniverses(false);

    // Values stay up, LTP is not written anymore
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 250);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);
    QVERIFY(s1->stopped() == false);
    doc->inputOutputMap()->releaseUniverses(false);

    // Write a value when running (with HTP check)
    s1->setValue(SceneValue(fxi->id(), 5, 254), false, true);
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 254);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);
    doc->inputOutputMap()->releaseUniverses(false);

    // Write a value when running (no HTP check)
    s1->setValue(SceneValue(fxi->id(), 5, 180), false, false);
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 180);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);
    doc->inputOutputMap()->releaseUniverses(false);

    s1->stop(FunctionParent::master());
    QVERIFY(s1->stopped() == true);
    QVERIFY(s1->isRunning() == true);

    timer.timerTick();
    QVERIFY(s1->isRunning() == false);
    QVERIFY(s1->stopped() == true);
    // Now, the channels are inside MasterTimer's GenericFader to be zeroed out
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QCOMPARE(ua[0]->preGMValues()[5], (char) 90); // HTP fades out
    QCOMPARE(ua[0]->preGMValues()[0], (char) 100);  // LTP doesn't
    doc->inputOutputMap()->releaseUniverses(false);

    // Now, the channels are inside MasterTimer's GenericFader to be zeroed out
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 0);  // HTP fades out
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100); // LTP doesn't
    doc->inputOutputMap()->releaseUniverses(false);
}

void Scene_Test::writeHTPTwoTicksIntensity()
{
    Doc* doc = new Doc(this);
    MasterTimer timer(doc);
    QList<Universe*> ua;

    QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Futurelight", "DJScan250");
    QVERIFY(def != NULL);

    QLCFixtureMode* mode = def->mode("Mode 1");
    QVERIFY(mode != NULL);

    Fixture* fxi = new Fixture(doc);
    fxi->setFixtureDefinition(def, mode);
    QCOMPARE(fxi->channels(), quint32(6));
    fxi->setAddress(0);
    fxi->setUniverse(0);
    doc->addFixture(fxi);

    Scene* s1 = new Scene(doc);
    s1->setName("First");
    s1->setFadeInSpeed(MasterTimer::tick() * 2);
    s1->setFadeOutSpeed(MasterTimer::tick() * 2);
    s1->setValue(fxi->id(), 5, 250); // HTP
    s1->setValue(fxi->id(), 0, 100); // LTP
    doc->addFunction(s1);

    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    QVERIFY(ua[0]->preGMValues()[5] == (char) 0);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 0);
    doc->inputOutputMap()->releaseUniverses(false);

    s1->adjustAttribute(0.5, Function::Intensity);

    QVERIFY(s1->stopped() == true);
    s1->start(&timer, FunctionParent::master());
    timer.timerTick();

    QVERIFY(s1->stopped() == false);
    QVERIFY(s1->isRunning() == true);
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) floor((qreal(125) * qreal(0.5)) + 0.5));
    QVERIFY(ua[0]->preGMValues()[0] == (char) 50); // Intensity affects only HTP channels
    doc->inputOutputMap()->releaseUniverses(false);

    s1->adjustAttribute(1.0, Function::Intensity);

    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 250);
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);
    QVERIFY(s1->stopped() == false);
    doc->inputOutputMap()->releaseUniverses(false);

    s1->adjustAttribute(0.2, Function::Intensity);

    // Values stay up
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) floor((qreal(250) * qreal(0.2)) + 0.5));
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);
    QVERIFY(s1->stopped() == false);
    ua[0]->write(5, 255); // Overridden in the next round
    ua[0]->write(0, 42);  // Not overridden in the next round
    doc->inputOutputMap()->releaseUniverses(false);

    // Values stay up, LTP is not written anymore
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) floor((qreal(250) * qreal(0.2)) + 0.5));
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);
    QVERIFY(s1->stopped() == false);
    doc->inputOutputMap()->releaseUniverses(false);

    s1->stop(FunctionParent::master());
    QVERIFY(s1->stopped() == true);
    QVERIFY(s1->isRunning() == true);

    timer.timerTick();
    QVERIFY(s1->isRunning() == false);
    QVERIFY(s1->stopped() == true);
    // Now, the channels are inside MasterTimer's GenericFader to be zeroed out
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) floor((qreal(125) * qreal(0.2)) + 0.5)); // HTP fades out
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100);  // LTP doesn't
    doc->inputOutputMap()->releaseUniverses(false);

    // Now, the channels are inside MasterTimer's GenericFader to be zeroed out
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[5] == (char) 0);  // HTP fades out
    QVERIFY(ua[0]->preGMValues()[0] == (char) 100); // LTP doesn't
    doc->inputOutputMap()->releaseUniverses(false);
}

void Scene_Test::writeLTPReady()
{
    Doc* doc = new Doc(this);
    MasterTimer timer(doc);
    QList<Universe*> ua;

    QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Futurelight", "DJScan250");
    QVERIFY(def != NULL);

    QLCFixtureMode* mode = def->mode("Mode 1");
    QVERIFY(mode != NULL);

    Fixture* fxi = new Fixture(doc);
    fxi->setFixtureDefinition(def, mode);
    QCOMPARE(fxi->channels(), quint32(6));
    fxi->setAddress(0);
    fxi->setUniverse(0);
    doc->addFixture(fxi);

    Scene* s1 = new Scene(doc);
    s1->setName("First");
    s1->setFadeInSpeed(MasterTimer::tick() * 2);
    s1->setFadeOutSpeed(MasterTimer::tick() * 2);
    s1->setValue(fxi->id(), 0, 250); // LTP
    s1->setValue(fxi->id(), 1, 200); // LTP
    s1->setValue(fxi->id(), 2, 100); // LTP
    doc->addFunction(s1);

    QVERIFY(s1->stopped() == true);
    QVERIFY(s1->isRunning() == false);
    s1->start(&timer, FunctionParent::master());

    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[0] == (char) 125);
    QVERIFY(ua[0]->preGMValues()[1] == (char) 100);
    QVERIFY(ua[0]->preGMValues()[2] == (char) 50);
    doc->inputOutputMap()->releaseUniverses(false);

    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[0] == (char) 250);
    QVERIFY(ua[0]->preGMValues()[1] == (char) 200);
    QVERIFY(ua[0]->preGMValues()[2] == (char) 100);
    doc->inputOutputMap()->releaseUniverses(false);

    QVERIFY(s1->stopped() == false);
    QVERIFY(s1->isRunning() == true);

    // LTP values stay on
    timer.timerTick();
    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());
    QVERIFY(ua[0]->preGMValues()[0] == (char) 250);
    QVERIFY(ua[0]->preGMValues()[1] == (char) 200);
    QVERIFY(ua[0]->preGMValues()[2] == (char) 100);
    doc->inputOutputMap()->releaseUniverses(false);

    QVERIFY(s1->stopped() == false);
    QVERIFY(s1->isRunning() == true);
}

void Scene_Test::iconPauseAndReleaseFlags()
{
    MasterTimer timer(m_doc);
    Scene s(m_doc);

    // Only the QIcon construction is exercised: the image resource lives in the application binary
    s.getIcon();

    QVERIFY(s.releaseOnStop() == false);
    s.setReleaseOnStop(true);
    QVERIFY(s.releaseOnStop() == true);

    // Pausing a Scene that is not running is refused
    s.setPause(true);
    QVERIFY(s.isPaused() == false);

    // Un-flashing a Scene that is not flashing is a no-op
    s.unFlash(&timer);
    QVERIFY(s.flashing() == false);
    QVERIFY(timer.m_dmxSourceList.isEmpty());
}

void Scene_Test::colorValueEdgeCases()
{
    Doc *doc = new Doc(this);

    // A white LED counts as an equal amount of red, green and blue
    QLCFixtureDef *rgbwDef = m_doc->fixtureDefCache()->fixtureDef("Generic", "Generic RGBW");
    QVERIFY(rgbwDef != NULL);
    QLCFixtureMode *rgbwMode = rgbwDef->mode("RGBW");
    QVERIFY(rgbwMode != NULL);
    Fixture *rgbw = new Fixture(doc);
    rgbw->setFixtureDefinition(rgbwDef, rgbwMode);
    QCOMPARE(rgbw->channels(), quint32(4));
    rgbw->setAddress(0);
    rgbw->setUniverse(0);
    doc->addFixture(rgbw);

    // A colour wheel with split (double) colours reports the first colour
    QLCFixtureDef *wheelDef = m_doc->fixtureDefCache()->fixtureDef("Showtec", "Acrobat");
    QVERIFY(wheelDef != NULL);
    QLCFixtureMode *wheelMode = wheelDef->mode("16 Channel");
    QVERIFY(wheelMode != NULL);
    Fixture *wheel = new Fixture(doc);
    wheel->setFixtureDefinition(wheelDef, wheelMode);
    QCOMPARE(wheel->channels(), quint32(16));
    wheel->setAddress(10);
    wheel->setUniverse(0);
    doc->addFixture(wheel);

    // A plain dimmer carries no colour information at all
    Fixture *dimmer = new Fixture(doc);
    dimmer->setAddress(30);
    dimmer->setUniverse(0);
    dimmer->setChannels(2);
    doc->addFixture(dimmer);

    Scene *s = new Scene(doc);
    doc->addFunction(s);
    s->setValue(rgbw->id(), 3, 200);   // White
    s->setValue(rgbw->id(), 99, 10);   // channel the fixture does not have
    s->setValue(wheel->id(), 7, 68);   // "White + Blue" split colour
    s->setValue(dimmer->id(), 0, 255);
    s->setValue(4242, 0, 77);          // fixture that does not exist

    QCOMPARE(s->colorValue(rgbw->id()), QColor(200, 200, 200));
    QCOMPARE(s->colorValue(wheel->id()), QColor(255, 255, 255));
    QVERIFY(s->colorValue(dimmer->id()).isValid() == false);
    QVERIFY(s->colorValue(4242).isValid() == false);
}

void Scene_Test::fixtureGroupsAndPalettes()
{
    Scene s(m_doc);
    QVERIFY(s.fixtureGroups().isEmpty());
    QVERIFY(s.palettes().isEmpty());

    s.addFixtureGroup(3);
    s.addFixtureGroup(3); // no duplicates
    s.addFixtureGroup(5);
    QCOMPARE(s.fixtureGroups(), QList<quint32>() << 3 << 5);
    QVERIFY(s.removeFixtureGroup(42) == false);
    QVERIFY(s.removeFixtureGroup(3) == true);
    QCOMPARE(s.fixtureGroups(), QList<quint32>() << 5);

    s.addPalette(7);
    s.addPalette(7);
    s.addPalette(9);
    QCOMPARE(s.palettes(), QList<quint32>() << 7 << 9);
    QVERIFY(s.removePalette(42) == false);
    QVERIFY(s.removePalette(7) == true);
    QCOMPARE(s.palettes(), QList<quint32>() << 9);

    s.clear();
    QVERIFY(s.fixtureGroups().isEmpty());
    QVERIFY(s.palettes().isEmpty());
}

void Scene_Test::saveLoadRoundTrip()
{
    Scene s(m_doc);
    s.setName("Round");
    s.setTempoType(Function::Beats);
    s.setFadeInSpeed(1000);
    s.setFadeOutSpeed(2000);
    s.setDuration(4000);
    s.setReleaseOnStop(true);
    s.addChannelGroup(7);
    s.setChannelGroupLevel(7, 100);
    s.addChannelGroup(9);
    s.setChannelGroupLevel(9, 200);
    s.addFixtureGroup(11);
    s.addFixtureGroup(12);
    s.addPalette(21);
    // Fixtures are saved in insertion order while the values are sorted by fixture ID
    s.setValue(3, 0, 10);
    s.setValue(3, 2, 20);
    s.setValue(0, 1, 30);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(s.saveXML(&xmlWriter) == true);
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Scene loaded(m_doc);
    QVERIFY(loaded.loadXML(xmlReader) == true);
    QCOMPARE(loaded.tempoType(), Function::Beats);
    QCOMPARE(loaded.fadeInSpeed(), uint(1000));
    QCOMPARE(loaded.fadeOutSpeed(), uint(2000));
    QCOMPARE(loaded.duration(), uint(4000));
    QVERIFY(loaded.releaseOnStop() == true);
    QCOMPARE(loaded.channelGroups(), QList<quint32>() << 7 << 9);
    QCOMPARE(loaded.channelGroupsLevels().size(), 2);
    QCOMPARE(loaded.channelGroupsLevels().at(0), uchar(100));
    QCOMPARE(loaded.channelGroupsLevels().at(1), uchar(200));
    QCOMPARE(loaded.fixtureGroups(), QList<quint32>() << 11 << 12);
    QCOMPARE(loaded.palettes(), QList<quint32>() << 21);
    QCOMPARE(loaded.fixtures(), QList<quint32>() << 3 << 0);
    QCOMPARE(loaded.values().size(), 3);
    QCOMPARE(loaded.value(3, 0), uchar(10));
    QCOMPARE(loaded.value(3, 2), uchar(20));
    QCOMPARE(loaded.value(0, 1), uchar(30));
}

void Scene_Test::loadLegacyChannelGroupsAndEmptyValues()
{
    QByteArray xml("<Function Type=\"Scene\">"
                   "<ChannelGroups>4,6</ChannelGroups>"
                   "<ChannelGroupsVal></ChannelGroupsVal>"
                   "<FixtureVal ID=\"8\"></FixtureVal>"
                   "<FixtureGroup ID=\"2\"/>"
                   "<Palette ID=\"3\"/>"
                   "</Function>");
    QXmlStreamReader xmlReader(xml);
    xmlReader.readNextStartElement();

    Scene s(m_doc);
    QVERIFY(s.loadXML(xmlReader) == true);
    // The legacy tag carries only IDs: every level defaults to zero
    QCOMPARE(s.channelGroups(), QList<quint32>() << 4 << 6);
    QCOMPARE(s.channelGroupsLevels().size(), 2);
    QCOMPARE(s.channelGroupsLevels().at(0), uchar(0));
    QCOMPARE(s.channelGroupsLevels().at(1), uchar(0));
    // An empty FixtureVal still registers the fixture, without any value
    QCOMPARE(s.fixtures(), QList<quint32>() << 8);
    QVERIFY(s.values().isEmpty());
    QCOMPARE(s.fixtureGroups(), QList<quint32>() << 2);
    QCOMPARE(s.palettes(), QList<quint32>() << 3);
}

void Scene_Test::postLoad()
{
    Fixture *fxi = new Fixture(m_doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(4);
    m_doc->addFixture(fxi);

    Scene *s = new Scene(m_doc);
    m_doc->addFunction(s);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    xmlWriter.writeStartElement("Function");
    xmlWriter.writeAttribute("Type", "Scene");
    xmlWriter.writeStartElement("Bus");
    xmlWriter.writeAttribute("Role", "Fade");
    xmlWriter.writeCharacters("5");
    xmlWriter.writeEndElement();
    xmlWriter.writeStartElement("Value");
    xmlWriter.writeAttribute("Fixture", QString::number(fxi->id()));
    xmlWriter.writeAttribute("Channel", "1");
    xmlWriter.writeCharacters("100");
    xmlWriter.writeEndElement();
    xmlWriter.writeStartElement("Value"); // channel beyond the fixture's channel count
    xmlWriter.writeAttribute("Fixture", QString::number(fxi->id()));
    xmlWriter.writeAttribute("Channel", "9");
    xmlWriter.writeCharacters("50");
    xmlWriter.writeEndElement();
    xmlWriter.writeStartElement("Value"); // fixture that does not exist
    xmlWriter.writeAttribute("Fixture", "4242");
    xmlWriter.writeAttribute("Channel", "0");
    xmlWriter.writeCharacters("77");
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    // Legacy bus 5 holds a speed expressed in timer ticks
    Bus::instance()->setValue(5, 100);

    QVERIFY(s->loadXML(xmlReader) == true);
    QCOMPARE(s->m_legacyFadeBus, quint32(5));
    QCOMPARE(s->values().size(), 3);

    s->postLoad();
    uint expectedSpeed = (100 / MasterTimer::frequency()) * 1000;
    QCOMPARE(s->fadeInSpeed(), expectedSpeed);
    QCOMPARE(s->fadeOutSpeed(), expectedSpeed);
    // Values for unknown fixtures and non-existent channels are dropped
    QCOMPARE(s->values().size(), 1);
    QCOMPARE(s->value(fxi->id(), 1), uchar(100));

    // Without a legacy bus the speeds are left alone
    Scene s2(m_doc);
    s2.setFadeInSpeed(300);
    s2.postLoad();
    QCOMPARE(s2.fadeInSpeed(), uint(300));
}

void Scene_Test::flashForceLTP()
{
    Doc *doc = new Doc(this);
    QList<Universe*> ua;
    MasterTimer timer(doc);

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(10);
    doc->addFixture(fxi);

    Scene *s1 = new Scene(doc);
    s1->setValue(fxi->id(), 0, 123);
    // A value of a fixture that no longer exists is treated as an absolute
    // address; this one maps beyond every universe the timer hands over
    s1->setValue(4242, UNIVERSE_SIZE * doc->inputOutputMap()->universesCount(), 77);
    doc->addFunction(s1);

    s1->flash(&timer, false, true);
    QVERIFY(s1->flashing() == true);
    QVERIFY(s1->m_flashForceLTP == true);
    QCOMPARE(timer.m_dmxSourceList.size(), 1);

    ua = doc->inputOutputMap()->claimUniverses();
    s1->writeDMX(&timer, ua);
    QSharedPointer<GenericFader> fader = s1->m_fadersMap.value(0);
    QVERIFY(!fader.isNull());
    QCOMPARE(s1->m_fadersMap.count(), 1);
    QCOMPARE(fader->channelsCount(), 1); // the out-of-range value is skipped
    FadeChannel fc = fader->channels().values().first();
    QVERIFY(fc.flags() & FadeChannel::ForceLTP);
    QVERIFY(fc.flags() & FadeChannel::Flashing);
    ua[0]->processFaders(MasterTimer::tick());
    QCOMPARE(ua[0]->preGMValues()[0], char(123));
    doc->inputOutputMap()->releaseUniverses(false);

    s1->unFlash(&timer);
    ua = doc->inputOutputMap()->claimUniverses();
    s1->writeDMX(&timer, ua);
    doc->inputOutputMap()->releaseUniverses(false);
    QVERIFY(s1->m_fadersMap.isEmpty());
    QVERIFY(timer.m_dmxSourceList.isEmpty());
}

void Scene_Test::writeWithoutValuesStops()
{
    Doc *doc = new Doc(this);
    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));
    MasterTimerStub timer(m_doc, ua); // not parented to the Doc that is deleted below

    Scene *s = new Scene(doc);
    doc->addFunction(s);

    s->start(&timer, FunctionParent::master());
    QVERIFY(s->stopped() == false);
    QVERIFY(s->isRunning() == true);

    // Nothing to output: the Scene stops itself
    s->write(&timer, ua);
    QVERIFY(s->stopped() == true);
    QVERIFY(s->m_fadersMap.isEmpty());
    QCOMPARE(s->elapsed(), quint32(0));

    s->postRun(&timer, ua);
    delete doc;
}

void Scene_Test::writeSkipsMissingUniverse()
{
    Doc *doc = new Doc(this);
    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));
    MasterTimerStub timer(m_doc, ua); // not parented to the Doc that is deleted below

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(4);
    doc->addFixture(fxi);

    Scene *s = new Scene(doc);
    s->setValue(fxi->id(), 0, 200);
    doc->addFunction(s);

    s->preRun(&timer);
    // The fixture's universe is not among the ones handed over: no fader is requested
    s->write(&timer, QList<Universe*>());
    QVERIFY(s->m_fadersMap.isEmpty());
    QCOMPARE(s->elapsed(), quint32(MasterTimer::tick()));

    s->postRun(&timer, ua);
    delete doc;
}

void Scene_Test::writeNonFadingChannel()
{
    Doc *doc = new Doc(this);
    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));
    MasterTimerStub timer(m_doc, ua); // not parented to the Doc that is deleted below

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(10);
    fxi->setChannelCanFade(2, false);
    doc->addFixture(fxi);

    Scene *s = new Scene(doc);
    s->setFadeInSpeed(MasterTimer::tick() * 5);
    s->setValue(fxi->id(), 2, 200);
    s->setValue(fxi->id(), 3, 100);
    doc->addFunction(s);

    s->preRun(&timer);
    s->write(&timer, ua);
    QSharedPointer<GenericFader> fader = s->m_fadersMap.value(0);
    QVERIFY(!fader.isNull());
    QHash<quint32, FadeChannel> channels = fader->channels();
    QCOMPARE(channels.size(), 2);
    // A channel that cannot fade snaps to its target, the other one keeps the Scene's fade in
    QCOMPARE(channels.value(GenericFader::channelHash(fxi->id(), 2)).fadeTime(), uint(0));
    QCOMPARE(channels.value(GenericFader::channelHash(fxi->id(), 3)).fadeTime(), uint(MasterTimer::tick() * 5));

    s->postRun(&timer, ua);
    delete doc;
}

void Scene_Test::writeBeatsTempo()
{
    Doc *doc = new Doc(this);
    MasterTimer timer(doc);

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(10);
    doc->addFixture(fxi);

    Scene *s = new Scene(doc);
    s->setValue(fxi->id(), 0, 200);
    doc->addFunction(s);
    s->setTempoType(Function::Beats);
    s->setFadeInSpeed(1);     // a negligible fraction of a beat: shorter than any late-to-beat offset
    s->setFadeOutSpeed(1000); // one beat

    // External beats are not generated by the timer, so a requested beat survives until the tick.
    // Then move the timer to the last 20% of the current beat: the Scene is "late to beat" and
    // the fade in is meant to complete before the next beat, which is nearer than its own length.
    timer.setBeatSourceType(MasterTimer::External);
    QTest::qSleep(300);
    int elapsed = int(timer.m_beatTimer.elapsed());
    timer.m_beatTimeDuration = elapsed + elapsed / 4; // 20% of the beat left: late to beat, with slack
    QVERIFY(timer.nextBeatTimeOffset() > 0);

    s->start(&timer, FunctionParent::master());
    timer.requestBeat();
    timer.timerTick(); // preRun + first write, on a beat
    QVERIFY(s->isRunning() == true);
    QCOMPARE(s->elapsedBeats(), quint32(1000));
    QSharedPointer<GenericFader> fader = s->m_fadersMap.value(0);
    QVERIFY(!fader.isNull());
    QCOMPARE(fader->channelsCount(), 1);
    QCOMPARE(fader->channels().values().first().fadeTime(), uint(0));

    // The fade out is expressed in beats as well
    s->stop(FunctionParent::master());
    timer.timerTick();
    QVERIFY(s->isRunning() == false);
    QVERIFY(fader->isFadingOut() == true);
    QVERIFY(s->m_fadersMap.isEmpty());
}

void Scene_Test::releaseOnStopZeroesChannels()
{
    Doc *doc = new Doc(this);
    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));
    MasterTimerStub timer(m_doc, ua); // not parented to the Doc that is deleted below

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(10);
    fxi->setUniverse(0);
    fxi->setChannels(4);
    doc->addFixture(fxi);

    Fixture *remote = new Fixture(doc); // lives in a universe that is not handed over
    remote->setAddress(0);
    remote->setUniverse(1);
    remote->setChannels(4);
    doc->addFixture(remote);

    Scene *s = new Scene(doc);
    s->setValue(fxi->id(), 1, 200);
    s->setValue(remote->id(), 0, 100);
    s->setValue(4242, 0, 50); // fixture that does not exist
    s->setReleaseOnStop(true);
    doc->addFunction(s);

    s->preRun(&timer);
    s->write(&timer, ua);
    ua[0]->processFaders(MasterTimer::tick());
    QCOMPARE(ua[0]->preGMValue(11), uchar(200));

    // Stopping forcefully zeroes every channel the Scene touched in the available universes
    s->postRun(&timer, ua);
    QCOMPARE(ua[0]->preGMValue(11), uchar(0));
    QVERIFY(s->m_fadersMap.isEmpty());
    delete doc;
}

void Scene_Test::blendModeWhileRunning()
{
    Doc *doc = new Doc(this);
    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));
    MasterTimerStub timer(m_doc, ua); // not parented to the Doc that is deleted below

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(4);
    doc->addFixture(fxi);

    Scene *s = new Scene(doc);
    s->setValue(fxi->id(), 0, 200);
    doc->addFunction(s);

    s->preRun(&timer);
    s->write(&timer, ua);
    QSharedPointer<GenericFader> fader = s->m_fadersMap.value(0);
    QVERIFY(!fader.isNull());
    QCOMPARE(fader->m_blendMode, Universe::NormalBlend);

    // A blend mode change while running is forwarded to the live faders
    s->setBlendMode(Universe::AdditiveBlend);
    QCOMPARE(s->blendMode(), Universe::AdditiveBlend);
    QCOMPARE(fader->m_blendMode, Universe::AdditiveBlend);
    s->setBlendMode(Universe::AdditiveBlend); // same mode: nothing to do
    QCOMPARE(fader->m_blendMode, Universe::AdditiveBlend);

    s->postRun(&timer, ua);
    delete doc;
}

void Scene_Test::writePalettes()
{
    Doc *doc = new Doc(this);
    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));
    MasterTimerStub timer(m_doc, ua); // not parented to the Doc that is deleted below

    Fixture *fxi = new Fixture(doc);
    fxi->setAddress(0);
    fxi->setUniverse(0);
    fxi->setChannels(4);
    doc->addFixture(fxi);

    Fixture *grouped = new Fixture(doc);
    grouped->setAddress(10);
    grouped->setUniverse(0);
    grouped->setChannels(4);
    doc->addFixture(grouped);

    FixtureGroup *grp = new FixtureGroup(doc);
    grp->assignFixture(grouped->id());
    QVERIFY(doc->addFixtureGroup(grp) == true);

    QLCPalette *palette = new QLCPalette(QLCPalette::Dimmer, doc);
    palette->setValue(200);
    QVERIFY(doc->addPalette(palette) == true);

    Scene *s = new Scene(doc);
    s->addPalette(4242); // palette that does not exist: skipped
    s->addPalette(palette->id());
    s->addFixture(fxi->id());
    s->addFixtureGroup(grp->id());
    doc->addFunction(s);
    QVERIFY(s->values().isEmpty());

    s->start(&timer, FunctionParent::master()); // the stub timer runs preRun() right away
    s->write(&timer, ua);
    QVERIFY(s->stopped() == false); // palettes count as content: no engine self-stop
    QSharedPointer<GenericFader> fader = s->m_fadersMap.value(0);
    QVERIFY(!fader.isNull());
    QVERIFY(fader->channelsCount() >= 2);
    ua[0]->processFaders(MasterTimer::tick());
    // The palette is applied to both the direct fixture and the group's fixture
    QCOMPARE(ua[0]->preGMValue(0), uchar(200));
    QCOMPARE(ua[0]->preGMValue(10), uchar(200));

    s->postRun(&timer, ua);
    delete doc;
}

QTEST_MAIN(Scene_Test)
