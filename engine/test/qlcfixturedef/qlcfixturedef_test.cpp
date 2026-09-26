/*
  Q Light Controller - Unit tests
  qlcfixturedef_test.cpp

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

#define protected public
#include "qlcfixturedef.h"
#undef protected

#include "qlcfixturedef_test.h"
#include "qlcfixturemode.h"
#include "qlcchannel.h"
#include "fixture.h"
#include "qlcfile.h"

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

void QLCFixtureDef_Test::initial()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QVERIFY(fd->manufacturer().isEmpty());
    QVERIFY(fd->model().isEmpty());
    QVERIFY(fd->name() == " ");
    QVERIFY(fd->typeToString(fd->type()) == "Dimmer");
    delete fd;
}

void QLCFixtureDef_Test::manufacturer()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    fd->setManufacturer("Martin");
    QVERIFY(fd->manufacturer() == "Martin");
    QVERIFY(fd->name() == "Martin ");
    delete fd;
}

void QLCFixtureDef_Test::model()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    fd->setModel("MAC600");
    QVERIFY(fd->model() == "MAC600");
    QVERIFY(fd->name() == " MAC600");
    delete fd;
}

void QLCFixtureDef_Test::name()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    fd->setManufacturer("Martin");
    fd->setModel("MAC600");
    QVERIFY(fd->name() == "Martin MAC600");
    delete fd;
}

void QLCFixtureDef_Test::type()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    fd->setType(QLCFixtureDef::Scanner);
    QVERIFY(fd->typeToString(fd->type()) == "Scanner");
    delete fd;
}

void QLCFixtureDef_Test::addChannel()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QVERIFY(fd->channels().size() == 0);

    fd->addChannel(NULL);
    QVERIFY(fd->channels().size() == 0);

    QLCChannel* ch1 = new QLCChannel();
    fd->addChannel(ch1);
    QVERIFY(fd->channels().size() == 1);
    QVERIFY(fd->channels().at(0) == ch1);

    fd->addChannel(ch1);
    QVERIFY(fd->channels().size() == 1);

    QLCChannel* ch2 = new QLCChannel();
    fd->addChannel(ch2);
    QVERIFY(fd->channels().size() == 2);
    QVERIFY(fd->channels().at(0) == ch1);
    QVERIFY(fd->channels().at(1) == ch2);

    delete fd;
}

void QLCFixtureDef_Test::removeChannel()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QLCChannel* ch1 = new QLCChannel();
    QLCChannel* ch2 = new QLCChannel();

    QVERIFY(fd->channels().size() == 0);
    QVERIFY(fd->removeChannel(NULL) == false);
    QVERIFY(fd->removeChannel(ch1) == false);
    QVERIFY(fd->removeChannel(ch2) == false);

    fd->addChannel(ch1);
    fd->addChannel(ch2);
    QVERIFY(fd->channels().size() == 2);

    QVERIFY(fd->removeChannel(ch1) == true);
    QVERIFY(fd->channels().size() == 1);
    QVERIFY(fd->channels().at(0) == ch2);

    QVERIFY(fd->removeChannel(ch1) == false);
    QVERIFY(fd->channels().size() == 1);
    QVERIFY(fd->channels().at(0) == ch2);

    QVERIFY(fd->removeChannel(NULL) == false);
    QVERIFY(fd->channels().size() == 1);
    QVERIFY(fd->channels().at(0) == ch2);

    QVERIFY(fd->removeChannel(ch2) == true);
    QVERIFY(fd->channels().size() == 0);

    delete fd;
}

void QLCFixtureDef_Test::channel()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QLCChannel* ch1 = new QLCChannel();
    ch1->setName("foo");
    fd->addChannel(ch1);

    QLCChannel* ch2 = new QLCChannel();
    ch2->setName("bar");
    fd->addChannel(ch2);

    QLCChannel* ch3 = new QLCChannel();
    ch3->setName("xyzzy");
    fd->addChannel(ch3);

    QVERIFY(fd->channel("foo") == ch1);
    QVERIFY(fd->channel("bar") == ch2);
    QVERIFY(fd->channel("xyzzy") == ch3);
    QVERIFY(fd->channel("foobar") == NULL);

    delete fd;
}

void QLCFixtureDef_Test::channels()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QLCChannel* ch1 = new QLCChannel();
    QLCChannel* ch2 = new QLCChannel();
    QLCChannel* ch3 = new QLCChannel();

    QVERIFY(fd->channels().size() == 0);
    fd->addChannel(ch1);
    QVERIFY(fd->channels().size() == 1);
    QVERIFY(fd->channels().at(0) == ch1);
    fd->addChannel(ch2);
    QVERIFY(fd->channels().size() == 2);
    QVERIFY(fd->channels().at(0) == ch1);
    QVERIFY(fd->channels().at(1) == ch2);
    fd->addChannel(ch3);
    QVERIFY(fd->channels().size() == 3);
    QVERIFY(fd->channels().at(0) == ch1);
    QVERIFY(fd->channels().at(1) == ch2);
    QVERIFY(fd->channels().at(2) == ch3);

    delete fd;
}

void QLCFixtureDef_Test::addMode()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QLCFixtureMode* mode1 = new QLCFixtureMode(fd);
    QLCFixtureMode* mode2 = new QLCFixtureMode(fd);

    QVERIFY(fd->modes().size() == 0);

    fd->addMode(NULL);
    QVERIFY(fd->modes().size() == 0);

    fd->addMode(mode1);
    QVERIFY(fd->modes().size() == 1);
    QVERIFY(fd->modes().at(0) == mode1);

    fd->addMode(mode1);
    QVERIFY(fd->modes().size() == 1);
    QVERIFY(fd->modes().at(0) == mode1);

    fd->addMode(mode2);
    QVERIFY(fd->modes().size() == 2);
    QVERIFY(fd->modes().at(0) == mode1);
    QVERIFY(fd->modes().at(1) == mode2);

    delete fd;
}

void QLCFixtureDef_Test::removeMode()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QLCFixtureMode* mode1 = new QLCFixtureMode(fd);
    QLCFixtureMode* mode2 = new QLCFixtureMode(fd);

    QVERIFY(fd->modes().size() == 0);
    QVERIFY(fd->removeMode(NULL) == false);
    QVERIFY(fd->removeMode(mode1) == false);
    QVERIFY(fd->removeMode(mode2) == false);
    QVERIFY(fd->modes().size() == 0);

    fd->addMode(mode1);
    fd->addMode(mode2);
    QVERIFY(fd->modes().size() == 2);

    QVERIFY(fd->removeMode(mode1) == true);
    QVERIFY(fd->modes().size() == 1);
    QVERIFY(fd->modes().at(0) == mode2);

    QVERIFY(fd->removeMode(mode1) == false);
    QVERIFY(fd->modes().size() == 1);
    QVERIFY(fd->modes().at(0) == mode2);

    QVERIFY(fd->removeMode(NULL) == false);
    QVERIFY(fd->modes().size() == 1);
    QVERIFY(fd->modes().at(0) == mode2);

    QVERIFY(fd->removeMode(mode2) == true);
    QVERIFY(fd->modes().size() == 0);

    delete fd;
}

void QLCFixtureDef_Test::mode()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QLCFixtureMode* mode1 = new QLCFixtureMode(fd);
    mode1->setName("foo");
    fd->addMode(mode1);

    QLCFixtureMode* mode2 = new QLCFixtureMode(fd);
    mode2->setName("bar");
    fd->addMode(mode2);

    QLCFixtureMode* mode3 = new QLCFixtureMode(fd);
    mode3->setName("xyzzy");
    fd->addMode(mode3);

    QVERIFY(fd->mode("foo") == mode1);
    QVERIFY(fd->mode("bar") == mode2);
    QVERIFY(fd->mode("xyzzy") == mode3);
    QVERIFY(fd->mode("foobar") == NULL);

    delete fd;
}

void QLCFixtureDef_Test::modes()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    QLCFixtureMode* mode1 = new QLCFixtureMode(fd);
    QLCFixtureMode* mode2 = new QLCFixtureMode(fd);
    QLCFixtureMode* mode3 = new QLCFixtureMode(fd);

    QVERIFY(fd->modes().size() == 0);
    fd->addMode(mode1);
    QVERIFY(fd->modes().size() == 1);
    QVERIFY(fd->modes().at(0) == mode1);
    fd->addMode(mode2);
    QVERIFY(fd->modes().size() == 2);
    QVERIFY(fd->modes().at(0) == mode1);
    QVERIFY(fd->modes().at(1) == mode2);
    fd->addMode(mode3);
    QVERIFY(fd->modes().size() == 3);
    QVERIFY(fd->modes().at(0) == mode1);
    QVERIFY(fd->modes().at(1) == mode2);
    QVERIFY(fd->modes().at(2) == mode3);

    delete fd;
}

void QLCFixtureDef_Test::copy()
{
    QLCFixtureDef* fd = new QLCFixtureDef();
    fd->setManufacturer("Martin");
    fd->setModel("MAC600");
    fd->setType(QLCFixtureDef::MovingHead);

    QLCChannel* ch = new QLCChannel();
    ch->setName("TestChannel");
    fd->addChannel(ch);

    QLCFixtureMode* mode = new QLCFixtureMode(fd);
    mode->setName("TestMode");
    fd->addMode(mode);
    mode->insertChannel(ch, 0);

    QLCFixtureDef* copy = new QLCFixtureDef(fd);
    QVERIFY(copy->manufacturer() == "Martin");
    QVERIFY(copy->model() == "MAC600");
    QVERIFY(copy->typeToString(copy->type()) == "Moving Head");

    /* Verify that modes and channels get copied and that the channels in
       the copied mode are from the copied fixtureDef and not the one that
       the copy is taken FROM. */
    QVERIFY(copy->channels().at(0)->name() == "TestChannel");
    QVERIFY(copy->modes().at(0)->name() == "TestMode");
    QVERIFY(copy->modes().at(0)->channels().size() == 1);
    QVERIFY(copy->modes().size() == 1);
    QVERIFY(copy->modes().at(0)->channel(0) != ch);
    QVERIFY(copy->modes().at(0)->channel(0) == copy->channels().at(0));
    QVERIFY(copy->channels().at(0)->name() == "TestChannel");
    QVERIFY(copy->modes().at(0)->channel(0)->name() == "TestChannel");

    delete fd;
    delete copy;
}

