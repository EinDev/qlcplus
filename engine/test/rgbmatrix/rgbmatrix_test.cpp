/*
  Q Light Controller Plus - Unit tests
  rgbmatrix_test.cpp

  Copyright (C) Heikki Junnila
                Massimo Callegari

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

#define protected public
#define private public
#include "rgbscriptscache.h"
#include "rgbmatrix_test.h"
#include "mastertimer_stub.h"
#include "qlcfixturemode.h"
#include "qlcfixturehead.h"
#include "qlcfixturedef.h"
#include "qlcchannel.h"
#include "fixturegroup.h"
#include "genericfader.h"
#include "fadechannel.h"
#include "grandmaster.h"
#include "mastertimer.h"
#include "rgbmatrix.h"
#include "rgbplain.h"
#include "rgbimage.h"
#include "universe.h"
#include "fixture.h"
#include "qlcfile.h"
#include "doc.h"
#undef private
#undef protected

#include "../common/resource_paths.h"

namespace
{
    struct ChSpec
    {
        const char *name;
        QLCChannel::Group group;
        QLCChannel::PrimaryColour colour;
    };

    /** Build a single-mode fixture definition from $chans and register it in the
     *  Doc's definition cache (which takes ownership). When $headChannels is not
     *  empty, an explicit head with those channels is added to the mode, so that
     *  channels left outside of it can be auto-detected as master channels. */
    QLCFixtureDef *makeDef(Doc *doc, const QString &model, const QList<ChSpec> &chans,
                           const QList<quint32> &headChannels = QList<quint32>())
    {
        QLCFixtureDef *def = new QLCFixtureDef();
        def->setManufacturer("RGBMatrixTest");
        def->setModel(model);

        QLCFixtureMode *mode = new QLCFixtureMode(def);
        mode->setName("Default");

        for (int i = 0; i < chans.count(); i++)
        {
            QLCChannel *ch = new QLCChannel();
            ch->setName(chans.at(i).name);
            ch->setGroup(chans.at(i).group);
            ch->setColour(chans.at(i).colour);
            def->addChannel(ch);
            mode->insertChannel(ch, i);
        }

        if (headChannels.isEmpty() == false)
        {
            QLCFixtureHead head;
            foreach (quint32 ch, headChannels)
                head.addChannel(ch);
            mode->insertHead(-1, head);
        }

        def->addMode(mode);
        doc->fixtureDefCache()->addFixtureDef(def);
        return def;
    }

    uchar dmx(Universe *universe, int address)
    {
        return uchar(universe->preGMValues().at(address));
    }
}

quint32 RGBMatrix_Test::makeRig(QLCFixtureDef *def, const QSize &size, quint32 address)
{
    QLCFixtureMode *mode = def->modes().first();

    FixtureGroup *grp = new FixtureGroup(m_doc);
    grp->setName(def->model() + " group");
    grp->setSize(size);
    m_doc->addFixtureGroup(grp);

    for (int y = 0; y < size.height(); y++)
    {
        for (int x = 0; x < size.width(); x++)
        {
            Fixture *fxi = new Fixture(m_doc);
            fxi->setName(QString("%1 %2,%3").arg(def->model()).arg(x).arg(y));
            fxi->setFixtureDefinition(def, mode);
            fxi->setUniverse(0);
            fxi->setAddress(address);
            address += fxi->channels();
            m_doc->addFixture(fxi);
            grp->assignFixture(fxi->id(), QLCPoint(x, y));
        }
    }

    return grp->id();
}

void RGBMatrix_Test::initTestCase()
{
    m_doc = new Doc(this);

    QDir fxiDir(INTERNAL_FIXTUREDIR);
    fxiDir.setFilter(QDir::Files);
    fxiDir.setNameFilters(QStringList() << QString("*%1").arg(KExtFixture));
    QVERIFY(m_doc->fixtureDefCache()->loadMap(fxiDir) == true);

    QLCFixtureDef* def = m_doc->fixtureDefCache()->fixtureDef("Stairville", "LED PAR56");
    QVERIFY(def != NULL);
    QLCFixtureMode* mode = def->modes().first();
    QVERIFY(mode != NULL);

    FixtureGroup* grp = new FixtureGroup(m_doc);
    grp->setName("Test Group");
    grp->setSize(QSize(5, 5));
    m_doc->addFixtureGroup(grp);

    for (int i = 0; i < 25; i++)
    {
        Fixture* fxi = new Fixture(m_doc);
        fxi->setFixtureDefinition(def, mode);
        fxi->setAddress(i * fxi->channels());
        m_doc->addFixture(fxi);

        grp->assignFixture(fxi->id());
    }

    QVERIFY(m_doc->rgbScriptsCache()->load(QDir(INTERNAL_SCRIPTDIR)));
    QVERIFY(m_doc->rgbScriptsCache()->names().size() != 0);

    /* Synthetic fixture definitions for the runtime (write) tests, so that the
     * DMX addresses and channel roles are fully under the test's control */
    m_rgbDef = makeDef(m_doc, "RGB",
                       QList<ChSpec>() << ChSpec{"Red", QLCChannel::Intensity, QLCChannel::Red}
                                       << ChSpec{"Green", QLCChannel::Intensity, QLCChannel::Green}
                                       << ChSpec{"Blue", QLCChannel::Intensity, QLCChannel::Blue});

    // All channels end up in one auto-generated head, so the dimmer is a
    // per-head dimmer and there is no master intensity channel
    m_multiDef = makeDef(m_doc, "Multi",
                         QList<ChSpec>() << ChSpec{"Dimmer", QLCChannel::Intensity, QLCChannel::NoColour}
                                         << ChSpec{"Red", QLCChannel::Intensity, QLCChannel::Red}
                                         << ChSpec{"Green", QLCChannel::Intensity, QLCChannel::Green}
                                         << ChSpec{"Blue", QLCChannel::Intensity, QLCChannel::Blue}
                                         << ChSpec{"White", QLCChannel::Intensity, QLCChannel::White}
                                         << ChSpec{"Amber", QLCChannel::Intensity, QLCChannel::Amber}
                                         << ChSpec{"UV", QLCChannel::Intensity, QLCChannel::UV}
                                         << ChSpec{"Shutter", QLCChannel::Shutter, QLCChannel::NoColour});

    // Channel 0 is outside the head -> master intensity; channel 1 is the head dimmer
    m_masterHeadDef = makeDef(m_doc, "MasterHead",
                              QList<ChSpec>() << ChSpec{"Master", QLCChannel::Intensity, QLCChannel::NoColour}
                                              << ChSpec{"HeadDim", QLCChannel::Intensity, QLCChannel::NoColour}
                                              << ChSpec{"Red", QLCChannel::Intensity, QLCChannel::Red}
                                              << ChSpec{"Green", QLCChannel::Intensity, QLCChannel::Green}
                                              << ChSpec{"Blue", QLCChannel::Intensity, QLCChannel::Blue},
                              QList<quint32>() << 1 << 2 << 3 << 4);

    m_cmyDef = makeDef(m_doc, "CMY",
                       QList<ChSpec>() << ChSpec{"Cyan", QLCChannel::Intensity, QLCChannel::Cyan}
                                       << ChSpec{"Magenta", QLCChannel::Intensity, QLCChannel::Magenta}
                                       << ChSpec{"Yellow", QLCChannel::Intensity, QLCChannel::Yellow});

    m_rgbGroup = makeRig(m_rgbDef, QSize(4, 1), 256);
    m_rgbSquareGroup = makeRig(m_rgbDef, QSize(2, 2), 280);
    m_multiGroup = makeRig(m_multiDef, QSize(1, 1), 300);
    m_masterHeadGroup = makeRig(m_masterHeadDef, QSize(1, 1), 320);
    m_cmyGroup = makeRig(m_cmyDef, QSize(1, 1), 340);
}

void RGBMatrix_Test::cleanupTestCase()
{
    delete m_doc;
}

void RGBMatrix_Test::initial()
{
    RGBMatrix mtx(m_doc);
    QCOMPARE(mtx.type(), Function::RGBMatrixType);
    QCOMPARE(mtx.fixtureGroup(), FixtureGroup::invalidId());
    QCOMPARE(mtx.getColor(0), QColor(Qt::red));
    QCOMPARE(mtx.getColor(1), QColor());
    QCOMPARE(mtx.m_fadersMap.count(), 0);
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0);
    QCOMPARE(mtx.name(), tr("New RGB Matrix"));
    QCOMPARE(mtx.duration(), uint(500));
    QCOMPARE(mtx.totalDuration(), uint(0));
    QVERIFY(mtx.algorithm() != NULL);
    QCOMPARE(mtx.algorithm()->name(), QString("Stripes"));
    QCOMPARE(mtx.components().size(), 0);
}

void RGBMatrix_Test::group()
{
    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(0);
    QCOMPARE(mtx.fixtureGroup(), uint(0));

    mtx.setFixtureGroup(15);
    QCOMPARE(mtx.fixtureGroup(), uint(15));

    mtx.setFixtureGroup(FixtureGroup::invalidId());
    QCOMPARE(mtx.fixtureGroup(), FixtureGroup::invalidId());
}

