/*
  Q Light Controller Plus - Unit test
  efxfixture_test.cpp

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
#include <QList>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#define protected public
#define private public
#include "mastertimer_stub.h"
#include "efxfixture_test.h"
#include "qlcfixturemode.h"
#include "qlcfixturehead.h"
#include "qlcfixturedef.h"
#include "genericfader.h"
#include "fadechannel.h"
#include "efxfixture.h"
#include "qlcchannel.h"
#include "universe.h"
#include "function.h"
#include "fixture.h"
#include "qlcfile.h"
#include "efx.h"
#include "doc.h"
#undef private
#undef protected

#include "../common/resource_paths.h"

void EFXFixture_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(m_doc->fixtureDefCache()->loadMap(dir) == true);
}

void EFXFixture_Test::init()
{
    int address = 0;
    {
        QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Futurelight", "DJScan250");
        QVERIFY(def != NULL);
        QLCFixtureMode* mode = def->modes().first();
        QVERIFY(mode != NULL);

        Fixture* fxi = new Fixture(m_doc);
        fxi->setFixtureDefinition(def, mode);
        fxi->setAddress(address);
        m_fixture8bitAddress = address;
        address += fxi->channels();
        m_doc->addFixture(fxi);
        m_fixture8bit = fxi->id();
    }

    {
        QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Futurelight", "MH-440");
        QVERIFY(def != NULL);
        QLCFixtureMode* mode = def->modes().first();
        QVERIFY(mode != NULL);

        Fixture* fxi = new Fixture(m_doc);
        fxi->setFixtureDefinition(def, mode);
        fxi->setAddress(address);
        m_fixture16bitAddress = address;
        address += fxi->channels();
        m_doc->addFixture(fxi);
        m_fixture16bit = fxi->id();
    }

    {
        QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Futurelight", "CY-200");
        QVERIFY(def != NULL);
        QLCFixtureMode* mode = def->modes().first();
        QVERIFY(mode != NULL);

        Fixture* fxi = new Fixture(m_doc);
        fxi->setFixtureDefinition(def, mode);
        fxi->setAddress(address);
        m_fixturePanOnlyAddress = address;
        address += fxi->channels();
        m_doc->addFixture(fxi);
        m_fixturePanOnly = fxi->id();
    }

    {
        QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("American DJ", "Sweeper Beam Quad LED");
        QVERIFY(def != NULL);
        QLCFixtureMode* mode = def->modes().last(); // 39 Channel mode
        QVERIFY(mode != NULL);

        Fixture* fxi = new Fixture(m_doc);
        fxi->setFixtureDefinition(def, mode);
        fxi->setAddress(address);
        m_fixtureLedBarAddress = address;
        m_doc->addFixture(fxi);
        m_fixtureLedBar = fxi->id();
    }
}

void EFXFixture_Test::cleanupTestCase()
{
    delete m_doc;
    qDeleteAll(m_customDefs);
    m_customDefs.clear();
}

Fixture* EFXFixture_Test::createFixture(const QString& model, const QStringList& channels,
                                        quint32 address, const QList<int>& headChannels)
{
    QLCFixtureDef* def = new QLCFixtureDef();
    def->setManufacturer("EFXFixture_Test");
    def->setModel(model);
    m_customDefs.append(def);

    QLCFixtureMode* mode = new QLCFixtureMode(def);
    mode->setName("Test mode");

    int index = 0;
    foreach (QString token, channels)
    {
        QLCChannel* ch = new QLCChannel();
        ch->setName(QString("%1 %2").arg(token).arg(index));
        if (token == "PanMSB" || token == "PanLSB")
            ch->setGroup(QLCChannel::Pan);
        else if (token == "TiltMSB" || token == "TiltLSB")
            ch->setGroup(QLCChannel::Tilt);
        else if (token == "DimMSB" || token == "DimLSB")
            ch->setGroup(QLCChannel::Intensity);
        else if (token == "Red")
        {
            ch->setGroup(QLCChannel::Intensity);
            ch->setColour(QLCChannel::Red);
        }
        else if (token == "Green")
        {
            ch->setGroup(QLCChannel::Intensity);
            ch->setColour(QLCChannel::Green);
        }
        else if (token == "Blue")
        {
            ch->setGroup(QLCChannel::Intensity);
            ch->setColour(QLCChannel::Blue);
        }
        else
            ch->setGroup(QLCChannel::Colour);

        ch->setControlByte(token.endsWith("LSB") ? QLCChannel::LSB : QLCChannel::MSB);
        def->addChannel(ch);
        mode->insertChannel(ch, index++);
    }

    if (headChannels.isEmpty() == false)
    {
        QLCFixtureHead head;
        foreach (int chIndex, headChannels)
            head.addChannel(chIndex);
        mode->insertHead(-1, head);
    }

    def->addMode(mode);

    Fixture* fxi = new Fixture(m_doc);
    fxi->setName(model);
    fxi->setFixtureDefinition(def, mode);
    fxi->setAddress(address);
    fxi->setUniverse(0);
    m_doc->addFixture(fxi);
    return fxi;
}

void EFXFixture_Test::cleanup()
{
    m_doc->clearContents();
}

void EFXFixture_Test::initial()
{
    EFX e(m_doc);

    EFXFixture ef(&e);
    QVERIFY(ef.head().fxi == Fixture::invalidId());
    QVERIFY(ef.head().head == -1);
    QVERIFY(ef.direction() == EFX::Forward);
    QVERIFY(ef.serialNumber() == 0);
    QVERIFY(ef.isValid() == false);
    QVERIFY(ef.isDone() == false);

    QVERIFY(ef.m_runTimeDirection == EFX::Forward);
    QVERIFY(ef.m_done == false);
    QVERIFY(ef.m_elapsed == 0);
}

void EFXFixture_Test::copyFrom()
{
    EFX e(m_doc);

    EFXFixture ef(&e);
    ef.m_head.fxi = 15;
    ef.m_head.head = 16;
    ef.m_direction = EFX::Backward;
    ef.m_serialNumber = 25;
    ef.m_runTimeDirection = EFX::Backward;
    ef.m_done = true;
    ef.m_elapsed = 31337;

    EFXFixture copy(&e);
    copy.copyFrom(&ef);
    QVERIFY(copy.m_head.fxi == 15);
    QVERIFY(copy.m_head.head == 16);
    QVERIFY(copy.m_direction == EFX::Backward);
    QVERIFY(copy.m_serialNumber == 25);
    QVERIFY(copy.m_runTimeDirection == EFX::Backward);
    QVERIFY(copy.m_done == true);
    QVERIFY(copy.m_elapsed == 31337);
}

void EFXFixture_Test::publicProperties()
{
    EFX e(m_doc);
    EFXFixture ef(&e);

    ef.setHead(GroupHead(19, 5));
    QVERIFY(ef.head().fxi == 19);
    QVERIFY(ef.head().head == 5);

    ef.setHead(GroupHead());
    QVERIFY(ef.head().fxi == Fixture::invalidId());

    ef.setDirection(EFX::Backward);
    QVERIFY(ef.direction() == EFX::Backward);
    QVERIFY(ef.m_runTimeDirection == EFX::Backward);

    ef.setDirection(EFX::Forward);
    QVERIFY(ef.direction() == EFX::Forward);
    QVERIFY(ef.m_runTimeDirection == EFX::Forward);
}

void EFXFixture_Test::loadSuccess()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("ID", "83");
    xmlWriter.writeTextElement("Head", "76");
    xmlWriter.writeTextElement("Direction", "Backward");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    EFX e(m_doc);
    EFXFixture ef(&e);
    QVERIFY(ef.loadXML(xmlReader) == true);
    QVERIFY(ef.head().fxi == 83);
    QVERIFY(ef.head().head == 76);
    QVERIFY(ef.direction() == EFX::Backward);
}

void EFXFixture_Test::loadWrongRoot()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("EFXFixture");

    xmlWriter.writeTextElement("ID", "189");
    xmlWriter.writeTextElement("Direction", "Backward");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    EFX e(m_doc);
    EFXFixture ef(&e);
    QVERIFY(ef.loadXML(xmlReader) == false);
    QVERIFY(!ef.head().isValid());
    QVERIFY(ef.direction() == EFX::Forward);
}

void EFXFixture_Test::loadWrongDirection()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("ID", "97");
    xmlWriter.writeTextElement("Direction", "Phorrwarrd");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    EFX e(m_doc);
    EFXFixture ef(&e);
    QVERIFY(ef.loadXML(xmlReader) == true);
    QVERIFY(ef.head().fxi == 97);
    QVERIFY(ef.direction() == EFX::Forward);
}

void EFXFixture_Test::loadExtraTag()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("ID", "108");
    xmlWriter.writeTextElement("Direction", "Forward");
    xmlWriter.writeTextElement("Foobar", "Just testing");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    EFX e(m_doc);
    EFXFixture ef(&e);
    QVERIFY(ef.loadXML(xmlReader) == true);
    QVERIFY(ef.head().fxi == 108);
    QVERIFY(ef.direction() == EFX::Forward);
}

void EFXFixture_Test::save()
{
    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(56, 7));
    ef.setDirection(EFX::Backward);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("EFX");

    QVERIFY(ef.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "EFX");

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Fixture");

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "ID");
    QVERIFY(xmlReader.readElementText() == "56");

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Head");
    QVERIFY(xmlReader.readElementText() == "7");

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Mode");
    QVERIFY(xmlReader.readElementText() == "0");

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Direction");
    QVERIFY(xmlReader.readElementText() == "Backward");
}

void EFXFixture_Test::serialNumber()
{
    EFX e(m_doc);
    EFXFixture ef(&e);

    ef.setSerialNumber(15);
    QVERIFY(ef.serialNumber() == 15);
}

void EFXFixture_Test::isValid()
{
    EFX e(m_doc);
    EFXFixture ef(&e);

    QVERIFY(ef.isValid() == false);

    ef.setHead(GroupHead(0,0));
    QVERIFY(ef.isValid() == true);
}

void EFXFixture_Test::reset()
{
    EFX e(m_doc);

    EFXFixture* ef1 = new EFXFixture(&e);
    ef1->setHead(GroupHead(1,0));
    ef1->setSerialNumber(0);
    ef1->m_runTimeDirection = EFX::Forward;
    ef1->m_done = true;
    ef1->m_elapsed = 1337;
    e.addFixture(ef1);

    EFXFixture* ef2 = new EFXFixture(&e);
    ef2->setHead(GroupHead(2,0));
    ef2->setSerialNumber(1);
    ef2->m_runTimeDirection = EFX::Forward;
    ef2->m_done = true;
    ef2->m_elapsed = 13;
    e.addFixture(ef2);

    EFXFixture* ef3 = new EFXFixture(&e);
    ef3->setHead(GroupHead(3,0));
    ef3->setSerialNumber(2);
    ef3->setDirection(EFX::Forward);
    ef3->m_runTimeDirection = EFX::Backward;
    ef3->m_done = true;
    ef3->m_elapsed = 69;
    e.addFixture(ef3);

    EFXFixture* ef4 = new EFXFixture(&e);
    ef4->setHead(GroupHead(4,0));
    ef4->setSerialNumber(3);
    ef4->setDirection(EFX::Forward);
    ef4->m_runTimeDirection = EFX::Backward;
    ef4->m_done = true;
    ef4->m_elapsed = 42;
    e.addFixture(ef4);

    ef1->reset();
    QVERIFY(ef1->m_head.fxi == 1);
    QVERIFY(ef1->m_direction == EFX::Forward);
    QVERIFY(ef1->m_serialNumber == 0);
    QVERIFY(ef1->m_runTimeDirection == EFX::Forward);
    QVERIFY(ef1->m_done == false);
    QVERIFY(ef1->m_elapsed == 0);

    ef2->reset();
    QVERIFY(ef2->m_head.fxi == 2);
    QVERIFY(ef2->m_direction == EFX::Forward);
    QVERIFY(ef2->m_serialNumber == 1);
    QVERIFY(ef2->m_runTimeDirection == EFX::Forward);
    QVERIFY(ef2->m_done == false);
    QVERIFY(ef2->m_elapsed == 0);

    ef3->reset();
    QVERIFY(ef3->m_head.fxi == 3);
    QVERIFY(ef3->m_direction == EFX::Forward);
    QVERIFY(ef3->m_serialNumber == 2);
    QVERIFY(ef3->m_runTimeDirection == EFX::Forward);
    QVERIFY(ef3->m_done == false);
    QVERIFY(ef3->m_elapsed == 0);

    ef4->reset();
    QVERIFY(ef4->m_head.fxi == 4);
    QVERIFY(ef4->m_direction == EFX::Forward);
    QVERIFY(ef4->m_serialNumber == 3);
    QVERIFY(ef4->m_runTimeDirection == EFX::Forward);
    QVERIFY(ef4->m_done == false);
    QVERIFY(ef4->m_elapsed == 0);
}

void EFXFixture_Test::startOffset()
{
    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(0,0));

    QCOMPARE(0, ef.startOffset());
    for (int i = 0; i < 360; i += 90)
    {
        ef.setStartOffset(i);
        QCOMPARE(i, ef.startOffset());
    }
}

void EFXFixture_Test::setPoint8bit()
{
    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(m_fixture8bit, 0));

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();

    ef.start(fader);
    ef.setPointPanTilt(ua, fader, 5.4, 1.5); // PMSB: 5, PLSB: 0.4, TMSB: 1 (102), TLSB: 0.5(127)
    QCOMPARE(fader->channels().count(), 2);
    universe->processFaders(MasterTimer::tick());

    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixture8bitAddress + 0], 5);
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixture8bitAddress + 1], 1);
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixture8bitAddress + 2], 0); /* No LSB channels */
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixture8bitAddress + 3], 0); /* No LSB channels */
}

