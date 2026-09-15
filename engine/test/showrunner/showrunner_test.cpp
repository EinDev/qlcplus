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
#include "chaser.h"
#include "chaserstep.h"
#include "audio.h"
#include "fixture.h"
#include "doc.h"
#include "inputoutputmap.h"
#include "showrunner_test.h"

/****************************************************************************
 * Helpers for the live-rescheduling cases
 ****************************************************************************/

/**
 * A Show with one Scene clip on its own track, plus an optional long "filler"
 * clip on a second track that keeps the Show alive past the first clip's end
 * (the runner ends the Show as soon as the playhead passes the last clip end).
 * Each case owns its Doc so that the class fixture above stays untouched.
 */
struct LiveShow
{
    Doc *doc;
    Fixture *fixture;
    Show *show;
    Track *track;
    Scene *scene;
    ShowFunction *sf;
    Track *fillerTrack;
    Scene *fillerScene;
    ShowFunction *fillerSf;

    LiveShow(QObject *parent, quint32 start, quint32 duration, quint32 fillerDuration = 0)
        : fillerTrack(NULL), fillerScene(NULL), fillerSf(NULL)
    {
        doc = new Doc(parent);

        // Scene::write() stops a Scene with no values on its first tick, so
        // every test Scene needs at least one channel to keep running
        fixture = new Fixture(doc);
        fixture->setAddress(0);
        fixture->setUniverse(0);
        fixture->setChannels(1);
        doc->addFixture(fixture);

        show = new Show(doc);
        doc->addFunction(show);

        scene = makeScene("clip");

        track = new Track(scene->id(), show);
        show->addTrack(track);
        sf = track->createShowFunction(scene->id());
        sf->setStartTime(start);
        sf->setDuration(duration);

        if (fillerDuration > 0)
        {
            fillerScene = makeScene("filler");
            fillerTrack = new Track(fillerScene->id(), show);
            show->addTrack(fillerTrack);
            fillerSf = fillerTrack->createShowFunction(fillerScene->id());
            fillerSf->setStartTime(0);
            fillerSf->setDuration(fillerDuration);
        }

        show->rebuildSchedule();
    }

    ~LiveShow()
    {
        delete doc;
    }

    /** A Scene that keeps running once started (see the fixture above) */
    Scene *makeScene(const QString &name)
    {
        Scene *s = new Scene(doc);
        s->setName(name);
        s->setValue(fixture->id(), 0, 255);
        doc->addFunction(s);
        return s;
    }

    MasterTimer *timer() const { return doc->masterTimer(); }

    /** Run the runner (and the MasterTimer, so child functions get their
     *  preRun/postRun) until the playhead reaches $ms. */
    void advanceTo(ShowRunner &runner, quint32 ms)
    {
        while (runner.m_elapsedTime < ms)
        {
            runner.write(timer());
            timer()->timerTick();
        }
    }

    /** Apply a timeline edit the way the GUI thread does: rebuild the
     *  snapshot synchronously (the app queues this on the event loop). */
    void commitEdit()
    {
        show->rebuildSchedule();
    }

    /** Turn the Show into a Beats Show with a known beat source: the
     *  Internal generator at MasterTimer's default 120 BPM. (Its beats are
     *  only ever raised inside MasterTimer::timerTick(), which clears them
     *  again before returning, so the runner never sees one unless a test
     *  pulses it explicitly - see pulseBeat().) */
    void makeBeatsShow()
    {
        show->setTempoType(Function::Beats);
        commitEdit();
        doc->inputOutputMap()->setBeatGeneratorType(InputOutputMap::Internal);
    }

    /** One detected beat pulse processed by the runner. MasterTimer normally
     *  clears its own "beat requested" flag once per real timer tick after
     *  every listener has seen it; since no timer thread is running here,
     *  clear it manually so each call represents exactly one beat. */
    void pulseBeat(ShowRunner &runner)
    {
        timer()->requestBeat();
        runner.write(timer());
        timer()->m_beatRequested = false;
    }

    /** Frozen (scrub mode) runner: $n ticks of runner + MasterTimer. The
     *  playhead does not move while frozen, so advanceTo() would never
     *  return here. */
    void frozenTicks(ShowRunner &runner, int n)
    {
        for (int i = 0; i < n; i++)
        {
            runner.write(timer());
            timer()->timerTick();
        }
    }
};

static bool queueHas(const ShowRunner &runner, quint32 sfId)
{
    return runner.runningIndex(sfId) != -1;
}

/****************************************************************************
 * Pre-existing cases
 ****************************************************************************/

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
    QCOMPARE(runner.m_schedule->clips.count(), 1);
    QVERIFY(runner.m_schedule->showTempo == Function::Time);
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
    ShowRunner::RunningClip rc;
    rc.sfId = 0;
    rc.functionId = m_scene->id();
    rc.trackId = m_track->id();
    rc.start = 0;
    rc.stopTime = 1000;
    rc.function = m_scene;
    rc.overrideId = -1;
    runner.m_runningQueue.append(rc);
    runner.stop();
    QCOMPARE(runner.m_elapsedTime, quint32(0));
    QCOMPARE(runner.m_runningQueue.count(), 0);
}

