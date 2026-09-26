/*
  Q Light Controller Plus - Unit tests
  avolitesd4parser_test.cpp

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
#include <QFile>
#include <QXmlStreamReader>

#define private public
#include "avolitesd4parser_test.h"
#include "avolitesd4parser.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "qlccapability.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#undef private

/****************************************************************************
 * Test documents
 ****************************************************************************/

// A structurally complete D4 fixture: every attribute group, 8-bit and 16-bit
// channels, capabilities, three modes with Include and Physical sections, and
// a sprinkling of unknown/malformed tags that the parser must skip over.
static const char *kFullFixture = R"D4(<?xml version="1.0" encoding="utf-8"?>
<Fixture Name="Test Spot" ShortName="TSpot" Company="TestCo">
  <Control>
    <Attribute ID="Dimmer" Name="Dimmer" Group="I">
      <Function Name="Closed" Dmx="0~10"/>
      <Function Name="Open" Dmx="11~255"/>
      <Function Name="" Dmx="0~255"/>
      <Function Name="NoDmx"/>
      <Bogus/>
    </Attribute>
    <Attribute ID="Shutter" Name="Shutter" Group="I">
      <Function Name="Closed" Dmx="0~15"/>
      <Function Name="Strobe" Dmx="255~16"/>
    </Attribute>
    <Attribute ID="Pan" Name="Pan" Group="P">
      <Function Name="Pan" Dmx="0~65535"/>
    </Attribute>
    <Attribute ID="Tilt" Name="Tilt" Group="P">
      <Function Name="Tilt" Dmx="65535~0"/>
    </Attribute>
    <Attribute ID="Cyan" Name="Cyan" Group="C">
      <Function Name="Cyan" Dmx="0~255"/>
    </Attribute>
    <Attribute ID="Magenta" Name="Magenta" Group="c">
      <Function Name="Magenta" Dmx="0~255"/>
    </Attribute>
    <Attribute ID="Yellow" Name="Yellow" Group="C"/>
    <Attribute ID="Red" Name="Red" Group="C"/>
    <Attribute ID="Green" Name="Green" Group="C"/>
    <Attribute ID="Blue" Name="Blue" Group="C"/>
    <Attribute ID="Colour1" Name="Colour Wheel" Group="C">
      <Function Name="Open" Dmx="100"/>
      <Function Name="Amber" Dmx="101~255"/>
    </Attribute>
    <Attribute ID="Gobo1" Name="Gobo" Group="G">
      <Function Name="Open" Dmx="0~255"/>
    </Attribute>
    <Attribute ID="Iris" Name="Iris" Group="B"/>
    <Attribute ID="Prism" Name="Prism" Group="E"/>
    <Attribute ID="FX" Name="Effect" Group="E"/>
    <Attribute ID="FXMacro" Name="Macro FX" Group="E"/>
    <Attribute ID="Rotate" Name="Rotation" Group="E"/>
    <Attribute ID="Speed" Name="Speed" Group="S"/>
    <Attribute ID="Macro" Name="Macros" Group="S"/>
    <Attribute ID="Reserved1" Name="Reserved" Group="S"/>
    <Attribute ID="Lamp" Name="Lamp Control" Group="S"/>
    <Attribute ID="Weird" Name="Weird" Group="Z"/>
    <Attribute ID="NoGroupAttr" Name="Fan"/>
    <Attribute ID="Blank"/>
    <Attribute Name="NoID" Group="I"/>
    <Attribute ID="PT" Name="P/T Speed" Group="P"/>
    <Bogus/>
  </Control>
  <Palettes>
    <Palette Name="Ignored"/>
  </Palettes>
  <Something/>
  <Mode Name="Mode 1">
    <Include>
      <Attribute ID="Dimmer" ChannelOffset="1"/>
      <Attribute ID="Pan" ChannelOffset="2,3"/>
      <Attribute ID="Tilt" ChannelOffset="4,5"/>
      <Attribute ID="Shutter"/>
      <Attribute ID="DoesNotExist" ChannelOffset="6"/>
      <Bogus/>
    </Include>
    <Physical>
      <Bulb Type="MSD 575" Lumens="49000" ColourTemp="7200"/>
      <Lens Name="Fresnel" Degrees="40~10"/>
      <Weight Kg="35.5"/>
      <Size Height="0.5" Width="0.4" Depth="0.3"/>
      <Focus Type="Head" PanMax="540" TiltMax="270"/>
      <Bogus/>
    </Physical>
    <Bogus/>
  </Mode>
  <Mode Name="Mode 2">
    <Include>
      <Attribute ID="Dimmer" ChannelOffset="1"/>
      <Attribute ID="Gobo1" ChannelOffset="2"/>
    </Include>
    <Physical>
      <Bulb Type="MSD 575" Lumens="49000" ColourTemp="7200"/>
      <Lens Name="Fresnel" Degrees="10~40"/>
      <Weight Kg="35.5"/>
      <Size Height="0.5" Width="0.4" Depth="0.3"/>
      <Focus Type="Head" PanMax="540" TiltMax="270"/>
    </Physical>
  </Mode>
  <Mode Name="Mode 3">
    <Include>
      <Attribute ID="Dimmer" ChannelOffset="1"/>
    </Include>
    <Physical>
      <Bulb Type="LED" Lumens="1000" ColourTemp="6500"/>
      <Lens Name="Other" Degrees="25"/>
    </Physical>
  </Mode>
