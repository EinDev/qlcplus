/*
  Q Light Controller - Unit tests
  qlcinputprofile_test.cpp

  Copyright (C) Heikki Junnila

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

#include <QXmlStreamWriter>
#include <QtTest>

#include "qlcinputprofile_test.h"
#include "qlcinputprofile.h"
#include "qlcinputchannel.h"
#include "qlcchannel.h"

#include "../common/resource_paths.h"

void QLCInputProfile_Test::manufacturer()
{
    QLCInputProfile ip;
    QVERIFY(ip.manufacturer().isEmpty());
    ip.setManufacturer("Behringer");
    QVERIFY(ip.manufacturer() == "Behringer");
}

void QLCInputProfile_Test::model()
{
    QLCInputProfile ip;
    QVERIFY(ip.model().isEmpty());
    ip.setModel("BCF2000");
    QVERIFY(ip.model() == "BCF2000");
}

void QLCInputProfile_Test::name()
{
    QLCInputProfile ip;
    QVERIFY(ip.name() == " ");
    ip.setManufacturer("Behringer");
    QVERIFY(ip.name() == "Behringer ");
    ip.setModel("BCF2000");
    QVERIFY(ip.name() == "Behringer BCF2000");
}

void QLCInputProfile_Test::addChannel()
{
    QLCInputProfile ip;

    QLCInputChannel* ich1 = new QLCInputChannel();
    ip.insertChannel(0, ich1);
    QVERIFY(ip.channel(0) == ich1);
    QVERIFY(ip.channels().size() == 1);

    /* Shouldn't overwrite the existing mapping */
    QLCInputChannel* ich2 = new QLCInputChannel();
    ip.insertChannel(0, ich2);
    QVERIFY(ip.channel(0) == ich1);

    ip.insertChannel(5, ich2);
    QVERIFY(ip.channel(0) == ich1);
    QVERIFY(ip.channel(1) == NULL);
    QVERIFY(ip.channel(2) == NULL);
    QVERIFY(ip.channel(3) == NULL);
    QVERIFY(ip.channel(4) == NULL);
    QVERIFY(ip.channel(5) == ich2);
}

void QLCInputProfile_Test::removeChannel()
{
    QLCInputProfile ip;

    QLCInputChannel* ich1 = new QLCInputChannel();
    ip.insertChannel(0, ich1);
    QVERIFY(ip.channel(0) == ich1);

    QLCInputChannel* ich2 = new QLCInputChannel();
    ip.insertChannel(5, ich2);
    QVERIFY(ip.channel(0) == ich1);
    QVERIFY(ip.channel(5) == ich2);

    QVERIFY(ip.removeChannel(1) == false);
    QVERIFY(ip.removeChannel(2) == false);
    QVERIFY(ip.removeChannel(3) == false);
    QVERIFY(ip.removeChannel(4) == false);

    QVERIFY(ip.channel(0) == ich1);
    QVERIFY(ip.channel(5) == ich2);
    QVERIFY(ip.removeChannel(0) == true);
    QVERIFY(ip.channel(0) == NULL);
    QVERIFY(ip.channel(5) == ich2);
    QVERIFY(ip.removeChannel(5) == true);
    QVERIFY(ip.channel(0) == NULL);
    QVERIFY(ip.channel(5) == NULL);
}

void QLCInputProfile_Test::remapChannel()
{
    QLCInputProfile ip;

    QLCInputChannel* ich1 = new QLCInputChannel();
    ich1->setName("Foobar");
    ip.insertChannel(0, ich1);
    QVERIFY(ip.channel(0) == ich1);

    QLCInputChannel* ich2 = new QLCInputChannel();
    ip.insertChannel(5, ich2);
    QVERIFY(ip.channel(0) == ich1);
    QVERIFY(ip.channel(5) == ich2);

    QVERIFY(ip.remapChannel(ich1, 9000) == true);
    QVERIFY(ip.channel(0) == NULL);
    QVERIFY(ip.channel(5) == ich2);
    QVERIFY(ip.channel(9000) == ich1);
    QVERIFY(ip.channel(9000)->name() == "Foobar");

    QVERIFY(ip.remapChannel(NULL, 9000) == false);
    QVERIFY(ip.channels().size() == 2);
    QVERIFY(ip.channel(0) == NULL);
    QVERIFY(ip.channel(5) == ich2);
    QVERIFY(ip.channel(9000) == ich1);
    QVERIFY(ip.channel(9000)->name() == "Foobar");

    QLCInputChannel* ich3 = new QLCInputChannel();
    QVERIFY(ip.remapChannel(ich3, 5) == false);
    QVERIFY(ip.channels().size() == 2);
    QVERIFY(ip.channel(0) == NULL);
    QVERIFY(ip.channel(5) == ich2);
    QVERIFY(ip.channel(9000) == ich1);
    QVERIFY(ip.channel(9000)->name() == "Foobar");

    delete ich3;
}

