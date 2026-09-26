/*
  Q Light Controller Plus - Unit test
  inputoutputmap_test.cpp

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
#include <QSignalSpy>
#include <QtTest>

#define private public
#define protected public
#include "iopluginstub.h"
#include "inputoutputmap_test.h"
#include "inputoutputmap.h"
#include "qlcinputchannel.h"
#include "qlcinputsource.h"
#include "audiocapture.h"
#include "grandmaster.h"
#include "mastertimer.h"
#include "outputpatch.h"
#include "inputpatch.h"
#include "qlcconfig.h"
#include "universe.h"
#include "qlcfile.h"
#include "doc.h"
#undef protected
#undef private

#define TESTPLUGINDIR "../iopluginstub"
#define ENGINEDIR "../../src"
#include "../common/resource_paths.h"

static QDir testPluginDir()
{
    QDir dir(TESTPLUGINDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtPlugin));
    return dir;
}

void InputOutputMap_Test::initTestCase()
{
    m_doc = new Doc(this);
    m_doc->ioPluginCache()->load(testPluginDir());
    QVERIFY(m_doc->ioPluginCache()->plugins().size() != 0);
}

void InputOutputMap_Test::cleanupTestCase()
{
    delete m_doc;
    m_doc = NULL;
}

void InputOutputMap_Test::initial()
{
    InputOutputMap im(m_doc, 4);
    QVERIFY(im.universesCount() == 4);
    QVERIFY(im.m_universeArray.count() == 4);
    QVERIFY(im.universeNames().count() == 4);
    QVERIFY(im.m_profiles.size() == 0);
    QVERIFY(im.profileNames().size() == 0);
}

void InputOutputMap_Test::pluginNames()
{
    InputOutputMap im(m_doc, 4);
    QCOMPARE(im.outputPluginNames().size(), 1);
    QCOMPARE(im.outputPluginNames().at(0), QString("I/O Plugin Stub"));
}

void InputOutputMap_Test::pluginInputs()
{
    InputOutputMap im(m_doc, 4);

    QVERIFY(im.pluginInputs("Foo").size() == 0);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QVERIFY(im.pluginInputs(stub->name()).size() == 4);
    QVERIFY(im.pluginInputs(stub->name()) == stub->inputs());
    QVERIFY(im.inputPluginNames().count() == 1);
    QVERIFY(im.inputPluginNames().at(0) == stub->name());
    QVERIFY(im.pluginSupportsFeedback(stub->name()) == false);
}

void InputOutputMap_Test::pluginOutputs()
{
    InputOutputMap om(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QStringList ls(om.pluginOutputs(stub->name()));
    QVERIFY(ls == stub->outputs());

    QVERIFY(om.pluginOutputs("Foobar").isEmpty() == true);
}

void InputOutputMap_Test::configurePlugin()
{
    InputOutputMap im(m_doc, 4);

    QCOMPARE(im.canConfigurePlugin("Foo"), false);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QCOMPARE(im.canConfigurePlugin("Foo"), false);
    QCOMPARE(im.canConfigurePlugin(stub->name()), false);
    stub->m_canConfigure = true;
    QCOMPARE(im.canConfigurePlugin(stub->name()), true);

    /* Must be able to call multiple times */
    im.configurePlugin(stub->name());
    QVERIFY(stub->m_configureCalled == 1);
    im.configurePlugin(stub->name());
    QVERIFY(stub->m_configureCalled == 2);
    im.configurePlugin(stub->name());
    QVERIFY(stub->m_configureCalled == 3);
}

void InputOutputMap_Test::inputPluginStatus()
{
    InputOutputMap im(m_doc, 4);

    QVERIFY(im.inputPluginStatus("Foo", QLCIOPlugin::invalidLine()).contains("Nothing selected"));
    QVERIFY(im.inputPluginStatus("Bar", 0).contains("Nothing selected"));
    QVERIFY(im.inputPluginStatus("Baz", 1).contains("Nothing selected"));
    QVERIFY(im.inputPluginStatus("Xyzzy", 2).contains("Nothing selected"));
    QVERIFY(im.inputPluginStatus("AYBABTU", 3).contains("Nothing selected"));

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QVERIFY(im.inputPluginStatus(stub->name(), QLCIOPlugin::invalidLine()) == stub->inputInfo(QLCIOPlugin::invalidLine()));
    QVERIFY(im.inputPluginStatus(stub->name(), 0) == stub->inputInfo(0));
    QVERIFY(im.inputPluginStatus(stub->name(), 1) == stub->inputInfo(1));
    QVERIFY(im.inputPluginStatus(stub->name(), 2) == stub->inputInfo(2));

    QVERIFY(im.pluginDescription("Foo") == "");
    QVERIFY(im.pluginDescription(stub->name()) == stub->pluginInfo());
}

void InputOutputMap_Test::outputPluginStatus()
{
    InputOutputMap om(m_doc, 4);

    QVERIFY(om.outputPluginStatus("Foo", QLCIOPlugin::invalidLine()).contains("Nothing selected"));
    QVERIFY(om.outputPluginStatus("Bar", 0).contains("Nothing selected"));
    QVERIFY(om.outputPluginStatus("Baz", 1).contains("Nothing selected"));
    QVERIFY(om.outputPluginStatus("Xyzzy", 2).contains("Nothing selected"));
    QVERIFY(om.outputPluginStatus("AYBABTU", 3).contains("Nothing selected"));

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QVERIFY(om.outputPluginStatus(stub->name(), 4) == stub->outputInfo(QLCIOPlugin::invalidLine()));
    QVERIFY(om.outputPluginStatus(stub->name(), 0) == stub->outputInfo(0));
    QVERIFY(om.outputPluginStatus(stub->name(), 1) == stub->outputInfo(1));
    QVERIFY(om.outputPluginStatus(stub->name(), 2) == stub->outputInfo(2));
}

void InputOutputMap_Test::universeNames()
{
    InputOutputMap iom(m_doc, 4);

    QCOMPARE(quint32(iom.universeNames().size()), iom.universesCount());
    QVERIFY(iom.universeNames().at(0).contains("Universe"));
    QVERIFY(iom.universeNames().at(1).contains("Universe"));
    QVERIFY(iom.universeNames().at(2).contains("Universe"));
    QVERIFY(iom.universeNames().at(3).contains("Universe"));

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    iom.setOutputPatch(0, stub->name(), "", "", 3);
    QCOMPARE(quint32(iom.universeNames().size()), iom.universesCount());
    QCOMPARE(iom.universeNames().at(0), QString("Universe 1"));
    QCOMPARE(iom.universeNames().at(1), QString("Universe 2"));
    QCOMPARE(iom.universeNames().at(2), QString("Universe 3"));
    QCOMPARE(iom.universeNames().at(3), QString("Universe 4"));

    iom.setOutputPatch(3, stub->name(), "", "", 2);
    QCOMPARE(quint32(iom.universeNames().size()), iom.universesCount());
    QCOMPARE(iom.universeNames().at(0), QString("Universe 1"));
    QCOMPARE(iom.universeNames().at(1), QString("Universe 2"));
    QCOMPARE(iom.universeNames().at(2), QString("Universe 3"));
    QCOMPARE(iom.universeNames().at(3), QString("Universe 4"));

    iom.setUniverseName(1, "Name Changed");
    iom.setUniverseName(42, "This is not the Universe you're looking for");

    QCOMPARE(iom.getUniverseNameByIndex(1), QString("Name Changed"));
    QCOMPARE(iom.getUniverseNameByIndex(2), QString("Universe 3"));
    QCOMPARE(iom.getUniverseNameByIndex(42), QString());
    QCOMPARE(iom.getUniverseNameByID(3), QString("Universe 4"));
}

void InputOutputMap_Test::addUniverse()
{
    InputOutputMap im(m_doc, 4);
    QVERIFY(im.universesCount() == 4);
    QVERIFY(im.addUniverse() == true);
    QVERIFY(im.universesCount() == 5);
    QVERIFY(im.getUniverseID(4) == 4);
    QVERIFY(im.getUniverseID(42) == Universe::invalid());

    /* try to add an existing universe */
    QVERIFY(im.addUniverse(3) == false);
    QVERIFY(im.universesCount() == 5);

    /* add a universe with high id and check that
     * there's no gaps */
    QVERIFY(im.addUniverse(8) == true);
    QVERIFY(im.universesCount() == 9);
}

void InputOutputMap_Test::removeUniverse()
{
    InputOutputMap im(m_doc, 4);
    QVERIFY(im.universesCount() == 4);

    // Creating a gap in the universe list is forbidden
    QVERIFY(im.removeUniverse(1) == false);
    QVERIFY(im.universesCount() == 4);

    // Removing the last universe is OK
    QVERIFY(im.removeUniverse(3) == true);
    QVERIFY(im.universesCount() == 3);

    QVERIFY(im.removeUniverse(7) == false);
    im.removeAllUniverses();
    QVERIFY(im.universesCount() == 0);
}

void InputOutputMap_Test::universe()
{
    InputOutputMap im(m_doc, 4);
    QVERIFY(im.universes().count() == 4);

    im.setUniversePassthrough(1, true);
    QVERIFY(im.getUniversePassthrough(1) == true);
    im.setUniversePassthrough(42, true);
    QVERIFY(im.getUniversePassthrough(42) == false);

    im.setUniverseMonitor(2, true);
    QVERIFY(im.getUniverseMonitor(2) == true);
    im.setUniverseMonitor(42, true);
    QVERIFY(im.getUniverseMonitor(42) == false);
}