void RGBMatrix_Test::color()
{
    RGBMatrix mtx(m_doc);
    mtx.setColor(0, Qt::blue);
    QCOMPARE(mtx.getColor(0), QColor(Qt::blue));

    mtx.setColor(0, QColor());
    QCOMPARE(mtx.getColor(0), QColor());

    mtx.setColor(1, Qt::green);
    QCOMPARE(mtx.getColor(1), QColor(Qt::green));

    mtx.setColor(1, QColor());
    QCOMPARE(mtx.getColor(1), QColor());
}

void RGBMatrix_Test::copy()
{
    RGBMatrix mtx(m_doc);
    mtx.setColor(0, Qt::magenta);
    mtx.setColor(1, Qt::yellow);
    mtx.setFixtureGroup(0);
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Stripes"));
    QVERIFY(mtx.algorithm() != NULL);

    RGBMatrix* copyMtx = qobject_cast<RGBMatrix*> (mtx.createCopy(m_doc));
    QVERIFY(copyMtx != NULL);
    QCOMPARE(copyMtx->getColor(0), QColor(Qt::magenta));
    QCOMPARE(copyMtx->getColor(1), QColor(Qt::yellow));
    QCOMPARE(copyMtx->fixtureGroup(), uint(0));
    QVERIFY(copyMtx->algorithm() != NULL);
    QVERIFY(copyMtx->algorithm() != mtx.algorithm()); // Different object pointer!
    QCOMPARE(copyMtx->algorithm()->name(), QString("Stripes"));
}

void RGBMatrix_Test::previewMaps()
{
    RGBMatrix mtx(m_doc);
    RGBMatrixStep handler;
    QVERIFY(mtx.algorithm() != NULL);
    QCOMPARE(mtx.algorithm()->name(), QString("Stripes"));

    int steps = mtx.stepsCount();
    QCOMPARE(steps, 0);

    mtx.previewMap(0, &handler);
    QCOMPARE(handler.m_map.size(), 0); // No fixture group

    mtx.setFixtureGroup(0);
    steps = mtx.stepsCount();
    QCOMPARE(steps, 5);
    QCOMPARE(mtx.components().size(), 25);
    QCOMPARE(mtx.totalDuration(), uint(2500));

    mtx.setTotalDuration(8000);
    QCOMPARE(mtx.totalDuration(), uint(8000));

    mtx.previewMap(0, &handler);
    QCOMPARE(handler.m_map.size(), 5);

    for (int z = 0; z < steps; z++)
    {
        mtx.previewMap(z, &handler);
        for (int y = 0; y < 5; y++)
        {
            for (int x = 0; x < 5; x++)
            {
                if (x == z)
                    QCOMPARE(handler.m_map[y][x], QColor(Qt::black).rgb());
                else
                    QCOMPARE(handler.m_map[y][x], uint(0));
            }
        }
    }
}

void RGBMatrix_Test::property()
{
    RGBMatrix mtx(m_doc);
    QVERIFY(mtx.algorithm() != NULL);
    QCOMPARE(mtx.algorithm()->name(), QString("Stripes"));

    // check on invalid property
    QCOMPARE(mtx.property("foo"), QString());

    // check a valid property
    QCOMPARE(mtx.property("orientation"), QString("Horizontal"));

    mtx.setProperty("orientation", "Vertical");

    QCOMPARE(mtx.property("orientation"), QString("Vertical"));
}

void RGBMatrix_Test::loadSave()
{
    RGBMatrix* mtx = new RGBMatrix(m_doc);
    mtx->setColor(0, Qt::magenta);
    mtx->setColor(1, Qt::blue);
    mtx->setColor(2, Qt::green);
    mtx->setColor(3, Qt::red);
    mtx->setColor(4, Qt::yellow);
    mtx->setControlMode(RGBMatrix::ControlModeRgb);
    mtx->setFixtureGroup(42);
    mtx->setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Stripes"));
    QVERIFY(mtx->algorithm() != NULL);
    QCOMPARE(mtx->algorithm()->name(), QString("Stripes"));

    mtx->setName("Xyzzy");
    mtx->setDirection(Function::Backward);
    mtx->setRunOrder(Function::PingPong);
    mtx->setDuration(1200);
    mtx->setFadeInSpeed(10);
    mtx->setFadeOutSpeed(20);
    m_doc->addFunction(mtx);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    QVERIFY(mtx->saveXML(&xmlWriter) == true);

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    QCOMPARE(xmlReader.name().toString(), QString("Function"));
    QCOMPARE(xmlReader.attributes().value("Type").toString(), QString("RGBMatrix"));
    QCOMPARE(xmlReader.attributes().value("ID").toString(), QString::number(mtx->id()));
    QCOMPARE(xmlReader.attributes().value("Name").toString(), QString("Xyzzy"));

    int speed = 0, dir = 0, run = 0, algo = 0, grp = 0, color1 = 0, color2 = 0, color3 = 0, color4 = 0, color5 = 0, colormode = 0;

    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name().toString() == "Speed")
        {
            QCOMPARE(xmlReader.attributes().value("FadeIn").toString(), QString("10"));
            QCOMPARE(xmlReader.attributes().value("FadeOut").toString(), QString("20"));
            QCOMPARE(xmlReader.attributes().value("Duration").toString(), QString("1200"));
            speed++;
            xmlReader.skipCurrentElement();
        }
        else if (xmlReader.name().toString() == "Direction")
        {
            QCOMPARE(xmlReader.readElementText(), QString("Backward"));
            dir++;
        }
        else if (xmlReader.name().toString() == "RunOrder")
        {
            QCOMPARE(xmlReader.readElementText(), QString("PingPong"));
            run++;
        }
        else if (xmlReader.name().toString() == "Algorithm")
        {
            // RGBAlgorithms take care of Algorithm tag's contents
            algo++;
            xmlReader.skipCurrentElement();
        }
        else if (xmlReader.name().toString() == "Color")
        {
            bool ok = false;
            int colorNum = xmlReader.attributes().value("Index").toInt(&ok);
            QVERIFY(ok);

            switch (colorNum)
            {
                case 0:
                    QCOMPARE(xmlReader.readElementText().toUInt(), QColor(Qt::magenta).rgb());
                    color1++;
                break;
                case 1:
                    QCOMPARE(xmlReader.readElementText().toUInt(), QColor(Qt::blue).rgb());
                    color2++;
                break;
                case 2:
                    QCOMPARE(xmlReader.readElementText().toUInt(), QColor(Qt::green).rgb());
                    color3++;
                break;
                case 3:
                    QCOMPARE(xmlReader.readElementText().toUInt(), QColor(Qt::red).rgb());
                    color4++;
                break;
                case 4:
                    QCOMPARE(xmlReader.readElementText().toUInt(), QColor(Qt::yellow).rgb());
                    color5++;
                break;
                default:
                    // The color number can be between 1 and MAXINT, but here we expect only 5.
                    QVERIFY(colorNum > 0 && colorNum <= 5);
                break;
            }
        }
        else if (xmlReader.name().toString() == "FixtureGroup")
        {
            QCOMPARE(xmlReader.readElementText(), QString("42"));
            grp++;
        }
        else if (xmlReader.name().toString() == "ControlMode")
        {
            QCOMPARE(xmlReader.readElementText(), QString("RGB"));
            colormode++;
        }
        else
        {
            QFAIL(QString("Unexpected tag: %1").arg(xmlReader.name().toString()).toUtf8().constData());
        }
    }

    QCOMPARE(speed, 1);
    QCOMPARE(dir, 1);
    QCOMPARE(run, 1);
    QCOMPARE(algo, 1);
    QCOMPARE(color1, 1);
    QCOMPARE(color2, 1);
    QCOMPARE(color3, 1);
    QCOMPARE(color4, 1);
    QCOMPARE(color5, 1);
    QCOMPARE(grp, 1);
    QCOMPARE(colormode, 1);

    xmlReader.setDevice(NULL);
    buffer.seek(0);
    xmlReader.setDevice(&buffer);
    xmlReader.readNextStartElement();

    RGBMatrix mtx2(m_doc);
    QVERIFY(mtx2.loadXML(xmlReader) == true);
    QCOMPARE(mtx2.direction(), Function::Backward);
    QCOMPARE(mtx2.runOrder(), Function::PingPong);
    QCOMPARE(mtx2.getColor(0), QColor(Qt::magenta));
    QCOMPARE(mtx2.getColor(1), QColor(Qt::blue));
    QCOMPARE(mtx2.controlMode(), RGBMatrix::ControlModeRgb);
    QCOMPARE(mtx2.fixtureGroup(), uint(42));
    QVERIFY(mtx2.algorithm() != NULL);
    QCOMPARE(mtx2.algorithm()->name(), mtx->algorithm()->name());
    QCOMPARE(mtx2.duration(), uint(1200));
    QCOMPARE(mtx2.fadeInSpeed(), uint(10));
    QCOMPARE(mtx2.fadeOutSpeed(), uint(20));

    buffer.close();
    buffer.setData(QByteArray());

    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    xmlWriter.setDevice(&buffer);

    // Put some extra garbage in
    xmlWriter.writeStartElement("Foo");
    xmlWriter.writeEndElement();

    buffer.close();
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);

    xmlReader.setDevice(&buffer);
    xmlReader.readNextStartElement();

    QVERIFY(mtx2.loadXML(xmlReader) == false); // Not a function node

    xmlReader.setDevice(NULL);
    buffer.close();
    buffer.setData(QByteArray());

    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    xmlWriter.setDevice(&buffer);

    // Put some extra garbage in
    xmlWriter.writeStartElement("Function");
    xmlWriter.writeAttribute("Type", "Scene");
    xmlWriter.writeEndElement();

    buffer.close();
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);

    xmlReader.setDevice(&buffer);
    xmlReader.readNextStartElement();
    QVERIFY(mtx2.loadXML(xmlReader) == false); // Not an RGBMatrix node

}