</Fixture>
)D4";

/****************************************************************************
 * Helpers
 ****************************************************************************/

void AvolitesD4Parser_Test::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

QString AvolitesD4Parser_Test::writeDocument(const QString& name, const QString& content)
{
    QString path = m_dir.path() + "/" + name;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) == false)
        return QString();
    file.write(content.toUtf8());
    file.close();
    return path;
}

QString AvolitesD4Parser_Test::fixtureWithControl(const QString& controlBody)
{
    return QString("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                   "<Fixture Name=\"Guess\" Company=\"TestCo\">\n"
                   "  <Control>\n%1\n  </Control>\n"
                   "</Fixture>\n").arg(controlBody);
}

/****************************************************************************
 * loadXML() error paths
 ****************************************************************************/

void AvolitesD4Parser_Test::emptyPath()
{
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QVERIFY(parser.lastError().isEmpty());
    QCOMPARE(parser.loadXML(QString(), &def), false);
    QCOMPARE(parser.lastError(), QString("filename not specified"));
}

void AvolitesD4Parser_Test::missingFile()
{
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = m_dir.path() + "/does-not-exist.d4";
    QCOMPARE(parser.loadXML(path, &def), false);
    QCOMPARE(parser.lastError(), QString("Unable to read from %1").arg(path));
}

void AvolitesD4Parser_Test::nonXmlFile()
{
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = writeDocument("notxml.d4", "this is not an xml document\n");
    QVERIFY(path.isEmpty() == false);
    QCOMPARE(parser.loadXML(path, &def), false);
    QCOMPARE(parser.lastError(), QString("wrong document format"));

    // An empty file is equally not a D4 document
    path = writeDocument("empty.d4", "");
    QCOMPARE(parser.loadXML(path, &def), false);
    QCOMPARE(parser.lastError(), QString("wrong document format"));
}

void AvolitesD4Parser_Test::wrongRootElement()
{
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = writeDocument("wrongroot.d4",
        "<?xml version=\"1.0\"?>\n<FixtureDefinition Name=\"X\" Company=\"Y\"/>\n");
    QCOMPARE(parser.loadXML(path, &def), false);
    QCOMPARE(parser.lastError(), QString("wrong document format"));
    QVERIFY(def.manufacturer().isEmpty());
    QVERIFY(def.model().isEmpty());

    // The failed load must not keep the file open: an early return that
    // leaked the XML reader (and its QFile) made this remove() fail on
    // Windows and left every test's temporary directory behind in %TEMP%.
    QVERIFY(QFile::remove(path));
}

void AvolitesD4Parser_Test::missingRequiredAttributes()
{
    AvolitesD4Parser parser;
    QLCFixtureDef def;

    // Company missing
    QString path = writeDocument("nocompany.d4",
        "<?xml version=\"1.0\"?>\n<Fixture Name=\"X\"/>\n");
    QCOMPARE(parser.loadXML(path, &def), false);
    QCOMPARE(parser.lastError(), QString("the document doesn't have the required attributes"));

    // Name missing
    path = writeDocument("noname.d4",
        "<?xml version=\"1.0\"?>\n<Fixture Company=\"Y\"/>\n");
    QCOMPARE(parser.loadXML(path, &def), false);
    QCOMPARE(parser.lastError(), QString("the document doesn't have the required attributes"));

    // Both present, no content at all: a valid (if useless) fixture
    path = writeDocument("bare.d4",
        "<?xml version=\"1.0\"?>\n<Fixture Name=\"X\" Company=\"Y\"/>\n");
    QCOMPARE(parser.loadXML(path, &def), true);
    QVERIFY(parser.lastError().isEmpty());
    QCOMPARE(def.manufacturer(), QString("Y"));
    QCOMPARE(def.model(), QString("X"));
    QCOMPARE(def.author(), QString("Avolites"));
    QCOMPARE(def.type(), QLCFixtureDef::Other);
    QCOMPARE(def.channels().size(), 0);
    QCOMPARE(def.modes().size(), 0);
}

