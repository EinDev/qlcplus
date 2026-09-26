/*
  Q Light Controller Plus - Unit test
  qlcinputsource_test.cpp

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

#include "qlcinputsource_test.h"

#define protected public
#include "qlcinputsource.h"
#undef protected

#include "qlcinputfeedback.h"

void QLCInputSource_Test::init()
{
    m_universes.clear();
    m_channels.clear();
    m_values.clear();
}

void QLCInputSource_Test::slotInputValue(quint32 universe, quint32 channel, uchar value, const QString& key)
{
    Q_UNUSED(key)
    m_universes << universe;
    m_channels << channel;
    m_values << value;
}

void QLCInputSource_Test::initial()
{
    QLCInputSource src;
    QCOMPARE(src.universe(), QLCInputSource::invalidUniverse);
    QCOMPARE(src.channel(), QLCInputSource::invalidChannel);
    QCOMPARE(src.id(), QLCInputSource::invalidID);
    QCOMPARE(src.page(), ushort(QLCInputSource::invalidChannel >> 16));
    QCOMPARE(src.isValid(), false);
    QCOMPARE(src.workingMode(), QLCInputSource::Absolute);
    QCOMPARE(src.sensitivity(), 20);
    QCOMPARE(src.sendExtraPressRelease(), false);
    QCOMPARE(src.needsUpdate(), false);
    QCOMPARE(src.isRunning(), false);

    QCOMPARE(src.feedbackValue(QLCInputFeedback::LowerValue), uchar(0));
    QCOMPARE(src.feedbackValue(QLCInputFeedback::UpperValue), uchar(UCHAR_MAX));
    QCOMPARE(src.feedbackValue(QLCInputFeedback::MonitorValue), uchar(UCHAR_MAX));
    QCOMPARE(src.feedbackValue(QLCInputFeedback::Undefinded), uchar(0));

    QCOMPARE(src.feedbackExtraParams(QLCInputFeedback::LowerValue), QVariant(-1));
    QCOMPARE(src.feedbackExtraParams(QLCInputFeedback::UpperValue), QVariant(-1));
    QCOMPARE(src.feedbackExtraParams(QLCInputFeedback::MonitorValue), QVariant(-1));
    QCOMPARE(src.feedbackExtraParams(QLCInputFeedback::Undefinded), QVariant(0));

    QLCInputSource src2(3, 42);
    QCOMPARE(src2.universe(), quint32(3));
    QCOMPARE(src2.channel(), quint32(42));
    QCOMPARE(src2.id(), QLCInputSource::invalidID);
    QCOMPARE(src2.page(), ushort(0));
    QCOMPARE(src2.isValid(), true);
    QCOMPARE(src2.workingMode(), QLCInputSource::Absolute);
    QCOMPARE(src2.sensitivity(), 20);
    QCOMPARE(src2.feedbackValue(QLCInputFeedback::LowerValue), uchar(0));
    QCOMPARE(src2.feedbackValue(QLCInputFeedback::UpperValue), uchar(UCHAR_MAX));
    QCOMPARE(src2.feedbackValue(QLCInputFeedback::MonitorValue), uchar(UCHAR_MAX));
}

void QLCInputSource_Test::universeChannelPage()
{
    QLCInputSource src;

    src.setUniverse(3);
    QCOMPARE(src.universe(), quint32(3));
    QCOMPARE(src.isValid(), false);

    src.setChannel(0x1234);
    QCOMPARE(src.channel(), quint32(0x1234));
    QCOMPARE(src.page(), ushort(0));
    QCOMPARE(src.isValid(), true);

    // the page lives in the upper 16 bits of the channel
    src.setPage(2);
    QCOMPARE(src.page(), ushort(2));
    QCOMPARE(src.channel(), quint32((2 << 16) | 0x1234));
    QCOMPARE(src.isValid(), true);

    src.setPage(0xFFFF);
    QCOMPARE(src.page(), ushort(0xFFFF));
    QCOMPARE(src.channel() & 0xFFFF, quint32(0x1234));

    src.setPage(0);
    QCOMPARE(src.page(), ushort(0));
    QCOMPARE(src.channel(), quint32(0x1234));

    src.setID(7);
    QCOMPARE(src.id(), quint32(7));

    src.setUniverse(QLCInputSource::invalidUniverse);
    QCOMPARE(src.isValid(), false);
    src.setUniverse(0);
    src.setChannel(QLCInputSource::invalidChannel);
    QCOMPARE(src.isValid(), false);
}

void QLCInputSource_Test::feedbackValues()
{
    QLCInputSource src(0, 1);

    src.setFeedbackValue(QLCInputFeedback::LowerValue, 10);
    src.setFeedbackValue(QLCInputFeedback::UpperValue, 200);
    src.setFeedbackValue(QLCInputFeedback::MonitorValue, 100);
    src.setFeedbackValue(QLCInputFeedback::Undefinded, 55);
    QCOMPARE(src.feedbackValue(QLCInputFeedback::LowerValue), uchar(10));
    QCOMPARE(src.feedbackValue(QLCInputFeedback::UpperValue), uchar(200));
    QCOMPARE(src.feedbackValue(QLCInputFeedback::MonitorValue), uchar(100));
    QCOMPARE(src.feedbackValue(QLCInputFeedback::Undefinded), uchar(0));

    src.setFeedbackExtraParams(QLCInputFeedback::LowerValue, QVariant("/osc/lower"));
    src.setFeedbackExtraParams(QLCInputFeedback::UpperValue, QVariant(5));
    src.setFeedbackExtraParams(QLCInputFeedback::MonitorValue, QVariant("/osc/monitor"));
    src.setFeedbackExtraParams(QLCInputFeedback::Undefinded, QVariant("ignored"));
    QCOMPARE(src.feedbackExtraParams(QLCInputFeedback::LowerValue).toString(), QString("/osc/lower"));
    QCOMPARE(src.feedbackExtraParams(QLCInputFeedback::UpperValue).toInt(), 5);
    QCOMPARE(src.feedbackExtraParams(QLCInputFeedback::MonitorValue).toString(), QString("/osc/monitor"));
    QCOMPARE(src.feedbackExtraParams(QLCInputFeedback::Undefinded), QVariant(0));
}

void QLCInputSource_Test::feedbackObject()
{
    QLCInputFeedback fb;
    QCOMPARE(fb.type(), QLCInputFeedback::Undefinded);
    QCOMPARE(fb.value(), uchar(0));
    QCOMPARE(fb.extraParams(), QVariant(-1));

    fb.setType(QLCInputFeedback::UpperValue);
    fb.setValue(77);
    fb.setExtraParams(QVariant("osc"));
    QCOMPARE(fb.type(), QLCInputFeedback::UpperValue);
    QCOMPARE(fb.value(), uchar(77));
    QCOMPARE(fb.extraParams().toString(), QString("osc"));

    QLCInputFeedback *copy = fb.createCopy();
    QVERIFY(copy != NULL);
    QVERIFY(copy != &fb);
    QCOMPARE(copy->type(), QLCInputFeedback::UpperValue);
    QCOMPARE(copy->value(), uchar(77));
    QCOMPARE(copy->extraParams().toString(), QString("osc"));

    // the copy is independent
    fb.setValue(1);
    QCOMPARE(copy->value(), uchar(77));
    delete copy;
}

void QLCInputSource_Test::absoluteMode()
{
    QLCInputSource src(1, 2);
    connect(&src, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
            this, SLOT(slotInputValue(quint32,quint32,uchar,QString)));

    // absolute is the default: switching to it without a running thread is a no-op
    src.setWorkingMode(QLCInputSource::Absolute);
    QCOMPARE(src.workingMode(), QLCInputSource::Absolute);
    QCOMPARE(src.isRunning(), false);
    QCOMPARE(src.needsUpdate(), false);

    src.setSensitivity(5);
    QCOMPARE(src.sensitivity(), 5);

    // absolute values are just stored, nothing is emitted
    src.updateInputValue(50);
    QCOMPARE(src.m_inputValue, uchar(50));
    QVERIFY(m_values.isEmpty());

    src.updateOuputValue(60);
    QCOMPARE(src.m_outputValue, uchar(60));
    QVERIFY(m_values.isEmpty());

    src.setSendExtraPressRelease(true);
    QCOMPARE(src.sendExtraPressRelease(), true);
    QCOMPARE(src.needsUpdate(), true);
    src.setSendExtraPressRelease(false);
    QCOMPARE(src.needsUpdate(), false);
}

void QLCInputSource_Test::extraPressRelease()
{
    QLCInputSource src(1, 2);
    connect(&src, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
            this, SLOT(slotInputValue(quint32,quint32,uchar,QString)));

    src.setFeedbackValue(QLCInputFeedback::UpperValue, 200);
    src.setFeedbackValue(QLCInputFeedback::LowerValue, 10);
    src.setSendExtraPressRelease(true);

    // a single input event becomes a synthetic press (upper) + release (lower)
    src.updateInputValue(255);
    QCOMPARE(m_values.size(), 2);
    QCOMPARE(m_universes.at(0), quint32(1));
    QCOMPARE(m_channels.at(0), quint32(2));
    QCOMPARE(m_values.at(0), uchar(200));
    QCOMPARE(m_universes.at(1), quint32(1));
    QCOMPARE(m_channels.at(1), quint32(2));
    QCOMPARE(m_values.at(1), uchar(10));

    src.updateInputValue(0);
    QCOMPARE(m_values.size(), 4);
    QCOMPARE(m_values.at(2), uchar(200));
    QCOMPARE(m_values.at(3), uchar(10));
}

void QLCInputSource_Test::encoderMode()
{
    QLCInputSource src(0, 5);
    connect(&src, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
            this, SLOT(slotInputValue(quint32,quint32,uchar,QString)));

    // straight to encoder mode: no thread, sensitivity is kept as is
    src.setWorkingMode(QLCInputSource::Encoder);
    QCOMPARE(src.workingMode(), QLCInputSource::Encoder);
    QCOMPARE(src.isRunning(), false);
    QCOMPARE(src.needsUpdate(), true);
    QCOMPARE(src.sensitivity(), 20);

    // a raw value above the previous one steps the output up by the sensitivity
    src.updateInputValue(10);
    QCOMPARE(m_values.size(), 1);
    QCOMPARE(m_universes.at(0), quint32(0));
    QCOMPARE(m_channels.at(0), quint32(5));
    QCOMPARE(m_values.at(0), uchar(20));
    QCOMPARE(src.sensitivity(), 20);

    // a lower raw value steps it down
    src.updateInputValue(5);
    QCOMPARE(m_values.size(), 2);
    QCOMPARE(m_values.at(1), uchar(0));
    QCOMPARE(src.sensitivity(), -20);

    // the same raw value keeps the last direction, clamped at 0
    src.updateInputValue(5);
    QCOMPARE(m_values.size(), 3);
    QCOMPARE(m_values.at(2), uchar(0));
    QCOMPARE(src.sensitivity(), -20);

    // finer steps
    src.setSensitivity(1);
    src.updateInputValue(100);
    QCOMPARE(m_values.size(), 4);
    QCOMPARE(m_values.at(3), uchar(1));
    QCOMPARE(src.sensitivity(), 1);

    // the output value is kept in sync with the item using this source,
    // and the step is clamped at full scale
    src.updateOuputValue(250);
    src.setSensitivity(10);
    src.updateInputValue(200);
    QCOMPARE(m_values.size(), 5);
    QCOMPARE(m_values.at(4), uchar(255));

    src.updateInputValue(150);
    QCOMPARE(m_values.size(), 6);
    QCOMPARE(m_values.at(5), uchar(245));
}

void QLCInputSource_Test::relativeMode()
{
    QLCInputSource src(2, 9);
    // the values are emitted from the source's own thread: deliver them
    // to this thread through the event loop
    connect(&src, SIGNAL(inputValueChanged(quint32,quint32,uchar,QString)),
            this, SLOT(slotInputValue(quint32,quint32,uchar,QString)), Qt::QueuedConnection);

    src.setWorkingMode(QLCInputSource::Relative);
    QCOMPARE(src.workingMode(), QLCInputSource::Relative);
    QCOMPARE(src.needsUpdate(), true);
    QTRY_VERIFY_WITH_TIMEOUT(src.isRunning() == true, 5000);

    // switching to relative again while running changes nothing
    src.setWorkingMode(QLCInputSource::Relative);
    QCOMPARE(src.isRunning(), true);

    // the input is primed to the center value, so nothing moves yet
    QTest::qWait(200);
    QVERIFY(m_values.isEmpty());

    // an off-center input ramps the value from the current output (0)
    // by (input - center) / sensitivity per step
    src.updateInputValue(200);
    QTRY_VERIFY_WITH_TIMEOUT(m_values.size() >= 3, 5000);
    QCOMPARE(m_universes.first(), quint32(2));
    QCOMPARE(m_channels.first(), quint32(9));
    QCOMPARE(m_values.at(0), uchar(3));
    QCOMPARE(m_values.at(1), uchar(7));
    QCOMPARE(m_values.at(2), uchar(10));
    for (int i = 1; i < m_values.size(); i++)
        QVERIFY(m_values.at(i) > m_values.at(i - 1));

    // syncing the output value re-bases the ramp, centering the input stops it
    src.updateOuputValue(200);
    src.updateInputValue(127);
    QTest::qWait(200);
    int settled = m_values.size();
    QTest::qWait(200);
    QCOMPARE(m_values.size(), settled);

    // moving the other way ramps down from the synced output value
    src.updateInputValue(50);
    QTRY_VERIFY_WITH_TIMEOUT(m_values.size() > settled, 5000);
    QVERIFY(m_values.last() < 200);
    QVERIFY(m_values.last() >= 180);

    // encoder mode stops the thread and resets the sensitivity to single steps
    src.setWorkingMode(QLCInputSource::Encoder);
    QCOMPARE(src.workingMode(), QLCInputSource::Encoder);
    QCOMPARE(src.isRunning(), false);
    QCOMPARE(src.sensitivity(), 1);

    // and it can be restarted, then stopped by absolute mode
    src.setWorkingMode(QLCInputSource::Relative);
    QTRY_VERIFY_WITH_TIMEOUT(src.isRunning() == true, 5000);
    src.setWorkingMode(QLCInputSource::Absolute);
    QCOMPARE(src.workingMode(), QLCInputSource::Absolute);
    QCOMPARE(src.isRunning(), false);
    QCOMPARE(src.sensitivity(), 1);
}

void QLCInputSource_Test::destroyWhileRunning()
{
    QLCInputSource *src = new QLCInputSource(0, 1);
    src->setWorkingMode(QLCInputSource::Relative);
    QTRY_VERIFY_WITH_TIMEOUT(src->isRunning() == true, 5000);

    // the destructor stops and joins the thread
    delete src;
}

QTEST_GUILESS_MAIN(QLCInputSource_Test)