void InputOutputMap_Test::profiles()
{
    InputOutputMap im(m_doc, 4);
    QVERIFY(im.m_profiles.size() == 0);

    QLCInputProfile* prof = new QLCInputProfile();
    prof->setManufacturer("Foo");
    prof->setModel("Bar");

    QVERIFY(im.addProfile(prof) == true);
    QVERIFY(im.m_profiles.size() == 1);
    QVERIFY(im.addProfile(prof) == false);
    QVERIFY(im.m_profiles.size() == 1);

    QVERIFY(im.profileNames().size() == 1);
    QVERIFY(im.profileNames().at(0) == prof->name());
    QVERIFY(im.profile(prof->name()) == prof);
    QVERIFY(im.profile("Foobar") == NULL);

    QVERIFY(im.removeProfile("Foobar") == false);
    QVERIFY(im.m_profiles.size() == 1);
    QVERIFY(im.removeProfile(prof->name()) == true);
    QVERIFY(im.m_profiles.size() == 0);
}

void InputOutputMap_Test::setInputPatch()
{
    InputOutputMap im(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QLCInputProfile* prof = new QLCInputProfile();
    prof->setManufacturer("Foo");
    prof->setModel("Bar");
    im.addProfile(prof);

    QVERIFY(im.inputPatch(0) == NULL);
    QVERIFY(im.inputPatch(1) == NULL);
    QVERIFY(im.inputPatch(2) == NULL);
    QVERIFY(im.inputPatch(3) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 0) == InputOutputMap::invalidUniverse());
    QVERIFY(im.inputMapping(stub->name(), 1) == InputOutputMap::invalidUniverse());
    QVERIFY(im.inputMapping(stub->name(), 2) == InputOutputMap::invalidUniverse());
    QVERIFY(im.inputMapping(stub->name(), 3) == InputOutputMap::invalidUniverse());
    QVERIFY(im.isUniversePatched(0) == false);
    QVERIFY(im.isUniversePatched(42) == false);

    QVERIFY(im.setInputPatch(0, "Foobar", "", "", 0, prof->name()) == true);
    QVERIFY(im.inputPatch(0) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 0) == InputOutputMap::invalidUniverse());

    QVERIFY(im.inputPatch(1) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 1) == InputOutputMap::invalidUniverse());

    QVERIFY(im.inputPatch(2) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 2) == InputOutputMap::invalidUniverse());

    QVERIFY(im.inputPatch(3) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 3) == InputOutputMap::invalidUniverse());

    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0) == true);
    QVERIFY(im.inputPatch(0)->plugin() == stub);
    QVERIFY(im.inputPatch(0)->input() == 0);
    QVERIFY(im.inputPatch(0)->profile() == NULL);
    QVERIFY(im.inputMapping(stub->name(), 0) == 0);
    QVERIFY(im.isUniversePatched(0) == true);

    QVERIFY(im.inputPatch(1) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 1) == InputOutputMap::invalidUniverse());

    QVERIFY(im.inputPatch(2) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 2) == InputOutputMap::invalidUniverse());

    QVERIFY(im.inputPatch(3) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 3) == InputOutputMap::invalidUniverse());

    QVERIFY(im.setInputPatch(2, stub->name(), "", stub->inputs().at(3), 3, prof->name()) == true);
    QVERIFY(im.inputPatch(0)->plugin() == stub);
    QVERIFY(im.inputPatch(0)->input() == 0);
    QVERIFY(im.inputPatch(0)->profile() == NULL);
    QVERIFY(im.inputMapping(stub->name(), 0) == 0);

    QVERIFY(im.inputPatch(1) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 1) == InputOutputMap::invalidUniverse());

    QVERIFY(im.inputPatch(2)->plugin() == stub);
    QVERIFY(im.inputPatch(2)->input() == 3);
    QVERIFY(im.inputPatch(2)->profile() == prof);
    QVERIFY(im.inputMapping(stub->name(), 2) == InputOutputMap::invalidUniverse());

    QVERIFY(im.inputPatch(3) == NULL);
    QVERIFY(im.inputMapping(stub->name(), 3) == 2);

    // Universe out of bounds
    QVERIFY(im.setInputPatch(im.universesCount(), stub->name(), "", stub->inputs().at(0), 0) == false);
}


void InputOutputMap_Test::setOutputPatch()
{
    InputOutputMap iom(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QVERIFY(iom.setOutputPatch(0, "Foobar", "", "", 0) == false);
    QVERIFY(iom.outputPatch(0) == NULL);
    QVERIFY(iom.outputPatch(1) == NULL);
    QVERIFY(iom.outputPatch(2) == NULL);
    QVERIFY(iom.outputPatch(3) == NULL);

    QVERIFY(iom.setOutputPatch(4, stub->name(), "", "", 0) == false);
    QVERIFY(iom.outputPatch(0) == NULL);
    QVERIFY(iom.outputPatch(1) == NULL);
    QVERIFY(iom.outputPatch(2) == NULL);
    QVERIFY(iom.outputPatch(3) == NULL);

    QVERIFY(iom.setOutputPatch(4, stub->name(), "", "", 4) == false);
    QVERIFY(iom.outputPatch(0) == NULL);
    QVERIFY(iom.outputPatch(1) == NULL);
    QVERIFY(iom.outputPatch(2) == NULL);
    QVERIFY(iom.outputPatch(3) == NULL);

    QVERIFY(iom.setOutputPatch(3, stub->name(), "", stub->outputs().at(0), 0) == true);
    QVERIFY(iom.outputPatch(3)->plugin() == stub);
    QVERIFY(iom.outputPatch(3)->output() == 0);

    QVERIFY(iom.setOutputPatch(2, stub->name(), "", stub->outputs().at(1), 1) == true);
    QVERIFY(iom.outputPatch(2)->plugin() == stub);
    QVERIFY(iom.outputPatch(2)->output() == 1);

    QVERIFY(iom.setOutputPatch(1, stub->name(), "", stub->outputs().at(2), 2) == true);
    QVERIFY(iom.outputPatch(1)->plugin() == stub);
    QVERIFY(iom.outputPatch(1)->output() == 2);

    QVERIFY(iom.setOutputPatch(0, stub->name(), "", stub->outputs().at(3), 3) == true);
    QVERIFY(iom.outputPatch(0)->plugin() == stub);
    QVERIFY(iom.outputPatch(0)->output() == 3);

    QVERIFY(iom.outputMapping("Foo", 42) == QLCIOPlugin::invalidLine());
    QVERIFY(iom.outputMapping(stub->name(), 0) == 3);

    QVERIFY(iom.feedbackPatch(42) == NULL);
    QVERIFY(iom.feedbackPatch(0) == NULL);
}

void InputOutputMap_Test::setMultipleOutputPatches()
{
    InputOutputMap iom(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    // add an output patch
    QVERIFY(iom.setOutputPatch(1, stub->name(), "", "", 0, false, 0) == true);
    QVERIFY(iom.outputPatchesCount(1) == 1);
    QVERIFY(iom.outputPatch(1, 0)->plugin() == stub);
    QVERIFY(iom.outputPatch(1, 0)->output() == 0);

    // add another output patch
    QVERIFY(iom.setOutputPatch(1, stub->name(), "", "", 0, false, 1) == true);
    QVERIFY(iom.outputPatchesCount(1) == 2);
    QVERIFY(iom.outputPatch(1, 1)->plugin() == stub);
    QVERIFY(iom.outputPatch(1, 1)->output() == 0);

    // remove the first output patch
    QVERIFY(iom.setOutputPatch(1, stub->name(), "", "", QLCIOPlugin::invalidLine(), false, 0) == true);
    QVERIFY(iom.outputPatchesCount(1) == 1);
    QVERIFY(iom.outputPatch(1, 0)->plugin() == stub);
    QVERIFY(iom.outputPatch(1, 0)->output() == 0);
    QVERIFY(iom.outputPatch(1, 1) == NULL);

    // remove the first output patch again
    QVERIFY(iom.setOutputPatch(1, stub->name(), "", "", QLCIOPlugin::invalidLine(), false, 0) == true);
    QVERIFY(iom.outputPatchesCount(1) == 0);
}

void InputOutputMap_Test::slotValueChanged()
{
    InputOutputMap im(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0) == true);
    QVERIFY(im.inputPatch(0)->plugin() == stub);
    QVERIFY(im.inputPatch(0)->input() == 0);

    QSignalSpy spy(&im, SIGNAL(inputValueChanged(quint32, quint32, uchar, const QString&)));
    stub->emitValueChanged(UINT_MAX, 0, 15, UCHAR_MAX);
    QVERIFY(spy.size() == 0);
    im.flushInputs();
    QVERIFY(spy.size() == 1);
    QVERIFY(spy.at(0).at(0) == 0);
    QVERIFY(spy.at(0).at(1) == 15);
    QVERIFY(spy.at(0).at(2) == UCHAR_MAX);

    /* Invalid mapping for this plugin -> no signal */
    stub->emitValueChanged(UINT_MAX, 3, 15, UCHAR_MAX);
    QVERIFY(spy.size() == 1);
    im.flushInputs();
    QVERIFY(spy.size() == 1);
    QVERIFY(spy.at(0).at(0) == 0);
    QVERIFY(spy.at(0).at(1) == 15);
    QVERIFY(spy.at(0).at(2) == UCHAR_MAX);

    /* Invalid mapping for this plugin -> no signal */
    stub->emitValueChanged(UINT_MAX, 1, 15, UCHAR_MAX);
    QVERIFY(spy.size() == 1);
    im.flushInputs();
    QVERIFY(spy.size() == 1);
    QVERIFY(spy.at(0).at(0) == 0);
    QVERIFY(spy.at(0).at(1) == 15);
    QVERIFY(spy.at(0).at(2) == UCHAR_MAX);

    stub->emitValueChanged(UINT_MAX, 0, 5, 127);
    QVERIFY(spy.size() == 1);
    im.flushInputs();
    QVERIFY(spy.size() == 2);
    QVERIFY(spy.at(0).at(0) == 0);
    QVERIFY(spy.at(0).at(1) == 15);
    QVERIFY(spy.at(0).at(2) == UCHAR_MAX);
    QVERIFY(spy.at(1).at(0) == 0);
    QVERIFY(spy.at(1).at(1) == 5);
    QVERIFY(spy.at(1).at(2) == 127);

    stub->emitValueChanged(UINT_MAX, 0, 2, 0);
    QVERIFY(spy.size() == 2);
    stub->emitValueChanged(UINT_MAX, 0, 2, UCHAR_MAX);
    QVERIFY(spy.size() == 3);
    QVERIFY(spy.at(0).at(0) == 0);
    QVERIFY(spy.at(0).at(1) == 15);
    QVERIFY(spy.at(0).at(2) == UCHAR_MAX);
    QVERIFY(spy.at(1).at(0) == 0);
    QVERIFY(spy.at(1).at(1) == 5);
    QVERIFY(spy.at(1).at(2) == 127);
    QVERIFY(spy.at(2).at(0) == 0);
    QVERIFY(spy.at(2).at(1) == 2);
    QVERIFY(spy.at(2).at(2) == 0);
    im.flushInputs();
    QVERIFY(spy.size() == 4);
    QVERIFY(spy.at(0).at(0) == 0);
    QVERIFY(spy.at(0).at(1) == 15);
    QVERIFY(spy.at(0).at(2) == UCHAR_MAX);
    QVERIFY(spy.at(1).at(0) == 0);
    QVERIFY(spy.at(1).at(1) == 5);
    QVERIFY(spy.at(1).at(2) == 127);
    QVERIFY(spy.at(2).at(0) == 0);
    QVERIFY(spy.at(2).at(1) == 2);
    QVERIFY(spy.at(2).at(2) == 0);
    QVERIFY(spy.at(3).at(0) == 0);
    QVERIFY(spy.at(3).at(1) == 2);
    QVERIFY(spy.at(3).at(2) == UCHAR_MAX);
}

void InputOutputMap_Test::slotConfigurationChanged()
{
    InputOutputMap im(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QSignalSpy spy(&im, SIGNAL(pluginConfigurationChanged(QString, bool)));
    stub->configure();
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).size(), 2);
    QCOMPARE(spy.at(0).at(0).toString(), QString(stub->name()));
}

