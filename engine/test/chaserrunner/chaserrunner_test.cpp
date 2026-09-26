/*
  Q Light Controller - Unit test
  chaserrunner_test.cpp

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
#include <QMap>

#define private public
#define protected public
#include "chaserrunner_test.h"
#include "mastertimer_stub.h"
#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "chaserrunner.h"
#include "genericfader.h"
#include "fadechannel.h"
#include "chaserstep.h"
#include "collection.h"
#include "grandmaster.h"
#include "sequence.h"
#include "universe.h"
#include "qlcfile.h"
#include "fixture.h"
#include "chaser.h"
#include "scene.h"
#include "doc.h"
#undef protected
#undef private

#include "../common/resource_paths.h"

void ChaserRunner_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    m_doc->fixtureDefCache()->loadMap(dir);
}

void ChaserRunner_Test::cleanupTestCase()
{
    delete m_doc;
}

void ChaserRunner_Test::init()
{
    QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Futurelight", "DJScan250");
    QVERIFY(def != NULL);
    QLCFixtureMode* mode = def->mode("Mode 1");
    QVERIFY(mode != NULL);

    Fixture* fxi = new Fixture(m_doc);
    QVERIFY(fxi != NULL);
    fxi->setFixtureDefinition(def, mode);
    fxi->setName("Test Fixture");
    fxi->setAddress(0);
    fxi->setUniverse(0);
    m_doc->addFixture(fxi);

    m_scene1 = new Scene(m_doc);
    m_scene1->setName("S1");
    QVERIFY(m_scene1 != NULL);
    for (quint32 i = 0; i < fxi->channels(); i++)
        m_scene1->setValue(fxi->id(), i, 255 - i);
    m_doc->addFunction(m_scene1);

    m_scene2 = new Scene(m_doc);
    m_scene2->setName("S2");
    QVERIFY(m_scene2 != NULL);
    for (quint32 i = 0; i < fxi->channels(); i++)
        m_scene2->setValue(fxi->id(), i, 127 - i);
    m_doc->addFunction(m_scene2);

    m_scene3 = new Scene(m_doc);
    m_scene3->setName("S3");
    QVERIFY(m_scene3 != NULL);
    for (quint32 i = 0; i < fxi->channels(); i++)
        m_scene3->setValue(fxi->id(), i, 0 + i);
    m_doc->addFunction(m_scene3);

    m_chaser = new Chaser(m_doc);
    m_chaser->addStep(ChaserStep(m_scene1->id()));
    m_chaser->addStep(ChaserStep(m_scene2->id()));
    m_chaser->addStep(ChaserStep(m_scene3->id()));
}

void ChaserRunner_Test::cleanup()
{
    // Functions a test left running on the Doc's master timer would be
    // deleted by clearContents() but stay in the timer's list, crashing the
    // next test that ticks it: stop them and let the timer drop them first.
    foreach (Function *function, m_doc->functions())
        function->stop(FunctionParent::master());
    m_doc->masterTimer()->timerTick();

    m_doc->clearContents();
}

void ChaserRunner_Test::initial()
{
    ChaserRunner cr(m_doc, m_chaser);
    QCOMPARE(cr.m_doc, m_doc);
    QCOMPARE(cr.m_chaser, m_chaser);

    QCOMPARE(cr.m_updateOverrideSpeeds, false);
    QCOMPARE(cr.m_direction, Function::Forward);
    QCOMPARE(cr.m_startOffset, quint32(0));
    QCOMPARE(cr.m_pendingAction.m_action, ChaserNoAction);
    QCOMPARE(cr.m_pendingAction.m_masterIntensity, 1.0);
    QCOMPARE(cr.m_pendingAction.m_stepIndex, -1);
    QCOMPARE(cr.m_pendingAction.m_fadeMode, (int)Chaser::FromFunction);
    QCOMPARE(cr.m_lastRunStepIdx, -1);
}

void ChaserRunner_Test::nextPrevious()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::SingleShot);

    ChaserRunner cr(m_doc, m_chaser);

    QCOMPARE(cr.m_pendingAction.m_action, ChaserNoAction);

    ChaserAction action;
    action.m_action = ChaserNextStep;

    cr.setAction(action);
    QCOMPARE(cr.m_pendingAction.m_action, ChaserNextStep);

    cr.setAction(action);
    QCOMPARE(cr.m_pendingAction.m_action, ChaserNextStep);

    action.m_action = ChaserPreviousStep;
    cr.setAction(action);
    QCOMPARE(cr.m_pendingAction.m_action, ChaserPreviousStep);

    cr.setAction(action);
    QCOMPARE(cr.m_pendingAction.m_action, ChaserPreviousStep);
}

void ChaserRunner_Test::currentFadeIn()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    m_chaser->setFadeInSpeed(100);
    m_chaser->replaceStep(ChaserStep(m_scene1->id(), 1000, 2000, 3000), 0);
    m_chaser->replaceStep(ChaserStep(m_scene2->id(), 1100, 2100, 3100), 1);
    m_chaser->replaceStep(ChaserStep(m_scene3->id(), 1200, 2200, 3200), 2);

    ChaserRunner cr(m_doc, m_chaser);

    m_chaser->setFadeInMode(Chaser::Default);
    QCOMPARE(cr.currentStepIndex(), -1);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), Function::defaultSpeed());
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), Function::defaultSpeed());
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), Function::defaultSpeed());

    m_chaser->setFadeInMode(Chaser::Common);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(100));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(100));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(100));

    m_chaser->setFadeInMode(Chaser::PerStep);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1000));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1100));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1200));
    cr.m_lastRunStepIdx = 3; // Nonexistent step
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), Function::defaultSpeed());

    // Check that override speed really overrides any setting
    m_chaser->setOverrideFadeInSpeed(1234);

    m_chaser->setFadeInMode(Chaser::Default);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));

    m_chaser->setFadeInMode(Chaser::Common);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));

    m_chaser->setFadeInMode(Chaser::PerStep);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 3; // Nonexistent step
    QCOMPARE(cr.stepFadeIn(cr.currentStepIndex()), uint(1234));
}

void ChaserRunner_Test::currentFadeOut()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    m_chaser->setFadeOutSpeed(200);
    m_chaser->replaceStep(ChaserStep(m_scene1->id(), 1000, 2000, 3000), 0);
    m_chaser->replaceStep(ChaserStep(m_scene2->id(), 1100, 2100, 3100), 1);
    m_chaser->replaceStep(ChaserStep(m_scene3->id(), 1200, 2200, 3200), 2);

    ChaserRunner cr(m_doc, m_chaser);

    m_chaser->setFadeOutMode(Chaser::Default);
    QCOMPARE(cr.currentStepIndex(), -1);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), Function::defaultSpeed());
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), Function::defaultSpeed());
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), Function::defaultSpeed());

    m_chaser->setFadeOutMode(Chaser::Common);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(200));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(200));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(200));

    m_chaser->setFadeOutMode(Chaser::PerStep);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(3000));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(3100));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(3200));
    cr.m_lastRunStepIdx = 3; // Nonexistent step
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), Function::defaultSpeed());

    // Check that override speed really overrides any setting
    m_chaser->setOverrideFadeOutSpeed(1234);

    m_chaser->setFadeOutMode(Chaser::Default);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));

    m_chaser->setFadeOutMode(Chaser::Common);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));

    m_chaser->setFadeOutMode(Chaser::PerStep);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 3; // Nonexistent step
    QCOMPARE(cr.stepFadeOut(cr.currentStepIndex()), uint(1234));
}

void ChaserRunner_Test::currentDuration()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    m_chaser->setDuration(300);
    m_chaser->replaceStep(ChaserStep(m_scene1->id(), 1000, 2000, 3000), 0);
    m_chaser->replaceStep(ChaserStep(m_scene2->id(), 1100, 2100, 3100), 1);
    m_chaser->replaceStep(ChaserStep(m_scene3->id(), 1200, 2200, 3200), 2);

    ChaserRunner cr(m_doc, m_chaser);

    // Default mode for duration is interpreted as Common
    m_chaser->setDurationMode(Chaser::Default);
    QCOMPARE(cr.currentStepIndex(), -1);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(300));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(300));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(300));

    m_chaser->setDurationMode(Chaser::Common);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(300));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(300));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(300));

    m_chaser->setDurationMode(Chaser::PerStep);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(3000));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(3200));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(3400));
    cr.m_lastRunStepIdx = 3; // Nonexistent step
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(300)); // Fall back to common speed

    // Check that override speed really overrides any setting
    m_chaser->setOverrideDuration(1234);

    m_chaser->setDurationMode(Chaser::Default);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));

    m_chaser->setDurationMode(Chaser::Common);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));

    m_chaser->setDurationMode(Chaser::PerStep);
    cr.m_lastRunStepIdx = 0;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 1;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 2;
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));
    cr.m_lastRunStepIdx = 3; // Nonexistent step
    QCOMPARE(cr.stepDuration(cr.currentStepIndex()), uint(1234));
}

/*
void ChaserRunner_Test::roundCheckSingleShotForward()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::SingleShot);
    m_chaser->setDuration(Function::infiniteSpeed());
    ChaserRunner cr(m_doc, m_chaser);

    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 3);
    QVERIFY(cr.roundCheck() == false);
    cr.m_currentStep = 4; // Over list.size
    QVERIFY(cr.roundCheck() == false);
    cr.m_currentStep = -1; // Under list.size
    QVERIFY(cr.roundCheck() == false);

    cr.reset();
    QCOMPARE(cr.currentStep(), 0);
}

void ChaserRunner_Test::roundCheckSingleShotBackward()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::SingleShot);
    m_chaser->setDuration(Function::infiniteSpeed());
    ChaserRunner cr(m_doc, m_chaser);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 3); // Over list.size
    QVERIFY(cr.roundCheck() == false);
    cr.m_currentStep = -1; // Under list.size
    QVERIFY(cr.roundCheck() == false);

    cr.reset();
    QCOMPARE(cr.currentStep(), 2);
}

void ChaserRunner_Test::roundCheckLoopForward()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());
    ChaserRunner cr(m_doc, m_chaser);

    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);

    QCOMPARE(cr.currentStep(), 1);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);

    // Loops around back to index 0
    QCOMPARE(cr.currentStep(), 3);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 0);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);

    // Loops around to index 2
    cr.m_currentStep = -1;
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);

    cr.reset();
    QCOMPARE(cr.currentStep(), 0);
}

void ChaserRunner_Test::roundCheckLoopBackward()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());
    ChaserRunner cr(m_doc, m_chaser);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);

    QCOMPARE(cr.currentStep(), 1);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);

    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 0);

    // Loops around back to index 2
    cr.m_currentStep = -1;
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);

    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 0);

    // Loops around to index 0
    QCOMPARE(cr.currentStep(), 3);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 0);

    cr.reset();
    QCOMPARE(cr.currentStep(), 2);
}

void ChaserRunner_Test::roundCheckPingPongForward()
{
    m_chaser->addStep(m_scene1->id()); // Easier to check direction changes with 4 steps
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::PingPong);
    m_chaser->setDuration(Function::infiniteSpeed());
    ChaserRunner cr(m_doc, m_chaser);

    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.m_direction, Function::Forward);

    QCOMPARE(cr.currentStep(), 1);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QCOMPARE(cr.m_direction, Function::Forward);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QCOMPARE(cr.m_direction, Function::Forward);

    QCOMPARE(cr.currentStep(), 3);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 3);
    QCOMPARE(cr.m_direction, Function::Forward);

    cr.m_currentStep = 4;
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QCOMPARE(cr.m_direction, Function::Backward);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QCOMPARE(cr.m_direction, Function::Backward);

    QCOMPARE(cr.currentStep(), 1);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QCOMPARE(cr.m_direction, Function::Backward);

    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 0);
    QCOMPARE(cr.m_direction, Function::Backward);

    cr.m_currentStep = -1;
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QCOMPARE(cr.m_direction, Function::Forward);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QCOMPARE(cr.m_direction, Function::Forward);

    QCOMPARE(cr.currentStep(), 3);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 3);
    QCOMPARE(cr.m_direction, Function::Forward);

    cr.m_currentStep = 4;
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QCOMPARE(cr.m_direction, Function::Backward);

    cr.reset();
    QCOMPARE(cr.currentStep(), 0);
    QCOMPARE(cr.m_direction, Function::Forward);
}

void ChaserRunner_Test::roundCheckPingPongBackward()
{
    m_chaser->addStep(m_scene1->id()); // Easier to check direction changes with 4 steps
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::PingPong);
    m_chaser->setDuration(Function::infiniteSpeed());
    ChaserRunner cr(m_doc, m_chaser);

    QCOMPARE(cr.currentStep(), 3);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.m_direction, Function::Backward);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QCOMPARE(cr.m_direction, Function::Backward);

    QCOMPARE(cr.currentStep(), 1);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QCOMPARE(cr.m_direction, Function::Backward);

    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 0);
    QCOMPARE(cr.m_direction, Function::Backward);

    cr.m_currentStep = -1;
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QCOMPARE(cr.m_direction, Function::Forward);

    QCOMPARE(cr.currentStep(), 2);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QCOMPARE(cr.m_direction, Function::Forward);

    QCOMPARE(cr.currentStep(), 3);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 3);
    QCOMPARE(cr.m_direction, Function::Forward);

    cr.m_currentStep = 4;
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 2);
    QCOMPARE(cr.m_direction, Function::Backward);

    QCOMPARE(cr.currentStep(), 1);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QCOMPARE(cr.m_direction, Function::Backward);

    QCOMPARE(cr.currentStep(), 0);
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 0);
    QCOMPARE(cr.m_direction, Function::Backward);

    cr.m_currentStep = -1;
    QVERIFY(cr.roundCheck() == true);
    QCOMPARE(cr.currentStep(), 1);
    QCOMPARE(cr.m_direction, Function::Forward);

    cr.reset();
    QCOMPARE(cr.currentStep(), 3);
    QCOMPARE(cr.m_direction, Function::Backward);
}
*/