void ShowRunner_Test::beatsShowRunsOnWallClock()
{
    // ShowFunction::startTime()/duration() are real milliseconds laid out on
    // the Show's own BPM grid (ADR 0001), so a Beats Show starts and stops its
    // clips on the wall clock exactly like a Time Show - never on a clock
    // stepped by the global beat generator, whose BPM may differ or be
    // unknown. The only beat-specific behaviour is the start: the Show holds
    // until the first beat pulse so that its timeline lines up with the grid.
    LiveShow ls(this, 3000, 2000, 60000);
    ls.scene->setTempoType(Function::Beats);
    ls.makeBeatsShow();
    QCOMPARE(ls.doc->inputOutputMap()->bpmNumber(), 120);

    ShowRunner runner(ls.doc, ls.show->id());
    QVERIFY(runner.m_schedule->showTempo == Function::Beats);
    QCOMPARE(runner.m_schedule->clips.count(), 2);
    QVERIFY(runner.m_waitingForBeat == true);

    // no beat yet: nothing starts and the playhead does not move
    for (int i = 0; i < 5; i++)
        runner.write(ls.timer());
    QCOMPARE(runner.m_elapsedTime, quint32(0));
    QCOMPARE(runner.m_runningQueue.count(), 0);

    // the first beat releases the Show: the filler starts, the playhead runs
    ls.pulseBeat(runner);
    QVERIFY(runner.m_waitingForBeat == false);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
    QCOMPARE(runner.m_elapsedTime, quint32(MasterTimer::tick()));

    // from here on the clip follows the wall clock, with no further beats
    ls.advanceTo(runner, 3000);
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), quint32(0));

    ls.advanceTo(runner, 5000);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
}

void ShowRunner_Test::beatsShowWithoutBpmPlays()
{
    // With the beat generator disabled there is no beat source at all
    // (bpmNumber() == 0, no pulse will ever come): a Beats Show must not
    // hold for a beat but start straight away on the wall clock.
    LiveShow ls(this, 1000, 2000, 60000);
    ls.show->setTempoType(Function::Beats);
    ls.commitEdit();
    QCOMPARE(ls.doc->inputOutputMap()->bpmNumber(), 0);

    ShowRunner runner(ls.doc, ls.show->id());
    QVERIFY(runner.m_waitingForBeat == true);

    runner.write(ls.timer());
    QVERIFY(runner.m_waitingForBeat == false);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
    QCOMPARE(runner.m_elapsedTime, quint32(MasterTimer::tick()));

    ls.advanceTo(runner, 1000);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);

    ls.advanceTo(runner, 3000);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
}

/****************************************************************************
 * Live rescheduling
 ****************************************************************************/

void ShowRunner_Test::scheduleNotifications()
{
    // Every model-level mutation that changes the timeline must flag the
    // schedule dirty (the app then rebuilds it once per event-loop turn):
    // the mute button and Tardis undo/redo bypass ShowManager entirely.
    LiveShow ls(this, 0, 10000);
    QVERIFY(ls.show->isScheduleDirty() == false);

    ls.sf->setDuration(15000);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();
    QVERIFY(ls.show->isScheduleDirty() == false);

    ls.sf->setStartTime(1000);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();

    ls.sf->setFunctionID(ls.scene->id() + 1);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.sf->setFunctionID(ls.scene->id());
    ls.commitEdit();

    // color/lock are cosmetic and must not cause a rebuild
    ls.sf->setColor(Qt::red);
    ls.sf->setLocked(true);
    QVERIFY(ls.show->isScheduleDirty() == false);

    ls.track->setMute(true);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();
    ls.track->setMute(false);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();

    ShowFunction *added = ls.track->createShowFunction(ls.scene->id());
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();

    // a clip taken out of a track must stop notifying that track
    QVERIFY(ls.track->removeShowFunction(added, false) == true);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();
    added->setStartTime(4242);
    QVERIFY(ls.show->isScheduleDirty() == false);
    delete added;

    Track *second = new Track(Function::invalidId(), ls.show);
    ls.show->addTrack(second);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();

    ls.show->moveTrack(second, -1);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();

    ls.show->removeTrack(second->id());
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();

    // the snapshot resolves a zero clip duration to the Function's own, so
    // a change to a referenced Function counts too - but not to others
    Scene *unrelated = ls.makeScene("unrelated");
    unrelated->setFadeInSpeed(100);
    QVERIFY(ls.show->isScheduleDirty() == false);
    ls.scene->setFadeInSpeed(100);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();

    QSignalSpy spy(ls.show, SIGNAL(scheduleChanged()));
    ls.commitEdit();
    QCOMPARE(spy.count(), 1);
}