/****************************************************************************
 * RGBMatrixStep
 ****************************************************************************/

void RGBMatrix_Test::stepHandler()
{
    const QColor red(Qt::red);
    const QColor blue(Qt::blue);
    RGBMatrixStep step;
    QCOMPARE(step.currentStepIndex(), 0);
    QCOMPARE(step.stepColor(), QColor());

    step.setCurrentStepIndex(7);
    QCOMPARE(step.currentStepIndex(), 7);
    step.setStepColor(Qt::cyan);
    QCOMPARE(step.stepColor(), QColor(Qt::cyan));

    // Forward start: first step, start color
    step.initializeDirection(Function::Forward, red, blue, 4, NULL);
    QCOMPARE(step.currentStepIndex(), 0);
    QCOMPARE(step.stepColor(), red);
    // No algorithm -> no color delta
    QCOMPARE(step.m_crDelta, 0);
    QCOMPARE(step.m_cgDelta, 0);
    QCOMPARE(step.m_cbDelta, 0);

    // Backward start: last step, end color
    step.initializeDirection(Function::Backward, red, blue, 4, NULL);
    QCOMPARE(step.currentStepIndex(), 3);
    QCOMPARE(step.stepColor(), blue);

    // Backward start without a valid end color falls back to the start color
    step.initializeDirection(Function::Backward, red, QColor(), 4, NULL);
    QCOMPARE(step.currentStepIndex(), 3);
    QCOMPARE(step.stepColor(), red);

    // Color deltas are only computed for an algorithm accepting 2+ colors
    QScopedPointer<RGBAlgorithm> stripes(RGBAlgorithm::algorithm(m_doc, "Stripes"));
    QVERIFY(stripes->acceptColors() >= 2);
    QScopedPointer<RGBAlgorithm> plain(RGBAlgorithm::algorithm(m_doc, "Plain Color"));
    QCOMPARE(plain->acceptColors(), 1);

    step.calculateColorDelta(red, blue, plain.data());
    QCOMPARE(step.m_crDelta, 0);
    QCOMPARE(step.m_cbDelta, 0);

    step.calculateColorDelta(red, blue, stripes.data());
    QCOMPARE(step.m_crDelta, -255);
    QCOMPARE(step.m_cgDelta, 0);
    QCOMPARE(step.m_cbDelta, 255);

    // updateStepColor: invalid step count is ignored, a single step is the start color
    step.setStepColor(Qt::green);
    step.updateStepColor(1, red, 0);
    QCOMPARE(step.stepColor(), QColor(Qt::green));
    step.updateStepColor(1, red, 1);
    QCOMPARE(step.stepColor(), red);
    // Half way through a 3 steps fade from red to blue
    step.updateStepColor(1, red, 3);
    QCOMPARE(step.stepColor().red(), 255 - 127);
    QCOMPARE(step.stepColor().green(), 0);
    QCOMPARE(step.stepColor().blue(), 127);
    step.updateStepColor(2, red, 3);
    QCOMPARE(step.stepColor(), blue);

    /* Loop, forward */
    step.initializeDirection(Function::Forward, red, blue, 3, stripes.data());
    QVERIFY(step.checkNextStep(Function::Loop, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 1);
    QVERIFY(step.checkNextStep(Function::Loop, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 2);
    QCOMPARE(step.stepColor(), blue);
    QVERIFY(step.checkNextStep(Function::Loop, red, blue, 3)); // wraps
    QCOMPARE(step.currentStepIndex(), 0);
    QCOMPARE(step.stepColor(), red);

    /* Loop, backward */
    step.initializeDirection(Function::Backward, red, blue, 3, stripes.data());
    QCOMPARE(step.currentStepIndex(), 2);
    QVERIFY(step.checkNextStep(Function::Loop, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 1);
    QVERIFY(step.checkNextStep(Function::Loop, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 0);
    QCOMPARE(step.stepColor(), red);
    QVERIFY(step.checkNextStep(Function::Loop, red, blue, 3)); // wraps to the end color
    QCOMPARE(step.currentStepIndex(), 2);
    QCOMPARE(step.stepColor(), blue);
    // wrapping backward with an invalid end color keeps the current color
    step.setCurrentStepIndex(0);
    step.setStepColor(red);
    QVERIFY(step.checkNextStep(Function::Loop, red, QColor(), 3));
    QCOMPARE(step.currentStepIndex(), 2);
    QCOMPARE(step.stepColor(), red);

    /* PingPong */
    step.initializeDirection(Function::Forward, red, blue, 3, stripes.data());
    QVERIFY(step.checkNextStep(Function::PingPong, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 1);
    QCOMPARE(step.m_direction, Function::Forward);
    QVERIFY(step.checkNextStep(Function::PingPong, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 2);
    QVERIFY(step.checkNextStep(Function::PingPong, red, blue, 3)); // bounce at the end
    QCOMPARE(step.currentStepIndex(), 1);
    QCOMPARE(step.m_direction, Function::Backward);
    QVERIFY(step.checkNextStep(Function::PingPong, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 0);
    QCOMPARE(step.stepColor(), red);
    QVERIFY(step.checkNextStep(Function::PingPong, red, blue, 3)); // bounce at the start
    QCOMPARE(step.currentStepIndex(), 1);
    QCOMPARE(step.m_direction, Function::Forward);
    // bounce at the end without a valid end color
    step.setCurrentStepIndex(2);
    QVERIFY(step.checkNextStep(Function::PingPong, red, QColor(), 3));
    QCOMPARE(step.currentStepIndex(), 1);
    QCOMPARE(step.m_direction, Function::Backward);

    /* SingleShot, forward: stops after the last step */
    step.initializeDirection(Function::Forward, red, blue, 3, stripes.data());
    QVERIFY(step.checkNextStep(Function::SingleShot, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 1);
    QVERIFY(step.checkNextStep(Function::SingleShot, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 2);
    QVERIFY(step.checkNextStep(Function::SingleShot, red, blue, 3) == false);
    QCOMPARE(step.currentStepIndex(), 2);

    /* SingleShot, backward: stops after the first step */
    step.initializeDirection(Function::Backward, red, blue, 3, stripes.data());
    QVERIFY(step.checkNextStep(Function::SingleShot, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 1);
    QVERIFY(step.checkNextStep(Function::SingleShot, red, blue, 3));
    QCOMPARE(step.currentStepIndex(), 0);
    QVERIFY(step.checkNextStep(Function::SingleShot, red, blue, 3) == false);
    QCOMPARE(step.currentStepIndex(), 0);
}

/****************************************************************************
 * Small helpers
 ****************************************************************************/

void RGBMatrix_Test::controlModeStrings()
{
    QCOMPARE(RGBMatrix::controlModeToString(RGBMatrix::ControlModeRgb), QString("RGB"));
    QCOMPARE(RGBMatrix::controlModeToString(RGBMatrix::ControlModeAmber), QString("Amber"));
    QCOMPARE(RGBMatrix::controlModeToString(RGBMatrix::ControlModeWhite), QString("White"));
    QCOMPARE(RGBMatrix::controlModeToString(RGBMatrix::ControlModeUV), QString("UV"));
    QCOMPARE(RGBMatrix::controlModeToString(RGBMatrix::ControlModeDimmer), QString("Dimmer"));
    QCOMPARE(RGBMatrix::controlModeToString(RGBMatrix::ControlModeShutter), QString("Shutter"));

    QCOMPARE(RGBMatrix::stringToControlMode("RGB"), RGBMatrix::ControlModeRgb);
    QCOMPARE(RGBMatrix::stringToControlMode("Amber"), RGBMatrix::ControlModeAmber);
    QCOMPARE(RGBMatrix::stringToControlMode("White"), RGBMatrix::ControlModeWhite);
    QCOMPARE(RGBMatrix::stringToControlMode("UV"), RGBMatrix::ControlModeUV);
    QCOMPARE(RGBMatrix::stringToControlMode("Dimmer"), RGBMatrix::ControlModeDimmer);
    QCOMPARE(RGBMatrix::stringToControlMode("Shutter"), RGBMatrix::ControlModeShutter);
    QCOMPARE(RGBMatrix::stringToControlMode("Foo"), RGBMatrix::ControlModeRgb);

    RGBMatrix mtx(m_doc);
    QCOMPARE(mtx.controlMode(), RGBMatrix::ControlModeRgb);
    QSignalSpy spy(&mtx, SIGNAL(changed(quint32)));
    mtx.setControlMode(RGBMatrix::ControlModeShutter);
    QCOMPARE(mtx.controlMode(), RGBMatrix::ControlModeShutter);
    QCOMPARE(spy.count(), 1);
}

void RGBMatrix_Test::rgbToGrey()
{
    // Pure greys are returned as-is
    QCOMPARE(RGBMatrix::rgbToGrey(qRgb(0, 0, 0)), uchar(0));
    QCOMPARE(RGBMatrix::rgbToGrey(qRgb(77, 77, 77)), uchar(77));
    QCOMPARE(RGBMatrix::rgbToGrey(qRgb(255, 255, 255)), uchar(255));
    // BT.601 luma weights
    QCOMPARE(RGBMatrix::rgbToGrey(qRgb(255, 0, 0)), uchar(76));
    QCOMPARE(RGBMatrix::rgbToGrey(qRgb(0, 255, 0)), uchar(150));
    QCOMPARE(RGBMatrix::rgbToGrey(qRgb(0, 0, 255)), uchar(29));
    QCOMPARE(RGBMatrix::rgbToGrey(qRgb(255, 255, 0)), uchar(226));
}

void RGBMatrix_Test::iconAndMutex()
{
    RGBMatrix mtx(m_doc);
    // The icon resource is compiled into the application, not into this test,
    // so only check that asking for it is harmless
    QIcon icon = mtx.getIcon();
    Q_UNUSED(icon);

    // The mutex is recursive: locking twice from the same thread must not deadlock
    mtx.algorithmMutex().lock();
    mtx.algorithmMutex().lock();
    mtx.algorithmMutex().unlock();
    mtx.algorithmMutex().unlock();
}

void RGBMatrix_Test::colorEdgeCases()
{
    RGBMatrix mtx(m_doc);
    QCOMPARE(mtx.getColors().count(), int(RGBAlgorithmColorDisplayCount));

    // Negative indices are ignored
    mtx.setColor(-1, Qt::blue);
    QCOMPARE(mtx.getColor(-1), QColor());
    QCOMPARE(mtx.getColors().count(), int(RGBAlgorithmColorDisplayCount));

    // Out of range read
    QCOMPARE(mtx.getColor(RGBAlgorithmColorDisplayCount), QColor());

    // Setting a color beyond the current count grows the vector
    mtx.setColor(RGBAlgorithmColorDisplayCount + 1, Qt::blue);
    QCOMPARE(mtx.getColors().count(), int(RGBAlgorithmColorDisplayCount) + 2);
    QCOMPARE(mtx.getColor(RGBAlgorithmColorDisplayCount + 1), QColor(Qt::blue));
    QCOMPARE(mtx.getColor(RGBAlgorithmColorDisplayCount), QColor());

    // An apiVersion 3 script gets its colors pushed in through rgbMapSetColors,
    // with zeros for the colors the matrix doesn't have
    RGBMatrix mtx2(m_doc);
    mtx2.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Balls"));
    QVERIFY(mtx2.algorithm() != NULL);
    QCOMPARE(mtx2.algorithm()->apiVersion(), 3);
    QCOMPARE(mtx2.algorithm()->acceptColors(), 5);
    mtx2.m_rgbColors.resize(2);
    mtx2.setColor(0, Qt::yellow);
    mtx2.setColor(1, QColor());
    QCOMPARE(mtx2.getColors().count(), 2);
    // No fixture group: the group lookup fails but the colors are still applied
    QVERIFY(mtx2.m_group == NULL);
    mtx2.setFixtureGroup(m_rgbGroup);
    mtx2.setColor(1, Qt::cyan);
    QVERIFY(mtx2.m_group != NULL);
}

void RGBMatrix_Test::durationWithoutAlgorithmOrGroup()
{
    RGBMatrix mtx(m_doc);
    mtx.setDuration(100);

    // Algorithm but no fixture group
    QCOMPARE(mtx.totalDuration(), uint(0));
    mtx.setTotalDuration(2000);
    QCOMPARE(mtx.duration(), uint(100));

    // No algorithm at all
    mtx.setAlgorithm(NULL);
    QVERIFY(mtx.algorithm() == NULL);
    QCOMPARE(mtx.algorithmIndex(), 0);
    QCOMPARE(mtx.stepsCount(), 0);
    QCOMPARE(mtx.totalDuration(), uint(0));
    mtx.setTotalDuration(2000);
    QCOMPARE(mtx.duration(), uint(100));
    mtx.setFixtureGroup(m_rgbGroup);
    QCOMPARE(mtx.stepsCount(), 0);
    QCOMPARE(mtx.totalDuration(), uint(0));

    // previewMap without an algorithm does nothing
    RGBMatrixStep handler;
    mtx.previewMap(0, &handler);
    QCOMPARE(handler.m_map.size(), 0);
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Stripes"));
    mtx.previewMap(0, NULL);
    QCOMPARE(handler.m_map.size(), 0);
}

void RGBMatrix_Test::copyEdgeCases()
{
    RGBMatrix mtx(m_doc);
    QVERIFY(mtx.copyFrom(NULL) == false);

    // A matrix without an algorithm copies as a matrix without an algorithm
    mtx.setAlgorithm(NULL);
    mtx.setDimmerControl(true);
    mtx.setControlMode(RGBMatrix::ControlModeWhite);
    mtx.setFixtureGroup(m_rgbGroup);

    RGBMatrix *copy = qobject_cast<RGBMatrix*> (mtx.createCopy(m_doc, false));
    QVERIFY(copy != NULL);
    QVERIFY(copy->algorithm() == NULL);
    QCOMPARE(copy->dimmerControl(), true);
    QCOMPARE(copy->controlMode(), RGBMatrix::ControlModeWhite);
    QCOMPARE(copy->fixtureGroup(), m_rgbGroup);
    QCOMPARE(copy->components(), m_doc->fixtureGroup(m_rgbGroup)->fixtureList());
    QVERIFY(m_doc->function(copy->id()) == NULL); // not added to the doc
    delete copy;
}

void RGBMatrix_Test::algorithmSwitch()
{
    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup);
    QCOMPARE(mtx.algorithm()->name(), QString("Stripes"));

    // A property set on the current script is cached...
    mtx.setProperty("orientation", "Vertical");
    QVERIFY(mtx.m_properties.contains("orientation"));
    QCOMPARE(mtx.property("orientation"), QString("Vertical"));

    // ...and dropped when switching to a script that doesn't expose it
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Balls"));
    QCOMPARE(mtx.algorithm()->name(), QString("Balls"));
    QVERIFY(mtx.m_properties.contains("orientation") == false);
    QCOMPARE(mtx.property("orientation"), QString());

    // A non-script algorithm has no properties at all
    mtx.setAlgorithm(new RGBPlain(m_doc));
    QCOMPARE(mtx.algorithm()->type(), RGBAlgorithm::Plain);
    QCOMPARE(mtx.property("orientation"), QString());
    mtx.setProperty("foo", "bar");
    QCOMPARE(mtx.property("foo"), QString("bar")); // served from the cache
    QCOMPARE(mtx.stepsCount(), 1);

    // Switching back to a script re-applies the cached properties it knows
    mtx.setProperty("orientation", "Vertical");
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Stripes"));
    QCOMPARE(mtx.property("orientation"), QString("Vertical"));
    QVERIFY(mtx.m_properties.contains("foo") == false);
    // 4x1 group with vertical stripes: one step
    QCOMPARE(mtx.stepsCount(), 1);
}

void RGBMatrix_Test::propertyStepRescale()
{
    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup); // 4x1
    QCOMPARE(mtx.stepsCount(), 4);

    // Pretend the matrix is at the last step, then change a property that
    // changes the step count: the phase (3/4) is preserved and clamped
    mtx.m_stepHandler->setCurrentStepIndex(3);
    mtx.setProperty("orientation", "Vertical");
    QCOMPARE(mtx.stepsCount(), 1);
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0);
    QCOMPARE(mtx.m_continuousPhase, 0.75);

    // Going back to 4 steps rescales the current index from the phase
    mtx.setProperty("orientation", "Horizontal");
    QCOMPARE(mtx.stepsCount(), 4);
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0); // 0/1 -> phase 0
    QCOMPARE(mtx.m_continuousPhase, 0.0);

    // Same step count: nothing is rescaled
    mtx.m_stepHandler->setCurrentStepIndex(2);
    mtx.setProperty("orientation", "Horizontal");
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 2);
}

/****************************************************************************
 * Load & Save extras
 ****************************************************************************/

void RGBMatrix_Test::loadSaveExtra()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Function");
    xmlWriter.writeAttribute("Type", "RGBMatrix");
    xmlWriter.writeAttribute("ID", "77");
    xmlWriter.writeAttribute("Name", "Legacy");
    // Legacy start/end color tags
    xmlWriter.writeTextElement("MonoColor", QString::number(QColor(Qt::green).rgb()));
    xmlWriter.writeTextElement("EndColor", QString::number(QColor(Qt::cyan).rgb()));
    xmlWriter.writeTextElement("DimmerControl", "1");
    xmlWriter.writeTextElement("ControlMode", "Shutter");
    xmlWriter.writeStartElement("Property");
    xmlWriter.writeAttribute("Name", "orientation");
    xmlWriter.writeAttribute("Value", "Vertical");
    xmlWriter.writeEndElement();
    xmlWriter.writeStartElement("Algorithm");
    xmlWriter.writeAttribute("Type", "Script");
    xmlWriter.writeCharacters("Stripes");
    xmlWriter.writeEndElement();
    xmlWriter.writeTextElement("FixtureGroup", QString::number(m_rgbGroup));
    xmlWriter.writeTextElement("TempoType", "Beats");
    xmlWriter.writeTextElement("Foo", "Bar"); // unknown tag, skipped
    xmlWriter.writeEndElement();

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    RGBMatrix mtx(m_doc);
    QVERIFY(mtx.loadXML(xmlReader) == true);
    QCOMPARE(mtx.getColor(0), QColor(Qt::green));
    QCOMPARE(mtx.getColor(1), QColor(Qt::cyan));
    QCOMPARE(mtx.getColor(2), QColor());
    QCOMPARE(mtx.dimmerControl(), true);
    QCOMPARE(mtx.controlMode(), RGBMatrix::ControlModeShutter);
    QCOMPARE(mtx.fixtureGroup(), m_rgbGroup);
    QVERIFY(mtx.algorithm() != NULL);
    QCOMPARE(mtx.algorithm()->name(), QString("Stripes"));
    QCOMPARE(mtx.property("orientation"), QString("Vertical"));
    QCOMPARE(mtx.tempoType(), Function::Beats);
    buffer.close();

    // Save it back: dimmer control and properties must be written, invalid colors skipped
    QBuffer out;
    out.open(QIODevice::WriteOnly | QIODevice::Text);
    xmlWriter.setDevice(&out);
    QVERIFY(mtx.saveXML(&xmlWriter) == true);
    xmlWriter.setDevice(NULL);
    out.close();

    out.open(QIODevice::ReadOnly | QIODevice::Text);
    xmlReader.setDevice(&out);
    xmlReader.readNextStartElement();
    QCOMPARE(xmlReader.name().toString(), QString("Function"));

    int dimmer = 0, property = 0, colors = 0, mode = 0;
    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name().toString() == "DimmerControl")
        {
            QCOMPARE(xmlReader.readElementText(), QString("1"));
            dimmer++;
        }
        else if (xmlReader.name().toString() == "Property")
        {
            QCOMPARE(xmlReader.attributes().value("Name").toString(), QString("orientation"));
            QCOMPARE(xmlReader.attributes().value("Value").toString(), QString("Vertical"));
            property++;
            xmlReader.skipCurrentElement();
        }
        else if (xmlReader.name().toString() == "Color")
        {
            colors++;
            xmlReader.skipCurrentElement();
        }
        else if (xmlReader.name().toString() == "ControlMode")
        {
            QCOMPARE(xmlReader.readElementText(), QString("Shutter"));
            mode++;
        }
        else
        {
            xmlReader.skipCurrentElement();
        }
    }
    QCOMPARE(dimmer, 1);
    QCOMPARE(property, 1);
    QCOMPARE(colors, 2);
    QCOMPARE(mode, 1);
}

/****************************************************************************
 * Running
 ****************************************************************************/

void RGBMatrix_Test::runLoopForward()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup); // 4x1 -> Stripes has 4 steps
    mtx.setDuration(MasterTimer::tick());
    mtx.setFadeInSpeed(0);
    mtx.setFadeOutSpeed(0);
    // A cached property is re-applied to the script when the run starts
    mtx.setProperty("orientation", "Horizontal");
    QCOMPARE(mtx.stepsCount(), 4);
    QCOMPARE(mtx.getColor(0), QColor(Qt::red));

    QVERIFY(mtx.stopped() == true);
    QVERIFY(mtx.isRunning() == false);
    mtx.start(&timer, FunctionParent::master()); // the stub calls preRun()
    QVERIFY(mtx.stopped() == false);
    QVERIFY(mtx.isRunning() == true);
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0);
    QCOMPARE(mtx.m_stepHandler->stepColor(), QColor(Qt::red));
    QVERIFY(mtx.m_runAlgorithm == mtx.m_algorithm);
    QVERIFY(mtx.m_requestEngineCreation == false);
    QCOMPARE(mtx.m_continuousPhase, 0.0);

    // Every write() at duration == tick renders one step and moves to the next one
    for (int step = 0; step < 6; step++)
    {
        mtx.write(&timer, ua);
        QCOMPARE(mtx.m_fadersMap.count(), 1);
        ua[0]->processFaders(MasterTimer::tick());

        for (int x = 0; x < 4; x++)
        {
            int address = 256 + x * 3;
            uchar expected = (x == step % 4) ? 255 : 0;
            QCOMPARE(dmx(ua[0], address), expected);   // red
            QCOMPARE(dmx(ua[0], address + 1), uchar(0)); // green
            QCOMPARE(dmx(ua[0], address + 2), uchar(0)); // blue
        }
        QCOMPARE(mtx.m_stepHandler->currentStepIndex(), (step + 1) % 4);
        QCOMPARE(mtx.elapsed(), uint(0)); // rounded back at every step
    }
    QVERIFY(mtx.stopped() == false);

    // Pausing freezes the step index and the output
    mtx.setPause(true);
    QVERIFY(mtx.isPaused());
    int pausedIndex = mtx.m_stepHandler->currentStepIndex();
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), pausedIndex);
    mtx.setPause(false);

    // The stub's stopFunction() stops the matrix and calls postRun()
    timer.stopFunction(&mtx);
    QVERIFY(mtx.stopped() == true);
    QVERIFY(mtx.isRunning() == false);
    QCOMPARE(mtx.m_fadersMap.count(), 0);
    QVERIFY(timer.m_functionList.isEmpty());

    qDeleteAll(ua);
}

