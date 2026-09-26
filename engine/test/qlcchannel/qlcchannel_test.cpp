/*
  Q Light Controller Plus - Unit tests
  qlcchannel_test.cpp

  Copyright (C) Heikki Junnila
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

#include "qlcchannel_test.h"
#include "qlccapability.h"
#include "qlcchannel.h"

void QLCChannel_Test::groupList()
{
    QStringList list(QLCChannel::groupList());

    QCOMPARE(list.size(), 21);
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Beam)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Colour)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Effect)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Gobo)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Intensity)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Maintenance)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::NoGroup)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Pan)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Prism)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Shutter)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Speed)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::Tilt)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::PositionX)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::PositionY)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::PositionZ)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::RotationX)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::RotationY)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::RotationZ)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::ScaleX)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::ScaleY)));
    QVERIFY(list.contains(QLCChannel::groupToString(QLCChannel::ScaleZ)));
}

void QLCChannel_Test::name()
{
    /* Verify that a name can be set & get for the channel */
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->name().isEmpty());

    channel->setName("Channel");
    QVERIFY(channel->name() == "Channel");

    delete channel;
}

void QLCChannel_Test::group()
{
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->group() == QLCChannel::Intensity);

    channel->setGroup(QLCChannel::Beam);
    QVERIFY(channel->group() == QLCChannel::Beam);

    channel->setGroup(QLCChannel::Group(31337));
    QVERIFY(channel->group() == QLCChannel::Group(31337));

    delete channel;
}

void QLCChannel_Test::defaultValue()
{
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->defaultValue() == 0);

    channel->setDefaultValue(137);
    QVERIFY(channel->defaultValue() == 137);
}

void QLCChannel_Test::controlByte()
{
    QCOMPARE(int(QLCChannel::MSB), 0);
    QCOMPARE(int(QLCChannel::LSB), 1);

    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->controlByte() == QLCChannel::MSB);

    channel->setControlByte(QLCChannel::LSB);
    QVERIFY(channel->controlByte() == QLCChannel::LSB);

    delete channel;
}

void QLCChannel_Test::colourList()
{
    QStringList list(QLCChannel::colourList());

    QCOMPARE(list.size(), 11);
    //QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::NoColour)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Red)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Green)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Blue)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Cyan)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Magenta)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Yellow)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Amber)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::White)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::UV)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Lime)));
    QVERIFY(list.contains(QLCChannel::colourToString(QLCChannel::Indigo)));
}

void QLCChannel_Test::colour()
{
    QCOMPARE(int(QLCChannel::NoColour), 0);
    QCOMPARE(int(QLCChannel::Red), 0xFF0000);
    QCOMPARE(int(QLCChannel::Green), 0x00FF00);
    QCOMPARE(int(QLCChannel::Blue), 0x0000FF);
    QCOMPARE(int(QLCChannel::Cyan), 0x00FFFF);
    QCOMPARE(int(QLCChannel::Magenta), 0xFF00FF);
    QCOMPARE(int(QLCChannel::Yellow), 0xFFFF00);
    QCOMPARE(int(QLCChannel::Amber), 0xFF7E00);
    QCOMPARE(int(QLCChannel::White), 0xFFFFFF);
    QCOMPARE(int(QLCChannel::UV), 0x9400D3);
    QCOMPARE(int(QLCChannel::Lime), 0xADFF2F);
    QCOMPARE(int(QLCChannel::Indigo), 0x4B0082);

    QLCChannel* channel = new QLCChannel();
    QCOMPARE(channel->colour(), QLCChannel::NoColour);

    channel->setColour(QLCChannel::Red);
    QCOMPARE(channel->colour(), QLCChannel::Red);

    channel->setColour(QLCChannel::Green);
    QCOMPARE(channel->colour(), QLCChannel::Green);

    channel->setColour(QLCChannel::Blue);
    QCOMPARE(channel->colour(), QLCChannel::Blue);

    channel->setColour(QLCChannel::Cyan);
    QCOMPARE(channel->colour(), QLCChannel::Cyan);

    channel->setColour(QLCChannel::Magenta);
    QCOMPARE(channel->colour(), QLCChannel::Magenta);

    channel->setColour(QLCChannel::Yellow);
    QCOMPARE(channel->colour(), QLCChannel::Yellow);

    channel->setColour(QLCChannel::Amber);
    QCOMPARE(channel->colour(), QLCChannel::Amber);

    channel->setColour(QLCChannel::White);
    QCOMPARE(channel->colour(), QLCChannel::White);

    channel->setColour(QLCChannel::UV);
    QCOMPARE(channel->colour(), QLCChannel::UV);

    channel->setColour(QLCChannel::Lime);
    QCOMPARE(channel->colour(), QLCChannel::Lime);

    channel->setColour(QLCChannel::Indigo);
    QCOMPARE(channel->colour(), QLCChannel::Indigo);

    channel->setColour(QLCChannel::NoColour);
    QCOMPARE(channel->colour(), QLCChannel::NoColour);
}

void QLCChannel_Test::searchCapabilityByValue()
{
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->capabilities().size() == 0);

    QLCCapability* cap1 = new QLCCapability(0, 9, "0-9");
    QVERIFY(channel->addCapability(cap1) == true);
    QVERIFY(channel->capabilities().size() == 1);

    QLCCapability* cap2 = new QLCCapability(10, 19, "10-19");
    QVERIFY(channel->addCapability(cap2) == true);
    QVERIFY(channel->capabilities().size() == 2);

    QLCCapability* cap3 = new QLCCapability(20, 29, "20-29");
    QVERIFY(channel->addCapability(cap3) == true);
    QVERIFY(channel->capabilities().size() == 3);

    QVERIFY(channel->searchCapability(0) == cap1);
    QVERIFY(channel->searchCapability(1) == cap1);
    QVERIFY(channel->searchCapability(2) == cap1);
    QVERIFY(channel->searchCapability(3) == cap1);
    QVERIFY(channel->searchCapability(4) == cap1);
    QVERIFY(channel->searchCapability(5) == cap1);
    QVERIFY(channel->searchCapability(6) == cap1);
    QVERIFY(channel->searchCapability(7) == cap1);
    QVERIFY(channel->searchCapability(8) == cap1);
    QVERIFY(channel->searchCapability(9) == cap1);

    QVERIFY(channel->searchCapability(10) == cap2);
    QVERIFY(channel->searchCapability(11) == cap2);
    QVERIFY(channel->searchCapability(12) == cap2);
    QVERIFY(channel->searchCapability(13) == cap2);
    QVERIFY(channel->searchCapability(14) == cap2);
    QVERIFY(channel->searchCapability(15) == cap2);
    QVERIFY(channel->searchCapability(16) == cap2);
    QVERIFY(channel->searchCapability(17) == cap2);
    QVERIFY(channel->searchCapability(18) == cap2);
    QVERIFY(channel->searchCapability(19) == cap2);

    QVERIFY(channel->searchCapability(30) == NULL);

    delete channel;
}

