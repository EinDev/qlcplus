/*
  Q Light Controller - Unit test
  universe_test.cpp

  Copyright (c) Heikki Junnila

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
#include <sys/time.h>

#include "universe_test.h"

#define protected public
#define private public
#include "universe.h"
#undef private
#undef protected

#include "channelmodifier.h"
#include "genericfader.h"
#include "fadechannel.h"
#include "grandmaster.h"
#include "iopluginstub.h"
#include "outputpatch.h"
#include "inputpatch.h"

void Universe_Test::init()
{
    m_gm = new GrandMaster(this);
    m_uni = new Universe(0, m_gm, this);
    // A plugin instance the patch tests can bind to without loading the
    // stub DLL through a Doc. It must outlive the universe, whose patches
    // close their plugin lines on destruction.
    m_stub = new IOPluginStub();
    m_stub->init();
}

void Universe_Test::cleanup()
{
    delete m_uni; m_uni = 0;
    delete m_gm; m_gm = 0;
    delete m_stub; m_stub = 0;
}

void Universe_Test::initial()
{
    QCOMPARE(m_uni->name(), QString("Universe 1"));
    QCOMPARE(m_uni->id(), quint32(0));
    QCOMPARE(m_uni->usedChannels(), ushort(0));
    QCOMPARE(m_uni->totalChannels(), ushort(0));
    QCOMPARE(m_uni->hasChanged(), false);
    QCOMPARE(m_uni->passthrough(), false);
    QVERIFY(m_uni->inputPatch() == NULL);
    QVERIFY(m_uni->outputPatch(0) == NULL);
    QVERIFY(m_uni->feedbackPatch() == NULL);
    QVERIFY(m_uni->intensityChannels().isEmpty());

    QByteArray const preGM = m_uni->preGMValues();

    QCOMPARE(preGM.length(), 512);

    QByteArray const *postGM = m_uni->postGMValues();
    QVERIFY(postGM != NULL);
    QCOMPARE(postGM->length(), 512);

    for (ushort i = 0; i < 512; ++i)
    {
        QVERIFY(m_uni->channelCapabilities(i) == Universe::Undefined);
        QCOMPARE(int(preGM.at(i)), 0);
        QCOMPARE(int(postGM->at(i)), 0);
    }
}

void Universe_Test::channelCapabilities()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(1, QLCChannel::Intensity);
    m_uni->setChannelCapability(2, QLCChannel::Pan);
    m_uni->setChannelCapability(3, QLCChannel::Tilt, Universe::HTP);
    m_uni->setChannelCapability(4, QLCChannel::Intensity);

    QVERIFY(m_uni->channelCapabilities(0) == (Universe::Intensity|Universe::HTP));
    QVERIFY(m_uni->channelCapabilities(1) == (Universe::Intensity|Universe::HTP));
    QVERIFY(m_uni->channelCapabilities(2) == Universe::LTP);
    QVERIFY(m_uni->channelCapabilities(3) == Universe::HTP);
    QVERIFY(m_uni->channelCapabilities(4) == (Universe::Intensity|Universe::HTP));
    QCOMPARE(m_uni->totalChannels(), ushort(5));
}

void Universe_Test::blendModes()
{
    QVERIFY(Universe::blendModeToString(Universe::NormalBlend) == "Normal");
    QVERIFY(Universe::blendModeToString(Universe::MaskBlend) == "Mask");
    QVERIFY(Universe::blendModeToString(Universe::AdditiveBlend) == "Additive");
    QVERIFY(Universe::blendModeToString(Universe::SubtractiveBlend) == "Subtractive");

    QVERIFY(Universe::stringToBlendMode("Foo") == Universe::NormalBlend);
    QVERIFY(Universe::stringToBlendMode("Normal") == Universe::NormalBlend);
    QVERIFY(Universe::stringToBlendMode("Mask") == Universe::MaskBlend);
    QVERIFY(Universe::stringToBlendMode("Additive") == Universe::AdditiveBlend);
    QVERIFY(Universe::stringToBlendMode("Subtractive") == Universe::SubtractiveBlend);

    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(4, QLCChannel::Intensity);
    m_uni->setChannelCapability(9, QLCChannel::Intensity);
    m_uni->setChannelCapability(11, QLCChannel::Intensity);

    QVERIFY(m_uni->write(0, 255) == true);
    QVERIFY(m_uni->write(4, 128) == true);
    QVERIFY(m_uni->write(9, 100) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(255));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(128));
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(100));
    QCOMPARE(quint8(m_uni->postGMValues()->at(11)), quint8(0));

    /* check masking on 0 remains 0 */
    QVERIFY(m_uni->writeBlended(11, 128, 1, Universe::MaskBlend) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(11)), quint8(0));

    /* check 180 masked on 128 gets halved */
    QVERIFY(m_uni->writeBlended(4, 180, 1, Universe::MaskBlend) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(90));

    /* chek adding 50 to 100 is actually 150 */
    QVERIFY(m_uni->writeBlended(9, 50, 1, Universe::AdditiveBlend) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(150));

    /* chek subtracting 55 to 255 is actually 200 */
    QVERIFY(m_uni->writeBlended(0, 55, 1, Universe::SubtractiveBlend) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(200));

    QVERIFY(m_uni->writeBlended(0, 255, 1, Universe::SubtractiveBlend) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(0));

    /* check an unknown blend mode */
    QVERIFY(m_uni->writeBlended(9, 255, 1, Universe::BlendMode(42)) == false);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(150));
}

void Universe_Test::grandMasterIntensityReduce()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(1, QLCChannel::Intensity);
    m_uni->setChannelCapability(2, QLCChannel::Pan);
    m_uni->setChannelCapability(3, QLCChannel::Tilt);
    m_uni->setChannelCapability(4, QLCChannel::Intensity);
    QCOMPARE(m_uni->usedChannels(), ushort(0));
    QCOMPARE(m_uni->totalChannels(), ushort(5));

    m_uni->write(0, 10);
    m_uni->write(1, 20);
    m_uni->write(2, 30);
    m_uni->write(3, 40);
    m_uni->write(4, 50);

    m_gm->setValue(63);
    QCOMPARE(int(m_uni->postGMValues()->at(0)), int(2));
    QCOMPARE(int(m_uni->postGMValues()->at(1)), int(5));
    QCOMPARE(int(m_uni->postGMValues()->at(2)), int(30));
    QCOMPARE(int(m_uni->postGMValues()->at(3)), int(40));
    QCOMPARE(int(m_uni->postGMValues()->at(4)), int(12));
    QCOMPARE(m_uni->usedChannels(), ushort(5));
}

void Universe_Test::grandMasterIntensityLimit()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(1, QLCChannel::Intensity);
    m_uni->setChannelCapability(2, QLCChannel::Pan);
    m_uni->setChannelCapability(3, QLCChannel::Tilt);
    m_uni->setChannelCapability(4, QLCChannel::Intensity);
    QCOMPARE(m_uni->usedChannels(), ushort(0));
    QCOMPARE(m_uni->totalChannels(), ushort(5));

    m_uni->write(0, 10);
    m_uni->write(1, 20);
    m_uni->write(2, 30);
    m_uni->write(3, 40);
    m_uni->write(4, 50);

    m_gm->setValueMode(GrandMaster::Limit);

    m_gm->setValue(63);
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(10));
    QCOMPARE(quint8(m_uni->postGMValues()->at(1)), quint8(20));
    QCOMPARE(quint8(m_uni->postGMValues()->at(2)), quint8(30));
    QCOMPARE(quint8(m_uni->postGMValues()->at(3)), quint8(40));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(50));

    m_gm->setValue(5);
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(5));
    QCOMPARE(quint8(m_uni->postGMValues()->at(1)), quint8(5));
    QCOMPARE(quint8(m_uni->postGMValues()->at(2)), quint8(30));
    QCOMPARE(quint8(m_uni->postGMValues()->at(3)), quint8(40));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(5));
    QCOMPARE(m_uni->usedChannels(), ushort(5));
}