void InputOutputMap_Test::loadInputProfiles()
{
    InputOutputMap im(m_doc, 4);

    // No profiles in a nonexistent directory
    QDir dir("/path/to/a/nonexistent/place/beyond/this/universe");
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtInputProfile));
    im.loadProfiles(dir);
    QVERIFY(im.profileNames().isEmpty() == true);

    // No profiles in an existing directory
    dir = testPluginDir();
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtInputProfile));
    im.loadProfiles(dir);
    QVERIFY(im.profileNames().isEmpty() == true);

    // Should be able to load profiles
    dir.setPath(INTERNAL_PROFILEDIR);
    im.loadProfiles(dir);
    QStringList names(im.profileNames());
    QVERIFY(names.size() > 0);

    // Shouldn't load duplicates
    im.loadProfiles(dir);
    QCOMPARE(names, im.profileNames());
}

void InputOutputMap_Test::inputSourceNames()
{
    InputOutputMap im(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*> (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QDir dir(INTERNAL_PROFILEDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtInputProfile));
    im.loadProfiles(dir);

    // Allow unpatched universe
    QString uni, ch;
    QVERIFY(im.inputSourceNames(new QLCInputSource(0, 0), uni, ch) == true);
    QCOMPARE(uni, QString("%1 -UNPATCHED-").arg(1));
    QCOMPARE(ch, QString("%1: ?").arg(1));

    // Don't allow unexisting universe
    QVERIFY(im.inputSourceNames(new QLCInputSource(100, 0), uni, ch) == false);

    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0, QString("Generic MIDI")) == true);
    QVERIFY(im.inputSourceNames(new QLCInputSource(0, 0), uni, ch) == true);
    QCOMPARE(uni, tr("%1: Generic MIDI").arg(1));
    QCOMPARE(ch, tr("%1: Bank select MSB").arg(1));

    uni.clear();
    ch.clear();
    QVERIFY(im.inputSourceNames(new QLCInputSource(0, 50000), uni, ch) == true);
    QCOMPARE(uni, tr("%1: Generic MIDI").arg(1));
    QCOMPARE(ch, tr("%1: ?").arg(50001));

    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0, QString()) == true);

    uni.clear();
    ch.clear();
    QVERIFY(im.inputSourceNames(new QLCInputSource(0, 0), uni, ch) == true);
    QCOMPARE(uni, tr("%1: %2").arg(1).arg(stub->name()));
    QCOMPARE(ch, tr("%1: ?").arg(1));

    QVERIFY(im.inputSourceNames(new QLCInputSource(0, QLCInputSource::invalidChannel), uni, ch) == false);
    QVERIFY(im.inputSourceNames(new QLCInputSource(InputOutputMap::invalidUniverse(), 0), uni, ch) == false);
    QVERIFY(im.inputSourceNames(new QLCInputSource(), uni, ch) == false);
}

void InputOutputMap_Test::profileDirectories()
{
    QDir dir = InputOutputMap::systemProfileDirectory();
    QVERIFY(dir.filter() & QDir::Files);
    QVERIFY(dir.nameFilters().contains(QString("*%1").arg(KExtInputProfile)));
#if defined(__APPLE__) || defined(Q_OS_MAC)
    // In a real .app bundle the executable lives in Contents/MacOS/ and
    // resources in Contents/Resources/ - siblings one level up - so
    // QLCFile::systemDirectory()'s APPLE branch (qlcfile.cpp) intentionally
    // inserts "/..". Mirrors the equivalent guard in
    // engine/test/rgbscript/rgbscript_test.cpp's directories().
    QString path("%1/../%2");
    QCOMPARE(dir.path(), path.arg(QCoreApplication::applicationDirPath())
                             .arg(INPUTPROFILEDIR));
#else
    QDir ipDir;
    ipDir.setPath(INPUTPROFILEDIR);
    QCOMPARE(dir.absolutePath(), ipDir.absolutePath());
#endif

    dir = InputOutputMap::userProfileDirectory();
#ifndef SKIP_TEST
    QVERIFY(dir.exists() == true);
#endif
    QVERIFY(dir.filter() & QDir::Files);
    QVERIFY(dir.nameFilters().contains(QString("*%1").arg(KExtInputProfile)));
    QVERIFY(dir.absolutePath().contains(USERINPUTPROFILEDIR));
}