/****************************************************************************
 * loadXML() happy paths
 ****************************************************************************/

void AvolitesD4Parser_Test::channels()
{
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = writeDocument("full.d4", kFullFixture);
    QVERIFY(parser.loadXML(path, &def) == true);
    QVERIFY(parser.lastError().isEmpty());

    QCOMPARE(def.manufacturer(), QString("TestCo"));
    QCOMPARE(def.model(), QString("Test Spot"));
    QCOMPARE(def.author(), QString("Avolites"));
    // 16-bit pan & tilt -> moving head
    QCOMPARE(def.type(), QLCFixtureDef::MovingHead);

    // Every Attribute with an ID becomes a channel (plus one "Fine" channel
    // per 16-bit function); the one without an ID is skipped.
    QCOMPARE(def.channels().size(), 27);

    QLCChannel *ch = def.channel("Dimmer");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Intensity);
    QCOMPARE(ch->colour(), QLCChannel::NoColour);
    QCOMPARE(ch->controlByte(), QLCChannel::MSB);
    // The nameless function and the one without a Dmx range add nothing
    QCOMPARE(ch->capabilities().size(), 2);
    QCOMPARE(ch->capabilities().at(0)->name(), QString("Closed"));
    QCOMPARE(int(ch->capabilities().at(0)->min()), 0);
    QCOMPARE(int(ch->capabilities().at(0)->max()), 10);
    QCOMPARE(ch->capabilities().at(1)->name(), QString("Open"));
    QCOMPARE(int(ch->capabilities().at(1)->min()), 11);
    QCOMPARE(int(ch->capabilities().at(1)->max()), 255);

    // "255~16": min and max swapped in the document, normalised by the parser
    ch = def.channel("Shutter");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Shutter);
    QCOMPARE(ch->capabilities().size(), 2);
    QCOMPARE(ch->capabilities().at(1)->name(), QString("Strobe"));
    QCOMPARE(int(ch->capabilities().at(1)->min()), 16);
    QCOMPARE(int(ch->capabilities().at(1)->max()), 255);

    // 16-bit pan: coarse channel plus a generated "Fine" LSB channel
    ch = def.channel("Pan");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Pan);
    QCOMPARE(ch->controlByte(), QLCChannel::MSB);
    QCOMPARE(ch->capabilities().size(), 1);
    QCOMPARE(ch->capabilities().at(0)->name(), QString("Pan"));
    QCOMPARE(int(ch->capabilities().at(0)->min()), 0);
    QCOMPARE(int(ch->capabilities().at(0)->max()), 255);

    ch = def.channel("Pan Fine");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Pan);
    QCOMPARE(ch->controlByte(), QLCChannel::LSB);
    QCOMPARE(ch->capabilities().size(), 1);
    QCOMPARE(ch->capabilities().at(0)->name(), QString("Pan Fine"));
    QCOMPARE(int(ch->capabilities().at(0)->min()), 0);
    QCOMPARE(int(ch->capabilities().at(0)->max()), 255);

    // 16-bit tilt with the range written the other way round
    ch = def.channel("Tilt");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Tilt);
    QCOMPARE(ch->capabilities().size(), 1);
    QCOMPARE(int(ch->capabilities().at(0)->min()), 0);
    QCOMPARE(int(ch->capabilities().at(0)->max()), 255);
    ch = def.channel("Tilt Fine");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Tilt);
    QCOMPARE(ch->controlByte(), QLCChannel::LSB);

    // Colour mixing channels are intensities with a primary colour
    struct { const char *name; QLCChannel::PrimaryColour colour; } colours[] = {
        { "Cyan", QLCChannel::Cyan }, { "Magenta", QLCChannel::Magenta },
        { "Yellow", QLCChannel::Yellow }, { "Red", QLCChannel::Red },
        { "Green", QLCChannel::Green }, { "Blue", QLCChannel::Blue }
    };
    for (size_t i = 0; i < sizeof(colours) / sizeof(colours[0]); i++)
    {
        ch = def.channel(colours[i].name);
        QVERIFY2(ch != NULL, colours[i].name);
        QCOMPARE(ch->group(), QLCChannel::Intensity);
        QCOMPARE(ch->colour(), colours[i].colour);
    }

    // A colour wheel is a Colour channel without a primary colour; a single
    // Dmx value is treated as a 0~value range
    ch = def.channel("Colour Wheel");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Colour);
    QCOMPARE(ch->colour(), QLCChannel::NoColour);
    QCOMPARE(ch->capabilities().size(), 2);
    QCOMPARE(ch->capabilities().at(0)->name(), QString("Open"));
    QCOMPARE(int(ch->capabilities().at(0)->min()), 0);
    QCOMPARE(int(ch->capabilities().at(0)->max()), 100);
    QCOMPARE(ch->capabilities().at(1)->name(), QString("Amber"));
    QCOMPARE(int(ch->capabilities().at(1)->min()), 101);
    QCOMPARE(int(ch->capabilities().at(1)->max()), 255);

    ch = def.channel("Gobo");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Gobo);
    QCOMPARE(ch->capabilities().size(), 1);

    ch = def.channel("Iris");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Beam);

    ch = def.channel("Prism");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Prism);

    ch = def.channel("Effect");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Effect);

    ch = def.channel("Macro FX");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Effect);

    ch = def.channel("Rotation");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::NoGroup);

    ch = def.channel("Speed");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Speed);

    ch = def.channel("Macros");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Effect);

    ch = def.channel("Reserved");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::NoGroup);

    ch = def.channel("Lamp Control");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Maintenance);

    // Unknown group letters and a missing group both fall back to SPECIAL
    ch = def.channel("Weird");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Maintenance);
    ch = def.channel("Fan");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::Maintenance);

    // Pan/tilt group without pan or tilt in the name
    ch = def.channel("P/T Speed");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::NoGroup);

    // The Attribute without a Name or Group still yields a (nameless) channel
    ch = def.channel("");
    QVERIFY(ch != NULL);
    QCOMPARE(ch->group(), QLCChannel::NoGroup);

    QVERIFY(def.channel("NoID") == NULL);
}

