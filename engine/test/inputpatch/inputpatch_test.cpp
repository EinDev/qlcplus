/*
  Q Light Controller - Unit test
  inputpatch_test.cpp

  Copyright (c) Heikki Junnila

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
#include "iopluginstub.h"
#include "inputpatch_test.h"
#include "qlcinputprofile.h"
#include "qlcinputchannel.h"
#include "qlcioplugin.h"
#include "inputpatch.h"
#include "qlcfile.h"
#include "doc.h"
#undef private

#define TESTPLUGINDIR "../iopluginstub"

static QDir testPluginDir()
{
    QDir dir(TESTPLUGINDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtPlugin));
    return dir;
}

void InputPatch_Test::initTestCase()
{
    m_doc = new Doc(this);
    m_doc->ioPluginCache()->load(testPluginDir());
    QVERIFY(m_doc->ioPluginCache()->plugins().size() != 0);
}

void InputPatch_Test::cleanupTestCase()
{
    delete m_doc;
    m_doc = NULL;
}

void InputPatch_Test::defaults()
{
    InputPatch ip(0, this);
    QVERIFY(ip.m_plugin == NULL);
    QVERIFY(ip.m_pluginLine == QLCIOPlugin::invalidLine());
    QVERIFY(ip.m_profile == NULL);
    QVERIFY(ip.m_pageSetCh == USHRT_MAX);
    QVERIFY(ip.pluginName() == KInputNone);
    QVERIFY(ip.inputName() == KInputNone);
    QVERIFY(ip.profileName() == KInputNone);
    QVERIFY(ip.isPatched() == false);
    QVERIFY(ip.input() == QLCIOPlugin::invalidLine());

    InputPatch ip2(this);
    QVERIFY(ip2.m_plugin == NULL);
    QVERIFY(ip2.m_pluginLine == QLCIOPlugin::invalidLine());
    QVERIFY(ip2.m_profile == NULL);
    QVERIFY(ip2.m_pageSetCh == USHRT_MAX);
    QVERIFY(ip2.pluginName() == KInputNone);
    QVERIFY(ip2.inputName() == KInputNone);
    QVERIFY(ip2.profileName() == KInputNone);
    QVERIFY(ip2.isPatched() == false);
    QVERIFY(ip2.input() == QLCIOPlugin::invalidLine());
}

void InputPatch_Test::patch()
{
    QCOMPARE(m_doc->ioPluginCache()->plugins().size(), 1);
    IOPluginStub* stub = static_cast<IOPluginStub*> (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QLCInputProfile prof1;
    prof1.setManufacturer("Foo");
    prof1.setManufacturer("Bar");

    InputPatch* ip = new InputPatch(0, this);
    QVERIFY(ip->set(stub, 0, &prof1) == true);
    QVERIFY(ip->m_plugin == stub);
    QVERIFY(ip->m_pluginLine == 0);
    QVERIFY(ip->m_profile == &prof1);
    QVERIFY(ip->pluginName() == stub->name());
    QVERIFY(ip->inputName() == stub->inputs()[0]);
    QVERIFY(ip->profileName() == prof1.name());
    QVERIFY(ip->isPatched() == true);
    QVERIFY(stub->m_openInputs.size() == 1);
    QVERIFY(stub->m_openInputs.at(0) == 0);

    QLCInputProfile prof2;
    prof2.setManufacturer("Xyzzy");
    prof2.setManufacturer("Foobar");

    QVERIFY(ip->set(stub, 3, &prof2) == true);
    QVERIFY(ip->m_plugin == stub);
    QVERIFY(ip->m_pluginLine == 3);
    QVERIFY(ip->m_profile == &prof2);
    QVERIFY(ip->pluginName() == stub->name());
    QVERIFY(ip->inputName() == stub->inputs()[3]);
    QVERIFY(ip->profileName() == prof2.name());
    QVERIFY(stub->m_openInputs.size() == 1);
    QVERIFY(stub->m_openInputs.at(0) == 3);

    ip->reconnect();
    QVERIFY(ip->m_plugin == stub);
    QVERIFY(ip->m_pluginLine == 3);
    QVERIFY(ip->m_profile == &prof2);
    QVERIFY(ip->pluginName() == stub->name());
    QVERIFY(ip->inputName() == stub->inputs()[3]);
    QVERIFY(ip->profileName() == prof2.name());
    QVERIFY(stub->m_openInputs.size() == 1);
    QVERIFY(stub->m_openInputs.at(0) == 3);

    QVERIFY(ip->set(&prof1) == true);
    QVERIFY(ip->m_plugin == stub);
    QVERIFY(ip->m_pluginLine == 3);
    QVERIFY(ip->m_profile == &prof1);
    QVERIFY(ip->pluginName() == stub->name());
    QVERIFY(ip->inputName() == stub->inputs()[3]);
    QVERIFY(ip->profileName() == prof1.name());
    QVERIFY(stub->m_openInputs.size() == 1);
    QVERIFY(stub->m_openInputs.at(0) == 3);

    delete ip;
    QVERIFY(stub->m_openInputs.size() == 0);

    InputPatch* ip2 = new InputPatch(0, this);
    QVERIFY(ip2->set(&prof1) == false);
}

void InputPatch_Test::parameters()
{
    InputPatch* ip = new InputPatch(0, this);
    IOPluginStub* stub = static_cast<IOPluginStub*> (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QVERIFY(ip->set(stub, 0, NULL) == true);
    QVERIFY(ip->m_plugin == stub);

    ip->setPluginParameter("Foo", 42);

    QVERIFY(ip->m_parametersCache.count() == 1);
    QVERIFY(ip->getPluginParameters().count() == 1);
    QVERIFY(ip->getPluginParameters().key(42) == "Foo");
    QVERIFY(ip->getPluginParameters().value("Foo") == 42);

    delete ip;
}

void InputPatch_Test::uidAndReconnect()
{
    IOPluginStub* stub = static_cast<IOPluginStub*> (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    InputPatch ip(0, this);

    // nothing patched yet
    QVERIFY(ip.inputUID() == KInputNone);
    QVERIFY(ip.reconnect() == false);
    QVERIFY(ip.getPluginParameters().isEmpty());

    QVERIFY(ip.set(stub, 1, NULL) == true);
    QCOMPARE(ip.inputUID(), stub->inputsUID().at(1));
    QCOMPARE(ip.inputName(), stub->inputs().at(1));
    QVERIFY(ip.isPatched() == true);

    // cached parameters are pushed to the plugin again after a reconnect
    ip.setPluginParameter("Foo", 42);
    ip.setPluginParameter("Bar", "baz");
    QCOMPARE(ip.getPluginParameters().value("Foo").toInt(), 42);
    QCOMPARE(ip.getPluginParameters().value("Bar").toString(), QString("baz"));
    QVERIFY(ip.reconnect() == true);
    QCOMPARE(stub->m_openInputs.size(), 1);
    QCOMPARE(stub->m_openInputs.at(0), quint32(1));
    QCOMPARE(ip.getPluginParameters().value("Foo").toInt(), 42);
    QCOMPARE(ip.getPluginParameters().value("Bar").toString(), QString("baz"));

    // an invalid line reports no UID/name and cannot reconnect
    QVERIFY(ip.set(stub, QLCIOPlugin::invalidLine(), NULL) == false);
    QVERIFY(ip.inputUID() == KInputNone);
    QVERIFY(ip.inputName() == KInputNone);
    QVERIFY(ip.isPatched() == false);
    QVERIFY(ip.reconnect() == false);
    QCOMPARE(stub->m_openInputs.size(), 0);

    // a line beyond the plugin's inputs has no UID/name either
    QVERIFY(ip.set(stub, 42, NULL) == true);
    QVERIFY(ip.inputUID() == KInputNone);
    QVERIFY(ip.inputName() == KInputNone);
    QVERIFY(ip.isPatched() == true);
}

void InputPatch_Test::profilePageControls()
{
    IOPluginStub* stub = static_cast<IOPluginStub*> (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);

    QLCInputProfile prof;
    prof.setManufacturer("Page");
    prof.setModel("Controls");
    // a non-empty global settings map is pushed to the plugin as parameters
    prof.setMidiSendNoteOff(false);

    QLCInputChannel *slider = new QLCInputChannel();
    slider->setType(QLCInputChannel::Slider);
    QVERIFY(prof.insertChannel(0, slider) == true);
    QLCInputChannel *next = new QLCInputChannel();
    next->setType(QLCInputChannel::NextPage);
    QVERIFY(prof.insertChannel(10, next) == true);
    QLCInputChannel *prev = new QLCInputChannel();
    prev->setType(QLCInputChannel::PrevPage);
    QVERIFY(prof.insertChannel(11, prev) == true);
    QLCInputChannel *pageSet = new QLCInputChannel();
    pageSet->setType(QLCInputChannel::PageSet);
    QVERIFY(prof.insertChannel(12, pageSet) == true);
    // a second next-page control does not override the first one found
    QLCInputChannel *next2 = new QLCInputChannel();
    next2->setType(QLCInputChannel::NextPage);
    QVERIFY(prof.insertChannel(20, next2) == true);

    InputPatch ip(0, this);
    QCOMPARE(ip.m_nextPageCh, ushort(USHRT_MAX));
    QVERIFY(ip.set(stub, 2, &prof) == true);
    QCOMPARE(ip.m_nextPageCh, ushort(10));
    QCOMPARE(ip.m_prevPageCh, ushort(11));
    QCOMPARE(ip.m_pageSetCh, ushort(12));
    QVERIFY(ip.getPluginParameters().contains("MIDISendNoteOff"));
    QCOMPARE(ip.getPluginParameters().value("MIDISendNoteOff").toBool(), false);

    // assigning the profile to an already patched line takes the same path
    InputPatch ip2(1, this);
    QVERIFY(ip2.set(stub, 3, NULL) == true);
    QCOMPARE(ip2.m_nextPageCh, ushort(USHRT_MAX));
    QVERIFY(ip2.getPluginParameters().contains("MIDISendNoteOff") == false);
    QVERIFY(ip2.set(&prof) == true);
    QCOMPARE(ip2.m_nextPageCh, ushort(10));
    QCOMPARE(ip2.m_prevPageCh, ushort(11));
    QCOMPARE(ip2.m_pageSetCh, ushort(12));
    QVERIFY(ip2.getPluginParameters().contains("MIDISendNoteOff"));
    QCOMPARE(ip2.profileName(), prof.name());
}

void InputPatch_Test::defaultArgOverloads()
{
    InputPatch ip(0, this);
    IOPluginStub* stub = static_cast<IOPluginStub*> (m_doc->ioPluginCache()->plugins().at(0));
    QVERIFY(stub != NULL);
    QVERIFY(ip.set(stub, 0, NULL) == true);

    QSignalSpy spy(&ip, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)));

    // the moc-generated overloads for the defaulted key argument: a first
    // value is only buffered, a later change to zero passes straight through
    ip.slotValueChanged(0, 0, 3, 150);
    QCOMPARE(spy.size(), 0);
    QVERIFY(ip.m_inputBuffer.contains(3));
    QCOMPARE(ip.m_inputBuffer.value(3).value, uchar(150));
    QVERIFY(ip.m_inputBuffer.value(3).key.isEmpty());

    ip.slotValueChanged(0, 0, 3, 0);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.at(0).at(1).toUInt(), quint32(3));
    QCOMPARE(spy.at(0).at(2).toUInt(), uint(150));

    emit ip.inputValueChanged(0, 4, 7);
    QCOMPARE(spy.size(), 2);
    QCOMPARE(spy.at(1).at(1).toUInt(), quint32(4));

    // a default-constructed buffer entry
    InputPatch::InputValue value;
    value.value = 9;
    value.key = "key";
    QCOMPARE(value.value, uchar(9));
    QCOMPARE(value.key, QString("key"));
}

QTEST_APPLESS_MAIN(InputPatch_Test)