void InputOutputMap_Test::claimReleaseDumpReset()
{
    InputOutputMap iom(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    iom.setOutputPatch(0, stub->name(), "", stub->outputs().at(0), 0);
    iom.setOutputPatch(1, stub->name(), "", stub->outputs().at(1), 1);
    iom.setOutputPatch(2, stub->name(), "", stub->outputs().at(2), 2);
    iom.setOutputPatch(3, stub->name(), "", stub->outputs().at(3), 3);

    QList<Universe*> unis = iom.claimUniverses();
    for (int i = 0; i < 512; i++)
        unis[0]->write(i, 'a');
    for (int i = 0; i < 512; i++)
        unis[1]->write(i, 'b');
    for (int i = 0; i < 512; i++)
        unis[2]->write(i, 'c');
    for (int i = 0; i < 512; i++)
        unis[3]->write(i, 'd');
    iom.releaseUniverses();

    foreach (Universe *universe, unis)
    {
        const QByteArray postGM = universe->postGMValues()->mid(0, universe->usedChannels());
        universe->dumpOutput(postGM, true);
    }

    for (int i = 0; i < 512; i++)
        QCOMPARE(stub->m_universe.data()[i], 'a');

    for (int i = 512; i < 1024; i++)
        QCOMPARE(stub->m_universe.data()[i], 'b');

    for (int i = 1024; i < 1536; i++)
        QCOMPARE(stub->m_universe.data()[i], 'c');

    for (int i = 1536; i < 2048; i++)
        QCOMPARE(stub->m_universe.data()[i], 'd');

    iom.resetUniverses();
    for (int u = 0; u < iom.m_universeArray.size(); u++)
    {
        for (quint32 i = 0; i < 512; i++)
            QVERIFY(iom.m_universeArray.at(u)->preGMValues().data()[i] == 0);
    }
}

void InputOutputMap_Test::blackout()
{
    InputOutputMap iom(m_doc, 4);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    iom.setOutputPatch(0, stub->name(), "", stub->outputs().at(0), 0);
    iom.setOutputPatch(1, stub->name(), "", stub->outputs().at(1), 1);
    iom.setOutputPatch(2, stub->name(), "", stub->outputs().at(2), 2);
    iom.setOutputPatch(3, stub->name(), "", stub->outputs().at(3), 3);

    QList<Universe*> unis = iom.claimUniverses();
    unis[0]->setChannelCapability(42, QLCChannel::Intensity);
    unis[1]->setChannelCapability(42, QLCChannel::Intensity);
    unis[2]->setChannelCapability(42, QLCChannel::Intensity);
    unis[3]->setChannelCapability(42, QLCChannel::Intensity);

    for (int i = 0; i < 512; i++)
        unis[0]->write(i, 'a');
    for (int i = 0; i < 512; i++)
        unis[1]->write(i, 'b');
    for (int i = 0; i < 512; i++)
        unis[2]->write(i, 'c');
    for (int i = 0; i < 512; i++)
        unis[3]->write(i, 'd');
    iom.releaseUniverses();

    foreach (Universe *universe, unis)
    {
        const QByteArray postGM = universe->postGMValues()->mid(0, universe->usedChannels());
        universe->dumpOutput(postGM, true);
    }

    iom.setBlackout(true);
    QVERIFY(iom.blackout() == true);

    foreach (Universe *universe, unis)
    {
        const QByteArray postGM = universe->postGMValues()->mid(0, universe->usedChannels());
        universe->dumpOutput(postGM, true);
    }

    int offset = 0;

    for (int u = 0; u < 4; u++)
    {
        for (int i = 0; i < 512; i++)
        {
            if (i == 42)
                QVERIFY(stub->m_universe[offset + i] == (char) 0);
            else if (u == 0)
                QVERIFY(stub->m_universe[offset + i] == (char) 'a');
            else if (u == 1)
                QVERIFY(stub->m_universe[offset + i] == (char) 'b');
            else if (u == 2)
                QVERIFY(stub->m_universe[offset + i] == (char) 'c');
            else if (u == 3)
                QVERIFY(stub->m_universe[offset + i] == (char) 'd');
        }

        offset += 512;
    }

    iom.setBlackout(true);
    QVERIFY(iom.blackout() == true);

    foreach (Universe *universe, unis)
    {
        const QByteArray postGM = universe->postGMValues()->mid(0, universe->usedChannels());
        universe->dumpOutput(postGM, true);
    }

    offset = 0;

    for (int u = 0; u < 4; u++)
    {
        for (int i = 0; i < 512; i++)
        {
            if (i == 42)
                QVERIFY(stub->m_universe[offset + i] == (char) 0);
            else if (u == 0)
                QVERIFY(stub->m_universe[offset + i] == (char) 'a');
            else if (u == 1)
                QVERIFY(stub->m_universe[offset + i] == (char) 'b');
            else if (u == 2)
                QVERIFY(stub->m_universe[offset + i] == (char) 'c');
            else if (u == 3)
                QVERIFY(stub->m_universe[offset + i] == (char) 'd');
        }

        offset += 512;
    }

    iom.toggleBlackout();
    QVERIFY(iom.blackout() == false);

    foreach (Universe *universe, unis)
    {
        const QByteArray postGM = universe->postGMValues()->mid(0, universe->usedChannels());
        universe->dumpOutput(postGM, true);
    }

    for (int i = 0; i < 512; i++)
        QVERIFY(stub->m_universe[i] == 'a');
    for (int i = 512; i < 1024; i++)
        QVERIFY(stub->m_universe[i] == 'b');
    for (int i = 1024; i < 1536; i++)
        QVERIFY(stub->m_universe[i] == 'c');
    for (int i = 1536; i < 2048; i++)
        QVERIFY(stub->m_universe[i] == 'd');

    iom.setBlackout(false);
    QVERIFY(iom.blackout() == false);

    foreach (Universe *universe, unis)
    {
        const QByteArray postGM = universe->postGMValues()->mid(0, universe->usedChannels());
        universe->dumpOutput(postGM, true);
    }

    for (int i = 0; i < 512; i++)
        QVERIFY(stub->m_universe[i] == 'a');
    for (int i = 512; i < 1024; i++)
        QVERIFY(stub->m_universe[i] == 'b');
    for (int i = 1024; i < 1536; i++)
        QVERIFY(stub->m_universe[i] == 'c');
    for (int i = 1536; i < 2048; i++)
        QVERIFY(stub->m_universe[i] == 'd');

    iom.toggleBlackout();
    QVERIFY(iom.blackout() == true);

    foreach (Universe *universe, unis)
    {
        const QByteArray postGM = universe->postGMValues()->mid(0, universe->usedChannels());
        universe->dumpOutput(postGM, true);
    }

    offset = 0;

    for (int u = 0; u < 4; u++)
    {
        for (int i = 0; i < 512; i++)
        {
            if (i == 42)
                QVERIFY(stub->m_universe[offset + i] == (char) 0);
            else if (u == 0)
                QVERIFY(stub->m_universe[offset + i] == (char) 'a');
            else if (u == 1)
                QVERIFY(stub->m_universe[offset + i] == (char) 'b');
            else if (u == 2)
                QVERIFY(stub->m_universe[offset + i] == (char) 'c');
            else if (u == 3)
                QVERIFY(stub->m_universe[offset + i] == (char) 'd');
        }

        offset += 512;
    }
}

void InputOutputMap_Test::grandMaster()
{
    InputOutputMap iom(m_doc, 4);

    QVERIFY(iom.grandMasterChannelMode() == GrandMaster::Intensity);
    QVERIFY(iom.grandMasterValueMode() == GrandMaster::Reduce);
    QVERIFY(iom.grandMasterValue() == 255);

    iom.setGrandMasterValue(100);
    QVERIFY(iom.grandMasterValue() == 100);

    iom.setGrandMasterChannelMode(GrandMaster::AllChannels);
    QVERIFY(iom.grandMasterChannelMode() == GrandMaster::AllChannels);

    iom.setGrandMasterValueMode(GrandMaster::Limit);
    QVERIFY(iom.grandMasterValueMode() == GrandMaster::Limit);
}

void InputOutputMap_Test::requestBlackout()
{
    InputOutputMap iom(m_doc, 2);
    QSignalSpy spy(&iom, SIGNAL(blackoutChanged(bool)));

    iom.requestBlackout(InputOutputMap::BlackoutRequestNone);
    QCOMPARE(iom.blackout(), false);
    QCOMPARE(spy.size(), 0);

    iom.requestBlackout(InputOutputMap::BlackoutRequestOn);
    QCOMPARE(iom.blackout(), true);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);

    // requesting the same state twice is a no-op
    iom.requestBlackout(InputOutputMap::BlackoutRequestOn);
    QCOMPARE(iom.blackout(), true);
    QCOMPARE(spy.size(), 1);

    iom.requestBlackout(InputOutputMap::BlackoutRequestNone);
    QCOMPARE(iom.blackout(), true);

    iom.requestBlackout(InputOutputMap::BlackoutRequestOff);
    QCOMPARE(iom.blackout(), false);
    QCOMPARE(spy.size(), 2);
}

void InputOutputMap_Test::universeLookupAndStart()
{
    InputOutputMap iom(m_doc, 3);

    QVERIFY(iom.universe(0) != NULL);
    QCOMPARE(iom.universe(0)->id(), quint32(0));
    QVERIFY(iom.universe(2) != NULL);
    QCOMPARE(iom.universe(2)->id(), quint32(2));
    QVERIFY(iom.universe(3) == NULL);
    QVERIFY(iom.universe(InputOutputMap::invalidUniverse()) == NULL);

    // out of bounds getters
    QVERIFY(iom.outputPatch(42, 0) == NULL);
    QCOMPARE(iom.outputPatchesCount(42), 0);
    QVERIFY(iom.inputPatch(42) == NULL);
    QVERIFY(iom.feedbackPatch(42) == NULL);

    // starting the universe threads must not block; the destructor
    // (removeAllUniverses) is in charge of stopping them again
    iom.startUniverses();
    foreach (Universe *uni, iom.universes())
        QTRY_VERIFY_WITH_TIMEOUT(uni->isRunning() == true, 2000);
    QTest::qSleep(50);
}