void AvolitesD4Parser_Test::modes()
{
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = writeDocument("full.d4", kFullFixture);
    QVERIFY(parser.loadXML(path, &def) == true);

    QCOMPARE(def.modes().size(), 3);

    // Mode 1: channels are ordered by their ChannelOffset, 16-bit offsets
    // ("2,3") place the coarse and the generated fine channel; the
    // conditional attribute (no offset) and the unknown ID are ignored.
    QLCFixtureMode *mode = def.mode("Mode 1");
    QVERIFY(mode != NULL);
    QCOMPARE(mode->channels().size(), 5);
    QCOMPARE(mode->channel(0)->name(), QString("Dimmer"));
    QCOMPARE(mode->channel(1)->name(), QString("Pan"));
    QCOMPARE(mode->channel(2)->name(), QString("Pan Fine"));
    QCOMPARE(mode->channel(3)->name(), QString("Tilt"));
    QCOMPARE(mode->channel(4)->name(), QString("Tilt Fine"));
    QCOMPARE(mode->useGlobalPhysical(), true);

    // The first mode's Physical becomes the fixture's global physical
    QLCPhysical phys = def.physical();
    QCOMPARE(phys.bulbType(), QString("MSD 575"));
    QCOMPARE(phys.bulbLumens(), 49000);
    QCOMPARE(phys.bulbColourTemperature(), 7200);
    QCOMPARE(phys.lensName(), QString("Fresnel"));
    // "40~10" is normalised to min 10, max 40
    QCOMPARE(phys.lensDegreesMin(), 10.0);
    QCOMPARE(phys.lensDegreesMax(), 40.0);
    QCOMPARE(phys.weight(), 35.5);
    QCOMPARE(phys.height(), 500);
    QCOMPARE(phys.width(), 400);
    QCOMPARE(phys.depth(), 300);
    QCOMPARE(phys.focusType(), QString("Head"));
    QCOMPARE(phys.focusPanMax(), 540);
    QCOMPARE(phys.focusTiltMax(), 270);

    // Mode 2: identical physical -> still global
    mode = def.mode("Mode 2");
    QVERIFY(mode != NULL);
    QCOMPARE(mode->channels().size(), 2);
    QCOMPARE(mode->channel(0)->name(), QString("Dimmer"));
    QCOMPARE(mode->channel(1)->name(), QString("Gobo"));
    QCOMPARE(mode->useGlobalPhysical(), true);

    // Mode 3: different physical -> stored on the mode, global untouched
    mode = def.mode("Mode 3");
    QVERIFY(mode != NULL);
    QCOMPARE(mode->channels().size(), 1);
    QCOMPARE(mode->channel(0)->name(), QString("Dimmer"));
    QCOMPARE(mode->useGlobalPhysical(), false);
    QCOMPARE(mode->physical().bulbType(), QString("LED"));
    QCOMPARE(mode->physical().bulbLumens(), 1000);
    QCOMPARE(mode->physical().bulbColourTemperature(), 6500);
    QCOMPARE(mode->physical().lensName(), QString("Other"));
    QCOMPARE(mode->physical().lensDegreesMin(), 25.0);
    QCOMPARE(mode->physical().lensDegreesMax(), 25.0);
    QCOMPARE(mode->physical().weight(), 0.0);
    QCOMPARE(def.physical().bulbLumens(), 49000);
}

