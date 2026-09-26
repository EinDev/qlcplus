/*
  Q Light Controller Plus - Unit test
  rgbimage_test.cpp

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
#include <QImageReader>
#include <QFileInfo>
#include <QBuffer>
#include <QDir>

#define private public
#include "rgbimage.h"
#undef private

#include "rgbimage_test.h"
#include "doc.h"

/* The test image is 4x3 with a distinct, opaque color per pixel, except for
 * the bottom-right one which is fully transparent.
 *
 * QTEST_MAIN (a QGuiApplication) is required: the animated source goes
 * through QMovie::currentImage(), which is backed by a QPixmap. */
#define IMG_W 4
#define IMG_H 3

void RGBImage_Test::initTestCase()
{
    m_doc = new Doc(this);

    QVERIFY(m_dir.isValid());
    m_doc->setWorkspacePath(m_dir.path());

    m_image = QImage(IMG_W, IMG_H, QImage::Format_ARGB32);
    for (int y = 0; y < IMG_H; y++)
        for (int x = 0; x < IMG_W; x++)
            m_image.setPixel(x, y, qRgb(10 * x + 1, 20 * y + 2, 3));
    m_image.setPixel(IMG_W - 1, IMG_H - 1, qRgba(200, 200, 200, 0));

    m_pngPath = QDir(m_dir.path()).absoluteFilePath("test.png");
    QVERIFY(m_image.save(m_pngPath, "PNG"));
}

void RGBImage_Test::cleanupTestCase()
{
    delete m_doc;
}

uint RGBImage_Test::pixel(int x, int y) const
{
    x %= IMG_W;
    y %= IMG_H;
    if (x == IMG_W - 1 && y == IMG_H - 1)
        return 0; // transparent pixels are rendered as 0
    return m_image.pixel(x, y);
}

void RGBImage_Test::defaults()
{
    RGBImage image(m_doc);
    QCOMPARE(image.name(), QString("Image"));
    QCOMPARE(image.author(), QString("Jano Svitok"));
    QCOMPARE(image.apiVersion(), 1);
    QCOMPARE(image.type(), RGBAlgorithm::Image);
    QCOMPARE(image.acceptColors(), 0);
    QCOMPARE(image.filename(), QString());
    QCOMPARE(image.animatedSource(), false);
    QCOMPARE(image.animationStyle(), RGBImage::Static);
    QCOMPARE(image.xOffset(), 0);
    QCOMPARE(image.yOffset(), 0);
    QVERIFY(image.doc() == m_doc);

    // Colors are not accepted, so they are neither stored nor returned
    image.rgbMapSetColors(QVector<uint>() << 1 << 2);
    QCOMPARE(image.rgbMapGetColors(), QVector<uint>());

    // Without an image nothing is rendered and the map is left alone
    RGBMap map;
    image.rgbMap(QSize(2, 2), 0, 0, map);
    QCOMPARE(map.size(), 0);
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), 1);
    image.setAnimationStyle(RGBImage::Horizontal);
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), 0);
    image.setAnimationStyle(RGBImage::Vertical);
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), 0);
    image.setAnimationStyle(RGBImage::Animation);
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), 1); // at least one step

    // Rewinding a non-animated source is a no-op
    image.rewindAnimation();
    QCOMPARE(image.animatedSource(), false);
}