void InputOutputMap_Test::replaceInputPatchAndProfile()
{
    InputOutputMap im(m_doc, 2);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QDir dir(INTERNAL_PROFILEDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtInputProfile));
    im.loadProfiles(dir);
    QVERIFY(im.profile("Generic MIDI") != NULL);

    // setting a profile on an unpatched universe is harmless
    QVERIFY(im.setInputProfile(0, "Generic MIDI") == true);
    QVERIFY(im.inputPatch(0) == NULL);
    QVERIFY(im.setInputProfile(42, "Generic MIDI") == false);

    QSignalSpy profileSpy(&im, SIGNAL(profileChanged(quint32, const QString&)));

    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0) == true);
    QVERIFY(im.inputPatch(0) != NULL);
    QCOMPARE(im.inputPatch(0)->input(), quint32(0));
    QVERIFY(im.inputPatch(0)->profile() == NULL);
    QCOMPARE(profileSpy.size(), 0);

    // replace the existing patch with another line: the old one is disconnected first
    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(1), 1, "Generic MIDI") == true);
    QVERIFY(im.inputPatch(0) != NULL);
    QCOMPARE(im.inputPatch(0)->input(), quint32(1));
    QCOMPARE(im.inputPatch(0)->profileName(), QString("Generic MIDI"));
    QCOMPARE(profileSpy.size(), 1);
    QCOMPARE(profileSpy.at(0).at(0).toUInt(), quint32(0));
    QCOMPARE(profileSpy.at(0).at(1).toString(), QString("Generic MIDI"));

    // change only the profile of the patched universe
    QVERIFY(im.setInputProfile(0, "Foobar") == true);
    QVERIFY(im.inputPatch(0)->profile() == NULL);
    QVERIFY(im.setInputProfile(0, "Generic MIDI") == true);
    QCOMPARE(im.inputPatch(0)->profileName(), QString("Generic MIDI"));

    // matching by UID/name falls back to the saved line number when nothing matches
    QVERIFY(im.setInputPatch(1, stub->name(), "no-such-uid", "no such name", 2) == true);
    QVERIFY(im.inputPatch(1) != NULL);
    QCOMPARE(im.inputPatch(1)->input(), quint32(2));

    // removing the patch again
    QVERIFY(im.setInputPatch(1, stub->name(), "", "", QLCIOPlugin::invalidLine()) == true);
    QVERIFY(im.inputPatch(1) == NULL);
    QVERIFY(im.isUniversePatched(1) == false);
}

void InputOutputMap_Test::feedbackPatch()
{
    InputOutputMap iom(m_doc, 2);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    // nothing patched yet, and unknown plugins offer no feedback
    QVERIFY(iom.pluginSupportsFeedback("Foobar") == false);
    QVERIFY(iom.sendFeedBack(0, 1, 255, QVariant()) == false);
    QVERIFY(iom.sendFeedBack(42, 1, 255, QVariant()) == false);
    QVERIFY(iom.feedbackPatch(0) == NULL);
    QVERIFY(iom.universe(0)->hasFeedback() == false);

    // an invalid line cannot create a feedback patch
    QVERIFY(iom.setOutputPatch(0, stub->name(), "", "", QLCIOPlugin::invalidLine(), true) == false);
    QVERIFY(iom.feedbackPatch(0) == NULL);
    QVERIFY(iom.setOutputPatch(0, "Foobar", "", "", 1, true) == false);
    QVERIFY(iom.feedbackPatch(0) == NULL);

    QSignalSpy fbSpy(iom.universe(0), SIGNAL(hasFeedbackChanged()));

    QVERIFY(iom.setOutputPatch(0, stub->name(), "", stub->outputs().at(1), 1, true) == true);
    QVERIFY(iom.feedbackPatch(0) != NULL);
    QCOMPARE(iom.feedbackPatch(0)->output(), quint32(1));
    QVERIFY(iom.feedbackPatch(0)->plugin() == stub);
    QVERIFY(iom.universe(0)->hasFeedback() == true);
    QVERIFY(iom.isUniversePatched(0) == true);
    QCOMPARE(fbSpy.size(), 1);

    // feedback goes through the plugin (the stub silently accepts it)
    QVERIFY(iom.sendFeedBack(0, 1, 255, QVariant("param")) == true);
    QVERIFY(iom.sendFeedBack(1, 1, 255, QVariant()) == false);

    // replace the feedback line
    QVERIFY(iom.setOutputPatch(0, stub->name(), "", stub->outputs().at(2), 2, true) == true);
    QCOMPARE(iom.feedbackPatch(0)->output(), quint32(2));
    QCOMPARE(fbSpy.size(), 2);

    // a plugin reconfiguration reconnects input, output and feedback patches
    QVERIFY(iom.setInputPatch(0, stub->name(), "", stub->inputs().at(3), 3) == true);
    QVERIFY(iom.setOutputPatch(0, stub->name(), "", stub->outputs().at(0), 0) == true);
    QSignalSpy cfgSpy(&iom, SIGNAL(pluginConfigurationChanged(QString, bool)));
    stub->configure();
    QCOMPARE(cfgSpy.size(), 1);
    QCOMPARE(cfgSpy.at(0).at(0).toString(), stub->name());
    QVERIFY(iom.feedbackPatch(0)->isPatched() == true);
    QVERIFY(iom.inputPatch(0)->isPatched() == true);

    // remove the feedback patch again
    QVERIFY(iom.setOutputPatch(0, stub->name(), "", "", QLCIOPlugin::invalidLine(), true) == true);
    QVERIFY(iom.feedbackPatch(0) == NULL);
    QVERIFY(iom.universe(0)->hasFeedback() == false);
    QCOMPARE(fbSpy.size(), 3);
    QVERIFY(iom.sendFeedBack(0, 1, 255, QVariant()) == false);
}

void InputOutputMap_Test::inputSourceNamesWithPages()
{
    InputOutputMap im(m_doc, 2);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QDir dir(INTERNAL_PROFILEDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtInputProfile));
    im.loadProfiles(dir);

    QString uni, ch;

    // shared pointer overload, unpatched universe with a page
    QSharedPointer<QLCInputSource> src(new QLCInputSource(0, 4));
    src->setPage(2);
    QCOMPARE(src->page(), ushort(2));
    QVERIFY(im.inputSourceNames(src, uni, ch) == true);
    QCOMPARE(uni, QString("1 -UNPATCHED-"));
    QCOMPARE(ch, QString("5: ? (Page 3)"));

    QSharedPointer<QLCInputSource> nullSrc;
    QVERIFY(im.inputSourceNames(nullSrc, uni, ch) == false);

    // patched without a profile
    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0) == true);
    QVERIFY(im.inputSourceNames(src, uni, ch) == true);
    QCOMPARE(uni, QString("1: %1").arg(stub->name()));
    QCOMPARE(ch, QString("5: ? (Page 3)"));

    // patched with a profile: channel 4 of Generic MIDI is a known control
    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0, "Generic MIDI") == true);
    QVERIFY(im.inputPatch(0)->profile() != NULL);
    QLCInputChannel *ich = im.inputPatch(0)->profile()->channel(4);
    QString channelName = (ich != NULL) ? ich->name() : QString("?");
    QVERIFY(im.inputSourceNames(src, uni, ch) == true);
    QCOMPARE(uni, QString("1: Generic MIDI"));
    QCOMPARE(ch, QString("5: %1 (Page 3)").arg(channelName));

    // page 0 with a profile channel that does not exist
    QSharedPointer<QLCInputSource> src2(new QLCInputSource(0, 60000));
    QVERIFY(im.inputSourceNames(src2, uni, ch) == true);
    QCOMPARE(ch, QString("60001: ?"));
}

void InputOutputMap_Test::removeDuplicates()
{
    InputOutputMap im(m_doc, 1);

    QStringList single;
    single << "A";
    im.removeDuplicates(single);
    QCOMPARE(single, QStringList() << "A");

    QStringList list;
    list << "A" << "B" << "A" << "A" << "B";
    im.removeDuplicates(list);
    QCOMPARE(list, QStringList() << "A" << "B" << "A 2" << "A 3" << "B 4");
}

void InputOutputMap_Test::workspaceProfiles()
{
    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    // a valid profile next to the workspace...
    QLCInputProfile local;
    local.setManufacturer("Local");
    local.setModel("Board");
    QLCInputChannel *ich = new QLCInputChannel();
    ich->setName("Fader");
    local.insertChannel(0, ich);
    QVERIFY(local.saveXML(tmp.filePath("local.qxi")) == true);

    // ...and a broken one that must be skipped with a warning
    QFile broken(tmp.filePath("broken.qxi"));
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("<this is not xml");
    broken.close();

    m_doc->setWorkspacePath(tmp.path());

    InputOutputMap im(m_doc, 1);
    QVERIFY(im.profileNames().isEmpty());

    // unknown names never trigger a workspace load when no path is set
    m_doc->setWorkspacePath(QString());
    QVERIFY(im.profile("Local Board") == NULL);
    QVERIFY(im.m_localProfilesLoaded == false);

    m_doc->setWorkspacePath(tmp.path());
    QLCInputProfile *found = im.profile("Local Board");
    QVERIFY(found != NULL);
    QCOMPARE(found->name(), QString("Local Board"));
    QVERIFY(im.m_localProfilesLoaded == true);
    QCOMPARE(im.profileNames().size(), 1);

    // already loaded: a second miss returns NULL without reloading
    QVERIFY(im.profile("Nope") == NULL);
    QCOMPARE(im.profileNames().size(), 1);

    // a workspace profile can be assigned to a patch by name
    QVERIFY(im.setInputPatch(0, stub->name(), "", "", 0, "Local Board") == true);
    QVERIFY(im.inputPatch(0)->profile() == found);

    // resetting the universes re-arms the lazy workspace loading
    im.resetUniverses();
    QVERIFY(im.m_localProfilesLoaded == false);

    m_doc->setWorkspacePath(QString());
}

