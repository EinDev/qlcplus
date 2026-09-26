/*
  Q Light Controller Plus - Unit test
  show_test.cpp

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
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QIcon>

#define protected public
#define private public
#include "show_test.h"
#include "show.h"
#include "showrunner.h"
#include "track.h"
#include "showfunction.h"
#include "scene.h"
#include "doc.h"
#undef private
#undef protected

void Show_Test::initTestCase()
{
    m_doc = new Doc(this);
}

void Show_Test::cleanupTestCase()
{
    delete m_doc;
}

void Show_Test::defaults()
{
    Show s(m_doc);

    // check defaults
    QCOMPARE(s.type(), Function::ShowType);
    QCOMPARE(s.id(), Function::invalidId());
    QCOMPARE(s.name(), "New Show");
    QCOMPARE(s.attributes().count(), 0);
}

void Show_Test::copy()
{
    Show show(m_doc);
    show.setID(123);
    show.setTimeDivision(Show::BPM_3_4, 123);

    Scene *scene = new Scene(m_doc);
    m_doc->addFunction(scene);

    Track *t = new Track(123, &show);
    t->setSceneID(456);
    t->setName("Original track");

    ShowFunction *sf = new ShowFunction(show.getLatestShowFunctionId());
    sf->setFunctionID(scene->id());
    sf->setStartTime(1000);
    sf->setDuration(2000);
    sf->setLocked(true);

    t->addShowFunction(sf);
    show.addTrack(t);

    QVERIFY(show.showFunction(666) == NULL);
    QVERIFY(show.showFunction(0) == sf);

    Show showCopy(m_doc);
    showCopy.copyFrom(&show);

    QVERIFY(show.timeDivisionType() == Show::BPM_3_4);
    QVERIFY(show.timeDivisionBPM() == 123);
    QVERIFY(show.totalDuration() == 3000);

    QVERIFY(showCopy.getTracksCount() == show.getTracksCount());

    Track *copyTrack = showCopy.tracks().first();

    QVERIFY(copyTrack->getSceneID() == 456);
    QVERIFY(copyTrack->name() == "Original track");
    QVERIFY(copyTrack->showFunctions().count() == 1);

    ShowFunction *copySF = copyTrack->showFunctions().first();
    QVERIFY(copySF->functionID() != scene->id());
    QVERIFY(copySF->startTime() == 1000);
    QVERIFY(copySF->duration() == 2000);
    QVERIFY(copySF->isLocked() == true);

    QVERIFY(show.contains(123) == true);
    QVERIFY(show.contains(124) == false);
    QVERIFY(show.contains(0) == true);

    QVERIFY(show.components().count() == 1);
}

void Show_Test::timeDivision()
{
    Show s(m_doc);
    QCOMPARE(s.timeDivisionType(), Show::Time);
    QCOMPARE(s.timeDivisionBPM(), 120);

    s.setTimeDivision(Show::BPM_4_4, 111);
    QCOMPARE(s.timeDivisionType(), Show::BPM_4_4);
    QCOMPARE(s.timeDivisionBPM(), 111);

    QCOMPARE(s.beatsDivision(), 4);
    s.setTimeDivisionType(Show::BPM_2_4);
    QCOMPARE(s.beatsDivision(), 2);
    s.setTimeDivisionType(Show::BPM_3_4);
    QCOMPARE(s.beatsDivision(), 3);
    s.setTimeDivisionType(Show::Time);
    QCOMPARE(s.beatsDivision(), 0);

    QCOMPARE(s.stringToTempo("Time"), Show::Time);
    QCOMPARE(s.stringToTempo("BPM_4_4"), Show::BPM_4_4);
    QCOMPARE(s.stringToTempo("BPM_3_4"), Show::BPM_3_4);
    QCOMPARE(s.stringToTempo("BPM_2_4"), Show::BPM_2_4);

    QCOMPARE(s.tempoToString(Show::Time), "Time");
    QCOMPARE(s.tempoToString(Show::BPM_4_4), "BPM_4_4");
    QCOMPARE(s.tempoToString(Show::BPM_3_4), "BPM_3_4");
    QCOMPARE(s.tempoToString(Show::BPM_2_4), "BPM_2_4");
}

void Show_Test::tracks()
{
    Show s(m_doc);
    s.setID(123);

    QCOMPARE(s.tracks().count(), 0);

    Track *t = new Track(123, &s);
    t->setName("First track");

    Track *t2 = new Track(321, &s);
    t2->setName("Second track");

    QVERIFY(s.addTrack(t) == true);
    QCOMPARE(s.getTracksCount(), 1);
    QCOMPARE(s.tracks().count(), 1);

    QVERIFY(s.track(456) == NULL);
    QVERIFY(s.getTrackFromSceneID(456) == NULL);

    QVERIFY(s.track(0) == t);
    QVERIFY(s.getTrackFromSceneID(123) == t);

    // check sutomatic ID assignment
    QVERIFY(t->id() == 0);
    QVERIFY(t->showId() == 123);

    // check automatic attribute registration
    QCOMPARE(s.attributes().count(), 1);
    QVERIFY(s.attributes().at(0).m_name == "First track-0");

    // add a second track and move it up
    QVERIFY(s.addTrack(t2) == true);
    QCOMPARE(s.getTracksCount(), 2);

    s.moveTrack(t2, -1);
    QVERIFY(s.tracks().at(0)->name() == "Second track");
    QVERIFY(s.tracks().at(1)->name() == "First track");

    // no change
    s.moveTrack(t2, -1);
    QVERIFY(s.tracks().at(0)->name() == "Second track");
    QVERIFY(s.tracks().at(1)->name() == "First track");

    // move back as original
    s.moveTrack(t, -1);
    QVERIFY(s.tracks().at(0)->name() == "First track");
    QVERIFY(s.tracks().at(1)->name() == "Second track");

    // check invalid track removal
    QVERIFY(s.removeTrack(456) == false);
    QCOMPARE(s.tracks().count(), 2);

    // check valid track removal
    QVERIFY(s.removeTrack(0) == true);
    QCOMPARE(s.tracks().count(), 1);
    QCOMPARE(s.attributes().count(), 1);

    QVERIFY(s.removeTrack(1) == true);
    QCOMPARE(s.tracks().count(), 0);
    QCOMPARE(s.attributes().count(), 0);
}

void Show_Test::duration()
{
    Show show(m_doc);
    show.setID(123);

    Scene *scene = new Scene(m_doc);
    m_doc->addFunction(scene);

    Track *t = new Track(123, &show);
    ShowFunction *sf = new ShowFunction(show.getLatestShowFunctionId());
    sf->setFunctionID(scene->id());
    sf->setStartTime(1000);
    sf->setDuration(2000);

    t->addShowFunction(sf);
    show.addTrack(t);

    QVERIFY(show.totalDuration() == 3000);

}

void Show_Test::load()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Function");
    xmlWriter.writeAttribute("Type", "Show");

    xmlWriter.writeStartElement("TimeDivision");
    xmlWriter.writeAttribute("Type", "BPM_2_4");
    xmlWriter.writeAttribute("BPM", "222");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Track");
    xmlWriter.writeAttribute("ID", "0");
    xmlWriter.writeAttribute("SceneID", "111");
    xmlWriter.writeAttribute("Name", "Read track 1");
    xmlWriter.writeAttribute("isMute", "0");
    xmlWriter.writeEndElement();

    xmlWriter.writeStartElement("Track");
    xmlWriter.writeAttribute("ID", "1");
    xmlWriter.writeAttribute("SceneID", "222");
    xmlWriter.writeAttribute("Name", "Read track 2");
    xmlWriter.writeAttribute("isMute", "1");
    xmlWriter.writeEndElement();

    xmlWriter.writeEndElement();

    xmlWriter.writeEndDocument();
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    Show s(m_doc);
    QVERIFY(s.loadXML(xmlReader) == true);

    QCOMPARE(s.timeDivisionType(), Show::BPM_2_4);
    QCOMPARE(s.timeDivisionBPM(), 222);

    QCOMPARE(s.getTracksCount(), 2);

    Track *t, *t2;
    t = s.track(111);
    QVERIFY(t == NULL);

    t = s.track(0);
    t2 = s.track(1);

    QCOMPARE(t->name(), "Read track 1");
    QVERIFY(t->getSceneID() == 111);
    QCOMPARE(t->isMute(), false);

    QCOMPARE(t2->name(), "Read track 2");
    QVERIFY(t2->getSceneID() == 222);
    QCOMPARE(t2->isMute(), true);
}

void Show_Test::save()
{
    Show s(m_doc);
    s.setID(123);
    s.setName("Test Show");
    s.setTimeDivision(Show::BPM_3_4, 111);

    Track *t = new Track(456, &s);
    t->setName("First track");

    Track *t2 = new Track(789, &s);
    t2->setName("Second track");
    t2->setMute(true);

    s.addTrack(t);
    s.addTrack(t2);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(s.saveXML(&xmlWriter) == true);
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);


    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Function");
    QVERIFY(xmlReader.attributes().value("Type").toString() == "Show");
    QVERIFY(xmlReader.attributes().value("ID").toString() == "123");
    QVERIFY(xmlReader.attributes().value("Name").toString() == "Test Show");

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "TimeDivision");
    QVERIFY(xmlReader.attributes().value("Type").toString() == "BPM_3_4");
    QVERIFY(xmlReader.attributes().value("BPM").toString() == "111");
    xmlReader.skipCurrentElement();

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Track");
    QVERIFY(xmlReader.attributes().value("ID").toString() == "0");
    QVERIFY(xmlReader.attributes().value("SceneID").toString() == "456");
    QVERIFY(xmlReader.attributes().value("Name").toString() == "First track");
    QVERIFY(xmlReader.attributes().value("isMute").toString() == "0");
    xmlReader.skipCurrentElement();

    xmlReader.readNextStartElement();
    QVERIFY(xmlReader.name().toString() == "Track");
    QVERIFY(xmlReader.attributes().value("ID").toString() == "1");
    QVERIFY(xmlReader.attributes().value("SceneID").toString() == "789");
    QVERIFY(xmlReader.attributes().value("Name").toString() == "Second track");
    QVERIFY(xmlReader.attributes().value("isMute").toString() == "1");
}

static bool loadShowFromXml(Show &s, const QString &xml)
{
    QByteArray data = xml.toUtf8();
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    return s.loadXML(xmlReader);
}

void Show_Test::icon()
{
    Show s(m_doc);
    // only the accessor is exercised: whether the resource is bundled into
    // this binary is not the Show's concern
    QIcon icon = s.getIcon();
    Q_UNUSED(icon);
}

void Show_Test::createCopy()
{
    Show *show = new Show(m_doc);
    show->setName("Original");
    show->setTimeDivision(Show::BPM_4_4, 100);
    QVERIFY(m_doc->addFunction(show));

    Scene *scene = new Scene(m_doc);
    scene->setName("Clip");
    QVERIFY(m_doc->addFunction(scene));

    Track *track = new Track(scene->id(), show);
    track->setName("T1");
    track->setSpoutSize(QSize(640, 480));
    show->addTrack(track);
    ShowFunction *sf = track->createShowFunction(scene->id());
    sf->setStartTime(500);
    sf->setDuration(1500);
    sf->setColor(QColor(Qt::green));

    int functionsBefore = m_doc->functions().count();

    // a copy added to the Doc: the show and each clip function get copied
    Function *copy = show->createCopy(m_doc);
    QVERIFY(copy != NULL);
    QVERIFY(copy != show);
    QCOMPARE(copy->type(), Function::ShowType);
    QVERIFY(copy->id() != Function::invalidId());
    QVERIFY(m_doc->function(copy->id()) == copy);
    QCOMPARE(m_doc->functions().count(), functionsBefore + 2);   // the Show + the Scene copy

    Show *showCopy = qobject_cast<Show *>(copy);
    QVERIFY(showCopy != NULL);
    QCOMPARE(showCopy->name(), QString("Original"));   // only the clips are renamed
    QCOMPARE(showCopy->timeDivisionType(), Show::BPM_4_4);
    QCOMPARE(showCopy->timeDivisionBPM(), 100);
    QCOMPARE(showCopy->getTracksCount(), 1);
    Track *trackCopy = showCopy->tracks().first();
    QCOMPARE(trackCopy->name(), QString("T1"));
    QCOMPARE(trackCopy->spoutSize(), QSize(640, 480));
    QCOMPARE(trackCopy->showFunctions().count(), 1);
    ShowFunction *sfCopy = trackCopy->showFunctions().first();
    QVERIFY(sfCopy->functionID() != scene->id());
    QCOMPARE(m_doc->function(sfCopy->functionID())->name(), QString("Copy of Clip"));
    QCOMPARE(sfCopy->startTime(), 500u);
    QCOMPARE(sfCopy->duration(), 1500u);
    QCOMPARE(sfCopy->color(), QColor(Qt::green));

    // a detached copy still copies the clip functions into the Doc
    Function *loose = show->createCopy(m_doc, false);
    QVERIFY(loose != NULL);
    QCOMPARE(loose->id(), Function::invalidId());
    QCOMPARE(m_doc->functions().count(), functionsBefore + 3);
    delete loose;

    m_doc->clearContents();
}

void Show_Test::copyFromInvalid()
{
    Show show(m_doc);
    Scene scene(m_doc);
    QVERIFY(show.copyFrom(&scene) == false);

    // a clip whose function is unknown to the Doc is skipped by the copy
    Show source(m_doc);
    Track *track = new Track(Function::invalidId(), &source);
    source.addTrack(track);
    track->createShowFunction(424242);

    Show copy(m_doc);
    QVERIFY(copy.copyFrom(&source) == true);
    QCOMPARE(copy.getTracksCount(), 1);
    QCOMPARE(copy.tracks().first()->showFunctions().count(), 0);
}

void Show_Test::tempoStrings()
{
    Show s(m_doc);
    s.setTimeDivisionBPM(93);
    QCOMPARE(s.timeDivisionBPM(), 93);

    QCOMPARE(Show::tempoToString(Show::Invalid), QString("Invalid"));
    QCOMPARE(Show::stringToTempo("garbage"), Show::Invalid);
    QCOMPARE(Show::stringToTempo(QString()), Show::Invalid);
}

void Show_Test::trackLookups()
{
    Show s(m_doc);
    s.setID(5);

    Track *t1 = new Track(Function::invalidId(), &s);
    Track *t2 = new Track(Function::invalidId(), &s);
    s.addTrack(t1);
    s.addTrack(t2);
    ShowFunction *sf1 = t1->createShowFunction(10);
    ShowFunction *sf2 = t2->createShowFunction(11);

    QVERIFY(s.getTrackFromShowFunctionID(sf1->id()) == t1);
    QVERIFY(s.getTrackFromShowFunctionID(sf2->id()) == t2);
    QVERIFY(s.getTrackFromShowFunctionID(777) == NULL);

    // a null track is ignored
    s.moveTrack(NULL, -1);
    QVERIFY(s.tracks().at(0) == t1);
    QVERIFY(s.tracks().at(1) == t2);
}

void Show_Test::moveTrackDown()
{
    Show s(m_doc);
    s.setID(5);

    Track *t1 = new Track(Function::invalidId(), &s);
    t1->setName("A");
    Track *t2 = new Track(Function::invalidId(), &s);
    t2->setName("B");
    Track *t3 = new Track(Function::invalidId(), &s);
    t3->setName("C");
    s.addTrack(t1);
    s.addTrack(t2);
    s.addTrack(t3);

    // the last track cannot move down
    s.moveTrack(t3, 1);
    QCOMPARE(s.tracks().at(2)->name(), QString("C"));
    QCOMPARE(t3->id(), 2u);

    // moving down swaps with the next one
    s.moveTrack(t1, 1);
    QCOMPARE(s.tracks().at(0)->name(), QString("B"));
    QCOMPARE(s.tracks().at(1)->name(), QString("A"));
    QCOMPARE(s.tracks().at(2)->name(), QString("C"));
    QCOMPARE(t1->id(), 1u);
    QCOMPARE(t2->id(), 0u);

    s.moveTrack(t1, 1);
    QCOMPARE(s.tracks().at(2)->name(), QString("A"));
    QCOMPARE(t1->id(), 2u);
}

void Show_Test::loadInvalid()
{
    Show s1(m_doc);
    QVERIFY(loadShowFromXml(s1, "<Foo Type=\"Show\"/>") == false);

    Show s2(m_doc);
    QVERIFY(loadShowFromXml(s2, "<Function Type=\"Scene\"/>") == false);

    // an unknown child tag is skipped, the rest still loads
    Show s3(m_doc);
    QVERIFY(loadShowFromXml(s3, "<Function Type=\"Show\">"
                                "<Bogus><Nested/></Bogus>"
                                "<TimeDivision Type=\"BPM_3_4\" BPM=\"99\"/>"
                                "</Function>") == true);
    QCOMPARE(s3.timeDivisionType(), Show::BPM_3_4);
    QCOMPARE(s3.timeDivisionBPM(), 99);
}

void Show_Test::postLoad()
{
    Scene *scene = new Scene(m_doc);
    QVERIFY(m_doc->addFunction(scene));

    Show *show = new Show(m_doc);
    QVERIFY(m_doc->addFunction(show));
    Track *track = new Track(Function::invalidId(), show);
    show->addTrack(track);
    ShowFunction *good = track->createShowFunction(scene->id());
    good->setStartTime(0);
    good->setDuration(1000);
    track->createShowFunction(31337);   // dangling: dropped by Track::postLoad

    m_doc->resetModified();
    show->postLoad();

    // the dangling clip is gone, the Doc knows it changed and a schedule
    // snapshot exists right away (a loaded Show may be started from anywhere)
    QCOMPARE(track->showFunctions().count(), 1);
    QVERIFY(m_doc->isModified());
    QVERIFY(show->m_currentSchedule.isNull() == false);
    QCOMPARE(show->m_currentSchedule->clips.count(), 1);
    QCOMPARE(show->m_currentSchedule->totalRunTime, 1000u);

    // nothing to fix: the modified flag stays put
    m_doc->resetModified();
    show->postLoad();
    QVERIFY(m_doc->isModified() == false);

    m_doc->clearContents();
}

void Show_Test::scheduleSkips()
{
    Scene *scene = new Scene(m_doc);
    QVERIFY(m_doc->addFunction(scene));

    Show *show = new Show(m_doc);
    QVERIFY(m_doc->addFunction(show));

    // a clip pointing at a function the Doc does not have is left out
    Track *track = new Track(Function::invalidId(), show);
    show->addTrack(track);
    ShowFunction *sf = track->createShowFunction(scene->id());
    sf->setStartTime(100);
    sf->setDuration(200);
    ShowFunction *dangling = track->createShowFunction(4242);
    dangling->setStartTime(0);
    dangling->setDuration(5000);

    // a track that lost its id is skipped as well
    Track *orphan = new Track(Function::invalidId(), show);
    show->addTrack(orphan);
    ShowFunction *orphanSf = orphan->createShowFunction(scene->id());
    orphanSf->setStartTime(0);
    orphanSf->setDuration(9000);
    orphan->setId(Track::invalidId());

    show->rebuildSchedule();
    QSharedPointer<const ShowSchedule> schedule = show->currentSchedule();
    QCOMPARE(schedule->clips.count(), 1);
    QCOMPARE(schedule->clips.first().functionId, scene->id());
    QCOMPARE(schedule->totalRunTime, 300u);
    QVERIFY(schedule->intensity.contains(track->id()));
    QVERIFY(schedule->intensity.contains(Track::invalidId()) == false);

    orphan->setId(1);   // give it back before the Show tears down
    m_doc->clearContents();
}

void Show_Test::running()
{
    Scene *scene = new Scene(m_doc);
    QVERIFY(m_doc->addFunction(scene));

    Show *show = new Show(m_doc);
    QVERIFY(m_doc->addFunction(show));
    Track *track = new Track(scene->id(), show);
    show->addTrack(track);
    ShowFunction *sf = track->createShowFunction(scene->id());
    sf->setStartTime(0);
    sf->setDuration(1000);
    show->rebuildSchedule();

    MasterTimer *timer = m_doc->masterTimer();
    QList<Universe *> universes;

    QVERIFY(show->m_runner == NULL);
    show->preRun(timer);
    QVERIFY(show->m_runner != NULL);
    QVERIFY(show->isRunning());
    ShowRunner *first = show->m_runner;

    // a second preRun (a restart before the previous run was torn down)
    // replaces the runner instead of leaking it
    show->preRun(timer);
    QVERIFY(show->m_runner != NULL);
    QVERIFY(show->m_runner != first);

    // pause goes to the runner and to the Function state; a paused write
    // only picks up timeline edits
    show->setPause(true);
    QVERIFY(show->isPaused());
    show->markScheduleDirty();
    show->rebuildSchedule();
    QVERIFY(show->takePendingSchedule().isNull() == false);
    show->rebuildSchedule();
    show->write(timer, universes);
    QVERIFY(show->takePendingSchedule().isNull());   // consumed by the runner
    show->setPause(false);
    QVERIFY(show->isPaused() == false);

    // the per-track attribute reaches the live runner
    int index = show->adjustAttribute(0.25, 0);
    QCOMPARE(index, 0);
    QCOMPARE(show->m_runner->m_intensityMap.value(track->id()), 0.25);
    QVERIFY(show->isScheduleDirty());

    show->slotChildStopped(scene->id());

    show->postRun(timer, universes);
    QVERIFY(show->m_runner == NULL);
    QVERIFY(show->isRunning() == false);

    m_doc->clearContents();
}


QTEST_MAIN(Show_Test)