void QLCInputProfile_Test::channel()
{
    QLCInputProfile ip;

    QLCInputChannel* ich1 = new QLCInputChannel();
    ip.insertChannel(0, ich1);

    QLCInputChannel* ich2 = new QLCInputChannel();
    ip.insertChannel(5, ich2);

    QVERIFY(ip.channel(0) == ich1);
    QVERIFY(ip.channel(1) == NULL);
    QVERIFY(ip.channel(2) == NULL);
    QVERIFY(ip.channel(3) == NULL);
    QVERIFY(ip.channel(4) == NULL);
    QVERIFY(ip.channel(5) == ich2);
}

void QLCInputProfile_Test::channels()
{
    QLCInputProfile ip;
    QVERIFY(ip.channels().size() == 0);

    QLCInputChannel* ich1 = new QLCInputChannel();
    ip.insertChannel(0, ich1);

    QLCInputChannel* ich2 = new QLCInputChannel();
    ip.insertChannel(5, ich2);

    QVERIFY(ip.channels().size() == 2);
    QVERIFY(ip.channels().contains(0) == true);
    QVERIFY(ip.channels().contains(1) == false);
    QVERIFY(ip.channels().contains(2) == false);
    QVERIFY(ip.channels().contains(3) == false);
    QVERIFY(ip.channels().contains(4) == false);
    QVERIFY(ip.channels().contains(5) == true);
    QVERIFY(ip.channels()[0] == ich1);
    QVERIFY(ip.channels()[5] == ich2);
}

void QLCInputProfile_Test::channelNumber()
{
    QLCInputProfile ip;
    QVERIFY(ip.channels().size() == 0);

    QLCInputChannel* ich1 = new QLCInputChannel();
    ip.insertChannel(0, ich1);

    QLCInputChannel* ich2 = new QLCInputChannel();
    ip.insertChannel(6510, ich2);

    QLCInputChannel* ich3 = new QLCInputChannel();
    ip.insertChannel(5, ich3);

    QCOMPARE(ip.channelNumber(NULL), QLCChannel::invalid());
    QCOMPARE(ip.channelNumber(ich1), quint32(0));
    QCOMPARE(ip.channelNumber(ich2), quint32(6510));
    QCOMPARE(ip.channelNumber(ich3), quint32(5));
}

void QLCInputProfile_Test::copy()
{
    QLCInputProfile ip;
    ip.setManufacturer("Behringer");
    ip.setModel("BCF2000");

    QLCInputChannel* ich1 = new QLCInputChannel();
    ich1->setName("Channel 1");
    ip.insertChannel(0, ich1);

    QLCInputChannel* ich2 = new QLCInputChannel();
    ich2->setName("Channel 2");
    ip.insertChannel(5, ich2);

    QLCInputChannel* ich3 = new QLCInputChannel();
    ich3->setName("Channel 3");
    ip.insertChannel(2, ich3);

    QLCInputChannel* ich4 = new QLCInputChannel();
    ich4->setName("Channel 4");
    ip.insertChannel(9000, ich4);

    QLCInputProfile *copy = ip.createCopy();
    QVERIFY(copy->manufacturer() == "Behringer");
    QVERIFY(copy->model() == "BCF2000");

    QVERIFY(copy->channels().size() == 4);

    /* Verify that it's a deep copy */
    QVERIFY(copy->channel(0) != ich1);
    QVERIFY(copy->channel(0) != NULL);
    QVERIFY(copy->channel(0)->name() == "Channel 1");

    QVERIFY(copy->channel(5) != ich2);
    QVERIFY(copy->channel(5) != NULL);
    QVERIFY(copy->channel(5)->name() == "Channel 2");

    QVERIFY(copy->channel(2) != ich3);
    QVERIFY(copy->channel(2) != NULL);
    QVERIFY(copy->channel(2)->name() == "Channel 3");

    QVERIFY(copy->channel(9000) != ich4);
    QVERIFY(copy->channel(9000) != NULL);
    QVERIFY(copy->channel(9000)->name() == "Channel 4");
}

