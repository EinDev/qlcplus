/*
  Q Light Controller
  rgbscript_test.cpp

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

#include <QtTest>

#define private public
#include "rgbscript_test.h"
#include "rgbscriptscache.h"

#ifdef QT_QML_LIB
  #include "rgbscriptv4.h"
#else
  #include "rgbscript.h"
#endif
#undef private

#include "doc.h"

#include "../common/resource_paths.h"

void RGBScript_Test::initTestCase()
{
    m_doc = new Doc(this);
}

void RGBScript_Test::cleanupTestCase()
{
    delete m_doc;
}

void RGBScript_Test::initial()
{
    RGBScript script(m_doc);
#ifdef QT_QML_LIB
    QVERIFY(script.s_jsThread == NULL);
#else
    QVERIFY(script.s_engine == NULL);
#endif
    QCOMPARE(script.m_apiVersion, 0);
    QCOMPARE(script.m_fileName, QString());
    QCOMPARE(script.m_contents, QString());
}

void RGBScript_Test::directories()
{
    QDir dir = RGBScriptsCache::systemScriptsDirectory();
    QCOMPARE(dir.filter(), QDir::Files);
    QCOMPARE(dir.nameFilters(), QStringList() << QString("*.js"));
#if defined(__APPLE__) || defined(Q_OS_MAC)
    QString path("%1/../%2");
    QCOMPARE(dir.path(), path.arg(QCoreApplication::applicationDirPath())
                             .arg("Resources/RGBScripts"));
#elif defined(WIN32) || defined(Q_OS_WIN)
    QVERIFY(dir.path().endsWith("RGBScripts"));
#else
    QVERIFY(dir.path().endsWith("qlcplus/rgbscripts"));
#endif

    dir = RGBScriptsCache::userScriptsDirectory();
    QCOMPARE(dir.filter(), QDir::Files);
    QCOMPARE(dir.nameFilters(), QStringList() << QString("*.js"));
#if defined(__APPLE__) || defined(Q_OS_MAC)
    QVERIFY(dir.path().endsWith("Library/Application Support/QLC+/RGBScripts"));
#elif defined(WIN32) || defined(Q_OS_WIN)
    QVERIFY(dir.path().endsWith("RGBScripts"));
#else
    QVERIFY(dir.path().endsWith(".qlcplus/rgbscripts"));
#endif
}

void RGBScript_Test::scripts()
{
    QDir dir(INTERNAL_SCRIPTDIR);
    dir.setFilter(QDir::Files);
    dir.setNameFilters(QStringList() << QString("*.js"));
    QVERIFY(dir.entryList().size() > 0);

    // Prepare check that file is registered for delivery
    QString proFilePath = dir.filePath("CMakeLists.txt");
    QFile proFile(proFilePath);
    QVERIFY(proFile.open(QIODevice::ReadWrite));
    QTextStream pro (&proFile);

    // Catch syntax / JS engine errors explicitly in the test.
    foreach (QString file, dir.entryList()) {
        RGBScript* script = new RGBScript(m_doc);
        QFile absFile(dir.absoluteFilePath(file));
        QVERIFY(script->load(absFile.fileName()));

        qDebug() << "Searching '" + file + "' in CMakeLists.txt";

        // Check that the script is listed in the cmake file.
        if (file != "empty.js") {
            QString searchString = "    " + file;
            QString line;
            bool foundInProFile = false;
            do {
                line = pro.readLine();
                if (line.contains(searchString, Qt::CaseSensitive)) {
                    foundInProFile = true;
                }
            } while (!line.isNull() && foundInProFile == false);

            QVERIFY(foundInProFile);
        }
    }
    proFile.close();

    QVERIFY(m_doc->rgbScriptsCache()->load(dir));
    QVERIFY(m_doc->rgbScriptsCache()->names().size() > 0);
}

void RGBScript_Test::script()
{
    QVERIFY(m_doc->rgbScriptsCache()->load(QDir(INTERNAL_SCRIPTDIR)));

    RGBScript* s = m_doc->rgbScriptsCache()->script("A script that should not exist");
    QCOMPARE(s->fileName(), QString());
    QCOMPARE(s->m_contents, QString());
    QCOMPARE(s->apiVersion(), 0);
    QCOMPARE(s->author(), QString());
    QCOMPARE(s->name(), QString());
#ifdef QT_QML_LIB
    QVERIFY(s->m_script.isUndefined() == true);
    QVERIFY(s->m_rgbMap.isUndefined() == true);
    QVERIFY(s->m_rgbMapStepCount.isUndefined() == true);
#else
    // QVERIFY(s->m_script.isValid() == false); // TODO: to be fixed !!
    QVERIFY(s->m_rgbMap.isValid() == false);
    QVERIFY(s->m_rgbMapStepCount.isValid() == false);
#endif
    s = m_doc->rgbScriptsCache()->script("Stripes");
    QVERIFY(s->fileName().endsWith("stripes.js"));
    QVERIFY(s->m_contents.isEmpty() == false);
    QVERIFY(s->apiVersion() > 0);
    QCOMPARE(s->author(), QString("Massimo Callegari"));
    QCOMPARE(s->name(), QString("Stripes"));
#ifdef QT_QML_LIB
    QVERIFY(s->m_script.isUndefined() == false);
    QVERIFY(s->m_rgbMap.isUndefined() == false);
    QVERIFY(s->m_rgbMapStepCount.isUndefined() == false);
#else
    QVERIFY(s->m_script.isValid() == true);
    QVERIFY(s->m_rgbMap.isValid() == true);
    QVERIFY(s->m_rgbMapStepCount.isValid() == true);
#endif
    delete s;
}

void RGBScript_Test::evaluateException()
{
    // Should be    function()
    QString code("( function { return 5; } )()");
    RGBScript s(m_doc);
    s.m_contents = code;
    QCOMPARE(s.evaluate(), false);
}

void RGBScript_Test::evaluateNoRgbMapFunction()
{
    // No rgbMap() function present
    QString code("( function() { return 5; } )()");
    RGBScript s(m_doc);
    RGBMap map;
    s.m_contents = code;
    QCOMPARE(s.evaluate(), false);
    s.rgbMap(QSize(5, 5), 1, 0, map);
    QCOMPARE(map, RGBMap());
}

void RGBScript_Test::evaluateNoRgbMapStepCountFunction()
{
    // No rgbMapStepCount() function present
    QString code("( function() { var foo = new Object; foo.rgbMap = function() { return 0; }; return foo; } )()");
    RGBScript s(m_doc);
    s.m_contents = code;
    QCOMPARE(s.evaluate(), false);
    QCOMPARE(s.rgbMapStepCount(QSize(5, 5)), -1);
}

void RGBScript_Test::evaluateInvalidApiVersion()
{
    // No apiVersion property
    QString code("( function() { var foo = new Object; foo.rgbMap = function() { return 0; }; foo.rgbMapStepCount = function(width, height) { return 0; }; return foo; } )()");
    RGBScript s(m_doc);
    s.m_contents = code;
    QCOMPARE(s.evaluate(), false);
}

void RGBScript_Test::rgbMapStepCount()
{
    RGBScript* s = m_doc->rgbScriptsCache()->script("Stripes");
    QCOMPARE(s->rgbMapStepCount(QSize(10, 15)), 10);
    delete s;
}

void RGBScript_Test::rgbMapColorArray()
{
    RGBMap map;
    RGBScript* s = m_doc->rgbScriptsCache()->script("Alternate");
    QCOMPARE(s->evaluate(), true);
    QVector<uint> rawRgbColors = {
            QColor(Qt::red).rgb() & 0x00ffffff,
            QColor(Qt::green).rgb() & 0x00ffffff
    };
    QSize mapSize = QSize(5, 5);

    s->rgbMapSetColors(rawRgbColors);
    s->rgbMap(mapSize, 0, 0, map);
    QVERIFY(map.isEmpty() == false);

    // check that both initial colors are used in the same step
    for (int y = 0; y < mapSize.height(); y++)
    {
        for (int x = 0; x < mapSize.width(); x++)
        {
            // qDebug() << "y: " << y << " x: " << x << " C: " << Qt::hex << map[y][x];
            if (x % 2 == 0)
                QCOMPARE(map[y][x], rawRgbColors[1]);
            else
                QCOMPARE(map[y][x], rawRgbColors[0]);
        }
    }
    delete s;
}

void RGBScript_Test::rgbMap()
{
    RGBMap map;
    RGBScript* s = m_doc->rgbScriptsCache()->script("Stripes");
    QVector<uint> rawRgbColors = {
        QColor(Qt::red).rgb(),
        uint(0)
    };
    s->rgbMap(QSize(3, 4), 0, 0, map);
    // verify that an array within an array has been returned
    QVERIFY(map.isEmpty() == false);

    s->setProperty("orientation", "Vertical");
    QVERIFY(s->property("orientation") == "Vertical");

    for (int step = 0; step < 5; step++)
    {
        RGBMap map;
        s->rgbMap(QSize(5, 5), rawRgbColors[0], step, map);
        for (int y = 0; y < 5; y++)
        {
            for (int x = 0; x < 5; x++)
            {
                if (y == step)
                    QCOMPARE(map[y][x], rawRgbColors[0]);
                else
                    QCOMPARE(map[y][x], rawRgbColors[1]);
            }
        }
    }
    delete s;
}

void RGBScript_Test::runScripts()
{
    QSize mapSize = QSize(7, 11); // Use different numbers for x and y for the test
    QSize mapSizePlus = QSize(12, 22); // Prepare a larger matrix to check behaviour on matrix change
    QVector<uint> rawRgbColors = {
        // QColor(Qt::red).rgb() is 0xffff0000 due to the alpha channel
        // This test also wants to test that there is no color space overrun.
        QColor(Qt::red).rgb() & 0xffffff,
        uint(0)
    };

    // Iterate the list of scripts
    QStringList names = m_doc->rgbScriptsCache()->names();
    foreach (QString name, names)
    {
        qDebug() << "Evaluating script" << name;
        QScopedPointer<RGBScript> s(m_doc->rgbScriptsCache()->script(name));
        QString fileName = s->fileName();
        QString scriptName = fileName.split(QDir::separator()).last();

        // Check naming conventions
        QVERIFY(fileName.endsWith(".js"));
        // Check that basename and extension are lowercase
        QVERIFY(scriptName.toLower() == scriptName);
        // Verify that the basename only uses lower case characters
        QString baseName = scriptName;
        baseName.truncate(scriptName.size() - 3);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QVERIFY(QRegExp("[a-z]*").exactMatch(baseName));
#else
        QVERIFY(QRegularExpression("[a-z]+").match(baseName).hasMatch());
#endif

#ifdef QT_QML_LIB
        QVERIFY(!s->m_script.isUndefined());
        QVERIFY(!s->m_rgbMap.isUndefined());
        QVERIFY(!s->m_rgbMapStepCount.isUndefined());
#else
        // QVERIFY(s->m_script.isValid()); // TODO: to be fixed !!
        QVERIFY(s->m_rgbMap.isValid());
        QVERIFY(s->m_rgbMapStepCount.isValid());
#endif

        { // limit the scope of this map to keep it clean for future executions
            RGBMap map;
            s->rgbMapSetColors(rawRgbColors);
            s->rgbMap(mapSize, 0, 0, map);
            QVERIFY(map.isEmpty() == false);
        }

        QVERIFY(s->apiVersion() >= 1 && s->apiVersion() <= 3);
        QVERIFY(!s->author().isEmpty());
        QVERIFY(!s->name().isEmpty());
        QVERIFY(s->type() == RGBAlgorithm::Script);
        if (s->apiVersion() <= 2)
            QVERIFY(s->acceptColors() >= 0 && s->acceptColors() <= 2);
        else
            QVERIFY(s->acceptColors() >= 0 && s->acceptColors() <= 5);

        int steps = s->rgbMapStepCount(mapSize);
        //qDebug() << "steps: " << steps;
        QVERIFY(steps > 0);

        // Run a few steps with the standard set of parameters.
        int realsteps = (steps > 5) ? 5 : steps;
        RGBMap rgbMap;
        for (int step = 0; step < realsteps; step++)
        {
            s->rgbMapSetColors(rawRgbColors);
            s->rgbMap(mapSize, rawRgbColors[0], step, rgbMap);
            QVERIFY(rgbMap.isEmpty() == false);
            // Check that the color values are limited to a valid range
            for (int y = 0; y < mapSize.height(); y++)
            {
                for (int x = 0; x < mapSize.width(); x++)
                {
                    QVERIFY(rgbMap[y][x] <= 0xffffff);
                }
            }
        }
        // Prepare a reference RGB map
        bool randomScript = fileName.contains("random", Qt::CaseInsensitive);
        RGBMap rgbRefMap;
        if (1 < s->acceptColors() && 2 < steps && ! randomScript) {
            // When more than 2 colors are accepted, the steps shall be reproducible to allow back and forth color fade.
            s->rgbMapSetColors(rawRgbColors);
            s->rgbMap(mapSizePlus, rawRgbColors[0], 0, rgbRefMap);
        }
        // Switch to the larger map and step a few times.
        for (int step = 0; step < realsteps; step++)
        {
            s->rgbMapSetColors(rawRgbColors);
            s->rgbMap(mapSizePlus, rawRgbColors[0], step, rgbMap);
            // Check that the color values are limited to a valid range
            for (int y = 0; y < mapSizePlus.height(); y++)
            {
                for (int x = 0; x < mapSizePlus.width(); x++)
                {
                    if (s->acceptColors() > 0)
                    {
                        // verify that the alpha channel is zero
                        QVERIFY((rgbMap[y][x] & 0xff000000) == 0);
                        QVERIFY((rgbMap[y][x] >> 16) <= 0x0000ff);
                        if (!randomScript && 0 == step && 1 < s->acceptColors() && 2 < steps)
                        {
                            // if more than one color is accepted  and the script has more than two steps - one per color,
                            // the color fade shall be relative and reproducible to the step
                            // as otherwise, the color fade cannot be aligned on stage and depends on e.g. matrix sizes or script settings.
                            QVERIFY(rgbMap[y][x] == rgbRefMap[y][x]);
                        }
                    }
                    else
                    {
                        QVERIFY(rgbMap[y][x] <= 0x00ffffff);
                    }
                }
            }
        }

        // Validate the parameters
        if (s->apiVersion() >= 2)
        {
            // Get the scripts properties and build test matrix
            QVERIFY(s->loadProperties());
            QList<RGBScriptProperty> properties = s->properties();
            foreach (RGBScriptProperty property, properties)
            {
                // Consider to even mix the properties for testing,
                // do not just test one combination with each value
                QVERIFY(! property.m_name.isEmpty());
                QVERIFY(! property.m_displayName.isEmpty());
                QVERIFY(! property.m_readMethod.isEmpty());
                QVERIFY(! property.m_writeMethod.isEmpty());
                QList<RGBScriptProperty> properties = s->properties();
                qDebug() << property.m_name;
                // Unknown, new and RGBScriptProperty::None are not valid
                QVERIFY(property.m_type == RGBScriptProperty::List ||
                        property.m_type == RGBScriptProperty::Range ||
                        property.m_type == RGBScriptProperty::Float ||
                        property.m_type == RGBScriptProperty::String);
                // Check property specificities
                switch (property.m_type)
                {
                case RGBScriptProperty::List:
                    // Verify the list is valid
                    QVERIFY(property.m_listValues.size() > 1);
                    QVERIFY(property.m_listValues.removeDuplicates() == 0);
                    // Verify the default value is valid
                    QVERIFY(property.m_listValues.contains(s->property(property.m_name)));
                    foreach (QString value, property.m_listValues)
                    {
                        // Test with all values from the list
                        qDebug() << property.m_name << value;
                        s->setProperty(property.m_name, value);
                        qDebug() << "  Readback" << s->property(property.m_name);
                        QVERIFY(s->property(property.m_name) == value);
                        for (int step = 0; step < realsteps; step++)
                        {
                            RGBMap map;
                            s->rgbMapSetColors(rawRgbColors);
                            s->rgbMap(mapSize, rawRgbColors[0], step, map);
                            QVERIFY(map.isEmpty() == false);
                            // Check that the color values are limited to a valid range
                            for (int y = 0; y < mapSize.height(); y++)
                            {
                                for (int x = 0; x < mapSize.width(); x++)
                                {
                                    QVERIFY(map[y][x] <= 0xffffff);
                                }
                            }
                        }
                    }
                    break;
                case RGBScriptProperty::Range:
                    QVERIFY(property.m_rangeMinValue < property.m_rangeMaxValue);
                    // Verify the default value is in the valid range
                    qDebug() << "  Default: " << s->property(property.m_name).toInt()
                           << " Min: " << property.m_rangeMinValue << " Max: " << property.m_rangeMaxValue;
                    QVERIFY(s->property(property.m_name).toInt() >= property.m_rangeMinValue);
                    QVERIFY(s->property(property.m_name).toInt() <= property.m_rangeMaxValue);
                    // test with min and max value from the list
                    qDebug() << property.m_name << QString::number(property.m_rangeMinValue);

                    s->setProperty(property.m_name, QString::number(property.m_rangeMinValue));
                    qDebug() << "  Readback" << s->property(property.m_name);
                    QVERIFY(s->property(property.m_name) == QString::number(property.m_rangeMinValue));
                    for (int step = 0; step < realsteps; step++)
                    {
                        RGBMap map;
                        s->rgbMapSetColors(rawRgbColors);
                        s->rgbMap(mapSize, rawRgbColors[0], step, map);
                        QVERIFY(map.isEmpty() == false);
                        // Check that the color values are limited to a valid range
                        for (int y = 0; y < mapSize.height(); y++)
                        {
                            for (int x = 0; x < mapSize.width(); x++)
                            {
                                QVERIFY(map[y][x] <= 0xffffff);
                            }
                        }
                    }
                    qDebug() << property.m_name << QString::number(property.m_rangeMaxValue);
                    s->setProperty(property.m_name, QString::number(property.m_rangeMaxValue));
                    qDebug() << "  Readback: " << s->property(property.m_name);
                    QVERIFY(s->property(property.m_name) == QString::number(property.m_rangeMaxValue));
                    for (int step = 0; step < realsteps; step++)
                    {
                        RGBMap map;
                        s->rgbMapSetColors(rawRgbColors);
                        s->rgbMap(mapSize, rawRgbColors[0], step, map);
                        QVERIFY(map.isEmpty() == false);
                        // Check that the color values are limited to a valid range
                        for (int y = 0; y < mapSize.height(); y++)
                        {
                            for (int x = 0; x < mapSize.width(); x++)
                            {
                                QVERIFY(map[y][x] <= 0xffffff);
                            }
                        }
                    }
                    break;
                case RGBScriptProperty::Float:
                    // Test with an integer value
                    s->setProperty(property.m_name, QString::number(-1024));
                    qDebug() << "  Readback: " << s->property(property.m_name);
                    QVERIFY(s->property(property.m_name) == QString::number(-1024));
                    for (int step = 0; step < realsteps; step++)
                    {
                        RGBMap map;
                        s->rgbMapSetColors(rawRgbColors);
                        s->rgbMap(mapSize, rawRgbColors[0], step, map);
                        QVERIFY(map.isEmpty() == false);
                        // Check that the color values are limited to a valid range
                        for (int y = 0; y < mapSize.height(); y++)
                        {
                            for (int x = 0; x < mapSize.width(); x++)
                            {
                                QVERIFY(map[y][x] <= 0xffffff);
                            }
                        }
                    }
                    break;
                case RGBScriptProperty::String:
                    // Test with an integer value
                    s->setProperty(property.m_name, QString("QLC+"));
                    qDebug() << "  Readback: " << s->property(property.m_name);
                    QVERIFY(s->property(property.m_name) == QString("QLC+"));
                    for (int step = 0; step < realsteps; step++)
                    {
                        RGBMap map;
                        s->rgbMapSetColors(rawRgbColors);
                        s->rgbMap(mapSize, rawRgbColors[0], step, map);
                        QVERIFY(map.isEmpty() == false);
                        // Check that the color values are limited to a valid range
                        for (int y = 0; y < mapSize.height(); y++)
                        {
                            for (int x = 0; x < mapSize.width(); x++)
                            {
                                QVERIFY(map[y][x] <= 0xffffff);
                            }
                        }
                    }
                    break;
                default:
                    qDebug() << "Untested property: " << property.m_name;
                    QVERIFY(false);
                    break;
                }
            }
        }
    }
}

void RGBScript_Test::malformedProperties()
{
    // A synthetic apiVersion:2 script whose "properties" array is deliberately
    // malformed, to check loadProperties() (engine/src/rgbscriptv4.cpp) skips
    // bad entries instead of corrupting state or crashing. Per that function:
    // a "key:value" pair needs a colon to be parsed at all, and an entry is
    // only kept if it ends up with both a non-empty name AND a recognised
    // type - so a bare string, a name with no type, and a name with an
    // unrecognised type should all be silently dropped, while a recognised
    // type with a malformed "values" (a range with only one number) should
    // still be kept, just with its min/max left at their default of 0.
    QString code(
        "(function() {"
        "  var algo = new Object;"
        "  algo.apiVersion = 2;"
        "  algo.name = 'MalformedPropsTest';"
        "  algo.author = 'test';"
        "  algo.properties = new Array();"
        "  algo.properties.push('noColonHere');"
        "  algo.properties.push('name:onlyName');"
        "  algo.properties.push('name:badType|type:bogus|write:w|read:r');"
        "  algo.properties.push('name:badRange|type:range|values:5|write:w|read:r');"
        "  algo.w = function(v) {};"
        "  algo.r = function() { return 0; };"
        "  algo.rgbMap = function(width, height, rgb, step) { return [[0]]; };"
        "  algo.rgbMapStepCount = function(width, height) { return 1; };"
        "  return algo;"
        "})()");

    RGBScript s(m_doc);
    s.m_fileName = "malformed_test.js";
    s.m_contents = code;
    // evaluate() calls loadProperties() internally for apiVersion >= 2 and
    // returns its result - malformed entries are warned about and skipped,
    // never treated as a fatal parse error.
    QCOMPARE(s.evaluate(), true);

    QList<RGBScriptProperty> props = s.properties();
    QCOMPARE(props.size(), 1);
    QCOMPARE(props.first().m_name, QString("badRange"));
    QCOMPARE(props.first().m_type, RGBScriptProperty::Range);
    // "values:5" has no comma, so the min/max assignment is skipped entirely
    // and both stay at the RGBScriptProperty default of 0.
    QCOMPARE(props.first().m_rangeMinValue, 0);
    QCOMPARE(props.first().m_rangeMaxValue, 0);
}

void RGBScript_Test::wavesCircularOption()
{
    // Hand-verified regression test for the Circular option added to Waves
    // in commit a47a5c9df. Uses width=10 (span), taillength=30% (-> 3 tail
    // pixels), direction=Right, orientation=Horizontal, tailfade=No (so a
    // filled pixel is exactly $color and an unfilled one is exactly 0, no
    // rounding to account for).
    RGBScript *s = m_doc->rgbScriptsCache()->script("Waves");
    QVERIFY(s->fileName().endsWith("waves.js"));

    s->setProperty("orientation", "Horizontal");
    s->setProperty("direction", "Right");
    s->setProperty("tailfade", "No");
    s->setProperty("taillength", "30");
    QSize size(10, 1);
    uint color = 0x00ff0000;

    // --- circular OFF (default): unchanged pre-session behaviour ---
    QCOMPARE(s->property("circular"), QString("No"));
    // span(10) + tailSteps(3) - (isEven ? 0 : 1) = 13
    QCOMPARE(s->rgbMapStepCount(size), 13);

    // One step before the old step count wraps back to 0, the 3-pixel tail
    // has fully slid off the right edge (pos 9 max, needs pos > 12-3=9) -
    // a completely blank frame just before the whole pattern jump-cuts back
    // to its start. This blank gap is exactly the bug the Circular option
    // fixes.
    RGBMap blankMap;
    s->rgbMap(size, color, 12, blankMap);
    for (int x = 0; x < size.width(); x++)
        QCOMPARE(blankMap[0][x], uint(0));

    // --- circular ON ---
    s->setProperty("circular", "Yes");
    QCOMPARE(s->property("circular"), QString("Yes"));
    // No exit steps needed any more - one pass around the span is one cycle.
    QCOMPARE(s->rgbMapStepCount(size), 10);

    // Last step of the cycle: tail wrapped-distance from each pos to step=9
    // is < 3 only for pos 7,8,9.
    RGBMap lastStepMap;
    s->rgbMap(size, color, 9, lastStepMap);
    for (int x = 0; x < size.width(); x++)
    {
        bool expectLit = (x == 7 || x == 8 || x == 9);
        QCOMPARE(lastStepMap[0][x], expectLit ? color : uint(0));
    }

    // First step of the next cycle: the wrapped distance from pos to step=0
    // is < 3 for pos 8,9 (still trailing off from the previous cycle's head)
    // and pos 0 (the new head) - i.e. the tail continues moving forward
    // (8,9 -> 9,0) across the wrap with no blank frame and no jump, unlike
    // the non-circular case checked above.
    RGBMap firstStepMap;
    s->rgbMap(size, color, 0, firstStepMap);
    for (int x = 0; x < size.width(); x++)
    {
        bool expectLit = (x == 8 || x == 9 || x == 0);
        QCOMPARE(firstStepMap[0][x], expectLit ? color : uint(0));
    }

    delete s;
}

QTEST_MAIN(RGBScript_Test)
