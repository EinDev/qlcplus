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

#include <QtTest>

#define private public
#include "scriptv4_test.h"
#include "scriptv4.h"
#include "doc.h"
#undef private

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
}

void ScriptV4_Test::convertLineMissingColonIsSyntaxError()
{
    bool ok = true;
    Script::convertLine("nocolonhere\n", &ok);
    QVERIFY(ok == false);
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
}

void ScriptV4_Test::functionAndFixtureListParseConvertedSyntax()
{
    Doc doc(this);
    Script scr(&doc);

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

QTEST_APPLESS_MAIN(ScriptV4_Test)