void QLCInputProfile_Test::assign()
{
    QLCInputProfile ip;
    ip.setManufacturer("Behringer");
    ip.setModel("BCF2000");

    QLCInputChannel* ich1 = new QLCInputChannel;
    ich1->setName("Channel 1");
    ip.insertChannel(0, ich1);

    QLCInputChannel* ich2 = new QLCInputChannel;
    ich2->setName("Channel 2");
    ip.insertChannel(5, ich2);

    QLCInputChannel* ich3 = new QLCInputChannel;
    ich3->setName("Channel 3");
    ip.insertChannel(2, ich3);

    QLCInputChannel* ich4 = new QLCInputChannel;
    ich4->setName("Channel 4");
    ip.insertChannel(9000, ich4);

    QLCInputProfile ip2;
    QLCInputChannel* ich5 = new QLCInputChannel;
    ich5->setName("First channel");
    ip2.insertChannel(0, ich5);
    QCOMPARE(ip2.channels().size(), 1);

    /* Test the assignment operator */
    ip2 = ip;
    QCOMPARE(ip2.channels().size(), 4);
    QVERIFY(ip2.channel(0) != NULL);
    QVERIFY(ip2.channel(0) != ich1);
    QVERIFY(ip2.channel(0) != ip.channel(0));
    QCOMPARE(ip2.channel(0)->name(), QString("Channel 1"));

    QVERIFY(ip2.channel(5) != NULL);
    QVERIFY(ip2.channel(5) != ich2);
    QVERIFY(ip2.channel(5) != ip.channel(5));
    QCOMPARE(ip2.channel(5)->name(), QString("Channel 2"));

    QVERIFY(ip2.channel(2) != NULL);
    QVERIFY(ip2.channel(2) != ich3);
    QVERIFY(ip2.channel(2) != ip.channel(2));
    QCOMPARE(ip2.channel(2)->name(), QString("Channel 3"));

    QVERIFY(ip2.channel(9000) != NULL);
    QVERIFY(ip2.channel(9000) != ich4);
    QVERIFY(ip2.channel(9000) != ip.channel(9000));
    QCOMPARE(ip2.channel(9000)->name(), QString("Channel 4"));
}

void QLCInputProfile_Test::load()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartDocument();
    xmlWriter.writeDTD("<!DOCTYPE InputProfile>");
    xmlWriter.writeStartElement("InputProfile");
    xmlWriter.writeTextElement("Manufacturer", "Behringer");
    xmlWriter.writeTextElement("Model", "BCF2000");
    xmlWriter.writeStartElement("Channel");
    xmlWriter.writeAttribute("Number", "492");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Type", "Slider");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    QLCInputProfile ip;
    QVERIFY(ip.loadXML(xmlReader) == true);
    QVERIFY(ip.manufacturer() == "Behringer");
    QVERIFY(ip.model() == "BCF2000");
    QVERIFY(ip.channels().size() == 1);
    QVERIFY(ip.channel(492) != NULL);
    QVERIFY(ip.channel(492)->name() == "Foobar");
    QVERIFY(ip.channel(492)->type() == QLCInputChannel::Slider);
}

void QLCInputProfile_Test::loadNoProfile()
{
    QXmlStreamReader doc;
    QLCInputProfile ip;
    QVERIFY(ip.loadXML(doc) == false);

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    QXmlStreamWriter xmlWriter(&buffer);
    xmlWriter.writeStartElement("Whatever");

    doc.setDevice(&buffer);
    QVERIFY(ip.loadXML(doc) == false);
}