void ShowRunner_Test::queuedRebuildCoalesces()
{
    // In the app nobody calls rebuildSchedule() after an edit: markScheduleDirty()
    // queues one rebuild on the event loop and a burst of edits (a drag, undo
    // walking many steps) must collapse into a single rebuild per turn.
    LiveShow ls(this, 0, 10000);
    QCoreApplication::processEvents();          // drain the set-up edits' queued call
    QVERIFY(ls.show->isScheduleDirty() == false);
    QSignalSpy spy(ls.show, SIGNAL(scheduleChanged()));

    // a runner created now has consumed the current snapshot
    ShowRunner runner(ls.doc, ls.show->id());
    QVERIFY(ls.show->takePendingSchedule().isNull());

    for (int i = 1; i <= 20; i++)
        ls.sf->setDuration(10000 + i * 100);
    ls.track->setMute(true);
    ls.track->setMute(false);
    QVERIFY(ls.show->isScheduleDirty() == true);
    QCOMPARE(spy.count(), 0);

    QCoreApplication::processEvents();
    QVERIFY(ls.show->isScheduleDirty() == false);
    QCOMPARE(spy.count(), 1);

    // the runner finds exactly one pending snapshot with the final values
    QSharedPointer<const ShowSchedule> pending = ls.show->takePendingSchedule();
    QVERIFY(pending.isNull() == false);
    QCOMPARE(pending->clips.count(), 1);
    QCOMPARE(pending->clips.at(0).end, quint32(12000));
    QVERIFY(ls.show->takePendingSchedule().isNull());

    // nothing dirty: another turn of the loop rebuilds nothing
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 1);
}

void ShowRunner_Test::extendEndPastPlayhead()
{
    // Scene clip 0-10s already ended at 12s; dragging its end to 20s must
    // start the Scene again right now (offset 12s) and grow the Show's end.
    LiveShow ls(this, 0, 10000, 14000);
    ShowRunner runner(ls.doc, ls.show->id());
    QCOMPARE(runner.m_totalRunTime, quint32(14000));

    ls.advanceTo(runner, 12000);
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(ls.scene->isRunning() == false);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);

    ls.sf->setDuration(20000);
    ls.commitEdit();
    runner.write(ls.timer());

    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(runner.m_runningQueue.count(), 2);
    // Function::start() stores the offset as elapsed() synchronously
    QCOMPARE(ls.scene->elapsed(), quint32(12000));
    QCOMPARE(runner.m_totalRunTime, quint32(20000));

    // the MasterTimer picks the start up on its next tick; Scene::write
    // advances elapsed by one tick
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);
    QCOMPARE(ls.scene->elapsed(), quint32(12000 + MasterTimer::tick()));

    // the Show now lives on past the old end
    ls.advanceTo(runner, 15000);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QVERIFY(ls.scene->isRunning() == true);

    // and still stops at the new end
    ls.advanceTo(runner, 20000);
    runner.write(ls.timer());
    ls.timer()->timerTick();
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(ls.scene->isRunning() == false);
}

void ShowRunner_Test::shrinkEndBeforePlayhead()
{
    LiveShow ls(this, 0, 20000, 60000);
    ShowRunner runner(ls.doc, ls.show->id());

    ls.advanceTo(runner, 12000);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QVERIFY(ls.scene->isRunning() == true);

    ls.sf->setDuration(5000);
    ls.commitEdit();
    runner.write(ls.timer());

    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(ls.scene->stopped() == true);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == false);

    // the filler is untouched
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
    QVERIFY(ls.fillerScene->isRunning() == true);
}

void ShowRunner_Test::moveStartEarlierUnderPlayhead()
{
    // Scene clip 15-25s, playhead at 12s: moving the start to 5s puts the
    // playhead inside the clip, so it starts now with a 7s offset.
    LiveShow ls(this, 15000, 10000, 60000);
    ShowRunner runner(ls.doc, ls.show->id());

    ls.advanceTo(runner, 12000);
    QVERIFY(queueHas(runner, ls.sf->id()) == false);

    ls.sf->setStartTime(5000);
    ls.commitEdit();
    runner.write(ls.timer());

    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), quint32(7000));
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);

    // A Scene does not honour the offset (it just fades in), so moving the
    // start again while it plays must NOT restart it - no re-fade glitch -
    // only its bookkeeping changes.
    QSignalSpy runningSpy(ls.scene, SIGNAL(running(quint32)));
    ls.sf->setStartTime(8000);
    ls.commitEdit();
    runner.write(ls.timer());
    ls.timer()->timerTick();

    int index = runner.runningIndex(ls.sf->id());
    QVERIFY(index != -1);
    QCOMPARE(runner.m_runningQueue.at(index).start, quint32(8000));
    QCOMPARE(runner.m_runningQueue.at(index).stopTime, quint32(18000));
    QVERIFY(ls.scene->isRunning() == true);
    QVERIFY(ls.scene->stopped() == false);
    QCOMPARE(runningSpy.count(), 0);

    // an end-only change likewise just updates the stop time in place
    ls.sf->setDuration(30000);
    ls.commitEdit();
    runner.write(ls.timer());
    index = runner.runningIndex(ls.sf->id());
    QVERIFY(index != -1);
    QCOMPARE(runner.m_runningQueue.at(index).stopTime, quint32(38000));
    QCOMPARE(runningSpy.count(), 0);

    // it must not be started a second time by phase 1 either
    ls.advanceTo(runner, 20000);
    QCOMPARE(runningSpy.count(), 0);
    QVERIFY(ls.scene->isRunning() == true);
}