void Universe_Test::grandMasterAllChannelsReduce()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(1, QLCChannel::Intensity);
    m_uni->setChannelCapability(2, QLCChannel::Pan);
    m_uni->setChannelCapability(3, QLCChannel::Tilt);
    m_uni->setChannelCapability(4, QLCChannel::Intensity);
    QCOMPARE(m_uni->usedChannels(), ushort(0));
    QCOMPARE(m_uni->totalChannels(), ushort(5));

    m_uni->write(0, 10);
    m_uni->write(1, 20);
    m_uni->write(2, 30);
    m_uni->write(3, 40);
    m_uni->write(4, 50);

    m_gm->setChannelMode(GrandMaster::AllChannels);

    m_gm->setValue(63);
    QCOMPARE(int(m_uni->postGMValues()->at(0)), int(2));
    QCOMPARE(int(m_uni->postGMValues()->at(1)), int(5));
    QCOMPARE(int(m_uni->postGMValues()->at(2)), int(7));
    QCOMPARE(int(m_uni->postGMValues()->at(3)), int(10));
    QCOMPARE(int(m_uni->postGMValues()->at(4)), int(12));
    QCOMPARE(m_uni->usedChannels(), ushort(5));
}

void Universe_Test::grandMasterAllChannelsLimit()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(1, QLCChannel::Intensity);
    m_uni->setChannelCapability(2, QLCChannel::Pan);
    m_uni->setChannelCapability(3, QLCChannel::Tilt);
    m_uni->setChannelCapability(4, QLCChannel::Intensity);
    QCOMPARE(m_uni->usedChannels(), ushort(0));
    QCOMPARE(m_uni->totalChannels(), ushort(5));

    m_uni->write(0, 10);
    m_uni->write(1, 20);
    m_uni->write(2, 30);
    m_uni->write(3, 40);
    m_uni->write(4, 50);

    m_gm->setChannelMode(GrandMaster::AllChannels);
    m_gm->setValueMode(GrandMaster::Limit);

    m_gm->setValue(63);
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(10));
    QCOMPARE(quint8(m_uni->postGMValues()->at(1)), quint8(20));
    QCOMPARE(quint8(m_uni->postGMValues()->at(2)), quint8(30));
    QCOMPARE(quint8(m_uni->postGMValues()->at(3)), quint8(40));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(50));

    m_gm->setValue(5);
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(5));
    QCOMPARE(quint8(m_uni->postGMValues()->at(1)), quint8(5));
    QCOMPARE(quint8(m_uni->postGMValues()->at(2)), quint8(5));
    QCOMPARE(quint8(m_uni->postGMValues()->at(3)), quint8(5));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(5));
    QCOMPARE(m_uni->usedChannels(), ushort(5));
}

void Universe_Test::applyGM()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(1, QLCChannel::Pan);

    for (int i = 0; i < 256; ++i)
    {
        QCOMPARE(m_uni->applyGM(0, i), uchar(i));
        QCOMPARE(m_uni->applyGM(1, i), uchar(i));
    }

    m_gm->setValue(127);
    for (int i = 0; i < 256; ++i)
    {
        QCOMPARE(m_uni->applyGM(0, i), uchar(i/2));
        QCOMPARE(m_uni->applyGM(1, i), uchar(i));
    }

    m_gm->setChannelMode(GrandMaster::AllChannels);
    for (int i = 0; i < 256; ++i)
    {
        QCOMPARE(m_uni->applyGM(0, i), uchar(i/2));
        QCOMPARE(m_uni->applyGM(1, i), uchar(i/2));
    }

    m_gm->setValueMode(GrandMaster::Limit);
    m_gm->setChannelMode(GrandMaster::Intensity);
    for (int i = 0; i < 256; ++i)
    {
        QCOMPARE(m_uni->applyGM(0, i), uchar(i < 127 ? i : 127));
        QCOMPARE(m_uni->applyGM(1, i), uchar(i));
    }

    m_gm->setChannelMode(GrandMaster::AllChannels);
    for (int i = 0; i < 256; ++i)
    {
        QCOMPARE(m_uni->applyGM(0, i), uchar(i < 127 ? i : 127));
        QCOMPARE(m_uni->applyGM(1, i), uchar(i < 127 ? i : 127));
    }
}

void Universe_Test::write()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(4, QLCChannel::Intensity);
    m_uni->setChannelCapability(9, QLCChannel::Intensity);
    m_uni->setChannelCapability(UNIVERSE_SIZE - 1, QLCChannel::Intensity);

    QVERIFY(m_uni->write(UNIVERSE_SIZE - 1, 255) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(UNIVERSE_SIZE - 1)), quint8(255));
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(0));

    QVERIFY(m_uni->write(9, 255) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(UNIVERSE_SIZE - 1)), quint8(255));
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(255));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(0));

    QVERIFY(m_uni->write(0, 255) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(UNIVERSE_SIZE - 1)), quint8(255));
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(255));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(255));

    m_gm->setValue(127);
    QCOMPARE(quint8(m_uni->postGMValues()->at(UNIVERSE_SIZE - 1)), quint8(127));
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(127));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(127));

    QVERIFY(m_uni->write(4, 200) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(UNIVERSE_SIZE - 1)), quint8(127));
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(127));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(100));
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(127));
}

void Universe_Test::writeRelative()
{
    // 127 == 0
    QVERIFY(m_uni->writeRelative(9, 127, 1) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(0));

    // 255 == +128
    QVERIFY(m_uni->writeRelative(9, 255, 1) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(128));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(0));

    // 0 == -127
    QVERIFY(m_uni->writeRelative(9, 0, 1) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(1));
    QCOMPARE(quint8(m_uni->postGMValues()->at(4)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(0)), quint8(0));

    m_uni->reset();

    QVERIFY(m_uni->write(9, 85) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(85));

    QVERIFY(m_uni->writeRelative(9, 117, 1) == true);

    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(75));
    QVERIFY(m_uni->write(9, 65) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(65));

    m_uni->reset();

    QVERIFY(m_uni->write(9, 255) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(255));
    QVERIFY(m_uni->writeRelative(9, 255, 1) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(255));

    m_uni->reset();

    QVERIFY(m_uni->write(9, 0) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(0));
    QVERIFY(m_uni->writeRelative(9, 0, 1) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(0));


    m_uni->reset();

    // write 4887 = 19*256+23
    QVERIFY(m_uni->write(9, 19) == true);
    QVERIFY(m_uni->write(10, 23) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(19));
    QCOMPARE(quint8(m_uni->postGMValues()->at(10)), quint8(23));

    // write relative 30067 = 117*256+115
    QVERIFY(m_uni->writeRelative(9, 30067, 2) == true);

    // expect 2442 = 9*256+138
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(9));
    QCOMPARE(quint8(m_uni->postGMValues()->at(10)), quint8(138));



    // write 4887 = 19*256+23
    QVERIFY(m_uni->write(9, 19) == true);
    QVERIFY(m_uni->write(10, 23) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(19));
    QCOMPARE(quint8(m_uni->postGMValues()->at(10)), quint8(23));

    // write relative 27507 = 107*256+115
    QVERIFY(m_uni->writeRelative(9, 27507, 2) == true);

    // expect 0 (due to clamping)
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(0));
    QCOMPARE(quint8(m_uni->postGMValues()->at(10)), quint8(0));



    // write 48663 = 190*256+23
    QVERIFY(m_uni->write(9, 190) == true);
    QVERIFY(m_uni->write(10, 23) == true);
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(190));
    QCOMPARE(quint8(m_uni->postGMValues()->at(10)), quint8(23));

    // write relative 58995 = 230*256+115
    QVERIFY(m_uni->writeRelative(9, 58995, 2) == true);

    // expect 65535 = 255*256+255 (due to clamping)
    QCOMPARE(quint8(m_uni->postGMValues()->at(9)), quint8(255));
    QCOMPARE(quint8(m_uni->postGMValues()->at(10)), quint8(255));



}