void ChaserRunner_Test::writeNoSteps()
{
    Chaser chaser(m_doc);
    ChaserRunner cr(m_doc, &chaser);

    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));
    MasterTimerStub timer(m_doc, ua);

    QVERIFY(cr.write(&timer, ua) == false);
}

void ChaserRunner_Test::writeForwardLoopZero()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);
}

void ChaserRunner_Test::writeBackwardLoopZero()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::Loop);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);
}

void ChaserRunner_Test::writeForwardSingleShotZero()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::SingleShot);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == false);
    cr.postRun(&timer, QList<Universe*>());
    timer.timerTick();
    QVERIFY(m_scene3->stopped() == true);
    QCOMPARE(timer.m_functionList.size(), 0);
}

void ChaserRunner_Test::writeBackwardSingleShotZero()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::SingleShot);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == false);
    cr.postRun(&timer, QList<Universe*>());
    timer.timerTick();
    QVERIFY(m_scene1->stopped() == true);
    QCOMPARE(timer.m_functionList.size(), 0);
}

void ChaserRunner_Test::writeForwardPingPongZero()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::PingPong);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QVERIFY(m_scene1->stopped() == true);
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QVERIFY(m_scene1->stopped() == true);
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);
}

void ChaserRunner_Test::writeBackwardPingPongZero()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::PingPong);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);
}

