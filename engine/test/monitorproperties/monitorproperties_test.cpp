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
#include <QMatrix4x4>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#define private public
#include "monitorproperties.h"
#undef private
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "qlcchannel.h"
#include "qlcconfig.h"
#include "qlcfile.h"
#include "fixture.h"
#include "doc.h"
#include "monitorproperties_test.h"

static QByteArray saveMonitor(const MonitorProperties &mp, const Doc *doc)
{
    QByteArray xmlData;
    QBuffer buffer(&xmlData);
    buffer.open(QIODevice::WriteOnly);
    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    mp.saveXML(&writer, doc);
    writer.writeEndDocument();
    buffer.close();
    return xmlData;
}

static bool loadMonitor(const QByteArray &xml, MonitorProperties &mp, const Doc *doc)
{
    QXmlStreamReader reader(xml);
    while (reader.readNextStartElement())
    {
        if (reader.name() == KXMLQLCMonitorProperties)
            return mp.loadXML(reader, doc);
        reader.skipCurrentElement();
    }
    return false;
}

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

// Per-fixture DMX position/rotation invert + rotation scale (added for the
// "moving fixtures don't move the way the view assumes" feature): a fixture
// that never sets these must keep behaving exactly as before (default
// flags = 0, default scale = 1.0, matching PreviewItem::m_rotationScale's own
// default member initializer) - and both must round-trip through save/load
// exactly like every other per-fixture flag/value already does.
void MonitorProperties_Test::fixtureDmxTransformDefaults()
{
    MonitorProperties mp;

    // A fixture never touched at all - containsFixture() is false, but the
    // getters must still return the same defaults as an explicitly-set one.
    QCOMPARE(mp.fixtureRotationScale(99, 0, 0), 1.0f);
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
    mp.setFixtureRotationScale(20, 0, 0, 2.5f);

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
    QCOMPARE(loaded.fixtureRotationScale(20, 0, 0), 2.5f);
}

// Per-fixture DMX position range in meters (default 800.0, replacing the old
// fixed +/-2.5m POSITION_DELTA_RANGE for drone-scale rigs) - independent of
// the rotation scale above. A fixture that never sets it must keep reading
// back the 800.0 default, matching PreviewItem::m_positionRange's own default
// member initializer.
void MonitorProperties_Test::fixturePositionRangeDefaults()
{
    MonitorProperties mp;

    QCOMPARE(mp.fixturePositionRange(99, 0, 0), 800.0f);
}

// Round-trips through save/load like every other per-fixture value, and
// confirms the "not written when equal to the 800.0 default" skip-behavior
// (mirroring how fixtureDmxTransformXML() above never asserts on the
// DmxScale skip case either, but this test explicitly covers both the
// skip and the non-skip case since it's new coverage).
void MonitorProperties_Test::fixturePositionRangeXML()
{
    Doc doc(this);
    MonitorProperties mp;

    mp.setFixturePositionRange(21, 0, 0, 500.0f);
    // Fixture 22 is left at the 800.0 default - its PositionRange attribute
    // must not be written at all.
    mp.setFixturePosition(22, 0, 0, QVector3D(1, 2, 3));

    QByteArray xmlData;
    QBuffer buffer(&xmlData);
    QVERIFY(buffer.open(QIODevice::WriteOnly));

    QXmlStreamWriter writer(&buffer);
    writer.writeStartDocument();
    QVERIFY(mp.saveXML(&writer, &doc));
    writer.writeEndDocument();
    buffer.close();

    // The default-valued fixture must not have a PositionRange attribute in
    // the raw XML at all - proving the skip-if-default behavior actually
    // skips the write, not just that loading it back happens to coincide
    // with the default.
    QVERIFY(xmlData.contains("PositionRange=\"500\""));
    QVERIFY(!xmlData.contains("PositionRange=\"800\""));

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

    QCOMPARE(loaded.fixturePositionRange(21, 0, 0), 500.0f);
    QCOMPARE(loaded.fixturePositionRange(22, 0, 0), 800.0f);
}

