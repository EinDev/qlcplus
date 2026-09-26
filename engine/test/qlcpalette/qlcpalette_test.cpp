/*
  Q Light Controller Plus - Unit tests
  qlcpalette_test.cpp

  Copyright (C) Massimo Callegari

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

#include <QtMath>

#include "qlcpalette_test.h"
#include "monitorproperties.h"
#include "qlcfixturemode.h"
#include "qlcfixturehead.h"
#include "qlcfixturedef.h"
#include "qlccapability.h"
#include "qlcphysical.h"
#include "fixturegroup.h"
#include "qlcpalette.h"
#include "qlcchannel.h"
#include "scenevalue.h"
#include "fixture.h"
#include "doc.h"

void QLCPalette_Test::initialization()
{
    QLCPalette p(QLCPalette::Undefined);

    QVERIFY(p.type() == QLCPalette::Undefined);
    QVERIFY(p.id() == QLCPalette::invalidId());
    QVERIFY(p.fanningType() == QLCPalette::Flat);
    QVERIFY(p.fanningLayout() == QLCPalette::XAscending);
    QVERIFY(p.fanningAmount() == 100);
    QVERIFY(p.fanningValue() == QVariant());

    p.setID(42);
    QVERIFY(p.id() == 42);

    QVERIFY(p.name() == QString());
    p.setName("My Palette");
    QVERIFY(p.name() == QString("My Palette"));
}

void QLCPalette_Test::type()
{
    QVERIFY(QLCPalette::typeToString(QLCPalette::Undefined) == QString());
    QVERIFY(QLCPalette::typeToString(QLCPalette::Dimmer) == QString("Dimmer"));
    QVERIFY(QLCPalette::typeToString(QLCPalette::Color) == QString("Color"));
    QVERIFY(QLCPalette::typeToString(QLCPalette::Pan) == QString("Pan"));
    QVERIFY(QLCPalette::typeToString(QLCPalette::Tilt) == QString("Tilt"));
    QVERIFY(QLCPalette::typeToString(QLCPalette::PanTilt) == QString("PanTilt"));
    QVERIFY(QLCPalette::typeToString(QLCPalette::Shutter) == QString("Shutter"));
    QVERIFY(QLCPalette::typeToString(QLCPalette::Gobo) == QString("Gobo"));
    QVERIFY(QLCPalette::typeToString(QLCPalette::Zoom) == QString("Zoom"));
    QVERIFY(QLCPalette::typeToString(QLCPalette::Position3D) == QString("Position3D"));

    QVERIFY(QLCPalette::stringToType("Foo") == QLCPalette::Undefined);
    QVERIFY(QLCPalette::stringToType("Dimmer") == QLCPalette::Dimmer);
    QVERIFY(QLCPalette::stringToType("Color") == QLCPalette::Color);
    QVERIFY(QLCPalette::stringToType("Pan") == QLCPalette::Pan);
    QVERIFY(QLCPalette::stringToType("Tilt") == QLCPalette::Tilt);
    QVERIFY(QLCPalette::stringToType("PanTilt") == QLCPalette::PanTilt);
    QVERIFY(QLCPalette::stringToType("Shutter") == QLCPalette::Shutter);
    QVERIFY(QLCPalette::stringToType("Gobo") == QLCPalette::Gobo);
    QVERIFY(QLCPalette::stringToType("Zoom") == QLCPalette::Zoom);
    QVERIFY(QLCPalette::stringToType("Position3D") == QLCPalette::Position3D);
}

void QLCPalette_Test::icon()
{
    QLCPalette p1(QLCPalette::Dimmer);
    QCOMPARE(p1.iconResource(), QString(":/intensity.png"));
    QLCPalette p2(QLCPalette::Color);
    QCOMPARE(p2.iconResource(), QString(":/color.png"));
    QLCPalette p3(QLCPalette::Pan);
    QCOMPARE(p3.iconResource(), QString(":/pan.png"));
    QLCPalette p4(QLCPalette::Tilt);
    QCOMPARE(p4.iconResource(), QString(":/tilt.png"));
    QLCPalette p5(QLCPalette::PanTilt);
    QCOMPARE(p5.iconResource(true), QString("qrc:/position.svg"));
    QLCPalette p6(QLCPalette::Shutter);
    QCOMPARE(p6.iconResource(), QString(":/shutter.png"));
    QLCPalette p7(QLCPalette::Gobo);
    QCOMPARE(p7.iconResource(), QString(":/gobo.png"));
    QLCPalette p8(QLCPalette::Zoom);
    QCOMPARE(p8.iconResource(), QString(":/beam.png"));
    QLCPalette p9(QLCPalette::Position3D);
    QCOMPARE(p9.iconResource(true), QString("qrc:/3dpoint.svg"));
}

void QLCPalette_Test::value()
{
    /* test one single integer value */
    QLCPalette p1(QLCPalette::Dimmer);

    QVERIFY(p1.values().count() == 0);

    p1.setValue(128);
    QVERIFY(p1.values().count() == 1);
    QVERIFY(p1.value().toInt() == 128);

    /* test composite color value */
    QLCPalette p2(QLCPalette::Color);
    p2.setValue(QLCPalette::colorToString(QColor(0x11, 0x22, 0x33), QColor(0x44, 0x55, 0x66)));
    QVERIFY(p1.values().count() == 1);

    QColor rgb, wauv;
    QVERIFY(p2.value().toString() == "#112233445566");
    QVERIFY(QLCPalette::stringToColor(p2.value().toString(), rgb, wauv) == true);
    QVERIFY(rgb == QColor(0x11, 0x22, 0x33));
    QVERIFY(wauv == QColor(0x44, 0x55, 0x66));

    /* test 2 integer values */
    QLCPalette p3(QLCPalette::PanTilt);
    p3.setValue(180, 90);

    QVERIFY(p3.values().count() == 2);
    QVERIFY(p3.values().at(0).toInt() == 180);
    QVERIFY(p3.values().at(1).toInt() == 90);
}

void QLCPalette_Test::fanning()
{
    QVERIFY(QLCPalette::fanningTypeToString(QLCPalette::Flat) == "Flat");
    QVERIFY(QLCPalette::fanningTypeToString(QLCPalette::Linear) == "Linear");
    QVERIFY(QLCPalette::fanningTypeToString(QLCPalette::Sine) == "Sine");
    QVERIFY(QLCPalette::fanningTypeToString(QLCPalette::Square) == "Square");
    QVERIFY(QLCPalette::fanningTypeToString(QLCPalette::Saw) == "Saw");

    QVERIFY(QLCPalette::stringToFanningType("Foo") == QLCPalette::Flat);
    QVERIFY(QLCPalette::stringToFanningType("Flat") == QLCPalette::Flat);
    QVERIFY(QLCPalette::stringToFanningType("Linear") == QLCPalette::Linear);
    QVERIFY(QLCPalette::stringToFanningType("Sine") == QLCPalette::Sine);
    QVERIFY(QLCPalette::stringToFanningType("Square") == QLCPalette::Square);
    QVERIFY(QLCPalette::stringToFanningType("Saw") == QLCPalette::Saw);

    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::XAscending) == "XAscending");
    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::XDescending) == "XDescending");
    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::XCentered) == "XCentered");
    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::YAscending) == "YAscending");
    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::YDescending) == "YDescending");
    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::YCentered) == "YCentered");
    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::ZAscending) == "ZAscending");
    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::ZDescending) == "ZDescending");
    QVERIFY(QLCPalette::fanningLayoutToString(QLCPalette::ZCentered) == "ZCentered");

    QVERIFY(QLCPalette::stringToFanningLayout("Foo") == QLCPalette::XAscending);
    QVERIFY(QLCPalette::stringToFanningLayout("XAscending") == QLCPalette::XAscending);
    QVERIFY(QLCPalette::stringToFanningLayout("XDescending") == QLCPalette::XDescending);
    QVERIFY(QLCPalette::stringToFanningLayout("XCentered") == QLCPalette::XCentered);
    QVERIFY(QLCPalette::stringToFanningLayout("YAscending") == QLCPalette::YAscending);
    QVERIFY(QLCPalette::stringToFanningLayout("YDescending") == QLCPalette::YDescending);
    QVERIFY(QLCPalette::stringToFanningLayout("YCentered") == QLCPalette::YCentered);
    QVERIFY(QLCPalette::stringToFanningLayout("ZAscending") == QLCPalette::ZAscending);
    QVERIFY(QLCPalette::stringToFanningLayout("ZDescending") == QLCPalette::ZDescending);
    QVERIFY(QLCPalette::stringToFanningLayout("ZCentered") == QLCPalette::ZCentered);

    QLCPalette p(QLCPalette::Dimmer);
    p.setFanningAmount(75);

    QVERIFY(p.fanningAmount() == 75);
}

void QLCPalette_Test::colorHelpers()
{
    QColor rgb(0xAA, 0xBB, 0xCC);
    QColor wauv(0x11, 0x22, 0x33);

    QVERIFY(QLCPalette::colorToString(rgb, wauv) == QString("#aabbcc112233"));

    QVERIFY(QLCPalette::stringToColor("#invalid", rgb, wauv) == false);
    QVERIFY(QLCPalette::stringToColor("#11deadbeef22", rgb, wauv) == true);

    QVERIFY(rgb.red() == 0x11);
    QVERIFY(rgb.green() == 0xDE);
    QVERIFY(rgb.blue() == 0xAD);

    QVERIFY(wauv.red() == 0xBE);
    QVERIFY(wauv.green() == 0xEF);
    QVERIFY(wauv.blue() == 0x22);
}