void QLCChannel_Test::searchCapabilityByName()
{
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->capabilities().size() == 0);

    QLCCapability* cap1 = new QLCCapability(0, 9, "0-9");
    QVERIFY(channel->addCapability(cap1) == true);

    QLCCapability* cap2 = new QLCCapability(10, 19, "10-19");
    QVERIFY(channel->addCapability(cap2) == true);

    QLCCapability* cap3 = new QLCCapability(20, 29, "20-29");
    QVERIFY(channel->addCapability(cap3) == true);

    QVERIFY(channel->searchCapability("0-9") == cap1);
    QVERIFY(channel->searchCapability("10-19") == cap2);
    QVERIFY(channel->searchCapability("20-29") == cap3);
    QVERIFY(channel->searchCapability("foo") == NULL);

    delete channel;
}

void QLCChannel_Test::addCapability()
{
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->capabilities().size() == 0);

    QLCCapability* cap1 = new QLCCapability(15, 19, "15-19");
    QVERIFY(channel->addCapability(cap1) == true);
    QVERIFY(channel->capabilities().size() == 1);
    QVERIFY(channel->capabilities()[0] == cap1);

    QLCCapability* cap2 = new QLCCapability(0, 9, "0-9");
    QVERIFY(channel->addCapability(cap2) == true);
    QVERIFY(channel->capabilities().size() == 2);
    QVERIFY(channel->capabilities()[0] == cap1);
    QVERIFY(channel->capabilities()[1] == cap2);

    /* Completely overlapping with cap2 */
    QLCCapability* cap3 = new QLCCapability(5, 6, "5-6");
    QVERIFY(channel->addCapability(cap3) == false);
    delete cap3;
    cap3 = NULL;

    /* Partially overlapping from low-end with cap1 */
    QLCCapability* cap4 = new QLCCapability(19, 25, "19-25");
    QVERIFY(channel->addCapability(cap4) == false);
    delete cap4;
    cap4 = NULL;

    /* Partially overlapping from high end with cap1 */
    QLCCapability* cap5 = new QLCCapability(10, 15, "10-15");
    QVERIFY(channel->addCapability(cap5) == false);
    delete cap5;
    cap5 = NULL;

    /* Partially overlapping with two ranges at both ends (cap1 & cap2) */
    QLCCapability* cap6 = new QLCCapability(8, 16, "8-16");
    QVERIFY(channel->addCapability(cap6) == false);
    delete cap6;
    cap6 = NULL;

    /* Completely containing cap1 */
    QLCCapability* cap7 = new QLCCapability(14, 20, "14-20");
    QVERIFY(channel->addCapability(cap7) == false);
    delete cap7;
    cap7 = NULL;

    /* Non-overlapping, between cap1 & cap2*/
    QLCCapability* cap8 = new QLCCapability(10, 14, "10-14");
    QVERIFY(channel->addCapability(cap8) == true);
    /* Don't delete cap8 because it's now a member of the channel and gets
       deleted from the channel's destructor. */

    delete channel;
}

void QLCChannel_Test::removeCapability()
{
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->capabilities().size() == 0);

    QLCCapability* cap1 = new QLCCapability(10, 20, "10-20");
    QVERIFY(channel->addCapability(cap1) == true);
    QVERIFY(channel->capabilities().size() == 1);

    QLCCapability* cap2 = new QLCCapability(0, 9, "0-9");
    QVERIFY(channel->addCapability(cap2) == true);
    QVERIFY(channel->capabilities().size() == 2);

    QVERIFY(channel->removeCapability(cap2) == true);
    QVERIFY(channel->capabilities().size() == 1);
    /* cap2 is deleted by QLCChannel::removeCapability() */

    QVERIFY(channel->removeCapability(cap2) == false);
    QVERIFY(channel->capabilities().size() == 1);

    QVERIFY(channel->removeCapability(cap1) == true);
    QVERIFY(channel->capabilities().size() == 0);
    /* cap1 is deleted by QLCChannel::removeCapability() */

    delete channel;
}

void QLCChannel_Test::sortCapabilities()
{
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->capabilities().size() == 0);

    QLCCapability* cap1 = new QLCCapability(10, 19, "10-19");
    QVERIFY(channel->addCapability(cap1) == true);

    QLCCapability* cap2 = new QLCCapability(50, 59, "50-59");
    QVERIFY(channel->addCapability(cap2) == true);

    QLCCapability* cap3 = new QLCCapability(40, 49, "40-49");
    QVERIFY(channel->addCapability(cap3) == true);

    QLCCapability* cap4 = new QLCCapability(0, 9, "0-9");
    QVERIFY(channel->addCapability(cap4) == true);

    QLCCapability* cap5 = new QLCCapability(200, 209, "200-209");
    QVERIFY(channel->addCapability(cap5) == true);

    QLCCapability* cap6 = new QLCCapability(30, 39, "30-39");
    QVERIFY(channel->addCapability(cap6) == true);

    QLCCapability* cap7 = new QLCCapability(26, 29, "26-29");
    QVERIFY(channel->addCapability(cap7) == true);

    QLCCapability* cap8 = new QLCCapability(20, 25, "20-25");
    QVERIFY(channel->addCapability(cap8) == true);

    QList <QLCCapability*> orig(channel->capabilities());
    QVERIFY(orig.at(0) == cap1);
    QVERIFY(orig.at(1) == cap2);
    QVERIFY(orig.at(2) == cap3);
    QVERIFY(orig.at(3) == cap4);
    QVERIFY(orig.at(4) == cap5);
    QVERIFY(orig.at(5) == cap6);
    QVERIFY(orig.at(6) == cap7);
    QVERIFY(orig.at(7) == cap8);

    channel->sortCapabilities();

    QList <QLCCapability*> sorted(channel->capabilities());
    QVERIFY(sorted.at(0) == cap4);
    QVERIFY(sorted.at(1) == cap1);
    QVERIFY(sorted.at(2) == cap8);
    QVERIFY(sorted.at(3) == cap7);
    QVERIFY(sorted.at(4) == cap6);
    QVERIFY(sorted.at(5) == cap3);
    QVERIFY(sorted.at(6) == cap2);
    QVERIFY(sorted.at(7) == cap5);

    delete channel;
}