void ChaserRunner_Test::writeForwardLoopFive()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    uint dur = MasterTimer::tick() * 5;
    m_chaser->setDuration(dur);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }
}

void ChaserRunner_Test::writeBackwardLoopFive()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::Loop);

    uint dur = MasterTimer::tick() * 5;
    m_chaser->setDuration(dur);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }
}

void ChaserRunner_Test::writeForwardSingleShotFive()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::SingleShot);

    uint dur = MasterTimer::tick() * 5;
    m_chaser->setDuration(dur);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    QVERIFY(cr.write(&timer, QList<Universe*>()) == false);
    timer.timerTick();

    cr.postRun(&timer, QList<Universe*>());
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 0);
}

void ChaserRunner_Test::writeBackwardSingleShotFive()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::SingleShot);

    uint dur = MasterTimer::tick() * 5;
    m_chaser->setDuration(dur);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    QVERIFY(cr.write(&timer, QList<Universe*>()) == false);
    timer.timerTick();

    cr.postRun(&timer, QList<Universe*>());
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 0);
}

void ChaserRunner_Test::writeForwardPingPongFive()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::PingPong);

    uint dur = MasterTimer::tick() * 5;
    m_chaser->setDuration(dur);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }
}

void ChaserRunner_Test::writeBackwardPingPongFive()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::PingPong);

    uint dur = MasterTimer::tick() * 5;
    m_chaser->setDuration(dur);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 3
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    // Step 2
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    // Step 1
    for (uint i = 0; i < dur; i += MasterTimer::tick())
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }
}