void QLCPalette_Test::load()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Palette");
    xmlWriter.writeAttribute("ID", "1");
    xmlWriter.writeAttribute("Type", "Color");
    xmlWriter.writeAttribute("Name", "Lavender");
    xmlWriter.writeAttribute("Value", "#AABBCCDDEEFF");
    xmlWriter.writeAttribute("Fan", "Linear");
    xmlWriter.writeAttribute("Layout", "LeftToRight");
    xmlWriter.writeAttribute("Amount", "42");
    xmlWriter.writeAttribute("FanValue", "#00ff00");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCPalette p(QLCPalette::Undefined);
    QVERIFY(p.loadXML(xmlReader) == true);

    QVERIFY(p.id() == 1);
    QVERIFY(p.type() == QLCPalette::Color);
    QVERIFY(p.name() == "Lavender");
    QVERIFY(p.value().toString() == "#AABBCCDDEEFF");
    QVERIFY(p.fanningType() == QLCPalette::Linear);
    QVERIFY(p.fanningLayout() == QLCPalette::XAscending);
    QVERIFY(p.fanningAmount() == 42);
    QVERIFY(p.fanningValue().toString() == "#00ff00");
}

void QLCPalette_Test::loadWrongRoot()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Politte");
    xmlWriter.writeAttribute("ID", "42");
    xmlWriter.writeAttribute("Type", "Dimmer");
    xmlWriter.writeAttribute("Name", "Just wrong");
    xmlWriter.writeAttribute("Value", "42");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QLCPalette p(QLCPalette::Undefined);
    QVERIFY(p.loadXML(xmlReader) == false);
}

void QLCPalette_Test::save()
{
    QLCPalette p(QLCPalette::PanTilt);
    p.setID(3);
    p.setName("Center up");
    p.setValue(90, 145);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(p.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Palette");

    QVERIFY(xmlReader.attributes().value("ID").toString() == "3");
    QVERIFY(xmlReader.attributes().value("Name").toString() == "Center up");
    QVERIFY(xmlReader.attributes().value("Type").toString() == "PanTilt");
    QVERIFY(xmlReader.attributes().value("Value").toString() == "90,145");
}

void QLCPalette_Test::saveLoadRoundTrip()
{
    // Tardis's PaletteDelete/PaletteCreate actions snapshot a palette via
    // saveXML() and later recreate an equivalent one via the static loader().
    // Verify that round trip actually reproduces an equivalent object, and
    // that the reconstructed copy is independent of the original.
    Doc doc(this);

    QLCPalette *original = new QLCPalette(QLCPalette::PanTilt);
    original->setID(7);
    original->setName("Round Trip");
    original->setValue(90, 145);
    original->setFanningType(QLCPalette::Linear);
    original->setFanningLayout(QLCPalette::YAscending);
    original->setFanningAmount(55);
    original->setFanningValue(30);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(original->saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Palette");

    QVERIFY(QLCPalette::loader(xmlReader, &doc) == true);

    QLCPalette *copy = doc.palette(7);
    QVERIFY(copy != NULL);
    QVERIFY(copy != original);

    QCOMPARE(copy->id(), original->id());
    QCOMPARE(copy->name(), original->name());
    QCOMPARE(copy->type(), original->type());
    QCOMPARE(copy->values(), original->values());
    QCOMPARE(copy->fanningType(), original->fanningType());
    QCOMPARE(copy->fanningLayout(), original->fanningLayout());
    QCOMPARE(copy->fanningAmount(), original->fanningAmount());
    QCOMPARE(copy->fanningValue().toInt(), original->fanningValue().toInt());

    // The reconstructed copy must be a fully independent object: destroying
    // the original (as Tardis does after snapshotting it) must not affect it.
    delete original;

    QCOMPARE(copy->id(), quint32(7));
    QCOMPARE(copy->name(), QString("Round Trip"));
    QCOMPARE(copy->values(), QVariantList() << QVariant(90) << QVariant(145));
}

/*****************************************************************************
 * Helpers: in-memory fixture definitions carrying exactly the channel presets
 * the palette value builders branch on (independent of the bundled .qxf files)
 *****************************************************************************/

static QLCChannel *addPresetChannel(QLCFixtureDef *def, QLCChannel::Preset preset,
                                    const QString &name = QString())
{
    QLCChannel *ch = new QLCChannel();
    if (name.isEmpty() == false)
        ch->setName(name);
    ch->setPreset(preset);
    ch->addPresetCapability();
    def->addChannel(ch);
    return ch;
}

/* Moving head: a master dimmer outside the head, 16bit pan/tilt, RGB plus
   W/A/UV, a shutter channel with preset capabilities, a gobo wheel and a
   16bit zoom. The indices are fixed and referenced by the tests below. */
enum MoverChannel
{
    MoverDimmer = 0, MoverPan, MoverPanFine, MoverTilt, MoverTiltFine,
    MoverRed, MoverGreen, MoverBlue, MoverWhite, MoverAmber, MoverUV,
    MoverShutter, MoverGobo, MoverZoom, MoverZoomFine, MoverChannelCount
};

static const qreal MoverPanMax = 540.0;
static const qreal MoverTiltMax = 270.0;
static const qreal MoverZoomMin = 10.0;
static const qreal MoverZoomMax = 60.0;

static QLCFixtureDef *makeMoverDef()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("Mover");
    def->setType(QLCFixtureDef::MovingHead);

    addPresetChannel(def, QLCChannel::IntensityDimmer);
    addPresetChannel(def, QLCChannel::PositionPan);
    addPresetChannel(def, QLCChannel::PositionPanFine);
    addPresetChannel(def, QLCChannel::PositionTilt);
    addPresetChannel(def, QLCChannel::PositionTiltFine);
    addPresetChannel(def, QLCChannel::IntensityRed);
    addPresetChannel(def, QLCChannel::IntensityGreen);
    addPresetChannel(def, QLCChannel::IntensityBlue);
    addPresetChannel(def, QLCChannel::IntensityWhite);
    addPresetChannel(def, QLCChannel::IntensityAmber);
    addPresetChannel(def, QLCChannel::IntensityUV);

    QLCChannel *shutter = new QLCChannel();
    shutter->setName("Shutter");
    shutter->setGroup(QLCChannel::Shutter);
    QLCCapability *closed = new QLCCapability(0, 9, "Closed");
    closed->setPreset(QLCCapability::ShutterClose);
    QLCCapability *open = new QLCCapability(10, 19, "Open");
    open->setPreset(QLCCapability::ShutterOpen);
    QLCCapability *strobe = new QLCCapability(20, 255, "Strobe");
    strobe->setPreset(QLCCapability::StrobeSlowToFast);
    shutter->addCapability(closed);
    shutter->addCapability(open);
    shutter->addCapability(strobe);
    def->addChannel(shutter);

    addPresetChannel(def, QLCChannel::GoboWheel);
    addPresetChannel(def, QLCChannel::BeamZoomSmallBig);
    addPresetChannel(def, QLCChannel::BeamZoomFine);
    Q_ASSERT(def->channels().size() == MoverChannelCount);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("Test mode");
    for (int i = 0; i < def->channels().size(); i++)
        mode->insertChannel(def->channels().at(i), i);

    /* one head holding everything but the master dimmer, so that
       QLCFixtureMode::cacheHeads() detects channel 0 as master intensity */
    QLCFixtureHead head;
    for (int i = MoverPan; i < MoverChannelCount; i++)
        head.addChannel(i);
    mode->insertHead(-1, head);

    QLCPhysical phy;
    phy.setFocusPanMax(int(MoverPanMax));
    phy.setFocusTiltMax(int(MoverTiltMax));
    phy.setLensDegreesMin(MoverZoomMin);
    phy.setLensDegreesMax(MoverZoomMax);
    mode->setPhysical(phy);

    def->addMode(mode);
    return def;
}

/* Two-head RGB bar: master dimmer outside the heads, each head with its own
   dimmer and RGB. No pan/tilt, shutter, gobo or zoom. */
enum BarChannel
{
    BarMaster = 0, BarDimmer1, BarRed1, BarGreen1, BarBlue1,
    BarDimmer2, BarRed2, BarGreen2, BarBlue2, BarChannelCount
};

static QLCFixtureDef *makeBarDef()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("Bar");
    def->setType(QLCFixtureDef::LEDBarPixels);

    addPresetChannel(def, QLCChannel::IntensityMasterDimmer, "Master");
    addPresetChannel(def, QLCChannel::IntensityDimmer, "Dimmer 1");
    addPresetChannel(def, QLCChannel::IntensityRed, "Red 1");
    addPresetChannel(def, QLCChannel::IntensityGreen, "Green 1");
    addPresetChannel(def, QLCChannel::IntensityBlue, "Blue 1");
    addPresetChannel(def, QLCChannel::IntensityDimmer, "Dimmer 2");
    addPresetChannel(def, QLCChannel::IntensityRed, "Red 2");
    addPresetChannel(def, QLCChannel::IntensityGreen, "Green 2");
    addPresetChannel(def, QLCChannel::IntensityBlue, "Blue 2");
    Q_ASSERT(def->channels().size() == BarChannelCount);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("9 Channel");
    for (int i = 0; i < def->channels().size(); i++)
        mode->insertChannel(def->channels().at(i), i);

    QLCFixtureHead head1;
    for (int i = BarDimmer1; i <= BarBlue1; i++)
        head1.addChannel(i);
    mode->insertHead(-1, head1);

    QLCFixtureHead head2;
    for (int i = BarDimmer2; i <= BarBlue2; i++)
        head2.addChannel(i);
    mode->insertHead(-1, head2);

    def->addMode(mode);
    return def;
}