void QLCChannel_Test::copy()
{
    QLCChannel* channel = new QLCChannel();
    QVERIFY(channel->capabilities().size() == 0);

    channel->setName("Foobar");
    channel->setGroup(QLCChannel::Tilt);
    channel->setControlByte(QLCChannel::ControlByte(3));
    channel->setColour(QLCChannel::Yellow);

    QLCCapability* cap1 = new QLCCapability(10, 19, "10-19");
    QVERIFY(channel->addCapability(cap1) == true);

    QLCCapability* cap2 = new QLCCapability(50, 59, "50-59");
    QVERIFY(channel->addCapability(cap2) == true);

    QLCCapability* cap3 = new QLCCapability(40, 49, "40-49");
    QVERIFY(channel->addCapability(cap3) == true);

    QLCCapability* cap4 = new QLCCapability(0, 9, "0-9");
    QVERIFY(channel->addCapability(cap4) == true);

    QLCCapability* cap5 = new QLCCapability(200, 209, "200-209");
    QVERIFY(channel->addCapability(cap5) == true);

    QLCCapability* cap6 = new QLCCapability(30, 39, "30-39");
    QVERIFY(channel->addCapability(cap6) == true);

    QLCCapability* cap7 = new QLCCapability(26, 29, "26-29");
    QVERIFY(channel->addCapability(cap7) == true);

    QLCCapability* cap8 = new QLCCapability(20, 25, "20-25");
    QVERIFY(channel->addCapability(cap8) == true);

    /* Create a copy of the original channel */
    QLCChannel* copy = channel->createCopy();

    QVERIFY(copy->name() == "Foobar");
    QVERIFY(copy->group() == QLCChannel::Tilt);
    QVERIFY(copy->controlByte() == QLCChannel::ControlByte(3));
    QVERIFY(copy->colour() == QLCChannel::Yellow);

    /* Verify that the capabilities in the copied channel are also
       copies i.e. their pointers are not the same as the originals. */
    QList <QLCCapability*> caps(copy->capabilities());
    QVERIFY(caps.size() == 8);
    QVERIFY(caps.at(0) != cap1);
    QVERIFY(caps.at(0)->name() == cap1->name());
    QVERIFY(caps.at(0)->min() == cap1->min());
    QVERIFY(caps.at(0)->max() == cap1->max());

    QVERIFY(caps.at(1) != cap2);
    QVERIFY(caps.at(1)->name() == cap2->name());
    QVERIFY(caps.at(1)->min() == cap2->min());
    QVERIFY(caps.at(1)->max() == cap2->max());

    QVERIFY(caps.at(2) != cap3);
    QVERIFY(caps.at(2)->name() == cap3->name());
    QVERIFY(caps.at(2)->min() == cap3->min());
    QVERIFY(caps.at(2)->max() == cap3->max());

    QVERIFY(caps.at(3) != cap4);
    QVERIFY(caps.at(3)->name() == cap4->name());
    QVERIFY(caps.at(3)->min() == cap4->min());
    QVERIFY(caps.at(3)->max() == cap4->max());

    QVERIFY(caps.at(4) != cap5);
    QVERIFY(caps.at(4)->name() == cap5->name());
    QVERIFY(caps.at(4)->min() == cap5->min());
    QVERIFY(caps.at(4)->max() == cap5->max());

    QVERIFY(caps.at(5) != cap6);
    QVERIFY(caps.at(5)->name() == cap6->name());
    QVERIFY(caps.at(5)->min() == cap6->min());
    QVERIFY(caps.at(5)->max() == cap6->max());

    QVERIFY(caps.at(6) != cap7);
    QVERIFY(caps.at(6)->name() == cap7->name());
    QVERIFY(caps.at(6)->min() == cap7->min());
    QVERIFY(caps.at(6)->max() == cap7->max());

    QVERIFY(caps.at(7) != cap8);
    QVERIFY(caps.at(7)->name() == cap8->name());
    QVERIFY(caps.at(7)->min() == cap8->min());
    QVERIFY(caps.at(7)->max() == cap8->max());
}

void QLCChannel_Test::load()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Channel");
    xmlWriter.writeAttribute("Name", "Channel1");

    xmlWriter.writeStartElement("Group");
    xmlWriter.writeAttribute("Byte", "1");
    xmlWriter.writeCharacters("Tilt");
    xmlWriter.writeEndElement();

    xmlWriter.writeTextElement("Colour", QLCChannel::colourToString(QLCChannel::Cyan));

    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "0");
    xmlWriter.writeAttribute("Max", "10");
    xmlWriter.writeCharacters("Cap1");
    xmlWriter.writeEndElement();

    /* Overlaps with cap1, shouldn't appear in the channel */
    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "5");
    xmlWriter.writeAttribute("Max", "15");
    xmlWriter.writeCharacters("Cap2");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "11");
    xmlWriter.writeAttribute("Max", "20");
    xmlWriter.writeCharacters("Cap3");
    xmlWriter.writeEndElement();

    /* Invalid capability tag, shouldn't appear in the channel, since it
       is not recognized by the channel. */
    xmlWriter.writeStartElement("apability");
    xmlWriter.writeAttribute("Min", "21");
    xmlWriter.writeAttribute("Max", "30");
    xmlWriter.writeCharacters("Cap4");
    xmlWriter.writeEndElement();

    /* Missing minimum value, shouldn't appear in the channel, because
       loadXML() fails. */
    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Max", "30");
    xmlWriter.writeCharacters("Cap5");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCChannel ch;
    QVERIFY(ch.loadXML(xmlReader) == true);
    qDebug() << int(ch.colour());
    QVERIFY(ch.name() == "Channel1");
    QVERIFY(ch.group() == QLCChannel::Tilt);
    QVERIFY(ch.controlByte() == QLCChannel::LSB);
    QVERIFY(ch.colour() == QLCChannel::Cyan);
    QVERIFY(ch.capabilities().size() == 2);
    QVERIFY(ch.capabilities()[0]->name() == "Cap1");
    QVERIFY(ch.capabilities()[1]->name() == "Cap3");
}