void ShowRunner_Test::moveStartRestartsOffsetSensitiveFunction()
{
    // A Chaser plays from the offset it is started with, so dragging its
    // clip's start while it runs must restart it at the new offset.
    LiveShow ls(this, 15000, 10000, 60000);

    Scene *step = ls.makeScene("step");
    Chaser *chaser = new Chaser(ls.doc);
    chaser->setDuration(20000);
    chaser->addStep(ChaserStep(step->id()));
    ls.doc->addFunction(chaser);
    ls.sf->setFunctionID(chaser->id());
    ls.commitEdit();

    ShowRunner runner(ls.doc, ls.show->id());
    ls.advanceTo(runner, 12000);
    QVERIFY(queueHas(runner, ls.sf->id()) == false);

    ls.sf->setStartTime(5000);
    ls.commitEdit();
    runner.write(ls.timer());               // playhead 12000 (+ one tick after)
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(chaser->elapsed(), quint32(7000));
    ls.timer()->timerTick();                // preRun + first write
    QVERIFY(chaser->isRunning() == true);

    QSignalSpy runningSpy(chaser, SIGNAL(running(quint32)));
    ls.sf->setStartTime(8000);
    ls.commitEdit();
    runner.write(ls.timer());

    // stopped this tick; the restart waits for the MasterTimer to complete
    // the stop (postRun would otherwise wipe the new offset)
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(chaser->stopped() == true);
    QVERIFY(runner.m_startPassPending == true);
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == false);

    quint32 playhead = runner.m_elapsedTime;
    runner.write(ls.timer());               // start pass at this playhead
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(chaser->elapsed(), playhead - 8000);
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == true);
    QCOMPARE(runningSpy.count(), 1);
}

void ShowRunner_Test::deleteRunningClipAndUndo()
{
    LiveShow ls(this, 0, 20000, 60000);
    ShowRunner runner(ls.doc, ls.show->id());

    ls.advanceTo(runner, 12000);
    QVERIFY(ls.scene->isRunning() == true);

    quint32 sfId = ls.sf->id();
    QVERIFY(ls.track->removeShowFunction(ls.sf, true) == true);   // deletes it
    ls.sf = NULL;
    ls.commitEdit();
    runner.write(ls.timer());                                       // no pointer to the dead clip is touched

    QVERIFY(queueHas(runner, sfId) == false);
    QVERIFY(ls.scene->stopped() == true);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == false);

    // Undo re-creates the clip with its original UID
    ShowFunction *restored = new ShowFunction(sfId);
    restored->setFunctionID(ls.scene->id());
    restored->setStartTime(0);
    restored->setDuration(20000);
    QVERIFY(ls.track->addShowFunction(restored) == true);
    ls.commitEdit();
    runner.write(ls.timer());

    QVERIFY(queueHas(runner, sfId) == true);
    QCOMPARE(ls.scene->elapsed(), runner.m_elapsedTime - MasterTimer::tick());
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);
}

void ShowRunner_Test::muteTrackStopsUnmuteResumes()
{
    LiveShow ls(this, 0, 20000, 60000);
    ShowRunner runner(ls.doc, ls.show->id());

    ls.advanceTo(runner, 12000);
    QVERIFY(ls.scene->isRunning() == true);

    ls.track->setMute(true);
    ls.commitEdit();
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == false);
    QVERIFY(ls.fillerScene->isRunning() == true);

    ls.track->setMute(false);
    ls.commitEdit();
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), runner.m_elapsedTime - MasterTimer::tick());
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);
}

void ShowRunner_Test::trackIntensityFollowsSchedule()
{
    // The snapshot carries the Show's per-track attribute values, so a
    // rebuilt schedule re-applies them to the running clips' overrides.
    LiveShow ls(this, 0, 20000, 60000);
    ShowRunner runner(ls.doc, ls.show->id());

    ls.advanceTo(runner, 1000);
    QVERIFY(ls.scene->isRunning() == true);
    QCOMPARE(runner.m_intensityMap[ls.track->id()], 1.0);
    QCOMPARE(ls.scene->getAttributeValue(Function::Intensity), 1.0);

    int trackAttr = ls.show->tracks().indexOf(ls.track);
    QVERIFY(trackAttr >= 0);
    ls.show->adjustAttribute(0.5, trackAttr);
    // a runner created later (Virtual Console start) must seed from this value
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();
    runner.write(ls.timer());

    QCOMPARE(runner.m_intensityMap[ls.track->id()], 0.5);
    QCOMPARE(ls.scene->getAttributeValue(Function::Intensity), 0.5);
    QCOMPARE(ls.fillerScene->getAttributeValue(Function::Intensity), 1.0);
}

void ShowRunner_Test::addClipAtPlayhead()
{
    LiveShow ls(this, 30000, 10000, 60000);
    ShowRunner runner(ls.doc, ls.show->id());

    ls.advanceTo(runner, 12000);
    QCOMPARE(runner.m_runningQueue.count(), 1);

    Scene *pasted = ls.makeScene("pasted");
    ShowFunction *sf = ls.track->createShowFunction(pasted->id());
    sf->setStartTime(11000);
    sf->setDuration(5000);
    ls.commitEdit();
    runner.write(ls.timer());

    QVERIFY(queueHas(runner, sf->id()) == true);
    QCOMPARE(pasted->elapsed(), quint32(1000));
    ls.timer()->timerTick();
    QVERIFY(pasted->isRunning() == true);

    // phase 1 continues after the playhead: the original 30s clip still starts
    ls.advanceTo(runner, 30000);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QVERIFY(queueHas(runner, sf->id()) == false);
}

