/*
  Q Light Controller Plus - Unit test
  scriptv4_test.cpp

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

// NOTE: this is the "Script" implementation actually compiled for the QLC+ 5
// qmlui build (engine/src/scriptv4.h + scriptv4.cpp), not the legacy
// engine/src/script.h/.cpp pair - the two headers happen to declare a class
// with the same name ("Script") but different members/semantics; only one of
// them is ever compiled into qlcplusengine for a given build (see
// engine/src/CMakeLists.txt's `if(qmlui) ... else() script.cpp ... endif()`).
// This suite therefore includes scriptv4.h explicitly to test the real thing.
//
// The same build condition holds for ScriptRunner (engine/src/scriptrunner.cpp),
// the QThread + QJSEngine host that actually executes a Script's JavaScript,
// so it is covered from this suite as well rather than from a separate one.

#include <QtTest>
#include <QBuffer>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#define private public
#define protected public
#include "scriptv4_test.h"
#include "mastertimer_stub.h"
#include "scriptrunner.h"
#include "genericfader.h"
#include "fadechannel.h"
#include "universe.h"
#include "fixture.h"
#include "scene.h"
#include "scriptv4.h"
#include "doc.h"
#undef protected
#undef private

/****************************************************************************
 * Fixture / cleanup
 ****************************************************************************/

void ScriptV4_Test::init()
{
    m_doc = new Doc(this);

    // Generic 4-channel dimmer at the start of universe 0
    m_fixture = new Fixture(m_doc);
    m_fixture->setName("Dimmer");
    m_fixture->setChannels(4);
    m_fixture->setAddress(0);
    m_fixture->setUniverse(0);
    QVERIFY(m_doc->addFixture(m_fixture));

    // Generic 4-channel dimmer whose last channels fall past the end of the
    // universe (address 510 + channel 2 == 512) - for setFixture()'s
    // address-range check
    m_edgeFixture = new Fixture(m_doc);
    m_edgeFixture->setName("Edge");
    m_edgeFixture->setChannels(4);
    m_edgeFixture->setAddress(510);
    m_edgeFixture->setUniverse(0);
    QVERIFY(m_doc->addFixture(m_edgeFixture));

    m_scene1 = new Scene(m_doc);
    m_scene1->setName("S1");
    m_scene1->setValue(m_fixture->id(), 0, 100);
    QVERIFY(m_doc->addFunction(m_scene1));

    m_scene2 = new Scene(m_doc);
    m_scene2->setName("S2");
    m_scene2->setValue(m_fixture->id(), 1, 200);
    QVERIFY(m_doc->addFunction(m_scene2));
}

void ScriptV4_Test::cleanup()
{
    delete m_doc;
    m_doc = NULL;
}

/****************************************************************************
 * Static conversion helpers
 ****************************************************************************/

void ScriptV4_Test::convertLegacyMethodMapsKnownKeywords()
{
    QCOMPARE(Script::convertLegacyMethod("stoponexit"), Script::stopOnExitCmd);
    QCOMPARE(Script::convertLegacyMethod("startfunction"), Script::startFunctionCmd);
    QCOMPARE(Script::convertLegacyMethod("stopfunction"), Script::stopFunctionCmd);
    QCOMPARE(Script::convertLegacyMethod("blackout"), Script::blackoutCmd);
    QCOMPARE(Script::convertLegacyMethod("wait"), Script::waitCmd);
    QCOMPARE(Script::convertLegacyMethod("waitfunctionstart"), Script::waitFunctionStartCmd);
    QCOMPARE(Script::convertLegacyMethod("waitfunctionstop"), Script::waitFunctionStopCmd);
    QCOMPARE(Script::convertLegacyMethod("setfixture"), Script::setFixtureCmd);
    QCOMPARE(Script::convertLegacyMethod("systemcommand"), Script::systemCmd);

    // An unrecognized legacy keyword must not crash and must yield an empty
    // JS method name, rather than e.g. echoing the input back.
    QCOMPARE(Script::convertLegacyMethod("notarealcommand"), QString(""));
}

void ScriptV4_Test::convertLineWaitPlainNumber()
{
    bool ok = false;
    QString result = Script::convertLine("wait:1.5\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.waitTime(1.5);\n"));
}

void ScriptV4_Test::convertLineWaitWithTimeUnitIsQuoted()
{
    // A "wait" value containing a time unit suffix (s/m/h) must be quoted in
    // the generated JS call, unlike a bare millisecond number.
    bool ok = false;
    QString result = Script::convertLine("wait:2s\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.waitTime(\"2s\");\n"));
}

void ScriptV4_Test::convertLineBlackoutOnOff()
{
    bool ok = false;
    QString onResult = Script::convertLine("blackout:on\n", &ok);
    QVERIFY(ok);
    QCOMPARE(onResult, QString("Engine.setBlackout(true);\n"));

    ok = false;
    QString offResult = Script::convertLine("blackout:off\n", &ok);
    QVERIFY(ok);
    QCOMPARE(offResult, QString("Engine.setBlackout(false);\n"));

    // Anything else is passed through as a quoted string
    ok = false;
    QString otherResult = Script::convertLine("blackout:maybe\n", &ok);
    QVERIFY(ok);
    QCOMPARE(otherResult, QString("Engine.setBlackout(\"maybe\");\n"));
}

void ScriptV4_Test::convertLineQuotedValueConvertsToSingleQuotes()
{
    // Legacy quoted values used double quotes; the generated JS argument
    // must use single quotes instead (double quotes would break the
    // generated method-call string literal).
    bool ok = false;
    QString result = Script::convertLine("waitfunctionstart:\"12\"\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.waitFunctionStart('12');\n"));
}

void ScriptV4_Test::convertLineRandomValueConvertsToEngineRandomCall()
{
    bool ok = false;
    QString result = Script::convertLine("wait:random(10,20)\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.waitTime(Engine.random(10,20));\n"));

    // Time-unit bounds get quoted so Engine.random(QString, QString) is picked
    ok = false;
    result = Script::convertLine("wait:random(1s,2m)\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.waitTime(Engine.random(\"1s\",\"2m\"));\n"));
}

void ScriptV4_Test::convertLineMissingColonIsSyntaxError()
{
    bool ok = true;
    Script::convertLine("nocolonhere\n", &ok);
    QVERIFY(ok == false);

    // The ok flag is optional
    Script::convertLine("nocolonhere\n");
}

