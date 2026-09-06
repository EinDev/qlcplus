/*
  Q Light Controller Plus
  slowclickapplication.h

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

#ifndef SLOWCLICKAPPLICATION_H
#define SLOWCLICKAPPLICATION_H

#include <QApplication>

/**
 * A QApplication that times every mouse click's full synchronous processing
 * (press/release/double-click) and logs a warning when one is slow enough to
 * be felt as UI stutter. QApplication::notify() is the single choke point
 * every event passes through before delivery, so timing around the base
 * class call captures the whole chain - including any QML onClicked/onPressed
 * handler that runs synchronously as part of QQuickWindow's own event
 * delivery, not just Qt-side dispatch overhead.
 *
 * Meant to stay in the codebase permanently, as a lightweight complement to
 * FreezeWatchdog (freezewatchdog.h): FreezeWatchdog only catches genuine
 * hangs (main thread silent for kFreezeThresholdMs, e.g. 12s). This catches
 * the shorter, sub-freeze-threshold stalls that show up as "clicks feel
 * slow" rather than "the app froze" - too brief to ever reach the freeze
 * watchdog's threshold, but still worth a log line to correlate against a
 * reported symptom.
 *
 * Like every other qDebug/qWarning in this codebase, the warning is only
 * visible when launched with -d (see CLAUDE.md: without it, Qt's default
 * Windows handler routes everything to OutputDebugString and it's lost) -
 * this doesn't add a separate always-on logging path.
 */
class SlowClickApplication : public QApplication
{
    Q_OBJECT

public:
    using QApplication::QApplication;

protected:
    bool notify(QObject *receiver, QEvent *event) override;
};

#endif // SLOWCLICKAPPLICATION_H