void ShowRunner_Test::totalRunTimeShrinkEndsShow()
{
    LiveShow ls(this, 0, 10000);
    ShowRunner runner(ls.doc, ls.show->id());
    QSignalSpy finished(&runner, SIGNAL(showFinished()));

    ls.advanceTo(runner, 5000);
    QVERIFY(ls.scene->isRunning() == true);
    QCOMPARE(finished.count(), 0);

    ls.sf->setDuration(3000);
    ls.commitEdit();
    runner.write(ls.timer());

    QCOMPARE(runner.m_totalRunTime, quint32(3000));
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(ls.scene->stopped() == true);
    QCOMPARE(finished.count(), 1);
    // the show ends on this very tick: the playhead does not advance
    QCOMPARE(runner.m_elapsedTime, quint32(5000));
}

void ShowRunner_Test::pausedAppliesStopsDefersStarts()
{
    LiveShow ls(this, 0, 20000, 60000);
    ShowRunner runner(ls.doc, ls.show->id());

    ls.advanceTo(runner, 12000);
    QVERIFY(ls.scene->isRunning() == true);

    // Show::write() calls applyPendingSchedule() instead of write() while paused
    runner.setPause(true);
    QVERIFY(ls.scene->isPaused() == true);

    ls.sf->setDuration(5000);
    Scene *later = ls.makeScene("later");
    ShowFunction *sfLater = ls.track->createShowFunction(later->id());
    sfLater->setStartTime(10000);
    sfLater->setDuration(10000);
    ls.commitEdit();
    runner.applyPendingSchedule();

    // the shortened clip stops right away...
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(ls.scene->stopped() == true);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == false);

    // ...the new one under the playhead waits for playback to resume
    QVERIFY(queueHas(runner, sfLater->id()) == false);
    QVERIFY(runner.m_startPassPending == true);
    QVERIFY(later->isRunning() == false);
    QCOMPARE(runner.m_elapsedTime, quint32(12000));

    runner.setPause(false);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, sfLater->id()) == true);
    QCOMPARE(later->elapsed(), quint32(2000));
    ls.timer()->timerTick();
    QVERIFY(later->isRunning() == true);
    QVERIFY(later->isPaused() == false);
}

void ShowRunner_Test::beatClipReschedule()
{
    // Live edits in a Beats Show apply on the wall clock like in a Time Show:
    // Beats-tempo Scene clip 3000-5000ms, released by one beat pulse, then
    // shrunk under the playhead and extended back over it.
    LiveShow ls(this, 3000, 2000, 60000);
    ls.scene->setTempoType(Function::Beats);
    ls.makeBeatsShow();

    ShowRunner runner(ls.doc, ls.show->id());
    QVERIFY(runner.m_schedule->showTempo == Function::Beats);
    ls.pulseBeat(runner);

    ls.advanceTo(runner, 3500);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);

    // shrink to 3000-3400: gone from under the playhead, stopped at once
    ls.sf->setDuration(400);
    ls.commitEdit();
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == false);

    // extend to 3000-7000: under the playhead again, restarts at the offset
    // the wall clock has reached (no beat needed for any of this)
    ls.advanceTo(runner, 4000);
    ls.sf->setDuration(4000);
    ls.commitEdit();
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), quint32(1000));
    QCOMPARE(runner.m_totalRunTime, quint32(60000));

    // and phase 2 still stops it at the new end
    ls.advanceTo(runner, 7000);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
}

/****************************************************************************
 * Which clock a clip runs on is the Show's tempo, not the Function's
 ****************************************************************************/

/**
 * A Beats-tempo Chaser (two Scene steps, looping, so it keeps running for as
 * long as the runner lets it) added to $ls on its own track.
 */
static ShowFunction *addBeatsChaserClip(LiveShow &ls, Chaser *&chaser, quint32 start, quint32 duration)
{
    chaser = new Chaser(ls.doc);
    chaser->setName("beats chaser");
    chaser->setTempoType(Function::Beats);
    chaser->addStep(ChaserStep(ls.makeScene("step 1")->id()));
    chaser->addStep(ChaserStep(ls.makeScene("step 2")->id()));
    ls.doc->addFunction(chaser);

    Track *track = new Track(chaser->id(), ls.show);
    ls.show->addTrack(track);
    ShowFunction *sf = track->createShowFunction(chaser->id());
    sf->setStartTime(start);
    sf->setDuration(duration);
    ls.commitEdit();
    return sf;
}