/* CMY colour changer without explicit heads: Fixture::setFixtureDefinition()
   creates one head containing every channel, so there is no master
   intensity channel and the head's intensity is channel 0. */
enum CmyChannel { CmyDimmer = 0, CmyCyan, CmyMagenta, CmyYellow, CmyChannelCount };

static QLCFixtureDef *makeCmyDef()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("CMY");
    def->setType(QLCFixtureDef::ColorChanger);

    addPresetChannel(def, QLCChannel::IntensityDimmer);
    addPresetChannel(def, QLCChannel::IntensityCyan);
    addPresetChannel(def, QLCChannel::IntensityMagenta);
    addPresetChannel(def, QLCChannel::IntensityYellow);
    Q_ASSERT(def->channels().size() == CmyChannelCount);

    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("4 Channel");
    for (int i = 0; i < def->channels().size(); i++)
        mode->insertChannel(def->channels().at(i), i);

    def->addMode(mode);
    return def;
}

/* Add a fixture using $def's first mode at DMX $address, placed at $posMm
   (millimetres) in the monitor properties. Returns NULL if Doc refused it. */
static Fixture *addTestFixture(Doc *doc, QLCFixtureDef *def, quint32 address,
                               QVector3D posMm = QVector3D())
{
    Fixture *fxi = new Fixture(doc);
    fxi->setName(QString("%1 @%2").arg(def->model()).arg(address));
    fxi->setFixtureDefinition(def, def->modes().first());
    fxi->setAddress(address);
    if (doc->addFixture(fxi) == false)
    {
        delete fxi;
        return NULL;
    }
    doc->monitorProperties()->setFixturePosition(fxi->id(), 0, 0, posMm);
    return fxi;
}

/* Value of $channel for fixture $fxi in $list, or -1 if absent */
static int valueOf(const QList<SceneValue> &list, quint32 fxi, quint32 channel)
{
    foreach (const SceneValue &sv, list)
    {
        if (sv.fxi == fxi && sv.channel == channel)
            return sv.value;
    }
    return -1;
}

/* Number of values addressed to fixture $fxi in $list */
static int countFor(const QList<SceneValue> &list, quint32 fxi)
{
    int count = 0;
    foreach (const SceneValue &sv, list)
    {
        if (sv.fxi == fxi)
            count++;
    }
    return count;
}

/* Expected MSB/LSB the way Fixture::positionToValues() derives them */
static uchar degreesMSB(qreal degrees, qreal maxDegrees)
{
    quint16 raw = quint16((degrees * 65535.0) / maxDegrees);
    return uchar(raw >> 8);
}

static uchar degreesLSB(qreal degrees, qreal maxDegrees)
{
    quint16 raw = quint16((degrees * 65535.0) / maxDegrees);
    return uchar(raw & 0x00FF);
}

/* Build a <Palette> element from $attrs (or a $root element) and load it */
static bool loadPalette(QLCPalette &p, const QMap<QString, QString> &attrs,
                        const QString &root = QString("Palette"))
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement(root);
    QMapIterator<QString, QString> it(attrs);
    while (it.hasNext())
    {
        it.next();
        xmlWriter.writeAttribute(it.key(), it.value());
    }
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    return p.loadXML(xmlReader);
}

/* Save $p and return the attributes of the written <Palette> element */
static QMap<QString, QString> savePalette(const QLCPalette &p, bool *ok)
{
    QMap<QString, QString> attrs;

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    *ok = p.saveXML(&xmlWriter);

    xmlWriter.setDevice(NULL);
    buffer.close();

    if (*ok == false)
        return attrs;

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    *ok = (xmlReader.name().toString() == "Palette");

    foreach (const QXmlStreamAttribute &attr, xmlReader.attributes())
        attrs[attr.name().toString()] = attr.value().toString();

    return attrs;
}

/*****************************************************************************
 * Properties
 *****************************************************************************/

void QLCPalette_Test::copy()
{
    QLCPalette p(QLCPalette::PanTilt);
    p.setID(9);
    p.setName("Original");
    p.setValue(10, 20);
    p.setFanningType(QLCPalette::Sine);
    p.setFanningLayout(QLCPalette::ZCentered);
    p.setFanningAmount(42);
    p.setFanningValue(15);

    QLCPalette *copy = p.createCopy();
    QVERIFY(copy != NULL);
    QVERIFY(copy != &p);

    QCOMPARE(copy->type(), QLCPalette::PanTilt);
    QCOMPARE(copy->name(), QString("Original"));
    QCOMPARE(copy->values(), p.values());
    QCOMPARE(copy->fanningType(), QLCPalette::Sine);
    QCOMPARE(copy->fanningLayout(), QLCPalette::ZCentered);
    QCOMPARE(copy->fanningAmount(), 42);
    QCOMPARE(copy->fanningValue().toInt(), 15);

    /* the ID is not part of the copy */
    QCOMPARE(copy->id(), QLCPalette::invalidId());

    /* the copy holds its own values */
    copy->setValue(1, 2);
    QCOMPARE(p.values().at(0).toInt(), 10);
    QCOMPARE(p.values().at(1).toInt(), 20);

    delete copy;
}