void ChaserRunner_Test::writeNoAutoStep()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    for (int i = 0; i < 10; i++)
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    ChaserAction action;
    action.m_action = ChaserNextStep;
    cr.setAction(action);

    for (int i = 0; i < 10; i++)
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    cr.setAction(action);

    for (int i = 0; i < 10; i++)
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    cr.setAction(action);

    for (int i = 0; i < 10; i++)
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }

    action.m_action = ChaserPreviousStep;
    cr.setAction(action);

    for (int i = 0; i < 10; i++)
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene3);
    }

    cr.setAction(action);

    for (int i = 0; i < 10; i++)
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene2);
    }

    cr.setAction(action);

    for (int i = 0; i < 10; i++)
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QCOMPARE(timer.m_functionList[0], m_scene1);
    }
}

void ChaserRunner_Test::adjustIntensity()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    cr.adjustStepIntensity(0.5);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);
    QCOMPARE(m_scene1->getAttributeValue(Scene::ParentIntensity), qreal(0.5));
    QCOMPARE(m_scene2->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene3->getAttributeValue(Function::Intensity), qreal(1.0));

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene2->getAttributeValue(Scene::ParentIntensity), qreal(0.5));
    QCOMPARE(m_scene3->getAttributeValue(Function::Intensity), qreal(1.0));

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene2->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene3->getAttributeValue(Scene::ParentIntensity), qreal(0.5));

    cr.adjustStepIntensity(0.7);
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene2->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene3->getAttributeValue(Scene::ParentIntensity), qreal(0.7));

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);
    QCOMPARE(m_scene1->getAttributeValue(Scene::ParentIntensity), qreal(0.7));
    QCOMPARE(m_scene2->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene3->getAttributeValue(Function::Intensity), qreal(1.0));

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene2->getAttributeValue(Scene::ParentIntensity), qreal(0.7));
    QCOMPARE(m_scene3->getAttributeValue(Function::Intensity), qreal(1.0));

    cr.adjustStepIntensity(1.5);
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene2->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene3->getAttributeValue(Function::Intensity), qreal(1.0));

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene2->getAttributeValue(Function::Intensity), qreal(1.0));
    QCOMPARE(m_scene3->getAttributeValue(Function::Intensity), qreal(1.0));
}

void ChaserRunner_Test::adjustMasterIntensityAcrossRunningCrossfadeSteps()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer *timer = m_doc->masterTimer();

    cr.adjustStepIntensity(0.75, 0, Chaser::BlendedCrossfade);
    timer->timerTick();
    cr.adjustStepIntensity(0.25, 1, Chaser::BlendedCrossfade);
    timer->timerTick();

    QCOMPARE(cr.runningStepsNumber(), 2);
    QCOMPARE(cr.m_runnerSteps.size(), 2);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_index, 0);
    QCOMPARE(cr.m_runnerSteps.at(1)->m_index, 1);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_masterIntensity, qreal(1.0));
    QCOMPARE(cr.m_runnerSteps.at(1)->m_masterIntensity, qreal(1.0));
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(0.75));
    QCOMPARE(m_scene2->getAttributeValue(Function::Intensity), qreal(0.25));
    QCOMPARE(m_scene1->getAttributeValue(Scene::ParentIntensity), qreal(1.0));
    QCOMPARE(m_scene2->getAttributeValue(Scene::ParentIntensity), qreal(1.0));

    cr.adjustStepIntensity(0.0);

    QCOMPARE(cr.m_runnerSteps.at(0)->m_masterIntensity, qreal(0.0));
    QCOMPARE(cr.m_runnerSteps.at(1)->m_masterIntensity, qreal(0.0));
    QCOMPARE(m_scene1->getAttributeValue(Scene::ParentIntensity), qreal(0.0));
    QCOMPARE(m_scene2->getAttributeValue(Scene::ParentIntensity), qreal(0.0));

    timer->timerTick();

    QList<Universe *> universes = m_doc->inputOutputMap()->claimUniverses();
    universes[0]->processFaders(MasterTimer::tick());
    QCOMPARE(universes[0]->postGMValue(0), uchar(0));
    QCOMPARE(universes[0]->postGMValue(1), uchar(0));
    m_doc->inputOutputMap()->releaseUniverses(false);
}

void ChaserRunner_Test::startTimeOffset()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(1000);

    // A start time inside the second step: start there, part way through
    ChaserRunner cr(m_doc, m_chaser, 1500);
    QCOMPARE(cr.m_pendingAction.m_action, ChaserSetStepIndex);
    QCOMPARE(cr.m_pendingAction.m_stepIndex, 1);
    QCOMPARE(cr.m_startOffset, quint32(500));

    MasterTimer timer(m_doc);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 1);
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);
    // The offset plus the start tick, plus the tick of this write's round
    QCOMPARE(cr.m_runnerSteps.at(0)->m_elapsed, quint32(500 + 2 * MasterTimer::tick()));
    QCOMPARE(cr.m_startOffset, quint32(0));

    // Per-step durations are used to find the step too
    m_chaser->setDurationMode(Chaser::PerStep);
    m_chaser->replaceStep(ChaserStep(m_scene1->id(), 0, 100, 0), 0);
    m_chaser->replaceStep(ChaserStep(m_scene2->id(), 0, 200, 0), 1);
    m_chaser->replaceStep(ChaserStep(m_scene3->id(), 0, 300, 0), 2);
    ChaserRunner cr2(m_doc, m_chaser, 350);
    QCOMPARE(cr2.m_pendingAction.m_stepIndex, 2);
    QCOMPARE(cr2.m_startOffset, quint32(50));

    // A start time beyond the last step starts normally
    ChaserRunner cr3(m_doc, m_chaser, 5000);
    QCOMPARE(cr3.m_pendingAction.m_action, ChaserNoAction);
    QCOMPARE(cr3.m_startOffset, quint32(0));
}

