/*
  Q Light Controller - Unit tests
  qlcfixturedefcache_test.cpp

  Copyright (C) Heikki Junnila

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

#define private public
#define protected public
#include "qlcfixturedefcache.h"
#include "qlcfixturedef.h"
#undef protected
#undef private

#include "qlcfixturedefcache_test.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "qlcconfig.h"
#include "qlcfile.h"

#include "../common/resource_paths.h"

/** Write @p content into @p path, creating/truncating the file */
static bool writeTextFile(const QString &path, const QString &content)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text) == false)
        return false;
    file.write(content.toUtf8());
    file.close();
    return true;
}

/** Build a bare "Manufacturer / Model" definition with one channel and,
    optionally, one mode using that channel */
static QLCFixtureDef *makeDef(const QString &manufacturer, const QString &model,
                              bool withMode)
{
    QLCFixtureDef *def = new QLCFixtureDef();
    def->setManufacturer(manufacturer);
    def->setModel(model);

    QLCChannel *ch = new QLCChannel();
    ch->setName("Dimmer");
    ch->setGroup(QLCChannel::Intensity);
    def->addChannel(ch);

    if (withMode)
    {
        QLCFixtureMode *mode = new QLCFixtureMode(def);
        mode->setName("Mode 1");
        mode->insertChannel(ch, 0);
        def->addMode(mode);
    }

    return def;
}

void QLCFixtureDefCache_Test::init()
{
    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(cache.load(QDir("/just/kidding/stoopid")) == false);
    QVERIFY(cache.loadMap(dir) == true);
}

void QLCFixtureDefCache_Test::cleanup()
{
    cache.clear();
}

void QLCFixtureDefCache_Test::duplicates()
{
    // Check that duplicates are discarded
    int num = cache.m_defs.size();
    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    cache.load(dir);
    QCOMPARE(cache.m_defs.size(), num);
}

void QLCFixtureDefCache_Test::add()
{
    QVERIFY(cache.fixtureCache().isEmpty() == false);
    QVERIFY(cache.fixtureCache()["Martin"]["MAC250"] == false);

    QVERIFY(cache.addFixtureDef(NULL) == false);

    QVERIFY(cache.manufacturers().count() != 0);
    cache.clear();
    QVERIFY(cache.manufacturers().count() == 0);

    /* Add the first fixtureDef */
    QLCFixtureDef* def = new QLCFixtureDef();
    def->setManufacturer("Martin");
    def->setModel("MAC250");

    QVERIFY(cache.addFixtureDef(def) == true);
    QVERIFY(cache.manufacturers().count() == 1);
    QVERIFY(cache.manufacturers().contains("Martin") == true);
    QVERIFY(cache.manufacturers().contains("MAC250") == false);

    QVERIFY(cache.models("Martin").count() == 1);
    QVERIFY(cache.models("Martin").contains("MAC250") == true);
    QVERIFY(cache.models("Foo").count() == 0);

    /* Another fixtureDef, same manufacturer & model. Should be ignored. */
    QLCFixtureDef* def2 = new QLCFixtureDef();
    def2->setManufacturer("Martin");
    def2->setModel("MAC250");

    QVERIFY(cache.addFixtureDef(def2) == false);
    QVERIFY(cache.manufacturers().count() == 1);
    QVERIFY(cache.manufacturers().contains("Martin") == true);

    delete def2;
    def2 = NULL;

    /* Another fixtureDef, same manufacturer, different model */
    def2 = new QLCFixtureDef();
    def2->setManufacturer("Martin");
    def2->setModel("MAC500");

    QVERIFY(cache.addFixtureDef(def2) == true);
    QVERIFY(cache.manufacturers().count() == 1);
    QVERIFY(cache.manufacturers().contains("Martin") == true);
    QVERIFY(cache.manufacturers().contains("MAC500") == false);

    QVERIFY(cache.models("Martin").count() == 2);
    QVERIFY(cache.models("Martin").contains("MAC250") == true);
    QVERIFY(cache.models("Martin").contains("MAC500") == true);

    /* Another fixtureDef, different manufacturer, different model */
    QLCFixtureDef* def3 = new QLCFixtureDef();
    def3->setManufacturer("Futurelight");
    def3->setModel("PHS700");

    QVERIFY(cache.addFixtureDef(def3) == true);
    QVERIFY(cache.manufacturers().count() == 2);
    QVERIFY(cache.manufacturers().contains("Martin") == true);
    QVERIFY(cache.manufacturers().contains("Futurelight") == true);
    QVERIFY(cache.manufacturers().contains("PHS700") == false);

    /* Another fixtureDef, different manufacturer, same model */
    QLCFixtureDef* def4 = new QLCFixtureDef();
    def4->setManufacturer("Yoyodyne");
    def4->setModel("MAC250");

    QVERIFY(cache.addFixtureDef(def4) == true);
    QVERIFY(cache.manufacturers().count() == 3);
    QVERIFY(cache.manufacturers().contains("Martin") == true);
    QVERIFY(cache.manufacturers().contains("Futurelight") == true);
    QVERIFY(cache.manufacturers().contains("Yoyodyne") == true);
    QVERIFY(cache.manufacturers().contains("MAC250") == false);

    QVERIFY(cache.models("Yoyodyne").count() == 1);
    QVERIFY(cache.models("Yoyodyne").contains("MAC250") == true);
}