void Universe_Test::reset()
{
    int i;

    for (i = 0; i < 512; i++)
        m_uni->setChannelCapability(i, QLCChannel::Intensity);

    for (i = 0; i < 128; i++)
    {
        m_uni->write(i, 200);
        QCOMPARE(quint8(m_uni->postGMValues()->at(i)), quint8(200));
    }

    // Reset channels 10-127 (512 shouldn't cause a crash)
    m_uni->reset(10, 512);
    for (i = 0; i < 10; i++)
        QCOMPARE(quint8(m_uni->postGMValues()->at(i)), quint8(200));
    for (i = 10; i < 128; i++)
        QCOMPARE(int(m_uni->postGMValues()->at(i)), 0);

    // Reset all
    m_uni->reset();
    for (i = 0; i < 128; i++)
        QCOMPARE((int)m_uni->postGMValues()->at(i), 0);
}

void Universe_Test::loadEmpty()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Universe");
    xmlWriter.writeAttribute("Name", "Universe 123");
    //xmlWriter.writeAttribute("ID", "1");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(m_uni->loadXML(xmlReader, 0, 0) == true);
    QCOMPARE(m_uni->name(), QString("Universe 123"));
    //QCOMPARE(m_uni->id(), 1U);
    QCOMPARE(m_uni->passthrough(), false);
}

void Universe_Test::loadPassthroughTrue()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Universe");
    xmlWriter.writeAttribute("Name", "Universe 123");
    //xmlWriter.writeAttribute("ID", "1");
    xmlWriter.writeAttribute("Passthrough", "True");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(m_uni->loadXML(xmlReader, 0, 0) == true);
    QCOMPARE(m_uni->name(), QString("Universe 123"));
    //QCOMPARE(m_uni->id(), 1U);
    QCOMPARE(m_uni->passthrough(), true);
}

void Universe_Test::loadPassthrough1()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Universe");
    xmlWriter.writeAttribute("Name", "Universe 123");
    //xmlWriter.writeAttribute("ID", "1");
    xmlWriter.writeAttribute("Passthrough", "1");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(m_uni->loadXML(xmlReader, 0, 0) == true);
    QCOMPARE(m_uni->name(), QString("Universe 123"));
    //QCOMPARE(m_uni->id(), 1U);
    QCOMPARE(m_uni->passthrough(), true);
}

void Universe_Test::loadWrong()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("U");
    xmlWriter.writeAttribute("Name", "Universe 123");
    //xmlWriter.writeAttribute("ID", "1");
    xmlWriter.writeAttribute("Passthrough", "1");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(m_uni->loadXML(xmlReader, 0, 0) == false);
}

void Universe_Test::loadPassthroughFalse()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Universe");
    xmlWriter.writeAttribute("Name", "Universe 123");
    //xmlWriter.writeAttribute("ID", "1");
    xmlWriter.writeAttribute("Passthrough", "False");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(m_uni->loadXML(xmlReader, 0, 0) == true);
    QCOMPARE(m_uni->name(), QString("Universe 123"));
    //QCOMPARE(m_uni->id(), 1U);
    QCOMPARE(m_uni->passthrough(), false);
}

void Universe_Test::saveEmpty()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    m_uni->setName("Universe 123");
    m_uni->setID(1);

    QVERIFY(m_uni->saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QCOMPARE(xmlReader.name().toString(), QString("Universe"));
    QCOMPARE(xmlReader.attributes().value("Name").toString(), QString("Universe 123"));
    QCOMPARE(xmlReader.attributes().value("ID").toString(), QString("1"));
    QCOMPARE(xmlReader.attributes().hasAttribute("Passthrough"), false);
}

void Universe_Test::savePasthroughTrue()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    m_uni->setName("Universe 123");
    m_uni->setID(1);
    m_uni->setPassthrough(true);

    QVERIFY(m_uni->saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QCOMPARE(xmlReader.name().toString(), QString("Universe"));
    QCOMPARE(xmlReader.attributes().value("Name").toString(), QString("Universe 123"));
    QCOMPARE(xmlReader.attributes().value("ID").toString(), QString("1"));
    QCOMPARE(xmlReader.attributes().value("Passthrough").toString(), QString("True"));
}

void Universe_Test::setGMValueEfficiency()
{
    int i;

    for (i = 0; i < 512; i++)
        m_uni->setChannelCapability(i, QLCChannel::Intensity);

    for (i = 0; i < 512; i++)
        m_uni->write(i, 200);

    /* This applies 50%(127) Grand Master to ALL channels in all universes.
       I'm not really sure what kinds of figures to expect here, since this
       is just one part in the overall processor load. Typically I get ~0.37ms
       on an Intel Core 2 E6550@2.33GHz, which looks plausible to me:
       DMX frame interval is 1/44Hz =~ 23ms. Applying GM to ALL channels takes
       less than 1ms so there's a full 22ms to spare after GM. */
    QBENCHMARK
    {
        // This is slower than plain write() because UA has to dig out each
        // Intensity-enabled channel from its internal QSet.
        m_gm->setValue(127);
    }

    for (i = 0; i < 512; i++)
        QCOMPARE(int(m_uni->postGMValues()->at(i)), int(100));
}

void Universe_Test::writeEfficiency()
{
    m_gm->setValue(127);

    int i;
    for (i = 0; i < 512; i++)
        m_uni->setChannelCapability(i, QLCChannel::Intensity);

    QBENCHMARK
    {
        for (i = 0; i < 512; i++)
            m_uni->write(i, 200);
    }

    for (i = 0; i < 512; i++)
        QCOMPARE(int(m_uni->postGMValues()->at(i)), int(100));
}

void Universe_Test::hasChangedEfficiency()
{
    for (int i = 0; i < 512; i++)
    {
        m_uni->write(i, 200);
        QCOMPARE(m_uni->hasChanged(), true);
    }

    QBENCHMARK
    {
        for (int i = 0; i < 512; i++)
        {
            m_uni->write(i, 200);
            m_uni->hasChanged();
        }
    }
}

void Universe_Test::hasNotChangedEfficiency()
{
    m_uni->write(UNIVERSE_SIZE - 1, 200);
    m_uni->hasChanged();
    QCOMPARE(m_uni->hasChanged(), false);

    QBENCHMARK
    {
        for (int i = 0; i < 512; i++)
        {
            m_uni->hasChanged();
        }
    }
}

void Universe_Test::zeroIntensityChannelsEfficiency()
{
    m_gm->setValue(255);
    int i;

    for (i = 0; i < 512; i++)
        m_uni->setChannelCapability(i, QLCChannel::Intensity);

    for (i = 0; i < 512; i++)
        m_uni->write(i, 200);

    QBENCHMARK
    {
        m_uni->zeroIntensityChannels();
    }

    for (i = 0; i < 512; i++)
        QCOMPARE(int(m_uni->postGMValues()->at(i)), int(0));
}

void Universe_Test::zeroIntensityChannelsEfficiency2()
{
    int i;

    for (i = 0; i < 512; i++)
    {
        if (i % 2)
            m_uni->setChannelCapability(i, QLCChannel::Intensity);
        else
            m_uni->setChannelCapability(i, QLCChannel::Shutter);

        m_uni->write(i, 200);
    }

    QBENCHMARK
    {
        m_uni->zeroIntensityChannels();
    }

    for (i = 0; i < 512; i++)
    {
        if (i % 2)
            QCOMPARE(int(m_uni->postGMValues()->at(i)), int(0));
        else
            QCOMPARE(quint8(m_uni->postGMValues()->at(i)), quint8(200));
    }
}

