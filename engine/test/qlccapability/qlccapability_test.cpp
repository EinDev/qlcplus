/*
  Q Light Controller Plus - Unit tests
  qlccapability_test.cpp

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

#include "qlccapability_test.h"
#include "qlccapability.h"
#include "qlcconfig.h"
#include "qlcfile.h"

void QLCCapability_Test::initial()
{
    QLCCapability cap;
    QVERIFY(cap.min() == 0);
    QVERIFY(cap.max() == UCHAR_MAX);
    QVERIFY(cap.name().isEmpty());
}

void QLCCapability_Test::min_data()
{
    QTest::addColumn<uchar> ("value");
    for (uchar i = 0; i < UCHAR_MAX; i++)
        QTest::newRow("foo") << i;
    QTest::newRow("foo") << uchar(UCHAR_MAX);
}

void QLCCapability_Test::min()
{
    QLCCapability cap;
    QVERIFY(cap.min() == 0);

    QFETCH(uchar, value);

    cap.setMin(value);
    QCOMPARE(cap.min(), value);
}

void QLCCapability_Test::max_data()
{
    QTest::addColumn<uchar> ("value");
    for (uchar i = 0; i < UCHAR_MAX; i++)
        QTest::newRow("foo") << i;
    QTest::newRow("foo") << uchar(UCHAR_MAX);
}

void QLCCapability_Test::max()
{
    QLCCapability cap;
    QVERIFY(cap.max() == UCHAR_MAX);

    QFETCH(uchar, value);

    cap.setMax(value);
    QCOMPARE(cap.max(), value);
}

void QLCCapability_Test::middle()
{
    QLCCapability cap;
    QVERIFY(cap.max() == UCHAR_MAX);
    QCOMPARE(cap.middle(), uchar(127));
    cap.setMin(100);
    cap.setMax(200);
    QCOMPARE(cap.middle(), uchar(150));
}

void QLCCapability_Test::name()
{
    QLCCapability cap;
    QVERIFY(cap.name().isEmpty());

    cap.setName("Foobar");
    QVERIFY(cap.name() == "Foobar");
}

void QLCCapability_Test::alias()
{
    QLCCapability cap;
    AliasInfo info1, info2;
    info1.sourceChannel = "Channel 1";
    info1.targetChannel = "Channel 3";
    info1.targetMode = "12 Channel";

    info2.sourceChannel = "Foo";
    info2.targetChannel = "Bar";
    info2.targetMode = "Mode";

    cap.addAlias(info1);
    QVERIFY(cap.aliasList().count() == 1);

    cap.removeAlias(info1);
    QVERIFY(cap.aliasList().count() == 0);

    cap.addAlias(info1);
    cap.addAlias(info2);
    QVERIFY(cap.aliasList().count() == 2);

    cap.removeAlias(info1);
    QVERIFY(cap.aliasList().count() == 1);

    info1.sourceChannel = "John";
    info1.targetChannel = "Doe";
    QList<AliasInfo> aliasList;
    aliasList << info1 << info2;

    cap.replaceAliases(aliasList);
    QVERIFY(cap.aliasList().count() == 2);
    QVERIFY(cap.aliasList().first().sourceChannel == "John");
    QVERIFY(cap.aliasList().first().targetChannel == "Doe");
}

void QLCCapability_Test::overlaps()
{
    QLCCapability cap1;
    QLCCapability cap2;
    QVERIFY(cap1.overlaps(&cap2) == true);
    QVERIFY(cap2.overlaps(&cap1) == true);

    /* cap2 contains cap1 completely */
    cap1.setMin(10);
    cap1.setMax(245);
    QVERIFY(cap1.overlaps(&cap2) == true);
    QVERIFY(cap2.overlaps(&cap1) == true);

    /* cap2's max overlaps cap1 */
    cap2.setMin(0);
    cap2.setMax(10);
    QVERIFY(cap1.overlaps(&cap2) == true);
    QVERIFY(cap2.overlaps(&cap1) == true);

    /* cap2's max overlaps cap1 */
    cap2.setMin(0);
    cap2.setMax(15);
    QVERIFY(cap1.overlaps(&cap2) == true);
    QVERIFY(cap2.overlaps(&cap1) == true);

    /* cap2's min overlaps cap1 */
    cap2.setMin(245);
    cap2.setMax(UCHAR_MAX);
    QVERIFY(cap1.overlaps(&cap2) == true);
    QVERIFY(cap2.overlaps(&cap1) == true);

    /* cap2's min overlaps cap1 */
    cap2.setMin(240);
    cap2.setMax(UCHAR_MAX);
    QVERIFY(cap1.overlaps(&cap2) == true);
    QVERIFY(cap2.overlaps(&cap1) == true);

    /* cap1 contains cap2 completely */
    cap2.setMin(20);
    cap2.setMax(235);
    QVERIFY(cap1.overlaps(&cap2) == true);
    QVERIFY(cap2.overlaps(&cap1) == true);

    cap2.setMin(0);
    cap2.setMax(9);
    QVERIFY(cap1.overlaps(&cap2) == false);
    QVERIFY(cap2.overlaps(&cap1) == false);
}