void EFXFixture_Test::setPoint16bit()
{
    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(m_fixture16bit, 0));

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();

    ef.start(fader);
    ef.setPointPanTilt(ua, fader, 5.4, 1.5); // PMSB: 5, PLSB: 0.4, TMSB: 1 (102), TLSB: 0.5(127)
    QCOMPARE(fader->channels().count(), 4);
    universe->processFaders(MasterTimer::tick());
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixture16bitAddress + 0], 5);
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixture16bitAddress + 1], 1);
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixture16bitAddress + 2], 102); /* 255 * 0.4 */
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixture16bitAddress + 3], 127); /* 255 * 0.5 */
}

void EFXFixture_Test::setPointPanOnly()
{
    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(m_fixturePanOnly, 0));

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();

    ef.start(fader);
    ef.setPointPanTilt(ua, fader, 5.4, 1.5); // PMSB: 5, PLSB: 0.4, TMSB: 1 (102), TLSB: 0.5(127)
    QCOMPARE(fader->channels().count(), 1);
    universe->processFaders(MasterTimer::tick());
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixturePanOnlyAddress + 0], 5); /* Pan */
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixturePanOnlyAddress + 1], 0);
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixturePanOnlyAddress + 2], 0);
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixturePanOnlyAddress + 3], 0);
}

