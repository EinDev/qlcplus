/*
  Q Light Controller
  mastertimer_test.cpp

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
#include <QElapsedTimer>

#define private public
#include "mastertimer_test.h"
#include "dmxsource_stub.h"
#include "function_stub.h"
#include "mastertimer.h"
#include "qlcchannel.h"
#include "universe.h"
#include "qlcfile.h"
#include "doc.h"
#undef private

#include "../common/resource_paths.h"

namespace {

/** Guarantees every function/DMX source a test registered with $mt is fully
 *  stopped/unregistered before this guard - and therefore any locally
 *  stack-allocated Function_Stub/DMXSource_Stub the test declared earlier -
 *  goes out of scope, even if an assertion elsewhere in the test body
 *  returns the function early. Without this, a QVERIFY/QTRY_VERIFY that
 *  fails *before* the test's own manual cleanup code runs destroys those
 *  stack objects while MasterTimer's background timer thread may still hold
 *  pointers to them - a cross-thread use-after-free that intermittently
 *  segfaulted this test suite's cleanupTestCase() (via ~Doc() -> ~MasterTimer()
 *  -> stop() walking a function list containing already-destroyed stubs)
 *  whenever a timing-sensitive assertion happened to fail under CI load.
 *
 *  Declare this *after* every stub it must outlive - C++ destroys locals in
 *  reverse declaration order, so this needs to run its cleanup before those
 *  stubs' own destructors do. */
class MasterTimerCleanup
{
public:
    explicit MasterTimerCleanup(MasterTimer *mt) : m_mt(mt) {}

    ~MasterTimerCleanup()
    {
        m_mt->stopAllFunctions();
        for (DMXSource *src : m_dmxSources)
            m_mt->unregisterDMXSource(src);
    }

    void trackDMXSource(DMXSource *src) { m_dmxSources.append(src); }

private:
    MasterTimer *m_mt;
    QList<DMXSource *> m_dmxSources;
};

} // namespace

void MasterTimer_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(m_doc->fixtureDefCache()->loadMap(dir) == true);
}

void MasterTimer_Test::cleanupTestCase()
{
    delete m_doc;
}

void MasterTimer_Test::init()
{
}

void MasterTimer_Test::cleanup()
{
    m_doc->clearContents();
}

void MasterTimer_Test::initial()
{
    MasterTimer* mt = m_doc->masterTimer();

    QVERIFY(mt->runningFunctions() == 0);
    QVERIFY(mt->m_functionList.size() == 0);
    QVERIFY(mt->m_functionListMutex.tryLock() == true);
    mt->m_functionListMutex.unlock();

    QVERIFY(mt->m_dmxSourceList.size() == 0);
    QVERIFY(mt->m_dmxSourceListMutex.tryLock() == true);
    mt->m_dmxSourceListMutex.unlock();

    //QVERIFY(mt->m_running == false);
    QVERIFY(mt->m_stopAllFunctions == false);
}

void MasterTimer_Test::startStop()
{
    MasterTimer* mt = m_doc->masterTimer();

    mt->start();
    QTest::qWait(100);

    QVERIFY(mt->runningFunctions() == 0);
    QVERIFY(mt->m_functionList.size() == 0);
    QVERIFY(mt->m_dmxSourceList.size() == 0);
    // QVERIFY(mt->m_running == true);
    QVERIFY(mt->m_stopAllFunctions == false);

    mt->stop();
    QTest::qWait(100);

    QVERIFY(mt->runningFunctions() == 0);
    QVERIFY(mt->m_functionList.size() == 0);
    QVERIFY(mt->m_dmxSourceList.size() == 0);
    // QVERIFY(mt->m_running == false);
    QVERIFY(mt->m_stopAllFunctions == false);
}

