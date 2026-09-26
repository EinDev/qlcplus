/*
  Q Light Controller - Unit tests
  qlcfile_test.cpp

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
#include <QTemporaryDir>
#include <QUrl>
#include <QDir>

#if defined(WIN32) || defined(Q_OS_WIN)
#else
#   include <sys/types.h>
#   include <sys/stat.h>
#endif

#include "qlcfile_test.h"
#include "qlcfile.h"
#include "qlcconfig.h"

void QLCFile_Test::XMLReader()
{
    QVERIFY(QLCFile::getXMLReader("") == NULL);

    QXmlStreamReader *reader = QLCFile::getXMLReader("readonly.xml.in");

    QVERIFY(reader != NULL);
    QVERIFY(reader->device() != NULL);
    QVERIFY(reader->device()->isOpen() == true);

    /* check releasing an invalid reader does nothing */
    QLCFile::releaseXMLReader(NULL);

    /* release the real reader */
    QLCFile::releaseXMLReader(reader);
    reader = NULL;

    /* check a file that cannot be created */
    reader = QLCFile::getXMLReader("foo.xml");
    QVERIFY(reader == NULL);
}

void QLCFile_Test::getXMLHeader()
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter doc(&buffer);
    doc.setAutoFormatting(true);
    doc.setAutoFormattingIndent(1);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    doc.setCodec("UTF-8");
#endif

    QLCFile::writeXMLHeader(&doc, "DocumentTag", "TestUnit");
    doc.writeEndDocument();

    doc.setDevice(NULL);
    buffer.close();

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);

    while (!xmlReader.atEnd())
    {
        if (xmlReader.readNext() == QXmlStreamReader::DTD)
            break;
    }

    bool creatorTag = false, author = false, appname = false,
         appversion = false;

    QVERIFY(xmlReader.hasError() == false);
    QVERIFY(xmlReader.dtdName().toString() == "DocumentTag");

    QVERIFY(xmlReader.readNextStartElement() == true);

    QVERIFY(xmlReader.name().toString() == "DocumentTag");

    while (xmlReader.readNextStartElement())
    {
        if (xmlReader.name() == KXMLQLCCreatorAuthor)
        {
            author = true;
            QVERIFY(xmlReader.readElementText() == "TestUnit");
        }
        else if (xmlReader.name() == KXMLQLCCreator)
            creatorTag = true;
        else if (xmlReader.name() == KXMLQLCCreatorName)
        {
            appname = true;
            xmlReader.skipCurrentElement();
        }
        else if (xmlReader.name() == KXMLQLCCreatorVersion)
        {
            appversion = true;
            xmlReader.skipCurrentElement();
        }
    }

    QVERIFY(creatorTag == true);
    QVERIFY(appname == true);
    QVERIFY(appversion == true);
    QVERIFY(author == true);
}

void QLCFile_Test::errorString()
{
    QCOMPARE(QLCFile::errorString(QFile::NoError),
             tr("No error occurred."));
    QCOMPARE(QLCFile::errorString(QFile::ReadError),
             tr("An error occurred when reading from the file."));
    QCOMPARE(QLCFile::errorString(QFile::WriteError),
             tr("An error occurred when writing to the file."));
    QCOMPARE(QLCFile::errorString(QFile::FatalError),
             tr("A fatal error occurred."));
    QCOMPARE(QLCFile::errorString(QFile::ResourceError),
             tr("Resource error occurred."));
    QCOMPARE(QLCFile::errorString(QFile::OpenError),
             tr("The file could not be opened."));
    QCOMPARE(QLCFile::errorString(QFile::AbortError),
             tr("The operation was aborted."));
    QCOMPARE(QLCFile::errorString(QFile::TimeOutError),
             tr("A timeout occurred."));
    QCOMPARE(QLCFile::errorString(QFile::UnspecifiedError),
             tr("An unspecified error occurred."));
    QCOMPARE(QLCFile::errorString(QFile::RemoveError),
             tr("The file could not be removed."));
    QCOMPARE(QLCFile::errorString(QFile::RenameError),
             tr("The file could not be renamed."));
    QCOMPARE(QLCFile::errorString(QFile::PositionError),
             tr("The position in the file could not be changed."));
    QCOMPARE(QLCFile::errorString(QFile::ResizeError),
             tr("The file could not be resized."));
    QCOMPARE(QLCFile::errorString(QFile::PermissionsError),
             tr("The file could not be accessed."));
    QCOMPARE(QLCFile::errorString(QFile::CopyError),
             tr("The file could not be copied."));
    QCOMPARE(QLCFile::errorString(QFile::FileError(31337)),
             tr("An unknown error occurred."));
}