void QLCCapability_Test::copy()
{
    QLCCapability cap1;
    QVERIFY(cap1.min() == 0);
    QVERIFY(cap1.max() == UCHAR_MAX);
    QVERIFY(cap1.name().isEmpty());

    cap1.setMin(5);
    cap1.setMax(15);
    cap1.setName("Foobar");

    QLCCapability *cap2 = cap1.createCopy();
    QVERIFY(cap2->min() == 5);
    QVERIFY(cap2->max() == 15);
    QVERIFY(cap2->name() == "Foobar");
}

void QLCCapability_Test::load()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "13");
    xmlWriter.writeAttribute("Max", "19");
    xmlWriter.writeCharacters("Test1");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCCapability cap;
    QVERIFY(cap.loadXML(xmlReader) == true);
    QVERIFY(cap.name() == "Test1");
    QVERIFY(cap.min() == 13);
    QVERIFY(cap.max() == 19);
}

void QLCCapability_Test::loadWrongRoot()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("apability");
    xmlWriter.writeAttribute("Min", "13");
    xmlWriter.writeAttribute("Max", "19");
    xmlWriter.writeCharacters("Test1");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCCapability cap;
    QVERIFY(cap.loadXML(xmlReader) == false);
    QVERIFY(cap.name().isEmpty());
    QVERIFY(cap.min() == 0);
    QVERIFY(cap.max() == UCHAR_MAX);
}

void QLCCapability_Test::loadNoMin()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Max", "19");
    xmlWriter.writeCharacters("Test1");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCCapability cap;
    QVERIFY(cap.loadXML(xmlReader) == false);
    QVERIFY(cap.name().isEmpty());
    QVERIFY(cap.min() == 0);
    QVERIFY(cap.max() == UCHAR_MAX);
}

void QLCCapability_Test::loadNoMax()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "13");
    xmlWriter.writeCharacters("Test1");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCCapability cap;
    QVERIFY(cap.loadXML(xmlReader) == false);
    QVERIFY(cap.name().isEmpty());
    QVERIFY(cap.min() == 0);
    QVERIFY(cap.max() == UCHAR_MAX);
}

void QLCCapability_Test::loadMinGreaterThanMax()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Capability");
    xmlWriter.writeAttribute("Min", "20");
    xmlWriter.writeAttribute("Max", "19");
    xmlWriter.writeCharacters("Test1");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCCapability cap;
    QVERIFY(cap.loadXML(xmlReader) == false);
    QVERIFY(cap.name().isEmpty());
    QVERIFY(cap.min() == 0);
    QVERIFY(cap.max() == UCHAR_MAX);
}

