/*
  Q Light Controller Plus - Unit test
  rgbaudio_test.cpp

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
#include <QBuffer>

#define protected public
#define private public
#include "audiocapture.h"
#include "rgbaudio.h"
#undef private
#undef protected

#include "rgbaudio_test.h"
#include "doc.h"

/* These tests never let the audio capture thread start: the first rgbMap()
 * round, which registers the number of bands with the capture device (and
 * with it the device itself), is simulated by setting the band count
 * directly, and the spectrum data is fed through the slot the capture
 * device would otherwise signal. */

namespace
{
    /** Wire $audio to the Doc's (never started) capture device as if it had
     *  already been through its first rgbMap() round with $bands bands */
    void attach(Doc *doc, RGBAudio &audio, int bands)
    {
        QSharedPointer<AudioCapture> capture = doc->audioInputCapture();
        QVERIFY(capture.isNull() == false);
        QVERIFY(capture->isRunning() == false);

        audio.setAudioCapture(capture.data());
        QVERIFY(audio.m_audioInput == capture.data());
        QCOMPARE(audio.m_bandsNumber, -1);
        audio.m_bandsNumber = bands;
    }
}

void RGBAudio_Test::initTestCase()
{
    m_doc = new Doc(this);
}

void RGBAudio_Test::cleanupTestCase()
{
    delete m_doc;
}

void RGBAudio_Test::defaults()
{
    RGBAudio audio(m_doc);
    QCOMPARE(audio.name(), QString("Audio Spectrum"));
    QCOMPARE(audio.author(), QString("Massimo Callegari"));
    QCOMPARE(audio.apiVersion(), 1);
    QCOMPARE(audio.type(), RGBAlgorithm::Audio);
    QCOMPARE(audio.acceptColors(), 2);
    QCOMPARE(audio.rgbMapStepCount(QSize(5, 5)), 1);
    QVERIFY(audio.doc() == m_doc);
    QVERIFY(audio.m_audioInput == NULL);
    QCOMPARE(audio.m_bandsNumber, -1);
    QCOMPARE(audio.m_barColors.count(), 0);

    // The raw color API is not used by this algorithm
    audio.rgbMapSetColors(QVector<uint>() << 1 << 2);
    QCOMPARE(audio.rgbMapGetColors(), QVector<uint>());
}

void RGBAudio_Test::copyAndClone()
{
    RGBAudio audio(m_doc);
    audio.setColors(QVector<QColor>() << Qt::red << Qt::blue);

    // Copies start from scratch: no capture, no bands
    RGBAudio copy(audio);
    QVERIFY(copy.doc() == m_doc);
    QVERIFY(copy.m_audioInput == NULL);
    QCOMPARE(copy.m_bandsNumber, -1);
    QCOMPARE(copy.getColor(0), QColor());

    RGBAlgorithm *clone = audio.clone();
    QVERIFY(clone != NULL);
    QVERIFY(clone != &audio);
    QCOMPARE(clone->type(), RGBAlgorithm::Audio);
    QCOMPARE(clone->name(), QString("Audio Spectrum"));
    delete clone;
}

void RGBAudio_Test::barColors()
{
    RGBAudio audio(m_doc);

    // No height: nothing to compute
    audio.calculateColors(0);
    QCOMPARE(audio.m_barColors.count(), 0);

    // Start color only: every row gets it
    audio.setColors(QVector<QColor>() << Qt::red);
    QCOMPARE(audio.getColor(0), QColor(Qt::red));
    QCOMPARE(audio.getColor(1), QColor());
    audio.calculateColors(3);
    QCOMPARE(audio.m_barColors.count(), 3);
    for (int i = 0; i < 3; i++)
        QCOMPARE(audio.m_barColors.at(i), QColor(Qt::red).rgb());

    // A single row can't be a gradient either
    audio.setColors(QVector<QColor>() << Qt::red << Qt::blue);
    QCOMPARE(audio.m_barColors.count(), 0); // invalidated by setColors()
    audio.calculateColors(1);
    QCOMPARE(audio.m_barColors.count(), 1);
    QCOMPARE(audio.m_barColors.at(0), QColor(Qt::red).rgb());

    // Start to end gradient over 4 rows, top to bottom
    audio.setColors(QVector<QColor>() << Qt::red << Qt::blue);
    audio.calculateColors(4);
    QCOMPARE(audio.m_barColors.count(), 4);
    QCOMPARE(audio.m_barColors.at(0), qRgb(255, 0, 0));
    QCOMPARE(audio.m_barColors.at(1), qRgb(170, 0, 85));
    QCOMPARE(audio.m_barColors.at(2), qRgb(85, 0, 170));
    QCOMPARE(audio.m_barColors.at(3), qRgb(0, 0, 255));

    // Only two colors are accepted
    audio.setColors(QVector<QColor>() << Qt::red << Qt::blue << Qt::green);
    QCOMPARE(audio.getColor(2), QColor());
}