void AvolitesD4Parser_Test::unknownTopLevelTags()
{
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = writeDocument("unknown.d4",
        "<?xml version=\"1.0\"?>\n"
        "<Fixture Name=\"X\" Company=\"Y\">\n"
        "  <Palettes><Palette Name=\"P1\"><Value/></Palette></Palettes>\n"
        "  <Unknown><Nested/></Unknown>\n"
        "  <Control>\n"
        "    <Attribute ID=\"Dimmer\" Name=\"Dimmer\" Group=\"I\"/>\n"
        "  </Control>\n"
        "</Fixture>\n");
    QVERIFY(parser.loadXML(path, &def) == true);
    QCOMPARE(def.channels().size(), 1);
    QCOMPARE(def.type(), QLCFixtureDef::Dimmer);
}

void AvolitesD4Parser_Test::overlongChannelOffset()
{
    // A ChannelOffset with more than two addresses can't be mapped; it must
    // be skipped without derailing the rest of the include, the mode, or the
    // modes that follow it.
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = writeDocument("overlong.d4",
        "<?xml version=\"1.0\"?>\n"
        "<Fixture Name=\"X\" Company=\"Y\">\n"
        "  <Control>\n"
        "    <Attribute ID=\"Dimmer\" Name=\"Dimmer\" Group=\"I\"/>\n"
        "    <Attribute ID=\"Gobo\" Name=\"Gobo\" Group=\"G\"/>\n"
        "  </Control>\n"
        "  <Mode Name=\"Broken\">\n"
        "    <Include>\n"
        "      <Attribute ID=\"Gobo\" ChannelOffset=\"1,2,3\"/>\n"
        "      <Attribute ID=\"Dimmer\" ChannelOffset=\"4\"/>\n"
        "    </Include>\n"
        "  </Mode>\n"
        "  <Mode Name=\"After\">\n"
        "    <Include>\n"
        "      <Attribute ID=\"Dimmer\" ChannelOffset=\"1\"/>\n"
        "    </Include>\n"
        "  </Mode>\n"
        "</Fixture>\n");
    QVERIFY(parser.loadXML(path, &def) == true);
    QCOMPARE(def.modes().size(), 2);

    QLCFixtureMode *mode = def.mode("Broken");
    QVERIFY(mode != NULL);
    QCOMPARE(mode->channels().size(), 1);
    QCOMPARE(mode->channel(0)->name(), QString("Dimmer"));

    mode = def.mode("After");
    QVERIFY(mode != NULL);
    QCOMPARE(mode->channels().size(), 1);
    QCOMPARE(mode->channel(0)->name(), QString("Dimmer"));
}

void AvolitesD4Parser_Test::namelessMode()
{
    // A Mode without a Name is dropped, but the modes after it must survive.
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = writeDocument("nameless.d4",
        "<?xml version=\"1.0\"?>\n"
        "<Fixture Name=\"X\" Company=\"Y\">\n"
        "  <Control>\n"
        "    <Attribute ID=\"Dimmer\" Name=\"Dimmer\" Group=\"I\"/>\n"
        "  </Control>\n"
        "  <Mode>\n"
        "    <Include>\n"
        "      <Attribute ID=\"Dimmer\" ChannelOffset=\"1\"/>\n"
        "    </Include>\n"
        "  </Mode>\n"
        "  <Mode Name=\"Named\">\n"
        "    <Include>\n"
        "      <Attribute ID=\"Dimmer\" ChannelOffset=\"1\"/>\n"
        "    </Include>\n"
        "  </Mode>\n"
        "</Fixture>\n");
    QVERIFY(parser.loadXML(path, &def) == true);
    QCOMPARE(def.modes().size(), 1);
    QLCFixtureMode *mode = def.mode("Named");
    QVERIFY(mode != NULL);
    QCOMPARE(mode->channels().size(), 1);
}