void QLCPalette_Test::temporary()
{
    QLCPalette p(QLCPalette::Dimmer);
    QVERIFY(p.isTemporary() == true);

    QSignalSpy spy(&p, SIGNAL(temporaryChanged()));

    p.setTemporary(true);
    QCOMPARE(spy.count(), 0);

    p.setTemporary(false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(p.isTemporary() == false);

    p.setTemporary(false);
    QCOMPARE(spy.count(), 1);
}

void QLCPalette_Test::typedValues()
{
    QLCPalette p(QLCPalette::Position3D);

    /* nothing set yet: every typed getter reports "invalid" */
    QCOMPARE(p.intValue1(), -1);
    QCOMPARE(p.intValue2(), -1);
    QCOMPARE(p.floatValue1(), -1.0f);
    QCOMPARE(p.floatValue2(), -1.0f);
    QCOMPARE(p.floatValue3(), -1.0f);
    QVERIFY(p.strValue1().isEmpty());
    QVERIFY(p.rgbValue().isValid() == false);
    QVERIFY(p.wauvValue().isValid() == false);
    QCOMPARE(p.vector3DValue(), QVector3D());
    QVERIFY(p.value().isValid() == false);

    /* one value */
    p.setValue(7);
    QCOMPARE(p.intValue1(), 7);
    QCOMPARE(p.intValue2(), -1);
    QCOMPARE(p.floatValue1(), 7.0f);
    QCOMPARE(p.floatValue2(), -1.0f);
    QCOMPARE(p.floatValue3(), -1.0f);
    QCOMPARE(p.strValue1(), QString("7"));
    QCOMPARE(p.vector3DValue(), QVector3D());

    /* three values */
    p.setValue(1.5f, 2.5f, 3.5f);
    QCOMPARE(p.values().count(), 3);
    QCOMPARE(p.floatValue1(), 1.5f);
    QCOMPARE(p.floatValue2(), 2.5f);
    QCOMPARE(p.floatValue3(), 3.5f);
    QCOMPARE(p.intValue1(), 2);
    QCOMPARE(p.intValue2(), 3);
    QCOMPARE(p.vector3DValue(), QVector3D(1.5f, 2.5f, 3.5f));

    /* a whole list at once */
    QVariantList list;
    list << 4 << 5;
    p.setValues(list);
    QCOMPARE(p.values(), list);
    QCOMPARE(p.intValue1(), 4);
    QCOMPARE(p.intValue2(), 5);
    QCOMPARE(p.floatValue3(), -1.0f);

    p.resetValues();
    QVERIFY(p.values().isEmpty());
    QVERIFY(p.value().isValid() == false);

    /* colour palettes unpack their packed string */
    QLCPalette c(QLCPalette::Color);
    c.setValue(QLCPalette::colorToString(QColor(0x10, 0x20, 0x30), QColor(0x40, 0x50, 0x60)));
    QCOMPARE(c.strValue1(), QString("#102030405060"));
    QCOMPARE(c.rgbValue(), QColor(0x10, 0x20, 0x30));
    QCOMPARE(c.wauvValue(), QColor(0x40, 0x50, 0x60));

    c.setValue("#0a0b0c");
    QCOMPARE(c.rgbValue(), QColor(0x0a, 0x0b, 0x0c));
    QVERIFY(c.wauvValue().isValid() == false);
}

void QLCPalette_Test::setterSignals()
{
    QLCPalette p(QLCPalette::Dimmer);
    QSignalSpy nameSpy(&p, SIGNAL(nameChanged()));
    QSignalSpy typeSpy(&p, SIGNAL(fanningTypeChanged()));
    QSignalSpy layoutSpy(&p, SIGNAL(fanningLayoutChanged()));
    QSignalSpy amountSpy(&p, SIGNAL(fanningAmountChanged()));
    QSignalSpy valueSpy(&p, SIGNAL(fanningValueChanged()));

    p.setName("A");
    QCOMPARE(nameSpy.count(), 1);
    p.setName("A");
    QCOMPARE(nameSpy.count(), 1);

    p.setFanningType(QLCPalette::Flat);
    QCOMPARE(typeSpy.count(), 0);
    p.setFanningType(QLCPalette::Linear);
    QCOMPARE(typeSpy.count(), 1);
    p.setFanningType(QLCPalette::Linear);
    QCOMPARE(typeSpy.count(), 1);
    QCOMPARE(p.fanningType(), QLCPalette::Linear);

    p.setFanningLayout(QLCPalette::XAscending);
    QCOMPARE(layoutSpy.count(), 0);
    p.setFanningLayout(QLCPalette::YCentered);
    QCOMPARE(layoutSpy.count(), 1);
    QCOMPARE(p.fanningLayout(), QLCPalette::YCentered);

    p.setFanningAmount(100);
    QCOMPARE(amountSpy.count(), 0);
    p.setFanningAmount(50);
    QCOMPARE(amountSpy.count(), 1);

    p.setFanningValue(QVariant());
    QCOMPARE(valueSpy.count(), 0);
    p.setFanningValue(5);
    QCOMPARE(valueSpy.count(), 1);
    p.setFanningValue(5);
    QCOMPARE(valueSpy.count(), 1);
    QCOMPARE(p.fanningValue().toInt(), 5);
}

/*****************************************************************************
 * Values from fixtures
 *****************************************************************************/

void QLCPalette_Test::fixturesDimmer()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    QScopedPointer<QLCFixtureDef> bar(makeBarDef());
    QScopedPointer<QLCFixtureDef> cmy(makeCmyDef());
    Doc doc(this);

    Fixture *m = addTestFixture(&doc, mover.data(), 0);
    Fixture *b = addTestFixture(&doc, bar.data(), 20);
    Fixture *c = addTestFixture(&doc, cmy.data(), 40);
    QVERIFY(m != NULL);
    QVERIFY(b != NULL);
    QVERIFY(c != NULL);

    Fixture *d = new Fixture(&doc);
    d->setName("Generic dimmer");
    d->setChannels(4);
    d->setAddress(60);
    QVERIFY(doc.addFixture(d) == true);

    QCOMPARE(m->masterIntensityChannel(), quint32(MoverDimmer));
    QCOMPARE(b->masterIntensityChannel(), quint32(BarMaster));
    QCOMPARE(c->masterIntensityChannel(), QLCChannel::invalid());
    QCOMPARE(d->type(), QLCFixtureDef::Dimmer);

    QLCPalette p(QLCPalette::Dimmer);
    p.setValue(200);

    QList<SceneValue> list = p.valuesFromFixtures(&doc,
            QList<quint32>() << m->id() << b->id() << c->id() << d->id());

    /* mover: only the master, the head has no colourless intensity channel */
    QCOMPARE(countFor(list, m->id()), 1);
    QCOMPARE(valueOf(list, m->id(), MoverDimmer), 200);

    /* bar: master plus one dimmer per head */
    QCOMPARE(countFor(list, b->id()), 3);
    QCOMPARE(valueOf(list, b->id(), BarMaster), 200);
    QCOMPARE(valueOf(list, b->id(), BarDimmer1), 200);
    QCOMPARE(valueOf(list, b->id(), BarDimmer2), 200);

    /* cmy: no master, the auto-generated head maps channel 0 as intensity */
    QCOMPARE(countFor(list, c->id()), 1);
    QCOMPARE(valueOf(list, c->id(), CmyDimmer), 200);

    /* generic dimmer: channel 0 is always treated as the master */
    QVERIFY(countFor(list, d->id()) >= 1);
    QCOMPARE(valueOf(list, d->id(), 0), 200);
}

void QLCPalette_Test::fixturesDimmerFanning()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    Doc doc(this);

    Fixture *f0 = addTestFixture(&doc, mover.data(), 0, QVector3D(0, 0, 0));
    Fixture *f1 = addTestFixture(&doc, mover.data(), 20, QVector3D(1000, 0, 0));
    Fixture *f2 = addTestFixture(&doc, mover.data(), 40, QVector3D(2000, 0, 0));
    QVERIFY(f0 != NULL && f1 != NULL && f2 != NULL);
    QList<quint32> three = QList<quint32>() << f0->id() << f1->id() << f2->id();

    QLCPalette p(QLCPalette::Dimmer);
    p.setValue(0);
    p.setFanningValue(200);
    p.setFanningType(QLCPalette::Linear);
    QCOMPARE(p.fanningLayout(), QLCPalette::XAscending);

    /* Linear, amount 100: factor equals the progress */
    QList<SceneValue> list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(list.size(), 3);
    QCOMPARE(valueOf(list, f0->id(), MoverDimmer), 0);
    QCOMPARE(valueOf(list, f1->id(), MoverDimmer), 100);
    QCOMPARE(valueOf(list, f2->id(), MoverDimmer), 200);

    /* Linear, amount 50: half the slope until the amount is reached, then full */
    p.setFanningAmount(50);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(valueOf(list, f0->id(), MoverDimmer), 0);
    QCOMPARE(valueOf(list, f1->id(), MoverDimmer), 50);
    QCOMPARE(valueOf(list, f2->id(), MoverDimmer), 200);

    /* Linear, amount 200: the slope is halved over the whole range */
    p.setFanningAmount(200);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(valueOf(list, f0->id(), MoverDimmer), 0);
    QCOMPARE(valueOf(list, f1->id(), MoverDimmer), 50);
    QCOMPARE(valueOf(list, f2->id(), MoverDimmer), 100);

    /* Sine, amount 100: one full period, starting and ending at the bottom */
    p.setFanningAmount(100);
    p.setFanningType(QLCPalette::Sine);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(valueOf(list, f0->id(), MoverDimmer), 0);
    /* sin(450 deg) may round to just below 1.0 and the result is truncated */
    QVERIFY(valueOf(list, f1->id(), MoverDimmer) >= 199);
    QVERIFY(valueOf(list, f1->id(), MoverDimmer) <= 200);
    QCOMPARE(valueOf(list, f2->id(), MoverDimmer), 0);

    /* Saw is not implemented: the factor stays 1.0 */
    p.setFanningType(QLCPalette::Saw);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(valueOf(list, f0->id(), MoverDimmer), 200);
    QCOMPARE(valueOf(list, f1->id(), MoverDimmer), 200);
    QCOMPARE(valueOf(list, f2->id(), MoverDimmer), 200);

    /* Square: 5 fixtures give exact quarter steps; the first half period
       is 0, the second half is 1 (the transitions at 0, 0.5 and 1 sit on
       sin() zero crossings and are deliberately not asserted) */
    Fixture *f3 = addTestFixture(&doc, mover.data(), 60, QVector3D(3000, 0, 0));
    Fixture *f4 = addTestFixture(&doc, mover.data(), 80, QVector3D(4000, 0, 0));
    QVERIFY(f3 != NULL && f4 != NULL);
    p.setFanningType(QLCPalette::Square);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << three << f3->id() << f4->id());
    QCOMPARE(list.size(), 5);
    QCOMPARE(valueOf(list, f1->id(), MoverDimmer), 0);
    QCOMPARE(valueOf(list, f3->id(), MoverDimmer), 200);
}