void RGBAudio_Test::spectrumData()
{
    RGBAudio audio(m_doc);
    attach(m_doc, audio, 3);

    // Data for a different number of bands is ignored
    double wrong[2] = { 1.0, 2.0 };
    audio.slotAudioBarsChanged(wrong, 2, 2.0, 100);
    QCOMPARE(audio.m_spectrumValues.count(), 0);
    QCOMPARE(audio.m_maxMagnitude, 0.0);

    double bands[3] = { 10.0, 20.0, 30.0 };
    audio.slotAudioBarsChanged(bands, 3, 30.0, 1234);
    QCOMPARE(audio.m_spectrumValues.count(), 3);
    QCOMPARE(audio.m_spectrumValues.at(0), 10.0);
    QCOMPARE(audio.m_spectrumValues.at(2), 30.0);
    QCOMPARE(audio.m_maxMagnitude, 30.0);
    QCOMPARE(audio.m_volumePower, quint32(1234));

    // The capture device signal is wired to the slot
    QVERIFY(QObject::disconnect(audio.m_audioInput, SIGNAL(dataProcessed(double*,int,double,quint32)),
                                &audio, SLOT(slotAudioBarsChanged(double*,int,double,quint32))));
}

void RGBAudio_Test::rgbMapBars()
{
    RGBAudio audio(m_doc);
    audio.setColors(QVector<QColor>() << Qt::red << Qt::blue);
    attach(m_doc, audio, 3);

    // Full volume: column 0 at the maximum magnitude fills the whole
    // height, column 1 at half fills half of it, column 2 is silent
    double bands[3] = { 100.0, 50.0, 0.0 };
    audio.slotAudioBarsChanged(bands, 3, 100.0, 0x7FFF);

    RGBMap map;
    audio.rgbMap(QSize(3, 4), qRgb(1, 2, 3), 0, map);
    QCOMPARE(map.count(), 4);
    QCOMPARE(audio.m_barColors.count(), 4); // computed on first use

    for (int y = 0; y < 4; y++)
    {
        QCOMPARE(map[y].count(), 3);
        QCOMPARE(map[y][0], audio.m_barColors.at(y));
        QCOMPARE(map[y][1], y >= 2 ? audio.m_barColors.at(y) : uint(0));
        QCOMPARE(map[y][2], uint(0));
    }
    QCOMPARE(map[0][0], qRgb(255, 0, 0));
    QCOMPARE(map[3][0], qRgb(0, 0, 255));

    // Half volume halves every bar; a magnitude above the maximum is clamped
    // (the volume height is integer arithmetic: 0x4000 * 4 / 0x7FFF = 2)
    double loud[3] = { 100.0, 400.0, 25.0 };
    audio.slotAudioBarsChanged(loud, 3, 100.0, 0x4000);
    audio.rgbMap(QSize(3, 4), 0, 0, map);
    for (int y = 0; y < 4; y++)
    {
        QCOMPARE(map[y][0], y >= 2 ? audio.m_barColors.at(y) : uint(0));
        QCOMPARE(map[y][1], y >= 0 ? audio.m_barColors.at(y) : uint(0));
        QCOMPARE(map[y][2], uint(0));
    }

    // The bar colors are recomputed when the colors change
    audio.setColors(QVector<QColor>() << Qt::green);
    QCOMPARE(audio.m_barColors.count(), 0);
    audio.rgbMap(QSize(3, 4), 0, 0, map);
    QCOMPARE(audio.m_barColors.count(), 4);
    QCOMPARE(map[3][0], QColor(Qt::green).rgb());

    // Leaving the algorithm attached with its bands still registered is
    // handled by the destructor
}

void RGBAudio_Test::rgbMapNoSignal()
{
    RGBAudio audio(m_doc);
    audio.setColors(QVector<QColor>() << Qt::red << Qt::blue);
    attach(m_doc, audio, 2);

    // No magnitude at all: everything stays off
    double bands[2] = { 5.0, 5.0 };
    audio.slotAudioBarsChanged(bands, 2, 0.0, 0x7FFF);
    RGBMap map;
    audio.rgbMap(QSize(2, 3), 0, 0, map);
    QCOMPARE(map.count(), 3);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 2; x++)
            QCOMPARE(map[y][x], uint(0));

    // No volume: the bars have no height either
    audio.slotAudioBarsChanged(bands, 2, 5.0, 0);
    audio.rgbMap(QSize(2, 3), 0, 0, map);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 2; x++)
            QCOMPARE(map[y][x], uint(0));

    // No spectrum data received yet (e.g. right after attaching)
    audio.m_spectrumValues.clear();
    audio.rgbMap(QSize(2, 3), 0, 0, map);
    for (int y = 0; y < 3; y++)
        for (int x = 0; x < 2; x++)
            QCOMPARE(map[y][x], uint(0));
}