void InputOutputMap_Test::beatGenerator()
{
    InputOutputMap im(m_doc, 1);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QCOMPARE(im.beatGeneratorType(), InputOutputMap::Disabled);
    QCOMPARE(im.bpmNumber(), 0);

    QCOMPARE(im.beatTypeToString(InputOutputMap::Disabled), QString("Disabled"));
    QCOMPARE(im.beatTypeToString(InputOutputMap::Internal), QString("Internal"));
    QCOMPARE(im.beatTypeToString(InputOutputMap::Plugin), QString("Plugin"));
    QCOMPARE(im.beatTypeToString(InputOutputMap::Audio), QString("Audio"));
    QCOMPARE(im.stringToBeatType("Internal"), InputOutputMap::Internal);
    QCOMPARE(im.stringToBeatType("Plugin"), InputOutputMap::Plugin);
    QCOMPARE(im.stringToBeatType("Audio"), InputOutputMap::Audio);
    QCOMPARE(im.stringToBeatType("Disabled"), InputOutputMap::Disabled);
    QCOMPARE(im.stringToBeatType("Foo"), InputOutputMap::Disabled);

    QSignalSpy typeSpy(&im, SIGNAL(beatGeneratorTypeChanged()));
    QSignalSpy bpmSpy(&im, SIGNAL(bpmNumberChanged(int)));
    QSignalSpy beatSpy(&im, SIGNAL(beat()));

    // while disabled, BPM requests are ignored
    im.setBpmNumber(90);
    QCOMPARE(im.bpmNumber(), 0);
    QCOMPARE(bpmSpy.size(), 0);

    // setting the same type again is a no-op
    im.setBeatGeneratorType(InputOutputMap::Disabled);
    QCOMPARE(typeSpy.size(), 0);

    // internal: the master timer is the beat source
    im.setBeatGeneratorType(InputOutputMap::Internal);
    QCOMPARE(im.beatGeneratorType(), InputOutputMap::Internal);
    QCOMPARE(typeSpy.size(), 1);
    QCOMPARE(m_doc->masterTimer()->beatSourceType(), MasterTimer::Internal);
    QCOMPARE(im.bpmNumber(), m_doc->masterTimer()->bpmNumber());

    im.setBpmNumber(90);
    QCOMPARE(im.bpmNumber(), 90);
    QCOMPARE(m_doc->masterTimer()->bpmNumber(), 90);
    QVERIFY(bpmSpy.size() >= 1);
    QCOMPARE(bpmSpy.last().at(0).toInt(), 90);
    int bpmSignals = bpmSpy.size();
    im.setBpmNumber(90);
    QCOMPARE(bpmSpy.size(), bpmSignals);

    // master timer beats are forwarded only in internal mode
    im.slotMasterTimerBeat();
    QCOMPARE(beatSpy.size(), 1);

    // plugin: beats come from an input plugin, BPM is derived from their spacing
    im.setBeatGeneratorType(InputOutputMap::Plugin);
    QCOMPARE(im.beatGeneratorType(), InputOutputMap::Plugin);
    QCOMPARE(m_doc->masterTimer()->beatSourceType(), MasterTimer::External);
    QCOMPARE(im.bpmNumber(), 0);
    im.slotMasterTimerBeat();
    QCOMPARE(beatSpy.size(), 1);

    // synthetic releases and non-beat keys are ignored
    im.slotPluginBeat(0, 0, 0, "beat");
    im.slotPluginBeat(0, 0, 255, "foo");
    QCOMPARE(beatSpy.size(), 1);

    QTest::qSleep(60);
    im.slotPluginBeat(0, 0, 255, "beat");
    QCOMPARE(beatSpy.size(), 2);
    // derived from the >= 60 ms spacing; only the upper bound is deterministic
    QVERIFY(im.bpmNumber() > 0);
    QVERIFY(im.bpmNumber() <= 1100);
    int derived = im.bpmNumber();

    // a second beat with the same spacing is only a drift, or a small change
    QTest::qSleep(60);
    im.slotProcessBeat();
    QCOMPARE(beatSpy.size(), 3);
    QVERIFY(im.bpmNumber() > 0);
    Q_UNUSED(derived);

    // a beat source providing its own tempo estimate wins
    im.slotProcessBeat(128);
    QCOMPARE(im.bpmNumber(), 128);
    QCOMPARE(m_doc->masterTimer()->bpmNumber(), 128);
    QCOMPARE(beatSpy.size(), 4);
    im.slotProcessBeat(128);
    QCOMPARE(im.bpmNumber(), 128);
    QCOMPARE(beatSpy.size(), 5);

    // back to disabled: BPM is reset and the master timer is told
    im.setBeatGeneratorType(InputOutputMap::Disabled);
    QCOMPARE(im.beatGeneratorType(), InputOutputMap::Disabled);
    QCOMPARE(im.bpmNumber(), 0);
    QCOMPARE(m_doc->masterTimer()->beatSourceType(), MasterTimer::None);
    im.slotPluginBeat(0, 0, 255, "beat");
    QCOMPARE(beatSpy.size(), 5);

    m_doc->masterTimer()->requestBpmNumber(120);
}

void InputOutputMap_Test::networkServer()
{
    InputOutputMap im(m_doc, 1);

    QCOMPARE(im.networkServerType(), int(InputOutputMap::NativeServer));
    QCOMPARE(im.networkServerAutoStart(), false);
    QVERIFY(im.networkServerName().isEmpty());
    QVERIFY(im.networkServerPassword().isEmpty());

    QCOMPARE(im.networkServerTypeToString(InputOutputMap::NoServer), QString("None"));
    QCOMPARE(im.networkServerTypeToString(InputOutputMap::NativeServer), QString("Native"));
    QCOMPARE(im.networkServerTypeToString(InputOutputMap::WebServer), QString("Web"));
    QCOMPARE(im.networkServerTypeToString(InputOutputMap::NativeServer | InputOutputMap::WebServer),
             QString("Native|Web"));

    QCOMPARE(im.stringToNetworkServerType(""), int(InputOutputMap::NoServer));
    QCOMPARE(im.stringToNetworkServerType("None"), int(InputOutputMap::NoServer));
    QCOMPARE(im.stringToNetworkServerType("Native"), int(InputOutputMap::NativeServer));
    QCOMPARE(im.stringToNetworkServerType("web"), int(InputOutputMap::WebServer));
    QCOMPARE(im.stringToNetworkServerType(" Native | Web "),
             int(InputOutputMap::NativeServer | InputOutputMap::WebServer));
    QCOMPARE(im.stringToNetworkServerType("Web|Native|Bogus"),
             int(InputOutputMap::NativeServer | InputOutputMap::WebServer));

    im.setNetworkServerType(InputOutputMap::WebServer);
    QCOMPARE(im.networkServerType(), int(InputOutputMap::WebServer));
    im.setNetworkServerAutoStart(true);
    QCOMPARE(im.networkServerAutoStart(), true);
    im.setNetworkServerName("Console");
    QCOMPARE(im.networkServerName(), QString("Console"));
    im.setNetworkServerPassword("secret");
    QCOMPARE(im.networkServerPassword(), QString("secret"));
}

void InputOutputMap_Test::defaults()
{
    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    // Never touch the real user settings (the registry on Windows): redirect
    // QSettings to an INI file inside a temporary directory for this process.
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QCoreApplication::setOrganizationName("QLCPlusTest");
    QCoreApplication::setApplicationName("inputoutputmap_test");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, tmp.path());

    {
        QSettings check;
        QVERIFY(QDir::cleanPath(check.fileName()).startsWith(QDir::cleanPath(tmp.path())));
    }

    QDir dir(INTERNAL_PROFILEDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtInputProfile));

    {
        InputOutputMap im(m_doc, 3);
        im.loadProfiles(dir);
        QVERIFY(im.setInputPatch(0, stub->name(), "", "", 0, "Generic MIDI") == true);
        QVERIFY(im.setOutputPatch(0, stub->name(), "", "", 1) == true);
        QVERIFY(im.setOutputPatch(1, stub->name(), "", "", 2, true) == true);
        im.setUniversePassthrough(2, true);
        im.saveDefaults();
    }

    {
        QSettings settings;
        QCOMPARE(settings.value("/inputmap/universe0/plugin/").toString(), stub->name());
        QCOMPARE(settings.value("/inputmap/universe0/input/").toString(), QString("0"));
        QCOMPARE(settings.value("/inputmap/universe0/profile/").toString(), QString("Generic MIDI"));
        QCOMPARE(settings.value("/inputmap/universe1/plugin/").toString(), QString(KInputNone));
        QCOMPARE(settings.value("/inputmap/universe1/input/").toString(), QString(KInputNone));
        QCOMPARE(settings.value("/inputmap/universe1/profile/").toString(), QString(KInputNone));
        QCOMPARE(settings.value("/inputmap/universe2/passthrough/").toBool(), true);
        QVERIFY(settings.contains("/inputmap/universe0/passthrough/") == false);
        QCOMPARE(settings.value("/outputmap/universe0/plugin/").toString(), stub->name());
        QCOMPARE(settings.value("/outputmap/universe0/output/").toUInt(), quint32(1));
        QCOMPARE(settings.value("/outputmap/universe0/feedbackplugin/").toString(), QString(KOutputNone));
        QCOMPARE(settings.value("/outputmap/universe0/feedback/").toString(), QString(KOutputNone));
        QCOMPARE(settings.value("/outputmap/universe1/plugin/").toString(), QString(KOutputNone));
        QCOMPARE(settings.value("/outputmap/universe1/output/").toString(), QString(KOutputNone));
        QCOMPARE(settings.value("/outputmap/universe1/feedbackplugin/").toString(), stub->name());
        QCOMPARE(settings.value("/outputmap/universe1/feedback/").toString(), QString("2"));
    }

    {
        InputOutputMap im(m_doc, 3);
        im.loadProfiles(dir);
        QVERIFY(im.inputPatch(0) == NULL);
        im.loadDefaults();

        QVERIFY(im.inputPatch(0) != NULL);
        QVERIFY(im.inputPatch(0)->plugin() == stub);
        QCOMPARE(im.inputPatch(0)->input(), quint32(0));
        QCOMPARE(im.inputPatch(0)->profileName(), QString("Generic MIDI"));
        QVERIFY(im.inputPatch(1) == NULL);
        QVERIFY(im.inputPatch(2) == NULL);

        QVERIFY(im.outputPatch(0) != NULL);
        QCOMPARE(im.outputPatch(0)->output(), quint32(1));
        QVERIFY(im.feedbackPatch(0) == NULL);
        QVERIFY(im.outputPatch(1) == NULL);
        QVERIFY(im.feedbackPatch(1) != NULL);
        QCOMPARE(im.feedbackPatch(1)->output(), quint32(2));
        QVERIFY(im.outputPatch(2) == NULL);

        QCOMPARE(im.getUniversePassthrough(0), false);
        QCOMPARE(im.getUniversePassthrough(2), true);
    }
}