void RGBImage_Test::filename()
{
    RGBImage image(m_doc);
    image.setFilename(m_pngPath);
    QCOMPARE(image.filename(), m_pngPath);
    QCOMPARE(image.animatedSource(), false);
    QCOMPARE(image.m_image.width(), IMG_W);
    QCOMPARE(image.m_image.height(), IMG_H);

    // A file that doesn't exist can't be loaded: no image any more
    image.setFilename(QDir(m_dir.path()).absoluteFilePath("missing.png"));
    QVERIFY(image.filename().endsWith("missing.png"));
    QCOMPARE(image.m_image.width(), 0);
    RGBMap map;
    image.rgbMap(QSize(2, 2), 0, 0, map);
    QCOMPARE(map.size(), 0);

    // An empty filename is accepted and clears the animated flag
    image.setFilename(QString());
    QCOMPARE(image.filename(), QString());
    QCOMPARE(image.animatedSource(), false);

    // A .gif that is not a real animation falls back to a static image load
    QString gifPath = QDir(m_dir.path()).absoluteFilePath("bogus.gif");
    QFile gif(gifPath);
    QVERIFY(gif.open(QIODevice::WriteOnly));
    gif.write("not a gif");
    gif.close();
    image.setFilename(gifPath);
    QCOMPARE(image.animatedSource(), false);
    QCOMPARE(image.m_image.width(), 0);
}

void RGBImage_Test::imageData()
{
    RGBImage image(m_doc);

    // Two rows of two RGB888 pixels
    QByteArray data;
    data.append(char(1)).append(char(2)).append(char(3));
    data.append(char(4)).append(char(5)).append(char(6));
    data.append(char(7)).append(char(8)).append(char(9));
    data.append(char(250)).append(char(251)).append(char(252));
    image.setImageData(2, 2, data);
    QCOMPARE(image.m_image.width(), 2);
    QCOMPARE(image.m_image.height(), 2);

    RGBMap map;
    image.rgbMap(QSize(2, 2), 0, 0, map);
    QCOMPARE(map.size(), 2);
    QCOMPARE(map[0][0], uint(qRgb(1, 2, 3)));
    QCOMPARE(map[0][1], uint(qRgb(4, 5, 6)));
    QCOMPARE(map[1][0], uint(qRgb(7, 8, 9)));
    QCOMPARE(map[1][1], uint(qRgb(250, 251, 252)));
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), 1);

    // A truncated buffer only fills the leading pixels, the rest stays black
    image.setImageData(2, 2, data.left(7));
    image.rgbMap(QSize(2, 2), 0, 0, map);
    QCOMPARE(map[0][0], uint(qRgb(1, 2, 3)));
    QCOMPARE(map[0][1], uint(qRgb(4, 5, 6)));
    QCOMPARE(map[1][0], uint(qRgb(0, 0, 0)));
    QCOMPARE(map[1][1], uint(qRgb(0, 0, 0)));
}

void RGBImage_Test::animationStyles()
{
    QCOMPARE(RGBImage::animationStyleToString(RGBImage::Static), QString("Static"));
    QCOMPARE(RGBImage::animationStyleToString(RGBImage::Horizontal), QString("Horizontal"));
    QCOMPARE(RGBImage::animationStyleToString(RGBImage::Vertical), QString("Vertical"));
    QCOMPARE(RGBImage::animationStyleToString(RGBImage::Animation), QString("Animation"));
    QCOMPARE(RGBImage::animationStyleToString(RGBImage::AnimationStyle(42)), QString("Static"));

    QCOMPARE(RGBImage::stringToAnimationStyle("Static"), RGBImage::Static);
    QCOMPARE(RGBImage::stringToAnimationStyle("Horizontal"), RGBImage::Horizontal);
    QCOMPARE(RGBImage::stringToAnimationStyle("Vertical"), RGBImage::Vertical);
    QCOMPARE(RGBImage::stringToAnimationStyle("Animation"), RGBImage::Animation);
    QCOMPARE(RGBImage::stringToAnimationStyle("Foo"), RGBImage::Static);
    QCOMPARE(RGBImage::stringToAnimationStyle(QString()), RGBImage::Static);

    QCOMPARE(RGBImage::animationStyles(),
             QStringList() << "Static" << "Horizontal" << "Vertical" << "Animation");

    RGBImage image(m_doc);
    image.setAnimationStyle(RGBImage::Vertical);
    QCOMPARE(image.animationStyle(), RGBImage::Vertical);
    image.setAnimationStyle(RGBImage::Animation);
    QCOMPARE(image.animationStyle(), RGBImage::Animation);
    // Out of range values fall back to Static
    image.setAnimationStyle(RGBImage::AnimationStyle(42));
    QCOMPARE(image.animationStyle(), RGBImage::Static);
    image.setAnimationStyle(RGBImage::Horizontal);
    image.setAnimationStyle(RGBImage::AnimationStyle(-1));
    QCOMPARE(image.animationStyle(), RGBImage::Static);
}