void QLCFile_Test::version()
{
    QVERIFY(QLCFile::getQtRuntimeVersion() > 40000);
}

void QLCFile_Test::windowManager()
{
    QVERIFY(QLCFile::hasWindowManager() == true);

    QLCFile::setHasWindowManager(false);
    QVERIFY(QLCFile::hasWindowManager() == false);
}

void QLCFile_Test::writeXMLHeaderInvalid()
{
    QVERIFY(QLCFile::writeXMLHeader(NULL, "DocumentTag") == false);

    // a writer without a device cannot be written to either
    QXmlStreamWriter doc;
    QVERIFY(QLCFile::writeXMLHeader(&doc, "DocumentTag") == false);
}

/** Point the process' idea of the home directory somewhere else for the
 *  duration of a scope, restoring the previous value on the way out (also
 *  on an early return from a failed assertion) */
struct ScopedHomeEnv
{
    QByteArray name;
    QByteArray previous;
    bool wasSet;

    explicit ScopedHomeEnv(const QString &home)
    {
#if defined(WIN32) || defined(Q_OS_WIN)
        name = "USERPROFILE";
#else
        name = "HOME";
#endif
        wasSet = qEnvironmentVariableIsSet(name.constData());
        previous = qgetenv(name.constData());
        qputenv(name.constData(), QDir::toNativeSeparators(home).toLocal8Bit());
    }

    ~ScopedHomeEnv()
    {
        if (wasSet)
            qputenv(name.constData(), previous);
        else
            qunsetenv(name.constData());
    }
};

void QLCFile_Test::userDirectory()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QString homePath = QDir::cleanPath(home.path());

    ScopedHomeEnv env(homePath);

    // a directory that does not exist yet is created on the way
    QDir dir = QLCFile::userDirectory("qlcplus-test/profiles", "/unused/fallback",
                                      QStringList() << "*.foo" << "*.bar");
    const QString expected = homePath + "/qlcplus-test/profiles";
    QVERIFY2(QDir::cleanPath(dir.absolutePath()).compare(expected, Qt::CaseInsensitive) == 0,
             qPrintable(dir.absolutePath()));
    QVERIFY(QDir(expected).exists());
    QVERIFY(dir.filter() & QDir::Files);
    QCOMPARE(dir.nameFilters(), QStringList() << "*.foo" << "*.bar");

    // second time round it exists already
    QDir again = QLCFile::userDirectory("qlcplus-test/profiles", "/unused/fallback", QStringList());
    QCOMPARE(QDir::cleanPath(again.absolutePath()).toLower(), expected.toLower());
}

void QLCFile_Test::fileUrlPrefix()
{
#if defined(WIN32) || defined(Q_OS_WIN)
    QCOMPARE(QLCFile::fileUrlPrefix(), QString("file:///"));
#else
    QCOMPARE(QLCFile::fileUrlPrefix(), QString("file://"));
#endif
    QVERIFY(QUrl(QLCFile::fileUrlPrefix() + "tmp/x.qxw").isLocalFile());
}

QTEST_APPLESS_MAIN(QLCFile_Test)