void QLCFixtureDefCache_Test::reload()
{
    QLCFixtureDef *def = cache.fixtureDef("Botex", "SP-1500");
    QLCChannel *channel = def->channel("Control");

    QVERIFY(def->channels().count() == 5);
    def->removeChannel(channel);
    QVERIFY(def->channels().count() == 4);

    // reloadFixtureDef() deletes the old QLCFixtureDef and inserts a brand new
    // instance into the cache (see fixtureeditor.cpp's callers, which always
    // re-fetch by manufacturer/model afterwards rather than reusing their old
    // pointer) - do the same here instead of dereferencing the now-dangling
    // "def" pointer.
    QVERIFY(cache.reloadFixtureDef(def) == true);
    def = cache.fixtureDef("Botex", "SP-1500");
    QVERIFY(def->channels().count() == 5);
}

void QLCFixtureDefCache_Test::fixtureDef()
{
    // check the content of a cached fixture relative path
    QString firstManufacturer = cache.m_defs.first()->manufacturer();
    QString firstModel = cache.m_defs.first()->model();
    QString relPath = QString("%1/%1-%2.qxf").arg(firstManufacturer).arg(firstModel);
    relPath.replace(" ", "-");
    QVERIFY(cache.m_defs.first()->definitionSourceFile() == relPath);

    // request a fixture cached but not yet loaded
    QLCFixtureDef *def = cache.fixtureDef("Futurelight", "CY-200");

    // check that once loaded, the relative path becomes absolute
    QDir absDir(def->definitionSourceFile());
    QVERIFY(absDir.isAbsolute() == true);

    cache.clear();

    QLCFixtureDef *def1 = new QLCFixtureDef();
    def1->setManufacturer("Martin");
    def1->setModel("MAC250");
    cache.addFixtureDef(def1);

    QLCFixtureDef *def2 = new QLCFixtureDef();
    def2->setManufacturer("Martin");
    def2->setModel("MAC500");
    cache.addFixtureDef(def2);

    QLCFixtureDef *def3 = new QLCFixtureDef();
    def3->setManufacturer("Robe");
    def3->setModel("WL250");
    cache.addFixtureDef(def3);

    QLCFixtureDef *def4 = new QLCFixtureDef();
    def4->setManufacturer("Futurelight");
    def4->setModel("DJ Scan 250");
    cache.addFixtureDef(def4);

    QVERIFY(cache.fixtureDef("Martin", "MAC250") == def1);
    QVERIFY(cache.fixtureDef("Martin", "MAC500") == def2);
    QVERIFY(cache.fixtureDef("Robe", "WL250") == def3);
    QVERIFY(cache.fixtureDef("Futurelight", "DJ Scan 250") == def4);
    QVERIFY(cache.fixtureDef("Martin", "MAC 250") == NULL);
    QVERIFY(cache.fixtureDef("Mar tin", "MAC250") == NULL);
    QVERIFY(cache.fixtureDef("Foobar", "Foobar") == NULL);
    QVERIFY(cache.fixtureDef("", "") == NULL);
}