void RGBImage_Test::offsets()
{
    RGBImage image(m_doc);
    image.setXOffset(5);
    QCOMPARE(image.xOffset(), 5);
    image.setYOffset(-3);
    QCOMPARE(image.yOffset(), -3);
    image.setXOffset(0);
    QCOMPARE(image.xOffset(), 0);
}

void RGBImage_Test::stepCount()
{
    RGBImage image(m_doc);
    image.setFilename(m_pngPath);

    image.setAnimationStyle(RGBImage::Static);
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), 1);
    QCOMPARE(image.rgbMapStepCount(QSize(10, 10)), 1);

    image.setAnimationStyle(RGBImage::Horizontal);
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), IMG_W);

    image.setAnimationStyle(RGBImage::Vertical);
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), IMG_H);

    // Animation: one frame per group-width slice of the image, at least one
    image.setAnimationStyle(RGBImage::Animation);
    QCOMPARE(image.rgbMapStepCount(QSize(2, 2)), IMG_W / 2);
    QCOMPARE(image.rgbMapStepCount(QSize(1, 1)), IMG_W);
    QCOMPARE(image.rgbMapStepCount(QSize(IMG_W * 2, 1)), 1);
}

void RGBImage_Test::mapStatic()
{
    RGBImage image(m_doc);
    image.setFilename(m_pngPath);
    QCOMPARE(image.animationStyle(), RGBImage::Static);

    // A map matching the image is the image, the color and step are ignored
    RGBMap map;
    image.rgbMap(QSize(IMG_W, IMG_H), 0x123456, 3, map);
    QCOMPARE(map.size(), IMG_H);
    for (int y = 0; y < IMG_H; y++)
    {
        QCOMPARE(map[y].size(), IMG_W);
        for (int x = 0; x < IMG_W; x++)
            QCOMPARE(map[y][x], pixel(x, y));
    }
    // The transparent pixel is off
    QCOMPARE(map[IMG_H - 1][IMG_W - 1], uint(0));

    // A larger map wraps around the image
    image.rgbMap(QSize(IMG_W + 2, IMG_H + 1), 0, 0, map);
    QCOMPARE(map.size(), IMG_H + 1);
    for (int y = 0; y < IMG_H + 1; y++)
    {
        QCOMPARE(map[y].size(), IMG_W + 2);
        for (int x = 0; x < IMG_W + 2; x++)
            QCOMPARE(map[y][x], pixel(x, y));
    }

    // A smaller map is the top-left corner, shifted by the offsets
    image.setXOffset(1);
    image.setYOffset(2);
    image.rgbMap(QSize(2, 2), 0, 0, map);
    QCOMPARE(map.size(), 2);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
            QCOMPARE(map[y][x], pixel(x + 1, y + 2));
}

void RGBImage_Test::mapHorizontal()
{
    RGBImage image(m_doc);
    image.setFilename(m_pngPath);
    image.setAnimationStyle(RGBImage::Horizontal);

    // Each step scrolls the image one pixel to the left
    for (int step = 0; step < IMG_W + 1; step++)
    {
        RGBMap map;
        image.rgbMap(QSize(2, IMG_H), 0, step, map);
        QCOMPARE(map.size(), IMG_H);
        for (int y = 0; y < IMG_H; y++)
            for (int x = 0; x < 2; x++)
                QCOMPARE(map[y][x], pixel(x + step, y));
    }

    // Offsets are added to the scrolling
    image.setXOffset(2);
    image.setYOffset(1);
    RGBMap map;
    image.rgbMap(QSize(2, 2), 0, 1, map);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
            QCOMPARE(map[y][x], pixel(x + 2 + 1, y + 1));
}