void Universe_Test::nameAndMisc()
{
    QSignalSpy nameSpy(m_uni, SIGNAL(nameChanged()));

    m_uni->setName("Stage left");
    QCOMPARE(m_uni->name(), QString("Stage left"));
    QCOMPARE(nameSpy.size(), 1);

    // an empty name falls back to the default, derived from the ID
    m_uni->setID(4);
    m_uni->setName("");
    QCOMPARE(m_uni->name(), QString("Universe 5"));
    QCOMPARE(nameSpy.size(), 2);

    QCOMPARE(m_uni->monitor(), false);
    m_uni->setMonitor(true);
    QCOMPARE(m_uni->monitor(), true);

    // out of range value getters are safe
    QCOMPARE(m_uni->postGMValue(UNIVERSE_SIZE), uchar(0));
    QCOMPARE(m_uni->preGMValue(UNIVERSE_SIZE), uchar(0));
    QCOMPARE(m_uni->preGMValue(-1), uchar(0));
    QCOMPARE(m_uni->faderCycles(), quint32(0));

    // default values extend the used/total channel counts and are applied
    // to the output immediately
    m_uni->setChannelDefaultValue(20, 77);
    QCOMPARE(m_uni->totalChannels(), ushort(21));
    QCOMPARE(m_uni->usedChannels(), ushort(21));
    QCOMPARE(m_uni->postGMValue(20), uchar(77));
    QCOMPARE(m_uni->preGMValue(20), uchar(77));

    m_uni->setChannelDefaultValue(5, 1);
    QCOMPARE(m_uni->totalChannels(), ushort(21));
    QCOMPARE(m_uni->usedChannels(), ushort(21));
    QCOMPARE(m_uni->postGMValue(5), uchar(1));

    // intensityChannels() reports the current pre-GM value of each intensity channel
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(2, QLCChannel::Intensity);
    m_uni->write(0, 200);
    m_uni->write(2, 100);
    QHash<int, uchar> intensity = m_uni->intensityChannels();
    QCOMPARE(intensity.size(), 2);
    QCOMPARE(intensity.value(0), uchar(200));
    QCOMPARE(intensity.value(2), uchar(100));
}

void Universe_Test::channelCapabilityEdges()
{
    // out of range channels are ignored
    m_uni->setChannelCapability(UNIVERSE_SIZE, QLCChannel::Intensity);
    QCOMPARE(m_uni->totalChannels(), ushort(0));
    QVERIFY(m_uni->channelCapabilities(UNIVERSE_SIZE) == Universe::Undefined);

    // forced LTP on a non-intensity channel
    m_uni->setChannelCapability(6, QLCChannel::Pan, Universe::LTP);
    QVERIFY(m_uni->channelCapabilities(6) == Universe::LTP);

    // forced HTP on an intensity channel keeps the intensity flag
    m_uni->setChannelCapability(7, QLCChannel::Intensity, Universe::HTP);
    QVERIFY(m_uni->channelCapabilities(7) == (Universe::HTP | Universe::Intensity));
    QCOMPARE(m_uni->totalChannels(), ushort(8));

    m_uni->write(7, 150);
    QVERIFY(m_uni->intensityChannels().contains(7));

    // re-declaring an intensity channel as something else removes it from
    // the intensity list, so it is no longer zeroed every cycle
    m_uni->setChannelCapability(7, QLCChannel::Pan);
    QVERIFY(m_uni->channelCapabilities(7) == Universe::LTP);
    QVERIFY(m_uni->intensityChannels().contains(7) == false);
    m_uni->zeroIntensityChannels();
    QCOMPARE(m_uni->postGMValue(7), uchar(150));

    // and back again: the intensity ranges are rebuilt and the channel gets zeroed
    m_uni->setChannelCapability(7, QLCChannel::Intensity);
    m_uni->zeroIntensityChannels();
    QCOMPARE(m_uni->postGMValue(7), uchar(0));
}

void Universe_Test::writeMultipleAndBlended()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(2, QLCChannel::Pan);
    m_uni->setChannelCapability(3, QLCChannel::Pan);
    m_uni->setChannelCapability(4, QLCChannel::Intensity);

    // 16 bit write, MSB first
    QVERIFY(m_uni->writeMultiple(10, 0x1234, 2) == true);
    QCOMPARE(m_uni->postGMValue(10), uchar(0x12));
    QCOMPARE(m_uni->postGMValue(11), uchar(0x34));

    // normal blend honours HTP on intensity channels, and so does write()
    // unless LTP is forced
    QVERIFY(m_uni->write(0, 200) == true);
    QVERIFY(m_uni->write(0, 100) == false);
    QCOMPARE(m_uni->postGMValue(0), uchar(200));
    QVERIFY(m_uni->write(0, 100, true) == true);
    QCOMPARE(m_uni->postGMValue(0), uchar(100));
    QVERIFY(m_uni->write(0, 200) == true);
    QVERIFY(m_uni->writeBlended(0, 100, 1, Universe::NormalBlend) == false);
    QCOMPARE(m_uni->postGMValue(0), uchar(200));

    // resetting past the end of the universe is a no-op
    m_uni->reset(UNIVERSE_SIZE, 1);
    QCOMPARE(m_uni->postGMValue(0), uchar(200));
    QVERIFY(m_uni->writeBlended(0, 250, 1, Universe::NormalBlend) == true);
    QCOMPARE(m_uni->postGMValue(0), uchar(250));

    // 16 bit blends on an LTP pair
    QVERIFY(m_uni->writeBlended(2, 0x8000, 2, Universe::NormalBlend) == true);
    QCOMPARE(m_uni->postGMValue(2), uchar(0x80));
    QCOMPARE(m_uni->postGMValue(3), uchar(0x00));
    QCOMPARE(m_uni->usedChannels(), ushort(4));

    QVERIFY(m_uni->writeBlended(2, 0x4000, 2, Universe::AdditiveBlend) == true);
    QCOMPARE(m_uni->postGMValue(2), uchar(0xC0));
    QCOMPARE(m_uni->postGMValue(3), uchar(0x00));

    QVERIFY(m_uni->writeBlended(2, 0x8000, 2, Universe::MaskBlend) == true);
    QCOMPARE(m_uni->postGMValue(2), uchar(0x60));
    QCOMPARE(m_uni->postGMValue(3), uchar(0x00));

    QVERIFY(m_uni->writeBlended(2, 0x1000, 2, Universe::SubtractiveBlend) == true);
    QCOMPARE(m_uni->postGMValue(2), uchar(0x50));
    QCOMPARE(m_uni->postGMValue(3), uchar(0x00));

    QVERIFY(m_uni->writeBlended(2, 0xFFFF, 2, Universe::SubtractiveBlend) == true);
    QCOMPARE(m_uni->postGMValue(2), uchar(0x00));
    QCOMPARE(m_uni->postGMValue(3), uchar(0x00));

    // additive blend clamps at full scale
    QVERIFY(m_uni->write(4, 200) == true);
    QVERIFY(m_uni->writeBlended(4, 100, 1, Universe::AdditiveBlend) == true);
    QCOMPARE(m_uni->postGMValue(4), uchar(255));

    // 16 bit additive blend clamps too
    QVERIFY(m_uni->writeBlended(2, 0xFFFF, 2, Universe::NormalBlend) == true);
    QVERIFY(m_uni->writeBlended(2, 0x0001, 2, Universe::AdditiveBlend) == true);
    QCOMPARE(m_uni->postGMValue(2), uchar(0xFF));
    QCOMPARE(m_uni->postGMValue(3), uchar(0xFF));
}

