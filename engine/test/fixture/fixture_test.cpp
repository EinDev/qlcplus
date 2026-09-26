/*
  Q Light Controller Plus - Unit test
  fixture_test.cpp

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

#include "qlcfixturedefcache.h"
#include "qlcmodifierscache.h"
#include "channelmodifier.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "qlccapability.h"
#include "qlcphysical.h"
#include "qlcchannel.h"
#include "qlcconfig.h"
#include "qlcfile.h"

#include "fixture_test.h"
// componentsToString()/stringToComponents() are protected helpers of Fixture;
// expose them the same way the neighbouring suites do for their classes.
#define protected public
#include "fixture.h"
#undef protected
#include "doc.h"

#include "../common/resource_paths.h"

void Fixture_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(m_doc->fixtureDefCache()->loadMap(dir) == true);
}

void Fixture_Test::cleanupTestCase()
{
    delete m_doc;
}

void Fixture_Test::id()
{
    QVERIFY(Fixture::invalidId() == UINT_MAX);

    Fixture fxi(this);
    QVERIFY(fxi.id() == Fixture::invalidId());

    fxi.setID(50);
    QVERIFY(fxi.id() == 50);

    fxi.setID(INT_MAX);
    QVERIFY(fxi.id() == INT_MAX);
}

void Fixture_Test::name()
{
    Fixture fxi(this);
    QVERIFY(fxi.name().isEmpty());

    fxi.setName("MyFixture");
    QVERIFY(fxi.name() == "MyFixture");
}

void Fixture_Test::address()
{
    Fixture fxi(this);
    fxi.setChannels(5);

    QVERIFY(fxi.address() == 0);
    QVERIFY(fxi.universe() == 0);
    QVERIFY(fxi.universeAddress() == 0);

    fxi.setUniverse(1);
    QVERIFY(fxi.address() == 0);
    QVERIFY(fxi.universe() == 1);
    QVERIFY(fxi.universeAddress() == (1 << 9));

    fxi.setUniverse(2);
    QVERIFY(fxi.address() == 0);
    QVERIFY(fxi.universe() == 2);
    QVERIFY(fxi.universeAddress() == (2 << 9));

    fxi.setUniverse(3);
    QVERIFY(fxi.address() == 0);
    QVERIFY(fxi.universe() == 3);
    QVERIFY(fxi.universeAddress() == (3 << 9));

    /* The application might support only 4 universes, but there's no
       reason why Fixture itself couldn't support a million universes,
       as long as it fits into a uint minus 9 bits. */
    fxi.setUniverse(100);
    QVERIFY(fxi.address() == 0);
    QVERIFY(fxi.universe() == 100);
    QVERIFY(fxi.universeAddress() == (100 << 9));

    fxi.setAddress(15);
    fxi.setUniverse(0);
    QVERIFY(fxi.address() == 15);
    QVERIFY(fxi.universe() == 0);
    QVERIFY(fxi.universeAddress() == 15);

    /* Fixture should allow address overflow; maybe the first two channels
       that still fit to the universe here are enough for some fixture,
       who knows? */
    fxi.setAddress(510);
    QVERIFY(fxi.address() == 510);
    QVERIFY(fxi.universe() == 0);
    QVERIFY(fxi.universeAddress() == 510);

    /* Invalid addresses should not be allowed */
    fxi.setAddress(600);
    QVERIFY(fxi.address() == 510);
    QVERIFY(fxi.universe() == 0);
    QVERIFY(fxi.universeAddress() == 510);

    fxi.setAddress(100);
    QVERIFY(fxi.channelAddress(0) == 100);
    QVERIFY(fxi.channelAddress(1) == 101);
    QVERIFY(fxi.channelAddress(2) == 102);
    QVERIFY(fxi.channelAddress(3) == 103);
    QVERIFY(fxi.channelAddress(4) == 104);
    QVERIFY(fxi.channelAddress(5) == QLCChannel::invalid());
    QVERIFY(fxi.channelAddress(20) == QLCChannel::invalid());
}

void Fixture_Test::lessThan()
{
    Fixture fxi1(this);
    Fixture fxi2(this);

    QVERIFY(!(fxi1 < fxi2));
    QVERIFY(!(fxi2 < fxi1));

    fxi1.setAddress(0);
    fxi2.setAddress(1);
    QVERIFY(fxi1 < fxi2);
    QVERIFY(!(fxi2 < fxi1));

    fxi1.setAddress(511);
    fxi2.setAddress(42);
    QVERIFY(fxi2 < fxi1);
    QVERIFY(!(fxi1 < fxi2));
}