void QLCPalette_Test::fixturesColor()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    QScopedPointer<QLCFixtureDef> bar(makeBarDef());
    QScopedPointer<QLCFixtureDef> cmy(makeCmyDef());
    Doc doc(this);

    Fixture *m = addTestFixture(&doc, mover.data(), 0);
    Fixture *b = addTestFixture(&doc, bar.data(), 20);
    Fixture *c = addTestFixture(&doc, cmy.data(), 40);
    QVERIFY(m != NULL && b != NULL && c != NULL);
    QList<quint32> all = QList<quint32>() << m->id() << b->id() << c->id();

    QLCPalette p(QLCPalette::Color);
    p.setValue(QLCPalette::colorToString(QColor(0xff, 0x80, 0x00), QColor(0x10, 0x20, 0x30)));

    QList<SceneValue> list = p.valuesFromFixtures(&doc, all);

    /* mover: RGB plus white/amber/UV from the packed second colour */
    QCOMPARE(countFor(list, m->id()), 6);
    QCOMPARE(valueOf(list, m->id(), MoverRed), 0xff);
    QCOMPARE(valueOf(list, m->id(), MoverGreen), 0x80);
    QCOMPARE(valueOf(list, m->id(), MoverBlue), 0x00);
    QCOMPARE(valueOf(list, m->id(), MoverWhite), 0x10);
    QCOMPARE(valueOf(list, m->id(), MoverAmber), 0x20);
    QCOMPARE(valueOf(list, m->id(), MoverUV), 0x30);

    /* bar: RGB on both heads, no dimmers touched */
    QCOMPARE(countFor(list, b->id()), 6);
    QCOMPARE(valueOf(list, b->id(), BarRed1), 0xff);
    QCOMPARE(valueOf(list, b->id(), BarGreen1), 0x80);
    QCOMPARE(valueOf(list, b->id(), BarBlue1), 0x00);
    QCOMPARE(valueOf(list, b->id(), BarRed2), 0xff);
    QCOMPARE(valueOf(list, b->id(), BarGreen2), 0x80);
    QCOMPARE(valueOf(list, b->id(), BarBlue2), 0x00);
    QCOMPARE(valueOf(list, b->id(), BarDimmer1), -1);

    /* cmy: the RGB colour is converted to its CMY components */
    QCOMPARE(countFor(list, c->id()), 3);
    QCOMPARE(valueOf(list, c->id(), CmyCyan), QColor(0xff, 0x80, 0x00).cyan());
    QCOMPARE(valueOf(list, c->id(), CmyMagenta), QColor(0xff, 0x80, 0x00).magenta());
    QCOMPARE(valueOf(list, c->id(), CmyYellow), QColor(0xff, 0x80, 0x00).yellow());
    QCOMPARE(valueOf(list, c->id(), CmyCyan), 0);
    QCOMPARE(valueOf(list, c->id(), CmyYellow), 255);

    /* a plain #rrggbb value carries no white/amber/UV information */
    p.setValue("#ff8000");
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 3);
    QCOMPARE(valueOf(list, m->id(), MoverWhite), -1);
}

void QLCPalette_Test::fixturesColorFanning()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    Doc doc(this);

    Fixture *f0 = addTestFixture(&doc, mover.data(), 0, QVector3D(0, 0, 0));
    Fixture *f1 = addTestFixture(&doc, mover.data(), 20, QVector3D(1000, 0, 0));
    Fixture *f2 = addTestFixture(&doc, mover.data(), 40, QVector3D(2000, 0, 0));
    QVERIFY(f0 != NULL && f1 != NULL && f2 != NULL);

    QLCPalette p(QLCPalette::Color);
    p.setValue("#ff0000");
    p.setFanningType(QLCPalette::Linear);
    p.setFanningValue(QVariant::fromValue(QColor(0, 0, 255)));

    QList<SceneValue> list = p.valuesFromFixtures(&doc,
            QList<quint32>() << f0->id() << f1->id() << f2->id());
    QCOMPARE(list.size(), 9);

    /* first fixture: the start colour */
    QCOMPARE(valueOf(list, f0->id(), MoverRed), 255);
    QCOMPARE(valueOf(list, f0->id(), MoverGreen), 0);
    QCOMPARE(valueOf(list, f0->id(), MoverBlue), 0);

    /* middle fixture: half way (qRound() of +/-127.5) */
    QCOMPARE(valueOf(list, f1->id(), MoverRed), 127);
    QCOMPARE(valueOf(list, f1->id(), MoverGreen), 0);
    QCOMPARE(valueOf(list, f1->id(), MoverBlue), 128);

    /* last fixture: the fanning end colour */
    QCOMPARE(valueOf(list, f2->id(), MoverRed), 0);
    QCOMPARE(valueOf(list, f2->id(), MoverGreen), 0);
    QCOMPARE(valueOf(list, f2->id(), MoverBlue), 255);
}

void QLCPalette_Test::fixturesPan()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    QScopedPointer<QLCFixtureDef> bar(makeBarDef());
    Doc doc(this);

    Fixture *f0 = addTestFixture(&doc, mover.data(), 0, QVector3D(0, 0, 0));
    Fixture *f1 = addTestFixture(&doc, mover.data(), 20, QVector3D(1000, 0, 0));
    Fixture *f2 = addTestFixture(&doc, mover.data(), 40, QVector3D(2000, 0, 0));
    Fixture *b = addTestFixture(&doc, bar.data(), 60);
    QVERIFY(f0 != NULL && f1 != NULL && f2 != NULL && b != NULL);
    QList<quint32> three = QList<quint32>() << f0->id() << f1->id() << f2->id();

    QLCPalette p(QLCPalette::Pan);
    p.setValue(270);

    /* flat: every mover gets the same 16bit position, the bar nothing */
    QList<SceneValue> list = p.valuesFromFixtures(&doc, QList<quint32>() << f0->id() << b->id());
    QCOMPARE(list.size(), 2);
    QCOMPARE(valueOf(list, f0->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));
    QCOMPARE(valueOf(list, f0->id(), MoverPanFine), int(degreesLSB(270, MoverPanMax)));
    QCOMPARE(countFor(list, b->id()), 0);

    /* linear fanning adds up to the fan value along the layout */
    p.setFanningType(QLCPalette::Linear);
    p.setFanningValue(90);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(list.size(), 6);
    QCOMPARE(valueOf(list, f0->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));
    QCOMPARE(valueOf(list, f1->id(), MoverPan), int(degreesMSB(315, MoverPanMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverPan), int(degreesMSB(360, MoverPanMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverPanFine), int(degreesLSB(360, MoverPanMax)));

    /* centered layouts fan symmetrically away from the middle fixture */
    p.setFanningLayout(QLCPalette::XCentered);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(list.size(), 6);
    QCOMPARE(valueOf(list, f0->id(), MoverPan), int(degreesMSB(180, MoverPanMax)));
    QCOMPARE(valueOf(list, f1->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverPan), int(degreesMSB(360, MoverPanMax)));

    /* all fixtures on the centre line: no distance to fan over */
    Fixture *f3 = addTestFixture(&doc, mover.data(), 80, QVector3D(500, 7, 7));
    Fixture *f4 = addTestFixture(&doc, mover.data(), 100, QVector3D(500, 9, 9));
    QVERIFY(f3 != NULL && f4 != NULL);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << f3->id() << f4->id());
    QCOMPARE(list.size(), 4);
    QCOMPARE(valueOf(list, f3->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));
    QCOMPARE(valueOf(list, f4->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));
}