void Universe_Test::channelModifiers()
{
    ChannelModifier mod;
    mod.setName("Test curve");
    QList< QPair<uchar, uchar> > map;
    map << QPair<uchar, uchar>(0, 10) << QPair<uchar, uchar>(255, 200);
    mod.setModifierMap(map);
    QCOMPARE(mod.getValue(0), uchar(10));
    QCOMPARE(mod.getValue(255), uchar(200));

    QVERIFY(m_uni->channelModifier(3) == NULL);
    QVERIFY(m_uni->channelModifier(UNIVERSE_SIZE) == NULL);

    // out of range channels are ignored
    m_uni->setChannelModifier(UNIVERSE_SIZE, &mod);
    QCOMPARE(m_uni->totalChannels(), ushort(0));

    m_uni->setChannelModifier(3, &mod);
    QVERIFY(m_uni->channelModifier(3) == &mod);
    QCOMPARE(m_uni->totalChannels(), ushort(4));
    QCOMPARE(m_uni->usedChannels(), ushort(4));
    // a modified zero is applied right away
    QCOMPARE(m_uni->postGMValue(3), uchar(10));

    QVERIFY(m_uni->write(3, 255) == true);
    QCOMPARE(m_uni->postGMValue(3), uchar(200));
    QVERIFY(m_uni->write(3, 128) == true);
    QCOMPARE(m_uni->postGMValue(3), mod.getValue(128));
    QCOMPARE(m_uni->preGMValue(3), uchar(128));

    // the grand master is applied before the modifier
    m_uni->setChannelCapability(3, QLCChannel::Intensity);
    m_gm->setValue(127);
    QVERIFY(m_uni->write(3, 200) == true);
    QCOMPARE(m_uni->postGMValue(3), mod.getValue(100));
    m_gm->setValue(255);

    // a partial reset restores the modified zero value instead of 0
    m_uni->reset(3, 1);
    QCOMPARE(m_uni->postGMValue(3), uchar(10));
    QCOMPARE(m_uni->preGMValue(3), uchar(0));

    // the same happens when intensity channels are zeroed each cycle
    QVERIFY(m_uni->write(3, 200) == true);
    m_uni->zeroIntensityChannels();
    QCOMPARE(m_uni->postGMValue(3), uchar(10));

    // removing the modifier explicitly
    m_uni->setChannelModifier(3, NULL);
    QVERIFY(m_uni->channelModifier(3) == NULL);
    QCOMPARE(m_uni->postGMValue(3), uchar(0));

    // a full reset also drops all modifiers
    m_uni->setChannelModifier(3, &mod);
    QCOMPARE(m_uni->postGMValue(3), uchar(10));
    m_uni->reset();
    QVERIFY(m_uni->channelModifier(3) == NULL);
    QCOMPARE(m_uni->postGMValue(3), uchar(0));
}

void Universe_Test::lastWrites()
{
    Universe::LastChannelWrite info;
    QVERIFY(m_uni->lastChannelWrite(0, info) == false);
    QVERIFY(m_uni->lastChannelWrite(UNIVERSE_SIZE, info) == false);

    // out of range writes are ignored
    m_uni->recordLastWrite(-1, 1, false, 0, 0, 0, "x");
    m_uni->recordLastWrite(UNIVERSE_SIZE, 1, false, 0, 0, 0, "x");
    QVERIFY(m_uni->lastChannelWrite(-1, info) == false);
    QVERIFY(m_uni->lastChannelWrite(UNIVERSE_SIZE, info) == false);

    m_uni->recordLastWrite(7, 200, true, 3, 2, 9, "Scene fader");
    QVERIFY(m_uni->lastChannelWrite(7, info) == true);
    QCOMPARE(info.value, uchar(200));
    QCOMPARE(info.hasFixture, true);
    QCOMPARE(info.fixtureID, quint32(3));
    QCOMPARE(info.channel, quint32(2));
    QCOMPARE(info.parentFunctionID, quint32(9));
    QCOMPARE(info.faderName, QString("Scene fader"));
    QVERIFY(info.timestampMs > 0);

    // a later write replaces the record
    m_uni->recordLastWrite(7, 10, false, 0, 7, 0, "");
    QVERIFY(m_uni->lastChannelWrite(7, info) == true);
    QCOMPARE(info.value, uchar(10));
    QCOMPARE(info.hasFixture, false);
    QCOMPARE(info.channel, quint32(7));
    QVERIFY(info.faderName.isEmpty());

    // reset forgets everything
    m_uni->reset();
    QVERIFY(m_uni->lastChannelWrite(7, info) == false);
}

void Universe_Test::passthrough()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(1, QLCChannel::Pan);

    QSignalSpy ptSpy(m_uni, SIGNAL(passthroughChanged()));
    QSignalSpy inSpy(m_uni, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)));

    // without passthrough, input values are only forwarded as a signal
    m_uni->slotInputValueChanged(0, 3, 77, "key");
    QCOMPARE(inSpy.size(), 1);
    QCOMPARE(inSpy.at(0).at(0).toUInt(), quint32(0));
    QCOMPARE(inSpy.at(0).at(1).toUInt(), quint32(3));
    QCOMPARE(inSpy.at(0).at(2).toUInt(), quint32(77));
    QCOMPARE(inSpy.at(0).at(3).toString(), QString("key"));
    QCOMPARE(m_uni->postGMValue(3), uchar(0));

    m_uni->setPassthrough(true);
    QCOMPARE(m_uni->passthrough(), true);
    QCOMPARE(ptSpy.size(), 1);
    m_uni->setPassthrough(true);
    QCOMPARE(ptSpy.size(), 1);

    // passthrough values are HTP merged into the output
    m_uni->slotInputValueChanged(0, 0, 100, QString());
    QCOMPARE(inSpy.size(), 1);
    QCOMPARE(m_uni->usedChannels(), ushort(1));
    QCOMPARE(m_uni->postGMValue(0), uchar(100));
    QCOMPARE(m_uni->preGMValue(0), uchar(0));

    QVERIFY(m_uni->write(0, 50) == true);
    QCOMPARE(m_uni->postGMValue(0), uchar(100));
    QVERIFY(m_uni->write(0, 200) == true);
    QCOMPARE(m_uni->postGMValue(0), uchar(200));

    m_uni->slotInputValueChanged(0, 1, 30, QString());
    QCOMPARE(m_uni->postGMValue(1), uchar(30));
    QCOMPARE(m_uni->usedChannels(), ushort(2));

    // other universes and out of range channels are ignored
    m_uni->slotInputValueChanged(1, 5, 30, QString());
    QCOMPARE(m_uni->postGMValue(5), uchar(0));
    QCOMPARE(m_uni->usedChannels(), ushort(2));
    m_uni->slotInputValueChanged(0, UNIVERSE_SIZE, 30, QString());
    QCOMPARE(m_uni->usedChannels(), ushort(2));
    m_uni->slotInputValueChanged(0, UNIVERSE_SIZE - 1, 30, QString());
    QCOMPARE(m_uni->usedChannels(), ushort(UNIVERSE_SIZE));
    QCOMPARE(m_uni->postGMValue(UNIVERSE_SIZE - 1), uchar(30));

    // a partial reset keeps the passthrough values in the output
    QVERIFY(m_uni->write(0, 255) == true);
    QCOMPARE(m_uni->postGMValue(0), uchar(255));
    m_uni->reset(0, 2);
    QCOMPARE(m_uni->preGMValue(0), uchar(0));
    QCOMPARE(m_uni->postGMValue(0), uchar(100));
    QCOMPARE(m_uni->postGMValue(1), uchar(30));

    // a full reset outputs the passthrough values, but disables passthrough
    QVERIFY(m_uni->write(0, 255) == true);
    m_uni->reset();
    QCOMPARE(m_uni->postGMValue(0), uchar(100));
    QCOMPARE(m_uni->postGMValue(1), uchar(30));
    QCOMPARE(m_uni->postGMValue(UNIVERSE_SIZE - 1), uchar(30));
    QCOMPARE(m_uni->passthrough(), false);

    // input values are forwarded again
    m_uni->slotInputValueChanged(0, 3, 78, QString());
    QCOMPARE(inSpy.size(), 2);

    // re-enabling reuses the stored passthrough values
    m_uni->setPassthrough(true);
    QCOMPARE(ptSpy.size(), 2);
    QVERIFY(m_uni->write(0, 10) == true);
    QCOMPARE(m_uni->postGMValue(0), uchar(100));
    m_uni->setPassthrough(false);
    QCOMPARE(ptSpy.size(), 3);
    QVERIFY(m_uni->write(0, 10) == true);
    QCOMPARE(m_uni->postGMValue(0), uchar(10));
}

