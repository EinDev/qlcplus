/*
  Q Light Controller Plus - Unit test
  scriptv4_test.h

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

#ifndef SCRIPTV4_TEST_H
#define SCRIPTV4_TEST_H

#include <QObject>

class Doc;
class Fixture;
class Scene;

class ScriptV4_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    /* Static conversion helpers */
    void convertLegacyMethodMapsKnownKeywords();

    void convertLineWaitPlainNumber();
    void convertLineWaitWithTimeUnitIsQuoted();
    void convertLineBlackoutOnOff();
    void convertLineQuotedValueConvertsToSingleQuotes();
    void convertLineRandomValueConvertsToEngineRandomCall();
    void convertLineMissingColonIsSyntaxError();
    void convertLineCommentsUrlsAndUnbalancedQuotes();
    void convertLineSystemCommandAndUnknownKeyword();

    void getValueFromStringPlainAndRandomRange();

    void functionAndFixtureListParseConvertedSyntax();
    void functionAndFixtureListSkipMalformedLines();

    /* Script data / copying / XML */
    void initialAndIcon();
    void setDataAndDataLines();
    void copyFromAndCreateCopy();
    void loadXMLRejectsWrongNodes();
    void loadXMLLegacyVersionConvertsCommands();
    void loadXMLVersion2KeepsCommandsVerbatim();
    void saveXMLRoundTrip();
    void syntaxErrorsLines();
    void totalDuration();

    /* ScriptRunner, driven directly (no thread) */
    void runnerInactiveMethodsRefuse();
    void runnerCollectScriptDataRunsAllEngineMethods();
    void runnerSetFixtureValidation();
    void runnerWriteAppliesFixtureValues();
    void runnerFunctionQueueOperations();
    void runnerWaitFunctionStart();
    void runnerWaitFunctionStop();
    void runnerWriteDropsDeletedFunction();
    void runnerAttributesBlackoutBpm();
    void runnerRandomAndChannelValue();
    void runnerSystemCommandTokenizer();
    void runnerStopReleasesFunctionsAndFaders();

    /* ScriptRunner / Script, threaded JS execution */
    void runnerThreadedStopWhileWaiting();
    void runnerThreadedRunsToCompletion();
    void scriptRunLifecycle();
    void scriptRunPausedAndSelfStops();

private:
    Doc *m_doc;
    Fixture *m_fixture;
    Fixture *m_edgeFixture;
    Scene *m_scene1;
    Scene *m_scene2;
};

#endif // SCRIPTV4_TEST_H
