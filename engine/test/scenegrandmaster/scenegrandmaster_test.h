/*
  Q Light Controller Plus - Unit test
  scenegrandmaster_test.h

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

#ifndef SCENEGRANDMASTER_TEST_H
#define SCENEGRANDMASTER_TEST_H

#include <QObject>

class Doc;

/**
 * End-to-end regression test for the "enable a scene, then move a fader" flow:
 * patch a real fixture, start a Scene through the real MasterTimer/Doc/
 * InputOutputMap pipeline, adjust the scene's own intensity attribute, and
 * check the DMX that would actually be sent (Universe::postGMValues(), i.e.
 * after Grand Master is applied) - not just the pre-GM buffer that most
 * existing engine tests stop at.
 */
class SceneGrandMaster_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void intensityFaderAndGrandMasterCombine();
};

#endif