void ChaserRunner_Test::stopStepAction()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer *timer = m_doc->masterTimer();
    QSignalSpy spy(&cr, SIGNAL(currentStepChanged(int)));

    // Two crossfading steps running at the same time
    cr.adjustStepIntensity(0.75, 0, Chaser::Crossfade);
    timer->timerTick();
    cr.adjustStepIntensity(0.25, 1, Chaser::Crossfade);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 2);
    QVERIFY(m_scene1->isRunning());
    QVERIFY(m_scene2->isRunning());

    // Stopping a step that is not running changes nothing
    ChaserAction action;
    action.m_action = ChaserStopStep;
    action.m_stepIndex = 2;
    cr.setAction(action);
    QCOMPARE(cr.runningStepsNumber(), 2);
    QCOMPARE(spy.count(), 0);
    QCOMPARE(cr.m_pendingAction.m_action, ChaserNoAction);

    // Stopping the first step leaves the second as the current one
    action.m_stepIndex = 0;
    cr.setAction(action);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 1);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_index, 1);
    QCOMPARE(cr.currentStepIndex(), 1);
    QCOMPARE(cr.m_lastFunctionID, m_scene1->id());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    QVERIFY(m_scene1->isRunning() == false);
    QVERIFY(m_scene2->isRunning());

    // Stopping the last running step doesn't emit a step change
    action.m_stepIndex = 1;
    cr.setAction(action);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 0);
    QCOMPARE(spy.count(), 1);
    QVERIFY(m_scene2->isRunning() == false);
}

void ChaserRunner_Test::currentRunningStep()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.currentRunningStep() == NULL);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    ChaserRunnerStep *step = cr.currentRunningStep();
    QVERIFY(step != NULL);
    QCOMPARE(step, cr.m_runnerSteps.at(0));
    QCOMPARE(step->m_index, 0);
    QCOMPARE(step->m_function, m_scene1);
}

void ChaserRunner_Test::computeNextStepLoop()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    ChaserRunner cr(m_doc, m_chaser);

    // Forward: in the middle, wrapping past the end and before the start
    QCOMPARE(cr.computeNextStep(0), 1);
    QCOMPARE(cr.computeNextStep(1), 2);
    QCOMPARE(cr.computeNextStep(2), 0);
    QCOMPARE(cr.computeNextStep(-2), 2);

    // Backward: in the middle, wrapping before the start and past the end
    cr.m_direction = Function::Backward;
    QCOMPARE(cr.computeNextStep(2), 1);
    QCOMPARE(cr.computeNextStep(1), 0);
    QCOMPARE(cr.computeNextStep(0), 2);
    QCOMPARE(cr.computeNextStep(4), 0);
}

void ChaserRunner_Test::computeNextStepSingleShotPingPong()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::SingleShot);

    ChaserRunner cr(m_doc, m_chaser);
    QCOMPARE(cr.computeNextStep(1), 2);
    QCOMPARE(cr.computeNextStep(2), -1);
    cr.m_direction = Function::Backward;
    QCOMPARE(cr.computeNextStep(1), 0);
    QCOMPARE(cr.computeNextStep(0), -1);

    m_chaser->setRunOrder(Function::PingPong);
    cr.m_direction = Function::Forward;
    QCOMPARE(cr.computeNextStep(0), 1);
    QCOMPARE(cr.computeNextStep(2), 1); // bounce back, don't repeat the last step
    cr.m_direction = Function::Backward;
    QCOMPARE(cr.computeNextStep(2), 1);
    QCOMPARE(cr.computeNextStep(0), 1); // bounce back, don't repeat the first step

    // computeNextStep() never changes the run-time direction
    QCOMPARE(cr.m_direction, Function::Backward);
}

void ChaserRunner_Test::computeNextStepRandom()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Random);

    ChaserRunner cr(m_doc, m_chaser);
    QCOMPARE(cr.m_order.size(), 3);

    // Impose a known order: position 0 -> step 2, 1 -> step 0, 2 -> step 1
    cr.m_order[0] = 2;
    cr.m_order[1] = 0;
    cr.m_order[2] = 1;
    QCOMPARE(cr.randomStepIndex(0), 2);
    QCOMPARE(cr.randomStepIndex(2), 1);
    QCOMPARE(cr.randomStepIndex(3), 3);
    QCOMPARE(cr.randomStepIndex(-1), -1);

    // Step 0 sits at position 1, so the next one is at position 2 -> step 1
    QCOMPARE(cr.computeNextStep(0), 1);
    // Step 2 sits at position 0 -> position 1 -> step 0
    QCOMPARE(cr.computeNextStep(2), 0);
    // Step 1 sits at the last position: wraps beyond the order
    QCOMPARE(cr.computeNextStep(1), 3);
    // A step that is not in the order is used as-is
    QCOMPARE(cr.computeNextStep(7), 8);

    cr.m_direction = Function::Backward;
    QCOMPARE(cr.computeNextStep(1), 0);
    QCOMPARE(cr.computeNextStep(2), -1);
}