void QLCPalette_Test::fixturesTilt()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    Doc doc(this);

    Fixture *f0 = addTestFixture(&doc, mover.data(), 0, QVector3D(0, 0, 0));
    Fixture *f1 = addTestFixture(&doc, mover.data(), 20, QVector3D(0, 1000, 0));
    Fixture *f2 = addTestFixture(&doc, mover.data(), 40, QVector3D(0, 2000, 0));
    QVERIFY(f0 != NULL && f1 != NULL && f2 != NULL);
    QList<quint32> three = QList<quint32>() << f0->id() << f1->id() << f2->id();

    QLCPalette p(QLCPalette::Tilt);
    p.setValue(135);

    QList<SceneValue> list = p.valuesFromFixtures(&doc, QList<quint32>() << f0->id());
    QCOMPARE(list.size(), 2);
    QCOMPARE(valueOf(list, f0->id(), MoverTilt), int(degreesMSB(135, MoverTiltMax)));
    QCOMPARE(valueOf(list, f0->id(), MoverTiltFine), int(degreesLSB(135, MoverTiltMax)));

    /* a two-value (pan, tilt) list uses the second entry */
    p.setValue(0, 135);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << f0->id());
    QCOMPARE(list.size(), 2);
    QCOMPARE(valueOf(list, f0->id(), MoverTilt), int(degreesMSB(135, MoverTiltMax)));

    /* Y centered fanning */
    p.setValue(135);
    p.setFanningType(QLCPalette::Linear);
    p.setFanningLayout(QLCPalette::YCentered);
    p.setFanningValue(45);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(list.size(), 6);
    QCOMPARE(valueOf(list, f0->id(), MoverTilt), int(degreesMSB(90, MoverTiltMax)));
    QCOMPARE(valueOf(list, f1->id(), MoverTilt), int(degreesMSB(135, MoverTiltMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverTilt), int(degreesMSB(180, MoverTiltMax)));

    /* Y ascending fanning */
    p.setFanningLayout(QLCPalette::YAscending);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(valueOf(list, f0->id(), MoverTilt), int(degreesMSB(135, MoverTiltMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverTilt), int(degreesMSB(180, MoverTiltMax)));
}

void QLCPalette_Test::fixturesPanTilt()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    Doc doc(this);

    Fixture *f0 = addTestFixture(&doc, mover.data(), 0, QVector3D(0, 0, 0));
    Fixture *f1 = addTestFixture(&doc, mover.data(), 20, QVector3D(0, 0, 1000));
    Fixture *f2 = addTestFixture(&doc, mover.data(), 40, QVector3D(0, 0, 2000));
    QVERIFY(f0 != NULL && f1 != NULL && f2 != NULL);
    QList<quint32> three = QList<quint32>() << f0->id() << f1->id() << f2->id();

    QLCPalette p(QLCPalette::PanTilt);

    /* a PanTilt palette needs exactly two values */
    p.setValue(270);
    QList<SceneValue> list = p.valuesFromFixtures(&doc, QList<quint32>() << f0->id());
    QCOMPARE(list.size(), 0);

    p.setValue(270, 135);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << f0->id());
    QCOMPARE(list.size(), 4);
    QCOMPARE(valueOf(list, f0->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));
    QCOMPARE(valueOf(list, f0->id(), MoverPanFine), int(degreesLSB(270, MoverPanMax)));
    QCOMPARE(valueOf(list, f0->id(), MoverTilt), int(degreesMSB(135, MoverTiltMax)));
    QCOMPARE(valueOf(list, f0->id(), MoverTiltFine), int(degreesLSB(135, MoverTiltMax)));

    /* Z centered fanning applies the same signed offset to both axes */
    p.setFanningType(QLCPalette::Linear);
    p.setFanningLayout(QLCPalette::ZCentered);
    p.setFanningValue(90);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(list.size(), 12);
    QCOMPARE(valueOf(list, f0->id(), MoverPan), int(degreesMSB(180, MoverPanMax)));
    QCOMPARE(valueOf(list, f0->id(), MoverTilt), int(degreesMSB(45, MoverTiltMax)));
    QCOMPARE(valueOf(list, f1->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));
    QCOMPARE(valueOf(list, f1->id(), MoverTilt), int(degreesMSB(135, MoverTiltMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverPan), int(degreesMSB(360, MoverPanMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverTilt), int(degreesMSB(225, MoverTiltMax)));

    /* Z ascending fanning */
    p.setFanningLayout(QLCPalette::ZAscending);
    list = p.valuesFromFixtures(&doc, three);
    QCOMPARE(valueOf(list, f0->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverPan), int(degreesMSB(360, MoverPanMax)));
    QCOMPARE(valueOf(list, f2->id(), MoverTilt), int(degreesMSB(225, MoverTiltMax)));
}

void QLCPalette_Test::fixturesShutter()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    QScopedPointer<QLCFixtureDef> bar(makeBarDef());
    Doc doc(this);

    Fixture *m = addTestFixture(&doc, mover.data(), 0);
    Fixture *b = addTestFixture(&doc, bar.data(), 20);
    QVERIFY(m != NULL && b != NULL);
    QList<quint32> both = QList<quint32>() << m->id() << b->id();

    QLCPalette p(QLCPalette::Shutter);

    /* a Shutter palette needs (preset, percentage) */
    p.setValue(int(QLCCapability::ShutterOpen));
    QList<SceneValue> list = p.valuesFromFixtures(&doc, both);
    QCOMPARE(list.size(), 0);

    /* open/close use the middle of the capability range */
    p.setValue(int(QLCCapability::ShutterOpen), 50);
    list = p.valuesFromFixtures(&doc, both);
    QCOMPARE(list.size(), 1);
    QCOMPARE(valueOf(list, m->id(), MoverShutter), (10 + 19) / 2);
    QCOMPARE(countFor(list, b->id()), 0);

    p.setValue(int(QLCCapability::ShutterClose), 100);
    list = p.valuesFromFixtures(&doc, both);
    QCOMPARE(list.size(), 1);
    QCOMPARE(valueOf(list, m->id(), MoverShutter), (0 + 9) / 2);

    /* strobe-like presets scale the percentage over the range */
    p.setValue(int(QLCCapability::StrobeSlowToFast), 50);
    list = p.valuesFromFixtures(&doc, both);
    QCOMPARE(list.size(), 1);
    QCOMPARE(valueOf(list, m->id(), MoverShutter), 20 + (255 - 20) * 50 / 100);

    p.setValue(int(QLCCapability::StrobeSlowToFast), 0);
    list = p.valuesFromFixtures(&doc, both);
    QCOMPARE(valueOf(list, m->id(), MoverShutter), 20);

    p.setValue(int(QLCCapability::StrobeSlowToFast), 100);
    list = p.valuesFromFixtures(&doc, both);
    QCOMPARE(valueOf(list, m->id(), MoverShutter), 255);

    /* a preset the shutter channel does not offer yields nothing */
    p.setValue(int(QLCCapability::StrobeRandom), 50);
    list = p.valuesFromFixtures(&doc, both);
    QCOMPARE(list.size(), 0);
}

void QLCPalette_Test::fixturesGobo()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    QScopedPointer<QLCFixtureDef> bar(makeBarDef());
    Doc doc(this);

    Fixture *m = addTestFixture(&doc, mover.data(), 0);
    Fixture *b = addTestFixture(&doc, bar.data(), 20);
    QVERIFY(m != NULL && b != NULL);

    QLCPalette p(QLCPalette::Gobo);
    p.setValue(42);

    /* the bar has no gobo channel at all */
    QList<SceneValue> list = p.valuesFromFixtures(&doc, QList<quint32>() << b->id());
    QCOMPARE(list.size(), 0);

    /* The mover does have a gobo wheel, but QLCFixtureHead::cacheChannels()
       never maps the Gobo group, so Fixture::channelNumber(QLCChannel::Gobo)
       cannot resolve it and Gobo palettes currently produce no values. */
    QVERIFY(m->channelNumber(QLCChannel::Gobo, QLCChannel::MSB) == QLCChannel::invalid());
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QEXPECT_FAIL("", "Gobo channels are not cached by QLCFixtureHead::cacheChannels()", Continue);
    QCOMPARE(list.size(), 1);
    QEXPECT_FAIL("", "Gobo channels are not cached by QLCFixtureHead::cacheChannels()", Continue);
    QCOMPARE(valueOf(list, m->id(), MoverGobo), 42);
}

void QLCPalette_Test::fixturesZoom()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    QScopedPointer<QLCFixtureDef> bar(makeBarDef());
    Doc doc(this);

    Fixture *m = addTestFixture(&doc, mover.data(), 0);
    Fixture *b = addTestFixture(&doc, bar.data(), 20);
    QVERIFY(m != NULL && b != NULL);

    QLCPalette p(QLCPalette::Zoom);
    p.setValue(35.0f);

    QList<SceneValue> list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id() << b->id());

    /* 35 degrees is half way through the 10..60 lens range: 0x7FFF */
    QCOMPARE(countFor(list, b->id()), 0);
    QCOMPARE(countFor(list, m->id()), 2);
    QCOMPARE(valueOf(list, m->id(), MoverZoom),
             int(degreesMSB(35.0 - MoverZoomMin, MoverZoomMax - MoverZoomMin)));
    QCOMPARE(valueOf(list, m->id(), MoverZoomFine),
             int(degreesLSB(35.0 - MoverZoomMin, MoverZoomMax - MoverZoomMin)));

    /* values outside the lens range are clamped */
    p.setValue(1000.0f);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(valueOf(list, m->id(), MoverZoom), 255);
    QCOMPARE(valueOf(list, m->id(), MoverZoomFine), 255);

    p.setValue(-5.0f);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(valueOf(list, m->id(), MoverZoom), 0);
    QCOMPARE(valueOf(list, m->id(), MoverZoomFine), 0);
}

void QLCPalette_Test::fixturesPosition3D()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    QScopedPointer<QLCFixtureDef> bar(makeBarDef());
    Doc doc(this);
    MonitorProperties *mProps = doc.monitorProperties();
    mProps->setGridSize(QVector3D(10, 3, 10));
    mProps->setGridUnits(MonitorProperties::Meters);

    /* the mover sits at the grid origin with no rotation */
    Fixture *m = addTestFixture(&doc, mover.data(), 0, QVector3D(0, 0, 0));
    Fixture *b = addTestFixture(&doc, bar.data(), 20);
    QVERIFY(m != NULL && b != NULL);

    QLCPalette p(QLCPalette::Position3D);

    /* needs three values */
    p.setValue(1.0f, 0.0f);
    QList<SceneValue> list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 0);

    /* a fixture without pan/tilt cannot point anywhere */
    p.setValue(1.0f, 0.0f, 1.0f);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << b->id());
    QCOMPARE(list.size(), 0);

    /* target on the +x/+z diagonal at the fixture's height: pan 225, tilt 45
       (the beam axis is -y, so a horizontal target is 90 degrees off axis,
       which maps to the middle of the 270 degree tilt range minus 90) */
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 4);
    QCOMPARE(valueOf(list, m->id(), MoverPan), int(degreesMSB(225, MoverPanMax)));
    QCOMPARE(valueOf(list, m->id(), MoverTilt), int(degreesMSB(45, MoverTiltMax)));
    QVERIFY(valueOf(list, m->id(), MoverPanFine) >= 0);
    QVERIFY(valueOf(list, m->id(), MoverTiltFine) >= 0);

    /* the remaining three pan quadrants */
    p.setValue(-1.0f, 0.0f, 1.0f);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 4);
    QCOMPARE(valueOf(list, m->id(), MoverPan), int(degreesMSB(135, MoverPanMax)));

    p.setValue(-1.0f, 0.0f, -1.0f);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 4);
    QCOMPARE(valueOf(list, m->id(), MoverPan), int(degreesMSB(45, MoverPanMax)));

    p.setValue(1.0f, 0.0f, -1.0f);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 4);
    QCOMPARE(valueOf(list, m->id(), MoverPan), int(degreesMSB(315, MoverPanMax)));

    /* almost straight down (slightly towards +x): the tilt is the angle
       between the -y beam axis and the target direction, subtracted from
       the centred 135 degrees; the pan is on the +x axis (90 + 180) */
    p.setValue(1.0f, -5.0f, 0.0f);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 4);
    qreal downTilt = (MoverTiltMax / 2) - qRadiansToDegrees(qAcos(5.0 / qSqrt(26.0)));
    QCOMPARE(valueOf(list, m->id(), MoverTilt), int(degreesMSB(downTilt, MoverTiltMax)));
    QCOMPARE(valueOf(list, m->id(), MoverPan), int(degreesMSB(270, MoverPanMax)));

    /* inverted flags: pan is mirrored over the pan range, tilt adds the raw
       90 degree off-axis angle to the centre instead of subtracting it */
    mProps->setFixtureFlags(m->id(), 0, 0,
                            MonitorProperties::InvertedPanFlag | MonitorProperties::InvertedTiltFlag);
    p.setValue(1.0f, 0.0f, 1.0f);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 4);
    QCOMPARE(valueOf(list, m->id(), MoverPan), int(degreesMSB(MoverPanMax - 225, MoverPanMax)));
    QCOMPARE(valueOf(list, m->id(), MoverTilt), int(degreesMSB(135 + 90, MoverTiltMax)));
    mProps->setFixtureFlags(m->id(), 0, 0, 0);

    /* imperial grid units only change the metric conversion of the offset */
    mProps->setGridUnits(MonitorProperties::Feet);
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 4);
    QCOMPARE(valueOf(list, m->id(), MoverPan), int(degreesMSB(225, MoverPanMax)));
    mProps->setGridUnits(MonitorProperties::Meters);

    /* a rotated fixture pans relative to its own orientation */
    mProps->setFixtureRotation(m->id(), 0, 0, QVector3D(0, 90, 0));
    list = p.valuesFromFixtures(&doc, QList<quint32>() << m->id());
    QCOMPARE(list.size(), 4);
    QVERIFY(valueOf(list, m->id(), MoverPan) != int(degreesMSB(225, MoverPanMax)));
}