void QLCCapability_Test::save()
{
    QLCCapability cap;
    cap.setName("Testing");
    cap.setMin(5);
    cap.setMax(87);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(cap.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    xmlReader.readNextStartElement();

    QVERIFY(xmlReader.name().toString() == "Capability");
    QVERIFY(xmlReader.attributes().value("Min").toString() == "5");
    QVERIFY(xmlReader.attributes().value("Max").toString() == "87");
    QVERIFY(xmlReader.readElementText() == "Testing");
}

void QLCCapability_Test::savePreset()
{
    QLCCapability cap;
    cap.setName("PresetTest");
    cap.setMin(0);
    cap.setMax(127);
    cap.setPreset(QLCCapability::StrobeFreqRange);
    cap.setResource(0, 1);
    cap.setResource(1, 30);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(cap.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    xmlReader.readNextStartElement();

    QVERIFY(xmlReader.name().toString() == "Capability");
    QVERIFY(xmlReader.attributes().value("Min").toString() == "0");
    QVERIFY(xmlReader.attributes().value("Max").toString() == "127");
    QVERIFY(xmlReader.attributes().value("Preset").toString() == "StrobeFreqRange");
    QVERIFY(xmlReader.attributes().value("Res1").toString() == "1");
    QVERIFY(xmlReader.attributes().value("Res2").toString() == "30");
    QVERIFY(xmlReader.readElementText() == "PresetTest");
}

void QLCCapability_Test::saveAlias()
{
    QLCCapability cap;
    cap.setName("PresetTest");
    cap.setMin(10);
    cap.setMax(20);

    AliasInfo alias;
    alias.sourceChannel = "Channel 1";
    alias.targetChannel = "Channel 3";
    alias.targetMode = "12 Channel";
    cap.addAlias(alias);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(cap.saveXML(&xmlWriter) == true);

    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCCapability capRead;
    QVERIFY(capRead.loadXML(xmlReader) == true);

    QVERIFY(capRead.aliasList().count() == 1);
    QVERIFY(cap.aliasList().first().sourceChannel == "Channel 1");
    QVERIFY(cap.aliasList().first().targetChannel == "Channel 3");
    QVERIFY(cap.aliasList().first().targetMode == "12 Channel");
}

/* Build a <Capability> element with $attrs, $text and optional $subtags
   (already serialised element names with attributes) and load it into $cap */
static bool loadCapability(QLCCapability &cap, const QMap<QString, QString> &attrs,
                           const QString &text, const QStringList &subtags = QStringList())
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Capability");
    QMapIterator<QString, QString> it(attrs);
    while (it.hasNext())
    {
        it.next();
        xmlWriter.writeAttribute(it.key(), it.value());
    }
    xmlWriter.writeCharacters(text);
    foreach (const QString &tag, subtags)
    {
        xmlWriter.writeStartElement(tag);
        xmlWriter.writeAttribute("Mode", "Mode " + tag);
        xmlWriter.writeAttribute("Channel", "Source " + tag);
        xmlWriter.writeAttribute("With", "Target " + tag);
        xmlWriter.writeEndElement();
    }
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    return cap.loadXML(xmlReader);
}

/* Save $cap and return the attributes and text of the written element */
static QMap<QString, QString> saveCapability(const QLCCapability &cap, QString *text, bool *ok)
{
    QMap<QString, QString> attrs;

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    *ok = cap.saveXML(&xmlWriter);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    *ok = *ok && (xmlReader.name().toString() == "Capability");

    foreach (const QXmlStreamAttribute &attr, xmlReader.attributes())
        attrs[attr.name().toString()] = attr.value().toString();

    xmlReader.readNext();
    *text = xmlReader.text().toString();

    return attrs;
}

void QLCCapability_Test::presetStrings()
{
    for (int i = QLCCapability::Custom; i < QLCCapability::LastPreset; i++)
    {
        QLCCapability::Preset preset = QLCCapability::Preset(i);
        QString str = QLCCapability::presetToString(preset);
        QVERIFY2(str.isEmpty() == false, qPrintable(QString::number(i)));
        QCOMPARE(QLCCapability::stringToPreset(str), preset);

        QLCCapability cap;
        QCOMPARE(cap.preset(), QLCCapability::Custom);
        cap.setPreset(preset);
        QCOMPARE(cap.preset(), preset);
        QCOMPARE(cap.presetInt(), i);
        cap.setPreset(preset); // same value, no-op
        QCOMPARE(cap.preset(), preset);
    }

    QCOMPARE(QLCCapability::presetToString(QLCCapability::ShutterOpen), QString("ShutterOpen"));
    QCOMPARE(QLCCapability::stringToPreset("GoboShakeMacro"), QLCCapability::GoboShakeMacro);
    QCOMPARE(int(QLCCapability::stringToPreset("NotAPreset")), -1);
}

void QLCCapability_Test::presetTypes()
{
    for (int i = QLCCapability::Custom; i < QLCCapability::LastPreset; i++)
    {
        QLCCapability::Preset preset = QLCCapability::Preset(i);
        QByteArray ctx = QLCCapability::presetToString(preset).toLatin1();
        QLCCapability cap;
        cap.setPreset(preset);

        QLCCapability::PresetType expected = QLCCapability::None;
        switch (preset)
        {
            case QLCCapability::StrobeFrequency:
            case QLCCapability::PulseFrequency:
            case QLCCapability::RampUpFrequency:
            case QLCCapability::RampDownFrequency:
            case QLCCapability::PrismEffectOn:
                expected = QLCCapability::SingleValue;
            break;
            case QLCCapability::StrobeFreqRange:
            case QLCCapability::PulseFreqRange:
            case QLCCapability::RampUpFreqRange:
            case QLCCapability::RampDownFreqRange:
                expected = QLCCapability::DoubleValue;
            break;
            case QLCCapability::ColorMacro:
                expected = QLCCapability::SingleColor;
            break;
            case QLCCapability::ColorDoubleMacro:
                expected = QLCCapability::DoubleColor;
            break;
            case QLCCapability::GoboMacro:
            case QLCCapability::GoboShakeMacro:
            case QLCCapability::GenericPicture:
                expected = QLCCapability::Picture;
            break;
            default:
            break;
        }
        QVERIFY2(cap.presetType() == expected, ctx.constData());
    }
}

void QLCCapability_Test::presetUnits()
{
    QLCCapability cap;
    QVERIFY(cap.presetUnits().isEmpty());

    const QLCCapability::Preset hertz[] = {
        QLCCapability::StrobeFrequency, QLCCapability::PulseFrequency,
        QLCCapability::RampUpFrequency, QLCCapability::RampDownFrequency,
        QLCCapability::StrobeFreqRange, QLCCapability::PulseFreqRange,
        QLCCapability::RampUpFreqRange, QLCCapability::RampDownFreqRange
    };
    for (QLCCapability::Preset preset : hertz)
    {
        cap.setPreset(preset);
        QCOMPARE(cap.presetUnits(), QString("Hz"));
    }

    cap.setPreset(QLCCapability::PrismEffectOn);
    QCOMPARE(cap.presetUnits(), QString("Faces"));

    cap.setPreset(QLCCapability::ColorMacro);
    QVERIFY(cap.presetUnits().isEmpty());

    cap.setPreset(QLCCapability::LampOn);
    QVERIFY(cap.presetUnits().isEmpty());
}

void QLCCapability_Test::warning()
{
    QLCCapability cap;
    QCOMPARE(cap.warning(), QLCCapability::NoWarning);

    QSignalSpy spy(&cap, SIGNAL(warningChanged()));

    cap.setWarning(QLCCapability::NoWarning);
    QCOMPARE(spy.count(), 0);

    cap.setWarning(QLCCapability::EmptyName);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(cap.warning(), QLCCapability::EmptyName);

    cap.setWarning(QLCCapability::Overlapping);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(cap.warning(), QLCCapability::Overlapping);

    /* the other setters also emit only on change */
    QSignalSpy minSpy(&cap, SIGNAL(minChanged()));
    QSignalSpy maxSpy(&cap, SIGNAL(maxChanged()));
    QSignalSpy nameSpy(&cap, SIGNAL(nameChanged()));
    cap.setMin(0);
    cap.setMax(UCHAR_MAX);
    cap.setName(QString());
    QCOMPARE(minSpy.count(), 0);
    QCOMPARE(maxSpy.count(), 0);
    QCOMPARE(nameSpy.count(), 0);
    cap.setMin(1);
    cap.setMax(2);
    cap.setName("x");
    QCOMPARE(minSpy.count(), 1);
    QCOMPARE(maxSpy.count(), 1);
    QCOMPARE(nameSpy.count(), 1);
}

void QLCCapability_Test::resources()
{
    QLCCapability cap;
    QVERIFY(cap.resources().isEmpty());
    QVERIFY(cap.resource(0).isValid() == false);
    QVERIFY(cap.resource(-1).isValid() == false);

    /* negative indices are ignored */
    cap.setResource(-1, 42);
    QVERIFY(cap.resources().isEmpty());

    /* indices beyond the end append */
    cap.setResource(0, 1);
    cap.setResource(5, "two");
    QCOMPARE(cap.resources().count(), 2);
    QCOMPARE(cap.resource(0).toInt(), 1);
    QCOMPARE(cap.resource(1).toString(), QString("two"));
    QVERIFY(cap.resource(2).isValid() == false);

    /* existing indices are replaced */
    cap.setResource(1, QColor(Qt::red));
    QCOMPARE(cap.resources().count(), 2);
    QCOMPARE(cap.resource(1).value<QColor>(), QColor(Qt::red));

    QVariantList list = cap.resources();
    QCOMPARE(list.count(), 2);
    QCOMPARE(list.at(0).toInt(), 1);
}

void QLCCapability_Test::copyPresetAndAliases()
{
    QLCCapability cap1(10, 20, "Strobe");
    cap1.setPreset(QLCCapability::StrobeFreqRange);
    cap1.setWarning(QLCCapability::Overlapping);
    cap1.setResource(0, 1.5f);
    cap1.setResource(1, 25.0f);

    AliasInfo alias;
    alias.targetMode = "Mode";
    alias.sourceChannel = "A";
    alias.targetChannel = "B";
    cap1.addAlias(alias);

    QLCCapability *cap2 = cap1.createCopy();
    QVERIFY(cap2 != NULL);
    QVERIFY(cap2 != &cap1);
    QCOMPARE(cap2->min(), uchar(10));
    QCOMPARE(cap2->max(), uchar(20));
    QCOMPARE(cap2->name(), QString("Strobe"));
    QCOMPARE(cap2->preset(), QLCCapability::StrobeFreqRange);
    QCOMPARE(cap2->warning(), QLCCapability::Overlapping);
    QCOMPARE(cap2->resources().count(), 2);
    QCOMPARE(cap2->resource(0).toFloat(), 1.5f);
    QCOMPARE(cap2->resource(1).toFloat(), 25.0f);
    QCOMPARE(cap2->aliasList().count(), 1);
    QCOMPARE(cap2->aliasList().first().targetMode, QString("Mode"));
    QCOMPARE(cap2->aliasList().first().sourceChannel, QString("A"));
    QCOMPARE(cap2->aliasList().first().targetChannel, QString("B"));

    /* the copy is independent */
    cap2->removeAlias(alias);
    QCOMPARE(cap2->aliasList().count(), 0);
    QCOMPARE(cap1.aliasList().count(), 1);

    /* removing an alias that does not match anything is a no-op */
    alias.targetChannel = "C";
    cap1.removeAlias(alias);
    QCOMPARE(cap1.aliasList().count(), 1);

    delete cap2;
}

void QLCCapability_Test::lessThan()
{
    QLCCapability low(0, 9, "low");
    QLCCapability high(10, 19, "high");
    QLCCapability sameMin(0, 100, "same min");

    QVERIFY(low < high);
    QVERIFY((high < low) == false);
    QVERIFY((low < sameMin) == false);
    QVERIFY((sameMin < low) == false);
}

void QLCCapability_Test::savePicture()
{
    bool ok = false;
    QString text;
    QMap<QString, QString> attrs;

    /* a picture inside the system gobo folder is saved relative to it */
    QString goboDir = QDir::cleanPath(QLCFile::systemDirectory(GOBODIR).path());
    QVERIFY(goboDir.isEmpty() == false);
    {
        QLCCapability cap(0, 10, "Gobo 1");
        cap.setPreset(QLCCapability::GoboMacro);
        cap.setResource(0, goboDir + "/gobo00001.svg");
        attrs = saveCapability(cap, &text, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Preset"), QString("GoboMacro"));
        QCOMPARE(attrs.value("Res1"), QString("gobo00001.svg"));
        QCOMPARE(text, QString("Gobo 1"));
    }

    /* a picture elsewhere keeps its full path */
    {
        QLCCapability cap(11, 20, "Custom");
        cap.setPreset(QLCCapability::GenericPicture);
        QString path = QDir::tempPath() + "/somewhere-else/picture.png";
        cap.setResource(0, path);
        attrs = saveCapability(cap, &text, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Preset"), QString("GenericPicture"));
        QCOMPARE(attrs.value("Res1"), path);
    }

    /* a shake macro is a picture too */
    {
        QLCCapability cap(21, 30, "Shake");
        cap.setPreset(QLCCapability::GoboShakeMacro);
        cap.setResource(0, "shake.svg");
        attrs = saveCapability(cap, &text, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Res1"), QString("shake.svg"));
    }
}

void QLCCapability_Test::saveColours()
{
    bool ok = false;
    QString text;
    QMap<QString, QString> attrs;

    {
        QLCCapability cap(0, 10, "Red");
        cap.setPreset(QLCCapability::ColorMacro);
        cap.setResource(0, QColor(255, 0, 0));
        attrs = saveCapability(cap, &text, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Preset"), QString("ColorMacro"));
        QCOMPARE(attrs.value("Res1"), QString("#ff0000"));
        QVERIFY(attrs.contains("Res2") == false);
    }

    {
        QLCCapability cap(11, 20, "Red/Blue");
        cap.setPreset(QLCCapability::ColorDoubleMacro);
        cap.setResource(0, QColor(255, 0, 0));
        cap.setResource(1, QColor(0, 0, 255));
        attrs = saveCapability(cap, &text, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Preset"), QString("ColorDoubleMacro"));
        QCOMPARE(attrs.value("Res1"), QString("#ff0000"));
        QCOMPARE(attrs.value("Res2"), QString("#0000ff"));
        QCOMPARE(text, QString("Red/Blue"));
    }

    /* invalid colours are not written */
    {
        QLCCapability cap(21, 30, "Broken");
        cap.setPreset(QLCCapability::ColorDoubleMacro);
        cap.setResource(0, QColor());
        cap.setResource(1, QColor(0, 255, 0));
        attrs = saveCapability(cap, &text, &ok);
        QVERIFY(ok);
        QVERIFY(attrs.contains("Res1") == false);
        QCOMPARE(attrs.value("Res2"), QString("#00ff00"));
    }

    /* presets without resources ignore whatever resources are set */
    {
        QLCCapability cap(31, 40, "Lamp");
        cap.setPreset(QLCCapability::LampOn);
        cap.setResource(0, 12);
        attrs = saveCapability(cap, &text, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Preset"), QString("LampOn"));
        QVERIFY(attrs.contains("Res1") == false);
    }
}

void QLCCapability_Test::saveSingleValue()
{
    bool ok = false;
    QString text;
    QMap<QString, QString> attrs;

    QLCCapability cap(0, 255, "Strobe 12.5Hz");
    cap.setPreset(QLCCapability::StrobeFrequency);
    cap.setResource(0, 12.5f);
    attrs = saveCapability(cap, &text, &ok);
    QVERIFY(ok);
    QCOMPARE(attrs.value("Preset"), QString("StrobeFrequency"));
    QCOMPARE(attrs.value("Res1"), QString("12.5"));
    QVERIFY(attrs.contains("Res2") == false);
    QCOMPARE(text, QString("Strobe 12.5Hz"));

    /* a custom capability writes no preset attribute */
    QLCCapability plain(0, 1, "Plain");
    attrs = saveCapability(plain, &text, &ok);
    QVERIFY(ok);
    QVERIFY(attrs.contains("Preset") == false);
    QVERIFY(attrs.contains("Res1") == false);
}

void QLCCapability_Test::loadPreset()
{
    QMap<QString, QString> attrs;
    attrs["Min"] = "0";
    attrs["Max"] = "127";

    /* single value */
    {
        QLCCapability cap;
        attrs["Preset"] = "StrobeFrequency";
        attrs["Res1"] = "12.5";
        QVERIFY(loadCapability(cap, attrs, "Strobe") == true);
        QCOMPARE(cap.preset(), QLCCapability::StrobeFrequency);
        QCOMPARE(cap.resources().count(), 1);
        QCOMPARE(cap.resource(0).toFloat(), 12.5f);
        QCOMPARE(cap.name(), QString("Strobe"));
        QCOMPARE(cap.min(), uchar(0));
        QCOMPARE(cap.max(), uchar(127));
    }

    /* double value */
    {
        QLCCapability cap;
        attrs["Preset"] = "PulseFreqRange";
        attrs["Res1"] = "1";
        attrs["Res2"] = "30";
        QVERIFY(loadCapability(cap, attrs, "Pulse") == true);
        QCOMPARE(cap.preset(), QLCCapability::PulseFreqRange);
        QCOMPARE(cap.resources().count(), 2);
        QCOMPARE(cap.resource(0).toFloat(), 1.0f);
        QCOMPARE(cap.resource(1).toFloat(), 30.0f);
    }

    /* presets without resources */
    {
        QLCCapability cap;
        attrs.remove("Res1");
        attrs.remove("Res2");
        attrs["Preset"] = "ResetAll";
        QVERIFY(loadCapability(cap, attrs, "Reset") == true);
        QCOMPARE(cap.preset(), QLCCapability::ResetAll);
        QVERIFY(cap.resources().isEmpty());
    }

    /* an unknown preset keyword yields an out-of-range preset value */
    {
        QLCCapability cap;
        attrs["Preset"] = "NoSuchPreset";
        QVERIFY(loadCapability(cap, attrs, "Odd") == true);
        QCOMPARE(int(cap.preset()), -1);
        QCOMPARE(cap.presetType(), QLCCapability::None);
    }
}

void QLCCapability_Test::loadPicture()
{
    QMap<QString, QString> attrs;
    attrs["Min"] = "0";
    attrs["Max"] = "10";

    /* relative picture paths resolve against the system gobo folder */
    {
        QLCCapability cap;
        attrs["Preset"] = "GoboMacro";
        attrs["Res1"] = "gobo00001.svg";
        QVERIFY(loadCapability(cap, attrs, "Gobo") == true);
        QCOMPARE(cap.preset(), QLCCapability::GoboMacro);
        QString path = cap.resource(0).toString();
        QVERIFY(path.endsWith("gobo00001.svg"));
        QVERIFY(path.startsWith(QLCFile::systemDirectory(GOBODIR).path()));
    }

    /* absolute picture paths are kept as they are */
    {
        QLCCapability cap;
        QString absolute = QDir::tempPath() + "/pictures/picture.png";
        attrs["Preset"] = "GenericPicture";
        attrs["Res1"] = absolute;
        QVERIFY(loadCapability(cap, attrs, "Picture") == true);
        QCOMPARE(cap.preset(), QLCCapability::GenericPicture);
        QCOMPARE(cap.resource(0).toString(), absolute);
    }
}

void QLCCapability_Test::loadColours()
{
    QMap<QString, QString> attrs;
    attrs["Min"] = "0";
    attrs["Max"] = "10";

    {
        QLCCapability cap;
        attrs["Preset"] = "ColorMacro";
        attrs["Res1"] = "#ff0000";
        QVERIFY(loadCapability(cap, attrs, "Red") == true);
        QCOMPARE(cap.presetType(), QLCCapability::SingleColor);
        QCOMPARE(cap.resources().count(), 1);
        QCOMPARE(cap.resource(0).value<QColor>(), QColor(255, 0, 0));
    }

    {
        QLCCapability cap;
        attrs["Preset"] = "ColorDoubleMacro";
        attrs["Res1"] = "#ff0000";
        attrs["Res2"] = "#0000ff";
        QVERIFY(loadCapability(cap, attrs, "Red/Blue") == true);
        QCOMPARE(cap.presetType(), QLCCapability::DoubleColor);
        QCOMPARE(cap.resources().count(), 2);
        QCOMPARE(cap.resource(0).value<QColor>(), QColor(255, 0, 0));
        QCOMPARE(cap.resource(1).value<QColor>(), QColor(0, 0, 255));
    }

    /* an invalid first colour drops both */
    {
        QLCCapability cap;
        attrs["Res1"] = "notacolour";
        attrs["Res2"] = "#0000ff";
        QVERIFY(loadCapability(cap, attrs, "Broken") == true);
        QVERIFY(cap.resources().isEmpty());
    }

    /* an invalid second colour keeps the first */
    {
        QLCCapability cap;
        attrs["Res1"] = "#00ff00";
        attrs["Res2"] = "notacolour";
        QVERIFY(loadCapability(cap, attrs, "Half") == true);
        QCOMPARE(cap.resources().count(), 1);
        QCOMPARE(cap.resource(0).value<QColor>(), QColor(0, 255, 0));
    }
}

void QLCCapability_Test::loadLegacyResource()
{
    QMap<QString, QString> attrs;
    attrs["Min"] = "0";
    attrs["Max"] = "10";

    /* a legacy relative "Res" becomes a gobo macro in the system folder */
    {
        QLCCapability cap;
        attrs["Res"] = "gobo00002.svg";
        QVERIFY(loadCapability(cap, attrs, "Legacy gobo") == true);
        QCOMPARE(cap.preset(), QLCCapability::GoboMacro);
        QString path = cap.resource(0).toString();
        QVERIFY(path.endsWith("gobo00002.svg"));
    }

    /* a legacy absolute "Res" becomes a generic picture */
    {
        QLCCapability cap;
        QString absolute = QDir::tempPath() + "/legacy/picture.png";
        attrs["Res"] = absolute;
        QVERIFY(loadCapability(cap, attrs, "Legacy picture") == true);
        QCOMPARE(cap.preset(), QLCCapability::GenericPicture);
        QCOMPARE(cap.resource(0).toString(), absolute);
    }
}

void QLCCapability_Test::loadLegacyColours()
{
    QMap<QString, QString> attrs;
    attrs["Min"] = "0";
    attrs["Max"] = "10";

    /* a single legacy colour becomes a colour macro */
    {
        QLCCapability cap;
        attrs["Color"] = "#ff0000";
        QVERIFY(loadCapability(cap, attrs, "Legacy red") == true);
        QCOMPARE(cap.preset(), QLCCapability::ColorMacro);
        QCOMPARE(cap.resources().count(), 1);
        QCOMPARE(cap.resource(0).value<QColor>(), QColor(255, 0, 0));
    }

    /* two legacy colours become a double colour macro */
    {
        QLCCapability cap;
        attrs["Color"] = "#ff0000";
        attrs["Color2"] = "#0000ff";
        QVERIFY(loadCapability(cap, attrs, "Legacy split") == true);
        QCOMPARE(cap.preset(), QLCCapability::ColorDoubleMacro);
        QCOMPARE(cap.resources().count(), 2);
        QCOMPARE(cap.resource(1).value<QColor>(), QColor(0, 0, 255));
    }

    /* an invalid legacy colour leaves the capability custom */
    {
        QLCCapability cap;
        attrs["Color"] = "notacolour";
        attrs.remove("Color2");
        QVERIFY(loadCapability(cap, attrs, "Legacy broken") == true);
        QCOMPARE(cap.preset(), QLCCapability::Custom);
        QVERIFY(cap.resources().isEmpty());
    }
}

void QLCCapability_Test::loadEmptyName()
{
    QMap<QString, QString> attrs;
    attrs["Min"] = "5";
    attrs["Max"] = "10";

    /* an empty description is tolerated (with a warning) */
    QLCCapability cap;
    QVERIFY(loadCapability(cap, attrs, "   ") == true);
    QVERIFY(cap.name().isEmpty());
    QCOMPARE(cap.min(), uchar(5));
    QCOMPARE(cap.max(), uchar(10));

    /* ...and the surrounding whitespace of a real name is simplified */
    QLCCapability cap2;
    QVERIFY(loadCapability(cap2, attrs, "  Spaced   out  ") == true);
    QCOMPARE(cap2.name(), QString("Spaced out"));
}

void QLCCapability_Test::loadUnknownTag()
{
    QMap<QString, QString> attrs;
    attrs["Min"] = "5";
    attrs["Max"] = "10";

    /* unknown subtags are skipped, aliases still parsed */
    QLCCapability cap;
    QVERIFY(loadCapability(cap, attrs, "Name", QStringList() << "Bogus" << "Alias") == true);
    QCOMPARE(cap.name(), QString("Name"));
    QCOMPARE(cap.aliasList().count(), 1);
    QCOMPARE(cap.aliasList().first().targetMode, QString("Mode Alias"));
    QCOMPARE(cap.aliasList().first().sourceChannel, QString("Source Alias"));
    QCOMPARE(cap.aliasList().first().targetChannel, QString("Target Alias"));
}

void QLCCapability_Test::loadClampedRange()
{
    QMap<QString, QString> attrs;

    /* out-of-range limits are clamped to a DMX byte */
    QLCCapability cap;
    attrs["Min"] = "-20";
    attrs["Max"] = "300";
    QVERIFY(loadCapability(cap, attrs, "Clamped") == true);
    QCOMPARE(cap.min(), uchar(0));
    QCOMPARE(cap.max(), uchar(UCHAR_MAX));

    /* equal limits are a valid single value */
    QLCCapability single;
    attrs["Min"] = "42";
    attrs["Max"] = "42";
    QVERIFY(loadCapability(single, attrs, "Single") == true);
    QCOMPARE(single.min(), uchar(42));
    QCOMPARE(single.max(), uchar(42));
    QCOMPARE(single.middle(), uchar(42));
}

/* QTEST_GUILESS_MAIN: the picture presets resolve paths through
   QLCFile::systemDirectory(), which needs QCoreApplication::applicationDirPath(). */
QTEST_GUILESS_MAIN(QLCCapability_Test)