void QLCInputProfile_Test::loader()
{
    QLCInputProfile* prof = QLCInputProfile::loader("foobar");
    QVERIFY(prof == NULL);

    prof = QLCInputProfile::loader("broken.xml");
    QVERIFY(prof == NULL);

    QString path(INTERNAL_PROFILEDIR "Generic-MIDI.qxi");
    prof = QLCInputProfile::loader(path);
    QVERIFY(prof != NULL);
    QCOMPARE(prof->path(), path);
    QCOMPARE(prof->name(), QString("Generic MIDI"));
    QCOMPARE(prof->channels().size(), 256);
}

void QLCInputProfile_Test::save()
{
    QLCInputProfile ip;
    ip.setManufacturer("TestManufacturer");
    ip.setModel("TestModel");

    QLCInputChannel* ich1 = new QLCInputChannel();
    ich1->setName("Channel 1");
    ip.insertChannel(0, ich1);

    QLCInputChannel* ich2 = new QLCInputChannel();
    ich2->setName("Channel 2");
    ip.insertChannel(5, ich2);

    QLCInputChannel* ich3 = new QLCInputChannel();
    ich3->setName("Channel 3");
    ip.insertChannel(2, ich3);

    QString path("test.qxi");
    QVERIFY(ip.saveXML(path) == true);

#if !defined(WIN32) && !defined(Q_OS_WIN)
    QFile::Permissions perm = QFile::permissions(path);
    QFile::setPermissions(path, QFileDevice::WriteOther);
    QVERIFY(ip.saveXML(path) == false);
    QFile::setPermissions(path, perm);
#endif

    QLCInputProfile* prof = QLCInputProfile::loader(path);
    QVERIFY(prof != NULL);
    QCOMPARE(prof->manufacturer(), ip.manufacturer());
    QCOMPARE(prof->model(), ip.model());
    QCOMPARE(prof->name(), ip.name());
    QCOMPARE(prof->channels().size(), ip.channels().size());
    QCOMPARE(prof->channels()[0]->name(), ich1->name());
    QCOMPARE(prof->channels()[2]->name(), ich3->name());
    QCOMPARE(prof->channels()[5]->name(), ich2->name());

    QVERIFY(QFile::remove(path) == true);
    delete prof;
}

void QLCInputProfile_Test::types()
{
    QList<QLCInputProfile::Type> list = QLCInputProfile::types();
    QCOMPARE(list.size(), 6);
    QCOMPARE(list.at(0), QLCInputProfile::MIDI);
    QCOMPARE(list.at(1), QLCInputProfile::OS2L);
    QCOMPARE(list.at(2), QLCInputProfile::OSC);
    QCOMPARE(list.at(3), QLCInputProfile::HID);
    QCOMPARE(list.at(4), QLCInputProfile::DMX);
    QCOMPARE(list.at(5), QLCInputProfile::Enttec);

    QCOMPARE(QLCInputProfile::typeToString(QLCInputProfile::MIDI), QString("MIDI"));
    QCOMPARE(QLCInputProfile::typeToString(QLCInputProfile::OS2L), QString("OS2L"));
    QCOMPARE(QLCInputProfile::typeToString(QLCInputProfile::OSC), QString("OSC"));
    QCOMPARE(QLCInputProfile::typeToString(QLCInputProfile::HID), QString("HID"));
    QCOMPARE(QLCInputProfile::typeToString(QLCInputProfile::DMX), QString("DMX"));
    QCOMPARE(QLCInputProfile::typeToString(QLCInputProfile::Enttec), QString("Enttec"));
    QVERIFY(QLCInputProfile::typeToString(QLCInputProfile::Type(42)).isEmpty());

    foreach (QLCInputProfile::Type type, list)
        QCOMPARE(QLCInputProfile::stringToType(QLCInputProfile::typeToString(type)), type);
    QCOMPARE(QLCInputProfile::stringToType("bogus"), QLCInputProfile::Enttec);
    QCOMPARE(QLCInputProfile::stringToType(""), QLCInputProfile::Enttec);

    QLCInputProfile ip;
    QCOMPARE(ip.type(), QLCInputProfile::MIDI);
    ip.setType(QLCInputProfile::OSC);
    QCOMPARE(ip.type(), QLCInputProfile::OSC);
}