void EFXFixture_Test::setPointLedBar()
{
    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(m_fixtureLedBar, 0));

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();

    ef.start(fader);
    ef.setPointPanTilt(ua, fader, 5.4, 1.5); // PMSB: 5, PLSB: 0.4, TMSB: 1 (102), TLSB: 0.5(127)
    QCOMPARE(fader->channels().count(), 1);
    universe->processFaders(MasterTimer::tick());

    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixtureLedBarAddress + 0], 1); /* Tilt */
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixtureLedBarAddress + 1], 0);
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixtureLedBarAddress + 2], 0);
    QCOMPARE((int)(uchar)universe->preGMValues()[m_fixtureLedBarAddress + 3], 0);
}


void EFXFixture_Test::nextStepLoop()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();
    MasterTimerStub mts(m_doc, ua);

    EFX e(m_doc);
    e.setDuration(1000); // 1s

    EFXFixture* ef = new EFXFixture(&e);
    ef->setHead(GroupHead(0,0));
    e.addFixture(ef);

    /* Initialize the EFXFixture so that it can do the math */
    ef->setSerialNumber(0);
    QVERIFY(ef->isValid() == true);
    QVERIFY(ef->isDone() == false);
    QVERIFY(ef->m_elapsed == 0);

    e.preRun(&mts);

    /* Run two cycles (2 * tickms * freq) to see that Loop never quits */
    uint max = (MasterTimer::tick() * MasterTimer::frequency()) + MasterTimer::tick();
    uint i = MasterTimer::tick();
    for (uint times = 0; times < 2; times++)
    {
        for (; i < max; i += MasterTimer::tick())
        {
            ef->nextStep(ua, fader);
            QVERIFY(ef->isDone() == false); // Loop is never ready
            QCOMPARE(ef->m_elapsed, i);
        }

        i = 0; // m_elapsed is zeroed after a full pass
    }

    e.postRun(&mts, ua);
}

void EFXFixture_Test::nextStepLoopZeroDuration()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();
    MasterTimerStub mts(m_doc, ua);

    EFX e(m_doc);
    e.setDuration(0); // 0s

    EFXFixture* ef = new EFXFixture(&e);
    ef->setHead(GroupHead(0,0));
    e.addFixture(ef);

    /* Initialize the EFXFixture so that it can do math */
    ef->setSerialNumber(0);
    QVERIFY(ef->isValid() == true);
    QVERIFY(ef->isDone() == false);
    QVERIFY(ef->m_elapsed == 0);

    e.preRun(&mts);

    /* Run two cycles (2 * tickms * freq) to see that Loop never quits */
    uint max = (MasterTimer::tick() * MasterTimer::frequency()) + MasterTimer::tick();
    uint i = MasterTimer::tick();
    for (uint times = 0; times < 2; times++)
    {
        for (; i < max; i += MasterTimer::tick())
        {
            ef->nextStep(ua, fader);
            QVERIFY(ef->isDone() == false); // Loop is never ready
            QVERIFY(ef->m_elapsed == 0); // elapsed is never increased
        }

        // m_elapsed is NOT zeroed since there are no "rounds" when duration == 0
    }

    e.postRun(&mts, ua);
}