void MasterTimer_Test::startStopFunction()
{
    MasterTimer* mt = m_doc->masterTimer();
    mt->start();

    Function_Stub fs(m_doc);

    // See MasterTimerCleanup's doc comment - must outlive fs.
    MasterTimerCleanup cleanup(mt);

    QVERIFY(mt->runningFunctions() == 0);

    mt->startFunction(NULL);
    QVERIFY(mt->runningFunctions() == 0);

    mt->startFunction(&fs);
    mt->timerTick();
    QVERIFY(mt->runningFunctions() == 1);

    mt->startFunction(&fs);
    QVERIFY(mt->runningFunctions() == 1);

    QTest::qWait(100);
    fs.stop(FunctionParent::master());

    // fs.stop() only requests a stop - MasterTimer's background timer thread
    // has to actually process it before runningFunctions() reflects it, and a
    // fixed wait isn't always enough under CI load. See stopAllFunctions()'s
    // equivalent comment on QTRY_VERIFY.
    QTRY_VERIFY(mt->runningFunctions() == 0);
}

void MasterTimer_Test::registerUnregisterDMXSource()
{
    MasterTimer* mt = m_doc->masterTimer();
    QVERIFY(mt->m_dmxSourceList.size() == 0);

    DMXSource_Stub s1;
    /* Normal registration */
    mt->registerDMXSource(&s1);
    QVERIFY(mt->m_dmxSourceList.size() == 1);
    QVERIFY(mt->m_dmxSourceList.at(0) == &s1);

    /* No double additions */
    mt->registerDMXSource(&s1);
    QVERIFY(mt->m_dmxSourceList.size() == 1);
    QVERIFY(mt->m_dmxSourceList.at(0) == &s1);

    DMXSource_Stub s2;
    /* Normal registration of another source */
    mt->registerDMXSource(&s2);
    QVERIFY(mt->m_dmxSourceList.size() == 2);
    QVERIFY(mt->m_dmxSourceList.at(0) == &s1);
    QVERIFY(mt->m_dmxSourceList.at(1) == &s2);

    /* No double additions */
    mt->registerDMXSource(&s2);
    QVERIFY(mt->m_dmxSourceList.size() == 2);
    QVERIFY(mt->m_dmxSourceList.at(0) == &s1);
    QVERIFY(mt->m_dmxSourceList.at(1) == &s2);

    /* No double additions */
    mt->registerDMXSource(&s1);
    QVERIFY(mt->m_dmxSourceList.size() == 2);
    QVERIFY(mt->m_dmxSourceList.at(0) == &s1);
    QVERIFY(mt->m_dmxSourceList.at(1) == &s2);

    /* Removal of a source */
    mt->unregisterDMXSource(&s1);
    QVERIFY(mt->m_dmxSourceList.size() == 1);
    QVERIFY(mt->m_dmxSourceList.at(0) == &s2);

    /* No double removals */
    mt->unregisterDMXSource(&s1);
    QVERIFY(mt->m_dmxSourceList.size() == 1);
    QVERIFY(mt->m_dmxSourceList.at(0) == &s2);

    /* Removal of the last source */
    mt->unregisterDMXSource(&s2);
    QVERIFY(mt->m_dmxSourceList.size() == 0);
}

