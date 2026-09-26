/*
  Q Light Controller Plus - Unit test
  rgbimage_test.h

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

#ifndef RGBIMAGE_TEST_H
#define RGBIMAGE_TEST_H

#include <QTemporaryDir>
#include <QObject>
#include <QImage>

class Doc;

class RGBImage_Test final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void defaults();
    void filename();
    void imageData();
    void animationStyles();
    void offsets();
    void stepCount();
    void mapStatic();
    void mapHorizontal();
    void mapVertical();
    void mapAnimation();
    void copyAndClone();
    void animatedGif();
    void saveXML();
    void loadXML();
    void loadXMLMalformed();

private:
    /** Expected map value for image pixel ($x, $y), wrapped around the image size */
    uint pixel(int x, int y) const;

private:
    Doc *m_doc;
    QTemporaryDir m_dir;
    QString m_pngPath;
    QImage m_image;
};

#endif