void QLCInputProfile_Test::channelExtraParams()
{
    QLCInputProfile ip;
    QLCInputChannel *ch = new QLCInputChannel();
    ch->setName("Fader 1");
    ch->setLowerChannel(5);
    QVERIFY(ip.insertChannel(0, ch) == true);

    QVERIFY(ip.channelExtraParams(NULL).isValid() == false);

    // OSC feedback needs the channel path (its name)
    ip.setType(QLCInputProfile::OSC);
    QCOMPARE(ip.channelExtraParams(ch).toString(), QString("Fader 1"));

    // MIDI feedback needs the channel modifier
    ip.setType(QLCInputProfile::MIDI);
    QCOMPARE(ip.channelExtraParams(ch).toInt(), 5);

    // other profile types carry no extra parameters
    ip.setType(QLCInputProfile::HID);
    QVERIFY(ip.channelExtraParams(ch).isValid() == false);
    ip.setType(QLCInputProfile::DMX);
    QVERIFY(ip.channelExtraParams(ch).isValid() == false);
}

void QLCInputProfile_Test::colorAndMidiChannelTables()
{
    QLCInputProfile ip;

    QVERIFY(ip.hasColorTable() == false);
    QVERIFY(ip.colorTable().isEmpty());

    ip.addColor(1, "Red", QColor(Qt::red));
    ip.addColor(5, "Blue", QColor(Qt::blue));
    QVERIFY(ip.hasColorTable() == true);
    QCOMPARE(ip.colorTable().size(), 2);
    QCOMPARE(ip.colorTable().value(1).first, QString("Red"));
    QCOMPARE(ip.colorTable().value(1).second, QColor(Qt::red));
    QCOMPARE(ip.colorTable().value(5).first, QString("Blue"));
    QCOMPARE(ip.colorTable().value(5).second, QColor(Qt::blue));

    // adding the same value again replaces the entry
    ip.addColor(1, "Dark red", QColor(128, 0, 0));
    QCOMPARE(ip.colorTable().size(), 2);
    QCOMPARE(ip.colorTable().value(1).first, QString("Dark red"));
    QCOMPARE(ip.colorTable().value(1).second, QColor(128, 0, 0));

    ip.removeColor(1);
    QCOMPARE(ip.colorTable().size(), 1);
    QVERIFY(ip.colorTable().contains(1) == false);
    ip.removeColor(42);
    QCOMPARE(ip.colorTable().size(), 1);
    ip.removeColor(5);
    QVERIFY(ip.hasColorTable() == false);

    QVERIFY(ip.hasMidiChannelTable() == false);
    QVERIFY(ip.midiChannelTable().isEmpty());

    ip.addMidiChannel(0, "Ch 1");
    ip.addMidiChannel(9, "Drums");
    QVERIFY(ip.hasMidiChannelTable() == true);
    QCOMPARE(ip.midiChannelTable().size(), 2);
    QCOMPARE(ip.midiChannelTable().value(0), QString("Ch 1"));
    QCOMPARE(ip.midiChannelTable().value(9), QString("Drums"));

    ip.addMidiChannel(9, "Percussion");
    QCOMPARE(ip.midiChannelTable().size(), 2);
    QCOMPARE(ip.midiChannelTable().value(9), QString("Percussion"));

    ip.removeMidiChannel(0);
    QCOMPARE(ip.midiChannelTable().size(), 1);
    ip.removeMidiChannel(42);
    QCOMPARE(ip.midiChannelTable().size(), 1);
    ip.removeMidiChannel(9);
    QVERIFY(ip.hasMidiChannelTable() == false);
}