void ScriptV4_Test::convertLineCommentsUrlsAndUnbalancedQuotes()
{
    // Pure comment lines and blank lines are returned untouched
    bool ok = false;
    QCOMPARE(Script::convertLine("// just a comment\n", &ok), QString("// just a comment\n"));
    QVERIFY(ok);
    QCOMPARE(Script::convertLine("   \n", &ok), QString("   \n"));
    QVERIFY(ok);

    // A trailing comment is preserved after the converted command
    ok = false;
    QString result = Script::convertLine("wait:100 // pause\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.waitTime(100); // pause\n"));

    // "://" inside a value is a URL, not a comment marker
    ok = false;
    result = Script::convertLine("systemcommand:http://example.org/x\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.systemCommand(\"http://example.org/x\");\n"));

    // An opening quote without a closing one is a syntax error
    ok = true;
    Script::convertLine("startfunction:\"12\n", &ok);
    QVERIFY(ok == false);

    // A value without any trailing whitespace/newline is a syntax error too
    ok = true;
    Script::convertLine("wait:100", &ok);
    QVERIFY(ok == false);
}

void ScriptV4_Test::convertLineSystemCommandAndUnknownKeyword()
{
    // systemcommand joins all its arguments into one quoted string
    bool ok = false;
    QString result = Script::convertLine("systemcommand:echo arg:hello arg:world\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.systemCommand(\"echo hello world\");\n"));

    // setfixture with the known ch/val keywords
    ok = false;
    result = Script::convertLine("setfixture:5 ch:10 val:200\n", &ok);
    QVERIFY(ok);
    QCOMPARE(result, QString("Engine.setFixture(5,10,200);\n"));

    // Any keyword other than ch/val/arg after the command is an error
    ok = true;
    result = Script::convertLine("setfixture:5 bogus:10\n", &ok);
    QVERIFY(ok == false);
    // ...but the tokens parsed so far still produce a call
    QVERIFY(result.startsWith("Engine.setFixture(5"));
}

void ScriptV4_Test::getValueFromStringPlainAndRandomRange()
{
    bool ok = false;
    QCOMPARE(Script::getValueFromString("1500", &ok), quint32(1500));
    QVERIFY(ok);

    ok = false;
    QCOMPARE(Script::getValueFromString("2s", &ok), quint32(2000));
    QVERIFY(ok);

    for (int i = 0; i < 25; i++)
    {
        ok = false;
        quint32 value = Script::getValueFromString("random(10,20)", &ok);
        QVERIFY(ok);
        QVERIFY(value >= 10 && value <= 20);
    }

    // A random() without a comma-separated range cannot be parsed
    ok = false;
    QCOMPARE(Script::getValueFromString("random(10)", &ok), quint32(-1));
    QVERIFY(ok == false);
}

void ScriptV4_Test::functionAndFixtureListParseConvertedSyntax()
{
    Script scr(m_doc);

    // appendData() runs each line through convertLine(), same as loading a
    // legacy .qxw script would (loadXML() feeds it one <Command> element's
    // text at a time, with no embedded newline) - exercise
    // functionList()/fixtureList() against the resulting JS-flavoured data,
    // not hand-written JS.
    scr.appendData("startfunction:12");
    scr.appendData("setfixture:5 ch:10 val:200");
    scr.appendData("wait:1");
    scr.appendData("startfunction:33");

    QList<quint32> functions = scr.functionList();
    QCOMPARE(functions.size(), 4);
    QCOMPARE(functions.at(0), quint32(12));
    QCOMPARE(functions.at(1), quint32(0)); // line index of first startfunction
    QCOMPARE(functions.at(2), quint32(33));
    QCOMPARE(functions.at(3), quint32(3)); // line index of second startfunction

    QList<quint32> fixtures = scr.fixtureList();
    QCOMPARE(fixtures.size(), 1);
    QCOMPARE(fixtures.at(0), quint32(5));
}

void ScriptV4_Test::functionAndFixtureListSkipMalformedLines()
{
    Script scr(m_doc);
    scr.setData("Engine.startFunction(\n"           // no closing parenthesis
                "Engine.stopFunction\n"             // no parenthesis at all
                "Engine.stopFunction(7);\n"         // ok
                "Engine.startFunction(7, 1);\n"     // duplicate id, ignored
                "Engine.setFixture\n"               // no parenthesis at all
                "Engine.setFixture(3, 0, 255);\n"   // ok
                "Engine.setFixture(3, 1, 0);\n");   // duplicate id, ignored

    QList<quint32> functions = scr.functionList();
    QCOMPARE(functions.size(), 2);
    QCOMPARE(functions.at(0), quint32(7));
    QCOMPARE(functions.at(1), quint32(2));

    QList<quint32> fixtures = scr.fixtureList();
    QCOMPARE(fixtures.size(), 1);
    QCOMPARE(fixtures.at(0), quint32(3));
}

/****************************************************************************
 * Script data / copying / XML
 ****************************************************************************/

void ScriptV4_Test::initialAndIcon()
{
    Script scr(m_doc);
    QCOMPARE(scr.type(), Function::ScriptType);
    QCOMPARE(scr.name(), QString("New Script"));
    QVERIFY(scr.data().isEmpty());
    QVERIFY(scr.dataLines().isEmpty());
    QVERIFY(scr.m_runner == NULL);

    // Only exercised, not asserted on: the resource may or may not be
    // compiled into the engine library
    QIcon icon = scr.getIcon();
    Q_UNUSED(icon);
}

void ScriptV4_Test::setDataAndDataLines()
{
    Script scr(m_doc);
    m_doc->resetModified();

    QVERIFY(scr.setData("Engine.waitTime(10);\r\nEngine.waitTime(20);\n\rEngine.waitTime(30);\r\n\n"));
    QVERIFY(m_doc->isModified());
    QCOMPARE(scr.data(), QString("Engine.waitTime(10);\r\nEngine.waitTime(20);\n\rEngine.waitTime(30);\r\n\n"));

    // Every line ending flavour splits, trailing empty lines are dropped
    QStringList lines = scr.dataLines();
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0), QString("Engine.waitTime(10);"));
    QCOMPARE(lines.at(1), QString("Engine.waitTime(20);"));
    QCOMPARE(lines.at(2), QString("Engine.waitTime(30);"));

    // Setting the same data again is a no-op
    QVERIFY(scr.setData(scr.data()) == false);

    // appendData() converts legacy syntax and adds a newline
    QVERIFY(scr.appendData("blackout:on"));
    QCOMPARE(scr.dataLines().size(), 4);
    QCOMPARE(scr.dataLines().last(), QString("Engine.setBlackout(true);"));
}