void QLCFixtureDefCache_Test::load()
{
    /* At least these should be available */
    QVERIFY(cache.manufacturers().contains("Elation") == true);
    QVERIFY(cache.manufacturers().contains("Eurolite") == true);
    QVERIFY(cache.manufacturers().contains("Futurelight") == true);
    QVERIFY(cache.manufacturers().contains("GLP") == true);
    QVERIFY(cache.manufacturers().contains("JB-Lighting") == true);
    QVERIFY(cache.manufacturers().contains("Lite-Works") == true);
    QVERIFY(cache.manufacturers().contains("Martin") == true);
    QVERIFY(cache.manufacturers().contains("Robe") == true);
    QVERIFY(cache.manufacturers().contains("SGM") == true);

    QString loadPath = QString("%1/Futurelight/Futurelight-CY-200.qxf").arg(INTERNAL_FIXTUREDIR);
    QVERIFY(cache.loadQXF(loadPath) == true);
    QVERIFY(cache.loadQXF("Foo/Baz.qxf") == false);
    QVERIFY(cache.loadD4("QLC/Plus.d4") == false);
}

void QLCFixtureDefCache_Test::defDirectories()
{
    QDir dir = QLCFixtureDefCache::systemDefinitionDirectory();

    QVERIFY(dir.filter() & QDir::Files);
    QVERIFY(dir.nameFilters().contains(QString("*%1").arg(KExtFixture)));
#if defined(__APPLE__) || defined(Q_OS_MAC)
    // In a real .app bundle the executable lives in Contents/MacOS/ and
    // resources in Contents/Resources/ - siblings one level up - so
    // QLCFile::systemDirectory()'s APPLE branch (qlcfile.cpp) intentionally
    // inserts "/..". Mirrors the equivalent guard in
    // engine/test/inputoutputmap/inputoutputmap_test.cpp's profileDirectories()
    // and engine/test/rgbscript/rgbscript_test.cpp's directories().
    QString path("%1/../%2");
    QCOMPARE(dir.path(), path.arg(QCoreApplication::applicationDirPath())
                             .arg(FIXTUREDIR));
#else
    QDir fxDir;
    fxDir.setPath(FIXTUREDIR);
    QCOMPARE(dir.absolutePath(), fxDir.absolutePath());
#endif

    dir = QLCFixtureDefCache::userDefinitionDirectory();
#ifndef SKIP_TEST
    QVERIFY(dir.exists() == true);
#endif
    QVERIFY(dir.filter() & QDir::Files);
    QVERIFY(dir.nameFilters().contains(QString("*%1").arg(KExtFixture)));
    QVERIFY(dir.absolutePath().contains(USERFIXTUREDIR));

}

void QLCFixtureDefCache_Test::storeDef()
{
    QLCFixtureDef *def = cache.fixtureDef("Futurelight", "CY-200");
    QFile defFile(def->definitionSourceFile());
    QVERIFY(defFile.open(QIODevice::ReadOnly | QIODevice::Text));
    QString defBuffer = defFile.readAll();
    defFile.close();
    QVERIFY(cache.storeFixtureDef("storeTest.qxf", defBuffer) == true);

    QDir dir = QLCFixtureDefCache::userDefinitionDirectory();
    QFile file (dir.absoluteFilePath("storeTest.qxf"));
    file.remove();
}

void QLCFixtureDefCache_Test::storeDefFailure()
{
    // The file name is resolved inside the user definition directory: a
    // sub-directory that doesn't exist there can't be opened for writing
    QLCFixtureDefCache c;
    QVERIFY(c.storeFixtureDef("no-such-subdir/storeTest.qxf", "<Foo/>") == false);
    QVERIFY(c.m_defs.isEmpty());
}

void QLCFixtureDefCache_Test::reloadFailures()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QLCFixtureDefCache c;

    /* A definition the cache doesn't know */
    QLCFixtureDef *stranger = makeDef("Foo", "Stranger", true);
    QVERIFY(c.reloadFixtureDef(stranger) == false);
    delete stranger;

    /* Cached, but its source file has gone: the stale instance is dropped */
    QLCFixtureDef *missing = makeDef("Foo", "Missing", true);
    missing->setDefinitionSourceFile(tmp.filePath("missing.qxf"));
    QVERIFY(c.addFixtureDef(missing) == true);
    QVERIFY(c.reloadFixtureDef(missing) == false);
    QVERIFY(c.fixtureDef("Foo", "Missing") == NULL);
    QVERIFY(c.m_defs.isEmpty());

    /* Cached and the file exists, but it defines no mode any more */
    QScopedPointer<QLCFixtureDef> noModes(makeDef("Foo", "NoModes", false));
    QCOMPARE(noModes->saveXML(tmp.filePath("nomodes.qxf")), QFile::NoError);

    QLCFixtureDef *cached = makeDef("Foo", "NoModes", true);
    cached->setDefinitionSourceFile(tmp.filePath("nomodes.qxf"));
    QVERIFY(c.addFixtureDef(cached) == true);
    QVERIFY(c.reloadFixtureDef(cached) == false);
    QVERIFY(c.fixtureDef("Foo", "NoModes") == NULL);
    QVERIFY(c.m_defs.isEmpty());

    QVERIFY(tmp.remove());
}