void ShowRunner_Test::beatsFunctionInTimeShowRunsOnRealTime()
{
    // Regression: a Beats-tempo Function placed in a Time Show never started,
    // because the runner used to pick the clock from the Function's tempo and
    // only advanced the beat clock in a Beats Show. The Show's timeline is
    // real time here, so the clip has to come and go with m_elapsedTime -
    // the Chaser only steps on the beat internally.
    LiveShow ls(this, 0, 20000);
    Chaser *chaser = NULL;
    ShowFunction *sf = addBeatsChaserClip(ls, chaser, 3000, 2000);

    ShowRunner runner(ls.doc, ls.show->id());
    QVERIFY(runner.m_schedule->showTempo == Function::Time);
    QCOMPARE(runner.m_schedule->clips.count(), 2);
    QCOMPARE(runner.m_totalRunTime, quint32(20000));

    // the runner never sees a beat pulse: it must not matter
    ls.advanceTo(runner, 3000);
    QVERIFY(queueHas(runner, sf->id()) == false);
    QVERIFY(chaser->isRunning() == false);

    runner.write(ls.timer());
    QVERIFY(queueHas(runner, sf->id()) == true);
    QCOMPARE(chaser->elapsed(), quint32(0));
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == true);

    // advanceTo() leaves the playhead at 5000ms before the tick that runs there
    ls.advanceTo(runner, 5000);
    QVERIFY(queueHas(runner, sf->id()) == true);
    QVERIFY(chaser->isRunning() == true);

    // start + duration = 5000ms: stopped on the real clock, like any clip
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, sf->id()) == false);
    QVERIFY(chaser->stopped() == true);
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == false);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
}

void ShowRunner_Test::beatsFunctionInTimeShowReschedule()
{
    // Same clip, ended at 5000ms; at 7000ms its end is dragged to 12000ms:
    // it restarts under the playhead at the right offset and stops at the
    // new end, on the real clock.
    LiveShow ls(this, 0, 20000);
    Chaser *chaser = NULL;
    ShowFunction *sf = addBeatsChaserClip(ls, chaser, 3000, 2000);
    ShowRunner runner(ls.doc, ls.show->id());

    ls.advanceTo(runner, 7000);
    QVERIFY(queueHas(runner, sf->id()) == false);
    QVERIFY(chaser->isRunning() == false);

    sf->setDuration(9000);
    ls.commitEdit();
    runner.write(ls.timer());

    QVERIFY(queueHas(runner, sf->id()) == true);
    QCOMPARE(chaser->elapsed(), quint32(4000));
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == true);

    ls.advanceTo(runner, 12000);
    QVERIFY(queueHas(runner, sf->id()) == true);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, sf->id()) == false);
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == false);
}

void ShowRunner_Test::timeFunctionInBeatsShowRunsOnWallClock()
{
    // A Time-tempo Scene clip 500-1500ms in a Beats Show: once the Show has
    // been released by its first beat, the clip starts and stops as the
    // wall clock passes its bounds, with no further beat pulse at all.
    LiveShow ls(this, 500, 1000, 60000);
    ls.makeBeatsShow();
    QVERIFY(ls.scene->tempoType() == Function::Time);

    ShowRunner runner(ls.doc, ls.show->id());
    QVERIFY(runner.m_schedule->showTempo == Function::Beats);

    ls.pulseBeat(runner);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
    QVERIFY(queueHas(runner, ls.sf->id()) == false);

    ls.advanceTo(runner, 500);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), quint32(0));
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);

    ls.advanceTo(runner, 1500);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == false);
}

void ShowRunner_Test::beatsShowStartedMidTimeline()
{
    // Playing a Beats Show from the cursor: the playhead starts from the
    // Show's start position and every clip is judged against it. Clip
    // 3000-5000ms, runner started at 2500ms.
    LiveShow ls(this, 3000, 2000, 60000);
    ls.makeBeatsShow();

    ShowRunner runner(ls.doc, ls.show->id(), 2500);
    QCOMPARE(runner.m_elapsedTime, quint32(2500));

    ls.pulseBeat(runner);
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
    QCOMPARE(ls.fillerScene->elapsed(), quint32(2500));

    ls.advanceTo(runner, 3000);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), quint32(0));

    ls.advanceTo(runner, 5000);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
}

void ShowRunner_Test::showTempoSwitchWhilePlaying()
{
    // Switching the Show from Time to Beats while it plays must not hold the
    // playhead for a beat: the running clip keeps running, the playhead keeps
    // moving, and the clip still stops on the wall clock - no beat pulse is
    // ever needed.
    LiveShow ls(this, 0, 10000, 60000);
    ls.doc->inputOutputMap()->setBeatGeneratorType(InputOutputMap::Internal);
    ShowRunner runner(ls.doc, ls.show->id());
    QVERIFY(runner.m_waitingForBeat == false);

    ls.advanceTo(runner, 2000);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QVERIFY(ls.scene->isRunning() == true);

    ls.show->setTempoType(Function::Beats);
    QVERIFY(ls.show->isScheduleDirty() == true);
    ls.commitEdit();

    runner.write(ls.timer());
    QVERIFY(runner.m_schedule->showTempo == Function::Beats);
    QVERIFY(runner.m_waitingForBeat == false);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(runner.m_elapsedTime, quint32(2000 + MasterTimer::tick()));
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);

    ls.advanceTo(runner, 10000);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
}

/****************************************************************************
 * Scrub preview: the runner frozen at the playhead, moved by seeks
 ****************************************************************************/