void QLCFixtureDef_Test::saveLoadXML()
{
    const QString path("qlcfixturedef_test_saveXML.qxf");

    QLCFixtureDef* def = new QLCFixtureDef;
    def->setManufacturer("Foobar");
    def->setModel("Xyzzy");
    def->setType(QLCFixtureDef::Other);

    QLCChannel* ch = new QLCChannel();
    ch->setName("Whatever");
    def->addChannel(ch);

    QLCFixtureMode* mode = new QLCFixtureMode(def);
    mode->setName("Barfoo");
    def->addMode(mode);
    mode->insertChannel(ch, 0);

    QVERIFY(def->saveXML(QString("zxcvb:/path/to/nowhere") + path) != QFile::NoError);
    QCOMPARE(def->saveXML(path), QFile::NoError);

    // Test only QLCFixtureDef's doings and don't go into channel/mode details
    // since they are tested in their individual unit tests.
    QLCFixtureDef* def2 = new QLCFixtureDef;
    QCOMPARE(def2->loadXML(QString()), QFile::OpenError);
    QCOMPARE(def2->loadXML("/path/beyond/this/universe/foo.qxf"), QFile::ReadError);
    QCOMPARE(def2->loadXML("readonly.xml"), QFile::ReadError);

    QCOMPARE(def2->loadXML(path), QFile::NoError);
    QCOMPARE(def2->manufacturer(), def->manufacturer());
    QCOMPARE(def2->model(), def->model());
    QCOMPARE(def2->channels().size(), 1);
    QCOMPARE(def2->channels().at(0)->name(), ch->name());
    QCOMPARE(def2->modes().size(), 1);
    QCOMPARE(def2->modes().at(0)->name(), mode->name());

    delete def;
    delete def2;
    QFile::remove(path);
    QVERIFY(QFile::exists(path) == false);
}