void QLCPalette_Test::fixturesLayouts()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    Doc doc(this);

    /* three fixtures whose x, y and z orderings all differ */
    Fixture *a = addTestFixture(&doc, mover.data(), 0, QVector3D(0, 2000, 1000));
    Fixture *b = addTestFixture(&doc, mover.data(), 20, QVector3D(1000, 0, 2000));
    Fixture *c = addTestFixture(&doc, mover.data(), 40, QVector3D(2000, 1000, 0));
    QVERIFY(a != NULL && b != NULL && c != NULL);
    QList<quint32> ids = QList<quint32>() << a->id() << b->id() << c->id();

    /* Linear fanning from 0 to 200 gives the first fixture in layout order 0,
       the middle one 100 and the last one 200 - or, for centered layouts,
       0 for the fixture in the middle and 200 for the two outer ones */
    QLCPalette p(QLCPalette::Dimmer);
    p.setValue(0);
    p.setFanningValue(200);
    p.setFanningType(QLCPalette::Linear);

    struct Expectation { QLCPalette::FanningLayout layout; int a; int b; int c; };
    const Expectation expectations[] = {
        { QLCPalette::XAscending,  0,   100, 200 },
        { QLCPalette::XDescending, 200, 100, 0   },
        { QLCPalette::XCentered,   200, 0,   200 },
        { QLCPalette::YAscending,  200, 0,   100 },
        { QLCPalette::YDescending, 0,   200, 100 },
        { QLCPalette::YCentered,   200, 200, 0   },
        { QLCPalette::ZAscending,  100, 200, 0   },
        { QLCPalette::ZDescending, 100, 0,   200 },
        { QLCPalette::ZCentered,   0,   200, 200 },
    };

    for (const Expectation &e : expectations)
    {
        QByteArray ctx = QLCPalette::fanningLayoutToString(e.layout).toLatin1();
        p.setFanningLayout(e.layout);
        QList<SceneValue> list = p.valuesFromFixtures(&doc, ids);
        QCOMPARE(list.size(), 3);
        QVERIFY2(valueOf(list, a->id(), MoverDimmer) == e.a,
                 qPrintable(QString("%1: a=%2").arg(ctx.constData()).arg(valueOf(list, a->id(), MoverDimmer))));
        QVERIFY2(valueOf(list, b->id(), MoverDimmer) == e.b,
                 qPrintable(QString("%1: b=%2").arg(ctx.constData()).arg(valueOf(list, b->id(), MoverDimmer))));
        QVERIFY2(valueOf(list, c->id(), MoverDimmer) == e.c,
                 qPrintable(QString("%1: c=%2").arg(ctx.constData()).arg(valueOf(list, c->id(), MoverDimmer))));
    }
}

void QLCPalette_Test::fixturesMisc()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    Doc doc(this);

    Fixture *m = addTestFixture(&doc, mover.data(), 0);
    QVERIFY(m != NULL);

    /* an Undefined palette never produces values */
    QLCPalette u(QLCPalette::Undefined);
    u.setValue(1);
    QCOMPARE(u.valuesFromFixtures(&doc, QList<quint32>() << m->id()).size(), 0);

    /* unknown fixture IDs are skipped, known ones still processed */
    QLCPalette p(QLCPalette::Dimmer);
    p.setValue(10);
    QList<SceneValue> list = p.valuesFromFixtures(&doc, QList<quint32>() << 9999 << m->id());
    QCOMPARE(list.size(), 1);
    QCOMPARE(valueOf(list, m->id(), MoverDimmer), 10);

    /* no fixtures, no values */
    QCOMPARE(p.valuesFromFixtures(&doc, QList<quint32>()).size(), 0);
}

void QLCPalette_Test::fixtureGroups()
{
    QScopedPointer<QLCFixtureDef> mover(makeMoverDef());
    Doc doc(this);

    Fixture *f0 = addTestFixture(&doc, mover.data(), 0);
    Fixture *f1 = addTestFixture(&doc, mover.data(), 20);
    Fixture *f2 = addTestFixture(&doc, mover.data(), 40);
    QVERIFY(f0 != NULL && f1 != NULL && f2 != NULL);

    FixtureGroup *g1 = new FixtureGroup(&doc);
    g1->setName("Outer");
    QVERIFY(doc.addFixtureGroup(g1) == true);
    QVERIFY(g1->assignFixture(f0->id()) == true);
    QVERIFY(g1->assignFixture(f2->id()) == true);

    FixtureGroup *g2 = new FixtureGroup(&doc);
    g2->setName("Middle");
    QVERIFY(doc.addFixtureGroup(g2) == true);
    QVERIFY(g2->assignFixture(f1->id()) == true);

    QLCPalette p(QLCPalette::Dimmer);
    p.setValue(77);

    /* unknown group IDs are skipped, the rest is flattened into one list */
    QList<SceneValue> list = p.valuesFromFixtureGroups(&doc,
            QList<quint32>() << g1->id() << 4242 << g2->id());
    QCOMPARE(list.size(), 3);
    QCOMPARE(valueOf(list, f0->id(), MoverDimmer), 77);
    QCOMPARE(valueOf(list, f1->id(), MoverDimmer), 77);
    QCOMPARE(valueOf(list, f2->id(), MoverDimmer), 77);

    list = p.valuesFromFixtureGroups(&doc, QList<quint32>() << g2->id());
    QCOMPARE(list.size(), 1);
    QCOMPARE(valueOf(list, f1->id(), MoverDimmer), 77);

    QCOMPARE(p.valuesFromFixtureGroups(&doc, QList<quint32>() << 4242).size(), 0);
}

/*****************************************************************************
 * Load & Save
 *****************************************************************************/

void QLCPalette_Test::loaderFailure()
{
    Doc doc(this);
    int before = doc.palettes().size();

    /* an unparsable ID makes loadXML() fail, so loader() adds nothing */
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    xmlWriter.writeStartElement("Palette");
    xmlWriter.writeAttribute("ID", "notanumber");
    xmlWriter.writeAttribute("Type", "Dimmer");
    xmlWriter.writeAttribute("Value", "1");
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(QLCPalette::loader(xmlReader, &doc) == false);
    QCOMPARE(doc.palettes().size(), before);
}