// HasDmxPositionFlag/HasDmxRotationFlag mark a fixture's persisted position/
// rotation as a genuine dragged DMX delta (see ContextManager::
// pushPositionDelta()/pushRotationDelta() and restorePersistedDmxTransforms()
// for why this distinction matters), set independently of each other and of
// every other flag - added while implementing the "restore on load" half of
// the position-resets-to-0 fix, to guard against a silent XML round-trip
// regression the way fixtureDmxTransformXML() already does for invert/scale.
void MonitorProperties_Test::hasDmxTransformFlagsXML()
{
    Doc doc(this);
    MonitorProperties mp;

    // Position-only on one fixture, rotation-only on another, to confirm
    // neither flag implies the other.
    mp.setFixturePosition(30, 0, 0, QVector3D(10, 20, 30));
    mp.setFixtureFlags(30, 0, 0, MonitorProperties::HasDmxPositionFlag);

    mp.setFixtureRotation(31, 0, 0, QVector3D(45, 0, 0));
    mp.setFixtureFlags(31, 0, 0, MonitorProperties::HasDmxRotationFlag);

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

    QCOMPARE(loaded.fixtureFlags(30, 0, 0), quint32(MonitorProperties::HasDmxPositionFlag));
    QCOMPARE(loaded.fixturePosition(30, 0, 0), QVector3D(10, 20, 30));

    QCOMPARE(loaded.fixtureFlags(31, 0, 0), quint32(MonitorProperties::HasDmxRotationFlag));
    QCOMPARE(loaded.fixtureRotation(31, 0, 0), QVector3D(45, 0, 0));
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

/*****************************************************************************
 * Point of view conversion
 *****************************************************************************/

void MonitorProperties_Test::pointOfViewConversion()
{
    // Leaving the undefined (legacy 2D) point of view converts a flat grid and
    // every fixture position into the 3D layout, exactly once.
    {
        MonitorProperties mp;
        mp.setGridSize(QVector3D(8, 4, 0));
        mp.setFixturePosition(1, 0, 0, QVector3D(100, 200, 0));
        mp.setFixturePosition(1, 0, 1, QVector3D(300, 400, 0));
        mp.setPointOfView(MonitorProperties::TopView);
        QCOMPARE(mp.pointOfView(), MonitorProperties::TopView);
        QCOMPARE(mp.gridSize(), QVector3D(8, 3, 4));
        QCOMPARE(mp.fixturePosition(1, 0, 0), QVector3D(100, 1000, 200));
        QCOMPARE(mp.fixturePosition(1, 0, 1), QVector3D(300, 1000, 400));

        // same value again: nothing changes
        mp.setPointOfView(MonitorProperties::TopView);
        QCOMPARE(mp.fixturePosition(1, 0, 0), QVector3D(100, 1000, 200));

        // once defined, a further change is not converted again
        mp.setPointOfView(MonitorProperties::FrontView);
        QCOMPARE(mp.pointOfView(), MonitorProperties::FrontView);
        QCOMPARE(mp.gridSize(), QVector3D(8, 3, 4));
        QCOMPARE(mp.fixturePosition(1, 0, 0), QVector3D(100, 1000, 200));
    }
    {
        MonitorProperties mp;
        mp.setGridSize(QVector3D(8, 4, 0));
        mp.setFixturePosition(2, 0, 0, QVector3D(100, 200, 0));
        mp.setPointOfView(MonitorProperties::RightSideView);
        QCOMPARE(mp.gridSize(), QVector3D(5, 8, 8));
        // z = grid depth (8 m -> 8000 mm) - x
        QCOMPARE(mp.fixturePosition(2, 0, 0), QVector3D(0, 200, 7900));
    }
    {
        MonitorProperties mp;
        mp.setGridSize(QVector3D(8, 4, 0));
        mp.setGridUnits(MonitorProperties::Feet);
        mp.setFixturePosition(3, 0, 0, QVector3D(100, 200, 0));
        mp.setPointOfView(MonitorProperties::LeftSideView);
        QCOMPARE(mp.gridSize(), QVector3D(5, 8, 8));
        QCOMPARE(mp.fixturePosition(3, 0, 0), QVector3D(0, 200, 100));
    }
    {
        // the front view keeps the grid as is and mirrors y over the grid height
        MonitorProperties mp;
        mp.setGridSize(QVector3D(8, 4, 0));
        mp.setGridUnits(MonitorProperties::Feet);
        mp.setFixturePosition(4, 0, 0, QVector3D(100, 200, 0));
        mp.setPointOfView(MonitorProperties::FrontView);
        QCOMPARE(mp.gridSize(), QVector3D(8, 4, 0));
        QVector3D pos = mp.fixturePosition(4, 0, 0);
        QCOMPARE(pos.x(), 100.0f);
        QVERIFY(qFuzzyCompare(pos.y(), 4.0f * 304.8f - 200.0f));
        QCOMPARE(pos.z(), 1000.0f);
    }
    {
        // a grid that already has a depth is left alone, positions are still converted
        MonitorProperties mp;
        mp.setFixturePosition(5, 0, 0, QVector3D(10, 20, 0));
        mp.setPointOfView(MonitorProperties::TopView);
        QCOMPARE(mp.gridSize(), QVector3D(5, 3, 5));
        QCOMPARE(mp.fixturePosition(5, 0, 0), QVector3D(10, 1000, 20));
    }
}

/*****************************************************************************
 * Fixture items
 *****************************************************************************/

void MonitorProperties_Test::removeFixtureItems()
{
    MonitorProperties mp;

    // unknown fixture: no-op
    mp.removeFixture(99, 0, 0);
    QCOMPARE(mp.containsFixture(99), false);

    // a fixture without sub items is removed completely, whatever item is asked
    mp.setFixturePosition(1, 0, 0, QVector3D(1, 1, 1));
    mp.removeFixture(1, 3, 0);
    QCOMPARE(mp.containsFixture(1), false);

    // with sub items present only the requested one goes
    mp.setFixturePosition(2, 0, 0, QVector3D(1, 1, 1));
    mp.setFixturePosition(2, 1, 0, QVector3D(2, 2, 2));
    mp.setFixturePosition(2, 0, 1, QVector3D(3, 3, 3));
    QCOMPARE(mp.fixtureIDList(2).count(), 3);
    mp.removeFixture(2, 1, 0);
    QCOMPARE(mp.containsFixture(2), true);
    QCOMPARE(mp.fixtureIDList(2).count(), 2);
    QCOMPARE(mp.containsItem(2, 1, 0), false);
    QCOMPARE(mp.containsItem(2, 0, 1), true);
    QCOMPARE(mp.fixturePosition(2, 0, 0), QVector3D(1, 1, 1));

    // the ID list of an unknown fixture only holds the base item
    QCOMPARE(mp.fixtureIDList(77), QList<quint32>() << 0);
}

void MonitorProperties_Test::containsFixtureItems()
{
    MonitorProperties mp;
    QCOMPARE(mp.containsItem(1, 0, 0), false);
    mp.setFixtureRotation(1, 0, 0, QVector3D(0, 45, 0));
    QCOMPARE(mp.containsItem(1, 0, 0), true);
    QCOMPARE(mp.containsItem(1, 2, 0), false);
    mp.setFixtureRotation(1, 2, 0, QVector3D(0, 90, 0));
    QCOMPARE(mp.containsItem(1, 2, 0), true);
    QCOMPARE(mp.containsItem(1, 0, 2), false);
}

void MonitorProperties_Test::subItemProperties()
{
    MonitorProperties mp;
    quint32 fid = 10;
    quint16 head = 1;
    quint16 linked = 2;

    mp.setFixturePosition(fid, head, linked, QVector3D(1, 2, 3));
    mp.setFixtureRotation(fid, head, linked, QVector3D(10, 20, 30));
    mp.setFixtureGelColor(fid, head, linked, QColor(Qt::green));
    mp.setFixtureFixedZoom(fid, head, linked, 15);
    mp.setFixtureRotationScale(fid, head, linked, 0.5f);
    mp.setFixturePositionRange(fid, head, linked, 12.5f);
    mp.setFixtureName(fid, head, linked, "Sub");
    mp.setFixtureFlags(fid, head, linked, MonitorProperties::LockedFlag);

    QCOMPARE(mp.fixturePosition(fid, head, linked), QVector3D(1, 2, 3));
    QCOMPARE(mp.fixtureRotation(fid, head, linked), QVector3D(10, 20, 30));
    QCOMPARE(mp.fixtureGelColor(fid, head, linked), QColor(Qt::green));
    QCOMPARE(mp.fixtureFixedZoom(fid, head, linked), 15);
    QCOMPARE(mp.fixtureRotationScale(fid, head, linked), 0.5f);
    QCOMPARE(mp.fixturePositionRange(fid, head, linked), 12.5f);
    QCOMPARE(mp.fixtureName(fid, head, linked), QString("Sub"));
    QCOMPARE(mp.fixtureFlags(fid, head, linked), quint32(MonitorProperties::LockedFlag));

    // the base item is untouched by sub item changes
    QCOMPARE(mp.fixturePosition(fid, 0, 0), QVector3D(0, 0, 0));
    QCOMPARE(mp.fixtureRotation(fid, 0, 0), QVector3D(0, 0, 0));
    QCOMPARE(mp.fixtureFixedZoom(fid, 0, 0), 0);
    QCOMPARE(mp.fixtureRotationScale(fid, 0, 0), 1.0f);
    QCOMPARE(mp.fixturePositionRange(fid, 0, 0), 800.0f);
    QCOMPARE(mp.fixtureName(fid, 0, 0), QString());
    QCOMPARE(mp.fixtureFlags(fid, 0, 0), quint32(0));

    // whole item get/set, sub and base
    PreviewItem item = mp.fixtureItem(fid, head, linked);
    QCOMPARE(item.m_name, QString("Sub"));
    QCOMPARE(item.m_zoom, 15);
    item.m_zoom = 40;
    mp.setFixtureItem(fid, head, linked, item);
    QCOMPARE(mp.fixtureFixedZoom(fid, head, linked), 40);

    PreviewItem base = mp.fixtureItem(fid, 0, 0);
    base.m_name = "Base";
    mp.setFixtureItem(fid, 0, 0, base);
    QCOMPARE(mp.fixtureName(fid, 0, 0), QString("Base"));

    // sub ID packing
    quint32 subID = mp.fixtureSubID(head, linked);
    QCOMPARE(mp.fixtureHeadIndex(subID), head);
    QCOMPARE(mp.fixtureLinkedIndex(subID), linked);
    QCOMPARE(mp.fixtureIDList(fid), QList<quint32>() << 0 << subID);
}

/*****************************************************************************
 * Light emitters
 *****************************************************************************/

void MonitorProperties_Test::lightHeads()
{
    MonitorProperties mp;
    mp.setLightPosition("bar.dae", 0, QVector3D(0, 1, 0));
    mp.setLightPosition("bar.dae", 1, QVector3D(0, 2, 0));
    QCOMPARE(mp.lightHeadList("bar.dae"), QList<quint32>() << 0 << 1);
    QVERIFY(mp.lightHeadList("none.dae").isEmpty());

    LightEmitter emitter = mp.lightEmitter("bar.dae", 1);
    QCOMPARE(emitter.m_position, QVector3D(0, 2, 0));
    emitter.m_position = QVector3D(0, 3, 0);
    mp.setLightEmitter("bar.dae", 1, emitter);
    QCOMPARE(mp.lightPosition("bar.dae", 1), QVector3D(0, 3, 0));

    // removing an unknown resource or head is a no-op
    mp.removeLight("none.dae", 0);
    mp.removeLight("bar.dae", 5);
    QCOMPARE(mp.lightHeadList("bar.dae").count(), 2);

    // the last head removed drops the resource entirely
    mp.removeLight("bar.dae", 0);
    QCOMPARE(mp.containsLightEmitter("bar.dae", 0), false);
    QCOMPARE(mp.containsLightEmitter("bar.dae", 1), true);
    QCOMPARE(mp.lightResources().count(), 1);
    mp.removeLight("bar.dae", 1);
    QCOMPARE(mp.lightResources().count(), 0);
    QCOMPARE(mp.containsLightEmitter("bar.dae", 1), false);
}

void MonitorProperties_Test::rotationMatrix()
{
    QMatrix4x4 identity = MonitorProperties::fixtureRotationMatrix(QVector3D(0, 0, 0));
    QVERIFY(identity.isIdentity());

    // every angle is negated: 90 degrees around Y maps +X onto +Z
    QMatrix4x4 m = MonitorProperties::fixtureRotationMatrix(QVector3D(0, 90, 0));
    QVector3D v = m.map(QVector3D(1, 0, 0));
    QVERIFY(qFuzzyIsNull(v.x()));
    QVERIFY(qFuzzyIsNull(v.y()));
    QVERIFY(qFuzzyCompare(v.z(), 1.0f));

    // X is applied first: the combined matrix equals Z * X
    QMatrix4x4 combined = MonitorProperties::fixtureRotationMatrix(QVector3D(90, 0, 90));
    QMatrix4x4 expected = MonitorProperties::fixtureRotationMatrix(QVector3D(0, 0, 90)) *
                          MonitorProperties::fixtureRotationMatrix(QVector3D(90, 0, 0));
    QVERIFY(qFuzzyCompare(combined, expected));
}

void MonitorProperties_Test::beamPosition()
{
    MonitorProperties mp;
    QVector3D beam;
    QMatrix4x4 rot;

    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer("Test");
    def->setModel("MH");
    def->setType(QLCFixtureDef::MovingHead);
    QLCChannel *pan = new QLCChannel();
    pan->setName("Pan");
    pan->setGroup(QLCChannel::Pan);
    def->addChannel(pan);
    QLCFixtureMode *mode = new QLCFixtureMode(def);
    mode->setName("Mode");
    mode->insertChannel(pan, 0);
    def->addMode(mode);

    Fixture mh(this);
    mh.setID(1);
    mh.setFixtureDefinition(def, mode);
    Fixture dimmer(this);
    dimmer.setID(2);
    dimmer.setChannels(1);

    QCOMPARE(MonitorProperties::fixtureBeamPosition(NULL, &mh, 0, beam, rot), false);
    QCOMPARE(MonitorProperties::fixtureBeamPosition(&mp, NULL, 0, beam, rot), false);

    // a fixture at 2500,1000,2500 mm sits on the centre of the default 5x3x5 m grid
    mp.setFixturePosition(2, 0, 0, QVector3D(2500, 1000, 2500));
    // not a moving head: no emitter data, the root position is still reported
    QCOMPARE(MonitorProperties::fixtureBeamPosition(&mp, &dimmer, 0, beam, rot), false);
    QCOMPARE(beam, QVector3D(0, 1, 0));
    QVERIFY(rot.isIdentity());

    // a moving head without any emitter metadata behaves the same
    mp.setFixturePosition(1, 0, 0, QVector3D(2500, 1000, 2500));
    QCOMPARE(MonitorProperties::fixtureBeamPosition(&mp, &mh, 0, beam, rot), false);
    QCOMPARE(beam, QVector3D(0, 1, 0));

    // emitter on head 0: the local offset is added to the root
    mp.setLightPosition("moving_head.dae", 0, QVector3D(0, 0.5f, 0));
    QCOMPARE(MonitorProperties::fixtureBeamPosition(&mp, &mh, 0, beam, rot), true);
    QCOMPARE(beam, QVector3D(0, 1.5f, 0));

    // head 1 has no emitter of its own: falls back to head 0's offset, rotated
    // by head 1's own rotation (90 degrees around Z turns +Y into +X)
    mp.setFixturePosition(1, 1, 0, QVector3D(3500, 1000, 2500));
    mp.setFixtureRotation(1, 1, 0, QVector3D(0, 0, 90));
    QCOMPARE(MonitorProperties::fixtureBeamPosition(&mp, &mh, 1, beam, rot), true);
    QVERIFY(qFuzzyCompare(beam.x(), 1.5f));
    QVERIFY(qFuzzyCompare(beam.y(), 1.0f));
    QVERIFY(qFuzzyIsNull(beam.z()));
    QVERIFY(!rot.isIdentity());

    // feet grid: the 5 ft grid centre is 0.762 m from the origin
    mp.setGridUnits(MonitorProperties::Feet);
    mp.setFixturePosition(1, 0, 0, QVector3D(0, 0, 0));
    mp.setLightPosition("moving_head.dae", 0, QVector3D(0, 0, 0));
    QCOMPARE(MonitorProperties::fixtureBeamPosition(&mp, &mh, 0, beam, rot), true);
    QVERIFY(qFuzzyCompare(beam.x(), -5.0f * 0.3048f / 2.0f));
    QVERIFY(qFuzzyIsNull(beam.y()));
    QVERIFY(qFuzzyCompare(beam.z(), -5.0f * 0.3048f / 2.0f));

    // emitter only on head 1 while head 0 is asked: no fallback possible
    MonitorProperties mp2;
    mp2.setLightPosition("moving_head.dae", 1, QVector3D(0, 1, 0));
    mp2.setFixturePosition(1, 0, 0, QVector3D(2500, 0, 2500));
    QCOMPARE(MonitorProperties::fixtureBeamPosition(&mp2, &mh, 0, beam, rot), false);
    QCOMPARE(beam, QVector3D(0, 0, 0));

    mh.setFixtureDefinition(NULL, NULL);
    delete def;
}

/*****************************************************************************
 * Generic items and backgrounds
 *****************************************************************************/

void MonitorProperties_Test::genericItemDefaults()
{
    MonitorProperties mp;
    mp.setItemResource(5, "/some/path/truss_arch.obj");
    // no explicit name: derived from the resource file's base name
    QCOMPARE(mp.itemName(5), QString("truss_arch"));
    mp.setItemName(5, "Arch");
    QCOMPARE(mp.itemName(5), QString("Arch"));

    // a never-set scale reads back as 1:1
    QCOMPARE(mp.itemScale(5), QVector3D(1, 1, 1));
    mp.setItemScale(5, QVector3D(2, 3, 4));
    QCOMPARE(mp.itemScale(5), QVector3D(2, 3, 4));
}

void MonitorProperties_Test::customBackgrounds()
{
    MonitorProperties mp;
    QVERIFY(mp.customBackground(1).isEmpty());
    mp.setCustomBackgroundItem(1, "one.png");
    QCOMPARE(mp.customBackground(1), QString("one.png"));
    QVERIFY(mp.customBackground(2).isEmpty());

    QMap<quint32, QString> list;
    list[3] = "three.png";
    list[4] = "four.png";
    mp.setCustomBackgroundList(list);
    QCOMPARE(mp.customBackgroundList(), list);
    QVERIFY(mp.customBackground(1).isEmpty());
    QCOMPARE(mp.customBackground(4), QString("four.png"));

    mp.resetCustomBackgroundList();
    QVERIFY(mp.customBackgroundList().isEmpty());
}

/*****************************************************************************
 * Load & save
 *****************************************************************************/

void MonitorProperties_Test::loadInvalid()
{
    Doc doc(this);
    MonitorProperties mp;

    // wrong root element
    {
        QXmlStreamReader reader(QByteArray("<Foo DisplayMode=\"1\"/>"));
        reader.readNextStartElement();
        QCOMPARE(mp.loadXML(reader, &doc), false);
    }
    // missing display mode
    {
        QXmlStreamReader reader(QByteArray("<Monitor ShowLabels=\"1\"/>"));
        reader.readNextStartElement();
        QCOMPARE(mp.loadXML(reader, &doc), false);
    }

    // entries missing their mandatory attribute and unknown tags are skipped
    QByteArray xml =
        "<Monitor DisplayMode=\"1\" ShowLabels=\"1\">"
        "<FxItem XPos=\"1\" YPos=\"2\"/>"
        "<LightEmitter XPos=\"1\"/>"
        "<MeshItem XPos=\"1\"/>"
        "<Bogus/>"
        "</Monitor>";
    QVERIFY(loadMonitor(xml, mp, &doc) == true);
    QCOMPARE(mp.displayMode(), MonitorProperties::Graphics);
    QCOMPARE(mp.labelsVisible(), true);
    QVERIFY(mp.fixtureItemsID().isEmpty());
    QVERIFY(mp.lightResources().isEmpty());
    QVERIFY(mp.genericItemsID().isEmpty());
}

void MonitorProperties_Test::loadLegacyAndOptional()
{
    Doc doc(this);
    MonitorProperties mp;
    QFont font("Courier", 14);

    QByteArray xml = QString(
        "<Monitor DisplayMode=\"0\" ShowLabels=\"0\">"
        "<Font>%1</Font>"
        "<ChannelStyle>1</ChannelStyle>"
        "<ValueStyle>1</ValueStyle>"
        "<Background>/abs/bg.png</Background>"
        "<BackgroundItem ID=\"7\">/abs/seven.png</BackgroundItem>"
        "<Grid Width=\"12\" Height=\"6\" Units=\"1\" POV=\"1\"/>"
        "<StageItem>2</StageItem>"
        "<FxItem ID=\"3\" Head=\"2\" XPos=\"10\" YPos=\"20\" Rotation=\"45\" "
        "InvertedPan=\"True\" InvertedTilt=\"True\" InvertedPosY=\"True\" "
        "InvertedRotX=\"True\" InvertedRotZ=\"True\"/>"
        "<LightEmitter Res=\"bar.dae\" Head=\"3\" XPos=\"1\" YPos=\"2\" ZPos=\"3\"/>"
        "</Monitor>").arg(font.toString()).toUtf8();

    QVERIFY(loadMonitor(xml, mp, &doc) == true);
    QCOMPARE(mp.font().family(), QString("Courier"));
    QCOMPARE(mp.font().pointSize(), 14);
    QCOMPARE(mp.channelStyle(), MonitorProperties::RelativeChannels);
    QCOMPARE(mp.valueStyle(), MonitorProperties::PercentageValues);
    QCOMPARE(mp.commonBackgroundImage(), doc.denormalizeComponentPath("/abs/bg.png"));
    QCOMPARE(mp.customBackground(7), doc.denormalizeComponentPath("/abs/seven.png"));
    // no Depth attribute: the depth follows the height
    QCOMPARE(mp.gridSize(), QVector3D(12, 6, 6));
    QCOMPARE(mp.gridUnits(), MonitorProperties::Feet);
    QCOMPARE(mp.pointOfView(), MonitorProperties::TopView);
    QCOMPARE(mp.stageType(), MonitorProperties::StageRock);

    QCOMPARE(mp.containsItem(3, 2, 0), true);
    QCOMPARE(mp.fixturePosition(3, 2, 0), QVector3D(10, 20, 0));
    // the legacy single-angle rotation lands on the Y axis
    QCOMPARE(mp.fixtureRotation(3, 2, 0), QVector3D(0, 45, 0));
    QCOMPARE(mp.fixtureFlags(3, 2, 0),
             quint32(MonitorProperties::InvertedPanFlag | MonitorProperties::InvertedTiltFlag |
                     MonitorProperties::InvertedPositionYFlag | MonitorProperties::InvertedRotationXFlag |
                     MonitorProperties::InvertedRotationZFlag));

    QCOMPARE(mp.lightPosition("bar.dae", 3), QVector3D(1, 2, 3));
    QCOMPARE(mp.containsLightEmitter("bar.dae", 0), false);
}

void MonitorProperties_Test::saveOptionalParts()
{
    Doc doc(this);
    MonitorProperties mp;

    mp.setCommonBackgroundImage(QDir::tempPath() + "/common_bg.png");
    mp.setGridSize(QVector3D(6, 4, 0));
    mp.setPointOfView(MonitorProperties::LeftSideView);
    quint32 flags = MonitorProperties::InvertedPanFlag | MonitorProperties::InvertedTiltFlag |
                    MonitorProperties::InvertedPositionYFlag | MonitorProperties::InvertedRotationXFlag |
                    MonitorProperties::InvertedRotationZFlag;
    mp.setFixturePosition(1, 2, 0, QVector3D(1, 2, 3));
    mp.setFixtureFlags(1, 2, 0, flags);
    mp.setLightPosition("", 0, QVector3D(9, 9, 9)); // an empty resource is never written
    mp.setLightPosition("head.dae", 2, QVector3D(1, 2, 3));

    QByteArray xml = saveMonitor(mp, &doc);
    QVERIFY(xml.contains("<Background>"));
    QVERIFY(!xml.contains("<BackgroundItem"));
    QVERIFY(xml.contains("POV=\"4\""));
    QVERIFY(xml.contains("Head=\"2\""));
    QVERIFY(xml.contains("InvertedPan=\"True\""));
    QVERIFY(xml.contains("InvertedTilt=\"True\""));
    QVERIFY(xml.contains("InvertedPosY=\"True\""));
    QVERIFY(xml.contains("InvertedRotX=\"True\""));
    QVERIFY(xml.contains("InvertedRotZ=\"True\""));
    QVERIFY(!xml.contains("Res=\"\""));
    QCOMPARE(xml.count("<LightEmitter"), 1);

    MonitorProperties loaded;
    QVERIFY(loadMonitor(xml, loaded, &doc) == true);
    QVERIFY(loaded.commonBackgroundImage().endsWith("common_bg.png"));
    QCOMPARE(loaded.pointOfView(), MonitorProperties::LeftSideView);
    QCOMPARE(loaded.gridSize(), QVector3D(5, 6, 6));
    QCOMPARE(loaded.fixtureFlags(1, 2, 0), flags);
    QCOMPARE(loaded.fixturePosition(1, 2, 0), QVector3D(1, 2, 3));
    QCOMPARE(loaded.lightResources(), QList<QString>() << "head.dae");
    QCOMPARE(loaded.lightPosition("head.dae", 2), QVector3D(1, 2, 3));

    // per-function backgrounds are written only when there is no common one
    mp.setCommonBackgroundImage(QString());
    mp.setCustomBackgroundItem(5, QDir::tempPath() + "/five.png");
    mp.setCustomBackgroundItem(6, QDir::tempPath() + "/six.png");
    xml = saveMonitor(mp, &doc);
    QVERIFY(!xml.contains("<Background>"));
    QVERIFY(xml.contains("<BackgroundItem ID=\"5\">"));
    QVERIFY(xml.contains("<BackgroundItem ID=\"6\">"));

    MonitorProperties loaded2;
    QVERIFY(loadMonitor(xml, loaded2, &doc) == true);
    QVERIFY(loaded2.commonBackgroundImage().isEmpty());
    QCOMPARE(loaded2.customBackgroundList().keys(), QList<quint32>() << 5 << 6);
    QVERIFY(loaded2.customBackground(6).endsWith("six.png"));
}

void MonitorProperties_Test::meshItemsXML()
{
    Doc doc(this);
    MonitorProperties mp;

    // relative resource: written as is
    mp.setItemResource(1, "truss.obj");
    mp.setItemName(1, "Truss");
    mp.setItemPosition(1, QVector3D(1, 2, 3));
    mp.setItemRotation(1, QVector3D(10, 20, 30));
    mp.setItemScale(1, QVector3D(2, 3, 4));
    mp.setItemFlags(1, MonitorProperties::HiddenFlag | MonitorProperties::LockedFlag);

    // resource inside the system meshes folder: stored relative to it
    QDir meshDir = QDir::cleanPath(QLCFile::systemDirectory(MESHESDIR).path());
    QString meshPath = meshDir.absolutePath() + QDir::separator() + "stage.obj";
    mp.setItemResource(2, meshPath);

    // absolute resource elsewhere: normalised against the workspace
    mp.setItemResource(3, QDir::tempPath() + "/elsewhere.obj");

    QByteArray xml = saveMonitor(mp, &doc);
    QVERIFY(xml.contains("Res=\"truss.obj\""));
    QVERIFY(xml.contains("Res=\"stage.obj\""));
    QVERIFY(xml.contains("Name=\"Truss\""));
    QVERIFY(xml.contains("XScale=\"2\""));
    QVERIFY(xml.contains("YRot=\"20\""));
    QVERIFY(xml.contains("Hidden=\"True\""));
    QVERIFY(xml.contains("Locked=\"True\""));

    MonitorProperties loaded;
    QVERIFY(loadMonitor(xml, loaded, &doc) == true);
    QCOMPARE(loaded.genericItemsID(), QList<quint32>() << 1 << 2 << 3);
    QCOMPARE(loaded.itemResource(1), QString("truss.obj"));
    QCOMPARE(loaded.itemName(1), QString("Truss"));
    QCOMPARE(loaded.itemPosition(1), QVector3D(1, 2, 3));
    QCOMPARE(loaded.itemRotation(1), QVector3D(10, 20, 30));
    QCOMPARE(loaded.itemScale(1), QVector3D(2, 3, 4));
    QCOMPARE(loaded.itemFlags(1), quint32(MonitorProperties::HiddenFlag | MonitorProperties::LockedFlag));

    QCOMPARE(loaded.itemResource(2), QString("stage.obj"));
    QCOMPARE(loaded.itemName(2), QString("stage"));
    QCOMPARE(loaded.itemFlags(2), quint32(0));

    QVERIFY(loaded.itemResource(3).endsWith("elsewhere.obj"));
    QCOMPARE(loaded.itemScale(3), QVector3D(1, 1, 1));
}

// QTEST_GUILESS_MAIN: saving generic items resolves the system meshes folder
// through QCoreApplication::applicationDirPath(), which needs a live
// QCoreApplication (same reason as qlcfixturedefcache_test).
QTEST_GUILESS_MAIN(MonitorProperties_Test)
