/*
  Q Light Controller Plus - Unit test
  gradient_test.cpp

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
#include <QColor>
#include <QImage>

#include "gradient_test.h"
#include "gradient.h"

void Gradient_Test::fullGradient()
{
    QImage img = Gradient::getRGBGradient();
    QVERIFY(img.isNull() == false);
    QCOMPARE(img.size(), QSize(256, 256));
    QCOMPARE(img.format(), QImage::Format_RGB32);

    /* Every column goes from black at the top to white at the bottom,
       through its own hue in the middle */
    foreach (int x, QList<int>() << 0 << 100 << 200 << 255)
    {
        QVERIFY(img.pixelColor(x, 0).lightness() < 8);
        QVERIFY(img.pixelColor(x, 255).lightness() > 247);
        QVERIFY(img.pixelColor(x, 127).saturation() > 200);
    }

    /* The image is built once and then reused as-is */
    QImage again = Gradient::getRGBGradient();
    QCOMPARE(again, img);
}

void Gradient_Test::scaledGradient()
{
    QImage img = Gradient::getRGBGradient(64, 32);
    QVERIFY(img.isNull() == false);
    QCOMPARE(img.size(), QSize(64, 32));
    QVERIFY(img.pixelColor(0, 0).lightness() < 8);
    QVERIFY(img.pixelColor(63, 0).lightness() < 8);

    /* Scaling doesn't touch the cached full size image */
    QCOMPARE(Gradient::getRGBGradient().size(), QSize(256, 256));
}

QTEST_MAIN(Gradient_Test)