static QString writeIOMapXML(bool nativeServer)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter w(&buffer);
    w.setAutoFormatting(true);

    w.writeStartElement("InputOutputMap");

    w.writeStartElement("BeatGenerator");
    w.writeAttribute("BeatType", "Internal");
    w.writeAttribute("BPM", "90");
    w.writeEndElement();

    w.writeStartElement("NetworkServer");
    w.writeAttribute("Type", nativeServer ? "Native|Web" : "Web");
    w.writeAttribute("AutoStart", "True");
    w.writeAttribute("Name", "Console");
    w.writeAttribute("Password", "secret");
    w.writeEndElement();

    // Universe 0: input with parameters + profile, feedback with parameters
    w.writeStartElement("Universe");
    w.writeAttribute("Name", "First");
    w.writeAttribute("ID", "0");
    w.writeAttribute("Passthrough", "True");

    // backward compatible: old files stored the line name in the UID attribute
    w.writeStartElement("Input");
    w.writeAttribute("Plugin", "I/O Plugin Stub");
    w.writeAttribute("UID", "1: Stub 1");
    w.writeAttribute("Line", "0");
    w.writeAttribute("Profile", "Generic MIDI");
    w.writeStartElement("PluginParameters");
    w.writeAttribute("inParam", "in-value");
    w.writeEndElement();
    w.writeEndElement();

    w.writeStartElement("Feedback");
    w.writeAttribute("Plugin", "I/O Plugin Stub");
    w.writeAttribute("UID", "3: Stub 3");
    w.writeAttribute("Line", "2");
    w.writeStartElement("PluginParameters");
    w.writeAttribute("fbParam", "fb-value");
    w.writeEndElement();
    w.writeEndElement();

    w.writeStartElement("Bogus");
    w.writeEndElement();

    w.writeEndElement(); // Universe 0

    // Universe 1: a single output with parameters, backward compatible UID-as-name
    w.writeStartElement("Universe");
    w.writeAttribute("Name", "Second");
    w.writeAttribute("ID", "1");
    w.writeStartElement("Output");
    w.writeAttribute("Plugin", "I/O Plugin Stub");
    w.writeAttribute("UID", "2: Stub 2");
    w.writeAttribute("Line", "1");
    w.writeStartElement("PluginParameters");
    w.writeAttribute("outParam", "out-value");
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndElement(); // Universe 1

    // Universe 2: two outputs without parameters, no name
    w.writeStartElement("Universe");
    w.writeAttribute("ID", "2");
    w.writeStartElement("Output");
    w.writeAttribute("Plugin", "I/O Plugin Stub");
    w.writeAttribute("Line", "3");
    w.writeEndElement();
    w.writeStartElement("Output");
    w.writeAttribute("Plugin", "I/O Plugin Stub");
    w.writeAttribute("Line", "0");
    w.writeEndElement();
    w.writeEndElement(); // Universe 2

    w.writeStartElement("Unknown");
    w.writeEndElement();

    w.writeEndElement(); // InputOutputMap
    w.writeEndDocument();
    buffer.close();

    return QString::fromUtf8(buffer.data());
}

void InputOutputMap_Test::loadSaveXML()
{
    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QDir dir(INTERNAL_PROFILEDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtInputProfile));

    QString saved;
    {
        InputOutputMap im(m_doc, 4);
        im.loadProfiles(dir);

        // wrong root tag
        {
            QXmlStreamReader wrong("<Foo/>");
            wrong.readNextStartElement();
            QVERIFY(im.loadXML(wrong) == false);
            QCOMPARE(im.universesCount(), quint32(4));
        }

        QString xml = writeIOMapXML(true);
        QXmlStreamReader reader(xml);
        reader.readNextStartElement();
        QCOMPARE(reader.name().toString(), QString("InputOutputMap"));
        QVERIFY(im.loadXML(reader) == true);

        QCOMPARE(im.universesCount(), quint32(3));
        QCOMPARE(im.getUniverseNameByIndex(0), QString("First"));
        QCOMPARE(im.getUniverseNameByIndex(1), QString("Second"));
        QCOMPARE(im.getUniverseNameByIndex(2), QString("Universe 3"));
        QCOMPARE(im.getUniversePassthrough(0), true);
        QCOMPARE(im.getUniversePassthrough(1), false);

        QCOMPARE(im.beatGeneratorType(), InputOutputMap::Internal);
        QCOMPARE(im.bpmNumber(), 90);
        QCOMPARE(im.networkServerType(), int(InputOutputMap::NativeServer | InputOutputMap::WebServer));
        QCOMPARE(im.networkServerAutoStart(), true);
        QCOMPARE(im.networkServerName(), QString("Console"));
        QCOMPARE(im.networkServerPassword(), QString("secret"));

        // universe 0
        InputPatch *ip = im.inputPatch(0);
        QVERIFY(ip != NULL);
        QVERIFY(ip->plugin() == stub);
        QCOMPARE(ip->input(), quint32(0));
        QCOMPARE(ip->profileName(), QString("Generic MIDI"));
        QCOMPARE(ip->getPluginParameters().value("inParam").toString(), QString("in-value"));
        QCOMPARE(im.outputPatchesCount(0), 0);
        OutputPatch *fb = im.feedbackPatch(0);
        QVERIFY(fb != NULL);
        QCOMPARE(fb->output(), quint32(2));
        QCOMPARE(fb->getPluginParameters().value("fbParam").toString(), QString("fb-value"));

        // universe 1
        QVERIFY(im.inputPatch(1) == NULL);
        QCOMPARE(im.outputPatchesCount(1), 1);
        OutputPatch *op = im.outputPatch(1, 0);
        QVERIFY(op != NULL);
        QCOMPARE(op->output(), quint32(1));
        QCOMPARE(op->outputName(), QString("2: Stub 2"));
        QCOMPARE(op->getPluginParameters().value("outParam").toString(), QString("out-value"));

        // universe 2
        QCOMPARE(im.outputPatchesCount(2), 2);
        QCOMPARE(im.outputPatch(2, 0)->output(), quint32(3));
        QCOMPARE(im.outputPatch(2, 1)->output(), quint32(0));

        QBuffer out;
        out.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter writer(&out);
        QVERIFY(im.saveXML(&writer) == true);
        out.close();
        saved = QString::fromUtf8(out.data());
    }

    QVERIFY(saved.contains("<InputOutputMap>"));
    QVERIFY(saved.contains("BeatType=\"Internal\""));
    QVERIFY(saved.contains("BPM=\"90\""));
    QVERIFY(saved.contains("Type=\"Native|Web\""));
    QVERIFY(saved.contains("AutoStart=\"True\""));
    QVERIFY(saved.contains("Name=\"Console\""));
    QVERIFY(saved.contains("Password=\"secret\""));
    QVERIFY(saved.contains("Passthrough=\"True\""));
    QVERIFY(saved.contains("inParam=\"in-value\""));
    QVERIFY(saved.contains("fbParam=\"fb-value\""));
    QVERIFY(saved.contains("outParam=\"out-value\""));
    QVERIFY(saved.contains("Profile=\"Generic MIDI\""));
    QCOMPARE(saved.count("<Universe "), 3);
    QCOMPARE(saved.count("<Output "), 3);
    QCOMPARE(saved.count("<Input "), 1);
    QCOMPARE(saved.count("<Feedback "), 1);

    // the saved document loads back into an equivalent map
    {
        InputOutputMap im(m_doc, 1);
        im.loadProfiles(dir);
        QXmlStreamReader reader(saved);
        reader.readNextStartElement();
        QVERIFY(im.loadXML(reader) == true);

        QCOMPARE(im.universesCount(), quint32(3));
        QCOMPARE(im.getUniverseNameByIndex(0), QString("First"));
        QCOMPARE(im.getUniversePassthrough(0), true);
        QCOMPARE(im.beatGeneratorType(), InputOutputMap::Internal);
        QCOMPARE(im.bpmNumber(), 90);
        QCOMPARE(im.networkServerName(), QString("Console"));

        QVERIFY(im.inputPatch(0) != NULL);
        QCOMPARE(im.inputPatch(0)->input(), quint32(0));
        QCOMPARE(im.inputPatch(0)->profileName(), QString("Generic MIDI"));
        QCOMPARE(im.inputPatch(0)->getPluginParameters().value("inParam").toString(), QString("in-value"));
        QVERIFY(im.feedbackPatch(0) != NULL);
        QCOMPARE(im.feedbackPatch(0)->output(), quint32(2));
        QCOMPARE(im.feedbackPatch(0)->getPluginParameters().value("fbParam").toString(), QString("fb-value"));
        QCOMPARE(im.outputPatchesCount(1), 1);
        QCOMPARE(im.outputPatch(1, 0)->output(), quint32(1));
        QCOMPARE(im.outputPatch(1, 0)->getPluginParameters().value("outParam").toString(), QString("out-value"));
        QCOMPARE(im.outputPatchesCount(2), 2);
        QCOMPARE(im.outputPatch(2, 0)->output(), quint32(3));
        QCOMPARE(im.outputPatch(2, 1)->output(), quint32(0));

        im.setBeatGeneratorType(InputOutputMap::Disabled);
    }

    m_doc->masterTimer()->requestBpmNumber(120);
}

