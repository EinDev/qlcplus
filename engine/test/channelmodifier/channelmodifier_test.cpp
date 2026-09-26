/*
  Q Light Controller Plus - Unit test
  channelmodifier_test.cpp

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
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QFile>

#define protected public
#define private public
#include "channelmodifier_test.h"
#include "channelmodifier.h"
#include "qlcfile.h"
#undef private
#undef protected

void ChannelModifier_Test::initial()
{
    ChannelModifier mod;
    QCOMPARE(mod.name(), QString());
    QCOMPARE(mod.type(), ChannelModifier::UserTemplate);
    QVERIFY(mod.modifierMap().isEmpty());
    for (int i = 0; i < 256; ++i)
        QCOMPARE(mod.getValue(i), uchar(0));
}

void ChannelModifier_Test::nameType()
{
    ChannelModifier mod;
    mod.setName("MyMod");
    mod.setType(ChannelModifier::SystemTemplate);
    QCOMPARE(mod.name(), QString("MyMod"));
    QCOMPARE(mod.type(), ChannelModifier::SystemTemplate);
}

void ChannelModifier_Test::modifierMap()
{
    ChannelModifier mod;
    QList<QPair<uchar, uchar> > map;
    map << QPair<uchar,uchar>(0,0)
        << QPair<uchar,uchar>(128,255)
        << QPair<uchar,uchar>(255,255);
    mod.setModifierMap(map);
    QCOMPARE(mod.modifierMap().size(), 3);
    QCOMPARE(mod.getValue(0), uchar(0));
    QCOMPARE(mod.getValue(64), uchar(127));
    QCOMPARE(mod.getValue(200), uchar(255));
}

void ChannelModifier_Test::saveLoad()
{
    ChannelModifier mod;
    QList<QPair<uchar, uchar> > map;
    map << QPair<uchar,uchar>(0,0) << QPair<uchar,uchar>(255,255);
    mod.setModifierMap(map);
    mod.setName("SaveMod");
    mod.setType(ChannelModifier::SystemTemplate);

    QString path("channelmodifier.xml");
    QCOMPARE(mod.saveXML(path), QFile::NoError);

    ChannelModifier mod2;
    QCOMPARE(mod2.loadXML(path, ChannelModifier::SystemTemplate), QFile::NoError);
    QCOMPARE(mod2.name(), QString("SaveMod"));
    QCOMPARE(mod2.type(), ChannelModifier::SystemTemplate);
    QCOMPARE(mod2.getValue(127), uchar(127));

    QFile::remove(path);
}

static QString writeTemplate(const QTemporaryDir &dir, const QString &name, const QByteArray &content)
{
    QFile f(dir.filePath(name));
    if (f.open(QIODevice::WriteOnly) == false)
        return QString();
    f.write(content);
    f.close();
    return f.fileName();
}

void ChannelModifier_Test::saveErrors()
{
    ChannelModifier mod;
    QCOMPARE(mod.saveXML(QString()), QFile::OpenError);

    // a directory that does not exist cannot be written into
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(mod.saveXML(dir.filePath("missing/subdir/mod.qxmt")) != QFile::NoError);
}

void ChannelModifier_Test::loadErrors()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ChannelModifier mod;
    QCOMPARE(mod.loadXML(QString(), ChannelModifier::UserTemplate), QFile::OpenError);
    QCOMPARE(mod.loadXML(dir.filePath("nope.qxmt"), ChannelModifier::UserTemplate), QFile::ReadError);

    // not well-formed before the DTD is reached
    QString truncated = writeTemplate(dir, "truncated.qxmt", "<?xml version=\"1.0\"?><Broken");
    QVERIFY(truncated.isEmpty() == false);
    QCOMPARE(mod.loadXML(truncated, ChannelModifier::UserTemplate), QFile::ResourceError);

    // a DTD with no root element behind it
    QString empty = writeTemplate(dir, "empty.qxmt", "<?xml version=\"1.0\"?><!DOCTYPE ChannelModifier>");
    QVERIFY(empty.isEmpty() == false);
    QCOMPARE(mod.loadXML(empty, ChannelModifier::UserTemplate), QFile::ResourceError);

    // the wrong document type parses but yields nothing
    QString other = writeTemplate(dir, "other.qxmt",
        "<?xml version=\"1.0\"?><!DOCTYPE FixtureDefinition><FixtureDefinition><Name>X</Name></FixtureDefinition>");
    QCOMPARE(mod.loadXML(other, ChannelModifier::UserTemplate), QFile::NoError);
    QCOMPARE(mod.name(), QString());
    QVERIFY(mod.modifierMap().isEmpty());
}

void ChannelModifier_Test::loadUnknownTag()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QString path = writeTemplate(dir, "odd.qxmt",
        "<?xml version=\"1.0\"?><!DOCTYPE ChannelModifier>"
        "<ChannelModifier>"
        "<Creator><Name>Test</Name></Creator>"
        "<Name>Odd</Name>"
        "<Bogus><Nested/></Bogus>"
        "<Handler Original=\"0\" Modified=\"10\"/>"
        "<Handler Original=\"255\" Modified=\"20\"/>"
        "</ChannelModifier>");
    QVERIFY(path.isEmpty() == false);

    ChannelModifier mod;
    QCOMPARE(mod.loadXML(path, ChannelModifier::SystemTemplate), QFile::NoError);
    QCOMPARE(mod.name(), QString("Odd"));
    QCOMPARE(mod.type(), ChannelModifier::SystemTemplate);
    QCOMPARE(mod.modifierMap().count(), 2);
    QCOMPARE(mod.getValue(0), uchar(10));
    QCOMPARE(mod.getValue(255), uchar(20));
}

QTEST_APPLESS_MAIN(ChannelModifier_Test)