void QLCChannel_Test::loadWrongRoot()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Chanel");
    xmlWriter.writeAttribute("Name", "Channel1");

    xmlWriter.writeStartElement("Group");
    xmlWriter.writeAttribute("Byte", "1");
    xmlWriter.writeCharacters("Tilt");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "0");
    xmlWriter.writeAttribute("Max", "10");
    xmlWriter.writeCharacters("Cap1");
    xmlWriter.writeEndElement();

    /* Overlaps with cap1, shouldn't appear in the channel */
    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "5");
    xmlWriter.writeAttribute("Max", "15");
    xmlWriter.writeCharacters("Cap2");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "11");
    xmlWriter.writeAttribute("Max", "20");
    xmlWriter.writeCharacters("Cap3");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCChannel ch;
    QVERIFY(ch.loadXML(xmlReader) == false);
    QVERIFY(ch.name().isEmpty());
    QVERIFY(ch.group() == QLCChannel::Intensity);
    QVERIFY(ch.controlByte() == QLCChannel::MSB);
    QVERIFY(ch.capabilities().size() == 0);
}

void QLCChannel_Test::save()
{
    QLCChannel* channel = new QLCChannel();

    channel->setName("Foobar");
    channel->setGroup(QLCChannel::Shutter);
    channel->setControlByte(QLCChannel::LSB);

    QLCCapability* cap1 = new QLCCapability(0, 9, "One");
    QVERIFY(channel->addCapability(cap1) == true);

    QLCCapability* cap2 = new QLCCapability(10, 19, "Two");
    QVERIFY(channel->addCapability(cap2) == true);

    QLCCapability* cap3 = new QLCCapability(20, 29, "Three");
    QVERIFY(channel->addCapability(cap3) == true);

    QLCCapability* cap4 = new QLCCapability(30, 39, "Four");
    QVERIFY(channel->addCapability(cap4) == true);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(channel->saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Channel");
    QVERIFY(xmlReader.attributes().value("Name").toString() == "Foobar");

    bool group = false;
    bool capOne = false, capTwo = false, capThree = false, capFour = false;

    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name().toString() == "Group")
        {
            group = true;
            QVERIFY(xmlReader.attributes().value("Byte").toString() == "1");
            QVERIFY(xmlReader.readElementText() == "Shutter");
        }
        else if (xmlReader.name().toString() == "Capability")
        {
            QString capName = xmlReader.readElementText();
            if (capName == "One" && capOne == false)
                capOne = true;
            else if (capName == "Two" && capTwo == false)
                capTwo = true;
            else if (capName == "Three" && capThree == false)
                capThree = true;
            else if (capName == "Four" && capFour == false)
                capFour = true;
            else
                QFAIL("Same capability saved multiple times");
        }
        else
        {
            QFAIL(QString("Unexpected tag: %1").arg(xmlReader.name().toString())
                  .toLatin1());
            xmlReader.skipCurrentElement();
        }
    }

    QVERIFY(group == true);
    QVERIFY(capOne == true);
    QVERIFY(capTwo == true);
    QVERIFY(capThree == true);
    QVERIFY(capFour == true);

    delete channel;
}

void QLCChannel_Test::assignment()
{
    QLCChannel source;
    source.setName("Source");
    source.setGroup(QLCChannel::Gobo);
    source.setDefaultValue(42);
    source.setControlByte(QLCChannel::LSB);
    source.setColour(QLCChannel::Amber);

    QLCCapability *srcCap1 = new QLCCapability(0, 9, "A");
    QLCCapability *srcCap2 = new QLCCapability(10, 19, "B");
    QVERIFY(source.addCapability(srcCap1) == true);
    QVERIFY(source.addCapability(srcCap2) == true);

    QLCChannel target;
    target.setName("Target");
    QVERIFY(target.addCapability(new QLCCapability(100, 200, "Old")) == true);
    QCOMPARE(target.capabilities().size(), 1);

    target = source;

    QCOMPARE(target.name(), QString("Source"));
    QCOMPARE(target.preset(), QLCChannel::Custom);
    QCOMPARE(target.group(), QLCChannel::Gobo);
    QCOMPARE(target.defaultValue(), uchar(42));
    QCOMPARE(target.controlByte(), QLCChannel::LSB);
    QCOMPARE(target.colour(), QLCChannel::Amber);

    /* The old capabilities are gone, the new ones are deep copies */
    QCOMPARE(target.capabilities().size(), 2);
    QVERIFY(target.capabilities().at(0) != srcCap1);
    QVERIFY(target.capabilities().at(1) != srcCap2);
    QCOMPARE(target.capabilities().at(0)->name(), QString("A"));
    QCOMPARE(target.capabilities().at(0)->min(), uchar(0));
    QCOMPARE(target.capabilities().at(0)->max(), uchar(9));
    QCOMPARE(target.capabilities().at(1)->name(), QString("B"));
    QCOMPARE(target.capabilities().at(1)->min(), uchar(10));
    QCOMPARE(target.capabilities().at(1)->max(), uchar(19));
    QVERIFY(target.searchCapability("Old") == NULL);

    /* Self assignment must be a no-op and must not destroy the capabilities */
    QLCChannel &self = target;
    target = self;
    QCOMPARE(target.capabilities().size(), 2);
    QCOMPARE(target.capabilities().at(0)->name(), QString("A"));
    QCOMPARE(target.name(), QString("Source"));
}

void QLCChannel_Test::presetStrings()
{
    for (int i = QLCChannel::Custom; i < QLCChannel::LastPreset; i++)
    {
        QLCChannel::Preset preset = QLCChannel::Preset(i);
        QString str = QLCChannel::presetToString(preset);
        QVERIFY2(str.isEmpty() == false, qPrintable(QString::number(i)));
        QCOMPARE(QLCChannel::stringToPreset(str), preset);
    }

    QCOMPARE(QLCChannel::presetToString(QLCChannel::Custom), QString("Custom"));
    QCOMPARE(QLCChannel::presetToString(QLCChannel::IntensityRed), QString("IntensityRed"));
    QCOMPARE(QLCChannel::presetToString(QLCChannel::NoFunction), QString("NoFunction"));
    QCOMPARE(QLCChannel::stringToPreset("PositionTiltFine"), QLCChannel::PositionTiltFine);

    /* Unknown keys map to -1 through QMetaEnum::keyToValue() */
    QCOMPARE(int(QLCChannel::stringToPreset("NotAPreset")), -1);
}