void RGBAudio_Test::postRun()
{
    RGBAudio audio(m_doc);
    attach(m_doc, audio, 4);

    // Detaches from the capture device and forgets the bands
    audio.postRun();
    QVERIFY(audio.m_audioInput == NULL);
    QCOMPARE(audio.m_bandsNumber, -1);
    QVERIFY(QObject::disconnect(m_doc->audioInputCapture().data(), SIGNAL(dataProcessed(double*,int,double,quint32)),
                                &audio, SLOT(slotAudioBarsChanged(double*,int,double,quint32))) == false);

    // Running it again on a detached algorithm is harmless
    audio.postRun();
    QVERIFY(audio.m_audioInput == NULL);
    QCOMPARE(audio.m_bandsNumber, -1);

    // Still no capture thread was ever started
    QVERIFY(m_doc->audioInputCapture()->isRunning() == false);
}

void RGBAudio_Test::loadSaveXML()
{
    RGBAudio audio(m_doc);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(audio.saveXML(&xmlWriter) == true);
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QCOMPARE(xmlReader.name().toString(), QString("Algorithm"));
    QCOMPARE(xmlReader.attributes().value("Type").toString(), QString("Audio"));
    QVERIFY(xmlReader.readNextStartElement() == false);
    buffer.close();

    // Round trip
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    xmlReader.setDevice(&buffer);
    xmlReader.readNextStartElement();
    RGBAudio loaded(m_doc);
    QVERIFY(loaded.loadXML(xmlReader) == true);
    buffer.close();

    // Wrong root tag
    {
        QBuffer bad;
        bad.open(QIODevice::WriteOnly | QIODevice::Text);
        xmlWriter.setDevice(&bad);
        xmlWriter.writeStartElement("Foo");
        xmlWriter.writeAttribute("Type", "Audio");
        xmlWriter.writeEndElement();
        xmlWriter.setDevice(NULL);
        bad.close();

        bad.open(QIODevice::ReadOnly | QIODevice::Text);
        xmlReader.setDevice(&bad);
        xmlReader.readNextStartElement();
        QVERIFY(loaded.loadXML(xmlReader) == false);
        xmlReader.setDevice(NULL);
    }

    // Wrong algorithm type
    {
        QBuffer bad;
        bad.open(QIODevice::WriteOnly | QIODevice::Text);
        xmlWriter.setDevice(&bad);
        xmlWriter.writeStartElement("Algorithm");
        xmlWriter.writeAttribute("Type", "Plain");
        xmlWriter.writeEndElement();
        xmlWriter.setDevice(NULL);
        bad.close();

        bad.open(QIODevice::ReadOnly | QIODevice::Text);
        xmlReader.setDevice(&bad);
        xmlReader.readNextStartElement();
        QVERIFY(loaded.loadXML(xmlReader) == false);
        xmlReader.setDevice(NULL);
    }
}

void RGBAudio_Test::firstRound()
{
    QSharedPointer<AudioCapture> capture = m_doc->audioInputCapture();
    QVERIFY(capture.isNull() == false);
    QVERIFY(capture->isRunning() == false);

    /* Registering the very first band count would start the capture
     * device: pretend another listener already registered one, so that
     * the device is left alone */
    BandsData other;
    other.m_registerCounter = 1;
    capture->m_fftMagnitudeMap[16] = other;

    RGBAudio audio(m_doc);
    QVERIFY(audio.m_audioInput == NULL);
    QCOMPARE(audio.m_bandsNumber, -1);

    /* The first round only attaches the capture and asks it for as many
     * bands as the map is wide: the map itself stays black */
    RGBMap map;
    audio.rgbMap(QSize(4, 3), 0xff0000, 0, map);
    QCOMPARE(audio.m_audioInput, capture.data());
    QCOMPARE(audio.m_bandsNumber, 4);
    QVERIFY(capture->isRunning() == false);
    QVERIFY(capture->m_fftMagnitudeMap.contains(4));
    QCOMPARE(capture->m_fftMagnitudeMap.value(4).m_registerCounter, 1);
    QCOMPARE(map.size(), 3);
    for (int y = 0; y < 3; y++)
    {
        QCOMPARE(map[y].size(), 4);
        for (int x = 0; x < 4; x++)
            QCOMPARE(map[y][x], uint(0));
    }

    /* The following rounds keep the same capture, without data yet */
    audio.rgbMap(QSize(4, 3), 0xff0000, 0, map);
    QCOMPARE(audio.m_audioInput, capture.data());
    QCOMPARE(audio.m_bandsNumber, 4);
    QVERIFY(capture->isRunning() == false);
    QCOMPARE(map[2][0], uint(0));

    capture->m_fftMagnitudeMap.remove(4);
    capture->m_fftMagnitudeMap.remove(16);
}

QTEST_GUILESS_MAIN(RGBAudio_Test)