void ShowRunner_Test::scrubStartsAndFreezes()
{
    // Runner started frozen at 12s over a 0-20s Scene clip: the clip is
    // started at its offset with no fade-in, held paused two ticks later,
    // and the playhead never moves on its own.
    LiveShow ls(this, 0, 20000, 60000);
    ls.show->setScrubMode(true);
    QVERIFY(ls.show->isScrubMode() == true);

    ShowRunner runner(ls.doc, ls.show->id(), 12000);
    QSignalSpy timeSpy(&runner, SIGNAL(timeChanged(quint32)));

    runner.write(ls.timer());
    QVERIFY(runner.m_frozen == true);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), quint32(12000));
    QCOMPARE(ls.scene->overrideFadeInSpeed(), uint(0));
    QCOMPARE(runner.m_elapsedTime, quint32(12000));
    ls.timer()->timerTick();                // preRun + first write
    QVERIFY(ls.scene->isRunning() == true);
    QVERIFY(ls.scene->isPaused() == false);

    // one more tick to let the faders land, then it is held
    runner.write(ls.timer());
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isPaused() == false);
    runner.write(ls.timer());
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isPaused() == true);
    QVERIFY(ls.fillerScene->isPaused() == true);

    ls.frozenTicks(runner, 10);
    QVERIFY(ls.scene->isRunning() == true);
    QVERIFY(ls.scene->isPaused() == true);
    QCOMPARE(runner.m_elapsedTime, quint32(12000));
    QCOMPARE(timeSpy.count(), 0);
}

void ShowRunner_Test::scrubSeekStopsAndStarts()
{
    // Clip A 0-10s, clip B 12-20s, filler 0-60s. Frozen at 5s then seeked
    // to 15s: A stops, B starts at offset 3s (no fade-in) and is held, the
    // filler spanning both positions keeps holding.
    LiveShow ls(this, 0, 10000, 60000);
    Scene *later = ls.makeScene("later");
    ShowFunction *sfLater = ls.track->createShowFunction(later->id());
    sfLater->setStartTime(12000);
    sfLater->setDuration(8000);
    ls.commitEdit();

    ls.show->setScrubMode(true);
    ShowRunner runner(ls.doc, ls.show->id(), 5000);
    ls.frozenTicks(runner, 3);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QVERIFY(ls.scene->isPaused() == true);
    QVERIFY(queueHas(runner, sfLater->id()) == false);

    ls.show->requestSeek(15000);
    runner.write(ls.timer());
    QCOMPARE(runner.m_elapsedTime, quint32(15000));
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(ls.scene->stopped() == true);
    QVERIFY(queueHas(runner, sfLater->id()) == true);
    QCOMPARE(later->elapsed(), quint32(3000));
    QCOMPARE(later->overrideFadeInSpeed(), uint(0));
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
    QVERIFY(ls.fillerScene->isPaused() == true);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == false);
    QVERIFY(later->isRunning() == true);

    // the request was consumed: nothing replays it, and B is held in turn
    ls.frozenTicks(runner, 2);
    QVERIFY(later->isPaused() == true);
    QCOMPARE(runner.m_elapsedTime, quint32(15000));
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
}

void ShowRunner_Test::scrubSeekRestartsChaserAtOffset()
{
    // A Chaser plays from the offset it is started with, so a seek restarts
    // it at the new one (still without fade-in) instead of holding it.
    LiveShow ls(this, 0, 20000, 60000);
    Scene *step = ls.makeScene("step");
    Chaser *chaser = new Chaser(ls.doc);
    chaser->setDuration(20000);
    chaser->addStep(ChaserStep(step->id()));
    ls.doc->addFunction(chaser);
    ls.sf->setFunctionID(chaser->id());
    ls.commitEdit();

    ls.show->setScrubMode(true);
    ShowRunner runner(ls.doc, ls.show->id(), 5000);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(chaser->elapsed(), quint32(5000));
    QCOMPARE(chaser->overrideFadeInSpeed(), uint(0));
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == true);
    ls.frozenTicks(runner, 2);
    QVERIFY(chaser->isPaused() == true);

    ls.show->requestSeek(8000);
    runner.write(ls.timer());
    // stopped this tick; the restart waits for the MasterTimer to complete
    // the stop (postRun would otherwise wipe the new offset)
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(chaser->stopped() == true);
    QVERIFY(runner.m_startPassPending == true);
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == false);

    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(chaser->elapsed(), quint32(8000));
    QCOMPARE(chaser->overrideFadeInSpeed(), uint(0));
    QCOMPARE(runner.m_elapsedTime, quint32(8000));
    ls.timer()->timerTick();
    QVERIFY(chaser->isRunning() == true);
    ls.frozenTicks(runner, 2);
    QVERIFY(chaser->isPaused() == true);
}

void ShowRunner_Test::scrubSkipsAudioUntilUnfreeze()
{
    // Scrubbing is silent: an Audio clip under the playhead is neither
    // started nor queued while frozen (a seek does not start it either),
    // and unfreezing starts it at the playhead's offset with its own fade.
    LiveShow ls(this, 0, 20000, 60000);
    Audio *audio = new Audio(ls.doc);   // no source: preRun's decoder guard
    audio->setName("audio");
    ls.doc->addFunction(audio);
    ls.sf->setFunctionID(audio->id());
    ls.commitEdit();

    ls.show->setScrubMode(true);
    ShowRunner runner(ls.doc, ls.show->id(), 5000);
    ls.frozenTicks(runner, 3);
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(audio->isRunning() == false);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);

    ls.show->requestSeek(7000);
    ls.frozenTicks(runner, 3);
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(audio->isRunning() == false);

    ls.show->setScrubMode(false);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(audio->elapsed(), quint32(7000));
    QCOMPARE(audio->overrideFadeInSpeed(), Function::defaultSpeed());
    ls.timer()->timerTick();
    QVERIFY(audio->isRunning() == true);
    QVERIFY(audio->isPaused() == false);
}

