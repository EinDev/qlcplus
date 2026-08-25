/*
  Q Light Controller Plus - Unit test
  scenegrandmaster_test.cpp

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
#include <cmath>

#include "scenegrandmaster_test.h"

// MasterTimer::timerTick() and Universe::processFaders() are private/protected
// (only meant to be driven by MasterTimer's own worker thread); poking them
// directly, like the rest of engine/test/ does, is what lets this test tick
// playback deterministically instead of racing a real background thread. Must
// come before any other engine header that might transitively include these
// first without the macros active (header guards would then hide the members).
#define protected public
#define private public
#include "mastertimer.h"
#include "universe.h"
#undef private
#undef protected

#include "qlcfixturemode.h"
#include "qlcfixturedef.h"
#include "inputoutputmap.h"
#include "qlcchannel.h"
#include "grandmaster.h"
#include "function.h"
#include "fixture.h"
#include "qlcfile.h"
#include "scene.h"
#include "doc.h"

#include "../common/resource_paths.h"

void SceneGrandMaster_Test::initTestCase()
{
}

void SceneGrandMaster_Test::cleanupTestCase()
{
}

/*
 * Reproduces the everyday operator flow: bring a scene up, then reach for the
 * intensity fader while the console's Grand Master is pulled down. Checks the
 * DMX that would actually leave the box (Universe::postGMValues()), not just
 * the pre-GM buffer most other engine tests stop at, and that Grand Master
 * (default: Reduce / Intensity-channel-only) leaves the non-intensity (Pan)
 * channel alone while scaling the intensity (Shutter/Dimmer) channel.
 */
void SceneGrandMaster_Test::intensityFaderAndGrandMasterCombine()
{
    Doc *doc = new Doc(this);

    QDir dir(INTERNAL_FIXTUREDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(doc->fixtureDefCache()->loadMap(dir) == true);

    QLCFixtureDef *def = doc->fixtureDefCache()->fixtureDef("Futurelight", "DJScan250");
    QVERIFY(def != NULL);

    QLCFixtureMode *mode = def->mode("Mode 1");
    QVERIFY(mode != NULL);

    Fixture *fxi = new Fixture(doc);
    fxi->setFixtureDefinition(def, mode);
    QCOMPARE(fxi->channels(), quint32(6));
    fxi->setAddress(0);
    fxi->setUniverse(0);
    doc->addFixture(fxi);

    MasterTimer timer(doc);

    Scene *scene = new Scene(doc);
    scene->setName("GM + Intensity fader regression");
    scene->setFadeInSpeed(0);
    scene->setFadeOutSpeed(0);
    scene->setValue(fxi->id(), 5, 200); // Shutter: Intensity group, HTP
    scene->setValue(fxi->id(), 0, 100); // Pan: not Intensity group, LTP
    doc->addFunction(scene);

    // Grand Master at 50% (default mode: Reduce, Intensity channels only)
    doc->inputOutputMap()->setGrandMasterValue(128);
    QCOMPARE(doc->inputOutputMap()->grandMasterChannelMode(), GrandMaster::Intensity);
    QCOMPARE(doc->inputOutputMap()->grandMasterValueMode(), GrandMaster::Reduce);

    scene->start(&timer, FunctionParent::master());
    timer.timerTick();

    QList<Universe *> ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());

    uchar expectedShutterPostGM = uchar(floor(200.0 * (128.0 / 255.0) + 0.5));
    QCOMPARE(uchar(ua[0]->preGMValues()[5]), uchar(200));
    QCOMPARE(uchar(ua[0]->postGMValues()->at(5)), expectedShutterPostGM);
    // Grand Master must not touch the non-intensity Pan channel
    QCOMPARE(uchar(ua[0]->preGMValues()[0]), uchar(100));
    QCOMPARE(uchar(ua[0]->postGMValues()->at(0)), uchar(100));
    doc->inputOutputMap()->releaseUniverses(false);

    // Operator now also raises the scene's own intensity fader to 50%, on top
    // of the 50% Grand Master already in effect
    scene->adjustAttribute(0.5, Function::Intensity);
    timer.timerTick();

    ua = doc->inputOutputMap()->claimUniverses();
    ua[0]->processFaders(MasterTimer::tick());

    uchar expectedShutterPreGM = uchar(floor(200.0 * 0.5 + 0.5));
    uchar expectedShutterPostGMBoth = uchar(floor(double(expectedShutterPreGM) * (128.0 / 255.0) + 0.5));
    QCOMPARE(uchar(ua[0]->preGMValues()[5]), expectedShutterPreGM);
    QCOMPARE(uchar(ua[0]->postGMValues()->at(5)), expectedShutterPostGMBoth);
    // Pan is LTP and not Intensity group: unaffected by either fader
    QCOMPARE(uchar(ua[0]->preGMValues()[0]), uchar(100));
    QCOMPARE(uchar(ua[0]->postGMValues()->at(0)), uchar(100));
    doc->inputOutputMap()->releaseUniverses(false);

    scene->stop(FunctionParent::master());
    timer.timerTick();

    // Deliberately not deleting doc/timer here: engine/test's own convention
    // (see e.g. Scene_Test::writeHTPTwoTicksIntensity) leaks this local Doc
    // rather than risk MasterTimer/Universe teardown ordering against a
    // manually-ticked (never actually .start()'d) MasterTimer instance.
}

QTEST_APPLESS_MAIN(SceneGrandMaster_Test)