void QLCFixtureDef_Test::assignment()
{
    /* The target already owns channels and modes: they must be thrown away */
    QLCFixtureDef target;
    target.setManufacturer("Old");
    target.setModel("Target");
    QLCChannel *oldCh = new QLCChannel();
    oldCh->setName("Old channel");
    target.addChannel(oldCh);
    QLCFixtureMode *oldMode = new QLCFixtureMode(&target);
    oldMode->setName("Old mode");
    oldMode->insertChannel(oldCh, 0);
    target.addMode(oldMode);

    QLCFixtureDef source;
    source.setManufacturer("New");
    source.setModel("Source");
    source.setType(QLCFixtureDef::Scanner);
    source.setAuthor("Me");
    QLCChannel *newCh = new QLCChannel();
    newCh->setName("New channel");
    source.addChannel(newCh);
    QLCFixtureMode *newMode = new QLCFixtureMode(&source);
    newMode->setName("New mode");
    newMode->insertChannel(newCh, 0);
    source.addMode(newMode);

    target = source;
    QCOMPARE(target.manufacturer(), QString("New"));
    QCOMPARE(target.model(), QString("Source"));
    QCOMPARE(target.type(), QLCFixtureDef::Scanner);
    QCOMPARE(target.author(), QString("Me"));
    QCOMPARE(target.channels().size(), 1);
    QVERIFY(target.channels().at(0) != newCh);
    QCOMPARE(target.channels().at(0)->name(), QString("New channel"));
    QCOMPARE(target.modes().size(), 1);
    QVERIFY(target.modes().at(0) != newMode);
    QCOMPARE(target.modes().at(0)->name(), QString("New mode"));
    QVERIFY(target.modes().at(0)->channel(0) == target.channels().at(0));

    /* Self assignment is a no-op */
    QLCFixtureDef &self = target;
    target = self;
    QCOMPARE(target.channels().size(), 1);
    QCOMPARE(target.modes().size(), 1);
}