void EFXFixture_Test::nextStepSingleShot()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();
    MasterTimerStub mts(m_doc, ua);

    EFX e(m_doc);
    e.setDuration(1000); // 1s
    e.setRunOrder(EFX::SingleShot);

    EFXFixture* ef = new EFXFixture(&e);
    ef->setHead(GroupHead(0,0));
    e.addFixture(ef);

    /* Initialize the EFXFixture so that it can do math */
    ef->setSerialNumber(0);
    QVERIFY(ef->isValid() == true);
    QVERIFY(ef->isDone() == false);
    QVERIFY(ef->m_elapsed == 0);

    e.preRun(&mts);

    ef->reset();

    /* Run one cycle (50 steps) */
    uint max = (MasterTimer::tick() * MasterTimer::frequency()) + MasterTimer::tick();
    for (uint i = MasterTimer::tick(); i < max; i += MasterTimer::tick())
    {
        ef->nextStep(ua, fader);
        QVERIFY(ef->isDone() == false);
        QCOMPARE(ef->m_elapsed, i);
    }

    ef->nextStep(ua, fader);

    /* Single-shot EFX should now be ready */
    QVERIFY(ef->isDone() == true);

    e.postRun(&mts, ua);
}

void EFXFixture_Test::modeStrings()
{
    QCOMPARE(EFXFixture::modeToString(EFXFixture::PanTilt), QString("Position"));
    QCOMPARE(EFXFixture::modeToString(EFXFixture::Dimmer), QString("Dimmer"));
    QCOMPARE(EFXFixture::modeToString(EFXFixture::RGB), QString("RGB"));
    QCOMPARE(EFXFixture::modeToString(EFXFixture::Mode(42)), QString("Position"));

    QCOMPARE(EFXFixture::stringToMode("Position"), EFXFixture::PanTilt);
    QCOMPARE(EFXFixture::stringToMode("Dimmer"), EFXFixture::Dimmer);
    QCOMPARE(EFXFixture::stringToMode("RGB"), EFXFixture::RGB);
    QCOMPARE(EFXFixture::stringToMode("Foobar"), EFXFixture::PanTilt);

    EFX e(m_doc);
    EFXFixture ef(&e);
    QCOMPARE(ef.mode(), EFXFixture::PanTilt);
    ef.setMode(EFXFixture::RGB);
    QCOMPARE(ef.mode(), EFXFixture::RGB);
    ef.setMode(EFXFixture::Dimmer);
    QCOMPARE(ef.mode(), EFXFixture::Dimmer);
}

void EFXFixture_Test::modeList()
{
    Fixture* full = createFixture("Full", QStringList() << "PanMSB" << "PanLSB" << "TiltMSB" << "TiltLSB"
                                  << "DimMSB" << "DimLSB" << "Red" << "Green" << "Blue", 100);
    Fixture* dimmerOnly = createFixture("DimmerOnly", QStringList() << "DimMSB", 120);
    Fixture* colourOnly = createFixture("ColourOnly", QStringList() << "Colour", 130);

    EFX e(m_doc);

    EFXFixture ef(&e);
    ef.setHead(GroupHead(full->id(), 0));
    QStringList modes = ef.modeList();
    QCOMPARE(modes.size(), 3);
    QVERIFY(modes.contains("Position"));
    QVERIFY(modes.contains("Dimmer"));
    QVERIFY(modes.contains("RGB"));
    // PanTilt is available so the default mode is kept
    QCOMPARE(ef.mode(), EFXFixture::PanTilt);

    EFXFixture ef2(&e);
    ef2.setHead(GroupHead(dimmerOnly->id(), 0));
    QCOMPARE(ef2.modeList(), QStringList() << "Dimmer");
    // PanTilt is not available: the first available mode is chosen
    QCOMPARE(ef2.mode(), EFXFixture::Dimmer);

    EFXFixture ef3(&e);
    ef3.setHead(GroupHead(colourOnly->id(), 0));
    QVERIFY(ef3.modeList().isEmpty());
    // No mode available at all: the mode is left untouched
    QCOMPARE(ef3.mode(), EFXFixture::PanTilt);

    EFXFixture ef4(&e);
    ef4.setHead(GroupHead(12345, 0)); // no such fixture
    QCOMPARE(ef4.head().fxi, quint32(12345));
    QCOMPARE(ef4.universe(), Universe::invalid());
}

void EFXFixture_Test::isValidModes()
{
    Fixture* dimmerOnly = createFixture("DimmerOnly", QStringList() << "DimMSB", 120);
    Fixture* rgbOnly = createFixture("RGBOnly", QStringList() << "Red" << "Green" << "Blue", 130);

    EFX e(m_doc);

    EFXFixture ef(&e);
    ef.setHead(GroupHead(dimmerOnly->id(), 0));
    QCOMPARE(ef.mode(), EFXFixture::Dimmer);
    QVERIFY(ef.isValid() == true);
    ef.setMode(EFXFixture::PanTilt);
    QVERIFY(ef.isValid() == false); // no pan, no tilt
    ef.setMode(EFXFixture::RGB);
    QVERIFY(ef.isValid() == false); // no RGB channels

    EFXFixture ef2(&e);
    ef2.setHead(GroupHead(rgbOnly->id(), 0));
    QCOMPARE(ef2.mode(), EFXFixture::RGB);
    QVERIFY(ef2.isValid() == true);
    ef2.setMode(EFXFixture::Dimmer);
    QVERIFY(ef2.isValid() == false); // no intensity channel
    ef2.setMode(EFXFixture::PanTilt);
    QVERIFY(ef2.isValid() == false);

    // Head index beyond the fixture's heads
    EFXFixture ef3(&e);
    ef3.setHead(GroupHead(rgbOnly->id(), 7));
    QVERIFY(ef3.isValid() == false);

    // 8bit scanner in Dimmer mode: has an intensity channel
    EFXFixture ef4(&e);
    ef4.setHead(GroupHead(m_fixture8bit, 0));
    ef4.setMode(EFXFixture::Dimmer);
    QVERIFY(ef4.isValid() == true);
}