void Universe_Test::faders()
{
    QVERIFY(m_uni->faders().isEmpty());

    QSharedPointer<GenericFader> autoFader = m_uni->requestFader(Universe::Auto);
    QSharedPointer<GenericFader> overrideFader = m_uni->requestFader(Universe::Override);
    QSharedPointer<GenericFader> flashFader = m_uni->requestFader(Universe::Flashing);
    QSharedPointer<GenericFader> auto2 = m_uni->requestFader();

    QCOMPARE(autoFader->priority(), int(Universe::Auto));
    QCOMPARE(overrideFader->priority(), int(Universe::Override));
    QCOMPARE(flashFader->priority(), int(Universe::Flashing));
    QCOMPARE(auto2->priority(), int(Universe::Auto));

    // faders are kept sorted by priority, later requests after their peers
    QList<QSharedPointer<GenericFader> > list = m_uni->faders();
    QCOMPARE(list.count(), 4);
    QVERIFY(list.at(0) == autoFader);
    QVERIFY(list.at(1) == auto2);
    QVERIFY(list.at(2) == overrideFader);
    QVERIFY(list.at(3) == flashFader);

    // raising a fader's priority moves it behind the last fader with a
    // lower or equal priority
    m_uni->requestFaderPriority(auto2, Universe::SimpleDesk);
    QCOMPARE(auto2->priority(), int(Universe::SimpleDesk));
    list = m_uni->faders();
    QVERIFY(list.at(0) == autoFader);
    QVERIFY(list.at(1) == overrideFader);
    QVERIFY(list.at(2) == flashFader);
    QVERIFY(list.at(3) == auto2);

    // requesting the same priority again does not move anything
    m_uni->requestFaderPriority(auto2, Universe::SimpleDesk);
    QVERIFY(m_uni->faders().at(3) == auto2);

    // an unknown fader is ignored
    QSharedPointer<GenericFader> stranger(new GenericFader());
    m_uni->requestFaderPriority(stranger, Universe::Override);
    QCOMPARE(m_uni->faders().count(), 4);
    QCOMPARE(stranger->priority(), int(Universe::Auto));

    // lowering the priority moves the fader forward
    m_uni->requestFaderPriority(auto2, Universe::Auto);
    QCOMPARE(auto2->priority(), int(Universe::Auto));
    list = m_uni->faders();
    QVERIFY(list.at(0) == auto2);
    QVERIFY(list.at(1) == autoFader);
    QVERIFY(list.at(2) == overrideFader);
    QVERIFY(list.at(3) == flashFader);

    // pause and fade out act on the faders of a given function
    overrideFader->setParentFunctionID(7);
    flashFader->setParentFunctionID(8);
    m_uni->setFaderPause(7, true);
    QCOMPARE(overrideFader->isPaused(), true);
    QCOMPARE(flashFader->isPaused(), false);
    QCOMPARE(autoFader->isPaused(), false);
    m_uni->setFaderPause(7, false);
    QCOMPARE(overrideFader->isPaused(), false);

    m_uni->setFaderFadeOut(500);
    QCOMPARE(overrideFader->isFadingOut(), true);
    QCOMPARE(flashFader->isFadingOut(), true);
    QCOMPARE(autoFader->isFadingOut(), false);
    QCOMPARE(auto2->isFadingOut(), false);

    // dismissing
    m_uni->dismissFader(autoFader);
    QCOMPARE(m_uni->faders().count(), 3);
    m_uni->dismissFader(autoFader);
    QCOMPARE(m_uni->faders().count(), 3);
    m_uni->dismissFader(stranger);
    QCOMPARE(m_uni->faders().count(), 3);
}

void Universe_Test::processFaders()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);
    m_uni->setChannelCapability(1, QLCChannel::Pan);

    QSignalSpy writtenSpy(m_uni, SIGNAL(universeWritten(quint32,QByteArray)));

    // nothing to do: no data change, no signal, but a cycle is counted
    m_uni->processFaders(20);
    QCOMPARE(m_uni->faderCycles(), quint32(1));
    QCOMPARE(writtenSpy.size(), 0);

    QSharedPointer<GenericFader> fader = m_uni->requestFader();
    fader->setName("Test fader");

    FadeChannel fc;
    fc.addChannel(0);
    fc.setFlags(FadeChannel::HTP | FadeChannel::Intensity | FadeChannel::CanFade);
    fc.setTarget(200);
    fader->add(fc);

    FadeChannel fc2;
    fc2.addChannel(1);
    fc2.setFlags(FadeChannel::LTP | FadeChannel::CanFade);
    fc2.setTarget(100);
    fader->add(fc2);
    QCOMPARE(fader->channelsCount(), 2);

    m_uni->processFaders(20);
    QCOMPARE(m_uni->faderCycles(), quint32(2));
    QCOMPARE(m_uni->postGMValue(0), uchar(200));
    QCOMPARE(m_uni->postGMValue(1), uchar(100));
    QCOMPARE(writtenSpy.size(), 1);
    QCOMPARE(writtenSpy.at(0).at(0).toUInt(), quint32(0));
    QCOMPARE(writtenSpy.at(0).at(1).toByteArray().size(), int(m_uni->usedChannels()));
    QCOMPARE(uchar(writtenSpy.at(0).at(1).toByteArray().at(0)), uchar(200));

    Universe::LastChannelWrite info;
    QVERIFY(m_uni->lastChannelWrite(0, info) == true);
    QCOMPARE(info.value, uchar(200));
    QCOMPARE(info.hasFixture, false);
    QCOMPARE(info.channel, quint32(0));
    QCOMPARE(info.faderName, QString("Test fader"));

    // a disabled fader is skipped, so its intensity channel is zeroed
    fader->setEnabled(false);
    m_uni->processFaders(20);
    QCOMPARE(m_uni->faderCycles(), quint32(3));
    QCOMPARE(m_uni->postGMValue(0), uchar(0));
    QCOMPARE(m_uni->postGMValue(1), uchar(100));
    QCOMPARE(writtenSpy.size(), 2);

    fader->setEnabled(true);
    m_uni->processFaders(20);
    QCOMPARE(m_uni->postGMValue(0), uchar(200));

    // a fader that asked to be deleted is dropped before writing
    fader->requestDelete();
    m_uni->processFaders(20);
    QCOMPARE(m_uni->faderCycles(), quint32(5));
    QVERIFY(m_uni->faders().isEmpty());
    QCOMPARE(fader->channelsCount(), 0);
    QCOMPARE(m_uni->postGMValue(0), uchar(0));
    QCOMPARE(m_uni->postGMValue(1), uchar(100));
}

void Universe_Test::processFadersFadeOut()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);

    QSharedPointer<GenericFader> fader = m_uni->requestFader();
    fader->setParentFunctionID(5);

    FadeChannel fc;
    fc.addChannel(0);
    fc.setFlags(FadeChannel::HTP | FadeChannel::Intensity | FadeChannel::CanFade);
    fc.setTarget(200);
    fader->add(fc);

    m_uni->processFaders(20);
    QCOMPARE(m_uni->postGMValue(0), uchar(200));

    // a fading out fader survives a delete request until its channels are gone
    m_uni->setFaderFadeOut(100);
    fader->requestDelete();
    QCOMPARE(fader->isFadingOut(), true);

    m_uni->processFaders(50);
    QCOMPARE(m_uni->faders().count(), 1);
    QCOMPARE(fader->channelsCount(), 1);
    uchar halfway = m_uni->postGMValue(0);
    QVERIFY(halfway > 0);
    QVERIFY(halfway < 200);

    m_uni->processFaders(100);
    QCOMPARE(fader->channelsCount(), 0);
    QCOMPARE(m_uni->postGMValue(0), uchar(0));
}

void Universe_Test::thread()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);

    QSharedPointer<GenericFader> fader = m_uni->requestFader();
    FadeChannel fc;
    fc.addChannel(0);
    fc.setFlags(FadeChannel::HTP | FadeChannel::Intensity | FadeChannel::CanFade);
    fc.setTarget(255);
    fader->add(fc);

    QVERIFY(m_uni->isRunning() == false);
    m_uni->start();

    // each tick lets the thread process the faders once
    for (int i = 0; i < 200 && m_uni->faderCycles() == 0; i++)
    {
        m_uni->tick();
        QThread::msleep(10);
    }
    QVERIFY(m_uni->faderCycles() > 0);
    QVERIFY(m_uni->isRunning() == true);

    quint32 cycles = m_uni->faderCycles();
    for (int i = 0; i < 200 && m_uni->faderCycles() == cycles; i++)
    {
        m_uni->tick();
        QThread::msleep(10);
    }
    QVERIFY(m_uni->faderCycles() > cycles);

    // the thread zeroes the intensity channel at the start of every cycle
    // before the fader writes it again, so only the end state is stable
    for (int i = 0; i < 200 && m_uni->postGMValue(0) != 255; i++)
        QThread::msleep(10);
    QCOMPARE(m_uni->postGMValue(0), uchar(255));

    // the destructor (see cleanup()) is in charge of stopping the thread
}