void QLCFixtureDef_Test::typeStrings()
{
    const QList<QLCFixtureDef::FixtureType> types = QList<QLCFixtureDef::FixtureType>()
        << QLCFixtureDef::ColorChanger << QLCFixtureDef::Dimmer << QLCFixtureDef::Effect
        << QLCFixtureDef::Fan << QLCFixtureDef::Flower << QLCFixtureDef::Hazer
        << QLCFixtureDef::Laser << QLCFixtureDef::MovingHead << QLCFixtureDef::Scanner
        << QLCFixtureDef::Smoke << QLCFixtureDef::Strobe << QLCFixtureDef::LEDBarBeams
        << QLCFixtureDef::LEDBarPixels << QLCFixtureDef::Other;

    foreach (QLCFixtureDef::FixtureType type, types)
        QCOMPARE(QLCFixtureDef::stringToType(QLCFixtureDef::typeToString(type)), type);

    QCOMPARE(QLCFixtureDef::typeToString(QLCFixtureDef::Fan), QString("Fan"));
    QCOMPARE(QLCFixtureDef::typeToString(QLCFixtureDef::Other), QString("Other"));
    QCOMPARE(QLCFixtureDef::stringToType("Whatever"), QLCFixtureDef::Other);
}

void QLCFixtureDef_Test::checkLoadedGeneric()
{
    /* The built-in generic definitions never come from a file */
    QLCFixtureDef generic;
    generic.setManufacturer(KXMLFixtureGeneric);
    generic.setModel(KXMLFixtureGeneric);
    QVERIFY(generic.m_isLoaded == false);
    generic.checkLoaded(QString());
    QVERIFY(generic.m_isLoaded == true);

    QLCFixtureDef panel;
    panel.setManufacturer(KXMLFixtureGeneric);
    panel.setModel(KXMLFixtureRGBPanel);
    panel.checkLoaded(QString());
    QVERIFY(panel.m_isLoaded == true);

    /* Anything else without a source path stays unloaded */
    QLCFixtureDef other;
    other.setManufacturer("Foo");
    other.setModel("Bar");
    other.checkLoaded(QString());
    QVERIFY(other.m_isLoaded == false);

    /* Already loaded: nothing happens, whatever the path (note that setting
       the source file resets the flag, so flag it afterwards) */
    other.setDefinitionSourceFile("/no/such/file.qxf");
    QVERIFY(other.m_isLoaded == false);
    other.setLoaded(true);
    other.checkLoaded(QString());
    QVERIFY(other.m_isLoaded == true);
    QCOMPARE(other.definitionSourceFile(), QString("/no/such/file.qxf"));
}

void QLCFixtureDef_Test::clearContents()
{
    QLCFixtureDef def;
    def.setManufacturer("Foo");
    def.setModel("Bar");
    def.setAuthor("Me");
    def.setType(QLCFixtureDef::Laser);
    QLCPhysical phys;
    phys.setWeight(42);
    def.setPhysical(phys);

    QLCChannel *ch = new QLCChannel();
    ch->setName("Dimmer");
    def.addChannel(ch);
    QLCFixtureMode *mode = new QLCFixtureMode(&def);
    mode->setName("Mode");
    mode->insertChannel(ch, 0);
    def.addMode(mode);

    def.clear();
    QVERIFY(def.manufacturer().isEmpty());
    QVERIFY(def.model().isEmpty());
    QVERIFY(def.author().isEmpty());
    QCOMPARE(def.type(), QLCFixtureDef::Dimmer);
    QVERIFY(def.channels().isEmpty());
    QVERIFY(def.modes().isEmpty());
    QCOMPARE(def.physical().weight(), 0.0);
}