void QLCChannel_Test::presetMapping()
{
    for (int i = QLCChannel::IntensityMasterDimmer; i < QLCChannel::LastPreset; i++)
    {
        QLCChannel::Preset preset = QLCChannel::Preset(i);
        QString key = QLCChannel::presetToString(preset);
        QByteArray ctx = key.toLatin1();

        QLCChannel ch;
        QSignalSpy presetSpy(&ch, SIGNAL(presetChanged()));
        ch.setPreset(preset);

        QCOMPARE(ch.preset(), preset);
        QCOMPARE(presetSpy.count(), 1);
        QVERIFY2(ch.name().isEmpty() == false, ctx.constData());

        /* "...Fine" presets are always the LSB of a 16bit pair */
        QLCChannel::ControlByte expectedByte = key.endsWith("Fine") ? QLCChannel::LSB : QLCChannel::MSB;
        QVERIFY2(ch.controlByte() == expectedByte, ctx.constData());

        QLCChannel::Group expectedGroup = QLCChannel::NoGroup;
        if (key.startsWith("Intensity"))
            expectedGroup = QLCChannel::Intensity;
        else if (key == "PositionPan" || key == "PositionPanFine" || key == "PositionXAxis")
            expectedGroup = QLCChannel::Pan;
        else if (key == "PositionTilt" || key == "PositionTiltFine" || key == "PositionYAxis")
            expectedGroup = QLCChannel::Tilt;
        else if (key.startsWith("Speed"))
            expectedGroup = QLCChannel::Speed;
        else if (key.startsWith("Color"))
            expectedGroup = QLCChannel::Colour;
        else if (key.startsWith("Gobo"))
            expectedGroup = QLCChannel::Gobo;
        else if (key.startsWith("Shutter"))
            expectedGroup = QLCChannel::Shutter;
        else if (key.startsWith("Beam"))
            expectedGroup = QLCChannel::Beam;
        else if (key.startsWith("Prism"))
            expectedGroup = QLCChannel::Prism;
        else if (key == "NoFunction")
            expectedGroup = QLCChannel::Nothing;
        /* every preset must have been classified above */
        QVERIFY2(expectedGroup != QLCChannel::NoGroup, ctx.constData());
        QVERIFY2(ch.group() == expectedGroup, ctx.constData());

        /* Only the primary colour intensity presets carry a colour */
        QLCChannel::PrimaryColour expectedColour = QLCChannel::NoColour;
        if (key.startsWith("Intensity"))
        {
            QString base = key.mid(9);
            if (base.endsWith("Fine"))
                base.chop(4);
            expectedColour = QLCChannel::stringToColour(base);
        }
        QVERIFY2(ch.colour() == expectedColour, ctx.constData());
    }

    /* Spot-check a few of the generated names */
    QLCChannel ch1;
    ch1.setPreset(QLCChannel::IntensityRedFine);
    QCOMPARE(ch1.name(), QString("Red fine"));
    QCOMPARE(ch1.colour(), QLCChannel::Red);
    QCOMPARE(ch1.controlByte(), QLCChannel::LSB);

    QLCChannel ch2;
    ch2.setPreset(QLCChannel::PositionPan);
    QCOMPARE(ch2.name(), QString("Pan"));
    QCOMPARE(ch2.group(), QLCChannel::Pan);

    QLCChannel ch3;
    ch3.setPreset(QLCChannel::SpeedPanTiltFastSlow);
    QCOMPARE(ch3.name(), QString("Pan/Tilt speed"));

    QLCChannel ch4;
    ch4.setPreset(QLCChannel::IntensityMasterDimmerFine);
    QCOMPARE(ch4.name(), QString("Master dimmer fine"));
    QCOMPARE(ch4.colour(), QLCChannel::NoColour);

    QLCChannel ch5;
    ch5.setPreset(QLCChannel::NoFunction);
    QCOMPARE(ch5.name(), QString("No function"));
    QCOMPARE(ch5.group(), QLCChannel::Nothing);

    QLCChannel ch6;
    ch6.setPreset(QLCChannel::IntensityHueFine);
    QCOMPARE(ch6.name(), QString("Hue fine"));
    QCOMPARE(ch6.colour(), QLCChannel::NoColour);
    QCOMPARE(ch6.controlByte(), QLCChannel::LSB);

    /* a value outside the preset table is stored but classifies as nothing */
    QLCChannel ch7;
    ch7.setPreset(QLCChannel::LastPreset);
    QCOMPARE(ch7.preset(), QLCChannel::LastPreset);
    QVERIFY(ch7.name().isEmpty());
    QCOMPARE(ch7.group(), QLCChannel::Intensity);
    QCOMPARE(ch7.colour(), QLCChannel::NoColour);
    QCOMPARE(ch7.controlByte(), QLCChannel::MSB);
}

void QLCChannel_Test::presetSameValue()
{
    QLCChannel ch;
    ch.setName("Keep me");
    ch.setPreset(QLCChannel::PositionTilt);

    /* an already existing name is never overwritten by the preset */
    QCOMPARE(ch.name(), QString("Keep me"));
    QCOMPARE(ch.group(), QLCChannel::Tilt);

    QSignalSpy spy(&ch, SIGNAL(presetChanged()));

    /* setting the same preset again is a no-op */
    ch.setPreset(QLCChannel::PositionTilt);
    QCOMPARE(spy.count(), 0);

    /* going back to Custom emits, but leaves group/byte/colour untouched */
    ch.setPreset(QLCChannel::Custom);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(ch.preset(), QLCChannel::Custom);
    QCOMPARE(ch.group(), QLCChannel::Tilt);
    QCOMPARE(ch.name(), QString("Keep me"));
}