void Universe_Test::inputPatch()
{
    QSignalSpy ipSpy(m_uni, SIGNAL(inputPatchChanged()));
    QSignalSpy inSpy(m_uni, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)));

    // requests that don't create a patch
    QVERIFY(m_uni->setInputPatch(NULL, 0, NULL) == true);
    QVERIFY(m_uni->inputPatch() == NULL);
    QVERIFY(m_uni->setInputPatch(m_stub, QLCIOPlugin::invalidLine(), NULL) == true);
    QVERIFY(m_uni->inputPatch() == NULL);
    QCOMPARE(ipSpy.size(), 0);
    QVERIFY(m_uni->isPatched() == false);
    m_uni->flushInput();

    QVERIFY(m_uni->setInputPatch(m_stub, 0, NULL) == true);
    QVERIFY(m_uni->inputPatch() != NULL);
    QVERIFY(m_uni->inputPatch()->plugin() == m_stub);
    QCOMPARE(m_uni->inputPatch()->input(), quint32(0));
    QCOMPARE(ipSpy.size(), 1);
    QVERIFY(m_uni->isPatched() == true);

    // values are buffered by the patch and forwarded on flush
    m_stub->emitValueChanged(UINT_MAX, 0, 3, 200);
    QCOMPARE(inSpy.size(), 0);
    m_uni->flushInput();
    QCOMPARE(inSpy.size(), 1);
    QCOMPARE(inSpy.at(0).at(0).toUInt(), quint32(0));
    QCOMPARE(inSpy.at(0).at(1).toUInt(), quint32(3));
    QCOMPARE(inSpy.at(0).at(2).toUInt(), quint32(200));

    // with passthrough, the patch feeds the universe output instead
    m_uni->setPassthrough(true);
    m_stub->emitValueChanged(UINT_MAX, 0, 4, 150);
    m_uni->flushInput();
    QCOMPARE(inSpy.size(), 1);
    QCOMPARE(m_uni->postGMValue(4), uchar(150));

    m_uni->setPassthrough(false);
    m_stub->emitValueChanged(UINT_MAX, 0, 4, 10);
    m_uni->flushInput();
    QCOMPARE(inSpy.size(), 2);
    QCOMPARE(inSpy.at(1).at(1).toUInt(), quint32(4));
    QCOMPARE(inSpy.at(1).at(2).toUInt(), quint32(10));

    // replace the line, then remove the patch
    QVERIFY(m_uni->setInputPatch(m_stub, 1, NULL) == true);
    QCOMPARE(m_uni->inputPatch()->input(), quint32(1));
    QCOMPARE(ipSpy.size(), 2);

    QVERIFY(m_uni->setInputPatch(m_stub, QLCIOPlugin::invalidLine(), NULL) == true);
    QVERIFY(m_uni->inputPatch() == NULL);
    QCOMPARE(ipSpy.size(), 3);
    QVERIFY(m_uni->isPatched() == false);

    // a patch removed while passthrough is on must be disconnected as such
    QVERIFY(m_uni->setInputPatch(m_stub, 2, NULL) == true);
    m_uni->setPassthrough(true);
    QVERIFY(m_uni->setInputPatch(m_stub, QLCIOPlugin::invalidLine(), NULL) == true);
    QVERIFY(m_uni->inputPatch() == NULL);
    m_uni->setPassthrough(false);
}

void Universe_Test::outputPatches()
{
    QSignalSpy countSpy(m_uni, SIGNAL(outputPatchesCountChanged()));
    QSignalSpy changeSpy(m_uni, SIGNAL(outputPatchChanged()));

    QVERIFY(m_uni->setOutputPatch(m_stub, 0, -1) == false);
    QVERIFY(m_uni->setOutputPatch(NULL, 0, 0) == false);
    QVERIFY(m_uni->setOutputPatch(m_stub, QLCIOPlugin::invalidLine(), 0) == false);
    QCOMPARE(m_uni->outputPatchesCount(), 0);
    QVERIFY(m_uni->outputPatch(0) == NULL);
    QVERIFY(m_uni->outputPatch(-1) == NULL);
    QVERIFY(m_uni->isPatched() == false);

    // dumping without patches is a no-op
    m_uni->dumpOutput(QByteArray(10, char(5)), true);

    QVERIFY(m_uni->setOutputPatch(m_stub, 0, 0) == true);
    QCOMPARE(m_uni->outputPatchesCount(), 1);
    QCOMPARE(countSpy.size(), 1);
    QVERIFY(m_uni->outputPatch(0) != NULL);
    QCOMPARE(m_uni->outputPatch(0)->output(), quint32(0));
    QVERIFY(m_uni->isPatched() == true);

    QVERIFY(m_uni->setOutputPatch(m_stub, 1, 1) == true);
    QCOMPARE(m_uni->outputPatchesCount(), 2);
    QCOMPARE(countSpy.size(), 2);

    // replacing an existing patch
    QVERIFY(m_uni->setOutputPatch(m_stub, 2, 0) == true);
    QCOMPARE(m_uni->outputPatchesCount(), 2);
    QCOMPARE(m_uni->outputPatch(0)->output(), quint32(2));
    QCOMPARE(changeSpy.size(), 1);

    // an index beyond the list appends
    QVERIFY(m_uni->setOutputPatch(m_stub, 3, 5) == true);
    QCOMPARE(m_uni->outputPatchesCount(), 3);
    QCOMPARE(m_uni->outputPatch(2)->output(), quint32(3));
    QVERIFY(m_uni->outputPatch(3) == NULL);

    // dumping writes the data to every patch and pushes the channel count
    // to the plugin once it changed
    m_uni->setChannelCapability(9, QLCChannel::Intensity);
    QCOMPARE(m_uni->totalChannels(), ushort(10));
    QVERIFY(m_uni->write(9, 123) == true);
    QByteArray data = m_uni->postGMValues()->mid(0, m_uni->usedChannels());
    QCOMPARE(data.size(), 10);
    m_uni->dumpOutput(data, true);
    QCOMPARE(m_stub->m_universe.at(2 * 512 + 9), char(123));
    QCOMPARE(m_stub->m_universe.at(1 * 512 + 9), char(123));
    QCOMPARE(m_stub->m_universe.at(3 * 512 + 9), char(123));
    QCOMPARE(m_uni->outputPatch(2)->getPluginParameters().value(PLUGIN_UNIVERSECHANNELS).toInt(), 10);

    // a blacked out patch dumps the blackout values instead
    m_uni->outputPatch(0)->setBlackout(true);
    m_uni->dumpOutput(data, true);
    QCOMPARE(m_stub->m_universe.at(2 * 512 + 9), char(0));
    QCOMPARE(m_stub->m_universe.at(1 * 512 + 9), char(123));
    QCOMPARE(m_stub->m_universe.at(3 * 512 + 9), char(123));

    // deleting patches
    QVERIFY(m_uni->setOutputPatch(NULL, 0, 0) == true);
    QCOMPARE(m_uni->outputPatchesCount(), 2);
    QCOMPARE(m_uni->outputPatch(0)->output(), quint32(1));
    QCOMPARE(countSpy.size(), 4);
    QVERIFY(m_uni->setOutputPatch(m_stub, QLCIOPlugin::invalidLine(), 1) == true);
    QCOMPARE(m_uni->outputPatchesCount(), 1);
    QVERIFY(m_uni->setOutputPatch(m_stub, QLCIOPlugin::invalidLine(), 0) == true);
    QCOMPARE(m_uni->outputPatchesCount(), 0);
    QVERIFY(m_uni->setOutputPatch(m_stub, QLCIOPlugin::invalidLine(), 0) == false);
    QVERIFY(m_uni->isPatched() == false);
}