void AvolitesD4Parser_Test::guessType_data()
{
    QTest::addColumn<QString>("control");
    QTest::addColumn<int>("type");

    QTest::newRow("16-bit pan/tilt -> moving head")
        << "<Attribute ID=\"Pan\" Name=\"Pan\" Group=\"P\"><Function Name=\"Pan\" Dmx=\"0~65535\"/></Attribute>"
           "<Attribute ID=\"Tilt\" Name=\"Tilt\" Group=\"P\"><Function Name=\"Tilt\" Dmx=\"0~65535\"/></Attribute>"
        << int(QLCFixtureDef::MovingHead);
    QTest::newRow("8-bit pan/tilt -> scanner")
        << "<Attribute ID=\"Pan\" Name=\"Pan\" Group=\"P\"><Function Name=\"Pan\" Dmx=\"0~255\"/></Attribute>"
           "<Attribute ID=\"Tilt\" Name=\"Tilt\" Group=\"P\"><Function Name=\"Tilt\" Dmx=\"0~255\"/></Attribute>"
        << int(QLCFixtureDef::Scanner);
    QTest::newRow("gobo only -> flower")
        << "<Attribute ID=\"Gobo\" Name=\"Gobo\" Group=\"G\"/>"
        << int(QLCFixtureDef::Flower);
    QTest::newRow("colour wheel -> colour changer")
        << "<Attribute ID=\"Colour\" Name=\"Colour\" Group=\"C\"/>"
        << int(QLCFixtureDef::ColorChanger);
    QTest::newRow("rgb -> colour changer")
        << "<Attribute ID=\"Red\" Name=\"Red\" Group=\"C\"/>"
           "<Attribute ID=\"Green\" Name=\"Green\" Group=\"C\"/>"
           "<Attribute ID=\"Blue\" Name=\"Blue\" Group=\"C\"/>"
        << int(QLCFixtureDef::ColorChanger);
    QTest::newRow("cmy -> colour changer")
        << "<Attribute ID=\"Cyan\" Name=\"Cyan\" Group=\"C\"/>"
           "<Attribute ID=\"Magenta\" Name=\"Magenta\" Group=\"C\"/>"
           "<Attribute ID=\"Yellow\" Name=\"Yellow\" Group=\"C\"/>"
        << int(QLCFixtureDef::ColorChanger);
    QTest::newRow("red only -> other")
        << "<Attribute ID=\"Red\" Name=\"Red\" Group=\"C\"/>"
        << int(QLCFixtureDef::Other);
    QTest::newRow("shutter with strobe capability -> strobe")
        << "<Attribute ID=\"Shutter\" Name=\"Shutter\" Group=\"I\">"
           "<Function Name=\"Closed\" Dmx=\"0~15\"/><Function Name=\"Strobe\" Dmx=\"16~255\"/></Attribute>"
        << int(QLCFixtureDef::Strobe);
    QTest::newRow("shutter without strobe capability -> other")
        << "<Attribute ID=\"Shutter\" Name=\"Shutter\" Group=\"I\">"
           "<Function Name=\"Closed\" Dmx=\"0~255\"/></Attribute>"
        << int(QLCFixtureDef::Other);
    QTest::newRow("channel named strobe -> strobe")
        << "<Attribute ID=\"Strobe\" Name=\"Strobe\" Group=\"B\"/>"
           "<Attribute ID=\"Dimmer\" Name=\"Dimmer\" Group=\"I\"/>"
        << int(QLCFixtureDef::Strobe);
    QTest::newRow("smoke -> smoke")
        << "<Attribute ID=\"Smoke\" Name=\"Smoke\" Group=\"B\"/>"
           "<Attribute ID=\"Haze\" Name=\"Haze\" Group=\"B\"/>"
        << int(QLCFixtureDef::Smoke);
    QTest::newRow("haze -> hazer")
        << "<Attribute ID=\"Haze\" Name=\"Haze\" Group=\"B\"/>"
           "<Attribute ID=\"Dimmer\" Name=\"Dimmer\" Group=\"I\"/>"
        << int(QLCFixtureDef::Hazer);
    QTest::newRow("plain intensity -> dimmer")
        << "<Attribute ID=\"Dimmer\" Name=\"Dimmer\" Group=\"I\"/>"
        << int(QLCFixtureDef::Dimmer);
    QTest::newRow("maintenance only -> other")
        << "<Attribute ID=\"Lamp\" Name=\"Lamp\" Group=\"S\"/>"
        << int(QLCFixtureDef::Other);
    QTest::newRow("no channels -> other")
        << ""
        << int(QLCFixtureDef::Other);
}

void AvolitesD4Parser_Test::guessType()
{
    QFETCH(QString, control);
    QFETCH(int, type);

    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QString path = writeDocument("guess.d4", fixtureWithControl(control));
    QVERIFY(parser.loadXML(path, &def) == true);
    QCOMPARE(int(def.type()), type);
}

/****************************************************************************
 * Private helpers
 ****************************************************************************/

