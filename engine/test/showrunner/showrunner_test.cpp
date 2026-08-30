/*
  Q Light Controller Plus - Test Unit
  showrunner_test.cpp

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
#define private public
#include "showrunner.h"
#include "mastertimer.h"
#undef private
#include "show.h"
#include "track.h"
#include "scene.h"
#include "doc.h"
#include "inputoutputmap.h"
#include "showrunner_test.h"

void ShowRunner_Test::initTestCase()
{
    m_doc = new Doc(this);
    m_show = new Show(m_doc);
    m_doc->addFunction(m_show);
    m_scene = new Scene(m_doc);
    m_doc->addFunction(m_scene);
    m_track = new Track(m_scene->id());
    ShowFunction *sf = new ShowFunction(m_show->getLatestShowFunctionId());
    sf->setFunctionID(m_scene->id());
    sf->setStartTime(0);
    sf->setDuration(1000);
    m_track->addShowFunction(sf);
    m_show->addTrack(m_track);
}

void ShowRunner_Test::cleanupTestCase()
{
    delete m_doc;
}

void ShowRunner_Test::initRunner()
{
    ShowRunner runner(m_doc, m_show->id());
    QCOMPARE(runner.m_timeFunctions.count(), 1);
    QCOMPARE(runner.m_totalRunTime, quint32(1000));
}

void ShowRunner_Test::intensity()
{
    ShowRunner runner(m_doc, m_show->id());
    runner.adjustIntensity(0.5, m_track);
    QCOMPARE(runner.m_intensityMap[m_track->id()], 0.5);
}

void ShowRunner_Test::stopRunner()
{
    ShowRunner runner(m_doc, m_show->id());
    runner.m_elapsedTime = 500;
    runner.m_runningQueue.append(QPair<Function*,quint32>(m_scene,1000));
    runner.stop();
    QCOMPARE(runner.m_elapsedTime, quint32(0));
    QCOMPARE(runner.m_runningQueue.count(), 0);
}

void ShowRunner_Test::beatTempoUsesRealMilliseconds()
{
    // Regression test for the ShowRunner beat-path unit bug: ShowFunction::startTime()/
    // duration() are always real milliseconds (ADR 0001), but ShowRunner::write() used
    // to track beat-tempo functions' elapsed time in an old "beat count x 1000"
    // pseudo-unit that no longer matches those stored values. Uses its own local Doc/
    // Show/Track/Scene, independent of the class fixture above, since it needs a
    // Beats-tempo Show/function and a known BPM.
    Doc localDoc(this);
    Show *show = new Show(&localDoc);
    localDoc.addFunction(show);
    show->setTempoType(Function::Beats);

    Scene *scene = new Scene(&localDoc);
    localDoc.addFunction(scene);
    scene->setTempoType(Function::Beats);

    Track *track = new Track(scene->id());
    ShowFunction *sf = new ShowFunction(show->getLatestShowFunctionId());
    sf->setFunctionID(scene->id());
    sf->setStartTime(3000);
    sf->setDuration(2000);
    track->addShowFunction(sf);
    show->addTrack(track);

    // Give the Doc a known, non-zero BPM. InputOutputMap::bpmNumber() returns 0
    // (Disabled) until a beat generator type is set; MasterTimer defaults to 120 BPM,
    // so 60000/120 = 500ms per beat exactly - no rounding to account for below.
    localDoc.inputOutputMap()->setBeatGeneratorType(InputOutputMap::Internal);
    QCOMPARE(localDoc.inputOutputMap()->bpmNumber(), 120);

    ShowRunner runner(&localDoc, show->id());
    QCOMPARE(runner.m_beatFunctions.count(), 1);

    MasterTimer *timer = localDoc.masterTimer();

    // Simulate one detected beat pulse being processed by the runner. MasterTimer
    // normally clears its own "beat requested" flag once per real timer tick after
    // every listener has seen it; since no timer thread is running here, clear it
    // manually so each call represents exactly one beat.
    auto pulseBeat = [&]() {
        timer->requestBeat();
        runner.write(timer);
        timer->m_beatRequested = false;
    };

    // First beat only establishes beat sync; m_elapsedBeats must not advance yet.
    pulseBeat();
    QCOMPARE(runner.m_elapsedBeats, quint32(0));

    // 5 more beats = 2500ms elapsed, still short of the 3000ms startTime.
    for (int i = 0; i < 5; i++)
        pulseBeat();
    QCOMPARE(runner.m_elapsedBeats, quint32(2500));
    QCOMPARE(runner.m_runningQueue.count(), 0);

    // 6th beat reaches 3000ms == startTime: the function must start now, not at
    // beat-pseudo-count 3000 (which under the old unit would be beat #3, i.e. 1500ms
    // early) nor fail to start at all.
    pulseBeat();
    QCOMPARE(runner.m_elapsedBeats, quint32(3000));
    QCOMPARE(runner.m_runningQueue.count(), 1);

    // 4 more beats reach startTime + duration = 5000ms: the function must stop now.
    for (int i = 0; i < 4; i++)
        pulseBeat();
    QCOMPARE(runner.m_elapsedBeats, quint32(5000));
    QCOMPARE(runner.m_runningQueue.count(), 0);
}

QTEST_APPLESS_MAIN(ShowRunner_Test)