void MasterTimer_Test::interval()
{
    MasterTimer* mt = m_doc->masterTimer();
    Function_Stub fs(m_doc);
    DMXSource_Stub dss;

    mt->start();
    QTest::qWait(100);

    fs.start(mt, FunctionParent::master());
    mt->timerTick();
    QVERIFY(mt->runningFunctions() == 1);

    mt->registerDMXSource(&dss);
    QVERIFY(mt->m_dmxSourceList.size() == 1);

    /* Wait for approximately one second - but measure how long it actually
     * took, since QTest::qWait() itself can run long under CI load. Basing
     * the expected tick count below on the actual elapsed time (rather than
     * assuming the requested 1000ms was exact) removes one whole source of
     * flakiness outright, on top of the jitter tolerance further down. */
    QElapsedTimer waitTimer;
    waitTimer.start();
    QTest::qWait(1000);
    qint64 elapsedMs = waitTimer.elapsed();

    /* Snapshot the write counts and fully stop/unregister the stubs *before*
     * asserting on those counts below. fs/dss are stack objects that are still
     * registered with MasterTimer's background timer thread at this point; if
     * a QVERIFY on the snapshotted values below were to fail with fs/dss still
     * registered, QVERIFY's early return would destroy fs/dss on the way out
     * of this function while that timer thread could still be calling
     * write()/writeDMX() on them - a cross-thread use-after-free that
     * intermittently crashed this test (SIGSEGV on Windows, SIGBUS on macOS)
     * whenever the timing-sensitive write-count assertion below happened to
     * fail. */
    int fsWriteCalls = fs.m_writeCalls;
    int dssWriteCalls = dss.m_writeCalls;

    fs.stop(FunctionParent::master());
    QTest::qWait(1000);
    mt->unregisterDMXSource(&dss);

#ifndef SKIP_TEST
    /* Expect one tick per MasterTimer::tick() ms actually elapsed (not the
       requested 1000ms). fs was registered via the manual timerTick() call
       above, one cycle before dss, so its window sits one write above dss's.
       jitterTolerance absorbs the timer thread occasionally losing/gaining a
       tick or two to scheduler contention on a loaded CI runner, on top of
       the inherent +/-1 uncertainty over which exact cycle a given context
       switch lands on (that part isn't new - it's the same estimate this
       test always made, just no longer additionally penalized for qWait()
       itself running long). */
    int expectedTicks = int(elapsedMs / MasterTimer::tick());
    const int jitterTolerance = 5;
    QVERIFY(fsWriteCalls >= expectedTicks + 1 - jitterTolerance &&
            fsWriteCalls <= expectedTicks + 1 + jitterTolerance);
    QVERIFY(dssWriteCalls >= expectedTicks - jitterTolerance &&
            dssWriteCalls <= expectedTicks + jitterTolerance);
#endif

    QVERIFY(mt->runningFunctions() == 0);
    QVERIFY(mt->m_dmxSourceList.size() == 0);
}

void MasterTimer_Test::functionInitiatedStop()
{
    MasterTimer* mt = m_doc->masterTimer();
    Function_Stub fs(m_doc);

    // See MasterTimerCleanup's doc comment - must outlive fs.
    MasterTimerCleanup cleanup(mt);

    mt->start();

    fs.start(mt, FunctionParent::master());
    mt->timerTick();
    QVERIFY(mt->runningFunctions() == 1);

    /* Wait a while so that the function starts running */
    QTest::qWait(100);

    /* Stop the function after it has been running for a while */
    fs.stop(FunctionParent::master());

    /* fs.stop() only requests a stop - the background timer thread has to
       actually process it before runningFunctions() reflects it, and a fixed
       wait isn't always enough under CI load. See stopAllFunctions()'s
       equivalent comment on QTRY_VERIFY. */
    QTRY_VERIFY(mt->runningFunctions() == 0);
    QVERIFY(fs.m_preRunCalls == 1);
    QVERIFY(fs.m_writeCalls > 0);
    QVERIFY(fs.m_postRunCalls == 1);
}

void MasterTimer_Test::runMultipleFunctions()
{
    MasterTimer* mt = m_doc->masterTimer();
    mt->start();

    Function_Stub fs1(m_doc);
    fs1.start(mt, FunctionParent::master());
    mt->timerTick();
    QVERIFY(mt->runningFunctions() == 1);

    Function_Stub fs2(m_doc);
    fs2.start(mt, FunctionParent::master());
    mt->timerTick();
    QVERIFY(mt->runningFunctions() == 2);

    Function_Stub fs3(m_doc);
    fs3.start(mt, FunctionParent::master());
    mt->timerTick();
    QVERIFY(mt->runningFunctions() == 3);

    // See MasterTimerCleanup's doc comment - must outlive fs1/fs2/fs3.
    MasterTimerCleanup cleanup(mt);

    /* Wait a while so that the functions start running */
    QTest::qWait(100);

    /* Stop the functions after they have been running for a while */
    fs1.stop(FunctionParent::master());
    fs2.stop(FunctionParent::master());
    fs3.stop(FunctionParent::master());

    /* See stopAllFunctions()'s equivalent comment on why this is
       QTRY_VERIFY rather than a fixed wait + immediate assert. */
    QTRY_VERIFY(mt->runningFunctions() == 0);
}

