/*
  Q Light Controller Plus - Unit test
  qlcinputsource_test.h

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

#ifndef QLCINPUTSOURCE_TEST_H
#define QLCINPUTSOURCE_TEST_H

#include <QObject>
#include <QList>

class QLCInputSource_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void initial();
    void universeChannelPage();
    void feedbackValues();
    void feedbackObject();
    void absoluteMode();
    void extraPressRelease();
    void encoderMode();
    void relativeMode();
    void destroyWhileRunning();

    /* Receiver for the values emitted by a relative-mode source thread.
       Public, so QTest doesn't mistake it for a test case. */
public slots:
    void slotInputValue(quint32 universe, quint32 channel, uchar value, const QString& key);

private:
    QList<quint32> m_universes;
    QList<quint32> m_channels;
    QList<uchar> m_values;
};

#endif