void RGBMatrix_Test::runOrders()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup); // 4 steps
    mtx.setDuration(MasterTimer::tick());
    mtx.setFadeInSpeed(0);
    mtx.setFadeOutSpeed(0);
    mtx.setColor(1, Qt::blue);

    struct Case
    {
        Function::RunOrder order;
        Function::Direction direction;
        QList<int> indices; // step index after each write
        bool stopsAtEnd;
    };

    const QList<Case> cases = {
        { Function::Loop, Function::Backward, {2, 1, 0, 3, 2}, false },
        { Function::PingPong, Function::Forward, {1, 2, 3, 2, 1, 0, 1}, false },
        { Function::PingPong, Function::Backward, {2, 1, 0, 1, 2, 3, 2}, false },
        { Function::SingleShot, Function::Forward, {1, 2, 3, 3}, true },
        { Function::SingleShot, Function::Backward, {2, 1, 0, 0}, true },
    };

    foreach (const Case &c, cases)
    {
        mtx.setRunOrder(c.order);
        mtx.setDirection(c.direction);
        mtx.start(&timer, FunctionParent::master());
        QCOMPARE(mtx.m_stepHandler->currentStepIndex(), c.direction == Function::Forward ? 0 : 3);
        QCOMPARE(mtx.m_stepHandler->stepColor(),
                 c.direction == Function::Forward ? QColor(Qt::red) : QColor(Qt::blue));

        for (int i = 0; i < c.indices.count(); i++)
        {
            mtx.write(&timer, ua);
            QCOMPARE(mtx.m_stepHandler->currentStepIndex(), c.indices.at(i));
            if (c.stopsAtEnd && i == c.indices.count() - 1)
                QVERIFY(mtx.stopped() == true);
            else
                QVERIFY(mtx.stopped() == false);
        }

        timer.stopFunction(&mtx);
        QVERIFY(mtx.stopped() == true);
    }

    qDeleteAll(ua);
}

