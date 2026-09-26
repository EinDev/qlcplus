/*
  Q Light Controller Plus - Unit tests
  rgbalgorithm_test.cpp

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

#define private public
#include "rgbalgorithm_test.h"
#include "rgbscriptscache.h"
#include "rgbalgorithm.h"
#ifdef QT_QML_LIB
  #include "rgbscriptv4.h"
#else
  #include "rgbscript.h"
#endif
#undef private

#include "doc.h"

#include "../common/resource_paths.h"

void RGBAlgorithm_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir dir(INTERNAL_SCRIPTDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*.js"));
    QVERIFY(dir.entryList().size() > 0);
    QVERIFY(m_doc->rgbScriptsCache()->load(dir));
}

void RGBAlgorithm_Test::cleanupTestCase()
{
    delete m_doc;
}

void RGBAlgorithm_Test::algorithms()
{
    QStringList list = RGBAlgorithm::algorithms(m_doc);
    QVERIFY(list.contains("Text"));
    QVERIFY(list.contains("Image"));
    QVERIFY(list.contains("Stripes"));
    QVERIFY(list.contains("Opposite"));
    QVERIFY(list.contains("Random Single"));
}

void RGBAlgorithm_Test::algorithm()
{
    RGBAlgorithm* algo = RGBAlgorithm::algorithm(m_doc, "Foo");
    QVERIFY(algo != NULL);
    QCOMPARE(algo->apiVersion(), 0); // Invalid
    delete algo;

    algo = RGBAlgorithm::algorithm(m_doc, QString());
    QVERIFY(algo != NULL);
    QCOMPARE(algo->apiVersion(), 0); // Invalid
    delete algo;

    algo = RGBAlgorithm::algorithm(m_doc, "Text");
    QVERIFY(algo != NULL);
    QCOMPARE(algo->type(), RGBAlgorithm::Text);
    QCOMPARE(algo->name(), QString("Text"));
    delete algo;

    algo = RGBAlgorithm::algorithm(m_doc, "Stripes");
    QVERIFY(algo != NULL);
    QCOMPARE(algo->type(), RGBAlgorithm::Script);
    QCOMPARE(algo->name(), QString("Stripes"));
    delete algo;

    algo = RGBAlgorithm::algorithm(m_doc, "Balls");
    QVERIFY(algo != NULL);
    QCOMPARE(algo->type(), RGBAlgorithm::Script);
    QCOMPARE(algo->name(), QString("Balls"));
    QCOMPARE(algo->apiVersion(), 3);
    QCOMPARE(algo->acceptColors(), 5);
    QVector<QColor> colors;
    colors << Qt::red;
    colors << Qt::green;
    colors << Qt::blue;
    algo->setColors(colors);

    QCOMPARE(algo->getColor(0), Qt::red);
    QCOMPARE(algo->getColor(1), Qt::green);
    QCOMPARE(algo->getColor(2), Qt::blue);
    delete algo;
}

void RGBAlgorithm_Test::builtInAlgorithms()
{
    RGBAlgorithm* algo = RGBAlgorithm::algorithm(m_doc, "Image");
    QVERIFY(algo != NULL);
    QCOMPARE(algo->type(), RGBAlgorithm::Image);
    QCOMPARE(algo->name(), QString("Image"));
    QCOMPARE(algo->apiVersion(), 1);
    QCOMPARE(algo->acceptColors(), 0);
    delete algo;

    algo = RGBAlgorithm::algorithm(m_doc, "Audio Spectrum");
    QVERIFY(algo != NULL);
    QCOMPARE(algo->type(), RGBAlgorithm::Audio);
    QCOMPARE(algo->name(), QString("Audio Spectrum"));
    QCOMPARE(algo->acceptColors(), 2);
    delete algo;

    algo = RGBAlgorithm::algorithm(m_doc, "Plain Color");
    QVERIFY(algo != NULL);
    QCOMPARE(algo->type(), RGBAlgorithm::Plain);
    QCOMPARE(algo->name(), QString("Plain Color"));
    QCOMPARE(algo->acceptColors(), 1);
    delete algo;

    // The built-in algorithms come first in the list, in a fixed order
    QStringList list = RGBAlgorithm::algorithms(m_doc);
    QCOMPARE(list.at(0), QString("Plain Color"));
    QCOMPARE(list.at(1), QString("Text"));
    QCOMPARE(list.at(2), QString("Image"));
    QCOMPARE(list.at(3), QString("Audio Spectrum"));
    QCOMPARE(list.mid(4), m_doc->rgbScriptsCache()->names());
}

void RGBAlgorithm_Test::colors()
{
    // The base class keeps as many colors as the algorithm accepts
    QScopedPointer<RGBAlgorithm> plain(RGBAlgorithm::algorithm(m_doc, "Plain Color"));
    QCOMPARE(plain->acceptColors(), 1);
    QVector<QColor> colors;
    colors << Qt::red << Qt::green;
    plain->setColors(colors);
    QCOMPARE(plain->getColor(0), QColor(Qt::red));
    QCOMPARE(plain->getColor(1), QColor()); // beyond acceptColors()
    QCOMPARE(plain->getColor(99), QColor());

    // Fewer colors than accepted: only those given are stored
    QScopedPointer<RGBAlgorithm> audio(RGBAlgorithm::algorithm(m_doc, "Audio Spectrum"));
    QCOMPARE(audio->acceptColors(), 2);
    audio->setColors(QVector<QColor>() << Qt::blue);
    QCOMPARE(audio->getColor(0), QColor(Qt::blue));
    QCOMPARE(audio->getColor(1), QColor());
    audio->setColors(QVector<QColor>());
    QCOMPARE(audio->getColor(0), QColor());
}

void RGBAlgorithm_Test::loaderBuiltIn()
{
    struct Case { const char *type; RGBAlgorithm::Type expected; const char *name; };
    const QList<Case> cases = {
        { "Image", RGBAlgorithm::Image, "Image" },
        { "Audio", RGBAlgorithm::Audio, "Audio Spectrum" },
        { "Plain", RGBAlgorithm::Plain, "Plain Color" },
    };

    foreach (const Case &c, cases)
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Algorithm");
        xmlWriter.writeAttribute("Type", c.type);
        xmlWriter.writeEndElement();
        xmlWriter.writeEndDocument();
        xmlWriter.setDevice(NULL);
        buffer.close();

        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();

        RGBAlgorithm* algo = RGBAlgorithm::loader(m_doc, xmlReader);
        QVERIFY2(algo != NULL, c.type);
        QCOMPARE(algo->type(), c.expected);
        QCOMPARE(algo->name(), QString(c.name));
        delete algo;
    }
}

void RGBAlgorithm_Test::loaderInvalidScript()
{
    // A script that isn't in the cache loads as an invalid script and is dropped
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    xmlWriter.writeStartElement("Algorithm");
    xmlWriter.writeAttribute("Type", "Script");
    xmlWriter.writeCharacters("No such script");
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    RGBAlgorithm* algo = RGBAlgorithm::loader(m_doc, xmlReader);
    QVERIFY(algo == NULL);
}

void RGBAlgorithm_Test::loader()
{
    // Script algo
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Algorithm");
    xmlWriter.writeAttribute("Type", "Script");
    xmlWriter.writeCharacters("Stripes");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    RGBAlgorithm* algo = RGBAlgorithm::loader(m_doc, xmlReader);
    QVERIFY(algo != NULL);
    QCOMPARE(algo->type(), RGBAlgorithm::Script);
    QCOMPARE(algo->name(), QString("Stripes"));
    delete algo;

    buffer.close();

    // Text algo
    QBuffer buffer2;
    buffer2.open(QIODevice::WriteOnly | QIODevice::Text);
    xmlWriter.setDevice(&buffer2);

    xmlWriter.writeStartElement("Algorithm");
    xmlWriter.writeAttribute("Type", "Text");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer2.close();

    buffer2.open(QIODevice::ReadOnly | QIODevice::Text);
    xmlReader.setDevice(&buffer2);
    xmlReader.readNextStartElement();

    algo = RGBAlgorithm::loader(m_doc, xmlReader);
    QVERIFY(algo != NULL);
    QCOMPARE(algo->type(), RGBAlgorithm::Text);
    QCOMPARE(algo->name(), QString("Text"));
    delete algo;

    buffer2.close();

    // Invalid type
    QBuffer buffer3;
    buffer3.open(QIODevice::WriteOnly | QIODevice::Text);
    xmlWriter.setDevice(&buffer3);

    xmlWriter.writeStartElement("Type", "Foo");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer3.close();

    buffer3.open(QIODevice::ReadOnly | QIODevice::Text);
    xmlReader.setDevice(&buffer3);
    xmlReader.readNextStartElement();

    algo = RGBAlgorithm::loader(m_doc, xmlReader);
    QVERIFY(algo == NULL);

    buffer3.close();

    // Invalid tag
    QBuffer buffer4;
    buffer4.open(QIODevice::WriteOnly | QIODevice::Text);
    xmlWriter.setDevice(&buffer4);

    xmlWriter.writeStartElement("Foo");
    xmlWriter.writeAttribute("Type", "Text");
    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer4.close();

    buffer4.open(QIODevice::ReadOnly | QIODevice::Text);
    xmlReader.setDevice(&buffer4);
    xmlReader.readNextStartElement();

    algo = RGBAlgorithm::loader(m_doc, xmlReader);
    QVERIFY(algo == NULL);
}

QTEST_MAIN(RGBAlgorithm_Test)