void InputOutputMap_Test::loadXMLWebOnlyAndUnknownTags()
{
    InputOutputMap im(m_doc, 2);
    im.setNetworkServerName("stale");
    im.setNetworkServerPassword("stale");

    QString xml = writeIOMapXML(false);
    QXmlStreamReader reader(xml);
    reader.readNextStartElement();
    QVERIFY(im.loadXML(reader) == true);

    // a web-only server carries no native credentials
    QCOMPARE(im.networkServerType(), int(InputOutputMap::WebServer));
    QCOMPARE(im.networkServerAutoStart(), true);
    QVERIFY(im.networkServerName().isEmpty());
    QVERIFY(im.networkServerPassword().isEmpty());

    QBuffer out;
    out.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter writer(&out);
    QVERIFY(im.saveXML(&writer) == true);
    out.close();
    QString saved = QString::fromUtf8(out.data());
    QVERIFY(saved.contains("Type=\"Web\""));
    QVERIFY(saved.contains("Name=\"Console\"") == false);
    QVERIFY(saved.contains("Password=") == false);

    im.setBeatGeneratorType(InputOutputMap::Disabled);
    m_doc->masterTimer()->requestBpmNumber(120);
}

// InputOutputMap_Test::profileDirectories() exercises InputOutputMap::
// systemProfileDirectory(), which on WIN32/APPLE builds QLCFile::systemDirectory()
// paths from QCoreApplication::applicationDirPath() - a static method that
// requires an application instance to exist (see QLCFile::systemDirectory() in
void InputOutputMap_Test::inputPatchBeatsPlugin()
{
    InputOutputMap im(m_doc, 2);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    // a plugin advertising beat events gets its values routed to the beat
    // slot as well, on every patch, re-patch and un-patch
    stub->m_extraCapabilities = QLCIOPlugin::Beats;
    QVERIFY(stub->capabilities() & QLCIOPlugin::Beats);

    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0) == true);
    QVERIFY(im.inputPatch(0) != NULL);
    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(1), 1) == true);
    QCOMPARE(im.inputPatch(0)->input(), quint32(1));

    // a plugin beat is only counted while the plugin is the beat source
    // (patches buffer their values, hence the flush)
    im.setBeatGeneratorType(InputOutputMap::Plugin);
    QSignalSpy beatSpy(&im, SIGNAL(beat()));
    stub->emitValueChanged(UINT_MAX, 1, 0, UCHAR_MAX, "beat");
    im.flushInputs();
    QCOMPARE(beatSpy.size(), 1);
    stub->emitValueChanged(UINT_MAX, 1, 1, UCHAR_MAX, "foo");
    im.flushInputs();
    QCOMPARE(beatSpy.size(), 1);
    im.setBeatGeneratorType(InputOutputMap::Disabled);

    QVERIFY(im.setInputPatch(0, stub->name(), "", "", QLCIOPlugin::invalidLine()) == true);
    QVERIFY(im.inputPatch(0) == NULL);

    stub->m_extraCapabilities = 0;
    QVERIFY((stub->capabilities() & QLCIOPlugin::Beats) == 0);
}

void InputOutputMap_Test::inputPatchUnknownPlugin()
{
    InputOutputMap im(m_doc, 1);

    IOPluginStub* stub = static_cast<IOPluginStub*>
                                (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);
    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(0), 0) == true);

    // re-patching an existing patch to a plugin that isn't loaded fails and
    // leaves a patch without any plugin behind
    QVERIFY(im.setInputPatch(0, "No Such Plugin", "", "", 0) == false);
    QVERIFY(im.inputPatch(0) != NULL);
    QVERIFY(im.inputPatch(0)->plugin() == NULL);

    QString uni, ch;
    QVERIFY(im.inputSourceNames(new QLCInputSource(0, 3), uni, ch) == true);
    QCOMPARE(uni, QString("1: ??"));
    QCOMPARE(ch, QString("4: ?"));

    // patching it again must cope with the plugin-less patch
    QVERIFY(im.setInputPatch(0, stub->name(), "", stub->inputs().at(1), 1) == true);
    QVERIFY(im.inputPatch(0)->plugin() == stub);
    QCOMPARE(im.inputPatch(0)->input(), quint32(1));
}

/** An AudioCapture that never touches an audio device: its initialisation
    fails on purpose, so the capture thread ends right after it starts. */
class StubAudioCapture final : public AudioCapture
{
public:
    StubAudioCapture() : AudioCapture(), initializeCalls(0) {}
    ~StubAudioCapture() override { wait(5000); }

    void setVolume(qreal) override {}
    bool initialize() override { initializeCalls.ref(); return false; }
    void uninitialize() override {}
    void suspend() override {}
    void resume() override {}
    qint64 latency() const override { return 0; }
    bool readAudio(int) override { return false; }

    QAtomicInt initializeCalls;
};

void InputOutputMap_Test::beatGeneratorAudio()
{
    InputOutputMap im(m_doc, 1);

    // hand the doc a capture stub so no real audio device is ever opened
    StubAudioCapture *capture = new StubAudioCapture();
    QSharedPointer<AudioCapture> shared(capture);
    m_doc->m_inputCapture = shared;

    QSignalSpy typeSpy(&im, SIGNAL(beatGeneratorTypeChanged()));
    im.setBeatGeneratorType(InputOutputMap::Audio);
    QCOMPARE(im.beatGeneratorType(), InputOutputMap::Audio);
    QCOMPARE(typeSpy.size(), 1);
    QCOMPARE(m_doc->masterTimer()->beatSourceType(), MasterTimer::External);
    QVERIFY(im.m_inputCapture.data() == capture);

    // registering the bands started the capture thread, whose
    // initialisation fails in the stub, so it stops again by itself
    QTRY_VERIFY_WITH_TIMEOUT(capture->initializeCalls.loadAcquire() == 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(capture->isRunning() == false, 5000);

    // a beat detected by the capture reaches the map's beat signal
    QSignalSpy beatSpy(&im, SIGNAL(beat()));
    emit capture->beatDetected(120);
    QCOMPARE(beatSpy.size(), 1);
    QCOMPARE(im.bpmNumber(), 120);

    // leaving audio mode unregisters the bands and drops the capture
    im.setBeatGeneratorType(InputOutputMap::Disabled);
    QCOMPARE(im.beatGeneratorType(), InputOutputMap::Disabled);
    QVERIFY(im.m_inputCapture.isNull());
    QCOMPARE(m_doc->masterTimer()->beatSourceType(), MasterTimer::None);

    m_doc->destroyAudioCapture();
    QVERIFY(m_doc->m_inputCapture.isNull());
    shared.clear();
}

void InputOutputMap_Test::defaultArgOverload()
{
    InputOutputMap im(m_doc, 1);
    QSignalSpy spy(&im, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)));

    // the moc-generated overload for the defaulted key argument
    emit im.inputValueChanged(0, 7, 42);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(1).toUInt(), quint32(7));
    QCOMPARE(spy.at(0).at(2).toUInt(), uint(42));
    QVERIFY(spy.at(0).at(3).toString().isEmpty());
}

// qlcfile.cpp). QTEST_APPLESS_MAIN doesn't create one, so applicationDirPath()
// warned and returned an empty string there, same failure on both platforms.
// engine/test/rgbscript/rgbscript_test.cpp exercises the equivalent
// systemScriptsDirectory()/applicationDirPath() pairing and already uses
// QTEST_MAIN for the same reason.
QTEST_MAIN(InputOutputMap_Test)
