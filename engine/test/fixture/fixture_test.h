/*
  Q Light Controller - Unit test
  fixture_test.h

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

#ifndef FIXTURE_TEST_H
#define FIXTURE_TEST_H

#include <QObject>

class Doc;

class Fixture_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void id();
    void name();
    void address();
    void lessThan();
    void type();
    void dimmer();
    void rgbPanel();
    void rgbPanel16bit();
    void fixtureDef();
    void channels();
    void degrees();
    void heads();
    void loadWrongRoot();
    void loadFixtureDef();
    void loadFixtureDefWrongChannels();
    void loadDimmer();
    void loadWrongAddress();
    void loadWrongUniverse();
    void loadWrongID();
    void loader();
    void save();
    //void status();

    // Appended after loader()/save(): loader() asserts the shared m_doc is
    // still empty, so anything that adds fixtures to a Doc below uses a local one.
    void crossUniverse();
    void setChannelsReplacesGenericDef();
    void channelLookupMisses();
    void positionNoMovement();
    void positionRelative();
    void axisValues();
    void zoom();
    void channelValuesCache();
    void fadeAndPrecedenceLists();
    void channelModifiers();
    void iconResources();
    void aliasChannels();
    void componentStrings();
    void saveBare();
    void saveLoadOptionalParts();
    void saveLoadRGBPanel();
    void loadMissingMode();
    void loadZeroChannels();
    void loadUnknownTag();
    void loaderFailures();

private:
    Doc* m_doc;
};

#endif
