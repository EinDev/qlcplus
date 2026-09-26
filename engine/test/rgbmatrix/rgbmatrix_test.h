/*
  Q Light Controller
  rgbmatrix_test.h

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

#ifndef RGBMATRIX_TEST_H
#define RGBMATRIX_TEST_H

#include <QObject>
#include <QList>
#include <QSize>

#ifdef QT_QML_LIB
  #include "rgbscriptv4.h"
#else
  #include "rgbscript.h"
#endif

class Doc;
class QLCFixtureDef;

class RGBMatrix_Test final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void initial();
    void group();
    void color();
    void copy();
    void previewMaps();
    void property();
    void loadSave();

    /* Added coverage */
    void stepHandler();
    void controlModeStrings();
    void rgbToGrey();
    void iconAndMutex();
    void colorEdgeCases();
    void durationWithoutAlgorithmOrGroup();
    void copyEdgeCases();
    void algorithmSwitch();
    void propertyStepRescale();
    void loadSaveExtra();
    void runLoopForward();
    void runOrders();
    void runControlModes();
    void runFades();
    void runBeats();
    void runTap();
    void runEarlyExits();
    void runImageAlgorithm();
    void attributes();
    void scriptPropertyAttributes();
    void blendMode();
    void scriptColorsFromScript();
    void propertyStepRescaleClamp();
    void runAnimatedImageAlgorithm();
    void attributeHelpersOutOfRange();

private:
    /** Create a fixture group of $size, filled with fixtures using $def, starting
     *  at DMX $address on universe 0. Returns the group ID. */
    quint32 makeRig(QLCFixtureDef *def, const QSize &size, quint32 address);

private:
    Doc* m_doc;

    QLCFixtureDef *m_rgbDef;
    QLCFixtureDef *m_multiDef;
    QLCFixtureDef *m_masterHeadDef;
    QLCFixtureDef *m_cmyDef;

    quint32 m_rgbGroup;        // 4x1, RGB fixtures @ 256
    quint32 m_rgbSquareGroup;  // 2x2, RGB fixtures @ 280
    quint32 m_multiGroup;      // 1x1, Dimmer+RGB+W+A+UV+Shutter fixture @ 300
    quint32 m_masterHeadGroup; // 1x1, master dimmer + head dimmer + RGB @ 320
    quint32 m_cmyGroup;        // 1x1, CMY fixture @ 340
};

#endif
