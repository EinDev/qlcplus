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
#include "qlcconfig.h"
#include "qlcfile.h"

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

void QLCModifiersCache_Test::loadSystemTemplatesSkipsBrokenFile()
{
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());

    QList<QPair<uchar,uchar> > map;
    map << QPair<uchar,uchar>(0,0) << QPair<uchar,uchar>(255,255);

    ChannelModifier good;
    good.setName("Good");
    good.setModifierMap(map);
    QCOMPARE(good.saveXML(tmpDir.filePath("good.qxmt")), QFile::NoError);

    // right extension, unreadable content: reported and skipped
    QFile broken(tmpDir.filePath("broken.qxmt"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("<?xml version=\"1.0\"?><Nope");
    broken.close();

    QLCModifiersCache cache;
    QCOMPARE(cache.load(QDir(tmpDir.path()), true), true);
    QCOMPARE(cache.templateNames(), QList<QString>() << "Good");
    QCOMPARE(cache.modifier("Good")->type(), ChannelModifier::SystemTemplate);
}

/** Point the process' home directory at a scratch location for a scope */
struct ScopedHomeEnv
{
    QByteArray name;
    QByteArray previous;
    bool wasSet;

    explicit ScopedHomeEnv(const QString &home)
    {
#if defined(WIN32) || defined(Q_OS_WIN)
        name = "USERPROFILE";
#else
        name = "HOME";
#endif
        wasSet = qEnvironmentVariableIsSet(name.constData());
        previous = qgetenv(name.constData());
        qputenv(name.constData(), QDir::toNativeSeparators(home).toLocal8Bit());
    }

    ~ScopedHomeEnv()
    {
        if (wasSet)
            qputenv(name.constData(), previous);
        else
            qunsetenv(name.constData());
    }
};

void QLCModifiersCache_Test::templateDirectories()
{
    QDir system = QLCModifiersCache::systemTemplateDirectory();
    QVERIFY(system.filter() & QDir::Files);
    QCOMPARE(system.nameFilters(), QStringList() << QString("*%1").arg(KExtModifierTemplate));
    QVERIFY(system.path().contains(MODIFIERSTEMPLATEDIR));

    // the user directory is created under the home directory on demand
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QString homePath = QDir::cleanPath(home.path());
    ScopedHomeEnv env(homePath);

    QDir user = QLCModifiersCache::userTemplateDirectory();
    const QString expected = homePath + "/" + USERMODIFIERSTEMPLATEDIR;
    QVERIFY2(QDir::cleanPath(user.absolutePath()).compare(expected, Qt::CaseInsensitive) == 0,
             qPrintable(user.absolutePath()));
    QVERIFY(QDir(expected).exists());
    QCOMPARE(user.nameFilters(), QStringList() << QString("*%1").arg(KExtModifierTemplate));
}

// systemTemplateDirectory() builds its path from QCoreApplication::
// applicationDirPath() on Windows/macOS, which needs an application instance
QTEST_GUILESS_MAIN(QLCModifiersCache_Test)