void ScriptV4_Test::copyFromAndCreateCopy()
{
    Script scr(m_doc);
    scr.setName("Original");
    scr.setData("Engine.waitTime(10);\n");
    scr.setRunOrder(Function::SingleShot);

    // Copying from a non-Script function is refused
    QVERIFY(scr.copyFrom(m_scene1) == false);
    QCOMPARE(scr.data(), QString("Engine.waitTime(10);\n"));

    // Copy outside of the Doc
    Function *copy = scr.createCopy(m_doc, false);
    QVERIFY(copy != NULL);
    QVERIFY(copy != &scr);
    QCOMPARE(copy->type(), Function::ScriptType);
    QCOMPARE(copy->name(), QString("Original"));
    QCOMPARE(qobject_cast<Script*>(copy)->data(), scr.data());
    QCOMPARE(copy->id(), Function::invalidId());
    delete copy;

    // Copy added to the Doc gets its own valid id
    int before = m_doc->functions().size();
    Function *added = scr.createCopy(m_doc, true);
    QVERIFY(added != NULL);
    QVERIFY(added->id() != Function::invalidId());
    QCOMPARE(m_doc->functions().size(), before + 1);
    QCOMPARE(m_doc->function(added->id()), added);
    QCOMPARE(qobject_cast<Script*>(added)->data(), scr.data());

    // copyFrom() between two scripts replaces the data
    Script other(m_doc);
    other.setData("Engine.waitTime(99);\n");
    QVERIFY(scr.copyFrom(&other));
    QCOMPARE(scr.data(), QString("Engine.waitTime(99);\n"));
}

void ScriptV4_Test::loadXMLRejectsWrongNodes()
{
    // Not a <Function> element
    {
        QBuffer buffer;
        buffer.open(QIODevice::ReadWrite | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Foo");
        xmlWriter.writeAttribute("Type", "Script");
        xmlWriter.writeEndElement();
        xmlWriter.writeEndDocument();
        buffer.seek(0);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();

        Script scr(m_doc);
        QVERIFY(scr.loadXML(xmlReader) == false);
    }

    // A <Function> element of the wrong type
    {
        QBuffer buffer;
        buffer.open(QIODevice::ReadWrite | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Function");
        xmlWriter.writeAttribute("Type", "Scene");
        xmlWriter.writeEndElement();
        xmlWriter.writeEndDocument();
        buffer.seek(0);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();

        Script scr(m_doc);
        QVERIFY(scr.loadXML(xmlReader) == false);
        QVERIFY(scr.data().isEmpty());
    }
}

void ScriptV4_Test::loadXMLLegacyVersionConvertsCommands()
{
    // No Version attribute == version 1 == legacy "keyword:value" syntax,
    // which loadXML() converts to JS one <Command> at a time
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    xmlWriter.writeStartElement("Function");
    xmlWriter.writeAttribute("Type", "Script");
    xmlWriter.writeAttribute("ID", "42");
    xmlWriter.writeAttribute("Name", "Legacy");

    xmlWriter.writeStartElement("Speed");
    xmlWriter.writeAttribute("FadeIn", "100");
    xmlWriter.writeAttribute("FadeOut", "200");
    xmlWriter.writeAttribute("Duration", "300");
    xmlWriter.writeEndElement();

    xmlWriter.writeTextElement("Direction", "Backward");
    xmlWriter.writeTextElement("RunOrder", "SingleShot");

    xmlWriter.writeTextElement("Command", QUrl::toPercentEncoding("startfunction:12"));
    xmlWriter.writeTextElement("Command", QUrl::toPercentEncoding("setfixture:5 ch:1 val:255"));
    xmlWriter.writeTextElement("Command", QUrl::toPercentEncoding("// a comment"));

    // Unknown children are skipped, including their own subtree
    xmlWriter.writeStartElement("Bogus");
    xmlWriter.writeTextElement("Child", "x");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();

    buffer.seek(0);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Script scr(m_doc);
    QVERIFY(scr.loadXML(xmlReader));
    QCOMPARE(scr.fadeInSpeed(), uint(100));
    QCOMPARE(scr.fadeOutSpeed(), uint(200));
    QCOMPARE(scr.duration(), uint(300));
    QCOMPARE(scr.direction(), Function::Backward);
    QCOMPARE(scr.runOrder(), Function::SingleShot);

    QStringList lines = scr.dataLines();
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(0), QString("Engine.startFunction(12);"));
    QCOMPARE(lines.at(1), QString("Engine.setFixture(5,1,255);"));
    QCOMPARE(lines.at(2), QString("// a comment"));
}

void ScriptV4_Test::loadXMLVersion2KeepsCommandsVerbatim()
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    xmlWriter.writeStartElement("Function");
    xmlWriter.writeAttribute("Type", "Script");
    xmlWriter.writeAttribute("ID", "43");
    xmlWriter.writeAttribute("Name", "Modern");
    xmlWriter.writeAttribute("Version", "2");
    xmlWriter.writeTextElement("Command", QUrl::toPercentEncoding("Engine.startFunction(12);"));
    xmlWriter.writeTextElement("Command", QUrl::toPercentEncoding("var x = 1 + 2; // math"));
    xmlWriter.writeEndElement();
    xmlWriter.writeEndDocument();

    buffer.seek(0);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Script scr(m_doc);
    QVERIFY(scr.loadXML(xmlReader));
    QCOMPARE(scr.data(), QString("Engine.startFunction(12);\nvar x = 1 + 2; // math\n"));
}