void RGBImage_Test::mapVertical()
{
    RGBImage image(m_doc);
    image.setFilename(m_pngPath);
    image.setAnimationStyle(RGBImage::Vertical);

    // Each step scrolls the image one pixel up
    for (int step = 0; step < IMG_H + 1; step++)
    {
        RGBMap map;
        image.rgbMap(QSize(IMG_W, 2), 0, step, map);
        QCOMPARE(map.size(), 2);
        for (int y = 0; y < 2; y++)
            for (int x = 0; x < IMG_W; x++)
                QCOMPARE(map[y][x], pixel(x, y + step));
    }

    image.setXOffset(1);
    image.setYOffset(1);
    RGBMap map;
    image.rgbMap(QSize(2, 2), 0, 2, map);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 2; x++)
            QCOMPARE(map[y][x], pixel(x + 1, y + 1 + 2));
}

void RGBImage_Test::mapAnimation()
{
    RGBImage image(m_doc);
    image.setFilename(m_pngPath);
    image.setAnimationStyle(RGBImage::Animation);

    // Each step shows the next group-width slice of the image
    QSize size(2, IMG_H);
    QCOMPARE(image.rgbMapStepCount(size), 2);
    for (int step = 0; step < 2; step++)
    {
        RGBMap map;
        image.rgbMap(size, 0, step, map);
        QCOMPARE(map.size(), IMG_H);
        for (int y = 0; y < IMG_H; y++)
            for (int x = 0; x < 2; x++)
                QCOMPARE(map[y][x], pixel(x + step * 2, y));
    }

    image.setXOffset(1);
    RGBMap map;
    image.rgbMap(size, 0, 1, map);
    for (int y = 0; y < IMG_H; y++)
        for (int x = 0; x < 2; x++)
            QCOMPARE(map[y][x], pixel(x + 1 + 2, y));
}

void RGBImage_Test::copyAndClone()
{
    RGBImage image(m_doc);
    image.setFilename(m_pngPath);
    image.setAnimationStyle(RGBImage::Vertical);
    image.setXOffset(3);
    image.setYOffset(1);

    RGBImage copy(image);
    QCOMPARE(copy.filename(), m_pngPath);
    QCOMPARE(copy.animationStyle(), RGBImage::Vertical);
    QCOMPARE(copy.xOffset(), 3);
    QCOMPARE(copy.yOffset(), 1);
    QCOMPARE(copy.animatedSource(), false);
    QVERIFY(copy.doc() == m_doc);
    // The image is reloaded from the file
    QCOMPARE(copy.m_image.width(), IMG_W);
    QCOMPARE(copy.m_image.height(), IMG_H);

    RGBAlgorithm *clone = image.clone();
    QVERIFY(clone != NULL);
    QVERIFY(clone != &image);
    QCOMPARE(clone->type(), RGBAlgorithm::Image);
    RGBImage *cloneImage = static_cast<RGBImage*> (clone);
    QCOMPARE(cloneImage->filename(), m_pngPath);
    QCOMPARE(cloneImage->animationStyle(), RGBImage::Vertical);
    QCOMPARE(cloneImage->xOffset(), 3);
    QCOMPARE(cloneImage->yOffset(), 1);

    // Original and clone render the same thing
    RGBMap map1, map2;
    image.rgbMap(QSize(2, 2), 0, 1, map1);
    clone->rgbMap(QSize(2, 2), 0, 1, map2);
    QCOMPARE(map1, map2);
    QCOMPARE(map1[0][0], pixel(3, 1 + 1));
    delete clone;

    // Copying an image without a file works too
    RGBImage empty(m_doc);
    RGBImage emptyCopy(empty);
    QCOMPARE(emptyCopy.filename(), QString());
    QCOMPARE(emptyCopy.m_image.width(), 0);
}

