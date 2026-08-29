/*
  Q Light Controller Plus - Test Unit
  monitorproperties_test.cpp

  Copyright (c) Massimo Callegari

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
#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#define private public
#include "monitorproperties.h"
#undef private
#include "doc.h"
#include "monitorproperties_test.h"

void MonitorProperties_Test::defaults()
{
    MonitorProperties mp;

    QCOMPARE(mp.displayMode(), MonitorProperties::DMX);
    QCOMPARE(mp.channelStyle(), MonitorProperties::DMXChannels);
    QCOMPARE(mp.valueStyle(), MonitorProperties::DMXValues);
    QCOMPARE(mp.gridSize(), QVector3D(5, 3, 5));
    QCOMPARE(mp.gridUnits(), MonitorProperties::Meters);
    QCOMPARE(mp.pointOfView(), MonitorProperties::Undefined);
    QCOMPARE(mp.stageType(), MonitorProperties::StageSimple);
    QCOMPARE(mp.labelsVisible(), false);
    QVERIFY(mp.commonBackgroundImage().isEmpty());
}

void MonitorProperties_Test::fixtureItems()
{
    MonitorProperties mp;

    mp.setFixturePosition(10, 0, 0, QVector3D(1, 2, 3));
    mp.setFixtureRotation(10, 0, 0, QVector3D(0, 90, 0));
    mp.setFixtureGelColor(10, 0, 0, QColor(Qt::red));
    mp.setFixtureName(10, 0, 0, "Main");
    mp.setFixtureFlags(10, 0, 0, MonitorProperties::HiddenFlag);

    QCOMPARE(mp.fixturePosition(10,0,0), QVector3D(1,2,3));
    QCOMPARE(mp.fixtureRotation(10,0,0), QVector3D(0,90,0));
    QCOMPARE(mp.fixtureGelColor(10,0,0), QColor(Qt::red));
    QCOMPARE(mp.fixtureName(10,0,0), QString("Main"));
    QCOMPARE(mp.fixtureFlags(10,0,0), quint32(MonitorProperties::HiddenFlag));

    mp.removeFixture(10);
    QCOMPARE(mp.containsFixture(10), false);
}

// Unlike lightItemsXML() below, there was previously no XML round-trip
// coverage at all for fixture items - fixtureItems() above only exercises the
// in-memory getters/setters, never saveXML()/loadXML(). Added while
// investigating a user report of a moving fixture's position resetting to 0
// after a save/reload: this test passes, which rules out MonitorProperties'
// own save/load handling as the cause (see ContextManager::pushPositionDelta()/
// slotUniverseWritten() instead, where the actual bug was found - a DMX
// Position/Rotation-channel-driven fixture's delta was never written back
// into MonitorProperties at all, so there was nothing here to load wrong).
void MonitorProperties_Test::fixtureItemsXML()
{
    Doc doc(this);
    MonitorProperties mp;

    mp.setFixturePosition(10, 0, 0, QVector3D(1.5, 2.5, 3.5));
    mp.setFixtureRotation(10, 0, 0, QVector3D(15, 90, 270));
    mp.setFixtureGelColor(10, 0, 0, QColor(Qt::red));
    mp.setFixtureFixedZoom(10, 0, 0, 25);
    mp.setFixtureFlags(10, 0, 0, MonitorProperties::HiddenFlag | MonitorProperties::LockedFlag);

    // A linked copy, to confirm sub-items round-trip independently of the
    // base item (fixtureIDList()'s subID = 0 vs head/linked-packed subID
    // path). Custom names are only ever set on linked copies in practice
    // (see ContextManager::setLinkedFixture()) - save/load both gate the
    // Name attribute on the Linked attribute being present, matching that.
    mp.setFixturePosition(10, 0, 1, QVector3D(4, 5, 6));
    mp.setFixtureName(10, 0, 1, "Linked 1");

    QByteArray xmlData;
    QBuffer buffer(&xmlData);
    QVERIFY(buffer.open(QIODevice::WriteOnly));

    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    QVERIFY(mp.saveXML(&writer, &doc));
    writer.writeEndDocument();
    buffer.close();

    MonitorProperties loaded;
    QXmlStreamReader reader(xmlData);
    while (reader.readNextStartElement())
    {
        if (reader.name() == KXMLQLCMonitorProperties)
        {
            QVERIFY(loaded.loadXML(reader, &doc));
            break;
        }
        reader.skipCurrentElement();
    }

    QCOMPARE(loaded.fixturePosition(10, 0, 0), QVector3D(1.5, 2.5, 3.5));
    QCOMPARE(loaded.fixtureRotation(10, 0, 0), QVector3D(15, 90, 270));
    QCOMPARE(loaded.fixtureGelColor(10, 0, 0), QColor(Qt::red));
    QCOMPARE(loaded.fixtureFixedZoom(10, 0, 0), 25);
    QCOMPARE(loaded.fixtureFlags(10, 0, 0),
             quint32(MonitorProperties::HiddenFlag | MonitorProperties::LockedFlag));

    QCOMPARE(loaded.fixturePosition(10, 0, 1), QVector3D(4, 5, 6));
    QCOMPARE(loaded.fixtureName(10, 0, 1), QString("Linked 1"));
}

// Per-fixture DMX position/rotation invert + scale (added for the "moving
// fixtures don't move the way the view assumes" feature): a fixture that
// never sets these must keep behaving exactly as before (default flags = 0,
// default scale = 1.0, matching PreviewItem::m_dmxScale's own default
// member initializer) - and both must round-trip through save/load exactly
// like every other per-fixture flag/value already does.
void MonitorProperties_Test::fixtureDmxTransformDefaults()
{
    MonitorProperties mp;

    // A fixture never touched at all - containsFixture() is false, but the
    // getters must still return the same defaults as an explicitly-set one.
    QCOMPARE(mp.fixtureDmxScale(99, 0, 0), 1.0f);
    QCOMPARE(mp.fixtureFlags(99, 0, 0) & (MonitorProperties::InvertedPositionXFlag |
                                          MonitorProperties::InvertedPositionYFlag |
                                          MonitorProperties::InvertedPositionZFlag |
                                          MonitorProperties::InvertedRotationXFlag |
                                          MonitorProperties::InvertedRotationYFlag |
                                          MonitorProperties::InvertedRotationZFlag), quint32(0));
}

void MonitorProperties_Test::fixtureDmxTransformXML()
{
    Doc doc(this);
    MonitorProperties mp;

    quint32 flags = MonitorProperties::InvertedPositionXFlag |
                    MonitorProperties::InvertedPositionZFlag |
                    MonitorProperties::InvertedRotationYFlag;
    mp.setFixtureFlags(20, 0, 0, flags);
    mp.setFixtureDmxScale(20, 0, 0, 2.5f);

    QByteArray xmlData;
    QBuffer buffer(&xmlData);
    QVERIFY(buffer.open(QIODevice::WriteOnly));

    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    QVERIFY(mp.saveXML(&writer, &doc));
    writer.writeEndDocument();
    buffer.close();

    MonitorProperties loaded;
    QXmlStreamReader reader(xmlData);
    while (reader.readNextStartElement())
    {
        if (reader.name() == KXMLQLCMonitorProperties)
        {
            QVERIFY(loaded.loadXML(reader, &doc));
            break;
        }
        reader.skipCurrentElement();
    }

    QCOMPARE(loaded.fixtureFlags(20, 0, 0), flags);
    QCOMPARE(loaded.fixtureDmxScale(20, 0, 0), 2.5f);
}

void MonitorProperties_Test::lightItems()
{
    MonitorProperties mp;

    mp.setLightPosition("moving_head.dae", 0, QVector3D(1.5f, 2.5f, 3.5f));

    QList<QString> resources = mp.lightResources();
    QCOMPARE(resources.count(), 1);
    QCOMPARE(resources.first(), QString("moving_head.dae"));
    QCOMPARE(mp.containsLightEmitter("moving_head.dae", 0), true);
    QCOMPARE(mp.lightPosition("moving_head.dae", 0), QVector3D(1.5f, 2.5f, 3.5f));

    mp.removeLight("moving_head.dae");
    QCOMPARE(mp.containsLightEmitter("moving_head.dae", 0), false);
}

void MonitorProperties_Test::lightItemsXML()
{
    Doc doc(this);
    MonitorProperties mp;
    mp.setLightPosition("moving_head.dae", 0, QVector3D(1.5f, 2.5f, 3.5f));

    QByteArray xmlData;
    QBuffer buffer(&xmlData);
    QVERIFY(buffer.open(QIODevice::WriteOnly));

    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    QVERIFY(mp.saveXML(&writer, &doc));
    writer.writeEndDocument();
    buffer.close();

    MonitorProperties loaded;
    QXmlStreamReader reader(xmlData);
    while (reader.readNextStartElement())
    {
        if (reader.name() == KXMLQLCMonitorProperties)
        {
            QVERIFY(loaded.loadXML(reader, &doc));
            break;
        }
        reader.skipCurrentElement();
    }

    QCOMPARE(loaded.lightPosition("moving_head.dae", 0), QVector3D(1.5f, 2.5f, 3.5f));
}

void MonitorProperties_Test::genericItems()
{
    MonitorProperties mp;

    quint32 id = 100;
    mp.setItemName(id, "Item");
    mp.setItemResource(id, "path");
    mp.setItemPosition(id, QVector3D(1,1,1));
    mp.setItemRotation(id, QVector3D(0,0,90));
    mp.setItemScale(id, QVector3D(2,2,2));
    mp.setItemFlags(id, MonitorProperties::InvertedPanFlag);

    QList<quint32> ids = mp.genericItemsID();
    QCOMPARE(ids.count(), 1);
    QCOMPARE(ids.first(), id);
    QCOMPARE(mp.itemName(id), QString("Item"));
    QCOMPARE(mp.itemResource(id), QString("path"));
    QCOMPARE(mp.itemPosition(id), QVector3D(1,1,1));
    QCOMPARE(mp.itemRotation(id), QVector3D(0,0,90));
    QCOMPARE(mp.itemScale(id), QVector3D(2,2,2));
    QCOMPARE(mp.itemFlags(id), quint32(MonitorProperties::InvertedPanFlag));

    mp.removeItem(id);
    QCOMPARE(mp.containsItem(id), false);
}

void MonitorProperties_Test::reset()
{
    MonitorProperties mp;
    mp.setGridSize(QVector3D(10,10,10));
    mp.setGridUnits(MonitorProperties::Feet);
    mp.setPointOfView(MonitorProperties::FrontView);
    mp.setStageType(MonitorProperties::StageBox);
    mp.setLabelsVisible(true);
    mp.setFixturePosition(1,0,0,QVector3D(1,2,3));
    mp.setItemName(2,"foo");
    mp.setCommonBackgroundImage("img.png");

    mp.reset();

    QCOMPARE(mp.gridSize(), QVector3D(5,3,5));
    QCOMPARE(mp.gridUnits(), MonitorProperties::Meters);
    QCOMPARE(mp.pointOfView(), MonitorProperties::Undefined);
    QCOMPARE(mp.stageType(), MonitorProperties::StageSimple);
    QCOMPARE(mp.labelsVisible(), false);
    QCOMPARE(mp.fixtureItemsID().count(), 0);
    QCOMPARE(mp.lightResources().count(), 0);
    QCOMPARE(mp.genericItemsID().count(), 0);
    QVERIFY(mp.commonBackgroundImage().isEmpty());
}

QTEST_APPLESS_MAIN(MonitorProperties_Test)