void EFXFixture_Test::loadModeAndLegacyTags()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");
    xmlWriter.writeTextElement("ID", QString::number(m_fixture8bit));
    xmlWriter.writeTextElement("Head", "0");
    xmlWriter.writeTextElement("Mode", QString::number(int(EFXFixture::Dimmer)));
    xmlWriter.writeTextElement("Direction", "Forward");
    xmlWriter.writeTextElement("StartOffset", "45");
    xmlWriter.writeTextElement("Intensity", "0.5"); // legacy tag, skipped
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    EFX e(m_doc);
    EFXFixture ef(&e);
    QVERIFY(ef.loadXML(xmlReader) == true);
    QCOMPARE(ef.head().fxi, quint32(m_fixture8bit));
    QCOMPARE(ef.head().head, 0);
    QCOMPARE(ef.mode(), EFXFixture::Dimmer);
    QCOMPARE(ef.direction(), Function::Forward);
    QCOMPARE(ef.startOffset(), 45);

    // Save and reload the mode through the XML round trip
    QBuffer saveBuffer;
    saveBuffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter saveWriter(&saveBuffer);
    QVERIFY(ef.saveXML(&saveWriter) == true);
    saveWriter.setDevice(NULL);
    saveBuffer.close();

    saveBuffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader saveReader(&saveBuffer);
    saveReader.readNextStartElement();
    EFXFixture ef2(&e);
    QVERIFY(ef2.loadXML(saveReader) == true);
    QCOMPARE(ef2.mode(), EFXFixture::Dimmer);
    QCOMPARE(ef2.startOffset(), 45);
    QCOMPARE(ef2.head().fxi, quint32(m_fixture8bit));
}

void EFXFixture_Test::timeOffsetPropagation()
{
    EFX e(m_doc);
    e.setDuration(1000);
    QVERIFY(e.addFixture(m_fixture8bit, 0) == true);
    QVERIFY(e.addFixture(m_fixture16bit, 0) == true);
    QVERIFY(e.addFixture(m_fixturePanOnly, 0) == true);
    QCOMPARE(e.fixtures().size(), 3);

    for (int i = 0; i < e.fixtures().size(); i++)
        e.fixtures().at(i)->setSerialNumber(i);

    // Parallel: nobody waits
    QCOMPARE(e.propagationMode(), EFX::Parallel);
    QCOMPARE(e.fixtures().at(0)->timeOffset(), uint(0));
    QCOMPARE(e.fixtures().at(2)->timeOffset(), uint(0));

    // Serial: loopDuration / (fixtures + 1) * serialNumber
    e.setPropagationMode(EFX::Serial);
    QCOMPARE(e.fixtures().at(0)->timeOffset(), uint(0));
    QCOMPARE(e.fixtures().at(1)->timeOffset(), uint(250));
    QCOMPARE(e.fixtures().at(2)->timeOffset(), uint(500));

    // Asymmetric: same offsets
    e.setPropagationMode(EFX::Asymmetric);
    QCOMPARE(e.fixtures().at(1)->timeOffset(), uint(250));
    QCOMPARE(e.fixtures().at(2)->timeOffset(), uint(500));
}

void EFXFixture_Test::durationChanged()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();
    MasterTimerStub mts(m_doc, ua);

    EFX e(m_doc);
    e.setDuration(1000);

    EFXFixture* ef = new EFXFixture(&e);
    ef->setHead(GroupHead(m_fixture8bit, 0));
    e.addFixture(ef);
    EFXFixture* ef2 = new EFXFixture(&e);
    ef2->setHead(GroupHead(m_fixture16bit, 0));
    e.addFixture(ef2);

    e.preRun(&mts);
    QCOMPARE(ef->serialNumber(), 0);
    QCOMPARE(ef2->serialNumber(), 1);

    // Nothing has elapsed yet: nothing to rescale
    ef->durationChanged();
    QCOMPARE(ef->m_elapsed, uint(0));

    // Run a quarter of the loop
    uint ticks = 250 / MasterTimer::tick();
    for (uint i = 0; i < ticks; i++)
        ef->nextStep(ua, fader);
    QCOMPARE(ef->m_elapsed, ticks * MasterTimer::tick());

    // Doubling the duration doubles the elapsed time (same angle)
    e.setDuration(2000);
    QVERIFY(qAbs(int(ef->m_elapsed) - int(2 * ticks * MasterTimer::tick())) <= 1);

    // Serial propagation: the offset of the second fixture is subtracted
    e.setPropagationMode(EFX::Serial);
    ef2->reset();
    uint offset = ef2->timeOffset();
    QCOMPARE(offset, uint(2000 / 3));
    for (uint i = 0; i < (offset / MasterTimer::tick()) + 5; i++)
        ef2->nextStep(ua, fader);
    QVERIFY(ef2->m_started == true);
    uint elapsedBefore = ef2->m_elapsed;
    ef2->durationChanged();
    QVERIFY(qAbs(int(ef2->m_elapsed) - int(elapsedBefore)) <= 1);

    // Elapsed time smaller than the offset wraps around the loop
    ef2->m_currentAngle = 0.1;
    ef2->durationChanged();
    QVERIFY(ef2->m_elapsed > 0);
    QVERIFY(ef2->m_elapsed < e.loopDuration());

    e.postRun(&mts, ua);
}

void EFXFixture_Test::setPoint16bitSecondary()
{
    Fixture* fxi = createFixture("Contiguous16", QStringList() << "PanMSB" << "PanLSB" << "TiltMSB" << "TiltLSB", 100);

    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(fxi->id(), 0));

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();
    fader->setHandleSecondary(true);

    ef.start(fader);
    QVERIFY(fader->handleSecondary() == true); // channels are contiguous
    ef.setPointPanTilt(ua, fader, 5.4, 1.5); // PMSB: 5, PLSB: 0.4, TMSB: 1 (102), TLSB: 0.5(127)
    QCOMPARE(fader->channelsCount(), 2); // 2 x 16bit channels
    universe->processFaders(MasterTimer::tick());
    QCOMPARE((int)(uchar)universe->preGMValues()[100], 5);
    QCOMPARE((int)(uchar)universe->preGMValues()[101], 102);
    QCOMPARE((int)(uchar)universe->preGMValues()[102], 1);
    QCOMPARE((int)(uchar)universe->preGMValues()[103], 127);

    // Outbound (negative) values are clamped to zero
    ef.setPointPanTilt(ua, fader, -3.0, -1.0);
    universe->processFaders(MasterTimer::tick());
    QCOMPARE((int)(uchar)universe->preGMValues()[100], 0);
    QCOMPARE((int)(uchar)universe->preGMValues()[101], 0);
    QCOMPARE((int)(uchar)universe->preGMValues()[102], 0);
    QCOMPARE((int)(uchar)universe->preGMValues()[103], 0);

    // Relative EFX: the fader channels get the Relative flag
    e.setIsRelative(true);
    ef.setPointPanTilt(ua, fader, 2.0, 3.0);
    foreach (FadeChannel fc, fader->channels())
        QVERIFY(fc.flags() & FadeChannel::Relative);
}