void RGBImage_Test::animatedGif()
{
    if (QImageReader::supportedImageFormats().contains("gif") == false)
        QSKIP("No GIF image format support in this Qt build");

    /* A hand-made 2x1 GIF89a with two frames (all red, then all blue),
     * a 4-entry global color table and an infinite NETSCAPE loop */
    static const unsigned char gifData[] = {
        'G', 'I', 'F', '8', '9', 'a',
        0x02, 0x00, 0x01, 0x00, 0x91, 0x00, 0x00,
        0xFF, 0x00, 0x00,  0x00, 0x00, 0xFF,  0x00, 0xFF, 0x00,  0x00, 0x00, 0x00,
        0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P', 'E', '2', '.', '0',
        0x03, 0x01, 0x00, 0x00, 0x00,
        0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00,
        0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00,
        0x02, 0x02, 0x04, 0x0A, 0x00,
        0x21, 0xF9, 0x04, 0x00, 0x0A, 0x00, 0x00, 0x00,
        0x2C, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00,
        0x02, 0x02, 0x4C, 0x0A, 0x00,
        0x3B
    };

    QString gifPath = QDir(m_dir.path()).absoluteFilePath("anim.gif");
    QFile gif(gifPath);
    QVERIFY(gif.open(QIODevice::WriteOnly));
    gif.write(reinterpret_cast<const char*>(gifData), sizeof(gifData));
    gif.close();

    RGBImage image(m_doc);
    image.setFilename(gifPath);
    QVERIFY(image.animatedSource());

    // Every rgbMap() call shows the next frame, scaled to the map size
    RGBMap map;
    image.rgbMap(QSize(2, 1), 0, 0, map);
    QCOMPARE(map.size(), 1);
    QCOMPARE(map[0].size(), 2);
    QCOMPARE(map[0][0], uint(qRgb(255, 0, 0)));
    QCOMPARE(map[0][1], uint(qRgb(255, 0, 0)));

    image.rgbMap(QSize(2, 1), 0, 0, map);
    QCOMPARE(map[0][0], uint(qRgb(0, 0, 255)));
    QCOMPARE(map[0][1], uint(qRgb(0, 0, 255)));

    // Rewinding positions the player on the first frame; rgbMap() always
    // moves to the next frame before rendering, so the frame after that
    // is what comes out
    image.rewindAnimation();
    QCOMPARE(image.m_animatedPlayer.currentFrameNumber(), 0);
    image.rgbMap(QSize(2, 1), 0, 0, map);
    QCOMPARE(map[0][0], uint(qRgb(0, 0, 255)));

    // A copy picks the animation up too
    RGBImage copy(image);
    QVERIFY(copy.animatedSource());
    copy.rgbMap(QSize(2, 1), 0, 0, map);
    QCOMPARE(map[0][0], uint(qRgb(255, 0, 0)));
}

void RGBImage_Test::saveXML()
{
    RGBImage image(m_doc);
    image.setFilename(m_pngPath);
    image.setAnimationStyle(RGBImage::Animation);
    image.setXOffset(2);
    image.setYOffset(-1);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(image.saveXML(&xmlWriter) == true);
    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QCOMPARE(xmlReader.name().toString(), QString("Algorithm"));
    QCOMPARE(xmlReader.attributes().value("Type").toString(), QString("Image"));

    int filename = 0, animation = 0, offset = 0;
    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name().toString() == "Filename")
        {
            // Stored relative to the workspace
            QCOMPARE(xmlReader.readElementText(), QString("test.png"));
            filename++;
        }
        else if (xmlReader.name().toString() == "Animation")
        {
            QCOMPARE(xmlReader.readElementText(), QString("Animation"));
            animation++;
        }
        else if (xmlReader.name().toString() == "Offset")
        {
            QCOMPARE(xmlReader.attributes().value("X").toString(), QString("2"));
            QCOMPARE(xmlReader.attributes().value("Y").toString(), QString("-1"));
            offset++;
            xmlReader.skipCurrentElement();
        }
        else
        {
            QFAIL(QString("Unexpected tag: %1").arg(xmlReader.name().toString()).toUtf8().constData());
        }
    }
    QCOMPARE(filename, 1);
    QCOMPARE(animation, 1);
    QCOMPARE(offset, 1);
    buffer.close();

    // Round trip
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    xmlReader.setDevice(&buffer);
    xmlReader.readNextStartElement();

    RGBImage loaded(m_doc);
    QVERIFY(loaded.loadXML(xmlReader) == true);
    QCOMPARE(QFileInfo(loaded.filename()).absoluteFilePath(), QFileInfo(m_pngPath).absoluteFilePath());
    QCOMPARE(loaded.animationStyle(), RGBImage::Animation);
    QCOMPARE(loaded.xOffset(), 2);
    QCOMPARE(loaded.yOffset(), -1);
    QCOMPARE(loaded.m_image.width(), IMG_W);
}