void QLCFixtureDefCache_Test::reloadOrAdd()
{
    QLCFixtureDefCache c;
    QScopedPointer<QLCFixtureDef> editor(makeDef("Foo", "Bar", true));

    /* 1. Not cached yet: a detached copy is added, flagged as a loaded user def */
    QVERIFY(c.reloadOrAddFixtureDef(editor.data()) == true);
    QCOMPARE(c.m_defs.size(), 1);
    QLCFixtureDef *cached = c.m_defs.first();
    QVERIFY(cached != editor.data());
    QVERIFY(cached->isUser() == true);
    QVERIFY(cached->m_isLoaded == true);
    QCOMPARE(cached->manufacturer(), QString("Foo"));
    QCOMPARE(cached->model(), QString("Bar"));
    QCOMPARE(cached->channels().size(), 1);
    QCOMPARE(cached->modes().size(), 1);

    /* 2. A different instance with the same manufacturer/model is cached:
          its contents are replaced in place, the instance survives */
    QLCChannel *pan = new QLCChannel();
    pan->setName("Pan");
    pan->setGroup(QLCChannel::Pan);
    editor->addChannel(pan);
    QVERIFY(c.reloadOrAddFixtureDef(editor.data()) == true);
    QCOMPARE(c.m_defs.size(), 1);
    QVERIFY(c.m_defs.first() == cached);
    QVERIFY(cached->isUser() == true);
    QVERIFY(cached->m_isLoaded == true);
    QCOMPARE(cached->channels().size(), 2);
    QVERIFY(cached->channel("Pan") != NULL);
    QVERIFY(cached->channel("Pan") != pan);

    /* 3. The very instance handed in already sits in the cache: it is
          swapped for a detached copy so editor and cache never share it */
    c.clear();
    QLCFixtureDef *shared = new QLCFixtureDef(editor.data());
    QVERIFY(c.addFixtureDef(shared) == true);
    QVERIFY(c.reloadOrAddFixtureDef(shared) == true);
    QCOMPARE(c.m_defs.size(), 1);
    QVERIFY(c.m_defs.first() != shared);
    QVERIFY(c.m_defs.first()->isUser() == true);
    QVERIFY(c.m_defs.first()->m_isLoaded == true);
    QCOMPARE(c.m_defs.first()->model(), QString("Bar"));
    QCOMPARE(c.m_defs.first()->channels().size(), 2);
    delete shared;
}

void QLCFixtureDefCache_Test::loadDirectory()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    dir.setFilter(QDir::Files);

    /* A complete definition, one without modes, two identical Avolites
       personalities and a file with an unknown extension */
    QScopedPointer<QLCFixtureDef> full(makeDef("Foo", "Full", true));
    QCOMPARE(full->saveXML(dir.absoluteFilePath("full.qxf")), QFile::NoError);
    QScopedPointer<QLCFixtureDef> noModes(makeDef("Foo", "NoModes", false));
    QCOMPARE(noModes->saveXML(dir.absoluteFilePath("nomodes.qxf")), QFile::NoError);

    const QString d4("<Fixture Name=\"Mini\" Company=\"Avo\"/>\n");
    QVERIFY(writeTextFile(dir.absoluteFilePath("one.d4"), d4));
    QVERIFY(writeTextFile(dir.absoluteFilePath("two.d4"), d4));
    QVERIFY(writeTextFile(dir.absoluteFilePath("three.txt"), "not a fixture"));

    QLCFixtureDefCache c;
    QVERIFY(c.load(dir) == true);
    QCOMPARE(c.m_defs.size(), 2);

    QLCFixtureDef *def = c.fixtureDef("Foo", "Full");
    QVERIFY(def != NULL);
    QVERIFY(def->isUser() == true);
    QCOMPARE(def->definitionSourceFile(), dir.absoluteFilePath("full.qxf"));
    QVERIFY(c.fixtureDef("Foo", "NoModes") == NULL);

    def = c.fixtureDef("Avo", "Mini");
    QVERIFY(def != NULL);
    QVERIFY(def->isUser() == true);
    QCOMPARE(def->definitionSourceFile(), dir.absoluteFilePath("one.d4"));

    /* The duplicate personality must not be added a second time */
    QVERIFY(c.loadD4(dir.absoluteFilePath("two.d4")) == true);
    QCOMPARE(c.m_defs.size(), 2);

    /* A definition without modes is refused on its own as well */
    QVERIFY(c.loadQXF(dir.absoluteFilePath("nomodes.qxf")) == false);
    QCOMPARE(c.m_defs.size(), 2);

    // every loader must have released its file, or the directory can't go
    c.clear();
    QVERIFY(tmp.remove());
}