void RGBMatrix_Test::runControlModes()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    // Plain color fills the whole map with the step color, so every mode
    // renders exactly rgbToGrey(color) on the channels it drives.
    const QColor color(200, 100, 50);
    const uchar grey = RGBMatrix::rgbToGrey(color.rgb());
    QVERIFY(grey != 0);

    struct Case
    {
        RGBMatrix::ControlMode mode;
        bool legacyDimmer;
        quint32 group;
        QList<QPair<int, uchar> > expected; // absolute address -> value
    };

    // Multi fixture @300: Dimmer(0) R(1) G(2) B(3) W(4) A(5) UV(6) Shutter(7)
    // MasterHead @320: Master(0) HeadDim(1) R(2) G(3) B(4)
    // CMY @340: C(0) M(1) Y(2)
    const QList<Case> cases = {
        { RGBMatrix::ControlModeRgb, false, m_multiGroup,
          { {301, 200}, {302, 100}, {303, 50}, {300, 0}, {304, 0} } },
        { RGBMatrix::ControlModeWhite, false, m_multiGroup, { {304, grey}, {301, 0} } },
        { RGBMatrix::ControlModeAmber, false, m_multiGroup, { {305, grey}, {301, 0} } },
        { RGBMatrix::ControlModeUV, false, m_multiGroup, { {306, grey}, {301, 0} } },
        { RGBMatrix::ControlModeShutter, false, m_multiGroup, { {307, grey}, {301, 0} } },
        // No master dimmer: the head dimmer carries the greyscale value
        { RGBMatrix::ControlModeDimmer, false, m_multiGroup, { {300, grey}, {301, 0} } },
        // Legacy dimmer control flag takes over any non-RGB/shutter mode
        { RGBMatrix::ControlModeWhite, true, m_multiGroup, { {300, grey}, {304, 0}, {301, 0} } },
        // Master dimmer present: it fades, the head dimmer is just opened
        { RGBMatrix::ControlModeDimmer, false, m_masterHeadGroup, { {320, grey}, {321, 255}, {322, 0} } },
        // CMY fixtures are driven through the CMY conversion of the color
        { RGBMatrix::ControlModeRgb, false, m_cmyGroup,
          { {340, uchar(color.cyan())}, {341, uchar(color.magenta())}, {342, uchar(color.yellow())} } },
        // A fixture without the requested channel is left alone
        { RGBMatrix::ControlModeWhite, false, m_rgbGroup, { {256, 0}, {257, 0}, {258, 0} } },
        { RGBMatrix::ControlModeShutter, false, m_rgbGroup, { {256, 0}, {257, 0}, {258, 0} } },
        { RGBMatrix::ControlModeRgb, false, m_multiGroup, { {301, 200} } },
    };

    foreach (const Case &c, cases)
    {
        ua[0]->reset();

        RGBMatrix mtx(m_doc);
        mtx.setAlgorithm(new RGBPlain(m_doc));
        mtx.setColor(0, color);
        mtx.setFixtureGroup(c.group);
        mtx.setDuration(MasterTimer::tick());
        mtx.setFadeInSpeed(0);
        mtx.setFadeOutSpeed(0);
        mtx.setControlMode(c.mode);
        mtx.setDimmerControl(c.legacyDimmer);

        mtx.start(&timer, FunctionParent::master());
        mtx.write(&timer, ua);
        ua[0]->processFaders(MasterTimer::tick());

        for (int i = 0; i < c.expected.count(); i++)
        {
            const QPair<int, uchar> &e = c.expected.at(i);
            QVERIFY2(dmx(ua[0], e.first) == e.second,
                     qPrintable(QString("mode %1 legacy %2 group %3: address %4 is %5, expected %6")
                                .arg(c.mode).arg(c.legacyDimmer).arg(c.group).arg(e.first)
                                .arg(dmx(ua[0], e.first)).arg(e.second)));
        }

        timer.stopFunction(&mtx);
    }

    // A black step turns the master dimmer off and closes the head dimmer too
    {
        ua[0]->reset();
        RGBMatrix mtx(m_doc);
        mtx.setAlgorithm(new RGBPlain(m_doc));
        mtx.setColor(0, Qt::black);
        mtx.setFixtureGroup(m_masterHeadGroup);
        mtx.setDuration(MasterTimer::tick());
        mtx.setFadeInSpeed(0);
        mtx.setFadeOutSpeed(0);
        mtx.setControlMode(RGBMatrix::ControlModeDimmer);
        mtx.start(&timer, FunctionParent::master());
        mtx.write(&timer, ua);
        ua[0]->processFaders(MasterTimer::tick());
        QCOMPARE(dmx(ua[0], 320), uchar(0));
        QCOMPARE(dmx(ua[0], 321), uchar(0));
        timer.stopFunction(&mtx);
    }

    // A fixture group larger than the map: heads outside the map are skipped
    {
        ua[0]->reset();
        RGBMatrix mtx(m_doc);
        mtx.setAlgorithm(new RGBPlain(m_doc));
        mtx.setFixtureGroup(m_rgbSquareGroup); // 2x2
        mtx.setDuration(MasterTimer::tick());
        mtx.setFadeInSpeed(0);
        mtx.setFadeOutSpeed(0);
        mtx.start(&timer, FunctionParent::master());
        // Shrink the group after preRun so the algorithm renders a 1x1 map
        FixtureGroup *grp = m_doc->fixtureGroup(m_rgbSquareGroup);
        grp->setSize(QSize(1, 1));
        // A head pointing to a fixture that doesn't exist is skipped as well
        QVERIFY(grp->assignHead(QLCPoint(1, 1), GroupHead(12345, 0)) == true);
        mtx.write(&timer, ua);
        ua[0]->processFaders(MasterTimer::tick());
        grp->setSize(QSize(2, 2));
        QCOMPARE(dmx(ua[0], 280), uchar(255)); // (0,0) rendered
        QCOMPARE(dmx(ua[0], 283), uchar(0));   // (1,0) outside the map
        QCOMPARE(dmx(ua[0], 286), uchar(0));   // (0,1) outside the map
        timer.stopFunction(&mtx);
    }

    qDeleteAll(ua);
}