void ChaserRunner_Test::writeRandomForward()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Random);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    cr.m_order[0] = 2;
    cr.m_order[1] = 0;
    cr.m_order[2] = 1;

    // The first round follows the imposed order
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene3);
    QCOMPARE(cr.currentStepIndex(), 2);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);
    QCOMPARE(cr.currentStepIndex(), 0);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);
    QCOMPARE(cr.currentStepIndex(), 1);

    // At the end of the round the order is reshuffled: the next step is
    // unknown but never the one that just ran
    for (int round = 0; round < 60; round++)
    {
        int last = cr.currentStepIndex();
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QVERIFY(cr.currentStepIndex() != last);
        QVERIFY(cr.currentStepIndex() >= 0 && cr.currentStepIndex() < 3);
        QCOMPARE(timer.m_functionList[0], m_doc->function(m_chaser->steps().at(cr.currentStepIndex()).fid));
    }
}

void ChaserRunner_Test::writeRandomBackward()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::Random);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    cr.m_order[0] = 2;
    cr.m_order[1] = 0;
    cr.m_order[2] = 1;

    // Backward starts from the last position of the order
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList[0], m_scene2);
    QCOMPARE(cr.currentStepIndex(), 1);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList[0], m_scene1);
    QCOMPARE(cr.currentStepIndex(), 0);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList[0], m_scene3);
    QCOMPARE(cr.currentStepIndex(), 2);

    for (int round = 0; round < 60; round++)
    {
        int last = cr.currentStepIndex();
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
        QCOMPARE(timer.m_functionList.size(), 1);
        QVERIFY(cr.currentStepIndex() != last);
        QVERIFY(cr.currentStepIndex() >= 0 && cr.currentStepIndex() < 3);
    }
}

void ChaserRunner_Test::writeRandomPrevious()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Random);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    cr.m_order[0] = 2;
    cr.m_order[1] = 0;
    cr.m_order[2] = 1;

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 2);

    // "Previous" from the first position wraps to the end of a fresh order
    ChaserAction action;
    action.m_action = ChaserPreviousStep;
    cr.setAction(action);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QVERIFY(cr.currentStepIndex() != 2);
    QVERIFY(cr.currentStepIndex() >= 0 && cr.currentStepIndex() < 3);

    // Same thing running backwards: "previous" moves up in the order
    m_chaser->setDirection(Function::Backward);
    ChaserRunner crb(m_doc, m_chaser);
    crb.m_order[0] = 2;
    crb.m_order[1] = 0;
    crb.m_order[2] = 1;

    QVERIFY(crb.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(crb.currentStepIndex(), 1);

    crb.setAction(action);
    QVERIFY(crb.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QVERIFY(crb.currentStepIndex() != 1);
    QVERIFY(crb.currentStepIndex() >= 0 && crb.currentStepIndex() < 3);
}

void ChaserRunner_Test::writeRandomSetStepIndex()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Random);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    cr.m_order[0] = 2;
    cr.m_order[1] = 0;
    cr.m_order[2] = 1;

    // A requested index is a position in the randomized order
    ChaserAction action;
    action.m_action = ChaserSetStepIndex;
    action.m_stepIndex = 1;
    action.m_masterIntensity = 1.0;
    action.m_stepIntensity = 1.0;
    action.m_fadeMode = Chaser::FromFunction;
    cr.setAction(action);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene1);
    QCOMPARE(cr.currentStepIndex(), 0);
}

void ChaserRunner_Test::writePingPongPrevious()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::PingPong);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 0);
    QCOMPARE(cr.m_direction, Function::Forward);

    // "Previous" at the first step reverses the direction: the previous
    // step of a ping pong at the start is the second one
    ChaserAction action;
    action.m_action = ChaserPreviousStep;
    cr.setAction(action);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 1);
    QCOMPARE(cr.m_direction, Function::Backward);
    QCOMPARE(timer.m_functionList[0], m_scene2);

    // Still going "previous" while backwards moves up
    cr.setAction(action);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 2);
    QCOMPARE(cr.m_direction, Function::Backward);

    // "Previous" at the last step reverses the direction again
    cr.setAction(action);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 1);
    QCOMPARE(cr.m_direction, Function::Forward);
    QCOMPARE(timer.m_functionList[0], m_scene2);
}

void ChaserRunner_Test::writeBackwardPrevious()
{
    m_chaser->setDirection(Function::Backward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 2);
    QCOMPARE(timer.m_functionList[0], m_scene3);

    // "Previous" for a backward loop at its first step wraps to step 0
    ChaserAction action;
    action.m_action = ChaserPreviousStep;
    cr.setAction(action);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 0);
    QCOMPARE(timer.m_functionList[0], m_scene1);

    cr.setAction(action);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);
}

void ChaserRunner_Test::writeBeats()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setTempoType(Function::Beats);
    m_chaser->setDuration(2000); // two beats per step

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 0);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_elapsedBeats, quint32(0));

    // Ticks without a beat don't advance the step
    for (int i = 0; i < 5; i++)
    {
        QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
        timer.timerTick();
    }
    QCOMPARE(cr.currentStepIndex(), 0);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_elapsedBeats, quint32(0));

    // First beat: one beat elapsed, still on the same step
    timer.m_beatRequested = true;
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.m_beatRequested = false;
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 0);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_elapsedBeats, quint32(1000));

    // Second beat: the step is over and the next one starts
    timer.m_beatRequested = true;
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.m_beatRequested = false;
    timer.timerTick();
    QCOMPARE(cr.currentStepIndex(), 1);
    QCOMPARE(timer.m_functionList.size(), 1);
    QCOMPARE(timer.m_functionList[0], m_scene2);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_elapsedBeats, quint32(0));
}

