/*
  Q Light Controller Plus
  slowclickapplication.cpp

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

#include <QElapsedTimer>
#include <QDebug>

#include "slowclickapplication.h"

namespace {

// Roughly "a few dropped frames" at 60fps (16.7ms/frame) - below this, a
// click's processing time isn't perceptible as UI stutter and would just be
// log noise on every ordinary click.
constexpr qint64 kSlowClickThresholdMs = 50;

QString describeReceiver(QObject *receiver)
{
    if (receiver == nullptr)
        return QStringLiteral("(null)");

    const QString name = receiver->objectName();
    const QString className = QString::fromLatin1(receiver->metaObject()->className());
    return name.isEmpty() ? className : QStringLiteral("%1 (%2)").arg(name, className);
}

QString describeEventType(QEvent::Type type)
{
    switch (type)
    {
        case QEvent::MouseButtonPress:      return QStringLiteral("press");
        case QEvent::MouseButtonRelease:    return QStringLiteral("release");
        case QEvent::MouseButtonDblClick:   return QStringLiteral("double-click");
        default:                            return QStringLiteral("?");
    }
}

} // namespace

bool SlowClickApplication::notify(QObject *receiver, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease &&
        type != QEvent::MouseButtonDblClick)
    {
        return QApplication::notify(receiver, event);
    }

    QElapsedTimer timer;
    timer.start();
    const bool handled = QApplication::notify(receiver, event);
    const qint64 elapsed = timer.elapsed();

    if (elapsed >= kSlowClickThresholdMs)
    {
        qWarning().noquote() << QStringLiteral("[SlowClick] %1 on %2 took %3 ms")
                                     .arg(describeEventType(type), describeReceiver(receiver))
                                     .arg(elapsed);
    }

    return handled;
}