void QLCInputProfile_Test::copyAndAssignTables()
{
    QLCInputProfile ip;
    ip.setManufacturer("Maker");
    ip.setModel("Tables");
    ip.setType(QLCInputProfile::OSC);
    ip.setMidiSendNoteOff(false);
    ip.addColor(3, "Green", QColor(Qt::green));
    ip.addMidiChannel(2, "Keys");
    QLCInputChannel *ch = new QLCInputChannel();
    ch->setName("Btn");
    ch->setType(QLCInputChannel::Button);
    QVERIFY(ip.insertChannel(7, ch) == true);

    QLCInputProfile *copy = ip.createCopy();
    QVERIFY(copy != NULL);
    QVERIFY(copy != &ip);
    QCOMPARE(copy->name(), ip.name());
    QCOMPARE(copy->type(), QLCInputProfile::OSC);
    QCOMPARE(copy->midiSendNoteOff(), false);
    QCOMPARE(copy->globalSettings().value("MIDISendNoteOff").toBool(), false);
    QVERIFY(copy->hasColorTable() == true);
    QCOMPARE(copy->colorTable().size(), 1);
    QCOMPARE(copy->colorTable().value(3).first, QString("Green"));
    QCOMPARE(copy->colorTable().value(3).second, QColor(Qt::green));
    QVERIFY(copy->hasMidiChannelTable() == true);
    QCOMPARE(copy->midiChannelTable().value(2), QString("Keys"));
    QCOMPARE(copy->channels().size(), 1);
    QVERIFY(copy->channel(7) != NULL);
    QVERIFY(copy->channel(7) != ch);
    QCOMPARE(copy->channel(7)->name(), QString("Btn"));
    QCOMPARE(copy->channel(7)->type(), QLCInputChannel::Button);
    delete copy;

    // assignment replaces the target's own tables and channels
    QLCInputProfile other;
    other.setManufacturer("Old");
    other.addColor(9, "Old", QColor(Qt::black));
    other.addMidiChannel(1, "Old");
    QLCInputChannel *oldCh = new QLCInputChannel();
    oldCh->setName("Old");
    QVERIFY(other.insertChannel(1, oldCh) == true);

    other = ip;
    QCOMPARE(other.name(), ip.name());
    QCOMPARE(other.type(), QLCInputProfile::OSC);
    QCOMPARE(other.midiSendNoteOff(), false);
    QCOMPARE(other.globalSettings().value("MIDISendNoteOff").toBool(), false);
    QCOMPARE(other.colorTable().size(), 1);
    QVERIFY(other.colorTable().contains(9) == false);
    QCOMPARE(other.colorTable().value(3).first, QString("Green"));
    QCOMPARE(other.midiChannelTable().size(), 1);
    QVERIFY(other.midiChannelTable().contains(1) == false);
    QCOMPARE(other.midiChannelTable().value(2), QString("Keys"));
    QCOMPARE(other.channels().size(), 1);
    QVERIFY(other.channel(1) == NULL);
    QVERIFY(other.channel(7) != NULL);
    QVERIFY(other.channel(7) != ch);
    QCOMPARE(other.channel(7)->name(), QString("Btn"));

    // self assignment is a no-op
    other = other;
    QCOMPARE(other.channels().size(), 1);
    QCOMPARE(other.channel(7)->name(), QString("Btn"));
    QCOMPARE(other.colorTable().size(), 1);
}