void ChaserRunner_Test::speedChangeWhileRunning()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());
    m_chaser->setFadeInMode(Chaser::Common);
    m_chaser->setFadeOutMode(Chaser::Common);
    m_chaser->setFadeInSpeed(100);
    m_chaser->setFadeOutSpeed(200);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.m_runnerSteps.at(0)->m_fadeIn, uint(100));
    QCOMPARE(cr.m_runnerSteps.at(0)->m_fadeOut, uint(200));
    QCOMPARE(m_scene1->overrideFadeInSpeed(), uint(100));
    QCOMPARE(cr.m_updateOverrideSpeeds, false);

    // Changing the chaser speeds updates the running step right away...
    m_chaser->setFadeInSpeed(300);
    QCOMPARE(cr.m_updateOverrideSpeeds, true);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_fadeIn, uint(300));
    QCOMPARE(cr.m_runnerSteps.at(0)->m_fadeOut, uint(200));
    m_chaser->setFadeOutSpeed(400);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_fadeOut, uint(400));
    QCOMPARE(cr.runningStepsNumber(), 1);

    // ...and the running Function gets the new speeds on the next write
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.m_updateOverrideSpeeds, false);
    QCOMPARE(m_scene1->overrideFadeInSpeed(), uint(300));
    QCOMPARE(m_scene1->overrideFadeOutSpeed(), uint(400));
    QCOMPARE(cr.currentStepIndex(), 0);
}

void ChaserRunner_Test::pauseNoSteps()
{
    Chaser empty(m_doc);
    ChaserRunner cr(m_doc, &empty);

    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));

    // Nothing to pause, nothing to write
    cr.setPause(true, ua);
    MasterTimer timer(m_doc);
    QVERIFY(cr.write(&timer, ua) == false);
    QCOMPARE(cr.runningStepsNumber(), 0);

    delete ua.takeFirst();
}

void ChaserRunner_Test::pauseWithUniverses()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QList<Universe*> ua;
    ua.append(new Universe(0, new GrandMaster()));

    QVERIFY(cr.write(&timer, ua) == true);
    timer.timerTick();
    QVERIFY(m_scene1->isRunning());

    // Move to the second step: the first Scene becomes the last function
    // ran, whose faders would still be fading out on the universes
    ChaserAction action;
    action.m_action = ChaserNextStep;
    cr.setAction(action);
    QVERIFY(cr.write(&timer, ua) == true);
    timer.timerTick();
    QCOMPARE(cr.m_lastFunctionID, m_scene1->id());
    QVERIFY(m_scene2->isRunning());
    QVERIFY(m_scene2->isPaused() == false);

    // A pause request is processed by the next write
    action.m_action = ChaserPauseRequest;
    action.m_fadeMode = 1;
    cr.setAction(action);
    QCOMPARE(cr.m_pendingAction.m_action, ChaserPauseRequest);
    QVERIFY(cr.write(&timer, ua) == true);
    QVERIFY(m_scene2->isPaused());
    QCOMPARE(cr.m_pendingAction.m_action, ChaserNoAction);

    cr.setPause(false, ua);
    QVERIFY(m_scene2->isPaused() == false);

    // The runner must survive the last function disappearing
    m_doc->deleteFunction(m_scene1->id());
    cr.setPause(true, ua);
    QVERIFY(m_scene2->isPaused());
    cr.setPause(false, ua);

    delete ua.takeFirst();
}

void ChaserRunner_Test::adjustRunningStepIntensity()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.runningStepsNumber(), 1);
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(1.0));

    // Adjusting the intensity of the running step doesn't start a new one
    cr.adjustStepIntensity(0.5, 0);
    QCOMPARE(cr.runningStepsNumber(), 1);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_stepIntensity, qreal(0.5));
    QCOMPARE(cr.m_runnerSteps.at(0)->m_masterIntensity, qreal(1.0));
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(0.5));
    QCOMPARE(m_scene1->getAttributeValue(Scene::ParentIntensity), qreal(1.0));

    // The master intensity is kept separate from the step intensity
    cr.adjustStepIntensity(0.5);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_stepIntensity, qreal(0.5));
    QCOMPARE(cr.m_runnerSteps.at(0)->m_masterIntensity, qreal(0.5));
    QCOMPARE(m_scene1->getAttributeValue(Function::Intensity), qreal(0.5));
    QCOMPARE(m_scene1->getAttributeValue(Scene::ParentIntensity), qreal(0.5));
}

void ChaserRunner_Test::adjustIntensityNonSceneStep()
{
    Collection *coll = new Collection(m_doc);
    m_doc->addFunction(coll);
    coll->addFunction(m_scene1->id());

    Chaser *chaser = new Chaser(m_doc);
    m_doc->addFunction(chaser);
    chaser->addStep(ChaserStep(coll->id()));
    chaser->setDuration(Function::infiniteSpeed());

    ChaserRunner cr(m_doc, chaser);
    MasterTimer timer(m_doc);

    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.runningStepsNumber(), 1);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_function, coll);
    QVERIFY(cr.m_runnerSteps.at(0)->m_intensityOverrideId != Function::invalidAttributeId());
    QCOMPARE(cr.m_runnerSteps.at(0)->m_pIntensityOverrideId, Function::invalidAttributeId());
    QCOMPARE(cr.m_lastFunctionID, Function::invalidId());

    // A non-Scene step gets the product of master and step intensity
    cr.adjustStepIntensity(0.5);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_masterIntensity, qreal(0.5));
    QCOMPARE(coll->getAttributeValue(Function::Intensity), qreal(0.5));

    cr.adjustStepIntensity(0.5, 0);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_stepIntensity, qreal(0.5));
    QCOMPARE(coll->getAttributeValue(Function::Intensity), qreal(0.25));

    // Moving on from a non-Scene step: no Scene blending is set up
    ChaserAction action;
    action.m_action = ChaserNextStep;
    cr.setAction(action);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.m_lastFunctionID, Function::invalidId());
}

