/*
  Q Light Controller Plus - Unit tests
  qlcinputchannel_test.cpp

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

#include "qlcinputchannel_test.h"
#include "qlcinputchannel.h"

void QLCInputChannel_Test::types()
{
    QStringList list(QLCInputChannel::types());
    QVERIFY(list.size() == 7);
    QVERIFY(list.contains(KXMLQLCInputChannelButton));
    QVERIFY(list.contains(KXMLQLCInputChannelSlider));
    QVERIFY(list.contains(KXMLQLCInputChannelKnob));
    QVERIFY(list.contains(KXMLQLCInputChannelEncoder));
    QVERIFY(list.contains(KXMLQLCInputChannelPageUp));
    QVERIFY(list.contains(KXMLQLCInputChannelPageDown));
    QVERIFY(list.contains(KXMLQLCInputChannelPageSet));
}

void QLCInputChannel_Test::type()
{
    QLCInputChannel ch;
    QVERIFY(ch.type() == QLCInputChannel::Button);

    ch.setType(QLCInputChannel::Slider);
    QVERIFY(ch.type() == QLCInputChannel::Slider);

    ch.setType(QLCInputChannel::Button);
    QVERIFY(ch.type() == QLCInputChannel::Button);

    ch.setType(QLCInputChannel::Knob);
    QVERIFY(ch.type() == QLCInputChannel::Knob);

    ch.setType(QLCInputChannel::Encoder);
    QVERIFY(ch.type() == QLCInputChannel::Encoder);

    ch.setType(QLCInputChannel::NextPage);
    QVERIFY(ch.type() == QLCInputChannel::NextPage);

    ch.setType(QLCInputChannel::PrevPage);
    QVERIFY(ch.type() == QLCInputChannel::PrevPage);

    ch.setType(QLCInputChannel::PageSet);
    QVERIFY(ch.type() == QLCInputChannel::PageSet);
}

void QLCInputChannel_Test::typeToString()
{
    QCOMPARE(QLCInputChannel::typeToString(QLCInputChannel::Button),
             QString(KXMLQLCInputChannelButton));
    QCOMPARE(QLCInputChannel::typeToString(QLCInputChannel::Knob),
             QString(KXMLQLCInputChannelKnob));
    QCOMPARE(QLCInputChannel::typeToString(QLCInputChannel::Encoder),
             QString(KXMLQLCInputChannelEncoder));
    QCOMPARE(QLCInputChannel::typeToString(QLCInputChannel::Slider),
             QString(KXMLQLCInputChannelSlider));
    QCOMPARE(QLCInputChannel::typeToString(QLCInputChannel::NextPage),
             QString(KXMLQLCInputChannelPageUp));
    QCOMPARE(QLCInputChannel::typeToString(QLCInputChannel::PrevPage),
             QString(KXMLQLCInputChannelPageDown));
    QCOMPARE(QLCInputChannel::typeToString(QLCInputChannel::PageSet),
             QString(KXMLQLCInputChannelPageSet));
    QCOMPARE(QLCInputChannel::typeToString(QLCInputChannel::Type(42)),
             QString(KXMLQLCInputChannelNone));
}

void QLCInputChannel_Test::stringToType()
{
    QCOMPARE(QLCInputChannel::stringToType(QString(KXMLQLCInputChannelButton)),
             QLCInputChannel::Button);
    QCOMPARE(QLCInputChannel::stringToType(QString(KXMLQLCInputChannelSlider)),
             QLCInputChannel::Slider);
    QCOMPARE(QLCInputChannel::stringToType(QString(KXMLQLCInputChannelKnob)),
             QLCInputChannel::Knob);
    QCOMPARE(QLCInputChannel::stringToType(QString(KXMLQLCInputChannelEncoder)),
             QLCInputChannel::Encoder);
    QCOMPARE(QLCInputChannel::stringToType(QString(KXMLQLCInputChannelPageUp)),
             QLCInputChannel::NextPage);
    QCOMPARE(QLCInputChannel::stringToType(QString(KXMLQLCInputChannelPageDown)),
             QLCInputChannel::PrevPage);
    QCOMPARE(QLCInputChannel::stringToType(QString(KXMLQLCInputChannelPageSet)),
             QLCInputChannel::PageSet);
    QCOMPARE(QLCInputChannel::stringToType(QString("foobar")),
             QLCInputChannel::NoType);
}

void QLCInputChannel_Test::name()
{
    QLCInputChannel ch;
    QVERIFY(ch.name().isEmpty());
    ch.setName("Foobar");
    QVERIFY(ch.name() == "Foobar");
}

void QLCInputChannel_Test::copy()
{
    QLCInputChannel ch;
    ch.setType(QLCInputChannel::Slider);
    ch.setName("Foobar");

    QLCInputChannel *copy = ch.createCopy();
    QVERIFY(copy->type() == QLCInputChannel::Slider);
    QVERIFY(copy->name() == "Foobar");

    QLCInputChannel *another = ch.createCopy();
    QVERIFY(another->type() == QLCInputChannel::Slider);
    QVERIFY(another->name() == "Foobar");
}

void QLCInputChannel_Test::load()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Channel");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Type", "Slider");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCInputChannel ch;
    ch.loadXML(xmlReader);
    QVERIFY(ch.name() == "Foobar");
    QVERIFY(ch.type() == QLCInputChannel::Slider);
}

void QLCInputChannel_Test::loadWrongType()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Channel");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Type", "Xyzzy");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCInputChannel ch;
    ch.loadXML(xmlReader);
    QVERIFY(ch.name() == "Foobar");
    QVERIFY(ch.type() == QLCInputChannel::NoType);
}

void QLCInputChannel_Test::save()
{
    QLCInputChannel ch;
    ch.setName("Foobar Name");
    ch.setType(QLCInputChannel::Knob);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(ch.saveXML(&xmlWriter, 12) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    xmlReader.readNextStartElement();

    QCOMPARE(xmlReader.name().toString(), QString(KXMLQLCInputChannel));

    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name() == KXMLQLCInputChannelName)
            QCOMPARE(xmlReader.readElementText(), QString("Foobar Name"));
        else if (xmlReader.name() == KXMLQLCInputChannelType)
            QCOMPARE(xmlReader.readElementText(), QString(KXMLQLCInputChannelKnob));
        else
            QFAIL("Unexpected crap in the XML!");
    }
}

void QLCInputChannel_Test::typeStringAndIcons()
{
    QLCInputChannel ch;
    ch.setType(QLCInputChannel::Knob);
    QCOMPARE(ch.typeString(), QString(KXMLQLCInputChannelKnob));
    ch.setType(QLCInputChannel::PageSet);
    QCOMPARE(ch.typeString(), QString(KXMLQLCInputChannelPageSet));
    ch.setType(QLCInputChannel::NoType);
    QCOMPARE(ch.typeString(), QString(KXMLQLCInputChannelNone));

    // raster resources
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::Button), QString(":/button.png"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::Knob), QString(":/knob.png"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::Encoder), QString(":/knob.png"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::Slider), QString(":/slider.png"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::PrevPage), QString(":/back.png"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::NextPage), QString(":/forward.png"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::PageSet), QString(":/star.png"));
    QVERIFY(QLCInputChannel::iconResource(QLCInputChannel::NoType).isEmpty());

    // vector resources for the QML UI
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::Button, true), QString("qrc:/button.svg"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::Knob, true), QString("qrc:/knob.svg"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::Encoder, true), QString("qrc:/knob.svg"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::Slider, true), QString("qrc:/slider.svg"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::PrevPage, true), QString("qrc:/back.svg"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::NextPage, true), QString("qrc:/forward.svg"));
    QCOMPARE(QLCInputChannel::iconResource(QLCInputChannel::PageSet, true), QString("qrc:/star.svg"));
    QVERIFY(QLCInputChannel::iconResource(QLCInputChannel::NoType, true).isEmpty());
}

void QLCInputChannel_Test::loadExtras()
{
    // wrong root element
    {
        QXmlStreamReader reader("<Foo/>");
        reader.readNextStartElement();
        QLCInputChannel ch;
        QVERIFY(ch.loadXML(reader) == false);
    }

    // not positioned on a start element yet
    {
        QXmlStreamReader reader("<Channel/>");
        QLCInputChannel ch;
        QVERIFY(ch.loadXML(reader) == false);
    }

    // extra press, relative movement with sensitivity, unknown tag
    {
        QXmlStreamReader reader("<Channel Number=\"3\">"
                                "<Name>Fader</Name>"
                                "<Type>Slider</Type>"
                                "<ExtraPress>True</ExtraPress>"
                                "<Movement Sensitivity=\"35\">Relative</Movement>"
                                "<Bogus>x</Bogus>"
                                "</Channel>");
        reader.readNextStartElement();
        QLCInputChannel ch;
        QVERIFY(ch.loadXML(reader) == true);
        QCOMPARE(ch.name(), QString("Fader"));
        QCOMPARE(ch.type(), QLCInputChannel::Slider);
        QCOMPARE(ch.sendExtraPress(), true);
        QCOMPARE(ch.movementType(), QLCInputChannel::Relative);
        QCOMPARE(ch.movementSensitivity(), 35);
    }

    // button feedback range and MIDI channel
    {
        QXmlStreamReader reader("<Channel>"
                                "<Name>Btn</Name>"
                                "<Type>Button</Type>"
                                "<Feedback LowerValue=\"20\" UpperValue=\"180\" MidiChannel=\"6\"/>"
                                "</Channel>");
        reader.readNextStartElement();
        QLCInputChannel ch;
        QVERIFY(ch.loadXML(reader) == true);
        QCOMPARE(ch.type(), QLCInputChannel::Button);
        QCOMPARE(ch.lowerValue(), uchar(20));
        QCOMPARE(ch.upperValue(), uchar(180));
        QCOMPARE(ch.lowerChannel(), 6);
        QCOMPARE(ch.sendExtraPress(), false);
    }

    // encoder movement without a mode, empty feedback keeps the defaults
    {
        QXmlStreamReader reader("<Channel>"
                                "<Type>Encoder</Type>"
                                "<Movement Sensitivity=\"3\"/>"
                                "<Feedback/>"
                                "</Channel>");
        reader.readNextStartElement();
        QLCInputChannel ch;
        QVERIFY(ch.loadXML(reader) == true);
        QCOMPARE(ch.type(), QLCInputChannel::Encoder);
        QCOMPARE(ch.movementType(), QLCInputChannel::Absolute);
        QCOMPARE(ch.movementSensitivity(), 3);
        QCOMPARE(ch.lowerValue(), uchar(0));
        QCOMPARE(ch.upperValue(), uchar(UCHAR_MAX));
        QCOMPARE(ch.lowerChannel(), -1);
    }
}

static QString saveChannelToString(const QLCInputChannel &ch, quint32 number)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    if (ch.saveXML(&xmlWriter, number) == false)
        return QString();
    xmlWriter.setDevice(NULL);
    buffer.close();
    return QString::fromUtf8(buffer.data());
}

void QLCInputChannel_Test::saveVariants()
{
    QLCInputChannel ch;
    QVERIFY(ch.saveXML(NULL, 0) == false);
    QXmlStreamWriter noDevice;
    QVERIFY(ch.saveXML(&noDevice, 0) == false);

    ch.setName("Ctrl");
    ch.setSendExtraPress(true);

    // a relative slider stores its movement and the extra press flag
    ch.setType(QLCInputChannel::Slider);
    ch.setMovementType(QLCInputChannel::Relative);
    ch.setMovementSensitivity(40);
    QString xml = saveChannelToString(ch, 5);
    QVERIFY(xml.contains("<Channel Number=\"5\">"));
    QVERIFY(xml.contains("<Name>Ctrl</Name>"));
    QVERIFY(xml.contains("<Type>Slider</Type>"));
    QVERIFY(xml.contains("<ExtraPress>True</ExtraPress>"));
    QVERIFY(xml.contains("<Movement Sensitivity=\"40\">Relative</Movement>"));
    QVERIFY(xml.contains("Feedback") == false);

    // so does a relative knob
    ch.setType(QLCInputChannel::Knob);
    xml = saveChannelToString(ch, 5);
    QVERIFY(xml.contains("<Movement Sensitivity=\"40\">Relative</Movement>"));

    // an absolute knob stores no movement at all
    ch.setMovementType(QLCInputChannel::Absolute);
    ch.setSendExtraPress(false);
    xml = saveChannelToString(ch, 5);
    QVERIFY(xml.contains("Movement") == false);
    QVERIFY(xml.contains("ExtraPress") == false);

    // an encoder stores only its sensitivity
    ch.setType(QLCInputChannel::Encoder);
    ch.setMovementSensitivity(2);
    xml = saveChannelToString(ch, 6);
    QVERIFY(xml.contains("<Channel Number=\"6\">"));
    QVERIFY(xml.contains("<Movement Sensitivity=\"2\"/>"));

    // a button with the default range stores no feedback
    ch.setType(QLCInputChannel::Button);
    xml = saveChannelToString(ch, 7);
    QVERIFY(xml.contains("Feedback") == false);
    QVERIFY(xml.contains("Movement") == false);

    // a custom range is stored with its MIDI channel
    ch.setRange(10, 200);
    ch.setLowerChannel(4);
    xml = saveChannelToString(ch, 7);
    QVERIFY(xml.contains("<Feedback LowerValue=\"10\" UpperValue=\"200\" MidiChannel=\"4\"/>"));

    // only the bounds that deviate from the defaults are written
    ch.setRange(0, 100);
    ch.setLowerChannel(-1);
    xml = saveChannelToString(ch, 7);
    QVERIFY(xml.contains("<Feedback UpperValue=\"100\"/>"));

    ch.setRange(5, UCHAR_MAX);
    xml = saveChannelToString(ch, 7);
    QVERIFY(xml.contains("<Feedback LowerValue=\"5\"/>"));
}

QTEST_APPLESS_MAIN(QLCInputChannel_Test)