void ScriptV4_Test::saveXMLRoundTrip()
{
    Script *scr = new Script(m_doc);
    scr->setName("Round trip");
    scr->setDirection(Function::Backward);
    scr->setRunOrder(Function::Loop);
    scr->setFadeInSpeed(11);
    scr->setFadeOutSpeed(22);
    scr->setDuration(33);
    scr->setData("Engine.startFunction(12);\nEngine.setFixture(5,1,255); // note & more\n");
    QVERIFY(m_doc->addFunction(scr));

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(scr->saveXML(&xmlWriter));
    xmlWriter.writeEndDocument();

    buffer.seek(0);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QCOMPARE(xmlReader.name().toString(), QString("Function"));
    QCOMPARE(xmlReader.attributes().value("Type").toString(), QString("Script"));
    QCOMPARE(xmlReader.attributes().value("Version").toString(), QString("2"));
    QCOMPARE(xmlReader.attributes().value("Name").toString(), QString("Round trip"));
    QCOMPARE(xmlReader.attributes().value("ID").toString(), QString::number(scr->id()));

    int speed = 0, dir = 0, run = 0;
    QStringList commands;
    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name() == QString("Speed"))
        {
            speed++;
            QCOMPARE(xmlReader.attributes().value("FadeIn").toString(), QString("11"));
            QCOMPARE(xmlReader.attributes().value("FadeOut").toString(), QString("22"));
            QCOMPARE(xmlReader.attributes().value("Duration").toString(), QString("33"));
            xmlReader.skipCurrentElement();
        }
        else if (xmlReader.name() == QString("Direction"))
        {
            dir++;
            QCOMPARE(xmlReader.readElementText(), QString("Backward"));
        }
        else if (xmlReader.name() == QString("RunOrder"))
        {
            run++;
            QCOMPARE(xmlReader.readElementText(), QString("Loop"));
        }
        else if (xmlReader.name() == QString("Command"))
        {
            QString raw = xmlReader.readElementText();
            // Stored percent-encoded, so the JS never trips the XML parser
            if (commands.size() == 1)
                QVERIFY(raw.contains("%20"));
            commands << QUrl::fromPercentEncoding(raw.toUtf8());
        }
        else
        {
            QFAIL(qPrintable(QString("Unexpected tag: %1").arg(xmlReader.name().toString())));
        }
    }
    QCOMPARE(speed, 1);
    QCOMPARE(dir, 1);
    QCOMPARE(run, 1);
    QCOMPARE(commands.size(), 2);
    QCOMPARE(commands.at(0), QString("Engine.startFunction(12);"));
    QCOMPARE(commands.at(1), QString("Engine.setFixture(5,1,255); // note & more"));

    // Load the same XML back into a fresh Script: data survives unchanged
    buffer.seek(0);
    QXmlStreamReader reReader(&buffer);
    reReader.readNextStartElement();
    Script loaded(m_doc);
    QVERIFY(loaded.loadXML(reReader));
    QCOMPARE(loaded.data(), scr->data());
    QCOMPARE(loaded.direction(), Function::Backward);
    QCOMPARE(loaded.fadeInSpeed(), uint(11));
}

void ScriptV4_Test::syntaxErrorsLines()
{
    Script scr(m_doc);

    // Valid script: no errors
    scr.setData("var a = 1;\nEngine.waitTime(10);\n");
    QVERIFY(scr.syntaxErrorsLines().isEmpty());

    // Empty script: still valid
    scr.setData("");
    QVERIFY(scr.syntaxErrorsLines().isEmpty());

    // Syntax error: the whole program fails to compile
    scr.setData("var a = ;\n");
    QStringList errors = scr.syntaxErrorsLines();
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().startsWith("Uncaught exception at line"));
    QVERIFY(errors.first().contains("SyntaxError"));

    // Runtime error: compiles, but throws when executed
    scr.setData("var a = 1;\nnoSuchFunction();\n");
    errors = scr.syntaxErrorsLines();
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().startsWith("Uncaught exception at line"));
    QVERIFY(errors.first().contains("ReferenceError"));
}

void ScriptV4_Test::totalDuration()
{
    Script scr(m_doc);
    QCOMPARE(scr.totalDuration(), quint32(0));

    // Both waitTime() overloads accumulate ticks even though the script is
    // not "running" during the dry run
    scr.setData("Engine.waitTime(1000);\nEngine.waitTime(\"2s\");\nEngine.waitTime(\"0.5\");\n");
    quint32 expected = (1000 / MasterTimer::tick() + 2000 / MasterTimer::tick() + 500 / MasterTimer::tick())
                        * MasterTimer::tick();
    QCOMPARE(scr.totalDuration(), expected);
    QCOMPARE(expected, quint32(3500));
}

/****************************************************************************
 * ScriptRunner, driven directly (no thread)
 ****************************************************************************/

void ScriptV4_Test::runnerInactiveMethodsRefuse()
{
    ScriptRunner runner(m_doc, QString());
    QVERIFY(runner.m_running == false);
    QCOMPARE(runner.currentWaitTime(), 0);
    QCOMPARE(runner.startFunctionSource().type(), FunctionParent::Master);

    // Every Engine method refuses to act while the script isn't running
    QCOMPARE(runner.getChannelValue(0, 0), 0);
    QVERIFY(runner.setFixture(m_fixture->id(), 0, 255) == false);
    QVERIFY(runner.startFunction(m_scene1->id()) == false);
    QVERIFY(runner.stopFunction(m_scene1->id()) == false);
    QVERIFY(runner.isFunctionRunning(m_scene1->id()) == false);
    QCOMPARE(runner.getFunctionAttribute(m_scene1->id(), 0), 0.0f);
    QVERIFY(runner.setFunctionAttribute(m_scene1->id(), 0, 0.5) == false);
    QVERIFY(runner.setFunctionAttribute(m_scene1->id(), QString("Intensity"), 0.5) == false);
    QVERIFY(runner.systemCommand("qlcplus_no_such_program_xyz") == false);
    QVERIFY(runner.waitFunctionStart(m_scene1->id()) == false);
    QVERIFY(runner.waitFunctionStop(m_scene1->id()) == false);
    QVERIFY(runner.setBlackout(true) == false);
    QVERIFY(runner.setBPM(120) == false);
    QCOMPARE(runner.random(10, 20), 0);
    QCOMPARE(runner.random(QString("10"), QString("20")), 0);
    QVERIFY(runner.m_functionQueue.isEmpty());
    QVERIFY(runner.m_fixtureValueQueue.isEmpty());

    // waitTime() still accounts the time (that's how totalDuration() works)
    QVERIFY(runner.waitTime(uint(1000)) == false);
    QCOMPARE(runner.currentWaitTime(), 1000);
    QVERIFY(runner.waitTime(QString("1s")) == false);
    QCOMPARE(runner.currentWaitTime(), 2000);

    // stopOnExit() is plain state, no running check
    QVERIFY(runner.stopOnExit(false));
    QVERIFY(runner.m_stopOnExit == false);

    // stop() on a runner that never ran is a no-op
    runner.stop();
    QVERIFY(runner.m_running == false);

    // Nothing queued and not running: write() reports completion
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);
    QVERIFY(runner.write(&timer, universes) == false);

    // The owner id is what identifies the script when it starts a Function
    ScriptRunner owned(m_doc, QString(), NULL, 77);
    QCOMPARE(owned.startFunctionSource().type(), FunctionParent::Function);
    QCOMPARE(owned.startFunctionSource().id(), quint32(77));
}