void ShowRunner_Test::scrubPastEndNoShowFinished()
{
    // A cursor past the last clip shows "nothing": every clip stops, but
    // the Show does not end - and a seek back under a clip starts it again.
    LiveShow ls(this, 0, 10000);
    ls.show->setScrubMode(true);
    ShowRunner runner(ls.doc, ls.show->id(), 5000);
    QSignalSpy finished(&runner, SIGNAL(showFinished()));
    ls.frozenTicks(runner, 3);
    QVERIFY(ls.scene->isPaused() == true);

    ls.show->requestSeek(15000);
    ls.frozenTicks(runner, 5);
    QCOMPARE(runner.m_elapsedTime, quint32(15000));
    QCOMPARE(runner.m_runningQueue.count(), 0);
    QVERIFY(ls.scene->isRunning() == false);
    QCOMPARE(finished.count(), 0);

    ls.show->requestSeek(2000);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), quint32(2000));
    QCOMPARE(finished.count(), 0);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);
}

void ShowRunner_Test::scrubHonoursMute()
{
    // Timeline edits still apply while frozen: muting the track stops its
    // held clip at once, unmuting starts it again at the offset and holds it.
    LiveShow ls(this, 0, 20000, 60000);
    ls.show->setScrubMode(true);
    ShowRunner runner(ls.doc, ls.show->id(), 5000);
    ls.frozenTicks(runner, 3);
    QVERIFY(ls.scene->isPaused() == true);

    ls.track->setMute(true);
    ls.commitEdit();
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(ls.scene->stopped() == true);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == false);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
    QCOMPARE(runner.m_elapsedTime, quint32(5000));

    ls.track->setMute(false);
    ls.commitEdit();
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    QCOMPARE(ls.scene->elapsed(), quint32(5000));
    QCOMPARE(ls.scene->overrideFadeInSpeed(), uint(0));
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);
    ls.frozenTicks(runner, 2);
    QVERIFY(ls.scene->isPaused() == true);
}

void ShowRunner_Test::unfreezeResumesAndResetsFadeIn()
{
    // Leaving scrub mode un-pauses the held clips, gives them back their
    // own fade-in (a Chaser reads the override on every step) and moves
    // the playhead on from where the scrub left it - seamlessly.
    LiveShow ls(this, 0, 20000, 60000);
    ls.show->setScrubMode(true);
    ShowRunner runner(ls.doc, ls.show->id(), 5000);
    QSignalSpy timeSpy(&runner, SIGNAL(timeChanged(quint32)));
    ls.frozenTicks(runner, 3);
    QVERIFY(ls.scene->isPaused() == true);
    QCOMPARE(ls.scene->overrideFadeInSpeed(), uint(0));
    QCOMPARE(timeSpy.count(), 0);

    ls.show->setScrubMode(false);
    runner.write(ls.timer());
    QVERIFY(runner.m_frozen == false);
    QVERIFY(ls.scene->isPaused() == false);
    QVERIFY(ls.fillerScene->isPaused() == false);
    QCOMPARE(ls.scene->overrideFadeInSpeed(), Function::defaultSpeed());
    QCOMPARE(runner.m_elapsedTime, quint32(5000 + MasterTimer::tick()));
    QCOMPARE(timeSpy.count(), 1);
    ls.timer()->timerTick();
    QVERIFY(ls.scene->isRunning() == true);

    // and playback goes on: the clip still ends at its end
    ls.advanceTo(runner, 20000);
    QVERIFY(queueHas(runner, ls.sf->id()) == true);
    runner.write(ls.timer());
    QVERIFY(queueHas(runner, ls.sf->id()) == false);
    QVERIFY(queueHas(runner, ls.fillerSf->id()) == true);
}

void ShowRunner_Test::showStopClearsScrubMode()
{
    // Scrub mode belongs to the run that asked for it: once the Show has
    // stopped, a later start (from the Virtual Console, say) must play.
    LiveShow ls(this, 0, 20000, 60000);
    FunctionParent parent = FunctionParent::master(FunctionParent::ShowManagerPlayback);

    ls.show->setScrubMode(true);
    ls.show->requestSeek(3000);
    ls.show->start(ls.timer(), parent, 5000);
    ls.timer()->timerTick();                // preRun creates the runner
    QVERIFY(ls.show->isRunning() == true);
    QVERIFY(ls.show->isScrubMode() == true);

    ls.show->stop(parent);
    ls.timer()->timerTick();                // postRun
    QVERIFY(ls.show->isRunning() == false);
    QVERIFY(ls.show->isScrubMode() == false);
    quint32 ms = 0;
    QVERIFY(ls.show->takeSeekRequest(ms) == false);
}

// Guiless rather than appless: Chaser::createRunner() moves its runner to
// QCoreApplication::instance()->thread(), which needs a live application.
QTEST_GUILESS_MAIN(ShowRunner_Test)