void EFXFixture_Test::setPointNonContiguousLsb()
{
    Fixture* fxi = createFixture("NonContiguous16", QStringList() << "PanMSB" << "TiltMSB" << "PanLSB" << "TiltLSB", 100);

    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(fxi->id(), 0));

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();
    fader->setHandleSecondary(true);

    ef.start(fader);
    QVERIFY(fader->handleSecondary() == false); // LSB channels are not adjacent
    ef.setPointPanTilt(ua, fader, 5.4, 1.5);
    QCOMPARE(fader->channelsCount(), 4);
    universe->processFaders(MasterTimer::tick());
    QCOMPARE((int)(uchar)universe->preGMValues()[100], 5);
    QCOMPARE((int)(uchar)universe->preGMValues()[101], 1);
    QCOMPARE((int)(uchar)universe->preGMValues()[102], 102);
    QCOMPARE((int)(uchar)universe->preGMValues()[103], 127);
}

void EFXFixture_Test::setPointDimmer()
{
    Fixture* fxi = createFixture("Dimmer16", QStringList() << "DimMSB" << "DimLSB", 200);
    Fixture* fxi8 = createFixture("Dimmer8", QStringList() << "DimMSB", 210);
    Fixture* fxiNc = createFixture("Dimmer16NonContiguous", QStringList() << "DimMSB" << "Colour" << "DimLSB", 220);

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];

    EFX e(m_doc);

    // 16bit dimmer with secondary channel handling
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxi->id(), 0));
        QCOMPARE(ef.mode(), EFXFixture::Dimmer);

        QSharedPointer<GenericFader> fader = universe->requestFader();
        fader->setHandleSecondary(true);
        ef.start(fader);
        QVERIFY(fader->handleSecondary() == true);
        QCOMPARE(ef.m_firstMsbChannel, quint32(0));
        QCOMPARE(ef.m_firstLsbChannel, quint32(1));

        ef.setPointDimmer(ua, fader, 100.5);
        QCOMPARE(fader->channelsCount(), 1);
        universe->processFaders(MasterTimer::tick());
        QCOMPARE((int)(uchar)universe->preGMValues()[200], 100);
        QCOMPARE((int)(uchar)universe->preGMValues()[201], 127);
        universe->dismissFader(fader);
    }

    // 16bit dimmer without secondary channel handling: only the MSB is written
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxi->id(), 0));

        QSharedPointer<GenericFader> fader = universe->requestFader();
        ef.start(fader);
        ef.setPointDimmer(ua, fader, 42.9);
        QCOMPARE(fader->channelsCount(), 1);
        universe->processFaders(MasterTimer::tick());
        QCOMPARE((int)(uchar)universe->preGMValues()[200], 42);
        universe->dismissFader(fader);
    }

    // 8bit dimmer
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxi8->id(), 0));

        QSharedPointer<GenericFader> fader = universe->requestFader();
        fader->setHandleSecondary(true);
        ef.start(fader);
        QCOMPARE(ef.m_firstLsbChannel, QLCChannel::invalid());
        ef.setPointDimmer(ua, fader, 77.0);
        QCOMPARE(fader->channelsCount(), 1);
        universe->processFaders(MasterTimer::tick());
        QCOMPARE((int)(uchar)universe->preGMValues()[210], 77);
        universe->dismissFader(fader);
    }

    // 16bit dimmer with non-contiguous LSB: secondary handling gets disabled
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxiNc->id(), 0));

        QSharedPointer<GenericFader> fader = universe->requestFader();
        fader->setHandleSecondary(true);
        ef.start(fader);
        QVERIFY(fader->handleSecondary() == false);
        ef.setPointDimmer(ua, fader, 33.0);
        universe->processFaders(MasterTimer::tick());
        QCOMPARE((int)(uchar)universe->preGMValues()[220], 33);
        universe->dismissFader(fader);
    }
}

void EFXFixture_Test::setPointDimmerMaster()
{
    // Pan & tilt in the head, dimmer outside of it -> master intensity channel
    Fixture* fxi = createFixture("MasterDimmer", QStringList() << "PanMSB" << "TiltMSB" << "DimMSB", 240,
                                 QList<int>() << 0 << 1);
    QCOMPARE(fxi->masterIntensityChannel(), quint32(2));
    QCOMPARE(fxi->channelNumber(QLCChannel::Intensity, QLCChannel::MSB, 0), QLCChannel::invalid());

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];

    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(fxi->id(), 0));
    QCOMPARE(ef.mode(), EFXFixture::PanTilt);
    QVERIFY(ef.modeList().contains("Dimmer"));
    ef.setMode(EFXFixture::Dimmer);
    QVERIFY(ef.isValid() == true);

    QSharedPointer<GenericFader> fader = universe->requestFader();
    ef.start(fader);
    QCOMPARE(ef.m_firstMsbChannel, quint32(2));
    ef.setPointDimmer(ua, fader, 66.0);
    universe->processFaders(MasterTimer::tick());
    QCOMPARE((int)(uchar)universe->preGMValues()[242], 66);
}