void AvolitesD4Parser_Test::is16Bit()
{
    AvolitesD4Parser parser;
    QCOMPARE(parser.is16Bit(""), false);
    QCOMPARE(parser.is16Bit("100"), false);
    QCOMPARE(parser.is16Bit("0~255"), false);
    QCOMPARE(parser.is16Bit("256~0"), false);
    QCOMPARE(parser.is16Bit("300"), true);
    QCOMPARE(parser.is16Bit("0~65535"), true);
    QCOMPARE(parser.is16Bit("65535~0"), true);
}

void AvolitesD4Parser_Test::getCapability()
{
    AvolitesD4Parser parser;

    QVERIFY(parser.getCapability("", "Foo") == NULL);

    QLCCapability *cap = parser.getCapability("100", "Single");
    QVERIFY(cap != NULL);
    QCOMPARE(cap->name(), QString("Single"));
    QCOMPARE(int(cap->min()), 0);
    QCOMPARE(int(cap->max()), 100);
    delete cap;

    cap = parser.getCapability("255~16", "Swapped");
    QVERIFY(cap != NULL);
    QCOMPARE(int(cap->min()), 16);
    QCOMPARE(int(cap->max()), 255);
    delete cap;

    // 16-bit values are reduced to their coarse byte
    cap = parser.getCapability("300~50000", "Wide", true);
    QVERIFY(cap != NULL);
    QCOMPARE(cap->name(), QString("Wide Fine"));
    QCOMPARE(int(cap->min()), 1);
    QCOMPARE(int(cap->max()), 195);
    delete cap;

    cap = parser.getCapability("0~65535", "Full");
    QVERIFY(cap != NULL);
    QCOMPARE(cap->name(), QString("Full"));
    QCOMPARE(int(cap->min()), 0);
    QCOMPARE(int(cap->max()), 255);
    delete cap;
}

void AvolitesD4Parser_Test::stringToAttributeEnum()
{
    AvolitesD4Parser parser;
    QCOMPARE(parser.stringToAttributeEnum(""), AvolitesD4Parser::SPECIAL);
    QCOMPARE(parser.stringToAttributeEnum("S"), AvolitesD4Parser::SPECIAL);
    QCOMPARE(parser.stringToAttributeEnum("I"), AvolitesD4Parser::INTENSITY);
    QCOMPARE(parser.stringToAttributeEnum("P"), AvolitesD4Parser::PANTILT);
    QCOMPARE(parser.stringToAttributeEnum("c"), AvolitesD4Parser::COLOUR);
    QCOMPARE(parser.stringToAttributeEnum("G"), AvolitesD4Parser::GOBO);
    QCOMPARE(parser.stringToAttributeEnum("b"), AvolitesD4Parser::BEAM);
    QCOMPARE(parser.stringToAttributeEnum("E"), AvolitesD4Parser::EFFECT);
    QCOMPARE(parser.stringToAttributeEnum("ZZ"), AvolitesD4Parser::SPECIAL);
}

void AvolitesD4Parser_Test::getGroup()
{
    AvolitesD4Parser parser;

    QCOMPARE(parser.getGroup("ID", "", ""), QLCChannel::NoGroup);

    // Special
    QCOMPARE(parser.getGroup("Speed", "", "S"), QLCChannel::Speed);
    QCOMPARE(parser.getGroup("X", "Pan Speed", "S"), QLCChannel::Speed);
    QCOMPARE(parser.getGroup("Macro", "", "S"), QLCChannel::Effect);
    QCOMPARE(parser.getGroup("X", "Reserved", "S"), QLCChannel::NoGroup);
    QCOMPARE(parser.getGroup("X", "Lamp", "S"), QLCChannel::Maintenance);
    QCOMPARE(parser.getGroup("X", "Lamp", ""), QLCChannel::Maintenance);
    QCOMPARE(parser.getGroup("X", "Lamp", "Q"), QLCChannel::Maintenance);

    // Intensity
    QCOMPARE(parser.getGroup("Shutter", "", "I"), QLCChannel::Shutter);
    QCOMPARE(parser.getGroup("X", "Dimmer", "I"), QLCChannel::Intensity);

    // Pan/Tilt
    QCOMPARE(parser.getGroup("Pan", "", "P"), QLCChannel::Pan);
    QCOMPARE(parser.getGroup("X", "tilt", "p"), QLCChannel::Tilt);
    QCOMPARE(parser.getGroup("X", "Rotation", "P"), QLCChannel::NoGroup);

    // Colour
    QCOMPARE(parser.getGroup("Cyan", "", "C"), QLCChannel::Intensity);
    QCOMPARE(parser.getGroup("X", "Magenta", "C"), QLCChannel::Intensity);
    QCOMPARE(parser.getGroup("Yellow", "", "C"), QLCChannel::Intensity);
    QCOMPARE(parser.getGroup("X", "Red", "C"), QLCChannel::Intensity);
    QCOMPARE(parser.getGroup("Green", "", "C"), QLCChannel::Intensity);
    QCOMPARE(parser.getGroup("X", "Blue", "C"), QLCChannel::Intensity);
    QCOMPARE(parser.getGroup("X", "Wheel", "C"), QLCChannel::Colour);

    // Gobo / Beam
    QCOMPARE(parser.getGroup("X", "Anything", "G"), QLCChannel::Gobo);
    QCOMPARE(parser.getGroup("X", "Anything", "B"), QLCChannel::Beam);

    // Effect
    QCOMPARE(parser.getGroup("Prism", "", "E"), QLCChannel::Prism);
    QCOMPARE(parser.getGroup("X", "Effect", "E"), QLCChannel::Effect);
    QCOMPARE(parser.getGroup("Macro", "", "E"), QLCChannel::Effect);
    QCOMPARE(parser.getGroup("X", "Rotation", "E"), QLCChannel::NoGroup);
}