void QLCChannel_Test::presetCapabilities()
{
    for (int i = QLCChannel::Custom; i < QLCChannel::LastPreset; i++)
    {
        QLCChannel::Preset preset = QLCChannel::Preset(i);
        QByteArray ctx = QLCChannel::presetToString(preset).toLatin1();

        QLCChannel ch;
        ch.setPreset(preset);

        QLCCapability *cap = ch.addPresetCapability();
        QVERIFY2(cap != NULL, ctx.constData());
        QCOMPARE(ch.capabilities().size(), 1);
        QVERIFY(ch.capabilities().first() == cap);
        QCOMPARE(cap->min(), uchar(0));
        QCOMPARE(cap->max(), uchar(UCHAR_MAX));

        if (preset == QLCChannel::Custom)
            QVERIFY2(cap->name().isEmpty() == true, ctx.constData());
        else
            QVERIFY2(cap->name().isEmpty() == false, ctx.constData());
    }

    struct Expectation { QLCChannel::Preset preset; const char *name; };
    const Expectation expectations[] = {
        { QLCChannel::IntensityMasterDimmer, "Master dimmer (0 - 100%)" },
        { QLCChannel::IntensityDimmer, "Dimmer (0 - 100%)" },
        { QLCChannel::IntensityRed, "Red intensity (0 - 100%)" },
        { QLCChannel::IntensityLime, "Lime intensity (0 - 100%)" },
        { QLCChannel::IntensityValue, "Value intensity (0 - 100%)" },
        { QLCChannel::IntensityDimmerFine, "Dimmer fine" },
        { QLCChannel::PositionPan, "Pan" },
        { QLCChannel::PositionYAxis, "Y Axis" },
        { QLCChannel::SpeedPanSlowFast, "Pan (Slow to fast)" },
        { QLCChannel::SpeedPanFastSlow, "Pan (Fast to slow)" },
        { QLCChannel::SpeedTiltSlowFast, "Tilt (Slow to fast)" },
        { QLCChannel::SpeedTiltFastSlow, "Tilt (Fast to slow)" },
        { QLCChannel::SpeedPanTiltSlowFast, "Pan and tilt (Slow to fast)" },
        { QLCChannel::SpeedPanTiltFastSlow, "Pan and tilt (Fast to slow)" },
        { QLCChannel::ColorMacro, "Color macro presets" },
        { QLCChannel::ColorWheel, "Color wheel presets" },
        { QLCChannel::ColorRGBMixer, "RGB mixer" },
        { QLCChannel::GoboWheel, "Gobo wheel presets" },
        { QLCChannel::GoboIndex, "Gobo index presets" },
        { QLCChannel::ShutterStrobeSlowFast, "Strobe (Slow to fast)" },
        { QLCChannel::ShutterStrobeFastSlow, "Strobe (Fast to slow)" },
        { QLCChannel::ShutterIrisMinToMax, "Iris (Minimum to maximum)" },
        { QLCChannel::ShutterIrisMaxToMin, "Iris (Maximum to minimum)" },
        { QLCChannel::ShutterIrisFine, "Iris fine" },
        { QLCChannel::BeamFocusNearFar, "Beam (Near to far)" },
        { QLCChannel::BeamFocusFarNear, "Beam (Far to near)" },
        { QLCChannel::BeamZoomSmallBig, "Zoom (Small to big)" },
        { QLCChannel::BeamZoomBigSmall, "Zoom (Big to small)" },
        { QLCChannel::BeamZoomFine, "Zoom fine" },
        { QLCChannel::PrismRotationSlowFast, "Prism rotation (Slow to fast)" },
        { QLCChannel::PrismRotationFastSlow, "Prism rotation (Fast to slow)" },
        { QLCChannel::NoFunction, "No function" },
    };

    for (const Expectation &e : expectations)
    {
        QLCChannel ch;
        ch.setPreset(e.preset);
        QLCCapability *cap = ch.addPresetCapability();
        QVERIFY2(cap->name() == QString(e.name),
                 qPrintable(QString("%1: got '%2'").arg(QLCChannel::presetToString(e.preset)).arg(cap->name())));
    }
}

void QLCChannel_Test::copyPreset()
{
    QLCChannel ch;
    ch.setPreset(QLCChannel::GoboWheelFine);
    ch.setDefaultValue(7);

    QLCChannel *copy = ch.createCopy();
    QVERIFY(copy != NULL);
    QCOMPARE(copy->preset(), QLCChannel::GoboWheelFine);
    QCOMPARE(copy->name(), QString("Gobo wheel fine"));
    QCOMPARE(copy->group(), QLCChannel::Gobo);
    QCOMPARE(copy->controlByte(), QLCChannel::LSB);
    QCOMPARE(copy->defaultValue(), uchar(7));

    /* a preset copy regenerates its single preset capability */
    QCOMPARE(copy->capabilities().size(), 1);
    QCOMPARE(copy->capabilities().first()->name(), QString("Gobo wheel fine"));

    delete copy;
}

void QLCChannel_Test::groupStrings()
{
    QList<QLCChannel::Group> groups;
    groups << QLCChannel::Intensity << QLCChannel::Colour << QLCChannel::Gobo
           << QLCChannel::Speed << QLCChannel::Pan << QLCChannel::Tilt
           << QLCChannel::Shutter << QLCChannel::Prism << QLCChannel::Beam
           << QLCChannel::Effect << QLCChannel::Maintenance << QLCChannel::Nothing
           << QLCChannel::PositionX << QLCChannel::PositionY << QLCChannel::PositionZ
           << QLCChannel::RotationX << QLCChannel::RotationY << QLCChannel::RotationZ
           << QLCChannel::ScaleX << QLCChannel::ScaleY << QLCChannel::ScaleZ;
    QCOMPARE(groups.size(), QLCChannel::groupList().size());

    QStringList list = QLCChannel::groupList();
    foreach (QLCChannel::Group grp, groups)
    {
        QString str = QLCChannel::groupToString(grp);
        QVERIFY2(str.isEmpty() == false, qPrintable(QString::number(grp)));
        QVERIFY2(list.contains(str), qPrintable(str));
        QVERIFY2(QLCChannel::stringToGroup(str) == grp, qPrintable(str));
    }

    QCOMPARE(QLCChannel::groupToString(QLCChannel::Intensity), QString("Intensity"));
    QCOMPARE(QLCChannel::groupToString(QLCChannel::PositionX), QString("Position X"));
    QCOMPARE(QLCChannel::groupToString(QLCChannel::ScaleZ), QString("Scale Z"));
    QCOMPARE(QLCChannel::groupToString(QLCChannel::NoGroup), QString("Nothing"));
    QCOMPARE(QLCChannel::stringToGroup("Bogus"), QLCChannel::NoGroup);

    QLCChannel ch;
    QCOMPARE(ch.groupString(), QString("Intensity"));
    ch.setGroup(QLCChannel::RotationY);
    QCOMPARE(ch.groupString(), QString("Rotation Y"));
}