void EFXFixture_Test::setPointIntensity()
{
    Fixture* fxi = createFixture("PanTiltDimmer16", QStringList() << "PanMSB" << "PanLSB" << "TiltMSB" << "TiltLSB"
                                 << "DimMSB" << "DimLSB", 300);
    Fixture* fxiMaster = createFixture("PanTiltMasterDimmer", QStringList() << "PanMSB" << "TiltMSB" << "DimMSB", 320,
                                       QList<int>() << 0 << 1);

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];

    EFX e(m_doc);
    e.setDimmerControlEnabled(true);

    // Dimmer control disabled: no intensity channel is cached
    {
        EFX e2(m_doc);
        EFXFixture ef(&e2);
        ef.setHead(GroupHead(fxi->id(), 0));
        QSharedPointer<GenericFader> fader = universe->requestFader();
        ef.start(fader);
        QCOMPARE(ef.m_intensityMsbChannel, QLCChannel::invalid());
        ef.setPointIntensity(ua, fader, 1.0);
        QCOMPARE(fader->channelsCount(), 0);
        universe->dismissFader(fader);
    }

    // 16bit intensity with secondary channel handling
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxi->id(), 0));
        QSharedPointer<GenericFader> fader = universe->requestFader();
        fader->setHandleSecondary(true);
        ef.start(fader);
        QCOMPARE(ef.m_intensityMsbChannel, quint32(4));
        QCOMPARE(ef.m_intensityLsbChannel, quint32(5));
        ef.setPointIntensity(ua, fader, 0.5);
        QCOMPARE(fader->channelsCount(), 1);
        universe->processFaders(MasterTimer::tick());
        QCOMPARE((int)(uchar)universe->preGMValues()[304], 127);
        QCOMPARE((int)(uchar)universe->preGMValues()[305], 127);
        universe->dismissFader(fader);
    }

    // 16bit intensity without secondary channel handling: MSB only
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxi->id(), 0));
        QSharedPointer<GenericFader> fader = universe->requestFader();
        ef.start(fader);
        ef.setPointIntensity(ua, fader, 1.0);
        QCOMPARE(fader->channelsCount(), 1);
        universe->processFaders(MasterTimer::tick());
        QCOMPARE((int)(uchar)universe->preGMValues()[304], 255);
        universe->dismissFader(fader);
    }

    // Master intensity channel outside of the head
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxiMaster->id(), 0));
        QSharedPointer<GenericFader> fader = universe->requestFader();
        ef.start(fader);
        QCOMPARE(ef.m_intensityMsbChannel, quint32(2));
        QCOMPARE(ef.m_intensityLsbChannel, QLCChannel::invalid());
        ef.setPointIntensity(ua, fader, 0.2);
        universe->processFaders(MasterTimer::tick());
        QCOMPARE((int)(uchar)universe->preGMValues()[322], 51);
        universe->dismissFader(fader);
    }

    // Pan-only fixture: no intensity at all
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(m_fixturePanOnly, 0));
        QSharedPointer<GenericFader> fader = universe->requestFader();
        ef.start(fader);
        ef.setPointIntensity(ua, fader, 1.0);
        QCOMPARE(fader->channelsCount(), 0);
        universe->dismissFader(fader);
    }
}

void EFXFixture_Test::setPointRGB()
{
    Fixture* fxi = createFixture("RGB", QStringList() << "Red" << "Green" << "Blue", 400);
    Fixture* fxiDim = createFixture("Dimmer8", QStringList() << "DimMSB", 410);

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];

    EFX e(m_doc);

    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxi->id(), 0));
        QCOMPARE(ef.mode(), EFXFixture::RGB);
        QVERIFY(ef.isValid() == true);

        QSharedPointer<GenericFader> fader = universe->requestFader();
        ef.start(fader);
        QVERIFY(ef.m_started == true);

        ef.setPointRGB(ua, fader, 0, 0);
        QCOMPARE(fader->channelsCount(), 3);
        universe->processFaders(MasterTimer::tick());
        QColor pixel = EFXFixture::m_rgbGradient.pixel(0, 0);
        QCOMPARE((int)(uchar)universe->preGMValues()[400], pixel.red());
        QCOMPARE((int)(uchar)universe->preGMValues()[401], pixel.green());
        QCOMPARE((int)(uchar)universe->preGMValues()[402], pixel.blue());

        ef.setPointRGB(ua, fader, 200, 100);
        universe->processFaders(MasterTimer::tick());
        pixel = EFXFixture::m_rgbGradient.pixel(200, 100);
        QCOMPARE((int)(uchar)universe->preGMValues()[400], pixel.red());
        QCOMPARE((int)(uchar)universe->preGMValues()[401], pixel.green());
        QCOMPARE((int)(uchar)universe->preGMValues()[402], pixel.blue());
        universe->dismissFader(fader);
    }

    // A fixture without RGB channels writes nothing
    {
        EFXFixture ef(&e);
        ef.setHead(GroupHead(fxiDim->id(), 0));
        ef.setMode(EFXFixture::RGB);
        QSharedPointer<GenericFader> fader = universe->requestFader();
        ef.setPointRGB(ua, fader, 10, 10);
        QCOMPARE(fader->channelsCount(), 0);
        universe->dismissFader(fader);
    }
}

void EFXFixture_Test::setPointNullFader()
{
    EFX e(m_doc);
    EFXFixture ef(&e);
    ef.setHead(GroupHead(m_fixture16bit, 0));

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    QSharedPointer<GenericFader> nullFader;

    ef.setPointPanTilt(ua, nullFader, 1.0, 2.0);
    ef.setPointDimmer(ua, nullFader, 1.0);
    ef.setPointIntensity(ua, nullFader, 1.0);
    ef.setPointRGB(ua, nullFader, 1.0, 2.0);

    // Nothing has been written
    for (int i = m_fixture16bitAddress; i < m_fixture16bitAddress + 4; i++)
        QCOMPARE((int)ua[0]->preGMValues()[i], 0);
}