void RGBMatrix_Test::runFades()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup);
    mtx.setDuration(MasterTimer::tick());
    mtx.setFadeInSpeed(100);
    mtx.setFadeOutSpeed(50);

    mtx.start(&timer, FunctionParent::master());
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_fadersMap.count(), 1);
    QSharedPointer<GenericFader> fader = mtx.m_fadersMap.first();
    QVERIFY(fader.isNull() == false);
    QCOMPARE(fader->name(), mtx.name());
    QCOMPARE(fader->parentFunctionID(), mtx.id());

    // Channels going up fade with the fade in time. Channels that are already
    // at their (zero) target are left untouched, so their fade time stays 0.
    int fadingIn = 0, untouched = 0;
    QHashIterator<quint32, FadeChannel> it(fader->channels());
    while (it.hasNext())
    {
        it.next();
        const FadeChannel &fc = it.value();
        if (fc.target() == 255)
        {
            QCOMPARE(fc.fadeTime(), uint(100));
            fadingIn++;
        }
        else
        {
            QCOMPARE(fc.target(), uint(0));
            QCOMPARE(fc.fadeTime(), uint(0));
            untouched++;
        }
    }
    QCOMPARE(fadingIn, 1);   // red of column 0
    QCOMPARE(untouched, 11); // everything else

    // After one tick of a 100ms fade the value is well below full
    ua[0]->processFaders(MasterTimer::tick());
    uchar partial = dmx(ua[0], 256);
    QVERIFY(partial > 0);
    QVERIFY(partial < 255);

    // Next step: column 1 starts fading in and column 0 fades out with the
    // fade out time, from wherever it got to
    mtx.write(&timer, ua);
    it = QHashIterator<quint32, FadeChannel>(fader->channels());
    fadingIn = 0;
    int fadingOut = 0;
    while (it.hasNext())
    {
        it.next();
        const FadeChannel &fc = it.value();
        if (fc.target() == 255)
        {
            QCOMPARE(fc.fadeTime(), uint(100));
            fadingIn++;
        }
        else if (fc.fadeTime() != 0)
        {
            QCOMPARE(fc.fadeTime(), uint(50));
            QCOMPARE(fc.start(), uint(partial));
            fadingOut++;
        }
    }
    QCOMPARE(fadingIn, 1);
    QCOMPARE(fadingOut, 1);

    // Fade out on postRun: the faders are handed over to the universe,
    // with every channel fading to zero in the fade out time
    mtx.stop(FunctionParent::master());
    mtx.postRun(&timer, ua);
    QCOMPARE(mtx.m_fadersMap.count(), 0);
    QVERIFY(fader->isFadingOut());
    QVERIFY(mtx.isRunning() == false);
    it = QHashIterator<quint32, FadeChannel>(fader->channels());
    while (it.hasNext())
    {
        it.next();
        QCOMPARE(it.value().target(), uint(0));
        QCOMPARE(it.value().fadeTime(), uint(50));
    }

    // The fade out time given at start() overrides the function's own one
    mtx.start(&timer, FunctionParent::master(), 0, Function::defaultSpeed(), 200);
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_fadersMap.count(), 1);
    fader = mtx.m_fadersMap.first();
    timer.stopFunction(&mtx);
    QVERIFY(fader->isFadingOut());
    it = QHashIterator<quint32, FadeChannel>(fader->channels());
    while (it.hasNext())
    {
        it.next();
        QCOMPARE(it.value().fadeTime(), uint(200));
    }

    // Beat tempo: the fade out is expressed in 1/1000 beats
    timer.requestBpmNumber(120); // 500ms per beat
    mtx.setTempoType(Function::Beats);
    mtx.setDuration(1000);
    mtx.setFadeOutSpeed(2000);
    mtx.start(&timer, FunctionParent::master());
    mtx.write(&timer, ua);
    fader = mtx.m_fadersMap.first();
    timer.stopFunction(&mtx);
    QVERIFY(fader->isFadingOut());
    it = QHashIterator<quint32, FadeChannel>(fader->channels());
    while (it.hasNext())
    {
        it.next();
        QCOMPARE(it.value().fadeTime(), uint(1000));
    }

    qDeleteAll(ua);
}