void QLCInputProfile_Test::saveLoadTables()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QLCInputProfile ip;
    ip.setManufacturer("Test");
    ip.setModel("Tables");
    ip.setType(QLCInputProfile::MIDI);
    ip.setMidiSendNoteOff(false);
    ip.addColor(1, "Red", QColor(255, 0, 0));
    ip.addColor(2, "Blue", QColor(0, 0, 255));
    ip.addMidiChannel(0, "Ch 1");
    ip.addMidiChannel(15, "Ch 16");

    QLCInputChannel *btn = new QLCInputChannel();
    btn->setName("Button");
    btn->setType(QLCInputChannel::Button);
    btn->setRange(10, 200);
    btn->setLowerChannel(3);
    QVERIFY(ip.insertChannel(4, btn) == true);

    QLCInputChannel *sld = new QLCInputChannel();
    sld->setName("Slider");
    sld->setType(QLCInputChannel::Slider);
    sld->setMovementType(QLCInputChannel::Relative);
    sld->setMovementSensitivity(30);
    QVERIFY(ip.insertChannel(9, sld) == true);

    QLCInputChannel *enc = new QLCInputChannel();
    enc->setName("Encoder");
    enc->setType(QLCInputChannel::Encoder);
    enc->setMovementSensitivity(4);
    QVERIFY(ip.insertChannel(11, enc) == true);

    QString path = tmp.filePath("tables.qxi");
    QVERIFY(ip.saveXML(path) == true);
    QCOMPARE(ip.path(), path);

    QLCInputProfile *loaded = QLCInputProfile::loader(path);
    QVERIFY(loaded != NULL);
    QCOMPARE(loaded->path(), path);
    QCOMPARE(loaded->manufacturer(), QString("Test"));
    QCOMPARE(loaded->model(), QString("Tables"));
    QCOMPARE(loaded->type(), QLCInputProfile::MIDI);
    QCOMPARE(loaded->midiSendNoteOff(), false);
    QCOMPARE(loaded->globalSettings().value("MIDISendNoteOff").toBool(), false);

    QVERIFY(loaded->hasColorTable() == true);
    QCOMPARE(loaded->colorTable().size(), 2);
    QCOMPARE(loaded->colorTable().value(1).first, QString("Red"));
    QCOMPARE(loaded->colorTable().value(1).second, QColor(255, 0, 0));
    QCOMPARE(loaded->colorTable().value(2).first, QString("Blue"));
    QCOMPARE(loaded->colorTable().value(2).second, QColor(0, 0, 255));

    QVERIFY(loaded->hasMidiChannelTable() == true);
    QCOMPARE(loaded->midiChannelTable().size(), 2);
    QCOMPARE(loaded->midiChannelTable().value(0), QString("Ch 1"));
    QCOMPARE(loaded->midiChannelTable().value(15), QString("Ch 16"));

    QCOMPARE(loaded->channels().size(), 3);
    QVERIFY(loaded->channel(4) != NULL);
    QCOMPARE(loaded->channel(4)->type(), QLCInputChannel::Button);
    QCOMPARE(loaded->channel(4)->lowerValue(), uchar(10));
    QCOMPARE(loaded->channel(4)->upperValue(), uchar(200));
    QCOMPARE(loaded->channel(4)->lowerChannel(), 3);
    QVERIFY(loaded->channel(9) != NULL);
    QCOMPARE(loaded->channel(9)->type(), QLCInputChannel::Slider);
    QCOMPARE(loaded->channel(9)->movementType(), QLCInputChannel::Relative);
    QCOMPARE(loaded->channel(9)->movementSensitivity(), 30);
    QVERIFY(loaded->channel(11) != NULL);
    QCOMPARE(loaded->channel(11)->type(), QLCInputChannel::Encoder);
    QCOMPARE(loaded->channel(11)->movementType(), QLCInputChannel::Absolute);
    QCOMPARE(loaded->channel(11)->movementSensitivity(), 4);
    delete loaded;

    // a profile without tables and default settings saves none of them
    QLCInputProfile plain;
    plain.setManufacturer("Plain");
    plain.setModel("Profile");
    QString plainPath = tmp.filePath("plain.qxi");
    QVERIFY(plain.saveXML(plainPath) == true);
    QFile file(plainPath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString content = QString::fromUtf8(file.readAll());
    file.close();
    QVERIFY(content.contains("<InputProfile"));
    QVERIFY(content.contains("<Type>MIDI</Type>"));
    QVERIFY(content.contains("ColorTable") == false);
    QVERIFY(content.contains("MidiChannelTable") == false);
    QCOMPARE(content.contains("MIDISendNoteOff"), plain.midiSendNoteOff() == false);
}