void RGBImage_Test::loadXML()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);

    xmlWriter.writeStartElement("Algorithm");
    xmlWriter.writeAttribute("Type", "Image");
    xmlWriter.writeTextElement("Filename", "test.png"); // relative to the workspace
    xmlWriter.writeTextElement("Animation", "Vertical");
    xmlWriter.writeStartElement("Offset");
    xmlWriter.writeAttribute("X", "7");
    xmlWriter.writeAttribute("Y", "9");
    xmlWriter.writeEndElement();
    xmlWriter.writeTextElement("Foo", "Bar"); // unknown tag
    xmlWriter.writeEndElement();

    xmlWriter.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();

    RGBImage image(m_doc);
    QVERIFY(image.loadXML(xmlReader) == true);
    QCOMPARE(QFileInfo(image.filename()).absoluteFilePath(), QFileInfo(m_pngPath).absoluteFilePath());
    QCOMPARE(image.animationStyle(), RGBImage::Vertical);
    QCOMPARE(image.xOffset(), 7);
    QCOMPARE(image.yOffset(), 9);
    QCOMPARE(image.m_image.width(), IMG_W);
}

void RGBImage_Test::loadXMLMalformed()
{
    // Wrong root tag
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Foo");
        xmlWriter.writeAttribute("Type", "Image");
        xmlWriter.writeEndElement();
        xmlWriter.setDevice(NULL);
        buffer.close();

        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();

        RGBImage image(m_doc);
        QVERIFY(image.loadXML(xmlReader) == false);
    }

    // Wrong algorithm type
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Algorithm");
        xmlWriter.writeAttribute("Type", "Text");
        xmlWriter.writeEndElement();
        xmlWriter.setDevice(NULL);
        buffer.close();

        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();

        RGBImage image(m_doc);
        QVERIFY(image.loadXML(xmlReader) == false);
    }

    // Invalid offsets are reported and ignored, the rest still loads
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly | QIODevice::Text);
        QXmlStreamWriter xmlWriter(&buffer);
        xmlWriter.writeStartElement("Algorithm");
        xmlWriter.writeAttribute("Type", "Image");
        xmlWriter.writeStartElement("Offset");
        xmlWriter.writeAttribute("X", "seven");
        xmlWriter.writeAttribute("Y", "");
        xmlWriter.writeEndElement();
        xmlWriter.writeTextElement("Animation", "Horizontal");
        xmlWriter.writeEndElement();
        xmlWriter.setDevice(NULL);
        buffer.close();

        buffer.open(QIODevice::ReadOnly | QIODevice::Text);
        QXmlStreamReader xmlReader(&buffer);
        xmlReader.readNextStartElement();

        RGBImage image(m_doc);
        image.setXOffset(1);
        image.setYOffset(2);
        QVERIFY(image.loadXML(xmlReader) == true);
        QCOMPARE(image.xOffset(), 1);
        QCOMPARE(image.yOffset(), 2);
        QCOMPARE(image.animationStyle(), RGBImage::Horizontal);
        QCOMPARE(image.filename(), QString());
    }
}

QTEST_MAIN(RGBImage_Test)