void RGBMatrix_Test::runBeats()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);
    timer.requestBpmNumber(120); // 500ms per beat
    QCOMPARE(timer.beatTimeDuration(), 500);

    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup);
    mtx.setTempoType(Function::Beats);
    mtx.setDuration(1000); // one step per beat (durations are in 1/1000 beats)
    mtx.setFadeInSpeed(0);
    mtx.setFadeOutSpeed(0);

    mtx.start(&timer, FunctionParent::master());
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0);

    // A beat moves to the next step and restarts the elapsed time
    timer.m_beatRequested = true;
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_stepBeatDuration, uint(500));
    QCOMPARE(mtx.elapsedBeats(), uint(0)); // reset together with the elapsed time
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 1);
    QCOMPARE(mtx.elapsed(), uint(0));

    // Without beats, the step advances when the step time has elapsed
    timer.m_beatRequested = false;
    int ticks = 0;
    while (mtx.m_stepHandler->currentStepIndex() == 1 && ticks < 100)
    {
        mtx.write(&timer, ua);
        ticks++;
    }
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 2);
    QCOMPARE(ticks, int(500 / MasterTimer::tick()));
    QVERIFY(mtx.elapsed() < 500);

    // Two beats per step: only every second beat advances
    timer.stopFunction(&mtx);
    mtx.setDuration(2000);
    mtx.start(&timer, FunctionParent::master());
    timer.m_beatRequested = true;
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0);
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 1);
    timer.m_beatRequested = false;
    timer.stopFunction(&mtx);

    qDeleteAll(ua);
}

void RGBMatrix_Test::runTap()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup);
    mtx.setDuration(40);

    // Tapping a stopped matrix does nothing
    mtx.tap();
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0);

    mtx.start(&timer, FunctionParent::master());
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0);

    // A tap right after the previous step is filtered out
    mtx.tap();
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 0);

    // After a quarter of the step duration a tap advances the step
    QTest::qSleep(30);
    mtx.tap();
    QCOMPARE(mtx.m_stepHandler->currentStepIndex(), 1);
    QCOMPARE(mtx.elapsed(), uint(0));

    timer.stopFunction(&mtx);

    // Without an algorithm a tap has nothing to advance
    RGBMatrix mtx2(m_doc);
    mtx2.setFixtureGroup(m_rgbGroup);
    mtx2.setDuration(40);
    mtx2.setAlgorithm(NULL);
    mtx2.start(&timer, FunctionParent::master());
    QTest::qSleep(30);
    mtx2.tap();
    QCOMPARE(mtx2.m_stepHandler->currentStepIndex(), 0);
    timer.stopFunction(&mtx2);

    qDeleteAll(ua);
}

void RGBMatrix_Test::runEarlyExits()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    // No fixture group: preRun() stops the matrix right away
    RGBMatrix mtx(m_doc);
    mtx.setDuration(MasterTimer::tick());
    mtx.start(&timer, FunctionParent::master());
    QVERIFY(mtx.stopped() == true);
    QVERIFY(mtx.isRunning() == false); // Function::preRun() is never reached
    // and so does write()
    mtx.m_stop = false;
    mtx.write(&timer, ua);
    QVERIFY(mtx.stopped() == true);
    QCOMPARE(mtx.m_fadersMap.count(), 0);
    timer.stopFunction(&mtx);

    // A zero duration produces no output
    mtx.setFixtureGroup(m_rgbGroup);
    mtx.setDuration(0);
    mtx.start(&timer, FunctionParent::master());
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_fadersMap.count(), 0);
    QCOMPARE(mtx.elapsed(), uint(0));
    timer.stopFunction(&mtx);

    // An invalid script (apiVersion 0) produces no output either
    mtx.setDuration(MasterTimer::tick());
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "No such script"));
    QVERIFY(mtx.algorithm() != NULL);
    QCOMPARE(mtx.algorithm()->apiVersion(), 0);
    mtx.start(&timer, FunctionParent::master());
    QVERIFY(mtx.m_requestEngineCreation == false);
    mtx.m_requestEngineCreation = true; // as if the algorithm changed while running
    mtx.write(&timer, ua);
    QVERIFY(mtx.m_requestEngineCreation == false);
    QCOMPARE(mtx.m_fadersMap.count(), 0);
    timer.stopFunction(&mtx);

    // No algorithm at all: preRun() doesn't initialize the step handler and
    // write() has nothing to run. Use a fresh matrix, whose run algorithm has
    // never been set (see the note on m_runAlgorithm in the report).
    RGBMatrix mtx2(m_doc);
    mtx2.setFixtureGroup(m_rgbGroup);
    mtx2.setDuration(MasterTimer::tick());
    mtx2.setAlgorithm(NULL);
    QVERIFY(mtx2.m_runAlgorithm == NULL);
    mtx2.m_stepHandler->setCurrentStepIndex(3);
    mtx2.start(&timer, FunctionParent::master());
    QCOMPARE(mtx2.m_stepHandler->currentStepIndex(), 3);
    mtx2.write(&timer, ua);
    QCOMPARE(mtx2.m_fadersMap.count(), 0);
    QCOMPARE(mtx2.elapsed(), uint(0)); // returns before the elapsed time is counted
    timer.stopFunction(&mtx2);

    qDeleteAll(ua);
}

void RGBMatrix_Test::runImageAlgorithm()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    // An image algorithm without an image: the map stays empty and no
    // channel is touched, but the run cycle completes normally
    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup);
    mtx.setDuration(MasterTimer::tick());
    mtx.setAlgorithm(new RGBImage(m_doc));
    QCOMPARE(mtx.algorithm()->type(), RGBAlgorithm::Image);
    QCOMPARE(mtx.stepsCount(), 1);

    mtx.start(&timer, FunctionParent::master());
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_stepHandler->m_map.count(), 0);
    QCOMPARE(mtx.m_fadersMap.count(), 0);
    timer.stopFunction(&mtx);

    qDeleteAll(ua);
}

/****************************************************************************
 * Attributes
 ****************************************************************************/

void RGBMatrix_Test::attributes()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup);
    mtx.setDuration(MasterTimer::tick());
    mtx.setFadeInSpeed(0);
    mtx.setFadeOutSpeed(0);

    // Fixed attributes: intensity, 5 colors and the pattern
    QList<Attribute> attrs = mtx.attributes();
    QVERIFY(attrs.count() >= int(RGBMatrix::ScriptPropertyAttr));
    QCOMPARE(attrs.at(Function::Intensity).m_name, tr("Intensity"));
    QCOMPARE(attrs.at(RGBMatrix::Color1Attr).m_name, tr("Color 1"));
    QCOMPARE(attrs.at(RGBMatrix::Color5Attr).m_name, tr("Color 5"));
    QCOMPARE(attrs.at(RGBMatrix::PatternAttr).m_name, tr("Pattern"));
    QCOMPARE(int(mtx.getAttributeValue(RGBMatrix::Color1Attr)), int(QColor(Qt::red).rgb() & 0x00FFFFFF));
    QCOMPARE(int(mtx.getAttributeValue(RGBMatrix::Color2Attr)), -1);
    QCOMPARE(int(mtx.getAttributeValue(RGBMatrix::PatternAttr)), mtx.algorithmIndex());
    // Stripes exposes its "orientation" list property
    QCOMPARE(attrs.at(RGBMatrix::ScriptPropertyAttr).m_name, QString("Orientation"));
    QCOMPARE(attrs.at(RGBMatrix::ScriptPropertyAttr).m_max, 1.0);

    // Color attributes drive the matrix colors
    QCOMPARE(mtx.adjustAttribute(qreal(QColor(Qt::green).rgb() & 0x00FFFFFF), RGBMatrix::Color1Attr),
             int(RGBMatrix::Color1Attr));
    QCOMPARE(mtx.getColor(0), QColor(Qt::green));
    QCOMPARE(mtx.adjustAttribute(qreal(QColor(Qt::blue).rgb() & 0x00FFFFFF), RGBMatrix::Color2Attr),
             int(RGBMatrix::Color2Attr));
    QCOMPARE(mtx.getColor(1), QColor(Qt::blue));
    // -1 clears a color
    QCOMPARE(mtx.adjustAttribute(-1.0, RGBMatrix::Color2Attr), int(RGBMatrix::Color2Attr));
    QCOMPARE(mtx.getColor(1), QColor());
    // and setColor() keeps the attribute in sync
    mtx.setColor(2, Qt::cyan);
    QCOMPARE(int(mtx.getAttributeValue(RGBMatrix::Color3Attr)), int(QColor(Qt::cyan).rgb() & 0x00FFFFFF));
    mtx.setColor(2, QColor());
    QCOMPARE(int(mtx.getAttributeValue(RGBMatrix::Color3Attr)), -1);

    // The pattern attribute selects an algorithm by index
    QStringList algos = RGBAlgorithm::algorithms(m_doc);
    int plainIdx = algos.indexOf("Plain Color");
    int stripesIdx = algos.indexOf("Stripes");
    QVERIFY(plainIdx >= 0 && stripesIdx >= 0);
    QCOMPARE(mtx.adjustAttribute(plainIdx, RGBMatrix::PatternAttr), int(RGBMatrix::PatternAttr));
    QCOMPARE(mtx.algorithm()->name(), QString("Plain Color"));
    QCOMPARE(mtx.algorithm()->getColor(0), QColor(Qt::green)); // colors carried over
    // Script property attributes are gone with the script
    QVERIFY(mtx.attributes().count() == int(RGBMatrix::ScriptPropertyAttr));
    QCOMPARE(mtx.adjustAttribute(stripesIdx, RGBMatrix::PatternAttr), int(RGBMatrix::PatternAttr));
    QCOMPARE(mtx.algorithm()->name(), QString("Stripes"));
    QCOMPARE(mtx.attributes().count(), int(RGBMatrix::ScriptPropertyAttr) + 1);
    // setAlgorithm() keeps the attribute in sync too
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Plain Color"));
    QCOMPARE(int(mtx.getAttributeValue(RGBMatrix::PatternAttr)), plainIdx);
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Stripes"));
    QCOMPARE(int(mtx.getAttributeValue(RGBMatrix::PatternAttr)), stripesIdx);

    // Re-applying the current style is a no-op
    mtx.applyStyleAttributes();
    QCOMPARE(mtx.algorithm()->name(), QString("Stripes"));
    QCOMPARE(mtx.getColor(0), QColor(Qt::green));

    // Out of range pattern indices are clamped by the attribute range
    QCOMPARE(mtx.adjustAttribute(-5.0, RGBMatrix::PatternAttr), int(RGBMatrix::PatternAttr));
    QCOMPARE(mtx.algorithm()->name(), algos.first());
    QCOMPARE(mtx.adjustAttribute(algos.count() + 10, RGBMatrix::PatternAttr), int(RGBMatrix::PatternAttr));
    QCOMPARE(mtx.algorithm()->name(), algos.last());
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Stripes"));

    // Intensity is forwarded to the running faders
    mtx.start(&timer, FunctionParent::master());
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_fadersMap.count(), 1);
    QSharedPointer<GenericFader> fader = mtx.m_fadersMap.first();
    QCOMPARE(fader->intensity(), 1.0);
    QCOMPARE(mtx.adjustAttribute(0.5, Function::Intensity), int(Function::Intensity));
    QCOMPARE(fader->intensity(), 0.5);
    ua[0]->processFaders(MasterTimer::tick());
    QCOMPARE(dmx(ua[0], 257), uchar(128)); // green at half intensity

    // An attribute override goes through the same path
    int overrideId = mtx.requestAttributeOverride(Function::Intensity, 0.5);
    QVERIFY(overrideId >= 0);
    QCOMPARE(mtx.adjustAttribute(0.5, overrideId), int(Function::Intensity));
    QCOMPARE(fader->intensity(), 0.25);
    mtx.releaseAttributeOverride(overrideId);

    timer.stopFunction(&mtx);
    qDeleteAll(ua);
}