void AvolitesD4Parser_Test::getColour()
{
    AvolitesD4Parser parser;

    QCOMPARE(parser.getColour("Red", "Red", "I"), QLCChannel::NoColour);
    QCOMPARE(parser.getColour("Red", "Red", ""), QLCChannel::NoColour);

    QCOMPARE(parser.getColour("Cyan", "", "C"), QLCChannel::Cyan);
    QCOMPARE(parser.getColour("X", "Magenta", "c"), QLCChannel::Magenta);
    QCOMPARE(parser.getColour("Yellow", "", "C"), QLCChannel::Yellow);
    QCOMPARE(parser.getColour("X", "Red", "C"), QLCChannel::Red);
    QCOMPARE(parser.getColour("Green", "", "C"), QLCChannel::Green);
    QCOMPARE(parser.getColour("X", "blue", "C"), QLCChannel::Blue);
    QCOMPARE(parser.getColour("X", "Wheel", "C"), QLCChannel::NoColour);
}

void AvolitesD4Parser_Test::comparePhysical()
{
    AvolitesD4Parser parser;
    QLCPhysical global;
    QLCPhysical mode;

    // Nothing global yet: anything goes
    mode.setBulbLumens(100);
    QCOMPARE(parser.comparePhysical(global, mode), true);

    global.setBulbLumens(100);
    global.setBulbColourTemperature(6500);
    global.setWeight(10);
    global.setWidth(100);
    global.setHeight(200);
    global.setDepth(300);
    global.setLensDegreesMin(10);
    global.setLensDegreesMax(40);
    global.setFocusPanMax(540);
    global.setFocusTiltMax(270);
    global.setPowerConsumption(500);

    QCOMPARE(parser.comparePhysical(global, mode), false);

    mode = global;
    QCOMPARE(parser.comparePhysical(global, mode), true);

    // Fields not compared don't matter
    mode.setBulbType("Other");
    mode.setLensName("Other");
    mode.setFocusType("Other");
    QCOMPARE(parser.comparePhysical(global, mode), true);

    mode.setPowerConsumption(501);
    QCOMPARE(parser.comparePhysical(global, mode), false);
}

void AvolitesD4Parser_Test::tagGuards()
{
    // Each parse helper refuses to run when the reader isn't positioned on
    // the element it expects.
    AvolitesD4Parser parser;
    QLCFixtureDef def;
    QLCFixtureMode mode(&def);

    QXmlStreamReader xml("<Foo><Bar/></Foo>");
    QVERIFY(xml.readNextStartElement() == true);
    QCOMPARE(xml.name().toString(), QString("Foo"));

    QCOMPARE(parser.parseChannel(&xml, &def), false);
    QCOMPARE(parser.parseAttribute(&xml, &def), false);
    QCOMPARE(parser.parseMode(&xml, &def), false);
    parser.parsePhysical(&xml, &def, &mode);
    parser.parseInclude(&xml, &mode);

    QCOMPARE(xml.name().toString(), QString("Foo"));
    QCOMPARE(def.channels().size(), 0);
    QCOMPARE(def.modes().size(), 0);
    QCOMPARE(mode.channels().size(), 0);
    QVERIFY(def.physical().isEmpty());
    QCOMPARE(mode.useGlobalPhysical(), true);
}

QTEST_GUILESS_MAIN(AvolitesD4Parser_Test)