void ScriptV4_Test::runnerCollectScriptDataRunsAllEngineMethods()
{
    // collectScriptData() evaluates AND calls the program, with the runner not
    // marked as running: every Engine call takes its early-return path and
    // nothing must end up queued
    QString content = QString(
        "Engine.getChannelValue(0, 0);\n"
        "Engine.setFixture(%1, 0, 255);\n"
        "Engine.setFixture(%1, 0, 255, 1000);\n"
        "Engine.stopOnExit(true);\n"
        "Engine.startFunction(%2);\n"
        "Engine.stopFunction(%2);\n"
        "Engine.isFunctionRunning(%2);\n"
        "Engine.getFunctionAttribute(%2, 0);\n"
        "Engine.setFunctionAttribute(%2, 0, 0.5);\n"
        "Engine.setFunctionAttribute(%2, 'Intensity', 0.5);\n"
        "Engine.systemCommand('qlcplus_no_such_program_xyz');\n"
        "Engine.waitTime(100);\n"
        "Engine.waitTime('1s');\n"
        "Engine.waitFunctionStart(%2);\n"
        "Engine.waitFunctionStop(%2);\n"
        "Engine.setBlackout(true);\n"
        "Engine.setBPM(120);\n"
        "Engine.random(10, 20);\n"
        "Engine.random('10', '20');\n")
        .arg(m_fixture->id()).arg(m_scene1->id());

    ScriptRunner runner(m_doc, content);
    QVERIFY(runner.collectScriptData().isEmpty());
    QVERIFY(runner.m_functionQueue.isEmpty());
    QVERIFY(runner.m_fixtureValueQueue.isEmpty());
    QCOMPARE(runner.currentWaitTime(), 1100);
    QVERIFY(m_doc->inputOutputMap()->blackout() == false);

    // Errors are reported with their line number
    ScriptRunner broken(m_doc, "var a = 1;\nvar b = ;\n");
    QStringList errors = broken.collectScriptData();
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().contains("line 2"));

    ScriptRunner throwing(m_doc, "var a = 1;\nthrow new Error('boom');\n");
    errors = throwing.collectScriptData();
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().contains("boom"));
}

void ScriptV4_Test::runnerSetFixtureValidation()
{
    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;

    QVERIFY(runner.setFixture(Fixture::invalidId(), 0, 255) == false);
    QVERIFY(runner.setFixture(m_fixture->id(), 4, 255) == false);     // no such channel
    QVERIFY(runner.setFixture(m_edgeFixture->id(), 1, 255));          // address 511
    QVERIFY(runner.setFixture(m_edgeFixture->id(), 2, 255) == false); // address 512
    QVERIFY(runner.setFixture(m_fixture->id(), 3, 128, 2000));

    QCOMPARE(runner.m_fixtureValueQueue.size(), 2);
    FixtureValue first = runner.m_fixtureValueQueue.at(0);
    QCOMPARE(first.m_universe, quint32(0));
    QCOMPARE(first.m_fixtureID, m_edgeFixture->id());
    QCOMPARE(first.m_channel, quint32(1));
    QCOMPARE(first.m_value, uchar(255));
    QCOMPARE(first.m_fadeTime, uint(0));
    FixtureValue second = runner.m_fixtureValueQueue.at(1);
    QCOMPARE(second.m_fixtureID, m_fixture->id());
    QCOMPARE(second.m_channel, quint32(3));
    QCOMPARE(second.m_value, uchar(128));
    QCOMPARE(second.m_fadeTime, uint(2000));

    runner.m_running = false;
}

void ScriptV4_Test::runnerWriteAppliesFixtureValues()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;
    QVERIFY(runner.setFixture(m_fixture->id(), 2, 200));
    QVERIFY(runner.setFixture(m_fixture->id(), 3, 50, 1000));

    // Still running: write() keeps going and drains the value queue into a
    // fader requested from the target universe
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(runner.m_fixtureValueQueue.isEmpty());
    QCOMPARE(runner.m_fadersMap.size(), 1);
    QVERIFY(runner.m_fadersMap.contains(0));
    QSharedPointer<GenericFader> fader = runner.m_fadersMap.value(0);
    QVERIFY(fader.isNull() == false);

    QHash<quint32, FadeChannel> channels = fader->channels();
    QCOMPARE(channels.size(), 2);
    bool sawCh2 = false, sawCh3 = false;
    foreach (const FadeChannel &fc, channels)
    {
        if (fc.channel() == 2)
        {
            sawCh2 = true;
            QCOMPARE(fc.target(), quint32(200));
            QCOMPARE(fc.fadeTime(), uint(0));
        }
        else if (fc.channel() == 3)
        {
            sawCh3 = true;
            QCOMPARE(fc.target(), quint32(50));
            QCOMPARE(fc.fadeTime(), uint(1000));
        }
        QCOMPARE(fc.fixture(), m_fixture->id());
        QVERIFY(fc.isReady() == false);
    }
    QVERIFY(sawCh2);
    QVERIFY(sawCh3);

    // The same fader is reused for the same universe on the next write
    QVERIFY(runner.setFixture(m_fixture->id(), 0, 10));
    QVERIFY(runner.write(&timer, universes));
    QCOMPARE(runner.m_fadersMap.size(), 1);
    QCOMPARE(runner.m_fadersMap.value(0), fader);
    QCOMPARE(fader->channels().size(), 3);

    // One fader cycle later the instant value has reached the universe
    universes[0]->processFaders(MasterTimer::tick());
    QCOMPARE(universes[0]->preGMValue(2), uchar(200));

    runner.m_running = false;
}