void QLCChannel_Test::colourStrings()
{
    QList<QLCChannel::PrimaryColour> colours;
    colours << QLCChannel::Red << QLCChannel::Green << QLCChannel::Blue
            << QLCChannel::Cyan << QLCChannel::Magenta << QLCChannel::Yellow
            << QLCChannel::Amber << QLCChannel::White << QLCChannel::UV
            << QLCChannel::Lime << QLCChannel::Indigo;
    QCOMPARE(colours.size(), QLCChannel::colourList().size());

    QStringList list = QLCChannel::colourList();
    foreach (QLCChannel::PrimaryColour col, colours)
    {
        QString str = QLCChannel::colourToString(col);
        QVERIFY2(str.isEmpty() == false, qPrintable(QString::number(col)));
        QVERIFY2(list.contains(str), qPrintable(str));
        QVERIFY2(QLCChannel::stringToColour(str) == col, qPrintable(str));
    }

    QCOMPARE(QLCChannel::colourToString(QLCChannel::Cyan), QString("Cyan"));
    QCOMPARE(QLCChannel::colourToString(QLCChannel::Indigo), QString("Indigo"));
    QCOMPARE(QLCChannel::colourToString(QLCChannel::NoColour), QString("Generic"));
    QCOMPARE(QLCChannel::stringToColour("Generic"), QLCChannel::NoColour);
    QCOMPARE(QLCChannel::stringToColour("Bogus"), QLCChannel::NoColour);
}

void QLCChannel_Test::icons()
{
    /* Coloured intensity channels get a painted pixmap icon whose
       colour code matches the primary colour value */
    QList<QLCChannel::PrimaryColour> colours;
    colours << QLCChannel::Red << QLCChannel::Green << QLCChannel::Blue
            << QLCChannel::Cyan << QLCChannel::Magenta << QLCChannel::Yellow
            << QLCChannel::Amber << QLCChannel::White << QLCChannel::UV
            << QLCChannel::Lime << QLCChannel::Indigo;

    foreach (QLCChannel::PrimaryColour col, colours)
    {
        QByteArray ctx = QLCChannel::colourToString(col).toLatin1();
        QLCChannel ch;
        ch.setGroup(QLCChannel::Intensity);
        ch.setColour(col);

        QIcon icon = ch.getIcon();
        QVERIFY2(icon.isNull() == false, ctx.constData());
        QVERIFY2(icon.pixmap(32, 32).isNull() == false, ctx.constData());

        QString code = ch.getIconNameFromGroup(QLCChannel::Intensity, false);
        QVERIFY2(code.startsWith("#"), ctx.constData());
        QCOMPARE(code.length(), 7);
        QCOMPARE(QColor(code).rgb() & 0xFFFFFF, uint(col));

        QString svg = ch.getIconNameFromGroup(QLCChannel::Intensity, true);
        QVERIFY2(svg.startsWith("qrc:/"), ctx.constData());
        QVERIFY2(svg.endsWith(".svg"), ctx.constData());
        QVERIFY2(svg != QString("qrc:/intensity.svg"), ctx.constData());
    }

    /* A plain dimmer falls back to the generic intensity resource */
    QLCChannel dimmer;
    dimmer.setGroup(QLCChannel::Intensity);
    QCOMPARE(dimmer.colour(), QLCChannel::NoColour);
    dimmer.getIcon(); // resource icon; may be null in a test binary without the resources
    QCOMPARE(dimmer.getIconNameFromGroup(QLCChannel::Intensity, false), QString(":/intensity.png"));
    QCOMPARE(dimmer.getIconNameFromGroup(QLCChannel::Intensity, true), QString("qrc:/intensity.svg"));

    /* Non-intensity groups map to fixed resource names */
    struct Expectation { QLCChannel::Group group; const char *png; const char *svg; };
    const Expectation expectations[] = {
        { QLCChannel::Pan, ":/pan.png", "qrc:/pan.svg" },
        { QLCChannel::Tilt, ":/tilt.png", "qrc:/tilt.svg" },
        { QLCChannel::Colour, ":/colorwheel.png", "qrc:/colorwheel.svg" },
        { QLCChannel::Effect, ":/star.png", "qrc:/star.svg" },
        { QLCChannel::Gobo, ":/gobo.png", "qrc:/gobo.svg" },
        { QLCChannel::Shutter, ":/shutter.png", "qrc:/shutter.svg" },
        { QLCChannel::Speed, ":/speed.png", "qrc:/speed.svg" },
        { QLCChannel::Prism, ":/prism.png", "qrc:/prism.svg" },
        { QLCChannel::Maintenance, ":/configure.png", "qrc:/configure.svg" },
        { QLCChannel::Beam, ":/beam.png", "qrc:/beam.svg" },
        { QLCChannel::Nothing, ":/uncheck.png", "qrc:/uncheck.svg" },
        /* groups without a dedicated icon fall back to the intensity one */
        { QLCChannel::PositionX, ":/intensity.png", "qrc:/intensity.svg" },
        { QLCChannel::ScaleZ, ":/intensity.png", "qrc:/intensity.svg" },
        { QLCChannel::NoGroup, ":/intensity.png", "qrc:/intensity.svg" },
    };

    for (const Expectation &e : expectations)
    {
        QLCChannel ch;
        ch.setGroup(e.group);
        QCOMPARE(ch.getIconNameFromGroup(e.group, false), QString(e.png));
        QCOMPARE(ch.getIconNameFromGroup(e.group, true), QString(e.svg));
        ch.getIcon(); // resource-based icon; only exercised, not asserted
    }
}