void ChaserRunner_Test::adjustIntensityEdgeCases()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());
    // A step whose Function doesn't exist
    m_chaser->addStep(ChaserStep(12345));
    QCOMPARE(m_chaser->stepsCount(), 4);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer *timer = m_doc->masterTimer();

    // A zero intensity never starts a new step
    cr.adjustStepIntensity(0.0, 2);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 0);

    // A step with a missing Function is not started
    cr.adjustStepIntensity(1.0, 3);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 0);

    // An index out of range falls back to the first step
    cr.adjustStepIntensity(1.0, 7);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 1);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_index, 0);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_function, m_scene1);

    // Without steps there's nothing to start
    Chaser empty(m_doc);
    ChaserRunner crEmpty(m_doc, &empty);
    crEmpty.adjustStepIntensity(1.0, 0);
    QCOMPARE(crEmpty.runningStepsNumber(), 0);
}

void ChaserRunner_Test::adjustIntensityFadeModes()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);
    m_chaser->setDuration(Function::infiniteSpeed());
    m_chaser->setFadeInMode(Chaser::Common);
    m_chaser->setFadeOutMode(Chaser::Common);
    m_chaser->setFadeInSpeed(100);
    m_chaser->setFadeOutSpeed(200);

    ChaserRunner cr(m_doc, m_chaser);
    MasterTimer *timer = m_doc->masterTimer();

    // Blended keeps the Function fade times
    cr.adjustStepIntensity(0.5, 0, Chaser::Blended);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 1);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_fadeIn, uint(100));
    QCOMPARE(cr.m_runnerSteps.at(0)->m_fadeOut, uint(200));
    QCOMPARE(m_scene1->blendFunctionID(), Function::invalidId());

    // Crossfade and BlendedCrossfade fade immediately (the slider fades)
    cr.adjustStepIntensity(0.5, 1, Chaser::Crossfade);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 2);
    QCOMPARE(cr.m_runnerSteps.at(1)->m_fadeIn, uint(0));
    QCOMPARE(cr.m_runnerSteps.at(1)->m_fadeOut, uint(0));
    // A Scene started on top of another blends into the previous one
    QCOMPARE(m_scene2->blendFunctionID(), m_scene1->id());

    cr.adjustStepIntensity(0.5, 2, Chaser::BlendedCrossfade);
    timer->timerTick();
    QCOMPARE(cr.runningStepsNumber(), 3);
    QCOMPARE(cr.m_runnerSteps.at(2)->m_fadeIn, uint(0));
    QCOMPARE(cr.m_runnerSteps.at(2)->m_fadeOut, uint(0));
    // ...and the previous one stops blending into its own predecessor
    QCOMPARE(m_scene3->blendFunctionID(), m_scene2->id());
    QCOMPARE(m_scene2->blendFunctionID(), Function::invalidId());
    QCOMPARE(m_scene1->blendFunctionID(), Function::invalidId());
}

void ChaserRunner_Test::adjustIntensityNullFunctionStep()
{
    m_chaser->setDirection(Function::Forward);
    m_chaser->setRunOrder(Function::Loop);

    ChaserRunner cr(m_doc, m_chaser);

    // A running step whose Function has gone is skipped
    ChaserRunnerStep *fake = new ChaserRunnerStep();
    fake->m_index = 0;
    fake->m_function = NULL;
    fake->m_masterIntensity = 1.0;
    fake->m_stepIntensity = 1.0;
    cr.m_runnerSteps.append(fake);

    cr.adjustStepIntensity(0.5);
    QCOMPARE(fake->m_masterIntensity, qreal(1.0));
    QCOMPARE(cr.m_pendingAction.m_masterIntensity, qreal(0.5));

    cr.m_runnerSteps.removeAll(fake);
    delete fake;
}

void ChaserRunner_Test::sequenceSteps()
{
    quint32 fxiID = m_scene1->values().at(0).fxi;

    Sequence *seq = new Sequence(m_doc);
    seq->setBoundSceneID(m_scene1->id());
    seq->setDuration(Function::infiniteSpeed());
    m_doc->addFunction(seq);

    ChaserStep step1(m_scene1->id());
    step1.values << SceneValue(fxiID, 0, 42) << SceneValue(fxiID, 1, 43);
    seq->addStep(step1);
    ChaserStep step2(m_scene1->id());
    step2.values << SceneValue(fxiID, 0, 99) << SceneValue(fxiID, 1, 98);
    seq->addStep(step2);

    ChaserRunner cr(m_doc, seq);
    MasterTimer timer(m_doc);

    // A Sequence step loads its values into the bound Scene
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.runningStepsNumber(), 1);
    QCOMPARE(cr.m_runnerSteps.at(0)->m_function, m_scene1);
    QCOMPARE(m_scene1->value(fxiID, 0), uchar(42));
    QCOMPARE(m_scene1->value(fxiID, 1), uchar(43));
    QVERIFY(m_scene1->isRunning());

    // The next step reuses the same Scene with the new values
    ChaserAction action;
    action.m_action = ChaserNextStep;
    cr.setAction(action);
    QVERIFY(cr.write(&timer, QList<Universe*>()) == true);
    timer.timerTick();
    QCOMPARE(cr.runningStepsNumber(), 1);
    QCOMPARE(cr.currentStepIndex(), 1);
    QCOMPARE(m_scene1->value(fxiID, 0), uchar(99));
    QCOMPARE(m_scene1->value(fxiID, 1), uchar(98));

    cr.postRun(&timer, QList<Universe*>());
    QCOMPARE(cr.runningStepsNumber(), 0);
}

QTEST_APPLESS_MAIN(ChaserRunner_Test)