void ScriptV4_Test::runnerFunctionQueueOperations()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    ScriptRunner runner(m_doc, QString(), NULL, 99);
    runner.m_running = true;

    // Unknown ids are refused up front
    QVERIFY(runner.startFunction(Function::invalidId()) == false);
    QVERIFY(runner.stopFunction(Function::invalidId()) == false);
    QVERIFY(runner.isFunctionRunning(Function::invalidId()) == false);
    QVERIFY(runner.m_functionQueue.isEmpty());

    // START: queued, then started on write() with the script as its parent
    QVERIFY(runner.startFunction(m_scene1->id()));
    QCOMPARE(runner.m_functionQueue.size(), 1);
    QVERIFY(runner.isFunctionRunning(m_scene1->id()) == false);
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(runner.m_functionQueue.isEmpty());
    QVERIFY(m_scene1->isRunning());
    QVERIFY(runner.isFunctionRunning(m_scene1->id()));
    QVERIFY(timer.m_functionList.contains(m_scene1));
    QCOMPARE(runner.m_startedFunctions, QList<quint32>() << m_scene1->id());
    QCOMPARE(m_scene1->m_sources.size(), 1);
    QCOMPARE(m_scene1->m_sources.first().type(), FunctionParent::Function);
    QCOMPARE(m_scene1->m_sources.first().id(), quint32(99));

    // STOP: force-stops regardless of who started it
    QVERIFY(runner.stopFunction(m_scene1->id()));
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(m_scene1->stopped());
    QVERIFY(runner.m_startedFunctions.isEmpty());

    // START_DONT_STOP: started, but not remembered for cleanup on exit
    QVERIFY(runner.stopOnExit(false));
    QVERIFY(runner.startFunction(m_scene2->id()));
    QCOMPARE(runner.m_functionQueue.head().second, ScriptRunner::START_DONT_STOP);
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(m_scene2->isRunning());
    QVERIFY(runner.m_startedFunctions.isEmpty());

    // Several operations queued at once are all dispatched in one write()
    QVERIFY(runner.stopOnExit(true));
    QVERIFY(runner.startFunction(m_scene1->id()));
    QVERIFY(runner.stopFunction(m_scene2->id()));
    QCOMPARE(runner.m_functionQueue.size(), 2);
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(runner.m_functionQueue.isEmpty());
    QVERIFY(m_scene2->stopped());
    QCOMPARE(runner.m_startedFunctions, QList<quint32>() << m_scene1->id());

    runner.m_startedFunctions.clear();
    runner.m_running = false;
}

void ScriptV4_Test::runnerWaitFunctionStart()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;

    // Waiting for a function that is already running passes straight through
    m_scene2->start(&timer, FunctionParent::master());
    QVERIFY(m_scene2->isRunning());
    QVERIFY(runner.waitFunctionStart(m_scene2->id()));
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(runner.m_functionQueue.isEmpty());
    QCOMPARE(runner.m_waitFunctionId, Function::invalidId());

    // Waiting for a function that is not running blocks the queue
    QVERIFY(runner.waitFunctionStart(m_scene1->id()));
    QVERIFY(runner.startFunction(m_scene2->id()));
    QCOMPARE(runner.m_functionQueue.size(), 2);
    QVERIFY(runner.write(&timer, universes));
    QCOMPARE(runner.m_waitFunctionId, m_scene1->id());
    QCOMPARE(runner.m_functionQueue.size(), 2);

    // ...and keeps blocking on later ticks, even when the JS side is done
    runner.m_running = false;
    QVERIFY(runner.write(&timer, universes));
    QCOMPARE(runner.m_functionQueue.size(), 2);

    // Some other function starting doesn't release the wait
    runner.slotWaitFunctionStarted(m_scene2->id());
    QCOMPARE(runner.m_waitFunctionId, m_scene1->id());

    // The MasterTimer reporting our function (once it really is running)
    // releases it...
    m_scene1->start(&timer, FunctionParent::master());
    QVERIFY(m_scene1->isRunning());
    emit m_doc->masterTimer()->functionStarted(m_scene1->id());
    QCOMPARE(runner.m_waitFunctionId, Function::invalidId());

    // ...and the rest of the queue is dispatched on the next tick; nothing
    // left to do afterwards, so write() reports completion
    QVERIFY(runner.write(&timer, universes) == false);
    QVERIFY(runner.m_functionQueue.isEmpty());
    QVERIFY(m_scene2->isRunning());
}

void ScriptV4_Test::runnerWaitFunctionStop()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;

    // A stopped function doesn't block
    QVERIFY(m_scene1->stopped());
    QVERIFY(runner.waitFunctionStop(m_scene1->id()));
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(runner.m_functionQueue.isEmpty());
    QCOMPARE(runner.m_waitFunctionId, Function::invalidId());

    // A running one does
    QVERIFY(runner.startFunction(m_scene2->id()));
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(m_scene2->isRunning());
    QVERIFY(m_scene2->stopped() == false);
    QCOMPARE(runner.m_startedFunctions, QList<quint32>() << m_scene2->id());

    QVERIFY(runner.waitFunctionStop(m_scene2->id()));
    QVERIFY(runner.write(&timer, universes));
    QCOMPARE(runner.m_waitFunctionId, m_scene2->id());
    QCOMPARE(runner.m_functionQueue.size(), 1);

    runner.slotWaitFunctionStopped(m_scene1->id());
    QCOMPARE(runner.m_waitFunctionId, m_scene2->id());

    m_scene2->stop(FunctionParent::master());
    emit m_doc->masterTimer()->functionStopped(m_scene2->id());
    QCOMPARE(runner.m_waitFunctionId, Function::invalidId());
    // A function that stopped no longer needs stopping on script exit
    QVERIFY(runner.m_startedFunctions.isEmpty());

    QVERIFY(runner.write(&timer, universes));
    QVERIFY(runner.m_functionQueue.isEmpty());

    runner.m_running = false;
}

void ScriptV4_Test::runnerWriteDropsDeletedFunction()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    Scene *doomed = new Scene(m_doc);
    doomed->setName("Doomed");
    QVERIFY(m_doc->addFunction(doomed));
    quint32 doomedId = doomed->id();

    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;
    QVERIFY(runner.startFunction(doomedId));
    QVERIFY(runner.startFunction(m_scene1->id()));
    QCOMPARE(runner.m_functionQueue.size(), 2);

    // The function disappears between being queued and the next tick: the
    // stale entry must be dropped and the remaining queue still dispatched
    QVERIFY(m_doc->deleteFunction(doomedId));
    QVERIFY(m_doc->function(doomedId) == NULL);

    QVERIFY(runner.write(&timer, universes));
    QVERIFY(runner.m_functionQueue.isEmpty());
    QVERIFY(m_scene1->isRunning());
    QCOMPARE(runner.m_startedFunctions, QList<quint32>() << m_scene1->id());

    runner.m_startedFunctions.clear();
    runner.m_running = false;
}