void QLCInputProfile_Test::loaderInvalid()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // an empty file has no root element at all
    QString emptyPath = tmp.filePath("empty.qxi");
    {
        QFile file(emptyPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
    }
    QVERIFY(QLCInputProfile::loader(emptyPath) == NULL);

    // a wrong root element is rejected as well
    QString wrongPath = tmp.filePath("wrong.qxi");
    {
        QFile file(wrongPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("<!DOCTYPE InputProfile>\n<Foo><Manufacturer>x</Manufacturer></Foo>\n");
        file.close();
    }
    QVERIFY(QLCInputProfile::loader(wrongPath) == NULL);

    // and a missing file
    QVERIFY(QLCInputProfile::loader(tmp.filePath("missing.qxi")) == NULL);
}

void QLCInputProfile_Test::loadTablesXMLErrors()
{
    QLCInputProfile ip;

    // wrong root elements for the table loaders
    {
        QXmlStreamReader reader("<Foo/>");
        reader.readNextStartElement();
        QVERIFY(ip.loadColorTableXML(reader) == false);
    }
    {
        QXmlStreamReader reader("<Foo/>");
        reader.readNextStartElement();
        QVERIFY(ip.loadMidiChannelTableXML(reader) == false);
    }
    QVERIFY(ip.hasColorTable() == false);
    QVERIFY(ip.hasMidiChannelTable() == false);

    // unknown child tags are skipped with a warning, known ones are loaded
    {
        QXmlStreamReader reader("<ColorTable><Bogus/>"
                                "<Color Value=\"7\" Label=\"Amber\" RGB=\"#ffbf00\"/>"
                                "</ColorTable>");
        reader.readNextStartElement();
        QVERIFY(ip.loadColorTableXML(reader) == true);
        QCOMPARE(ip.colorTable().size(), 1);
        QCOMPARE(ip.colorTable().value(7).first, QString("Amber"));
        QCOMPARE(ip.colorTable().value(7).second, QColor("#ffbf00"));
    }
    {
        QXmlStreamReader reader("<MidiChannelTable><Bogus/>"
                                "<Channel Value=\"3\" Label=\"Bass\"/>"
                                "</MidiChannelTable>");
        reader.readNextStartElement();
        QVERIFY(ip.loadMidiChannelTableXML(reader) == true);
        QCOMPARE(ip.midiChannelTable().size(), 1);
        QCOMPARE(ip.midiChannelTable().value(3), QString("Bass"));
    }

    // a full profile with unknown tags and a channel without a number
    {
        QXmlStreamReader reader("<InputProfile>"
                                "<Manufacturer>M</Manufacturer>"
                                "<Model>X</Model>"
                                "<Type>OSC</Type>"
                                "<MIDISendNoteOff>False</MIDISendNoteOff>"
                                "<Creator><Name>x</Name></Creator>"
                                "<Unknown>?</Unknown>"
                                "<Channel><Name>NoNumber</Name></Channel>"
                                "<Channel Number=\"2\"><Name>Two</Name><Type>Button</Type></Channel>"
                                "<ColorTable><Color Value=\"1\" Label=\"R\" RGB=\"#ff0000\"/></ColorTable>"
                                "<MidiChannelTable><Channel Value=\"0\" Label=\"One\"/></MidiChannelTable>"
                                "</InputProfile>");
        QLCInputProfile profile;
        QVERIFY(profile.loadXML(reader) == true);
        QCOMPARE(profile.manufacturer(), QString("M"));
        QCOMPARE(profile.model(), QString("X"));
        QCOMPARE(profile.type(), QLCInputProfile::OSC);
        QCOMPARE(profile.midiSendNoteOff(), false);
        QCOMPARE(profile.channels().size(), 1);
        QVERIFY(profile.channel(2) != NULL);
        QCOMPARE(profile.channel(2)->name(), QString("Two"));
        QCOMPARE(profile.channel(2)->type(), QLCInputChannel::Button);
        QCOMPARE(profile.colorTable().size(), 1);
        QCOMPARE(profile.colorTable().value(1).first, QString("R"));
        QCOMPARE(profile.midiChannelTable().size(), 1);
        QCOMPARE(profile.midiChannelTable().value(0), QString("One"));
    }
}

void QLCInputProfile_Test::saveUnwritable()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    QLCInputProfile ip;
    ip.setManufacturer("No");
    ip.setModel("Write");

    // a directory cannot be opened for writing
    QVERIFY(ip.saveXML(tmp.path()) == false);
    QVERIFY(ip.path().isEmpty());

    // neither can a path inside a directory that doesn't exist
    QVERIFY(ip.saveXML(tmp.filePath("missing/dir/profile.qxi")) == false);
    QVERIFY(ip.path().isEmpty());
}

QTEST_APPLESS_MAIN(QLCInputProfile_Test)
