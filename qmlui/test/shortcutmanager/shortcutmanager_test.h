/*
  Q Light Controller - Unit test
  shortcutmanager_test.h

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

#ifndef SHORTCUTMANAGER_TEST_H
#define SHORTCUTMANAGER_TEST_H

#include <QObject>
#include <QTemporaryDir>

class ShortcutManager_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void sequenceTextForRegisteredAction();
    void sequenceTextForUnknownActionIsEmpty();
    void sequenceTextFollowsOverride();
    void sequenceTextFollowsResetToDefault();
    void descriptionForAction();
    void shortcutFiredCarriesSequenceAndDescription();
    void shortcutFiredRespectsScope();
    void notifyButtonClickedEmitsHint();
    void notifyButtonClickedIgnoresUnknownAndUnbound();
    void notifyButtonClickedRespectsHintsToggle();
    void hintsEnabledDefaultsToTrueAndNotifies();

private:
    /** Keeps both the overrides JSON and the QSettings ini away from the
     *  real user profile for the whole run */
    QTemporaryDir m_tempDir;
    QString m_overridesFile;
};

#endif
