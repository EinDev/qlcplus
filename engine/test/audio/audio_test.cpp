/*
  Q Light Controller Plus - Unit test
  audio_test.cpp

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
#include <QXmlStreamWriter>
#include <QXmlStreamReader>

#define protected public
#define private public
#include "mastertimer_stub.h"
#include "audio_test.h"
#include "audio.h"
#include "doc.h"
#undef private
#undef protected

static QString saveToXml(const Audio &a)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    a.saveXML(&xmlWriter);
    xmlWriter.setDevice(nullptr);
    buffer.close();
    return QString::fromUtf8(buffer.data());
}

static bool loadFromXml(Audio &a, const QString &xml)
{
    QByteArray data = xml.toUtf8();
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    return a.loadXML(xmlReader);
}

void Audio_Test::initTestCase()
{
    m_doc = new Doc(this);
}

void Audio_Test::cleanupTestCase()
{
    delete m_doc;
}

void Audio_Test::cleanup()
{
    m_doc->clearContents();
}

void Audio_Test::basic()
{
    Audio a(m_doc);
    QCOMPARE(a.type(), Function::AudioType);
    QCOMPARE(a.name(), QString("New Audio"));
    QCOMPARE(a.volume(), 1.0);
    QCOMPARE(a.muted(), false);
    QCOMPARE(a.effectiveVolume(), 1.0);
}

void Audio_Test::volumeMuted()
{
    Audio a(m_doc);
    a.setVolume(0.5);
    QCOMPARE(a.effectiveVolume(), 0.5);
    a.setMuted(true);
    QCOMPARE(a.volume(), 0.5);
    QCOMPARE(a.effectiveVolume(), 0.0);
    a.setMuted(false);
    QCOMPARE(a.effectiveVolume(), 0.5);

    // copies carry volume and mute over
    a.setMuted(true);
    Audio b(m_doc);
    QVERIFY(b.copyFrom(&a));
    QCOMPARE(b.volume(), 0.5);
    QCOMPARE(b.muted(), true);
}

void Audio_Test::saveLoadVolumeMuted()
{
    // defaults: neither attribute is written
    Audio def(m_doc);
    QString defXml = saveToXml(def);
    QVERIFY(defXml.contains("Volume=") == false);
    QVERIFY(defXml.contains("Muted=") == false);

    Audio a(m_doc);
    a.setVolume(0.25);
    a.setMuted(true);
    QString xml = saveToXml(a);
    QVERIFY(xml.contains("Volume=\"0.25\""));
    QVERIFY(xml.contains("Muted=\"1\""));

    Audio a2(m_doc);
    QVERIFY(loadFromXml(a2, xml));
    QCOMPARE(a2.volume(), 0.25);
    QCOMPARE(a2.muted(), true);
    QCOMPARE(a2.effectiveVolume(), 0.0);
}

QTEST_MAIN(Audio_Test)