void MasterTimer_Test::stopAllFunctions()
{
    MasterTimer* mt = m_doc->masterTimer();
    mt->start();

    Function_Stub fs1(m_doc);
    fs1.start(mt, FunctionParent::master());

    DMXSource_Stub s1;
    mt->registerDMXSource(&s1);

    Function_Stub fs2(m_doc);
    fs2.start(mt, FunctionParent::master());

    DMXSource_Stub s2;
    mt->registerDMXSource(&s2);

    Function_Stub fs3(m_doc);
    fs3.start(mt, FunctionParent::master());

    // Declared last (destroyed first) so a QTRY_VERIFY failure below still
    // fully stops/unregisters everything above before fs1/fs2/fs3/s1/s2 are
    // destroyed - see MasterTimerCleanup's own doc comment for why this
    // ordering matters (a prior version of this test crashed cleanupTestCase()
    // this way whenever the timing-sensitive QVERIFY below happened to fail).
    MasterTimerCleanup cleanup(mt);
    cleanup.trackDMXSource(&s1);
    cleanup.trackDMXSource(&s2);

    // Starting 3 functions only queues them - MasterTimer's background timer
    // thread (20ms tick) has to actually run before runningFunctions() sees
    // them, and a fixed QTest::qWait() isn't always long enough for that
    // thread to get scheduled under CI load. QTRY_VERIFY polls up to 5s
    // instead of asserting after one fixed sleep - same correctness bar
    // (must still become true), no more flaky failures from scheduler jitter.
    QTRY_VERIFY(mt->runningFunctions() == 3);
    QVERIFY(mt->m_dmxSourceList.size() == 2);

    mt->stopAllFunctions();
    QVERIFY(mt->runningFunctions() == 0);
    QVERIFY(mt->m_dmxSourceList.size() == 2); // Shouldn't stop
}

void MasterTimer_Test::stop()
{
    MasterTimer* mt = m_doc->masterTimer();
    mt->start();

    Function_Stub fs1(m_doc);
    fs1.start(mt, FunctionParent::master());

    Function_Stub fs2(m_doc);
    fs2.start(mt, FunctionParent::master());

    Function_Stub fs3(m_doc);
    fs3.start(mt, FunctionParent::master());

    // See stopAllFunctions()'s equivalent comment - must outlive fs1/fs2/fs3.
    MasterTimerCleanup cleanup(mt);

    // See stopAllFunctions()'s equivalent comment on why this is QTRY_VERIFY.
    QTRY_VERIFY(mt->runningFunctions() == 3);

    mt->stop();
    QVERIFY(mt->runningFunctions() == 0);
    // QVERIFY(mt->m_running == false);
}

void MasterTimer_Test::restart()
{
    MasterTimer* mt = m_doc->masterTimer();
    mt->start();

    Function_Stub fs1(m_doc);
    fs1.start(mt, FunctionParent::master());

    Function_Stub fs2(m_doc);
    fs2.start(mt, FunctionParent::master());

    Function_Stub fs3(m_doc);
    fs3.start(mt, FunctionParent::master());

    // See stopAllFunctions()'s equivalent comment - must outlive fs1/fs2/fs3.
    MasterTimerCleanup cleanup(mt);

    // See stopAllFunctions()'s equivalent comment on why this is QTRY_VERIFY.
    QTRY_VERIFY(mt->runningFunctions() == 3);

    mt->stop();
    QVERIFY(mt->runningFunctions() == 0);
    QVERIFY(mt->m_functionList.size() == 0);
    QVERIFY(mt->m_functionListMutex.tryLock() == true);
    mt->m_functionListMutex.unlock();
    // QVERIFY(mt->m_running == false);
    QVERIFY(mt->m_stopAllFunctions == false);

    mt->start();
    QVERIFY(mt->runningFunctions() == 0);
    QVERIFY(mt->m_functionList.size() == 0);
    QVERIFY(mt->m_functionListMutex.tryLock() == true);
    mt->m_functionListMutex.unlock();
    // QVERIFY(mt->m_running == true);
    QVERIFY(mt->m_stopAllFunctions == false);

    fs1.start(mt, FunctionParent::master());
    fs2.start(mt, FunctionParent::master());
    fs3.start(mt, FunctionParent::master());
    QTRY_VERIFY(mt->runningFunctions() == 3);
}

QTEST_MAIN(MasterTimer_Test)