void ScriptV4_Test::runnerAttributesBlackoutBpm()
{
    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;

    // Attributes by index and by name
    QCOMPARE(runner.getFunctionAttribute(m_scene1->id(), Function::Intensity), 1.0f);
    QVERIFY(runner.setFunctionAttribute(m_scene1->id(), Function::Intensity, 0.5));
    QCOMPARE(runner.getFunctionAttribute(m_scene1->id(), Function::Intensity), 0.5f);
    QVERIFY(runner.setFunctionAttribute(m_scene1->id(), QString("Intensity"), 0.25));
    QCOMPARE(runner.getFunctionAttribute(m_scene1->id(), Function::Intensity), 0.25f);
    QCOMPARE(float(m_scene1->getAttributeValue(Function::Intensity)), 0.25f);

    // An unknown attribute name is accepted by the command but changes nothing
    QVERIFY(runner.setFunctionAttribute(m_scene1->id(), QString("NoSuchAttribute"), 0.9));
    QCOMPARE(runner.getFunctionAttribute(m_scene1->id(), Function::Intensity), 0.25f);

    // Unknown function ids
    QCOMPARE(runner.getFunctionAttribute(Function::invalidId(), 0), 0.0f);
    QVERIFY(runner.setFunctionAttribute(Function::invalidId(), 0, 0.5) == false);
    QVERIFY(runner.setFunctionAttribute(Function::invalidId(), QString("Intensity"), 0.5) == false);

    // Blackout is applied to the I/O map straight away
    QVERIFY(m_doc->inputOutputMap()->blackout() == false);
    QVERIFY(runner.setBlackout(true));
    QVERIFY(m_doc->inputOutputMap()->blackout());
    QVERIFY(runner.setBlackout(false));
    QVERIFY(m_doc->inputOutputMap()->blackout() == false);

    // BPM only takes effect with the internal beat generator enabled
    m_doc->inputOutputMap()->setBeatGeneratorType(InputOutputMap::Internal);
    QVERIFY(runner.setBPM(128));
    QCOMPARE(m_doc->inputOutputMap()->bpmNumber(), 128);

    runner.m_running = false;
}

void ScriptV4_Test::runnerRandomAndChannelValue()
{
    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;

    for (int i = 0; i < 50; i++)
    {
        int value = runner.random(10, 20);
        QVERIFY(value >= 10 && value <= 20);
        value = runner.random(QString("1s"), QString("1.5"));
        QVERIFY(value >= 1000 && value <= 1500);
    }
    QCOMPARE(runner.random(7, 7), 7);

    // Channel values are read from the universe's pre-grandmaster buffer
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    QVERIFY(universes.size() >= 1);
    QVERIFY(universes[0]->write(3, 77));
    QCOMPARE(runner.getChannelValue(0, 3), 77);
    QCOMPARE(runner.getChannelValue(0, 4), 0);
    QCOMPARE(runner.getChannelValue(-1, 3), 0);
    QCOMPARE(runner.getChannelValue(universes.size(), 3), 0);

    runner.m_running = false;
}

void ScriptV4_Test::runnerSystemCommandTokenizer()
{
    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;

    // A program that doesn't exist: the tokenizer still runs over every kind
    // of argument (single-token quoted, multi-token quoted, bare) and the
    // detached start simply fails, with no side effect on the machine
    QVERIFY(runner.systemCommand("qlcplus_no_such_program_xyz 'one' 'two words here' bare 'three' trailing"));
    QVERIFY(runner.systemCommand("qlcplus_no_such_program_xyz"));
    QVERIFY(runner.systemCommand(""));

    runner.m_running = false;
}

void ScriptV4_Test::runnerStopReleasesFunctionsAndFaders()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    ScriptRunner runner(m_doc, QString());
    runner.m_running = true;
    QVERIFY(runner.startFunction(m_scene1->id()));
    QVERIFY(runner.startFunction(m_scene2->id()));
    QVERIFY(runner.setFixture(m_fixture->id(), 0, 255));
    QVERIFY(runner.write(&timer, universes));
    QVERIFY(m_scene1->isRunning());
    QVERIFY(m_scene2->isRunning());
    QCOMPARE(runner.m_startedFunctions.size(), 2);
    QSharedPointer<GenericFader> fader = runner.m_fadersMap.value(0);
    QVERIFY(fader.isNull() == false);

    // Simulate one of them having been deleted meanwhile: stop() skips it
    quint32 scene2Id = m_scene2->id();
    QVERIFY(m_doc->deleteFunction(scene2Id));
    m_scene2 = NULL;

    runner.stop();
    QVERIFY(runner.m_running == false);
    QVERIFY(runner.m_startedFunctions.isEmpty());
    QVERIFY(m_scene1->stopped());
    QVERIFY(runner.m_fadersMap.isEmpty());
    QVERIFY(fader->deleteRequested());

    // Stopping twice is harmless
    runner.stop();
    QVERIFY(runner.m_running == false);
}

/****************************************************************************
 * ScriptRunner / Script, threaded JS execution
 ****************************************************************************/

void ScriptV4_Test::runnerThreadedStopWhileWaiting()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    // The script parks itself in a (very) long wait after queueing work
    QString content = QString(
        "Engine.startFunction(%1);\n"
        "Engine.setFixture(%2, 0, 200);\n"
        "Engine.waitTime(600000);\n"
        "Engine.setFixture(%2, 1, 100);\n")
        .arg(m_scene1->id()).arg(m_fixture->id());

    // Heap allocated on purpose: a QThread must never be destroyed while its
    // thread is still running, so on any failure below it is leaked, not deleted
    ScriptRunner *runner = new ScriptRunner(m_doc, content, NULL, 55);
    runner->execute();
    QVERIFY(runner->m_running);
    // A second execute() while running is ignored
    runner->execute();
    QVERIFY(runner->isRunning());

    // Wait for the JS thread to reach waitTime()
    QTRY_VERIFY_WITH_TIMEOUT(runner->m_waitCount > 0, 10000);
    QCOMPARE(runner->currentWaitTime(), 600000);
    QVERIFY(runner->m_engine != NULL);

    // One tick dispatches what was queued before the wait
    QVERIFY(runner->write(&timer, universes));
    QVERIFY(m_scene1->isRunning());
    QCOMPARE(m_scene1->m_sources.first().id(), quint32(55));
    QCOMPARE(runner->m_startedFunctions, QList<quint32>() << m_scene1->id());
    QSharedPointer<GenericFader> fader = runner->m_fadersMap.value(0);
    QVERIFY(fader.isNull() == false);
    QCOMPARE(fader->channels().size(), 1);

    // stop() interrupts the engine, breaks the wait loop and cleans up
    runner->stop();
    QVERIFY(runner->m_running == false);
    QVERIFY(runner->m_engine == NULL);
    QVERIFY(m_scene1->stopped());
    QVERIFY(runner->m_startedFunctions.isEmpty());
    QVERIFY(runner->m_fadersMap.isEmpty());
    QVERIFY(fader->deleteRequested());

    bool finished = runner->wait(10000);
    QVERIFY2(finished, "JS thread did not finish after stop()");
    if (finished == false)
        return;

    // The setFixture() after the wait never ran
    QVERIFY(runner->m_fixtureValueQueue.isEmpty());
    QVERIFY(runner->write(&timer, universes) == false);
    delete runner;
}