void RGBMatrix_Test::scriptPropertyAttributes()
{
    // A synthetic script exposing every property type: a slider can drive
    // lists (with more than one value), ranges and floats, but not strings
    // or single-value lists.
    QString code(
        "(function() {"
        "  var algo = new Object;"
        "  algo.apiVersion = 2;"
        "  algo.name = 'PropAttrTest';"
        "  algo.author = 'test';"
        "  algo.speed = 5; algo.amount = 0.5; algo.mode = 'A'; algo.label = 'x'; algo.single = 'Only';"
        "  algo.properties = new Array();"
        "  algo.properties.push('name:speed|type:range|display:Speed|values:1,10|write:setSpeed|read:getSpeed');"
        "  algo.properties.push('name:amount|type:float|display:Amount|write:setAmount|read:getAmount');"
        "  algo.properties.push('name:mode|type:list|display:Mode|values:A,B,C|write:setMode|read:getMode');"
        "  algo.properties.push('name:label|type:string|display:Label|write:setLabel|read:getLabel');"
        "  algo.properties.push('name:single|type:list|display:Single|values:Only|write:setSingle|read:getSingle');"
        "  algo.setSpeed = function(v) { algo.speed = parseInt(v); };"
        "  algo.getSpeed = function() { return algo.speed; };"
        "  algo.setAmount = function(v) { algo.amount = parseFloat(v); };"
        "  algo.getAmount = function() { return algo.amount; };"
        "  algo.setMode = function(v) { algo.mode = v; };"
        "  algo.getMode = function() { return algo.mode; };"
        "  algo.setLabel = function(v) { algo.label = v; };"
        "  algo.getLabel = function() { return algo.label; };"
        "  algo.setSingle = function(v) { algo.single = v; };"
        "  algo.getSingle = function() { return algo.single; };"
        "  algo.rgbMap = function(width, height, rgb, step) {"
        "    var map = new Array(); for (var y = 0; y < height; y++) { map[y] = new Array();"
        "    for (var x = 0; x < width; x++) map[y][x] = rgb; } return map; };"
        "  algo.rgbMapStepCount = function(width, height) { return width; };"
        "  return algo;"
        "})()");

    RGBScript *script = new RGBScript(m_doc);
    script->m_fileName = "propattr_test.js";
    script->m_contents = code;
    QVERIFY(script->evaluate());
    QCOMPARE(script->properties().count(), 5);

    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup);
    mtx.setAlgorithm(script);

    QList<Attribute> attrs = mtx.attributes();
    QCOMPARE(attrs.count(), int(RGBMatrix::ScriptPropertyAttr) + 3);
    const int speedAttr = RGBMatrix::ScriptPropertyAttr;
    const int amountAttr = RGBMatrix::ScriptPropertyAttr + 1;
    const int modeAttr = RGBMatrix::ScriptPropertyAttr + 2;
    QCOMPARE(attrs.at(speedAttr).m_name, QString("Speed"));
    QCOMPARE(attrs.at(speedAttr).m_min, 1.0);
    QCOMPARE(attrs.at(speedAttr).m_max, 10.0);
    QCOMPARE(attrs.at(speedAttr).m_value, 5.0);
    QCOMPARE(attrs.at(amountAttr).m_name, QString("Amount"));
    QCOMPARE(attrs.at(amountAttr).m_min, 0.0);
    QCOMPARE(attrs.at(amountAttr).m_max, 1.0);
    QCOMPARE(attrs.at(amountAttr).m_value, 0.5);
    QCOMPARE(attrs.at(modeAttr).m_name, QString("Mode"));
    QCOMPARE(attrs.at(modeAttr).m_max, 2.0);
    QCOMPARE(attrs.at(modeAttr).m_value, 0.0);

    // Range: rounded to an integer
    QCOMPARE(mtx.adjustAttribute(7.6, speedAttr), speedAttr);
    QCOMPARE(mtx.property("speed"), QString("8"));
    // Float: passed as-is
    QCOMPARE(mtx.adjustAttribute(0.25, amountAttr), amountAttr);
    QCOMPARE(mtx.property("amount"), QString("0.25"));
    // List: the value is an index into the list, clamped
    QCOMPARE(mtx.adjustAttribute(1.0, modeAttr), modeAttr);
    QCOMPARE(mtx.property("mode"), QString("B"));
    QCOMPARE(mtx.adjustAttribute(2.0, modeAttr), modeAttr);
    QCOMPARE(mtx.property("mode"), QString("C"));

    // Re-applying the same values doesn't touch the script
    mtx.applyStyleAttributes();
    QCOMPARE(mtx.property("speed"), QString("8"));
    QCOMPARE(mtx.property("amount"), QString("0.25"));
    QCOMPARE(mtx.property("mode"), QString("C"));

    // Replacing the algorithm unregisters its attributes
    mtx.setAlgorithm(RGBAlgorithm::algorithm(m_doc, "Plain Color"));
    QCOMPARE(mtx.attributes().count(), int(RGBMatrix::ScriptPropertyAttr));
}

void RGBMatrix_Test::blendMode()
{
    QScopedPointer<GrandMaster> gm(new GrandMaster());
    QList<Universe*> ua;
    ua.append(new Universe(0, gm.data()));
    MasterTimerStub timer(m_doc, ua);

    RGBMatrix mtx(m_doc);
    mtx.setFixtureGroup(m_rgbGroup);
    mtx.setDuration(MasterTimer::tick());
    QCOMPARE(mtx.blendMode(), Universe::NormalBlend);

    QSignalSpy spy(&mtx, SIGNAL(changed(quint32)));
    mtx.setBlendMode(Universe::NormalBlend); // no change, no signal
    QCOMPARE(spy.count(), 0);

    mtx.start(&timer, FunctionParent::master());
    mtx.write(&timer, ua);
    QSharedPointer<GenericFader> fader = mtx.m_fadersMap.first();
    QCOMPARE(fader->m_blendMode, Universe::NormalBlend);

    mtx.setBlendMode(Universe::AdditiveBlend);
    QCOMPARE(mtx.blendMode(), Universe::AdditiveBlend);
    QCOMPARE(fader->m_blendMode, Universe::AdditiveBlend);
    QCOMPARE(spy.count(), 1);

    timer.stopFunction(&mtx);

    // New faders pick up the current blend mode
    mtx.start(&timer, FunctionParent::master());
    mtx.write(&timer, ua);
    QCOMPARE(mtx.m_fadersMap.first()->m_blendMode, Universe::AdditiveBlend);
    timer.stopFunction(&mtx);

    qDeleteAll(ua);
}

QTEST_MAIN(RGBMatrix_Test)