void QLCFixtureDefCache_Test::loadMapFailures()
{
    QLCFixtureDefCache c;
    QVERIFY(c.loadMap(QDir("/just/kidding/stoopid")) == false);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    const QString mapPath(dir.absoluteFilePath("FixturesMap.xml"));

    /* The directory exists but holds no map at all */
    QVERIFY(c.loadMap(dir) == false);
    QCOMPARE(c.m_mapAbsolutePath, dir.absolutePath());

    /* Malformed XML: the error surfaces before any DTD is found */
    QVERIFY(writeTextFile(mapPath, "<?xml version=\"1.0\"?>\n<<<"));
    QVERIFY(c.loadMap(dir) == false);

    /* Some other document type */
    QVERIFY(writeTextFile(mapPath, "<!DOCTYPE Workspace>\n<Workspace/>\n"));
    QVERIFY(c.loadMap(dir) == false);

    /* The right document type but no root element at all */
    QVERIFY(writeTextFile(mapPath, "<!DOCTYPE FixturesMap>\n"));
    QVERIFY(c.loadMap(dir) == false);

    /* The right document type with a foreign root element */
    QVERIFY(writeTextFile(mapPath, "<!DOCTYPE FixturesMap>\n<Foo/>\n"));
    QVERIFY(c.loadMap(dir) == false);

    QVERIFY(c.m_defs.isEmpty());
    QVERIFY(tmp.remove());
}

void QLCFixtureDefCache_Test::loadMapContent()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());

    /* A duplicate entry, an entry without a model, an unknown tag inside a
       manufacturer and an unknown top-level tag must all be tolerated */
    const QString map(
        "<!DOCTYPE FixturesMap>\n"
        "<FixturesMap>\n"
        " <M n=\"Foo_Bar\">\n"
        "  <F n=\"Foo-Bar-One\" m=\"One\"/>\n"
        "  <F n=\"Foo-Bar-One\" m=\"One\"/>\n"
        "  <F n=\"NoModel\"/>\n"
        "  <Unknown/>\n"
        " </M>\n"
        " <Bogus/>\n"
        "</FixturesMap>\n");
    QVERIFY(writeTextFile(dir.absoluteFilePath("FixturesMap.xml"), map));

    QLCFixtureDefCache c;
    QVERIFY(c.loadMap(dir) == true);
    QCOMPARE(c.m_defs.size(), 1);

    QLCFixtureDef *def = c.m_defs.first();
    QCOMPARE(def->manufacturer(), QString("Foo Bar"));
    QCOMPARE(def->model(), QString("One"));
    QCOMPARE(def->definitionSourceFile(), QString("Foo_Bar/Foo-Bar-One.qxf"));
    QVERIFY(def->isUser() == false);

    // the map file must have been released again
    c.clear();
    QVERIFY(tmp.remove());
}

// QLCFixtureDefCache::systemDefinitionDirectory()/userDefinitionDirectory()
// (via QLCFile::systemDirectory()) call QCoreApplication::applicationDirPath(),
// which needs a live QCoreApplication instance - QTEST_APPLESS_MAIN doesn't
// construct one, so that call silently warned and returned an empty path on
// both Windows and macOS. QTEST_GUILESS_MAIN constructs a QCoreApplication
// (no GUI needed) - same fix already used for beattracker_test/universeperf_test.
QTEST_GUILESS_MAIN(QLCFixtureDefCache_Test)