void EFXFixture_Test::nextStepPingPong()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();
    MasterTimerStub mts(m_doc, ua);

    EFX e(m_doc);
    e.setDuration(MasterTimer::tick() * 5);
    e.setRunOrder(Function::PingPong);

    EFXFixture* ef = new EFXFixture(&e);
    ef->setHead(GroupHead(m_fixture8bit, 0));
    e.addFixture(ef);

    e.preRun(&mts);
    QCOMPARE(ef->m_runTimeDirection, Function::Forward);

    // Run one full loop: direction stays forward until the loop wraps
    for (uint i = 0; i < 5; i++)
    {
        ef->nextStep(ua, fader);
        QCOMPARE(ef->m_runTimeDirection, Function::Forward);
    }
    ef->nextStep(ua, fader);
    QCOMPARE(ef->m_runTimeDirection, Function::Backward);
    QCOMPARE(ef->m_elapsed, uint(0));
    QVERIFY(ef->isDone() == false);

    // ...and back again
    for (uint i = 0; i < 5; i++)
    {
        ef->nextStep(ua, fader);
        QCOMPARE(ef->m_runTimeDirection, Function::Backward);
    }
    ef->nextStep(ua, fader);
    QCOMPARE(ef->m_runTimeDirection, Function::Forward);

    // A stopped fixture is restarted at the next step
    ef->stop();
    QVERIFY(ef->m_started == false);
    ef->nextStep(ua, fader);
    QVERIFY(ef->m_started == true);

    e.postRun(&mts, ua);
    QCOMPARE(ef->m_runTimeDirection, Function::Forward);
    QCOMPARE(ef->m_elapsed, uint(0));
}

void EFXFixture_Test::nextStepSerial()
{
    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    QSharedPointer<GenericFader> fader = universe->requestFader();
    MasterTimerStub mts(m_doc, ua);

    EFX e(m_doc);
    e.setDuration(MasterTimer::tick() * 30);
    e.setPropagationMode(EFX::Serial);

    EFXFixture* ef1 = new EFXFixture(&e);
    ef1->setHead(GroupHead(m_fixture8bit, 0));
    e.addFixture(ef1);
    EFXFixture* ef2 = new EFXFixture(&e);
    ef2->setHead(GroupHead(m_fixture16bit, 0));
    e.addFixture(ef2);

    e.preRun(&mts);
    QCOMPARE(ef2->serialNumber(), 1);
    uint offset = ef2->timeOffset();
    QCOMPARE(offset, uint(MasterTimer::tick() * 10));

    // The first fixture starts immediately
    ef1->nextStep(ua, fader);
    QVERIFY(ef1->m_started == true);

    // The second one waits for its turn
    for (uint i = MasterTimer::tick(); i < offset; i += MasterTimer::tick())
    {
        ef2->nextStep(ua, fader);
        QVERIFY(ef2->m_started == false);
        QCOMPARE(ef2->m_elapsed, i);
    }
    ef2->nextStep(ua, fader);
    QVERIFY(ef2->m_started == true);

    // Once started, it keeps running even below the offset (after wrapping)
    for (uint i = 0; i < 40; i++)
        ef2->nextStep(ua, fader);
    QVERIFY(ef2->m_started == true);

    e.postRun(&mts, ua);
}

void EFXFixture_Test::nextStepModes()
{
    Fixture* rgb = createFixture("RGB", QStringList() << "Red" << "Green" << "Blue", 400);
    Fixture* dimmer = createFixture("Dimmer16", QStringList() << "DimMSB" << "DimLSB", 200);
    Fixture* mover = createFixture("PanTiltDimmer", QStringList() << "PanMSB" << "TiltMSB" << "DimMSB", 300);

    QList<Universe*> ua = m_doc->inputOutputMap()->universes();
    Universe *universe = ua[0];
    MasterTimerStub mts(m_doc, ua);

    // RGB mode
    {
        QSharedPointer<GenericFader> fader = universe->requestFader();
        EFX e(m_doc);
        e.setDuration(1000);
        EFXFixture* ef = new EFXFixture(&e);
        ef->setHead(GroupHead(rgb->id(), 0));
        QCOMPARE(ef->mode(), EFXFixture::RGB);
        e.addFixture(ef);
        e.preRun(&mts);
        for (int i = 0; i < 5; i++)
            ef->nextStep(ua, fader);
        QCOMPARE(fader->channelsCount(), 3);
        universe->processFaders(MasterTimer::tick());
        int sum = (int)(uchar)universe->preGMValues()[400] + (int)(uchar)universe->preGMValues()[401] + (int)(uchar)universe->preGMValues()[402];
        QVERIFY(sum > 0);
        e.postRun(&mts, ua);
        universe->dismissFader(fader);
    }

    // Dimmer mode
    {
        QSharedPointer<GenericFader> fader = universe->requestFader();
        fader->setHandleSecondary(true);
        EFX e(m_doc);
        e.setDuration(1000);
        EFXFixture* ef = new EFXFixture(&e);
        ef->setHead(GroupHead(dimmer->id(), 0));
        QCOMPARE(ef->mode(), EFXFixture::Dimmer);
        e.addFixture(ef);
        e.preRun(&mts);
        for (int i = 0; i < 5; i++)
            ef->nextStep(ua, fader);
        QCOMPARE(fader->channelsCount(), 1);
        e.postRun(&mts, ua);
        universe->dismissFader(fader);
    }

    // Invalid fixture (no such mode channels): nextStep bails out early
    {
        QSharedPointer<GenericFader> fader = universe->requestFader();
        EFX e(m_doc);
        e.setDuration(1000);
        EFXFixture* ef = new EFXFixture(&e);
        ef->setHead(GroupHead(dimmer->id(), 0));
        ef->setMode(EFXFixture::PanTilt);
        QVERIFY(ef->isValid() == false);
        e.addFixture(ef);
        e.preRun(&mts);
        ef->nextStep(ua, fader);
        QCOMPARE(ef->m_elapsed, uint(0));
        QVERIFY(ef->m_started == false);
        QCOMPARE(fader->channelsCount(), 0);
        e.postRun(&mts, ua);
        universe->dismissFader(fader);
    }

    // PanTilt mode with dimmer control
    {
        QSharedPointer<GenericFader> fader = universe->requestFader();
        EFX e(m_doc);
        e.setDuration(1000);
        e.setDimmerControlEnabled(true);
        EFXFixture* ef = new EFXFixture(&e);
        ef->setHead(GroupHead(mover->id(), 0));
        QCOMPARE(ef->mode(), EFXFixture::PanTilt);
        e.addFixture(ef);
        e.preRun(&mts);
        for (int i = 0; i < 5; i++)
            ef->nextStep(ua, fader);
        QCOMPARE(ef->m_intensityMsbChannel, quint32(2));
        QCOMPARE(fader->channelsCount(), 3); // pan, tilt, dimmer
        e.postRun(&mts, ua);
        universe->dismissFader(fader);
    }
}

QTEST_APPLESS_MAIN(EFXFixture_Test)