void QLCChannel_Test::searchCapabilityContains()
{
    QLCChannel channel;
    QLCCapability *cap1 = new QLCCapability(0, 9, "Red gobo");
    QLCCapability *cap2 = new QLCCapability(10, 19, "Blue gobo");
    QVERIFY(channel.addCapability(cap1) == true);
    QVERIFY(channel.addCapability(cap2) == true);

    QVERIFY(channel.searchCapability("Blue", false) == cap2);
    QVERIFY(channel.searchCapability("gobo", false) == cap1);
    QVERIFY(channel.searchCapability("Blue", true) == NULL);
    QVERIFY(channel.searchCapability("Nope", false) == NULL);
}

void QLCChannel_Test::setCapabilityRange()
{
    QLCChannel channel;
    QLCCapability *capA = new QLCCapability(0, 9, "A");
    QLCCapability *capB = new QLCCapability(10, 19, "B");
    QVERIFY(channel.addCapability(capA) == true);
    QVERIFY(channel.addCapability(capB) == true);

    /* Shrinking a range never overlaps */
    QVERIFY(channel.setCapabilityRange(capA, 0, 5) == true);
    QCOMPARE(capA->min(), uchar(0));
    QCOMPARE(capA->max(), uchar(5));

    /* Growing into a neighbour is rejected and rolled back */
    QVERIFY(channel.setCapabilityRange(capB, 5, 19) == false);
    QCOMPARE(capB->min(), uchar(10));
    QCOMPARE(capB->max(), uchar(19));

    /* Growing into free space is accepted */
    QVERIFY(channel.setCapabilityRange(capB, 6, 30) == true);
    QCOMPARE(capB->min(), uchar(6));
    QCOMPARE(capB->max(), uchar(30));

    /* Re-applying the same range is fine (the capability itself is skipped) */
    QVERIFY(channel.setCapabilityRange(capB, 6, 30) == true);
}

void QLCChannel_Test::savePreset()
{
    QLCChannel channel;
    channel.setPreset(QLCChannel::IntensityDimmer);
    channel.setDefaultValue(100);
    channel.addPresetCapability();

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(channel.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    xmlReader.readNextStartElement();
    QCOMPARE(xmlReader.name().toString(), QString("Channel"));
    QCOMPARE(xmlReader.attributes().value("Name").toString(), QString("Dimmer"));
    QCOMPARE(xmlReader.attributes().value("Default").toString(), QString("100"));
    QCOMPARE(xmlReader.attributes().value("Preset").toString(), QString("IntensityDimmer"));

    /* A preset channel is fully described by its preset: no Group,
       Colour or Capability children are written */
    QVERIFY(xmlReader.readNextStartElement() == false);
}

void QLCChannel_Test::saveColour()
{
    QLCChannel channel;
    channel.setName("Green");
    channel.setGroup(QLCChannel::Intensity);
    channel.setColour(QLCChannel::Green);
    QVERIFY(channel.addCapability(new QLCCapability(0, 255, "Green intensity")) == true);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(channel.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    xmlReader.readNextStartElement();
    QCOMPARE(xmlReader.name().toString(), QString("Channel"));
    QVERIFY(xmlReader.attributes().hasAttribute("Default") == false);
    QVERIFY(xmlReader.attributes().hasAttribute("Preset") == false);

    bool group = false, colour = false, capability = false;
    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name().toString() == "Group")
        {
            group = true;
            QCOMPARE(xmlReader.attributes().value("Byte").toString(), QString("0"));
            QCOMPARE(xmlReader.readElementText(), QString("Intensity"));
        }
        else if (xmlReader.name().toString() == "Colour")
        {
            colour = true;
            QCOMPARE(xmlReader.readElementText(), QString("Green"));
        }
        else if (xmlReader.name().toString() == "Capability")
        {
            capability = true;
            QCOMPARE(xmlReader.readElementText(), QString("Green intensity"));
        }
        else
        {
            QFAIL(qPrintable(QString("Unexpected tag: %1").arg(xmlReader.name().toString())));
        }
    }

    QVERIFY(group == true);
    QVERIFY(colour == true);
    QVERIFY(capability == true);
}

void QLCChannel_Test::loadPreset()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Channel");
    xmlWriter.writeAttribute("Name", "Zoom");
    xmlWriter.writeAttribute("Default", "12");
    xmlWriter.writeAttribute("Preset", "BeamZoomSmallBig");
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCChannel ch;
    QVERIFY(ch.loadXML(xmlReader) == true);
    QCOMPARE(ch.name(), QString("Zoom"));
    QCOMPARE(ch.defaultValue(), uchar(12));
    QCOMPARE(ch.preset(), QLCChannel::BeamZoomSmallBig);
    QCOMPARE(ch.group(), QLCChannel::Beam);
    QCOMPARE(ch.controlByte(), QLCChannel::MSB);

    /* the preset capability is generated on load */
    QCOMPARE(ch.capabilities().size(), 1);
    QCOMPARE(ch.capabilities().first()->name(), QString("Zoom (Small to big)"));
    QCOMPARE(ch.capabilities().first()->min(), uchar(0));
    QCOMPARE(ch.capabilities().first()->max(), uchar(255));
}

void QLCChannel_Test::loadNoName()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Channel");
    xmlWriter.writeStartElement("Group");
    xmlWriter.writeAttribute("Byte", "0");
    xmlWriter.writeCharacters("Pan");
    xmlWriter.writeEndElement();
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCChannel ch;
    QVERIFY(ch.loadXML(xmlReader) == false);
    QVERIFY(ch.name().isEmpty());
    QCOMPARE(ch.group(), QLCChannel::Intensity);
}

void QLCChannel_Test::loadUnknownTag()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Channel");
    xmlWriter.writeAttribute("Name", "Weird");

    xmlWriter.writeStartElement("Foo");
    xmlWriter.writeAttribute("Bar", "Baz");
    xmlWriter.writeTextElement("Nested", "ignored");
    xmlWriter.writeEndElement();

    xmlWriter.writeTextElement("Colour", "Magenta");

    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCChannel ch;
    QVERIFY(ch.loadXML(xmlReader) == true);
    QCOMPARE(ch.name(), QString("Weird"));
    /* the unknown subtree was skipped, the following tag still parsed */
    QCOMPARE(ch.colour(), QLCChannel::Magenta);
    QCOMPARE(ch.capabilities().size(), 0);
}

/* QTEST_MAIN (a QGuiApplication): icons() paints QPixmaps through QPainter,
   which needs a GUI application instance. */
QTEST_MAIN(QLCChannel_Test)
