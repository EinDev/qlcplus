/*
  Q Light Controller Plus - Test Unit
  qlcmodifierscache_test.cpp

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
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#define private public
#include "qlcmodifierscache.h"
#undef private
#include "channelmodifier.h"
#include "qlcmodifierscache_test.h"

void QLCModifiersCache_Test::addAndRetrieve()
{
    QLCModifiersCache cache;
    ChannelModifier *m1 = new ChannelModifier();
    m1->setName("mod1");
    QVERIFY(cache.addModifier(m1) == true);
    QVERIFY(cache.addModifier(m1) == false);

    ChannelModifier *m2 = new ChannelModifier();
    m2->setName("mod2");
    QVERIFY(cache.addModifier(m2) == true);

    QList<QString> names = cache.templateNames();
    QCOMPARE(names.count(), 2);
    QVERIFY(names.contains("mod1"));
    QVERIFY(names.contains("mod2"));

    QCOMPARE(cache.modifier("mod1"), m1);
    QCOMPARE(cache.modifier("nonexist"), static_cast<ChannelModifier*>(nullptr));
}

void QLCModifiersCache_Test::loadFromNonExistentDirectoryFails()
{
    QLCModifiersCache cache;
    QDir dir("this/path/does/not/exist_qlcplus_modifierscache_test");
    QCOMPARE(cache.load(dir), false);
    QCOMPARE(cache.templateNames().count(), 0);
}

void QLCModifiersCache_Test::loadDirectoryPopulatesCache()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QList<QPair<uchar,uchar> > map;
    map << QPair<uchar,uchar>(0,0) << QPair<uchar,uchar>(255,255);

    ChannelModifier modA;
    modA.setName("ModA");
    modA.setModifierMap(map);
    QCOMPARE(modA.saveXML(tmpDir.filePath("moda.qxmt")), QFile::NoError);

    ChannelModifier modB;
    modB.setName("ModB");
    modB.setModifierMap(map);
    QCOMPARE(modB.saveXML(tmpDir.filePath("modb.qxmt")), QFile::NoError);

    // A file with a non-matching extension must be skipped, not crash the scan
    QFile stray(tmpDir.filePath("notamodifier.txt"));
    QVERIFY(stray.open(QIODevice::WriteOnly));
    stray.write("not a modifier template");
    stray.close();

    QLCModifiersCache cache;
    QDir dir(tmpDir.path());
    QCOMPARE(cache.load(dir), true);

    QList<QString> names = cache.templateNames();
    QCOMPARE(names.count(), 2);
    QVERIFY(names.contains("ModA"));
    QVERIFY(names.contains("ModB"));
    QVERIFY(cache.modifier("ModA") != nullptr);
    QCOMPARE(cache.modifier("ModA")->type(), ChannelModifier::UserTemplate);
}

void QLCModifiersCache_Test::loadIgnoresDuplicateNamedModifier()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QList<QPair<uchar,uchar> > map;
    map << QPair<uchar,uchar>(0,0) << QPair<uchar,uchar>(255,255);

    ChannelModifier modA;
    modA.setName("Dup");
    modA.setModifierMap(map);
    QCOMPARE(modA.saveXML(tmpDir.filePath("a_dup.qxmt")), QFile::NoError);

    ChannelModifier modB;
    modB.setName("Dup"); // same name as above, from a different file
    modB.setModifierMap(map);
    QCOMPARE(modB.saveXML(tmpDir.filePath("b_dup.qxmt")), QFile::NoError);

    QLCModifiersCache cache;
    QDir dir(tmpDir.path());
    QCOMPARE(cache.load(dir), true);

    // Only the first-loaded of the two same-named templates must survive
    QCOMPARE(cache.templateNames().count(), 1);
    QVERIFY(cache.modifier("Dup") != nullptr);
}

QTEST_APPLESS_MAIN(QLCModifiersCache_Test)