void ScriptV4_Test::runnerThreadedRunsToCompletion()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    // Short waits in both flavours, then work queued right before the end
    QString content = QString(
        "Engine.waitTime('0.1');\n"
        "Engine.waitTime(40);\n"
        "Engine.setFixture(%2, 1, 90);\n"
        "Engine.startFunction(%1);\n")
        .arg(m_scene1->id()).arg(m_fixture->id());

    ScriptRunner *runner = new ScriptRunner(m_doc, content, NULL, 55);
    runner->execute();

    // Drive ticks exactly like MasterTimer does, until write() reports that
    // the JS thread fell off the end of the program AND everything it queued
    // right before that has been dispatched; each write() consumes one tick
    // of the pending wait
    int ticks = 0;
    bool done = false;
    while (done == false && ticks < 1000)
    {
        done = (runner->write(&timer, universes) == false);
        if (done == false)
            QTest::qWait(5);
        ticks++;
    }
    if (done == false)
    {
        runner->stop();
        runner->wait(10000);
        QFAIL("script did not run to completion");
    }
    QVERIFY2(runner->wait(10000), "JS thread did not finish");
    QVERIFY(ticks >= 140 / int(MasterTimer::tick()));
    QVERIFY(runner->m_running == false);
    QCOMPARE(runner->currentWaitTime(), 0);
    QVERIFY(runner->m_functionQueue.isEmpty());

    // The work queued just before the end was flushed before completion
    // was reported
    QVERIFY(m_scene1->isRunning());
    QCOMPARE(runner->m_startedFunctions, QList<quint32>() << m_scene1->id());
    QVERIFY(runner->m_fixtureValueQueue.isEmpty());
    QCOMPARE(runner->m_fadersMap.value(0)->channels().size(), 1);

    m_scene1->stop(FunctionParent::master());
    delete runner;
}

void ScriptV4_Test::scriptRunLifecycle()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    Script *scr = new Script(m_doc);
    scr->setName("Runner");
    QVERIFY(m_doc->addFunction(scr));
    scr->setData(QString(
        "Engine.startFunction(%1);\n"
        "Engine.waitTime(40);\n"
        "Engine.setFixture(%2, 2, 123);\n")
        .arg(m_scene1->id()).arg(m_fixture->id()));

    // write() before preRun(): no runner yet, nothing happens
    scr->write(&timer, universes);
    QVERIFY(scr->m_runner == NULL);

    // Start through the timer stub, like MasterTimer would
    scr->start(&timer, FunctionParent::master());
    QVERIFY(scr->isRunning());
    QVERIFY(scr->m_runner != NULL);
    QVERIFY(timer.m_functionList.contains(scr));

    int ticks = 0;
    while (scr->stopped() == false && ticks < 1000)
    {
        scr->write(&timer, universes);
        QTest::qWait(5);
        ticks++;
    }
    ScriptRunner *runner = scr->m_runner;
    if (scr->stopped() == false)
    {
        runner->stop();
        runner->wait(10000);
        QFAIL("script never signalled completion");
    }
    QVERIFY(ticks >= 2);
    QVERIFY(scr->elapsed() > 0);

    // The started Scene names the Script as its parent...
    QVERIFY(m_scene1->isRunning());
    QCOMPARE(m_scene1->m_sources.size(), 1);
    QCOMPARE(m_scene1->m_sources.first().type(), FunctionParent::Function);
    QCOMPARE(m_scene1->m_sources.first().id(), scr->id());
    // ...and the fixture value was handed to a fader on the universe
    QCOMPARE(runner->m_fadersMap.value(0)->channels().size(), 1);

    // postRun() tears the runner down
    scr->postRun(&timer, universes);
    QVERIFY(scr->m_runner == NULL);
    QVERIFY(scr->isRunning() == false);
    QVERIFY(runner->isFinished());
    // Let the deferred delete of the runner go through while the Doc is alive
    QTest::qWait(10);

    m_scene1->stop(FunctionParent::master());
}

void ScriptV4_Test::scriptRunPausedAndSelfStops()
{
    QList<Universe*> universes = m_doc->inputOutputMap()->universes();
    MasterTimerStub timer(m_doc, universes);

    Script *scr = new Script(m_doc);
    QVERIFY(m_doc->addFunction(scr));
    // No waits: the JS thread is done almost immediately
    scr->setData(QString("Engine.setFixture(%1, 0, 10);\n").arg(m_fixture->id()));

    scr->start(&timer, FunctionParent::master());
    QVERIFY(scr->m_runner != NULL);
    ScriptRunner *runner = scr->m_runner;
    QVERIFY2(runner->wait(10000), "JS thread did not finish");

    // A paused script doesn't tick its runner at all
    scr->setPause(true);
    QVERIFY(scr->isPaused());
    scr->write(&timer, universes);
    QCOMPARE(scr->elapsed(), quint32(0));
    QVERIFY(runner->m_fixtureValueQueue.size() == 1);
    QVERIFY(scr->stopped() == false);

    // Resumed: the runner flushes, reports completion, and the Script stops
    // itself the way MasterTimer expects
    scr->setPause(false);
    scr->write(&timer, universes);
    QVERIFY(scr->elapsed() > 0);
    QVERIFY(runner->m_fixtureValueQueue.isEmpty());
    QVERIFY(scr->stopped());

    scr->postRun(&timer, universes);
    QVERIFY(scr->m_runner == NULL);
    QTest::qWait(10);
}

QTEST_MAIN(ScriptV4_Test)
