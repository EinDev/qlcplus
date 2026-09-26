/*
  Q Light Controller Plus - Test Unit
  rgbplain_test.cpp

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
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QBuffer>

#define private public
#include "rgbplain.h"
#undef private
#include "doc.h"
#include "rgbplain_test.h"

void RGBPlain_Test::initTestCase()
{
    m_doc = new Doc(this);
}

void RGBPlain_Test::cleanupTestCase()
{
    delete m_doc;
}

void RGBPlain_Test::defaults()
{
    RGBPlain algo(m_doc);
    QCOMPARE(algo.rgbMapStepCount(QSize(4,4)), 1);
    QCOMPARE(algo.type(), RGBAlgorithm::Plain);
    QCOMPARE(algo.acceptColors(), 1);
    QCOMPARE(algo.name(), QString("Plain Color"));
    QCOMPARE(algo.author(), QString("Massimo Callegari"));
    QCOMPARE(algo.apiVersion(), 1);
    QVERIFY(algo.doc() == m_doc);
}

void RGBPlain_Test::mapping()
{
    RGBPlain algo(m_doc);
    RGBMap map;
    algo.rgbMap(QSize(2,2), qRgb(1,2,3), 0, map);
    QCOMPARE(map.size(),2);
    QCOMPARE(map[0][0], (uint)qRgb(1,2,3));
}

void RGBPlain_Test::colors()
{
    RGBPlain algo(m_doc);

    // The raw color API is a no-op for this algorithm: the color to render
    // is the one passed to rgbMap()
    algo.rgbMapSetColors(QVector<uint>() << 0xFF0000 << 0x00FF00);
    QCOMPARE(algo.rgbMapGetColors(), QVector<uint>());

    // Only the first (start) color is kept
    QVector<QColor> colors;
    colors << Qt::red << Qt::green << Qt::blue;
    algo.setColors(colors);
    QCOMPARE(algo.getColor(0), QColor(Qt::red));
    QCOMPARE(algo.getColor(1), QColor());
    QCOMPARE(algo.getColor(2), QColor());

    algo.setColors(QVector<QColor>());
    QCOMPARE(algo.getColor(0), QColor());
}

void RGBPlain_Test::copyAndClone()
{
    RGBPlain algo(m_doc);
    RGBPlain copy(algo);
    QVERIFY(copy.doc() == m_doc);
    QCOMPARE(copy.type(), RGBAlgorithm::Plain);
    QCOMPARE(copy.name(), QString("Plain Color"));

    RGBAlgorithm *clone = algo.clone();
    QVERIFY(clone != NULL);
    QVERIFY(clone != &algo);
    QCOMPARE(clone->type(), RGBAlgorithm::Plain);
    QCOMPARE(clone->name(), QString("Plain Color"));
    QVERIFY(clone->doc() == m_doc);

    RGBMap map;
    clone->rgbMap(QSize(3, 2), qRgb(9, 8, 7), 5, map);
    QCOMPARE(map.size(), 2);
    QCOMPARE(map[0].size(), 3);
    QCOMPARE(map[1][2], (uint)qRgb(9, 8, 7));
    delete clone;
}

void RGBPlain_Test::saveXML()
{
    RGBPlain algo(m_doc);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(algo.saveXML(&xmlWriter) == true);
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QCOMPARE(xmlReader.name().toString(), QString("Algorithm"));
    QCOMPARE(xmlReader.attributes().value("Type").toString(), QString("Plain"));
    // No child elements
    QVERIFY(xmlReader.readNextStartElement() == false);
    buffer.close();

    // Round trip
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    xmlReader.setDevice(&buffer);
    xmlReader.readNextStartElement();
    RGBPlain loaded(m_doc);
    QVERIFY(loaded.loadXML(xmlReader) == true);
}

void RGBPlain_Test::loadXML()
{
    // Wrong root tag
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Foo");
        xmlWriter.writeAttribute("Type", "Plain");
        xmlWriter.writeEndElement();
        xmlWriter.setDevice(NULL);
        buffer.close();

        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();
        RGBPlain algo(m_doc);
        QVERIFY(algo.loadXML(xmlReader) == false);
    }

    // Wrong algorithm type
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Algorithm");
        xmlWriter.writeAttribute("Type", "Image");
        xmlWriter.writeEndElement();
        xmlWriter.setDevice(NULL);
        buffer.close();

        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();
        RGBPlain algo(m_doc);
        QVERIFY(algo.loadXML(xmlReader) == false);
    }

    // Unknown children are skipped, the element after the algorithm is
    // left for the caller
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Root");
        xmlWriter.writeStartElement("Algorithm");
        xmlWriter.writeAttribute("Type", "Plain");
        xmlWriter.writeTextElement("Foo", "Bar");
        xmlWriter.writeEndElement();
        xmlWriter.writeTextElement("Next", "1");
        xmlWriter.writeEndElement();
        xmlWriter.setDevice(NULL);
        buffer.close();

        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement(); // Root
        xmlReader.readNextStartElement(); // Algorithm
        RGBPlain algo(m_doc);
        QVERIFY(algo.loadXML(xmlReader) == true);
        QVERIFY(xmlReader.readNextStartElement());
        QCOMPARE(xmlReader.name().toString(), QString("Next"));
    }
}

QTEST_APPLESS_MAIN(RGBPlain_Test)