void Fixture_Test::type()
{
    Fixture fxi(this);
    QCOMPARE(fxi.typeString(), QString(KXMLFixtureDimmer));
    QCOMPARE(fxi.type(), QLCFixtureDef::Dimmer);
    QCOMPARE(fxi.iconResource(), QString(":/dimmer.png"));

    QLCFixtureDef* fixtureDef;
    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    QVERIFY(fixtureDef != NULL);

    QLCFixtureMode* fixtureMode;
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::MovingHead);
    QCOMPARE(fxi.iconResource(), QString(":/movinghead.png"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("SGM", "Colorlab 250");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::ColorChanger);
    QCOMPARE(fxi.iconResource(), QString(":/fixture.png"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Chauvet", "Vue 3.1");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::Effect);
    QCOMPARE(fxi.iconResource(), QString(":/effect.png"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Cameo", "Storm");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::Flower);
    QCOMPARE(fxi.iconResource(), QString(":/flower.png"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Showtec", "Dragon F-350");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::Hazer);
    QCOMPARE(fxi.iconResource(true), QString("qrc:/hazer.svg"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("beamZ", "LS-3DRG");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::Laser);
    QCOMPARE(fxi.iconResource(), QString(":/laser.png"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("GLP", "PocketScan");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::Scanner);
    QCOMPARE(fxi.iconResource(), QString(":/scanner.png"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Robe", "Fog 1500 FT");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::Smoke);
    QCOMPARE(fxi.iconResource(true), QString("qrc:/smoke.svg"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Chauvet", "LED Shadow");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::Strobe);
    QCOMPARE(fxi.iconResource(), QString(":/strobe.png"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Varytec", "Gigabar II");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::LEDBarPixels);
    QCOMPARE(fxi.iconResource(), QString(":/ledbar_pixels.png"));

    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Clay Paky", "Show Batten 100");
    QVERIFY(fixtureDef != NULL);
    fixtureMode = fixtureDef->modes().at(0);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QCOMPARE(fxi.typeString(), fixtureDef->typeToString(fixtureDef->type()));
    QCOMPARE(fxi.type(), QLCFixtureDef::LEDBarBeams);
    QCOMPARE(fxi.iconResource(true), QString("qrc:/ledbar_beams.svg"));
}

void Fixture_Test::dimmer()
{
    Fixture fxi(this);

    QVERIFY(fxi.fixtureDef() == NULL);
    QVERIFY(fxi.fixtureMode() == NULL);
    QVERIFY(fxi.channels() == 0);
    QVERIFY(fxi.channel(0) == NULL);
    QVERIFY(fxi.channel(42) == NULL);

    /* All channels point to the same generic channel instance */
    fxi.setChannels(5);
    QVERIFY(fxi.channels() == 5);
    QVERIFY(fxi.channel(0) != NULL);
    const QLCChannel* ch = fxi.channel(0);
    QVERIFY(fxi.channel(1) != fxi.channel(0));
    QVERIFY(fxi.channel(2) != fxi.channel(1));
    QVERIFY(fxi.channel(3) != fxi.channel(2));
    QVERIFY(fxi.channel(4) != fxi.channel(3));
    QVERIFY(fxi.channel(5) == NULL);
    QVERIFY(fxi.channel(42) == NULL);
    QVERIFY(fxi.channel(QLCChannel::invalid()) == NULL);

    QVERIFY(ch->capabilities().count() == 1);
    QVERIFY(ch->capabilities().at(0)->min() == 0);
    QVERIFY(ch->capabilities().at(0)->max() == UCHAR_MAX);
    QVERIFY(ch->capabilities().at(0)->name() == "Intensity");

    /* Although the dimmer fixture HAS a channel with this name, it is
       not returned, because all channels have the same name. */
    QVERIFY(fxi.channel(QLCChannel::Intensity) == 0);
}

void Fixture_Test::rgbPanel()
{
    Fixture fxi(this);
    fxi.setName("RGB Panel");
    QLCFixtureDef *rowDef = fxi.genericRGBPanelDef(10, Fixture::RGBW, false);
    QLCFixtureMode *rowMode = fxi.genericRGBPanelMode(rowDef, Fixture::RGBW, false, 1000, 100);
    fxi.setFixtureDefinition(rowDef, rowMode);

    QVERIFY(fxi.channels() == 40);

    QVERIFY(fxi.channel(0)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(0)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(0)->name() == "Red 1");
    QVERIFY(fxi.channel(1)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(1)->colour() == QLCChannel::Green);
    QVERIFY(fxi.channel(1)->name() == "Green 1");
    QVERIFY(fxi.channel(2)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(2)->colour() == QLCChannel::Blue);
    QVERIFY(fxi.channel(2)->name() == "Blue 1");
    QVERIFY(fxi.channel(3)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(3)->colour() == QLCChannel::White);
    QVERIFY(fxi.channel(3)->name() == "White 1");

    QVERIFY(fxi.fixtureMode()->name() == "RGBW");
    QVERIFY(fxi.fixtureMode()->channels().count() == 40);
    QVERIFY(fxi.fixtureMode()->physical().width() == 1000);
    QVERIFY(fxi.fixtureMode()->physical().height() == 100);
    QVERIFY(fxi.fixtureMode()->physical().depth() == 100);
    QVERIFY(fxi.fixtureMode()->heads().count() == 10);

    rowDef = fxi.genericRGBPanelDef(10, Fixture::RGB, false);
    rowMode = fxi.genericRGBPanelMode(rowDef, Fixture::RGB, false, 1000, 100);
    fxi.setFixtureDefinition(rowDef, rowMode);

    QVERIFY(fxi.channels() == 30);
    QVERIFY(fxi.fixtureMode()->name() == "RGB");
    QVERIFY(fxi.fixtureMode()->channels().count() == 30);

    QVERIFY(fxi.channel(0)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(0)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(0)->name() == "Red 1");
    QVERIFY(fxi.channel(1)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(1)->colour() == QLCChannel::Green);
    QVERIFY(fxi.channel(1)->name() == "Green 1");
    QVERIFY(fxi.channel(2)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(2)->colour() == QLCChannel::Blue);
    QVERIFY(fxi.channel(2)->name() == "Blue 1");

    rowDef = fxi.genericRGBPanelDef(10, Fixture::RBG, false);
    rowMode = fxi.genericRGBPanelMode(rowDef, Fixture::RBG, false, 1000, 100);
    fxi.setFixtureDefinition(rowDef, rowMode);

    QVERIFY(fxi.channels() == 30);
    QVERIFY(fxi.fixtureMode()->name() == "RBG");
    QVERIFY(fxi.fixtureMode()->channels().count() == 30);

    QVERIFY(fxi.channel(0)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(0)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(0)->name() == "Red 1");
    QVERIFY(fxi.channel(1)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(1)->colour() == QLCChannel::Blue);
    QVERIFY(fxi.channel(1)->name() == "Blue 1");
    QVERIFY(fxi.channel(2)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(2)->colour() == QLCChannel::Green);
    QVERIFY(fxi.channel(2)->name() == "Green 1");

    rowDef = fxi.genericRGBPanelDef(10, Fixture::BGR, false);
    rowMode = fxi.genericRGBPanelMode(rowDef, Fixture::BGR, false, 1000, 100);
    fxi.setFixtureDefinition(rowDef, rowMode);

    QVERIFY(fxi.channels() == 30);
    QVERIFY(fxi.fixtureMode()->name() == "BGR");
    QVERIFY(fxi.fixtureMode()->channels().count() == 30);

    QVERIFY(fxi.channel(0)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(0)->colour() == QLCChannel::Blue);
    QVERIFY(fxi.channel(0)->name() == "Blue 1");
    QVERIFY(fxi.channel(1)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(1)->colour() == QLCChannel::Green);
    QVERIFY(fxi.channel(1)->name() == "Green 1");
    QVERIFY(fxi.channel(2)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(2)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(2)->name() == "Red 1");

    rowDef = fxi.genericRGBPanelDef(10, Fixture::BRG, false);
    rowMode = fxi.genericRGBPanelMode(rowDef, Fixture::BRG, false, 1000, 100);
    fxi.setFixtureDefinition(rowDef, rowMode);

    QVERIFY(fxi.channels() == 30);
    QVERIFY(fxi.fixtureMode()->name() == "BRG");
    QVERIFY(fxi.fixtureMode()->channels().count() == 30);

    QVERIFY(fxi.channel(0)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(0)->colour() == QLCChannel::Blue);
    QVERIFY(fxi.channel(0)->name() == "Blue 1");
    QVERIFY(fxi.channel(1)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(1)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(1)->name() == "Red 1");
    QVERIFY(fxi.channel(2)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(2)->colour() == QLCChannel::Green);
    QVERIFY(fxi.channel(2)->name() == "Green 1");

    rowDef = fxi.genericRGBPanelDef(10, Fixture::GBR, false);
    rowMode = fxi.genericRGBPanelMode(rowDef, Fixture::GBR, false, 1000, 100);
    fxi.setFixtureDefinition(rowDef, rowMode);

    QVERIFY(fxi.channels() == 30);
    QVERIFY(fxi.fixtureMode()->name() == "GBR");
    QVERIFY(fxi.fixtureMode()->channels().count() == 30);

    QVERIFY(fxi.channel(0)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(0)->colour() == QLCChannel::Green);
    QVERIFY(fxi.channel(0)->name() == "Green 1");
    QVERIFY(fxi.channel(1)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(1)->colour() == QLCChannel::Blue);
    QVERIFY(fxi.channel(1)->name() == "Blue 1");
    QVERIFY(fxi.channel(2)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(2)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(2)->name() == "Red 1");

    rowDef = fxi.genericRGBPanelDef(10, Fixture::GRB, false);
    rowMode = fxi.genericRGBPanelMode(rowDef, Fixture::GRB, false, 1000, 100);
    fxi.setFixtureDefinition(rowDef, rowMode);

    QVERIFY(fxi.channels() == 30);
    QVERIFY(fxi.fixtureMode()->name() == "GRB");
    QVERIFY(fxi.fixtureMode()->channels().count() == 30);

    QVERIFY(fxi.channel(0)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(0)->colour() == QLCChannel::Green);
    QVERIFY(fxi.channel(0)->name() == "Green 1");
    QVERIFY(fxi.channel(1)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(1)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(1)->name() == "Red 1");
    QVERIFY(fxi.channel(2)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(2)->colour() == QLCChannel::Blue);
    QVERIFY(fxi.channel(2)->name() == "Blue 1");
}

void Fixture_Test::rgbPanel16bit()
{
    Fixture fxi(this);
    fxi.setName("RGB Panel 16bit");
    QLCFixtureDef *rowDef = fxi.genericRGBPanelDef(10, Fixture::RGB, true);
    QLCFixtureMode *rowMode = fxi.genericRGBPanelMode(rowDef, Fixture::RGB, true, 1000, 100);
    fxi.setFixtureDefinition(rowDef, rowMode);

    QVERIFY(fxi.channels() == 60);
    QVERIFY(fxi.fixtureMode()->name() == "RGB 16bit");
    QVERIFY(fxi.fixtureMode()->heads().at(0).channels().count() == 6);

    QVERIFY(fxi.channel(0)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(0)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(0)->name() == "Red 1");
    QVERIFY(fxi.channel(0)->controlByte() == QLCChannel::MSB);

    QVERIFY(fxi.channel(1)->group() == QLCChannel::Intensity);
    QVERIFY(fxi.channel(1)->colour() == QLCChannel::Red);
    QVERIFY(fxi.channel(1)->name() == "Red Fine 1");
    QVERIFY(fxi.channel(1)->controlByte() == QLCChannel::LSB);
}

void Fixture_Test::fixtureDef()
{
    Fixture fxi(this);

    QVERIFY(fxi.fixtureDef() == NULL);
    QVERIFY(fxi.fixtureMode() == NULL);
    QVERIFY(fxi.channels() == 0);
    QVERIFY(fxi.channel(0) == NULL);
    QCOMPARE(fxi.channelNumber(QLCChannel::Pan, QLCChannel::MSB), QLCChannel::invalid());
    QCOMPARE(fxi.channelNumber(QLCChannel::Tilt, QLCChannel::MSB), QLCChannel::invalid());
    QCOMPARE(fxi.channelNumber(QLCChannel::Pan, QLCChannel::LSB), QLCChannel::invalid());
    QCOMPARE(fxi.channelNumber(QLCChannel::Tilt, QLCChannel::LSB), QLCChannel::invalid());
    QCOMPARE(fxi.masterIntensityChannel(), QLCChannel::invalid());

    QLCFixtureDef* fixtureDef;
    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC300");
    Q_ASSERT(fixtureDef != NULL);

    fxi.setFixtureDefinition(fixtureDef, NULL);
    QVERIFY(fxi.fixtureDef() == NULL);
    QVERIFY(fxi.fixtureMode() == NULL);

    QLCFixtureMode* fixtureMode;
    fixtureMode = fixtureDef->modes().last();
    Q_ASSERT(fixtureMode != NULL);

    fxi.setFixtureDefinition(NULL, fixtureMode);
    QVERIFY(fxi.fixtureDef() == NULL);
    QVERIFY(fxi.fixtureMode() == NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    QVERIFY(fxi.fixtureDef() != NULL);
    QVERIFY(fxi.fixtureMode() != NULL);
    QVERIFY(fxi.fixtureDef() == fixtureDef);
    QVERIFY(fxi.fixtureMode() == fixtureMode);

    QVERIFY(fxi.channels() == quint32(fixtureMode->channels().count()));
    QVERIFY(fxi.channel(fxi.channels() - 1) != NULL);
    QVERIFY(fxi.channel(fxi.channels()) == NULL);

    QVERIFY(fxi.channel(QLCChannel::Pan) != QLCChannel::invalid());
    const QLCChannel* ch = fxi.channel(fxi.channel(QLCChannel::Pan));
    QVERIFY(ch != NULL);

    QCOMPARE(fxi.channelNumber(QLCChannel::Pan, QLCChannel::MSB), quint32(7));
    QCOMPARE(fxi.channelNumber(QLCChannel::Tilt, QLCChannel::MSB), quint32(9));
    QCOMPARE(fxi.channelNumber(QLCChannel::Pan, QLCChannel::LSB), quint32(8));
    QCOMPARE(fxi.channelNumber(QLCChannel::Tilt, QLCChannel::LSB), quint32(10));
    QCOMPARE(fxi.masterIntensityChannel(), quint32(1));
    QCOMPARE(fxi.rgbChannels(), QVector <quint32> ());
    QCOMPARE(fxi.cmyChannels(), QVector <quint32> () << 2 << 3 << 4);
}

void Fixture_Test::channels()
{
    Fixture fxi(this);
    QLCFixtureDef* fixtureDef = m_doc->fixtureDefCache()->fixtureDef("i-Pix", "BB4");
    QVERIFY(fixtureDef != NULL);
    QLCFixtureMode* fixtureMode = fixtureDef->modes().last();
    QVERIFY(fixtureMode != NULL);
    fxi.setFixtureDefinition(fixtureDef, fixtureMode);

    QCOMPARE(fxi.channel(QLCChannel::Intensity, QLCChannel::Red), quint32(3));

    QSet <quint32> chs;
    chs << 3 << 4 << 21 << 22 << 12 << 13 << 30 << 31;
    QCOMPARE(chs, fxi.channels(QLCChannel::Intensity, QLCChannel::Red));
    chs.clear();
    chs << 5 << 6 << 23 << 24 << 14 << 15 << 32 << 33;
    QCOMPARE(chs, fxi.channels(QLCChannel::Intensity, QLCChannel::Green));
    chs.clear();
    chs << 7 << 8 << 16 << 17 << 25 << 26 << 34 << 35;
    QCOMPARE(chs, fxi.channels(QLCChannel::Intensity, QLCChannel::Blue));
    chs.clear();
    QCOMPARE(chs, fxi.channels(QLCChannel::Colour, QLCChannel::Blue));
}

void Fixture_Test::degrees()
{
    Fixture fxi(this);

    QLCFixtureDef* fixtureDef;
    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    QVERIFY(fixtureDef != NULL);

    QLCFixtureMode* fixtureMode;
    fixtureMode = fixtureDef->modes().at(1);
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);

    QCOMPARE(fxi.degreesRange(0).width(), 540.0);
    QCOMPARE(fxi.degreesRange(0).height(), 270.0);

    // dummy ID just for testing
    fxi.setID(99);

    QList<SceneValue> pos = fxi.positionToValues(QLCChannel::Pan, 90);
    QCOMPARE(pos.count(), 2);
    // verify coarse Pan
    QCOMPARE(pos.at(0).fxi, quint32(99));
    QCOMPARE(pos.at(0).channel, quint32(7));
    QCOMPARE(pos.at(0).value, uchar(42));
    // verify fine Pan
    QCOMPARE(pos.at(1).fxi, quint32(99));
    QCOMPARE(pos.at(1).channel, quint32(8));
    QCOMPARE(pos.at(1).value, uchar(170));

    pos = fxi.positionToValues(QLCChannel::Tilt, 45);
    QCOMPARE(pos.count(), 2);
    // verify coarse Tilt
    QCOMPARE(pos.at(0).fxi, quint32(99));
    QCOMPARE(pos.at(0).channel, quint32(9));
    QCOMPARE(pos.at(0).value, uchar(42));
    // verify fine Tilt
    QCOMPARE(pos.at(1).fxi, quint32(99));
    QCOMPARE(pos.at(1).channel, quint32(10));
    QCOMPARE(pos.at(1).value, uchar(170));
}

void Fixture_Test::heads()
{
    Fixture fxi(this);

    QLCFixtureDef* fixtureDef;
    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Equinox", "Photon");
    QVERIFY(fixtureDef != NULL);

    QLCFixtureMode* fixtureMode;
    fixtureMode = fixtureDef->modes().last();
    QVERIFY(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);

    QCOMPARE(fxi.heads(), 6);

    QLCFixtureHead head = fxi.head(0);
    QCOMPARE(head.channels().count(), 4);
    head = fxi.head(1);
    QCOMPARE(head.channels().count(), 4);
    head = fxi.head(2);
    QCOMPARE(head.channels().count(), 4);
    head = fxi.head(3);
    QCOMPARE(head.channels().count(), 4);
    head = fxi.head(4);
    QCOMPARE(head.channels().count(), 4);
    head = fxi.head(5);
    QCOMPARE(head.channels().count(), 4);
}

void Fixture_Test::loadWrongRoot()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Function");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Fixture fxi(this);
    QVERIFY(fxi.loadXML(xmlReader, m_doc, m_doc->fixtureDefCache()) == false);
}

void Fixture_Test::loadFixtureDef()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("Channels", "9");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Universe", "0");
    xmlWriter.writeTextElement("Model", "MAC250+");
    xmlWriter.writeTextElement("Mode", "Mode 1");
    xmlWriter.writeTextElement("Manufacturer", "Martin");
    xmlWriter.writeTextElement("ID", "42");
    xmlWriter.writeTextElement("Address", "21");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Fixture fxi(this);
    QVERIFY(fxi.loadXML(xmlReader, m_doc, m_doc->fixtureDefCache()) == true);
    QVERIFY(fxi.name() == "Foobar");
    QVERIFY(fxi.channels() == 9);
    QVERIFY(fxi.address() == 21);
    QVERIFY(fxi.universe() == 0);
    QVERIFY(fxi.fixtureDef() != NULL);
    QVERIFY(fxi.fixtureMode() != NULL);
}

void Fixture_Test::loadFixtureDefWrongChannels()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("Channels", "15");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Universe", "0");
    xmlWriter.writeTextElement("Model", "MAC250+");
    xmlWriter.writeTextElement("Mode", "Mode 1");
    xmlWriter.writeTextElement("Manufacturer", "Martin");
    xmlWriter.writeTextElement("ID", "42");
    xmlWriter.writeTextElement("Address", "21");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Fixture fxi(this);
    QVERIFY(fxi.loadXML(xmlReader, m_doc, m_doc->fixtureDefCache()) == true);
    QVERIFY(fxi.name() == "Foobar");
    QVERIFY(fxi.channels() == 9);
    QVERIFY(fxi.address() == 21);
    QVERIFY(fxi.universe() == 0);
    QVERIFY(fxi.fixtureDef() != NULL);
    QVERIFY(fxi.fixtureMode() != NULL);
}

void Fixture_Test::loadDimmer()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("Channels", "18");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Universe", "3");
    xmlWriter.writeTextElement("Model", "Foobar");
    xmlWriter.writeTextElement("Mode", "Foobar");
    xmlWriter.writeTextElement("Manufacturer", "Foobar");
    xmlWriter.writeTextElement("ID", "42");
    xmlWriter.writeTextElement("Address", "21");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Fixture fxi(this);
    QVERIFY(fxi.loadXML(xmlReader, m_doc, m_doc->fixtureDefCache()) == true);
    QVERIFY(fxi.name() == "Foobar");
    QVERIFY(fxi.channels() == 18);
    QVERIFY(fxi.address() == 21);
    QVERIFY(fxi.universe() == 3);
    QVERIFY(fxi.fixtureDef() != NULL);
    QVERIFY(fxi.fixtureMode() != NULL);
}

void Fixture_Test::loadWrongAddress()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("Channels", "18");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Universe", "0");
    xmlWriter.writeTextElement("Model", "Foobar");
    xmlWriter.writeTextElement("Mode", "Foobar");
    xmlWriter.writeTextElement("Manufacturer", "Foobar");
    xmlWriter.writeTextElement("ID", "42");
    xmlWriter.writeTextElement("Address", "512");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Fixture fxi(this);
    QVERIFY(fxi.loadXML(xmlReader, m_doc, m_doc->fixtureDefCache()) == true);
    QVERIFY(fxi.name() == "Foobar");
    QVERIFY(fxi.channels() == 18);
    QVERIFY(fxi.address() == 0);
    QVERIFY(fxi.universe() == 0);
}

void Fixture_Test::loadWrongUniverse()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("Channels", "18");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Universe", "4");
    xmlWriter.writeTextElement("Model", "Foobar");
    xmlWriter.writeTextElement("Mode", "Foobar");
    xmlWriter.writeTextElement("Manufacturer", "Foobar");
    xmlWriter.writeTextElement("ID", "42");
    xmlWriter.writeTextElement("Address", "25");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Fixture fxi(this);
    QVERIFY(fxi.loadXML(xmlReader, m_doc, m_doc->fixtureDefCache()) == true);
    QVERIFY(fxi.name() == "Foobar");
    QVERIFY(fxi.channels() == 18);
    QVERIFY(fxi.address() == 25);
    QVERIFY(fxi.universe() == 4);
}

void Fixture_Test::loadWrongID()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("Channels", "9");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Universe", "0");
    xmlWriter.writeTextElement("Model", "MAC250+");
    xmlWriter.writeTextElement("Mode", "Mode 1");
    xmlWriter.writeTextElement("Manufacturer", "Martin");
    xmlWriter.writeTextElement("ID", QString::number(Fixture::invalidId()));
    xmlWriter.writeTextElement("Address", "21");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Fixture fxi(this);
    QVERIFY(fxi.loadXML(xmlReader, m_doc, m_doc->fixtureDefCache()) == false);
}

void Fixture_Test::loader()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Fixture");

    xmlWriter.writeTextElement("Channels", "18");
    xmlWriter.writeTextElement("Name", "Foobar");
    xmlWriter.writeTextElement("Universe", "3");
    xmlWriter.writeTextElement("Model", "Foobar");
    xmlWriter.writeTextElement("Mode", "Foobar");
    xmlWriter.writeTextElement("Manufacturer", "Foobar");
    xmlWriter.writeTextElement("ID", "42");
    xmlWriter.writeTextElement("Address", "21");

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(m_doc != NULL);
    QVERIFY(m_doc->fixtures().size() == 0);

    QVERIFY(Fixture::loader(xmlReader, m_doc) == true);
    QVERIFY(m_doc->fixtures().size() == 1);
    QVERIFY(m_doc->fixture(0) == NULL); // No ID auto-assignment

    Fixture* fxi = m_doc->fixture(42);
    QVERIFY(fxi != NULL);
    QVERIFY(fxi->name() == "Foobar");
    QVERIFY(fxi->channels() == 18);
    QVERIFY(fxi->address() == 21);
    QVERIFY(fxi->universe() == 3);
    QVERIFY(fxi->fixtureDef() != NULL);
    QVERIFY(fxi->fixtureMode() != NULL);
}

void Fixture_Test::save()
{
    QLCFixtureDef* fixtureDef;
    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    Q_ASSERT(fixtureDef != NULL);

    QLCFixtureMode* fixtureMode;
    fixtureMode = fixtureDef->modes().at(0);
    Q_ASSERT(fixtureMode != NULL);

    Fixture fxi(this);
    fxi.setID(1337);
    fxi.setName("Test Fixture");
    fxi.setUniverse(2);
    fxi.setAddress(438);
    fxi.setFixtureDefinition(fixtureDef, fixtureMode);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("TestRoot");

    QVERIFY(fxi.saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "TestRoot");
    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Fixture");

    bool manufacturer = false, model = false, mode = false, name = false,
                                       channels = false, universe = false, address = false, id = false;

    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name().toString() == "Manufacturer")
        {
            QVERIFY(xmlReader.readElementText() == "Martin");
            manufacturer = true;
        }
        else if (xmlReader.name().toString() == "Model")
        {
            QVERIFY(xmlReader.readElementText() == "MAC250+");
            model = true;
        }
        else if (xmlReader.name().toString() == "Mode")
        {
            QVERIFY(xmlReader.readElementText() == fixtureMode->name());
            mode = true;
        }
        else if (xmlReader.name().toString() == "ID")
        {
            QVERIFY(xmlReader.readElementText() == "1337");
            id = true;
        }
        else if (xmlReader.name().toString() == "Name")
        {
            QVERIFY(xmlReader.readElementText() == "Test Fixture");
            name = true;
        }
        else if (xmlReader.name().toString() == "Universe")
        {
            QVERIFY(xmlReader.readElementText() == "2");
            universe = true;
        }
        else if (xmlReader.name().toString() == "Address")
        {
            QVERIFY(xmlReader.readElementText() == "438");
            address = true;
        }
        else if (xmlReader.name().toString() == "Channels")
        {
            QVERIFY(xmlReader.readElementText().toInt()
                    == fixtureMode->channels().count());
            channels = true;
        }
        else
        {
            QFAIL(QString("Unexpected tag: %1").arg(xmlReader.name().toString())
                  .toLatin1());
            xmlReader.skipCurrentElement();
        }
    }

    QVERIFY(manufacturer == true);
    QVERIFY(model == true);
    QVERIFY(mode == true);
    QVERIFY(id == true);
    QVERIFY(name == true);
    QVERIFY(universe == true);
    QVERIFY(address == true);
    QVERIFY(channels == true);
}

/*void Fixture_Test::status()
{
    // This test is mostly just a stability check since checking lots of
    // detailed HTML formatting is not that useful.
    QString info;

    Fixture fxi(this);
    info = fxi.status();

    fxi.setID(1337);
    info = fxi.status();

    fxi.setName("Test Fixture");
    info = fxi.status();

    fxi.setUniverse(2);
    info = fxi.status();

    fxi.setAddress(438);
    info = fxi.status();

    fxi.setChannels(12);
    info = fxi.status();

    QLCFixtureDef* fixtureDef;
    fixtureDef = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    Q_ASSERT(fixtureDef != NULL);

    QLCFixtureMode* fixtureMode;
    fixtureMode = fixtureDef->modes().at(0);
    Q_ASSERT(fixtureMode != NULL);

    fxi.setFixtureDefinition(fixtureDef, fixtureMode);
    info = fxi.status();
}*/

/*****************************************************************************
 * Helpers for the in-memory definitions used below. Fixture does not own a
 * non-generic definition, so these are deleted by the test after the Fixture
 * has been detached from them (setFixtureDefinition(NULL, NULL)).
 *****************************************************************************/

static QLCChannel *makeChannel(QLCFixtureDef *def, const QString &name, QLCChannel::Group group,
                               QLCChannel::Preset preset = QLCChannel::Custom,
                               QLCChannel::ControlByte cb = QLCChannel::MSB)
{
    QLCChannel *ch = new QLCChannel();
    ch->setName(name);
    ch->setGroup(group);
    ch->setControlByte(cb);
    if (preset != QLCChannel::Custom)
        ch->setPreset(preset);
    def->addChannel(ch);
    return ch;
}

static QLCFixtureMode *makeMode(QLCFixtureDef *def, const QString &name, const QList<QLCChannel *> &channels)
{
    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName(name);
    for (int i = 0; i < channels.count(); i++)
        mode->insertChannel(channels.at(i), i);
    def->addMode(mode);
    return mode;
}

static QByteArray writeFixture(const Fixture &fxi)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    xmlWriter.writeStartElement("TestRoot");
    fxi.saveXML(&xmlWriter);
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();
    return buffer.data();
}

static bool readFixture(const QByteArray &xml, Fixture &fxi, Doc *doc)
{
    QBuffer buffer;
    buffer.setData(xml);
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement(); // TestRoot
    xmlReader.readNextStartElement(); // Fixture
    return fxi.loadXML(xmlReader, doc, doc->fixtureDefCache());
}

/*****************************************************************************
 * Universe / channel bookkeeping
 *****************************************************************************/

void Fixture_Test::crossUniverse()
{
    Fixture fxi(this);
    QCOMPARE(fxi.crossUniverse(), false);
    fxi.setCrossUniverse(true);
    QCOMPARE(fxi.crossUniverse(), true);
    fxi.setCrossUniverse(false);
    QCOMPARE(fxi.crossUniverse(), false);
}

void Fixture_Test::setChannelsReplacesGenericDef()
{
    Fixture fxi(this);
    fxi.setChannels(3);
    QLCFixtureDef *firstDef = fxi.fixtureDef();
    QVERIFY(firstDef != NULL);
    QCOMPARE(fxi.channels(), quint32(3));
    QCOMPARE(fxi.fixtureMode()->name(), QString("3 Channel"));

    // same count: the generic definition is kept as is
    fxi.setChannels(3);
    QCOMPARE(fxi.fixtureDef(), firstDef);

    // different count: a new generic definition/mode replaces the old one
    fxi.setChannels(5);
    QVERIFY(fxi.fixtureDef() != NULL);
    QCOMPARE(fxi.channels(), quint32(5));
    QCOMPARE(fxi.fixtureMode()->name(), QString("5 Channel"));
    QCOMPARE(fxi.fixtureMode()->heads().count(), 5);
    QCOMPARE(fxi.channelValues().size(), 5);
}

void Fixture_Test::channelLookupMisses()
{
    Fixture bare(this);
    // no definition at all: any group lookup is invalid
    QCOMPARE(bare.channel(QLCChannel::Pan), QLCChannel::invalid());
    QCOMPARE(bare.channel(QLCChannel::Intensity, QLCChannel::Red), QLCChannel::invalid());
    QVERIFY(bare.channels(QLCChannel::Pan).isEmpty());

    Fixture fxi(this);
    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    QVERIFY(def != NULL);
    QLCFixtureMode *mode = def->modes().at(1);
    QVERIFY(mode != NULL);
    fxi.setFixtureDefinition(def, mode);

    // MAC250+ has no red intensity channel
    QCOMPARE(fxi.channel(QLCChannel::Intensity, QLCChannel::Red), QLCChannel::invalid());

    // head index out of range on every head-based lookup
    QCOMPARE(fxi.heads(), 1);
    QCOMPARE(fxi.channelNumber(QLCChannel::Pan, QLCChannel::MSB, 5), QLCChannel::invalid());
    QCOMPARE(fxi.channelNumber(QLCChannel::Pan, QLCChannel::MSB, -1), QLCChannel::invalid());
    QCOMPARE(fxi.rgbChannels(5), QVector<quint32>());
    QCOMPARE(fxi.cmyChannels(5), QVector<quint32>());
    QCOMPARE(fxi.head(5).channels().count(), 0);
}

/*****************************************************************************
 * Position / axis / zoom helpers
 *****************************************************************************/

void Fixture_Test::positionNoMovement()
{
    Fixture bare(this);
    QVERIFY(bare.positionToValues(QLCChannel::Pan, 90).isEmpty());
    QVERIFY(bare.positionToValues(QLCChannel::Tilt, 90).isEmpty());
    QVERIFY(bare.axisValueToValues(QLCChannel::Pan, 100).isEmpty());
    QVERIFY(bare.zoomToValues(10, false).isEmpty());
    QCOMPARE(bare.degreesRange(0), QRectF());

    Fixture dimmer(this);
    dimmer.setChannels(4);
    // a generic dimmer has heads but no pan/tilt/zoom: nothing to produce
    QVERIFY(dimmer.positionToValues(QLCChannel::Pan, 90).isEmpty());
    QVERIFY(dimmer.positionToValues(QLCChannel::Tilt, 45).isEmpty());
    QVERIFY(dimmer.axisValueToValues(QLCChannel::Tilt, 100).isEmpty());
    QVERIFY(dimmer.zoomToValues(10, false).isEmpty());
    // ...and no pan/tilt range either
    QCOMPARE(dimmer.degreesRange(0), QRectF());
}

void Fixture_Test::positionRelative()
{
    Fixture fxi(this);
    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    QVERIFY(def != NULL);
    QLCFixtureMode *mode = def->modes().at(1); // 16 bit pan/tilt, 540/270 degrees
    QVERIFY(mode != NULL);
    fxi.setFixtureDefinition(def, mode);
    fxi.setID(5);
    fxi.setAddress(0);

    // current values: pan MSB 128 (= 270 degrees), tilt MSB 64 (= 67.5 degrees)
    QByteArray values(16, 0);
    values[7] = char(128);
    values[9] = char(64);
    QVERIFY(fxi.setChannelValues(values) == true);
    QCOMPARE(fxi.channelValueAt(7), uchar(128));

    // +90 degrees relative pan: 360 degrees -> 0xAAAA
    QList<SceneValue> pos = fxi.positionToValues(QLCChannel::Pan, 90, true);
    QCOMPARE(pos.count(), 2);
    QCOMPARE(pos.at(0).fxi, quint32(5));
    QCOMPARE(pos.at(0).channel, quint32(7));
    QCOMPARE(pos.at(0).value, uchar(0xAA));
    QCOMPARE(pos.at(1).channel, quint32(8));
    QCOMPARE(pos.at(1).value, uchar(0xAA));

    // relative pan beyond the range is clamped to the maximum
    pos = fxi.positionToValues(QLCChannel::Pan, 500, true);
    QCOMPARE(pos.count(), 2);
    QCOMPARE(pos.at(0).value, uchar(0xFF));
    QCOMPARE(pos.at(1).value, uchar(0xFF));

    // -20 degrees relative tilt: 47.5 degrees -> 11529 = 0x2D09
    pos = fxi.positionToValues(QLCChannel::Tilt, -20, true);
    QCOMPARE(pos.count(), 2);
    QCOMPARE(pos.at(0).channel, quint32(9));
    QCOMPARE(pos.at(0).value, uchar(0x2D));
    QCOMPARE(pos.at(1).channel, quint32(10));
    QCOMPARE(pos.at(1).value, uchar(0x09));

    // relative tilt below zero is clamped to zero
    pos = fxi.positionToValues(QLCChannel::Tilt, -200, true);
    QCOMPARE(pos.count(), 2);
    QCOMPARE(pos.at(0).value, uchar(0));
    QCOMPARE(pos.at(1).value, uchar(0));
}

void Fixture_Test::axisValues()
{
    Fixture fxi(this);
    QLCFixtureDef *def = m_doc->fixtureDefCache()->fixtureDef("Martin", "MAC250+");
    QVERIFY(def != NULL);
    QLCFixtureMode *mode = def->modes().at(1);
    QVERIFY(mode != NULL);
    fxi.setFixtureDefinition(def, mode);
    fxi.setID(6);

    QList<SceneValue> vals = fxi.axisValueToValues(QLCChannel::Pan, 0x1234);
    QCOMPARE(vals.count(), 2);
    QCOMPARE(vals.at(0).fxi, quint32(6));
    QCOMPARE(vals.at(0).channel, quint32(7));
    QCOMPARE(vals.at(0).value, uchar(0x12));
    QCOMPARE(vals.at(1).channel, quint32(8));
    QCOMPARE(vals.at(1).value, uchar(0x34));

    // raw values are clamped to the 16 bit range
    vals = fxi.axisValueToValues(QLCChannel::Tilt, 70000);
    QCOMPARE(vals.count(), 2);
    QCOMPARE(vals.at(0).channel, quint32(9));
    QCOMPARE(vals.at(0).value, uchar(0xFF));
    QCOMPARE(vals.at(1).channel, quint32(10));
    QCOMPARE(vals.at(1).value, uchar(0xFF));

    vals = fxi.axisValueToValues(QLCChannel::Tilt, -5);
    QCOMPARE(vals.count(), 2);
    QCOMPARE(vals.at(0).value, uchar(0));
    QCOMPARE(vals.at(1).value, uchar(0));

    // an axis this fixture does not have
    QVERIFY(fxi.axisValueToValues(QLCChannel::Speed, 100).isEmpty());
}

void Fixture_Test::zoom()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("Zoom");
    QList<QLCChannel *> chList;
    chList << makeChannel(def, "Dimmer", QLCChannel::Intensity);
    chList << makeChannel(def, "Zoom", QLCChannel::Beam, QLCChannel::BeamZoomSmallBig);
    chList << makeChannel(def, "Zoom Fine", QLCChannel::Beam, QLCChannel::BeamZoomFine, QLCChannel::LSB);
    chList << makeChannel(def, "Zoom Inverted", QLCChannel::Beam, QLCChannel::BeamZoomBigSmall);
    chList << makeChannel(def, "Focus", QLCChannel::Beam, QLCChannel::BeamFocusNearFar);
    QLCFixtureMode *mode = makeMode(def, "Zoom", chList);

    QLCPhysical phy;
    phy.setLensDegreesMin(10);
    phy.setLensDegreesMax(50);
    mode->setPhysical(phy);

    Fixture fxi(this);
    fxi.setFixtureDefinition(def, mode);
    fxi.setID(8);
    fxi.setAddress(0);

    // absolute 30 degrees over a 10..50 lens: (30 - 10) / 40 -> 0x7FFF
    QList<SceneValue> vals = fxi.zoomToValues(30, false);
    QCOMPARE(vals.count(), 3);
    QCOMPARE(vals.at(0).fxi, quint32(8));
    QCOMPARE(vals.at(0).channel, quint32(1));
    QCOMPARE(vals.at(0).value, uchar(0x7F));
    QCOMPARE(vals.at(1).channel, quint32(2));
    QCOMPARE(vals.at(1).value, uchar(0xFF));
    QCOMPARE(vals.at(2).channel, quint32(3));
    QCOMPARE(vals.at(2).value, uchar(255 - 0x7F));

    // absolute values are clamped to the lens range
    vals = fxi.zoomToValues(100, false);
    QCOMPARE(vals.count(), 3);
    QCOMPARE(vals.at(0).value, uchar(0xFF));
    QCOMPARE(vals.at(1).value, uchar(0xFF));
    QCOMPARE(vals.at(2).value, uchar(0));

    // relative: current zoom MSB 64 (= 10 degrees of travel), inverted channel 191
    QByteArray values(5, 0);
    values[1] = char(64);
    values[3] = char(191);
    QVERIFY(fxi.setChannelValues(values) == true);

    vals = fxi.zoomToValues(10, true);
    QCOMPARE(vals.count(), 3);
    QCOMPARE(vals.at(0).channel, quint32(1));
    QCOMPARE(vals.at(0).value, uchar(0x7F));
    QCOMPARE(vals.at(1).channel, quint32(2));
    QCOMPARE(vals.at(1).value, uchar(0xFE));
    QCOMPARE(vals.at(2).channel, quint32(3));
    QCOMPARE(vals.at(2).value, uchar(64));

    vals = fxi.zoomToValues(-10, true);
    QCOMPARE(vals.count(), 3);
    QCOMPARE(vals.at(0).value, uchar(0));
    QCOMPARE(vals.at(1).value, uchar(0));
    QCOMPARE(vals.at(2).value, uchar(192));

    fxi.setFixtureDefinition(NULL, NULL);
    delete def;
}

/*****************************************************************************
 * Channel values, fade/precedence lists, modifiers
 *****************************************************************************/

void Fixture_Test::channelValuesCache()
{
    Fixture fxi(this);
    fxi.setChannels(4);
    fxi.setAddress(10);
    QCOMPARE(fxi.channelValues().size(), 4);

    // buffer too short to reach the fixture's address: nothing changes
    QCOMPARE(fxi.setChannelValues(QByteArray(5, 0)), false);

    QByteArray universe(512, 0);
    universe[10] = char(100);
    universe[12] = char(50);
    QSignalSpy spy(&fxi, SIGNAL(valuesChanged()));
    QCOMPARE(fxi.setChannelValues(universe), true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(fxi.channelValueAt(0), uchar(100));
    QCOMPARE(fxi.channelValueAt(1), uchar(0));
    QCOMPARE(fxi.channelValueAt(2), uchar(50));
    QCOMPARE(fxi.channelValueAt(-1), uchar(0));
    QCOMPARE(fxi.channelValueAt(4), uchar(0));

    QByteArray cached = fxi.channelValues();
    QCOMPARE(cached.size(), 4);
    QCOMPARE(uchar(cached.at(0)), uchar(100));
    QCOMPARE(uchar(cached.at(2)), uchar(50));

    // same values again: no change reported
    QCOMPARE(fxi.setChannelValues(universe), false);
    QCOMPARE(spy.count(), 1);

    // a buffer covering only part of the fixture updates the covered channels
    QByteArray partial(12, 0);
    partial[10] = char(7);
    QCOMPARE(fxi.setChannelValues(partial), true);
    QCOMPARE(fxi.channelValueAt(0), uchar(7));
    QCOMPARE(fxi.channelValueAt(2), uchar(50));
}

void Fixture_Test::fadeAndPrecedenceLists()
{
    Fixture fxi(this);
    fxi.setChannels(4);

    // lists longer than the channel count are rejected
    QList<int> tooMany;
    tooMany << 0 << 1 << 2 << 3 << 4;
    fxi.setExcludeFadeChannels(tooMany);
    QVERIFY(fxi.excludeFadeChannels().isEmpty());
    fxi.setForcedHTPChannels(tooMany);
    QVERIFY(fxi.forcedHTPChannels().isEmpty());
    fxi.setForcedLTPChannels(tooMany);
    QVERIFY(fxi.forcedLTPChannels().isEmpty());

    fxi.setChannelCanFade(2, false);
    fxi.setChannelCanFade(0, false);
    QCOMPARE(fxi.excludeFadeChannels(), QList<int>() << 0 << 2); // kept sorted
    QCOMPARE(fxi.channelCanFade(2), false);
    QCOMPARE(fxi.channelCanFade(1), true);
    fxi.setChannelCanFade(2, false); // no duplicates
    QCOMPARE(fxi.excludeFadeChannels(), QList<int>() << 0 << 2);
    fxi.setChannelCanFade(2, true);
    QCOMPARE(fxi.excludeFadeChannels(), QList<int>() << 0);
    fxi.setChannelCanFade(3, true); // not excluded: no-op
    QCOMPARE(fxi.excludeFadeChannels(), QList<int>() << 0);

    // forced HTP and LTP lists are mutually exclusive
    fxi.setForcedHTPChannels(QList<int>() << 0 << 1);
    fxi.setForcedLTPChannels(QList<int>() << 1 << 2);
    QCOMPARE(fxi.forcedHTPChannels(), QList<int>() << 0);
    QCOMPARE(fxi.forcedLTPChannels(), QList<int>() << 1 << 2);
    fxi.setForcedHTPChannels(QList<int>() << 2);
    QCOMPARE(fxi.forcedHTPChannels(), QList<int>() << 2);
    QCOMPARE(fxi.forcedLTPChannels(), QList<int>() << 1);
}

void Fixture_Test::channelModifiers()
{
    Fixture fxi(this);
    fxi.setChannels(3);

    ChannelModifier mod;
    mod.setName("TestMod");

    QVERIFY(fxi.channelModifier(0) == NULL);
    fxi.setChannelModifier(5, &mod); // out of range: ignored
    QVERIFY(fxi.channelModifier(5) == NULL);

    fxi.setChannelModifier(1, &mod);
    QCOMPARE(fxi.channelModifier(1), &mod);
    QVERIFY(fxi.channelModifier(0) == NULL);

    fxi.setChannelModifier(1, NULL);
    QVERIFY(fxi.channelModifier(1) == NULL);
}

/*****************************************************************************
 * Icons
 *****************************************************************************/

void Fixture_Test::iconResources()
{
    Fixture fxi(this);
    QCOMPARE(fxi.iconResource(true), QString("qrc:/dimmer.svg"));

    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("Icons");
    QList<QLCChannel *> chList;
    chList << makeChannel(def, "Speed", QLCChannel::Speed);
    QLCFixtureMode *mode = makeMode(def, "Mode", chList);
    fxi.setFixtureDefinition(def, mode);

    struct IconCase { QLCFixtureDef::FixtureType type; const char *res; };
    const IconCase cases[] = {
        { QLCFixtureDef::ColorChanger, "fixture" },
        { QLCFixtureDef::Dimmer, "dimmer" },
        { QLCFixtureDef::Effect, "effect" },
        { QLCFixtureDef::Fan, "fan" },
        { QLCFixtureDef::Flower, "flower" },
        { QLCFixtureDef::Hazer, "hazer" },
        { QLCFixtureDef::Laser, "laser" },
        { QLCFixtureDef::MovingHead, "movinghead" },
        { QLCFixtureDef::Scanner, "scanner" },
        { QLCFixtureDef::Smoke, "smoke" },
        { QLCFixtureDef::Strobe, "strobe" },
        { QLCFixtureDef::LEDBarBeams, "ledbar_beams" },
        { QLCFixtureDef::LEDBarPixels, "ledbar_pixels" },
        { QLCFixtureDef::Other, "other" }
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        def->setType(cases[i].type);
        QCOMPARE(fxi.type(), cases[i].type);
        QCOMPARE(fxi.iconResource(false), QString(":/%1.png").arg(cases[i].res));
        QCOMPARE(fxi.iconResource(true), QString("qrc:/%1.svg").arg(cases[i].res));
    }

    // The icon files live in the application's resources, not in this test
    // binary, so only the resource path can be checked. Building the QIcon
    // itself needs a QGuiApplication (hence QTEST_MAIN below).
    QIcon icon = fxi.getIconFromType();
    Q_UNUSED(icon);

    fxi.setFixtureDefinition(NULL, NULL);
    delete def;
}

/*****************************************************************************
 * Capability aliases
 *****************************************************************************/

void Fixture_Test::aliasChannels()
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("Alias");

    QLCChannel *control = makeChannel(def, "Control", QLCChannel::Maintenance);
    QLCChannel *color = makeChannel(def, "Color", QLCChannel::Colour);
    QLCChannel *strobe = makeChannel(def, "Strobe", QLCChannel::Shutter);
    QLCChannel *gobo = makeChannel(def, "Gobo", QLCChannel::Gobo);

    // Aliases targeting another mode must be ignored
    AliasInfo otherModeAlias;
    otherModeAlias.targetMode = "Other";
    otherModeAlias.sourceChannel = "Color";
    otherModeAlias.targetChannel = "Strobe";

    QLCCapability *plain = new QLCCapability(0, 9, "Plain");

    QLCCapability *capB = new QLCCapability(10, 19, "Strobe mode");
    capB->setPreset(QLCCapability::Alias);
    AliasInfo bAlias;
    bAlias.targetMode = "Main";
    bAlias.sourceChannel = "Color";
    bAlias.targetChannel = "Strobe";
    capB->addAlias(bAlias);
    capB->addAlias(otherModeAlias);

    QLCCapability *capC = new QLCCapability(20, 255, "Gobo mode");
    capC->setPreset(QLCCapability::Alias);
    AliasInfo cAlias;
    cAlias.targetMode = "Main";
    cAlias.sourceChannel = "Color";
    cAlias.targetChannel = "Gobo";
    capC->addAlias(cAlias);
    capC->addAlias(otherModeAlias);

    QVERIFY(control->addCapability(plain));
    QVERIFY(control->addCapability(capB));
    QVERIFY(control->addCapability(capC));

    QLCFixtureMode *mainMode = makeMode(def, "Main", QList<QLCChannel *>() << control << color);
    makeMode(def, "Other", QList<QLCChannel *>() << control << color);

    Fixture fxi(this);
    fxi.setFixtureDefinition(def, mainMode);
    fxi.setAddress(0);
    QSignalSpy spy(&fxi, SIGNAL(aliasChanged()));
    QCOMPARE(fxi.channel(1), (const QLCChannel *)color);

    // enter the "Strobe mode" range: Color is replaced by Strobe
    QByteArray values(2, 0);
    values[0] = char(15);
    QVERIFY(fxi.setChannelValues(values) == true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(fxi.channel(1), (const QLCChannel *)strobe);

    // enter the "Gobo mode" range: the previous alias is reverted, then Gobo applied
    values[0] = char(25);
    QVERIFY(fxi.setChannelValues(values) == true);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(fxi.channel(1), (const QLCChannel *)gobo);

    // a value inside the currently active capability changes nothing
    values[0] = char(30);
    QVERIFY(fxi.setChannelValues(values) == true);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(fxi.channel(1), (const QLCChannel *)gobo);

    // back to the alias-free range: the original channel set is restored
    values[0] = char(5);
    QVERIFY(fxi.setChannelValues(values) == true);
    QCOMPARE(spy.count(), 3);
    QCOMPARE(fxi.channel(1), (const QLCChannel *)color);

    fxi.setFixtureDefinition(NULL, NULL);
    delete def;
}

/*****************************************************************************
 * RGB panel component strings
 *****************************************************************************/

void Fixture_Test::componentStrings()
{
    bool is16bit = true;
    QCOMPARE(int(Fixture::stringToComponents("BGR", is16bit)), int(Fixture::BGR));
    QCOMPARE(is16bit, false);
    QCOMPARE(int(Fixture::stringToComponents("BRG", is16bit)), int(Fixture::BRG));
    QCOMPARE(int(Fixture::stringToComponents("GBR", is16bit)), int(Fixture::GBR));
    QCOMPARE(int(Fixture::stringToComponents("GRB", is16bit)), int(Fixture::GRB));
    QCOMPARE(int(Fixture::stringToComponents("RBG", is16bit)), int(Fixture::RBG));
    QCOMPARE(int(Fixture::stringToComponents("RGBW", is16bit)), int(Fixture::RGBW));
    QCOMPARE(int(Fixture::stringToComponents("RGB", is16bit)), int(Fixture::RGB));
    QCOMPARE(int(Fixture::stringToComponents("Foo", is16bit)), int(Fixture::RGB));

    QCOMPARE(int(Fixture::stringToComponents("GRB 16bit", is16bit)), int(Fixture::GRB));
    QCOMPARE(is16bit, true);
    QCOMPARE(int(Fixture::stringToComponents("RGB 8bit", is16bit)), int(Fixture::RGB));
    QCOMPARE(is16bit, false);

    QCOMPARE(Fixture::componentsToString(Fixture::GBR, true), QString("GBR 16bit"));
    QCOMPARE(Fixture::componentsToString(Fixture::RGBW, false), QString("RGBW"));
    QCOMPARE(Fixture::componentsToString(Fixture::RGB, false), QString("RGB"));
}

/*****************************************************************************
 * Load & save, optional parts
 *****************************************************************************/

void Fixture_Test::saveBare()
{
    Fixture fxi(this);
    fxi.setID(3);
    fxi.setName("Bare");

    QByteArray xml = writeFixture(fxi);
    QVERIFY(xml.contains("<Manufacturer>Generic</Manufacturer>"));
    QVERIFY(xml.contains("<Model>Generic</Model>"));
    QVERIFY(xml.contains("<Mode>Generic</Mode>"));
    QVERIFY(xml.contains("<Channels>0</Channels>"));
    QVERIFY(xml.contains("<ID>3</ID>"));
    QVERIFY(!xml.contains("CrossUniverse"));
    QVERIFY(!xml.contains("ExcludeFade"));
}

void Fixture_Test::saveLoadOptionalParts()
{
    Doc doc(this);
    ChannelModifier *mod = new ChannelModifier();
    mod->setName("SaveMod");
    QVERIFY(doc.modifiersCache()->addModifier(mod) == true);

    Fixture fxi(this);
    fxi.setID(7);
    fxi.setName("Optional");
    fxi.setChannels(6);
    fxi.setUniverse(1);
    fxi.setAddress(508);
    fxi.setCrossUniverse(true);
    fxi.setExcludeFadeChannels(QList<int>() << 0 << 2);
    fxi.setForcedHTPChannels(QList<int>() << 1 << 2);
    fxi.setForcedLTPChannels(QList<int>() << 3 << 4);
    fxi.setChannelModifier(5, mod);

    QByteArray xml = writeFixture(fxi);
    QVERIFY(xml.contains("<CrossUniverse>True</CrossUniverse>"));
    QVERIFY(xml.contains("<ExcludeFade>0,2</ExcludeFade>"));
    QVERIFY(xml.contains("<ForcedHTP>1,2</ForcedHTP>"));
    QVERIFY(xml.contains("<ForcedLTP>3,4</ForcedLTP>"));
    QVERIFY(xml.contains("<Modifier Channel=\"5\" Name=\"SaveMod\"/>"));

    Fixture loaded(this);
    QVERIFY(readFixture(xml, loaded, &doc) == true);
    QCOMPARE(loaded.id(), quint32(7));
    QCOMPARE(loaded.name(), QString("Optional"));
    QCOMPARE(loaded.universe(), quint32(1));
    // a cross-universe fixture may exceed the 512 channel boundary
    QCOMPARE(loaded.address(), quint32(508));
    QCOMPARE(loaded.crossUniverse(), true);
    QCOMPARE(loaded.channels(), quint32(6));
    QVERIFY(loaded.fixtureDef() != NULL);
    QCOMPARE(loaded.fixtureDef()->manufacturer(), QString(KXMLFixtureGeneric));
    QCOMPARE(loaded.fixtureDef()->model(), QString(KXMLFixtureGeneric));
    QCOMPARE(loaded.excludeFadeChannels(), QList<int>() << 0 << 2);
    QCOMPARE(loaded.forcedHTPChannels(), QList<int>() << 1 << 2);
    QCOMPARE(loaded.forcedLTPChannels(), QList<int>() << 3 << 4);
    QCOMPARE(loaded.channelModifier(5), mod);
    QVERIFY(loaded.channelModifier(0) == NULL);

    // a modifier the cache does not know is dropped on load
    QByteArray unknown = xml;
    unknown.replace("Name=\"SaveMod\"", "Name=\"Nope\"");
    Fixture loaded2(this);
    QVERIFY(readFixture(unknown, loaded2, &doc) == true);
    QVERIFY(loaded2.channelModifier(5) == NULL);
}

void Fixture_Test::saveLoadRGBPanel()
{
    Doc doc(this);
    Fixture fxi(this);
    QLCFixtureDef *def = fxi.genericRGBPanelDef(4, Fixture::RGBW, true);
    QLCFixtureMode *mode = fxi.genericRGBPanelMode(def, Fixture::RGBW, true, 800, 120);
    fxi.setFixtureDefinition(def, mode);
    fxi.setID(11);
    fxi.setName("Panel");

    QCOMPARE(fxi.channels(), quint32(32)); // 4 columns x (RGBW + fine)
    QCOMPARE(fxi.heads(), 4);
    QCOMPARE(mode->name(), QString("RGBW 16bit"));
    QCOMPARE(fxi.channel(6)->name(), QString("White 1"));
    QCOMPARE(fxi.channel(7)->name(), QString("White Fine 1"));
    QCOMPARE(fxi.channel(7)->colour(), QLCChannel::White);
    QCOMPARE(fxi.channel(7)->controlByte(), QLCChannel::LSB);

    QByteArray xml = writeFixture(fxi);
    QVERIFY(xml.contains("<Width>800</Width>"));
    QVERIFY(xml.contains("<Height>120</Height>"));

    Fixture loaded(this);
    QVERIFY(readFixture(xml, loaded, &doc) == true);
    QCOMPARE(loaded.channels(), quint32(32));
    QCOMPARE(loaded.heads(), 4);
    QVERIFY(loaded.fixtureDef() != NULL);
    QCOMPARE(loaded.fixtureDef()->model(), QString(KXMLFixtureRGBPanel));
    QCOMPARE(loaded.fixtureMode()->name(), QString("RGBW 16bit"));
    QCOMPARE(loaded.fixtureMode()->physical().width(), 800);
    QCOMPARE(loaded.fixtureMode()->physical().height(), 120);
    QCOMPARE(loaded.channel(7)->name(), QString("White Fine 1"));
}

void Fixture_Test::loadMissingMode()
{
    QByteArray xml =
        "<TestRoot><Fixture>"
        "<Manufacturer>Martin</Manufacturer><Model>MAC250+</Model><Mode>Nonexistent</Mode>"
        "<ID>42</ID><Name>NoMode</Name><Universe>0</Universe><Address>21</Address><Channels>9</Channels>"
        "</Fixture></TestRoot>";

    // known definition, unknown mode: falls back to a generic dimmer and logs the error
    Fixture fxi(this);
    QVERIFY(readFixture(xml, fxi, m_doc) == true);
    QCOMPARE(fxi.channels(), quint32(9));
    QVERIFY(fxi.fixtureDef() != NULL);
    QCOMPARE(fxi.fixtureDef()->manufacturer(), QString(KXMLFixtureGeneric));
    QVERIFY(m_doc->errorLog().contains("Fixture mode <b>Nonexistent</b> not found"));
}

void Fixture_Test::loadZeroChannels()
{
    QByteArray xml =
        "<TestRoot><Fixture>"
        "<Manufacturer>Generic</Manufacturer><Model>Generic</Model><Mode>Generic</Mode>"
        "<ID>1</ID><Name>Zero</Name><Universe>0</Universe><Address>0</Address><Channels>0</Channels>"
        "</Fixture></TestRoot>";

    Fixture fxi(this);
    QVERIFY(readFixture(xml, fxi, m_doc) == true);
    // an invalid channel count is corrected to one generic dimmer channel
    QCOMPARE(fxi.channels(), quint32(1));
    QCOMPARE(fxi.fixtureMode()->name(), QString("1 Channel"));
    QVERIFY(m_doc->errorLog().contains("out of bounds"));
}

void Fixture_Test::loadUnknownTag()
{
    QByteArray xml =
        "<TestRoot><Fixture>"
        "<Manufacturer>Generic</Manufacturer><Model>Generic</Model><Mode>Generic</Mode>"
        "<Foo>bar</Foo>"
        "<ID>2</ID><Name>Unknown</Name><Universe>2</Universe><CrossUniverse>True</CrossUniverse>"
        "<Address>510</Address><Channels>4</Channels>"
        "</Fixture></TestRoot>";

    Fixture fxi(this);
    QVERIFY(readFixture(xml, fxi, m_doc) == true);
    QCOMPARE(fxi.name(), QString("Unknown"));
    QCOMPARE(fxi.channels(), quint32(4));
    QCOMPARE(fxi.universe(), quint32(2));
    QCOMPARE(fxi.crossUniverse(), true);
    // the cross-universe flag keeps an address range that overflows the universe
    QCOMPARE(fxi.address(), quint32(510));
}

void Fixture_Test::loaderFailures()
{
    Doc doc(this);

    // wrong root element: nothing is added
    {
        QBuffer buffer;
        buffer.setData(QByteArray("<Function/>"));
        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();
        QVERIFY(Fixture::loader(xmlReader, &doc) == false);
        QCOMPARE(doc.fixtures().size(), 0);
    }

    QByteArray xml =
        "<Fixture>"
        "<Manufacturer>Generic</Manufacturer><Model>Generic</Model><Mode>Generic</Mode>"
        "<ID>42</ID><Name>Twice</Name><Universe>0</Universe><Address>0</Address><Channels>2</Channels>"
        "</Fixture>";

    for (int attempt = 0; attempt < 2; attempt++)
    {
        QBuffer buffer;
        buffer.setData(xml);
        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();
        // the second fixture with the same ID cannot be added to the Doc
        QCOMPARE(Fixture::loader(xmlReader, &doc), attempt == 0);
        QCOMPARE(doc.fixtures().size(), 1);
    }
    QVERIFY(doc.fixture(42) != NULL);
    QCOMPARE(doc.fixture(42)->name(), QString("Twice"));
}

// QTEST_MAIN (QGuiApplication): getIconFromType() constructs a QIcon, which
// needs a live QGuiApplication (QPixmap aborts without one).
QTEST_MAIN(Fixture_Test)