void QLCPalette_Test::loadValueTypes()
{
    QMap<QString, QString> attrs;
    attrs["ID"] = "5";

    /* Pan / Tilt: one integer */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Pan";
        attrs["Value"] = "45";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.id(), quint32(5));
        QCOMPARE(p.type(), QLCPalette::Pan);
        QCOMPARE(p.values().count(), 1);
        QCOMPARE(p.intValue1(), 45);
    }
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Tilt";
        attrs["Value"] = "-30";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.type(), QLCPalette::Tilt);
        QCOMPARE(p.intValue1(), -30);
    }

    /* Color: the packed string as-is */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Color";
        attrs["Value"] = "#112233";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.strValue1(), QString("#112233"));
        QCOMPARE(p.rgbValue(), QColor(0x11, 0x22, 0x33));
    }

    /* PanTilt: two comma separated integers */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "PanTilt";
        attrs["Value"] = "10,20";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.values().count(), 2);
        QCOMPARE(p.intValue1(), 10);
        QCOMPARE(p.intValue2(), 20);
    }
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "PanTilt";
        attrs["Value"] = "10";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.values().count(), 0);
    }

    /* Position3D: three comma separated floats */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Position3D";
        attrs["Value"] = "1.5,2.5,3.5";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.values().count(), 3);
        QCOMPARE(p.vector3DValue(), QVector3D(1.5f, 2.5f, 3.5f));
    }
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Position3D";
        attrs["Value"] = "1,2";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.values().count(), 0);
    }

    /* Dimmer / Zoom: one float */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Dimmer";
        attrs["Value"] = "128.5";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.values().count(), 1);
        QCOMPARE(p.floatValue1(), 128.5f);
    }
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Zoom";
        attrs["Value"] = "42";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.floatValue1(), 42.0f);
    }

    /* Shutter: preset,percentage */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Shutter";
        attrs["Value"] = "7,50";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.values().count(), 2);
        QCOMPARE(p.intValue1(), 7);
        QCOMPARE(p.intValue2(), 50);
    }
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Shutter";
        attrs["Value"] = "7";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.values().count(), 0);
    }

    /* Gobo values are not (yet) serialised */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Gobo";
        attrs["Value"] = "3";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.type(), QLCPalette::Gobo);
        QCOMPARE(p.values().count(), 0);
    }

    /* unknown type strings load as Undefined without a value */
    {
        QLCPalette p(QLCPalette::Dimmer);
        attrs["Type"] = "Bogus";
        attrs["Value"] = "3";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.type(), QLCPalette::Undefined);
        QCOMPARE(p.values().count(), 0);
    }

    /* no Value attribute at all is fine */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Dimmer";
        attrs.remove("Value");
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.type(), QLCPalette::Dimmer);
        QCOMPARE(p.values().count(), 0);
    }

    /* the Type attribute is mandatory */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs.remove("Type");
        attrs["Value"] = "1";
        QVERIFY(loadPalette(p, attrs) == false);
    }

    /* and so is a numeric ID */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Dimmer";
        attrs.remove("ID");
        QVERIFY(loadPalette(p, attrs) == false);
    }
}

void QLCPalette_Test::loadFanValueTypes()
{
    QMap<QString, QString> attrs;
    attrs["ID"] = "1";
    attrs["Fan"] = "Linear";
    attrs["Layout"] = "ZDescending";
    attrs["Amount"] = "60";
    attrs["FanValue"] = "33";

    /* numeric fan values */
    const char *intTypes[] = { "Dimmer", "Pan", "Tilt", "PanTilt", "Zoom" };
    for (const char *type : intTypes)
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = type;
        QVERIFY2(loadPalette(p, attrs) == true, type);
        QCOMPARE(p.fanningType(), QLCPalette::Linear);
        QCOMPARE(p.fanningLayout(), QLCPalette::ZDescending);
        QCOMPARE(p.fanningAmount(), 60);
        QVERIFY2(p.fanningValue().isValid(), type);
        QCOMPARE(p.fanningValue().toInt(), 33);
        QCOMPARE(p.fanningValue().userType(), int(QMetaType::Int));
    }

    /* colour fan values stay strings */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = "Color";
        attrs["FanValue"] = "#00ff00";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.fanningValue().toString(), QString("#00ff00"));
        QCOMPARE(p.fanningValue().userType(), int(QMetaType::QString));
    }

    /* types without fanning support ignore the fan value */
    const char *noFanTypes[] = { "Position3D", "Shutter", "Gobo", "Bogus" };
    for (const char *type : noFanTypes)
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs["Type"] = type;
        attrs["FanValue"] = "33";
        QVERIFY2(loadPalette(p, attrs) == true, type);
        QCOMPARE(p.fanningType(), QLCPalette::Linear);
        QVERIFY2(p.fanningValue().isValid() == false, type);
    }

    /* a lone Fan attribute keeps the layout and amount defaults */
    {
        QLCPalette p(QLCPalette::Undefined);
        attrs.clear();
        attrs["ID"] = "1";
        attrs["Type"] = "Dimmer";
        attrs["Fan"] = "Square";
        QVERIFY(loadPalette(p, attrs) == true);
        QCOMPARE(p.fanningType(), QLCPalette::Square);
        QCOMPARE(p.fanningLayout(), QLCPalette::XAscending);
        QCOMPARE(p.fanningAmount(), 100);
        QVERIFY(p.fanningValue().isValid() == false);
    }
}

void QLCPalette_Test::saveValueTypes()
{
    bool ok = false;
    QMap<QString, QString> attrs;

    /* single value types */
    {
        QLCPalette p(QLCPalette::Dimmer);
        p.setID(1);
        p.setName("Full");
        p.setValue(128.0f);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("ID"), QString("1"));
        QCOMPARE(attrs.value("Type"), QString("Dimmer"));
        QCOMPARE(attrs.value("Name"), QString("Full"));
        QCOMPARE(attrs.value("Value"), QString("128"));
        QVERIFY(attrs.contains("Fan") == false);
    }
    {
        QLCPalette p(QLCPalette::Pan);
        p.setValue(45);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Type"), QString("Pan"));
        QCOMPARE(attrs.value("Value"), QString("45"));
    }
    {
        QLCPalette p(QLCPalette::Tilt);
        p.setValue(-12);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Type"), QString("Tilt"));
        QCOMPARE(attrs.value("Value"), QString("-12"));
    }
    {
        QLCPalette p(QLCPalette::Zoom);
        p.setValue(42.5f);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Type"), QString("Zoom"));
        QCOMPARE(attrs.value("Value"), QString("42.5"));
    }
    {
        QLCPalette p(QLCPalette::Color);
        p.setValue("#aabbcc");
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Type"), QString("Color"));
        QCOMPARE(attrs.value("Value"), QString("#aabbcc"));
    }

    /* Position3D: three floats, or nothing if the list is incomplete */
    {
        QLCPalette p(QLCPalette::Position3D);
        p.setValue(1.5f, 2.5f, 3.5f);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Type"), QString("Position3D"));
        QCOMPARE(attrs.value("Value"), QString("1.5,2.5,3.5"));

        /* and it must load back to the same vector */
        QLCPalette back(QLCPalette::Undefined);
        attrs["ID"] = "3";
        QVERIFY(loadPalette(back, attrs) == true);
        QCOMPARE(back.vector3DValue(), QVector3D(1.5f, 2.5f, 3.5f));
    }
    {
        QLCPalette p(QLCPalette::Position3D);
        p.setValue(1.5f, 2.5f);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QVERIFY(attrs.contains("Value") == false);
    }

    /* Shutter: preset,percentage, or nothing if the list is incomplete */
    {
        QLCPalette p(QLCPalette::Shutter);
        p.setValue(7, 50);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Type"), QString("Shutter"));
        QCOMPARE(attrs.value("Value"), QString("7,50"));

        QLCPalette back(QLCPalette::Undefined);
        attrs["ID"] = "4";
        QVERIFY(loadPalette(back, attrs) == true);
        QCOMPARE(back.intValue1(), 7);
        QCOMPARE(back.intValue2(), 50);
    }
    {
        QLCPalette p(QLCPalette::Shutter);
        p.setValue(7);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QVERIFY(attrs.contains("Value") == false);
    }

    /* Gobo and Undefined write no value */
    {
        QLCPalette p(QLCPalette::Gobo);
        p.setValue(3);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Type"), QString("Gobo"));
        QVERIFY(attrs.contains("Value") == false);
    }
    {
        QLCPalette p(QLCPalette::Undefined);
        p.setValue(3);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QVERIFY(attrs.contains("Type"));
        QCOMPARE(attrs.value("Type"), QString(""));
        QVERIFY(attrs.contains("Value") == false);
    }

    /* fanning is written only when it is not flat */
    {
        QLCPalette p(QLCPalette::Pan);
        p.setValue(10);
        p.setFanningType(QLCPalette::Sine);
        p.setFanningLayout(QLCPalette::ZCentered);
        p.setFanningAmount(33);
        p.setFanningValue(77);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QCOMPARE(attrs.value("Fan"), QString("Sine"));
        QCOMPARE(attrs.value("Layout"), QString("ZCentered"));
        QCOMPARE(attrs.value("Amount"), QString("33"));
        QCOMPARE(attrs.value("FanValue"), QString("77"));

        p.setFanningType(QLCPalette::Flat);
        attrs = savePalette(p, &ok);
        QVERIFY(ok);
        QVERIFY(attrs.contains("Fan") == false);
        QVERIFY(attrs.contains("Layout") == false);
        QVERIFY(attrs.contains("Amount") == false);
        QVERIFY(attrs.contains("FanValue") == false);
    }
}

void QLCPalette_Test::saveNoValue()
{
    QLCPalette p(QLCPalette::Dimmer);
    p.setID(1);
    p.setName("Empty");

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(p.saveXML(&xmlWriter) == false);

    xmlWriter.setDevice(NULL);
    buffer.close();
    QVERIFY(buffer.data().isEmpty());
}

/* QTEST_GUILESS_MAIN: the fixture based cases build a Doc with fixtures and
   monitor properties, whose helpers rely on QCoreApplication::applicationDirPath(). */
QTEST_GUILESS_MAIN(QLCPalette_Test)