void Universe_Test::feedbackPatch()
{
    QSignalSpy fbSpy(m_uni, SIGNAL(hasFeedbackChanged()));

    QVERIFY(m_uni->hasFeedback() == false);
    QVERIFY(m_uni->feedbackPatch() == NULL);
    QVERIFY(m_uni->setFeedbackPatch(NULL, 0) == false);
    QVERIFY(m_uni->setFeedbackPatch(m_stub, QLCIOPlugin::invalidLine()) == false);
    QVERIFY(m_uni->feedbackPatch() == NULL);
    QCOMPARE(fbSpy.size(), 0);
    QVERIFY(m_uni->isPatched() == false);

    QVERIFY(m_uni->setFeedbackPatch(m_stub, 1) == true);
    QVERIFY(m_uni->hasFeedback() == true);
    QVERIFY(m_uni->feedbackPatch() != NULL);
    QCOMPARE(m_uni->feedbackPatch()->output(), quint32(1));
    QVERIFY(m_uni->feedbackPatch()->plugin() == m_stub);
    QCOMPARE(fbSpy.size(), 1);
    QVERIFY(m_uni->isPatched() == true);

    QVERIFY(m_uni->setFeedbackPatch(m_stub, 2) == true);
    QCOMPARE(m_uni->feedbackPatch()->output(), quint32(2));
    QCOMPARE(fbSpy.size(), 2);

    QVERIFY(m_uni->setFeedbackPatch(NULL, 2) == true);
    QVERIFY(m_uni->hasFeedback() == false);
    QVERIFY(m_uni->feedbackPatch() == NULL);
    QCOMPARE(fbSpy.size(), 3);
    QVERIFY(m_uni->isPatched() == false);
}

void Universe_Test::savePatches()
{
    QVERIFY(m_uni->setInputPatch(m_stub, 0, NULL) == true);
    m_uni->inputPatch()->setPluginParameter("inKey", "inValue");
    QVERIFY(m_uni->setOutputPatch(m_stub, 1, 0) == true);
    m_uni->outputPatch(0)->setPluginParameter("outKey", 42);
    QVERIFY(m_uni->setFeedbackPatch(m_stub, 2) == true);
    m_uni->feedbackPatch()->setPluginParameter("fbKey", "fbValue");

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(m_uni->saveXML(&xmlWriter) == true);
    xmlWriter.setDevice(NULL);
    buffer.close();

    QString xml = QString::fromUtf8(buffer.data());
    QVERIFY(xml.startsWith("<Universe "));
    QVERIFY(xml.contains("<Input Plugin=\"I/O Plugin Stub\" Name=\"1: Stub 1\" UID=\"\" Line=\"0\""));
    QVERIFY(xml.contains("<Output Plugin=\"I/O Plugin Stub\" Name=\"2: Stub 2\" UID=\"\" Line=\"1\""));
    QVERIFY(xml.contains("<Feedback Plugin=\"I/O Plugin Stub\" Name=\"3: Stub 3\" UID=\"\" Line=\"2\""));
    QVERIFY(xml.contains("Profile=") == false);
    QVERIFY(xml.contains("<PluginParameters inKey=\"inValue\"/>"));
    QVERIFY(xml.contains("fbKey=\"fbValue\""));

    // the stub tracks one output line per universe, so only the line
    // opened last (the feedback one) still reports output parameters -
    // both its own and the ones cached for the earlier output line
    QVERIFY(xml.contains("<PluginParameters fbKey=\"fbValue\" outKey=\"42\"/>"));
    QCOMPARE(xml.count("<PluginParameters"), 2);

    // the patches are still there for the plain load path (no ioMap needed)
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QCOMPARE(xmlReader.name().toString(), QString("Universe"));
    QCOMPARE(xmlReader.attributes().value("Name").toString(), QString("Universe 1"));

    // plugin parameters can only be loaded from their own tag
    QVERIFY(m_uni->loadXMLPluginParameters(xmlReader, Universe::InputPatchTag, 0) == false);
    QXmlStreamReader params("<PluginParameters loadedKey=\"loadedValue\"/>");
    params.readNextStartElement();
    QVERIFY(m_uni->loadXMLPluginParameters(params, Universe::InputPatchTag, 0) == true);
    QCOMPARE(m_uni->inputPatch()->getPluginParameters().value("loadedKey").toString(), QString("loadedValue"));

    // remove the patches again so the plugin lines are closed in order
    QVERIFY(m_uni->setInputPatch(m_stub, QLCIOPlugin::invalidLine(), NULL) == true);
    QVERIFY(m_uni->setOutputPatch(NULL, 0, 0) == true);
    QVERIFY(m_uni->setFeedbackPatch(NULL, 0) == true);
}

void Universe_Test::nullFaderSkipped()
{
    m_uni->setChannelCapability(0, QLCChannel::Intensity);

    QSharedPointer<GenericFader> fader = m_uni->requestFader();
    FadeChannel fc;
    fc.addChannel(0);
    fc.setFlags(FadeChannel::HTP | FadeChannel::Intensity | FadeChannel::CanFade);
    fc.setTarget(100);
    fader->add(fc);

    // a null entry in the fader list is skipped, the real fader still runs
    m_uni->m_faders.prepend(QSharedPointer<GenericFader>());
    QCOMPARE(m_uni->m_faders.size(), 2);
    m_uni->processFaders(20);
    QCOMPARE(m_uni->postGMValue(0), uchar(100));

    m_uni->m_faders.removeAll(QSharedPointer<GenericFader>());
    QCOMPARE(m_uni->m_faders.size(), 1);
}

void Universe_Test::savePatchXMLInvalid()
{
    QMap<QString, QVariant> params;

    // no plugin, the "None" placeholder or no line: nothing is written
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    m_uni->savePatchXML(&xmlWriter, "Input", "", "1: Stub 1", "", 0, "", params);
    m_uni->savePatchXML(&xmlWriter, "Input", KInputNone, "1: Stub 1", "", 0, "", params);
    m_uni->savePatchXML(&xmlWriter, "Input", m_stub->name(), "1: Stub 1", "",
                        QLCIOPlugin::invalidLine(), "", params);
    xmlWriter.setDevice(NULL);
    buffer.close();
    QVERIFY(buffer.data().isEmpty());

    // a valid patch with a profile name gets that attribute as well
    QBuffer valid;
    valid.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter validWriter(&valid);
    m_uni->savePatchXML(&validWriter, "Input", m_stub->name(), "1: Stub 1", "uid-1", 0,
                        "Generic MIDI", params);
    validWriter.setDevice(NULL);
    valid.close();
    QString xml = QString::fromUtf8(valid.data());
    QVERIFY(xml.contains("<Input Plugin=\"I/O Plugin Stub\" Name=\"1: Stub 1\" UID=\"uid-1\" Line=\"0\" Profile=\"Generic MIDI\"/>"));
}

void Universe_Test::defaultArgOverloads()
{
    QSignalSpy spy(m_uni, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)));

    // the moc-generated overloads for the defaulted key argument
    m_uni->slotInputValueChanged(0, 5, 200);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(1).toUInt(), quint32(5));
    QCOMPARE(spy.at(0).at(2).toUInt(), uint(200));
    QVERIFY(spy.at(0).at(3).toString().isEmpty());

    emit m_uni->inputValueChanged(0, 6, 100);
    QCOMPARE(spy.size(), 2);
    QCOMPARE(spy.at(1).at(1).toUInt(), quint32(6));
}

void Universe_Test::destroyWhileStarting()
{
    // deleting a universe whose thread has been started, whether or not it
    // has entered its run loop yet, must stop that thread cleanly
    Universe *uni = new Universe(1, m_gm, this);
    QVERIFY(uni->isRunning() == false);
    uni->start();
    delete uni;
}

QTEST_APPLESS_MAIN(Universe_Test)