void QLCFixtureDef_Test::saveFailures()
{
    QLCFixtureDef def;
    def.setManufacturer("Foo");
    def.setModel("Bar");

    QCOMPARE(def.saveXML(QString()), QFile::OpenError);

    /* The temporary file can be written, but the final name is taken by a
       directory that can't be removed to make room for it */
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QVERIFY(dir.mkdir("taken.qxf"));
    const QString path(dir.absoluteFilePath("taken.qxf"));
    QVERIFY(def.saveXML(path) != QFile::NoError);
    QVERIFY(QFileInfo(path).isDir());
    QFile::remove(path + ".temp");
    QVERIFY(tmp.remove());
}

void QLCFixtureDef_Test::loadFileFailures()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QDir dir(tmp.path());
    QLCFixtureDef def;

    /* Malformed XML: an error before any document type is found */
    const QString garbage(dir.absoluteFilePath("garbage.qxf"));
    QVERIFY(writeTextFile(garbage, "<?xml version=\"1.0\"?>\n<<<"));
    QCOMPARE(def.loadXML(garbage), QFile::ResourceError);

    /* Some other document type */
    const QString foreign(dir.absoluteFilePath("foreign.qxf"));
    QVERIFY(writeTextFile(foreign, "<!DOCTYPE Workspace>\n<Workspace/>\n"));
    QCOMPARE(def.loadXML(foreign), QFile::ReadError);

    /* The right document type, but the root element isn't a fixture */
    const QString wrongRoot(dir.absoluteFilePath("wrongroot.qxf"));
    QVERIFY(writeTextFile(wrongRoot, "<!DOCTYPE FixtureDefinition>\n<Foo/>\n"));
    QCOMPARE(def.loadXML(wrongRoot), QFile::ReadError);

    QVERIFY(def.manufacturer().isEmpty());
    QVERIFY(def.channels().isEmpty());
    QVERIFY(tmp.remove());
}

void QLCFixtureDef_Test::loadReaderEdgeCases()
{
    /* A reader that has already run to its end */
    QXmlStreamReader spent("<Foo/>");
    while (spent.atEnd() == false)
        spent.readNext();
    QLCFixtureDef def;
    QVERIFY(def.loadXML(spent) == false);

    /* Creator information hanging off the wrong element */
    QXmlStreamReader wrong("<Foo><Author>Me</Author></Foo>");
    QVERIFY(wrong.readNextStartElement());
    QVERIFY(def.loadCreator(wrong) == false);
    QVERIFY(def.author().isEmpty());

    /* Nameless channels/modes plus unknown tags are skipped, the rest of the
       definition still loads. Duplicate names are NOT rejected: addChannel()
       and addMode() only refuse an instance that is already in the list, so
       a repeated <Channel>/<Mode> element loads a second time. */
    const QString xml(
        "<FixtureDefinition>"
        " <Creator><Name>Q</Name><Version>1</Version><Author>Me</Author><Bogus/></Creator>"
        " <Manufacturer>Foo</Manufacturer>"
        " <Model>Bar</Model>"
        " <Type>Scanner</Type>"
        " <Channel Name=\"Dimmer\"><Group Byte=\"0\">Intensity</Group></Channel>"
        " <Channel Name=\"Dimmer\"><Group Byte=\"0\">Intensity</Group></Channel>"
        " <Channel/>"
        " <Mode Name=\"M1\"><Channel Number=\"0\">Dimmer</Channel></Mode>"
        " <Mode Name=\"M1\"><Channel Number=\"0\">Dimmer</Channel></Mode>"
        " <Mode/>"
        " <Unknown/>"
        "</FixtureDefinition>");
    QXmlStreamReader reader(xml);
    QVERIFY(def.loadXML(reader) == true);
    QCOMPARE(def.manufacturer(), QString("Foo"));
    QCOMPARE(def.model(), QString("Bar"));
    QCOMPARE(def.author(), QString("Me"));
    QCOMPARE(def.type(), QLCFixtureDef::Scanner);
    QCOMPARE(def.channels().size(), 2);
    QCOMPARE(def.channels().at(0)->name(), QString("Dimmer"));
    QCOMPARE(def.channels().at(1)->name(), QString("Dimmer"));
    QCOMPARE(def.modes().size(), 2);
    QCOMPARE(def.modes().at(0)->channels().size(), 1);
    QCOMPARE(def.modes().at(1)->name(), QString("M1"));
}

QTEST_APPLESS_MAIN(QLCFixtureDef_Test)
