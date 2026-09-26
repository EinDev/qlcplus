/*
  Q Light Controller Plus - Unit test
  mediaassets_test.cpp

  Copyright (c) EinDev

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

#include <QCryptographicHash>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QSignalSpy>
#include <QFileInfo>
#include <QBuffer>
#include <QtTest>
#include <QDir>

#include "mediaassets_test.h"

#define protected public
#define private public
#include "mediaassets.h"
#include "audio.h"
#include "video.h"
#include "scene.h"
#include "doc.h"
#undef private
#undef protected

void MediaAssets_Test::init()
{
    m_doc = new Doc(this);
    m_tmp = new QTemporaryDir();
    QVERIFY(m_tmp->isValid());
}

void MediaAssets_Test::cleanup()
{
    delete m_doc;
    m_doc = nullptr;
    delete m_tmp;
    m_tmp = nullptr;
}

QString MediaAssets_Test::writeFile(const QString &relativePath, const QByteArray &content)
{
    QString path = m_tmp->path() + "/" + relativePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) == false)
        return QString();
    f.write(content);
    f.close();
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString MediaAssets_Test::hashDirFor(const QByteArray &content)
{
    return QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha1).toHex().left(12));
}

QString MediaAssets_Test::hashDirForFile(const QString &path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadOnly) == false)
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(&f);
    return QString::fromLatin1(hash.result().toHex().left(12));
}

QString MediaAssets_Test::writeLargeFile(const QString &relativePath, int megabytes)
{
    QString path = m_tmp->path() + "/" + relativePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.open(QIODevice::WriteOnly) == false)
        return QString();

    // one megabyte of varying bytes, written @megabytes times with a
    // different first byte each round so the file is not a plain repeat
    QByteArray chunk(1024 * 1024, Qt::Uninitialized);
    for (int i = 0; i < chunk.size(); i++)
        chunk[i] = char((i * 31 + 7) & 0xff);
    for (int i = 0; i < megabytes; i++)
    {
        chunk[0] = char(i);
        f.write(chunk);
    }
    f.close();
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

void MediaAssets_Test::importCreatesHashDir()
{
    const QString project = m_tmp->path() + "/proj/show.qxw";
    m_doc->setWorkspacePath(m_tmp->path() + "/proj");
    m_doc->assets()->setProjectFile(project);

    QCOMPARE(m_doc->assets()->isStaging(), false);
    QCOMPARE(m_doc->assets()->assetsDirName(), QString("show.qxw.assets"));
    QCOMPARE(m_doc->assets()->assetsDir(), QDir::cleanPath(m_tmp->path() + "/proj/show.qxw.assets"));

    QString source = writeFile("src/a.wav", "hello");
    QString error;
    QString stored = m_doc->assets()->importFile(source, &error);
    QVERIFY2(stored.isEmpty() == false, qPrintable(error));
    QCOMPARE(stored, m_doc->assets()->assetsDir() + "/" + hashDirFor("hello") + "/a.wav");
    QVERIFY(QFile::exists(stored));
    QVERIFY(QFile::exists(source));   // the original is never touched

    QFile f(stored);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("hello"));

    QVERIFY(m_doc->assets()->isManaged(stored));
    QVERIFY(m_doc->assets()->isManaged(source) == false);
    QVERIFY(m_doc->assets()->isManaged(m_doc->assets()->assetsDir() + "/a.wav") == false);

    // no .partial leftovers: only the hash directory and the provenance manifest
    QDir store(m_doc->assets()->assetsDir());
    QCOMPARE(store.entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList() << hashDirFor("hello"));
    QCOMPARE(store.entryList(QDir::Files), QStringList() << MediaAssets::manifestFileName());

    // nothing references the file yet
    QCOMPARE(m_doc->assets()->referenced(), QStringList());
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList() << stored);
}

void MediaAssets_Test::sameContentTwice()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString first = writeFile("one/a.wav", "hello");
    QString second = writeFile("two/a.wav", "hello");
    QString stored1 = m_doc->assets()->importFile(first);
    QString stored2 = m_doc->assets()->importFile(second);
    QString stored3 = m_doc->assets()->importFile(first);
    QVERIFY(stored1.isEmpty() == false);
    QCOMPARE(stored2, stored1);
    QCOMPARE(stored3, stored1);

    QDir hashDir(m_doc->assets()->assetsDir() + "/" + hashDirFor("hello"));
    QCOMPARE(hashDir.entryList(QDir::Files), QStringList() << "a.wav");
    QDir store(m_doc->assets()->assetsDir());
    QCOMPARE(store.entryList(QDir::Dirs | QDir::NoDotAndDotDot).count(), 1);
}

void MediaAssets_Test::differentContentSameBasename()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString stored1 = m_doc->assets()->importFile(writeFile("one/a.wav", "hello"));
    QString stored2 = m_doc->assets()->importFile(writeFile("two/a.wav", "world"));
    QVERIFY(stored1.isEmpty() == false);
    QVERIFY(stored2.isEmpty() == false);
    QVERIFY(stored1 != stored2);
    QCOMPARE(QFileInfo(stored1).fileName(), QString("a.wav"));
    QCOMPARE(QFileInfo(stored2).fileName(), QString("a.wav"));
    QCOMPARE(QFileInfo(stored1).dir().dirName(), hashDirFor("hello"));
    QCOMPARE(QFileInfo(stored2).dir().dirName(), hashDirFor("world"));
    QVERIFY(QFile::exists(stored1));
    QVERIFY(QFile::exists(stored2));
}

void MediaAssets_Test::sameContentDifferentBasename()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    // Dedupe is keyed on <hash>/<basename>: the same bytes under another
    // name get their own file, so the function name (derived from the
    // basename) stays what the user picked
    QString stored1 = m_doc->assets()->importFile(writeFile("one/a.wav", "hello"));
    QString stored2 = m_doc->assets()->importFile(writeFile("one/b.wav", "hello"));
    QVERIFY(stored1 != stored2);
    QCOMPARE(QFileInfo(stored1).absolutePath(), QFileInfo(stored2).absolutePath());
    QDir hashDir(QFileInfo(stored1).absolutePath());
    QCOMPARE(hashDir.entryList(QDir::Files, QDir::Name), QStringList() << "a.wav" << "b.wav");
}

void MediaAssets_Test::importMissingFails()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString error;
    QCOMPARE(m_doc->assets()->importFile(m_tmp->path() + "/nope.wav", &error), QString());
    QVERIFY(error.isEmpty() == false);
    QVERIFY(QDir(m_doc->assets()->assetsDir()).exists() == false);

    // a directory is not importable either
    error.clear();
    QCOMPARE(m_doc->assets()->importFile(m_tmp->path(), &error), QString());
    QVERIFY(error.isEmpty() == false);
}

void MediaAssets_Test::importAlreadyManaged()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString stored = m_doc->assets()->importFile(writeFile("src/a.wav", "hello"));
    QVERIFY(stored.isEmpty() == false);
    // re-importing a stored path is a no-op returning the same path
    QCOMPARE(m_doc->assets()->importFile(stored), stored);
    QDir hashDir(QFileInfo(stored).absolutePath());
    QCOMPARE(hashDir.entryList(QDir::Files), QStringList() << "a.wav");
}

void MediaAssets_Test::xmlRoundTrip()
{
    const QString workspace = QDir::cleanPath(m_tmp->path() + "/proj");
    QDir().mkpath(workspace);
    m_doc->setWorkspacePath(workspace);
    m_doc->assets()->setProjectFile(workspace + "/show.qxw");

    QString storedWav = m_doc->assets()->importFile(writeFile("src/song.wav", "wav-bytes"));
    QString storedMp4 = m_doc->assets()->importFile(writeFile("src/clip.mp4", "mp4-bytes"));
    QVERIFY(storedWav.isEmpty() == false);
    QVERIFY(storedMp4.isEmpty() == false);

    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(storedWav);
    QVERIFY(m_doc->addFunction(audio));
    Video *video = new Video(m_doc);
    video->setSourceUrl(storedMp4);
    QVERIFY(m_doc->addFunction(video));
    QCOMPARE(audio->name(), QString("song.wav"));
    QCOMPARE(video->name(), QString("clip.mp4"));

    QCOMPARE(m_doc->assets()->referenced().count(), 2);
    QCOMPARE(m_doc->assets()->externalSources(), QStringList());
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList());

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(m_doc->saveXML(&xmlWriter));
    xmlWriter.setDevice(nullptr);
    buffer.close();

    const QString xml = QString::fromUtf8(buffer.data());
    QVERIFY2(xml.contains("<Source>show.qxw.assets/" + hashDirFor("wav-bytes") + "/song.wav</Source>"), qPrintable(xml));
    QVERIFY2(xml.contains(">show.qxw.assets/" + hashDirFor("mp4-bytes") + "/clip.mp4</Source>"), qPrintable(xml));
    QVERIFY(xml.contains(workspace) == false);   // nothing absolute leaked into the file

    // load into a fresh Doc bound the way App::loadXML binds it
    Doc other(this);
    other.setWorkspacePath(workspace);
    other.assets()->setProjectFile(workspace + "/show.qxw");

    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QVERIFY(other.loadXML(xmlReader));
    buffer.close();

    QList<Function *> audios = other.functionsByType(Function::AudioType);
    QList<Function *> videos = other.functionsByType(Function::VideoType);
    QCOMPARE(audios.count(), 1);
    QCOMPARE(videos.count(), 1);
    QCOMPARE(static_cast<Audio *>(audios.first())->getSourceFileName(), storedWav);
    QCOMPARE(static_cast<Video *>(videos.first())->sourceUrl(), storedMp4);
    QVERIFY(other.assets()->isManaged(static_cast<Audio *>(audios.first())->getSourceFileName()));
    QVERIFY(other.assets()->isManaged(static_cast<Video *>(videos.first())->sourceUrl()));
    QCOMPARE(other.assets()->externalSources(), QStringList());
}

void MediaAssets_Test::relocateCopiesOnlyReferenced()
{
    const QString oldWs = QDir::cleanPath(m_tmp->path() + "/old");
    const QString newWs = QDir::cleanPath(m_tmp->path() + "/new");
    QDir().mkpath(oldWs);
    m_doc->setWorkspacePath(oldWs);
    m_doc->assets()->setProjectFile(oldWs + "/a.qxw");

    QString used = m_doc->assets()->importFile(writeFile("src/used.wav", "used"));
    QString unused = m_doc->assets()->importFile(writeFile("src/unused.wav", "unused"));
    QString external = writeFile("src/external.mp4", "external");
    QVERIFY(used.isEmpty() == false);
    QVERIFY(unused.isEmpty() == false);

    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(used);
    QVERIFY(m_doc->addFunction(audio));
    Video *video = new Video(m_doc);
    video->setSourceUrl(external);
    QVERIFY(m_doc->addFunction(video));
    Video *url = new Video(m_doc);
    url->setSourceUrl("http://example.com/stream.mp4");
    QVERIFY(m_doc->addFunction(url));

    QCOMPARE(m_doc->assets()->externalSources(), QStringList() << external);
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList() << unused);

    QSignalSpy spy(audio, SIGNAL(sourceFilenameChanged()));
    QString error;
    QVERIFY2(m_doc->assets()->relocateTo(newWs + "/b.qxw", &error), qPrintable(error));
    m_doc->setWorkspacePath(newWs);

    const QString newStore = newWs + "/b.qxw.assets";
    QCOMPARE(m_doc->assets()->assetsDir(), newStore);
    QCOMPARE(m_doc->assets()->assetsDirName(), QString("b.qxw.assets"));
    QCOMPARE(audio->getSourceFileName(), newStore + "/" + hashDirFor("used") + "/used.wav");
    QVERIFY(QFile::exists(audio->getSourceFileName()));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(audio->name(), QString("used.wav"));

    // only the referenced managed file travelled, the old store is untouched
    QVERIFY(QFile::exists(newStore + "/" + hashDirFor("unused") + "/unused.wav") == false);
    QVERIFY(QFile::exists(used));
    QVERIFY(QFile::exists(unused));
    QCOMPARE(video->sourceUrl(), external);
    QCOMPARE(url->sourceUrl(), QString("http://example.com/stream.mp4"));
    QCOMPARE(m_doc->assets()->externalSources(), QStringList() << external);
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList());
    QVERIFY(m_doc->assets()->isManaged(audio->getSourceFileName()));
    QCOMPARE(m_doc->normalizeComponentPath(audio->getSourceFileName()),
             "b.qxw.assets/" + hashDirFor("used") + "/used.wav");
}

void MediaAssets_Test::relocateSameLocationIsNoop()
{
    const QString ws = QDir::cleanPath(m_tmp->path() + "/proj");
    QDir().mkpath(ws);
    m_doc->setWorkspacePath(ws);
    m_doc->assets()->setProjectFile(ws + "/show.qxw");

    QString stored = m_doc->assets()->importFile(writeFile("src/a.wav", "hello"));
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(stored);
    QVERIFY(m_doc->addFunction(audio));

    QDateTime before = QFileInfo(stored).lastModified();
    QSignalSpy spy(audio, SIGNAL(sourceFilenameChanged()));
    QVERIFY(m_doc->assets()->relocateTo(ws + "/show.qxw"));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(audio->getSourceFileName(), stored);
    QCOMPARE(QFileInfo(stored).lastModified(), before);
    QCOMPARE(m_doc->assets()->assetsDir(), ws + "/show.qxw.assets");
    QCOMPARE(QDir(ws).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList() << "show.qxw.assets");
}

void MediaAssets_Test::stagingThenRelocate()
{
    // untitled project: nothing bound, imports go to a temporary directory
    QVERIFY(m_doc->assets()->isStaging());
    QCOMPARE(m_doc->assets()->assetsDir(), QString());
    QCOMPARE(m_doc->assets()->assetsDirName(), QString());

    QString stagedWav = m_doc->assets()->importFile(writeFile("src/song.wav", "wav-bytes"));
    QString stagedMp4 = m_doc->assets()->importFile(writeFile("src/clip.mp4", "mp4-bytes"));
    QString stagedUnused = m_doc->assets()->importFile(writeFile("src/unused.wav", "unused"));
    QVERIFY(stagedWav.isEmpty() == false);
    QVERIFY(stagedMp4.isEmpty() == false);
    QVERIFY(stagedUnused.isEmpty() == false);

    const QString staging = m_doc->assets()->assetsDir();
    QVERIFY(staging.isEmpty() == false);
    QVERIFY(QDir(staging).exists());
    QVERIFY(staging.startsWith(QDir::cleanPath(QDir::tempPath()), Qt::CaseInsensitive));
    QVERIFY(stagedWav.startsWith(staging));
    QVERIFY(m_doc->assets()->isManaged(stagedWav));

    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(stagedWav);
    QVERIFY(m_doc->addFunction(audio));
    Video *video = new Video(m_doc);
    video->setSourceUrl(stagedMp4);
    QVERIFY(m_doc->addFunction(video));

    const QString ws = QDir::cleanPath(m_tmp->path() + "/saved");
    QDir().mkpath(ws);
    QString error;
    QVERIFY2(m_doc->assets()->relocateTo(ws + "/first.qxw", &error), qPrintable(error));
    m_doc->setWorkspacePath(ws);

    QCOMPARE(m_doc->assets()->isStaging(), false);
    const QString store = ws + "/first.qxw.assets";
    QCOMPARE(m_doc->assets()->assetsDir(), store);
    QCOMPARE(audio->getSourceFileName(), store + "/" + hashDirFor("wav-bytes") + "/song.wav");
    QCOMPARE(video->sourceUrl(), store + "/" + hashDirFor("mp4-bytes") + "/clip.mp4");
    QVERIFY(QFile::exists(audio->getSourceFileName()));
    QVERIFY(QFile::exists(video->sourceUrl()));
    QVERIFY(QFile::exists(store + "/" + hashDirFor("unused") + "/unused.wav") == false);
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList());
    QCOMPARE(m_doc->normalizeComponentPath(video->sourceUrl()),
             "first.qxw.assets/" + hashDirFor("mp4-bytes") + "/clip.mp4");

    // the staging directory survives the relocation (decoders may still hold
    // the old files open) and is dropped when the project is closed
    QVERIFY(QDir(staging).exists());
    m_doc->clearContents();
    m_doc->assets()->setProjectFile(QString());
    QVERIFY(m_doc->assets()->isStaging());
    QVERIFY(QDir(staging).exists() == false);
}

void MediaAssets_Test::relinkKeepsBpmAndName()
{
    QString wav = writeFile("src/song.wav", "wav-bytes");
    QString moved = writeFile("elsewhere/song.wav", "wav-bytes");

    // an Audio loaded with an analysed BPM, the way a saved project comes back
    QString xml = QString(
        "<Function ID=\"7\" Type=\"Audio\" Name=\"My Song\">"
        " <Source>%1</Source>"
        " <Bpm bpm=\"128.00\" period=\"468.75\" phase=\"12.50\" confidence=\"0.900\"/>"
        "</Function>").arg(wav);
    QByteArray data = xml.toUtf8();
    QBuffer buffer(&data);
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader reader(&buffer);
    reader.readNextStartElement();

    Audio audio(m_doc);
    QVERIFY(audio.loadXML(reader));
    QCOMPARE(audio.getSourceFileName(), wav);
    // Function::loader() applies the Name attribute before loadXML(); done
    // by hand here since the element is loaded directly
    audio.setName("My Song");
    QCOMPARE(audio.bpmAnalysisState(), Audio::Done);
    QCOMPARE(audio.detectedBpm(), 128.0);

    QSignalSpy sourceSpy(&audio, SIGNAL(sourceFilenameChanged()));
    QSignalSpy bpmSpy(&audio, SIGNAL(bpmChanged()));
    QSignalSpy changedSpy(&audio, SIGNAL(changed(quint32)));
    audio.relinkSource(moved);
    QCOMPARE(audio.getSourceFileName(), moved);
    QCOMPARE(audio.name(), QString("My Song"));
    QCOMPARE(audio.bpmAnalysisState(), Audio::Done);
    QCOMPARE(audio.detectedBpm(), 128.0);
    QCOMPARE(audio.beatPeriodMs(), 468.75);
    QCOMPARE(audio.beatPhaseMs(), 12.5);
    QCOMPARE(audio.bpmConfidence(), 0.9);
    QCOMPARE(sourceSpy.count(), 1);
    QCOMPARE(bpmSpy.count(), 0);
    QCOMPARE(changedSpy.count(), 1);

    // the full setter, by contrast, still wipes the analysis and renames
    audio.setSourceFileName(wav);
    QCOMPARE(audio.bpmAnalysisState(), Audio::NotAnalyzed);
    QCOMPARE(audio.name(), QString("song.wav"));

    Video video(m_doc);
    video.setSourceUrl(writeFile("src/clip.mp4", "mp4"));
    video.setName("Intro");
    QSignalSpy videoSpy(&video, SIGNAL(sourceChanged(QString)));
    QString movedClip = writeFile("elsewhere/clip.mp4", "mp4");
    video.relinkSource(movedClip);
    QCOMPARE(video.sourceUrl(), movedClip);
    QCOMPARE(video.name(), QString("Intro"));
    QCOMPARE(videoSpy.count(), 1);
    QCOMPARE(videoSpy.first().first().toString(), movedClip);
}

void MediaAssets_Test::unreferencedAfterDelete()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString stored = m_doc->assets()->importFile(writeFile("src/a.wav", "hello"));
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(stored);
    QVERIFY(m_doc->addFunction(audio));
    quint32 id = audio->id();

    QCOMPARE(m_doc->assets()->referenced(), QStringList() << stored);
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList());

    QVERIFY(m_doc->deleteFunction(id));
    QCOMPARE(m_doc->assets()->referenced(), QStringList());
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList() << stored);
    QVERIFY(QFile::exists(stored));   // never deleted implicitly
}

void MediaAssets_Test::normalizeCaseInsensitive()
{
#ifdef Q_OS_WIN
    const QString ws = QDir::cleanPath(m_tmp->path());
    QVERIFY(ws.length() > 2 && ws.at(1) == ':');

    // Same directory, every component in the other case: Qt normalises the
    // drive letter itself but not the folder names, and Windows hands the
    // same folder out in different spellings depending on who produced it
    const QString swapped = ws.toUpper();
    QVERIFY(swapped != ws);
    m_doc->setWorkspacePath(swapped);
    QCOMPARE(m_doc->normalizeComponentPath(ws + "/show.qxw.assets/abc/x.wav"),
             QString("show.qxw.assets/abc/x.wav"));
    QCOMPARE(m_doc->normalizeComponentPath(swapped + "/show.qxw.assets/abc/x.wav"),
             QString("show.qxw.assets/abc/x.wav"));
    QCOMPARE(m_doc->normalizeComponentPath(ws.left(1).toLower() + ws.mid(1) + "/show.qxw.assets/abc/x.wav"),
             QString("show.qxw.assets/abc/x.wav"));
#else
    QSKIP("Case-insensitive workspace paths are a Windows concern");
#endif
}

/*****************************************************************************
 * Increment 2: background copies
 *****************************************************************************/

void MediaAssets_Test::smallFileStaysSynchronous()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");
    QCOMPARE(m_doc->assets()->backgroundThreshold(), MediaAssets::DefaultBackgroundThreshold);

    QSignalSpy startedSpy(m_doc->assets(), SIGNAL(importStarted(QString,qint64)));
    QString source = writeFile("src/a.wav", "hello");
    QString stored = m_doc->assets()->importOrKeep(source);
    QCOMPARE(stored, m_doc->assets()->assetsDir() + "/" + hashDirFor("hello") + "/a.wav");
    QVERIFY(QFile::exists(stored));
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);
    QCOMPARE(m_doc->assets()->pendingImports(), QStringList());
    QCOMPARE(startedSpy.count(), 0);
}

void MediaAssets_Test::backgroundImportRelinksLargeFile()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    // 60 MB: above the default 50 MB threshold, still quick to write and hash
    const int megabytes = 60;
    QString source = writeLargeFile("src/big.wav", megabytes);
    QVERIFY(source.isEmpty() == false);
    QCOMPARE(QFileInfo(source).size(), qint64(megabytes) * 1024 * 1024);

    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(source);
    QVERIFY(m_doc->addFunction(audio));
    QCOMPARE(m_doc->assets()->externalSources(), QStringList() << source);

    QSignalSpy startedSpy(m_doc->assets(), SIGNAL(importStarted(QString,qint64)));
    QSignalSpy progressSpy(m_doc->assets(), SIGNAL(importProgress(QString,qint64,qint64)));
    QSignalSpy finishedSpy(m_doc->assets(), SIGNAL(importFinished(QString,QString,QString)));
    QSignalSpy pendingSpy(m_doc->assets(), SIGNAL(pendingImportsChanged()));
    QSignalSpy sourceSpy(audio, SIGNAL(sourceFilenameChanged()));

    // returns right away with the source itself, the copy runs on a thread
    QString returned = m_doc->assets()->importOrKeep(source);
    QCOMPARE(returned, source);
    QVERIFY(m_doc->assets()->hasPendingImports());
    QCOMPARE(m_doc->assets()->pendingImports(), QStringList() << source);
    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(startedSpy.first().at(0).toString(), source);
    QCOMPARE(startedSpy.first().at(1).toLongLong(), qint64(megabytes) * 1024 * 1024);
    QCOMPARE(audio->getSourceFileName(), source);   // nothing relinked yet
    QCOMPARE(finishedSpy.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 60000);

    const QString expected = m_doc->assets()->assetsDir() + "/" + hashDirForFile(source) + "/big.wav";
    QCOMPARE(finishedSpy.first().at(0).toString(), source);
    QCOMPARE(finishedSpy.first().at(1).toString(), expected);
    QCOMPARE(finishedSpy.first().at(2).toString(), QString());
    QVERIFY(QFile::exists(expected));
    QCOMPARE(QFileInfo(expected).size(), QFileInfo(source).size());
    QVERIFY(m_doc->assets()->isManaged(expected));

    // the function followed the copy, without the full setter's side effects
    QCOMPARE(audio->getSourceFileName(), expected);
    QCOMPARE(sourceSpy.count(), 1);
    QCOMPARE(m_doc->assets()->externalSources(), QStringList());
    QCOMPARE(m_doc->assets()->referenced(), QStringList() << expected);
    QVERIFY(m_doc->isModified());

    QCOMPARE(m_doc->assets()->hasPendingImports(), false);
    QVERIFY(pendingSpy.count() >= 2);
    QVERIFY(progressSpy.count() > 0);
    QCOMPARE(progressSpy.last().at(1).toLongLong(), qint64(megabytes) * 1024 * 1024);
    QCOMPARE(progressSpy.last().at(2).toLongLong(), qint64(megabytes) * 1024 * 1024);

    // no .partial leftovers (the manifest is the only file at the store root)
    QDir store(m_doc->assets()->assetsDir());
    QCOMPARE(store.entryList(QDir::Dirs | QDir::NoDotAndDotDot),
             QStringList() << hashDirForFile(source));
    QCOMPARE(store.entryList(QDir::Files), QStringList() << MediaAssets::manifestFileName());
}

void MediaAssets_Test::backgroundImportDedupesAndRelinksVideo()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");
    m_doc->assets()->setBackgroundThreshold(16);   // everything goes through the thread

    QString clip = writeFile("src/clip.mp4", "a video, not really but long enough");
    QString song = writeFile("src/song.wav", "an audio file, again long enough");

    Video *video = new Video(m_doc);
    video->setSourceUrl(clip);
    video->setName("Intro");
    QVERIFY(m_doc->addFunction(video));
    Video *stream = new Video(m_doc);
    stream->setSourceUrl("http://example.org/live.m3u8");
    QVERIFY(m_doc->addFunction(stream));
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(song);
    QVERIFY(m_doc->addFunction(audio));

    QSignalSpy finishedSpy(m_doc->assets(), SIGNAL(importFinished(QString,QString,QString)));

    QCOMPARE(m_doc->assets()->importOrKeep(clip), clip);
    QCOMPARE(m_doc->assets()->importOrKeep(clip), clip);   // same source twice: one job
    QCOMPARE(m_doc->assets()->importOrKeep(song), song);
    QCOMPARE(m_doc->assets()->pendingImports(), QStringList() << clip << song);

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 10000);
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);

    QString storedClip = m_doc->assets()->assetsDir() + "/" + hashDirFor("a video, not really but long enough") + "/clip.mp4";
    QString storedSong = m_doc->assets()->assetsDir() + "/" + hashDirFor("an audio file, again long enough") + "/song.wav";
    QCOMPARE(video->sourceUrl(), storedClip);
    QCOMPARE(video->name(), QString("Intro"));
    QCOMPARE(audio->getSourceFileName(), storedSong);
    QCOMPARE(stream->sourceUrl(), QString("http://example.org/live.m3u8"));

    // a source that is already stored is not queued again
    QCOMPARE(m_doc->assets()->importOrKeep(storedClip), storedClip);
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);
}

void MediaAssets_Test::backgroundImportFollowsRelocate()
{
    // untitled project: the copy lands in the staging directory, the first
    // save moves the store while the job is still in flight
    m_doc->assets()->setBackgroundThreshold(16);
    QVERIFY(m_doc->assets()->isStaging());

    QString clip = writeFile("src/clip.mp4", "a video, not really but long enough");
    Video *video = new Video(m_doc);
    video->setSourceUrl(clip);
    QVERIFY(m_doc->addFunction(video));

    QSignalSpy finishedSpy(m_doc->assets(), SIGNAL(importFinished(QString,QString,QString)));
    QCOMPARE(m_doc->assets()->importOrKeep(clip), clip);
    QVERIFY(m_doc->assets()->hasPendingImports());
    const QString staging = m_doc->assets()->assetsDir();

    // save before the (queued) completion is processed
    const QString project = m_tmp->path() + "/saved/show.qxw";
    QDir().mkpath(m_tmp->path() + "/saved");
    m_doc->setWorkspacePath(m_tmp->path() + "/saved");
    QVERIFY(m_doc->assets()->relocateTo(project));
    QCOMPARE(video->sourceUrl(), clip);   // still external at save time

    const QString hashDir = hashDirFor("a video, not really but long enough");
    // first completion relinks to the staging copy, which is then chained
    // into the real store by a second job
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 2, 10000);
    QCOMPARE(finishedSpy.at(0).at(1).toString(), staging + "/" + hashDir + "/clip.mp4");
    QCOMPARE(finishedSpy.at(1).at(1).toString(), m_doc->assets()->assetsDir() + "/" + hashDir + "/clip.mp4");
    QCOMPARE(video->sourceUrl(), m_doc->assets()->assetsDir() + "/" + hashDir + "/clip.mp4");
    QVERIFY(m_doc->assets()->isManaged(video->sourceUrl()));
    QCOMPARE(m_doc->assets()->externalSources(), QStringList());
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);
}

void MediaAssets_Test::closingProjectCancelsBackgroundImport()
{
    m_doc->assets()->setBackgroundThreshold(16);
    QString big = writeLargeFile("src/big.wav", 8);
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(big);
    QVERIFY(m_doc->addFunction(audio));

    QSignalSpy finishedSpy(m_doc->assets(), SIGNAL(importFinished(QString,QString,QString)));
    QCOMPARE(m_doc->assets()->importOrKeep(big), big);
    QVERIFY(m_doc->assets()->hasPendingImports());

    // what App::clearDocument() does: no crash, no late relink, no leftovers
    m_doc->assets()->setProjectFile(QString());
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);
    QTest::qWait(50);
    QCOMPARE(finishedSpy.count(), 0);
    QCOMPARE(audio->getSourceFileName(), big);
    QCOMPARE(m_doc->assets()->assetsDir(), QString());
}

/*****************************************************************************
 * Increment 3: collect and cleanup
 *****************************************************************************/

void MediaAssets_Test::collectExternalImportsAndRelinks()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString song = writeFile("ext/song.wav", "song");
    QString clip = writeFile("ext/clip.mp4", "clip");
    QString managed = m_doc->assets()->importFile(writeFile("ext/done.wav", "done"));
    QString missing = QDir::cleanPath(m_tmp->path() + "/ext/gone.wav");

    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(song);
    audio->setName("Song");
    QVERIFY(m_doc->addFunction(audio));
    Audio *audio2 = new Audio(m_doc);
    audio2->setSourceFileName(song);   // two functions on the same file
    QVERIFY(m_doc->addFunction(audio2));
    Video *video = new Video(m_doc);
    video->setSourceUrl(clip);
    video->setName("Clip");
    QVERIFY(m_doc->addFunction(video));
    Video *stream = new Video(m_doc);
    stream->setSourceUrl("rtsp://cam/1");
    QVERIFY(m_doc->addFunction(stream));
    Audio *already = new Audio(m_doc);
    already->setSourceFileName(managed);
    QVERIFY(m_doc->addFunction(already));
    Audio *lost = new Audio(m_doc);
    lost->setSourceFileName(missing);
    QVERIFY(m_doc->addFunction(lost));

    QCOMPARE(m_doc->assets()->externalSources().count(), 3);

    MediaAssets::CollectResult result = m_doc->assets()->collectExternal();
    QCOMPARE(result.copied, 2);
    QCOMPARE(result.queued, 0);
    QCOMPARE(result.failed, 1);
    QVERIFY(result.firstError.contains("gone.wav"));

    QString storedSong = m_doc->assets()->assetsDir() + "/" + hashDirFor("song") + "/song.wav";
    QString storedClip = m_doc->assets()->assetsDir() + "/" + hashDirFor("clip") + "/clip.mp4";
    QCOMPARE(audio->getSourceFileName(), storedSong);
    QCOMPARE(audio2->getSourceFileName(), storedSong);
    QCOMPARE(audio->name(), QString("Song"));
    QCOMPARE(video->sourceUrl(), storedClip);
    QCOMPARE(video->name(), QString("Clip"));
    QCOMPARE(stream->sourceUrl(), QString("rtsp://cam/1"));
    QCOMPARE(already->getSourceFileName(), managed);
    QCOMPARE(lost->getSourceFileName(), missing);
    QCOMPARE(m_doc->assets()->externalSources(), QStringList() << missing);
    QVERIFY(QFile::exists(song));   // originals are never touched
    QVERIFY(QFile::exists(clip));

    // nothing left to do the second time round, apart from the missing one
    result = m_doc->assets()->collectExternal();
    QCOMPARE(result.copied, 0);
    QCOMPARE(result.failed, 1);
}

void MediaAssets_Test::collectExternalQueuesLargeFiles()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");
    m_doc->assets()->setBackgroundThreshold(16);

    QString big = writeFile("ext/big.wav", "this one is above the tiny threshold");
    QString small = writeFile("ext/small.wav", "small");
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(big);
    QVERIFY(m_doc->addFunction(audio));
    Audio *audio2 = new Audio(m_doc);
    audio2->setSourceFileName(small);
    QVERIFY(m_doc->addFunction(audio2));

    QSignalSpy finishedSpy(m_doc->assets(), SIGNAL(importFinished(QString,QString,QString)));
    MediaAssets::CollectResult result = m_doc->assets()->collectExternal();
    QCOMPARE(result.copied, 1);
    QCOMPARE(result.queued, 1);
    QCOMPARE(result.failed, 0);
    QVERIFY(m_doc->assets()->isManaged(audio2->getSourceFileName()));
    QCOMPARE(audio->getSourceFileName(), big);
    QVERIFY(m_doc->assets()->hasPendingImports());

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QVERIFY(m_doc->assets()->isManaged(audio->getSourceFileName()));
    QCOMPARE(m_doc->assets()->externalSources(), QStringList());
}

void MediaAssets_Test::removeUnreferencedDeletesOnlyStoreFiles()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString used = m_doc->assets()->importFile(writeFile("src/used.wav", "used"));
    QString unused = m_doc->assets()->importFile(writeFile("src/unused.wav", "unused"));
    QString external = writeFile("src/external.wav", "external");
    // a stray file directly in the store root is not "managed" and never touched
    QString stray = writeFile("show.qxw.assets/stray.wav", "stray");

    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(used);
    QVERIFY(m_doc->addFunction(audio));

    QCOMPARE(m_doc->assets()->unreferenced(), QStringList() << unused);

    QString error;
    // everything but @unused is refused, @unused itself is deleted
    QCOMPARE(m_doc->assets()->removeUnreferenced(QStringList() << unused << used << external << stray, &error), false);
    QVERIFY(error.isEmpty() == false);
    QVERIFY(QFile::exists(unused) == false);
    QVERIFY(QDir(QFileInfo(unused).absolutePath()).exists() == false);   // emptied hash dir went too
    QVERIFY(QFile::exists(used));
    QVERIFY(QFile::exists(external));
    QVERIFY(QFile::exists(stray));
    QCOMPARE(audio->getSourceFileName(), used);
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList());

    // a clean list succeeds; an already-gone file is not an error
    error.clear();
    QCOMPARE(m_doc->assets()->removeUnreferenced(QStringList() << unused, &error), true);
    QVERIFY(error.isEmpty());

    // a file that became unreferenced after the list was made is deleted,
    // one that became referenced meanwhile is refused
    QString later = m_doc->assets()->importFile(writeFile("src/later.wav", "later"));
    QVERIFY(m_doc->deleteFunction(audio->id()));
    QCOMPARE(m_doc->assets()->removeUnreferenced(QStringList() << used << later), true);
    QVERIFY(QFile::exists(used) == false);
    QVERIFY(QFile::exists(later) == false);
}

/*****************************************************************************
 * Provenance and reload from origin
 *****************************************************************************/

void MediaAssets_Test::rewriteFile(const QString &path, const QByteArray &content)
{
    QDateTime before = QFileInfo(path).lastModified();

    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(content);
    f.close();

    // an mtime the check cannot mistake for the recorded one
    QFile touch(path);
    QVERIFY(touch.open(QIODevice::ReadWrite));
    QVERIFY(touch.setFileTime(before.addSecs(5), QFileDevice::FileModificationTime));
    touch.close();
}

void MediaAssets_Test::originRecordedOnImport()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString source = writeFile("src/lyrics.mp4", "take one");
    QFileInfo info(source);
    QString stored = m_doc->assets()->importFile(source);
    QVERIFY(stored.isEmpty() == false);

    MediaOrigin origin = m_doc->assets()->origin(stored);
    QVERIFY(origin.isValid());
    QCOMPARE(origin.path, source);
    QCOMPARE(origin.size, info.size());
    QCOMPARE(origin.mtime.toMSecsSinceEpoch(), info.lastModified().toMSecsSinceEpoch());
    QCOMPARE(origin.sha1, QString::fromLatin1(QCryptographicHash::hash("take one", QCryptographicHash::Sha1).toHex()));

    // the manifest sits at the store root, outside the <sha12>/ layout
    QVERIFY(QFile::exists(m_doc->assets()->assetsDir() + "/" + MediaAssets::manifestFileName()));
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList() << stored);

    Video *video = new Video(m_doc);
    video->setSourceUrl(stored);
    QVERIFY(m_doc->addFunction(video));
    QCOMPARE(m_doc->assets()->originOf(video).path, source);
    QVERIFY(m_doc->assets()->originAvailable(video));
    QCOMPARE(m_doc->assets()->originChanged(video), false);
    QVERIFY(m_doc->assets()->changedOrigins().isEmpty());

    // nothing known about a copy that was never imported through the store,
    // nor about an external reference
    Audio *external = new Audio(m_doc);
    external->setSourceFileName(writeFile("src/ext.wav", "external"));
    QVERIFY(m_doc->addFunction(external));
    QVERIFY(m_doc->assets()->originOf(external).isValid() == false);
    QVERIFY(m_doc->assets()->originAvailable(external) == false);
    QCOMPARE(m_doc->assets()->originChanged(external), false);
}

void MediaAssets_Test::originSurvivesSaveLoadAndRelocate()
{
    const QString workspace = QDir::cleanPath(m_tmp->path() + "/proj");
    QDir().mkpath(workspace);
    m_doc->setWorkspacePath(workspace);
    m_doc->assets()->setProjectFile(workspace + "/show.qxw");

    QString source = writeFile("src/song.wav", "wav-bytes");
    QString stored = m_doc->assets()->importFile(source);
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(stored);
    QVERIFY(m_doc->addFunction(audio));

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly | QIODevice::Text);
    QXmlStreamWriter xmlWriter(&buffer);
    QVERIFY(m_doc->saveXML(&xmlWriter));
    xmlWriter.setDevice(nullptr);
    buffer.close();

    // the .qxw itself carries no provenance: it lives in the store's manifest
    const QString xml = QString::fromUtf8(buffer.data());
    QVERIFY2(xml.contains("Origin") == false, qPrintable(xml));

    // a fresh Doc bound to the same project reads it back
    Doc other(this);
    other.setWorkspacePath(workspace);
    other.assets()->setProjectFile(workspace + "/show.qxw");
    buffer.open(QIODevice::ReadOnly | QIODevice::Text);
    QXmlStreamReader xmlReader(&buffer);
    xmlReader.readNextStartElement();
    QVERIFY(other.loadXML(xmlReader));
    buffer.close();

    QCOMPARE(other.functionsByType(Function::AudioType).count(), 1);
    Function *loaded = other.functionsByType(Function::AudioType).first();
    QCOMPARE(other.assets()->originOf(loaded).path, source);
    QCOMPARE(other.assets()->originOf(loaded).sha1, m_doc->assets()->origin(stored).sha1);
    QCOMPARE(other.assets()->originChanged(loaded), false);

    // Save As: the copy and its provenance move together
    const QString elsewhere = QDir::cleanPath(m_tmp->path() + "/elsewhere");
    QDir().mkpath(elsewhere);
    QVERIFY(other.assets()->relocateTo(elsewhere + "/copy.qxw"));
    QString moved = static_cast<Audio *>(loaded)->getSourceFileName();
    QVERIFY(moved.startsWith(elsewhere + "/copy.qxw.assets/"));
    QCOMPARE(other.assets()->originOf(loaded).path, source);
    QVERIFY(QFile::exists(elsewhere + "/copy.qxw.assets/" + MediaAssets::manifestFileName()));
    QCOMPARE(MediaAssets::readManifest(elsewhere + "/copy.qxw.assets").count(), 1);
}

void MediaAssets_Test::originChangedAfterRewrite()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString source = writeFile("src/lyrics.mp4", "take one");
    QString stored = m_doc->assets()->importFile(source);
    Video *video = new Video(m_doc);
    video->setSourceUrl(stored);
    video->setName("Lyrics wall");   // a custom name must survive the reload
    QVERIFY(m_doc->addFunction(video));
    QCOMPARE(m_doc->assets()->originChanged(video), false);

    QSignalSpy reloadedSpy(m_doc->assets(), SIGNAL(originReloaded(quint32,QString,QString,quint32)));

    // re-rendered: different bytes, same length, later mtime -> confirmed by hash
    rewriteFile(source, "take two");
    QCOMPARE(m_doc->assets()->originChanged(video), true);
    QCOMPARE(m_doc->assets()->changedOrigins().count(), 1);
    QVERIFY(m_doc->assets()->changedOrigins().first() == video);

    MediaAssets::ReloadStatus status;
    QString error;
    QString fresh = m_doc->assets()->importOrigin(video, &status, &error);
    QCOMPARE(status, MediaAssets::Reloaded);
    QCOMPARE(fresh, m_doc->assets()->assetsDir() + "/" + hashDirFor("take two") + "/lyrics.mp4");
    QVERIFY(fresh != stored);
    QCOMPARE(video->sourceUrl(), stored);   // importOrigin() does not touch the function

    m_doc->assets()->applyReload(video, fresh);
    QCOMPARE(video->sourceUrl(), fresh);
    QCOMPARE(video->name(), QString("Lyrics wall"));
    QCOMPARE(reloadedSpy.count(), 1);
    QCOMPARE(reloadedSpy.at(0).at(0).toUInt(), video->id());
    QCOMPARE(reloadedSpy.at(0).at(1).toString(), stored);
    QCOMPARE(reloadedSpy.at(0).at(2).toString(), fresh);
    QCOMPARE(m_doc->assets()->originChanged(video), false);
    QCOMPARE(m_doc->assets()->originOf(video).path, source);
    QCOMPARE(m_doc->assets()->originOf(video).sha1,
             QString::fromLatin1(QCryptographicHash::hash("take two", QCryptographicHash::Sha1).toHex()));

    // never delete files: the previous copy is still there for an undo
    QVERIFY(QFile::exists(stored));
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList() << stored);

    // undo (the function goes back to the old copy): that copy's own
    // provenance is what the check uses, so it reports the origin as changed
    video->setSourceUrl(stored);
    QCOMPARE(m_doc->assets()->originChanged(video), true);

    // a different length is detected without hashing
    rewriteFile(source, "take three, longer");
    video->setSourceUrl(fresh);
    QCOMPARE(m_doc->assets()->originChanged(video), true);

    // the bulk path applies it in one go
    MediaAssets::ReloadResult result = m_doc->assets()->reloadChanged();
    QCOMPARE(result.reloaded, 1);
    QCOMPARE(result.unchanged, 0);
    QCOMPARE(result.missing, 0);
    QCOMPARE(result.queued, 0);
    QCOMPARE(video->sourceUrl(), m_doc->assets()->assetsDir() + "/" + hashDirFor("take three, longer") + "/lyrics.mp4");
    QCOMPARE(m_doc->assets()->originChanged(video), false);

    result = m_doc->assets()->reloadChanged();
    QCOMPARE(result.reloaded, 0);
    QCOMPARE(result.unchanged, 1);
}

void MediaAssets_Test::originTouchedButIdenticalIsUnchanged()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString source = writeFile("src/song.wav", "same bytes");
    QString stored = m_doc->assets()->importFile(source);
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(stored);
    QVERIFY(m_doc->addFunction(audio));

    // re-saved without a change: the hash settles it and the recorded
    // mtime is refreshed so the next check is cheap again
    rewriteFile(source, "same bytes");
    QCOMPARE(m_doc->assets()->originChanged(audio), false);
    QCOMPARE(m_doc->assets()->origin(stored).mtime.toMSecsSinceEpoch(),
             QFileInfo(source).lastModified().toMSecsSinceEpoch());

    MediaAssets::ReloadStatus status;
    QCOMPARE(m_doc->assets()->importOrigin(audio, &status), stored);
    QCOMPARE(status, MediaAssets::Unchanged);
}

void MediaAssets_Test::reloadDedupesIdenticalContent()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString source = writeFile("src/clip.mp4", "version A");
    QString storedA = m_doc->assets()->importFile(source);
    Video *video = new Video(m_doc);
    video->setSourceUrl(storedA);
    QVERIFY(m_doc->addFunction(video));

    rewriteFile(source, "version B");
    MediaAssets::ReloadStatus status;
    QString storedB = m_doc->assets()->importOrigin(video, &status);
    QCOMPARE(status, MediaAssets::Reloaded);
    m_doc->assets()->applyReload(video, storedB);

    // back to the first content: the existing <sha12>/ copy is reused, no
    // third directory appears
    rewriteFile(source, "version A");
    QCOMPARE(m_doc->assets()->originChanged(video), true);
    QString again = m_doc->assets()->importOrigin(video, &status);
    QCOMPARE(status, MediaAssets::Reloaded);
    QCOMPARE(again, storedA);
    m_doc->assets()->applyReload(video, again);
    QCOMPARE(video->sourceUrl(), storedA);
    QCOMPARE(m_doc->assets()->originChanged(video), false);

    QDir store(m_doc->assets()->assetsDir());
    QCOMPARE(store.entryList(QDir::Dirs | QDir::NoDotAndDotDot).count(), 2);
}

void MediaAssets_Test::missingOriginIsUnavailable()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString source = writeFile("src/gone.wav", "bytes");
    QString stored = m_doc->assets()->importFile(source);
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(stored);
    QVERIFY(m_doc->addFunction(audio));

    QVERIFY(QFile::remove(source));
    QVERIFY(m_doc->assets()->originOf(audio).isValid());   // still recorded
    QCOMPARE(m_doc->assets()->originAvailable(audio), false);
    QCOMPARE(m_doc->assets()->originChanged(audio), false);
    QVERIFY(m_doc->assets()->changedOrigins().isEmpty());

    MediaAssets::ReloadStatus status;
    QString error;
    QCOMPARE(m_doc->assets()->importOrigin(audio, &status, &error), stored);
    QCOMPARE(status, MediaAssets::Missing);
    QVERIFY(error.contains("gone.wav"));

    MediaAssets::ReloadResult result = m_doc->assets()->reloadChanged();
    QCOMPARE(result.missing, 1);
    QCOMPARE(result.reloaded, 0);
    QCOMPARE(audio->getSourceFileName(), stored);
}

void MediaAssets_Test::reloadChangedQueuesLargeFiles()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");
    m_doc->assets()->setBackgroundThreshold(2 * 1024 * 1024);

    // imported while small, re-rendered to a size above the threshold
    QString source = writeFile("src/big.mp4", "small at first");
    QString stored = m_doc->assets()->importFile(source);
    Video *video = new Video(m_doc);
    video->setSourceUrl(stored);
    QVERIFY(m_doc->addFunction(video));

    QSignalSpy finishedSpy(m_doc->assets(), SIGNAL(importFinished(QString,QString,QString)));
    QSignalSpy reloadedSpy(m_doc->assets(), SIGNAL(originReloaded(quint32,QString,QString,quint32)));

    QVERIFY(QFile::remove(source));
    QCOMPARE(writeLargeFile("src/big.mp4", 3), source);
    QCOMPARE(m_doc->assets()->originChanged(video), true);

    MediaAssets::ReloadResult result = m_doc->assets()->reloadChanged();
    QCOMPARE(result.queued, 1);
    QCOMPARE(result.reloaded, 0);
    QVERIFY(m_doc->assets()->hasPendingImports());
    QCOMPARE(video->sourceUrl(), stored);   // untouched until the copy lands

    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 60000);
    QCOMPARE(reloadedSpy.count(), 1);
    QString fresh = m_doc->assets()->assetsDir() + "/" + hashDirForFile(source) + "/big.mp4";
    QCOMPARE(video->sourceUrl(), fresh);
    QVERIFY(fresh != stored);
    QCOMPARE(m_doc->assets()->originOf(video).path, source);
    QCOMPARE(m_doc->assets()->originChanged(video), false);
    QVERIFY(QFile::exists(stored));
}

void MediaAssets_Test::removeUnreferencedDropsOrigin()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString stored = m_doc->assets()->importFile(writeFile("src/old.wav", "old"));
    QVERIFY(m_doc->assets()->origin(stored).isValid());

    QVERIFY(m_doc->assets()->removeUnreferenced(QStringList() << stored));
    QVERIFY(m_doc->assets()->origin(stored).isValid() == false);
    QCOMPARE(MediaAssets::readManifest(m_doc->assets()->assetsDir()).count(), 0);
}

/*****************************************************************************
 * Error paths and remaining variants
 *****************************************************************************/

void MediaAssets_Test::storeDirectoryIsAFile()
{
    // the store directory cannot be created because a file is in its way:
    // every import route reports that and keeps the external reference
    m_doc->setWorkspacePath(m_tmp->path());
    QString blocker = writeFile("show.qxw.assets", "not a directory");
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");
    QCOMPARE(m_doc->assets()->assetsDir(), blocker);

    QString source = writeFile("src/a.wav", "hello");
    QString error;
    QCOMPARE(m_doc->assets()->importFile(source, &error), QString());
    QVERIFY2(error.contains("Cannot create the asset directory"), qPrintable(error));

    QCOMPARE(m_doc->assets()->importOrKeep(source), source);
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);

    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(source);
    QVERIFY(m_doc->addFunction(audio));
    MediaAssets::CollectResult result = m_doc->assets()->collectExternal();
    QCOMPARE(result.copied, 0);
    QCOMPARE(result.queued, 0);
    QCOMPARE(result.failed, 1);
    QVERIFY2(result.firstError.contains("Cannot create the asset directory"), qPrintable(result.firstError));
    QCOMPARE(audio->getSourceFileName(), source);

    // a background copy fails on its thread the same way
    m_doc->assets()->setBackgroundThreshold(1);
    QSignalSpy finishedSpy(m_doc->assets(), SIGNAL(importFinished(QString,QString,QString)));
    QCOMPARE(m_doc->assets()->importOrKeep(source), source);
    QVERIFY(m_doc->assets()->hasPendingImports());
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(finishedSpy.at(0).at(0).toString(), source);
    QCOMPARE(finishedSpy.at(0).at(1).toString(), QString());
    QVERIFY(finishedSpy.at(0).at(2).toString().contains("Cannot create the asset directory"));
    QCOMPARE(audio->getSourceFileName(), source);
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);
    QVERIFY(QFile::exists(blocker));
}

void MediaAssets_Test::hashDirectoryIsAFile()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    // the <sha12>/ directory of this content is blocked by a file
    QString source = writeFile("src/a.wav", "hello");
    QString blocker = writeFile("show.qxw.assets/" + hashDirFor("hello"), "in the way");
    QString error;
    QCOMPARE(m_doc->assets()->importFile(source, &error), QString());
    QVERIFY2(error.contains("Cannot create the asset directory"), qPrintable(error));
    // the partial copy is cleaned up
    QCOMPARE(QDir(m_doc->assets()->assetsDir()).entryList(QStringList() << "*.partial", QDir::Files), QStringList());

    QVERIFY(QFile::remove(blocker));
    QString stored = m_doc->assets()->importFile(source, &error);
    QVERIFY2(stored.isEmpty() == false, qPrintable(error));
    Video *video = new Video(m_doc);
    video->setSourceUrl(stored);
    QVERIFY(m_doc->addFunction(video));

    // the origin changes to content whose hash directory is blocked: the
    // reload fails and the function keeps its current copy
    rewriteFile(source, "world");
    writeFile("show.qxw.assets/" + hashDirFor("world"), "in the way");
    MediaAssets::ReloadStatus status;
    error.clear();
    QCOMPARE(m_doc->assets()->importOrigin(video, &status, &error), stored);
    QCOMPARE(status, MediaAssets::Failed);
    QVERIFY2(error.contains("Cannot create the asset directory"), qPrintable(error));
    QCOMPARE(video->sourceUrl(), stored);

    MediaAssets::ReloadResult result = m_doc->assets()->reloadChanged();
    QCOMPARE(result.failed, 1);
    QCOMPARE(result.reloaded, 0);
    QCOMPARE(result.unchanged, 0);
    QVERIFY(result.firstError.contains("Cannot create the asset directory"));
    QCOMPARE(video->sourceUrl(), stored);
}

/** Point the process' temporary directory somewhere else for a scope,
 *  restoring the previous value on the way out (also on an early return
 *  from a failed assertion, so the other cases keep a working temp dir) */
struct ScopedTempEnv
{
    QList<QByteArray> names;
    QList<QByteArray> previous;
    QList<bool> wasSet;

    explicit ScopedTempEnv(const QString &path)
    {
#if defined(WIN32) || defined(Q_OS_WIN)
        names << "TMP" << "TEMP";
#else
        names << "TMPDIR";
#endif
        for (const QByteArray &name : names)
        {
            wasSet << qEnvironmentVariableIsSet(name.constData());
            previous << qgetenv(name.constData());
            qputenv(name.constData(), QDir::toNativeSeparators(path).toLocal8Bit());
        }
    }

    ~ScopedTempEnv()
    {
        for (int i = 0; i < names.count(); i++)
        {
            if (wasSet.at(i))
                qputenv(names.at(i).constData(), previous.at(i));
            else
                qunsetenv(names.at(i).constData());
        }
    }
};

void MediaAssets_Test::stagingDirectoryUnavailable()
{
    // untitled project whose staging directory cannot be created: imports
    // fail with a clear reason and the external references are kept
    QVERIFY(m_doc->assets()->isStaging());
    QString source = writeFile("src/a.wav", "hello");
    const QString bogus = QDir::cleanPath(m_tmp->path() + "/does/not/exist");

    {
        ScopedTempEnv env(bogus);
        QVERIFY2(QDir::cleanPath(QDir::tempPath()).compare(bogus, Qt::CaseInsensitive) == 0,
                 qPrintable(QDir::tempPath()));

        QString error;
        QCOMPARE(m_doc->assets()->importFile(source, &error), QString());
        QVERIFY2(error.contains("No asset directory available"), qPrintable(error));
        QCOMPARE(m_doc->assets()->assetsDir(), QString());
        QCOMPARE(m_doc->assets()->stagingDir(), QString());

        QCOMPARE(m_doc->assets()->importOrKeep(source), source);
        m_doc->assets()->setBackgroundThreshold(1);
        QCOMPARE(m_doc->assets()->importOrKeep(source), source);
        QCOMPARE(m_doc->assets()->hasPendingImports(), false);
        m_doc->assets()->setBackgroundThreshold(MediaAssets::DefaultBackgroundThreshold);

        // nothing stored, nothing to list, nothing to record
        QCOMPARE(m_doc->assets()->unreferenced(), QStringList());
        m_doc->assets()->saveManifest();
        m_doc->assets()->recordOrigin(source, source, "abc");
        QVERIFY(m_doc->assets()->m_manifest.isEmpty());
        QVERIFY(m_doc->assets()->origin(source).isValid() == false);
    }

    // back to a usable temp directory: staging works again
    QString stored = m_doc->assets()->importFile(source);
    QVERIFY(stored.isEmpty() == false);
    QVERIFY(m_doc->assets()->isManaged(stored));
}

void MediaAssets_Test::copyAndManifestErrors()
{
    QCOMPARE(MediaAssets::assetsDirNameFor("/x/y/show"), QString("show.qxw.assets"));
    QCOMPARE(MediaAssets::assetsDirNameFor("show.QXW"), QString("show.QXW.assets"));

    QCOMPARE(m_doc->assets()->projectFile(), QString());
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/proj/../show.qxw");
    QCOMPARE(m_doc->assets()->projectFile(), QDir::cleanPath(m_tmp->path() + "/show.qxw"));
    const QString store = m_doc->assets()->assetsDir();

    // a source that cannot be read
    QString error;
    QCOMPARE(m_doc->assets()->copyIntoStore(m_tmp->path() + "/missing.wav", store, &error), QString());
    QVERIFY2(error.contains("Cannot read"), qPrintable(error));
    QVERIFY(QDir(store).exists());   // the store itself was created on the way

    QCOMPARE(MediaAssets::sourceOf(nullptr), QString());
    QCOMPARE(MediaAssets::hashFile(m_tmp->path() + "/missing.wav"), QString());
    QCOMPARE(MediaAssets::readManifest(QString()).count(), 0);
    QCOMPARE(MediaAssets::writeManifest(QString(), QHash<QString, MediaOrigin>()), false);

    // manifest.json cannot be written when a directory sits in its place
    QVERIFY(QDir().mkpath(store + "/" + MediaAssets::manifestFileName()));
    QCOMPARE(MediaAssets::writeManifest(store, QHash<QString, MediaOrigin>()), false);
    QVERIFY(QDir(store).rmdir(MediaAssets::manifestFileName()));

    // job slots invoked without a job as sender are ignored
    m_doc->assets()->slotJobProgress(1, 2);
    m_doc->assets()->slotJobFinished(store + "/x", QString(), QString());
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);

    MediaCopyJob job(m_tmp->path() + "/a.wav", store, 42);
    QCOMPARE(job.source(), m_tmp->path() + "/a.wav");
    QCOMPARE(job.storeDir(), store);
    QCOMPARE(job.size(), qint64(42));
    QVERIFY(job.reloadFunctions().isEmpty());
    job.addReloadFunction(7);
    job.addReloadFunction(7);
    QCOMPARE(job.reloadFunctions(), QList<quint32>() << 7);
}

void MediaAssets_Test::longStoredPathsWarn()
{
    const QString ws = QDir::cleanPath(m_tmp->path() + "/proj");
    QDir().mkpath(ws);
    m_doc->setWorkspacePath(ws);
    m_doc->assets()->setProjectFile(ws + "/show.qxw");
    const QString store = m_doc->assets()->assetsDir();

    // a stored path just past the warning threshold, but still below the
    // classic Windows MAX_PATH so the copy itself succeeds everywhere
    const int target = MediaAssets::PathLengthWarning + 6;
    const int pad = target - store.length() - 14;   // "/" + <sha12> + "/" + basename
    if (pad < 8 || pad > 200)
        QSKIP("temporary directory too long or too short for this layout");
    const QString name = QString(pad - 4, 'x') + ".wav";

    QString source = writeFile("long/" + name, "long");
    QVERIFY(source.isEmpty() == false);
    QString error;
    QString stored = m_doc->assets()->importFile(source, &error);
    QVERIFY2(stored.isEmpty() == false, qPrintable(error));
    QCOMPARE(stored.length(), target);
    QVERIFY(stored.length() > MediaAssets::PathLengthWarning);
    QVERIFY(QFile::exists(stored));

    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(stored);
    QVERIFY(m_doc->addFunction(audio));

    // relocating to a project with a longer name warns for the copy too
    const QString newWs = QDir::cleanPath(m_tmp->path() + "/next");
    QDir().mkpath(newWs);
    QVERIFY2(m_doc->assets()->relocateTo(newWs + "/show2.qxw", &error), qPrintable(error));
    QVERIFY(audio->getSourceFileName().startsWith(newWs + "/show2.qxw.assets/"));
    QVERIFY(audio->getSourceFileName().length() > MediaAssets::PathLengthWarning);
    QVERIFY(QFile::exists(audio->getSourceFileName()));
}

void MediaAssets_Test::relocateCopyFailure()
{
    const QString oldWs = QDir::cleanPath(m_tmp->path() + "/old");
    const QString newWs = QDir::cleanPath(m_tmp->path() + "/new");
    QDir().mkpath(oldWs);
    m_doc->setWorkspacePath(oldWs);
    m_doc->assets()->setProjectFile(oldWs + "/a.qxw");

    QString used = m_doc->assets()->importFile(writeFile("src/used.wav", "used"));
    QVERIFY(used.isEmpty() == false);
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(used);
    QVERIFY(m_doc->addFunction(audio));

    // the copy's hash directory in the new store is blocked by a file
    writeFile("new/b.qxw.assets/" + hashDirFor("used"), "blocker");

    QString error;
    QCOMPARE(m_doc->assets()->relocateTo(newWs + "/b.qxw", &error), false);
    QVERIFY2(error.contains("Cannot copy"), qPrintable(error));

    // the store moved, the function keeps pointing at the old copy
    QCOMPARE(m_doc->assets()->assetsDir(), newWs + "/b.qxw.assets");
    QCOMPARE(audio->getSourceFileName(), used);
    QVERIFY(QFile::exists(used));
    QVERIFY(m_doc->assets()->isManaged(used) == false);
}

void MediaAssets_Test::manifestReadWriteErrors()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");
    QString stored = m_doc->assets()->importFile(writeFile("src/a.wav", "hello"));
    QVERIFY(m_doc->assets()->origin(stored).isValid());
    const QString manifest = m_doc->assets()->assetsDir() + "/" + MediaAssets::manifestFileName();

    // a corrupt manifest is ignored rather than trusted
    QFile f(manifest);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("{ this is not json");
    f.close();
    QCOMPARE(MediaAssets::readManifest(m_doc->assets()->assetsDir()).count(), 0);
    m_doc->assets()->m_manifestDir.clear();   // force a re-read
    QVERIFY(m_doc->assets()->origin(stored).isValid() == false);

    // entries without an origin path are dropped on read
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("{ \"version\": 1, \"files\": { \"abc/x.wav\": { \"origin\": \"\", \"sha1\": \"00\" },"
            " \"abc/y.wav\": { \"origin\": \"/src/y.wav\", \"size\": 3, \"sha1\": \"11\" } } }");
    f.close();
    QHash<QString, MediaOrigin> entries = MediaAssets::readManifest(m_doc->assets()->assetsDir());
    QCOMPARE(entries.count(), 1);
    QCOMPARE(entries.value("abc/y.wav").path, QString("/src/y.wav"));
    QCOMPARE(entries.value("abc/y.wav").size, qint64(3));
    QVERIFY(entries.value("abc/y.wav").mtime.isValid() == false);

#if defined(WIN32) || defined(Q_OS_WIN)
    // the manifest cannot be replaced while another handle keeps it open
    QFile lock(manifest);
    QVERIFY(lock.open(QIODevice::ReadOnly));
    QCOMPARE(MediaAssets::writeManifest(m_doc->assets()->assetsDir(), entries), false);
    lock.close();
#endif
    QCOMPARE(MediaAssets::writeManifest(m_doc->assets()->assetsDir(), entries), true);
    QCOMPARE(MediaAssets::readManifest(m_doc->assets()->assetsDir()).count(), 1);
}

void MediaAssets_Test::reloadVariants()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString wav = writeFile("src/song.wav", "take one");
    QString stored = m_doc->assets()->importFile(wav);
    QVERIFY(stored.isEmpty() == false);
    Audio *audio = new Audio(m_doc);
    audio->setSourceFileName(stored);
    audio->setName("My song");
    QVERIFY(m_doc->addFunction(audio));

    Scene *scene = new Scene(m_doc);
    QVERIFY(m_doc->addFunction(scene));
    QString ext = writeFile("src/ext.wav", "external");
    Audio *external = new Audio(m_doc);
    external->setSourceFileName(ext);
    QVERIFY(m_doc->addFunction(external));
    Video *stream = new Video(m_doc);
    stream->setSourceUrl("http://example.org/live.m3u8");
    QVERIFY(m_doc->addFunction(stream));

    // not a stored copy: nothing to import from
    MediaAssets::ReloadStatus status;
    QString error;
    QCOMPARE(m_doc->assets()->importOrigin(external, &status, &error), ext);
    QCOMPARE(status, MediaAssets::NotManaged);
    QCOMPARE(m_doc->assets()->importOrigin(stream, &status, &error), QString());
    QCOMPARE(status, MediaAssets::NotManaged);

    // applyReload() is a no-op for the current path, an empty path and a
    // function without a media source
    QSignalSpy reloadedSpy(m_doc->assets(), SIGNAL(originReloaded(quint32,QString,QString,quint32)));
    m_doc->assets()->applyReload(audio, stored);
    m_doc->assets()->applyReload(audio, QString());
    m_doc->assets()->applyReload(scene, stored);
    QCOMPARE(reloadedSpy.count(), 0);

    // a stored file that never went through the store has no provenance
    QString handMade = writeFile("show.qxw.assets/0123456789ab/hand.wav", "hand");
    Audio *hand = new Audio(m_doc);
    hand->setSourceFileName(handMade);
    QVERIFY(m_doc->addFunction(hand));
    QVERIFY(m_doc->assets()->isManaged(handMade));
    QVERIFY(m_doc->assets()->originOf(hand).isValid() == false);
    QCOMPARE(m_doc->assets()->originChanged(hand), false);

    // the bulk reload skips everything without a usable origin
    MediaAssets::ReloadResult result = m_doc->assets()->reloadChanged();
    QCOMPARE(result.unchanged, 1);
    QCOMPARE(result.reloaded + result.queued + result.missing + result.busy + result.failed, 0);

    // a running function is not reloaded under its playback
    rewriteFile(wav, "take two");
    QCOMPARE(m_doc->assets()->changedOrigins(), QList<Function *>() << audio);
    audio->m_running = true;
    result = m_doc->assets()->reloadChanged();
    QCOMPARE(result.busy, 1);
    QCOMPARE(result.reloaded, 0);
    audio->m_running = false;
    QCOMPARE(audio->getSourceFileName(), stored);

    // an Audio reload keeps the user's name and reports the old duration
    QString fresh = m_doc->assets()->importOrigin(audio, &status, &error);
    QCOMPARE(status, MediaAssets::Reloaded);
    QVERIFY(fresh != stored);
    m_doc->assets()->applyReload(audio, fresh);
    QCOMPARE(audio->getSourceFileName(), fresh);
    QCOMPARE(audio->name(), QString("My song"));
    QCOMPARE(reloadedSpy.count(), 1);
    QCOMPARE(reloadedSpy.at(0).at(0).toUInt(), audio->id());
    QCOMPARE(reloadedSpy.at(0).at(1).toString(), stored);
    QCOMPARE(reloadedSpy.at(0).at(2).toString(), fresh);
    QCOMPARE(m_doc->assets()->originChanged(audio), false);

    // stale provenance for a copy whose content already matches the origin:
    // the re-import lands on the same copy, which counts as unchanged and
    // repairs the recorded origin on the way
    const QString key = MediaAssets::manifestKey(fresh, m_doc->assets()->assetsDir());
    QVERIFY(m_doc->assets()->m_manifest.contains(key));
    MediaOrigin stale = m_doc->assets()->m_manifest.value(key);
    stale.sha1 = "0000";
    stale.mtime = stale.mtime.addSecs(-100);
    m_doc->assets()->m_manifest.insert(key, stale);
    m_doc->assets()->m_changeCache.clear();
    QCOMPARE(m_doc->assets()->originChanged(audio), true);
    result = m_doc->assets()->reloadChanged();
    QCOMPARE(result.unchanged, 1);
    QCOMPARE(result.reloaded, 0);
    QCOMPARE(audio->getSourceFileName(), fresh);
    QCOMPARE(reloadedSpy.count(), 1);
    QCOMPARE(m_doc->assets()->originChanged(audio), false);
    QCOMPARE(m_doc->assets()->origin(fresh).sha1,
             QString::fromLatin1(QCryptographicHash::hash("take two", QCryptographicHash::Sha1).toHex()));
}

void MediaAssets_Test::relocateReimportsFinishedCopySynchronously()
{
    // like backgroundImportFollowsRelocate(), but the threshold goes back
    // up before the staging copy lands: the second hop into the real store
    // is then a plain synchronous copy
    m_doc->assets()->setBackgroundThreshold(16);
    QVERIFY(m_doc->assets()->isStaging());

    QString clip = writeFile("src/clip.mp4", "a video, not really but long enough");
    Video *video = new Video(m_doc);
    video->setSourceUrl(clip);
    QVERIFY(m_doc->addFunction(video));

    QSignalSpy finishedSpy(m_doc->assets(), SIGNAL(importFinished(QString,QString,QString)));
    QCOMPARE(m_doc->assets()->importOrKeep(clip), clip);
    QVERIFY(m_doc->assets()->hasPendingImports());
    const QString staging = m_doc->assets()->assetsDir();

    const QString project = m_tmp->path() + "/saved/show.qxw";
    QDir().mkpath(m_tmp->path() + "/saved");
    m_doc->setWorkspacePath(m_tmp->path() + "/saved");
    QVERIFY(m_doc->assets()->relocateTo(project));
    m_doc->assets()->setBackgroundThreshold(MediaAssets::DefaultBackgroundThreshold);

    const QString hashDir = hashDirFor("a video, not really but long enough");
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 10000);
    QCOMPARE(finishedSpy.at(0).at(1).toString(), staging + "/" + hashDir + "/clip.mp4");
    QCOMPARE(video->sourceUrl(), m_doc->assets()->assetsDir() + "/" + hashDir + "/clip.mp4");
    QVERIFY(m_doc->assets()->isManaged(video->sourceUrl()));
    QCOMPARE(m_doc->assets()->hasPendingImports(), false);
    QCOMPARE(m_doc->assets()->externalSources(), QStringList());
    // provenance points at the original pick, not at the staging copy
    QCOMPARE(m_doc->assets()->originOf(video).path, clip);
    QTest::qWait(50);
    QCOMPARE(finishedSpy.count(), 1);
}

void MediaAssets_Test::removeUnreferencedErrors()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString external = writeFile("src/external.wav", "external");
    QString unused = m_doc->assets()->importFile(writeFile("src/unused.wav", "unused"));
    QVERIFY(unused.isEmpty() == false);

    // the first refusal is what the error reports; valid entries are
    // still deleted
    QString error;
    QCOMPARE(m_doc->assets()->removeUnreferenced(QStringList() << external << unused, &error), false);
    QVERIFY2(error.contains("not inside the project's media store"), qPrintable(error));
    QVERIFY(QFile::exists(external));
    QVERIFY(QFile::exists(unused) == false);

#if defined(WIN32) || defined(Q_OS_WIN)
    // a stored file that is open elsewhere cannot be deleted
    QString locked = m_doc->assets()->importFile(writeFile("src/locked.wav", "locked"));
    QVERIFY(locked.isEmpty() == false);
    QFile lock(locked);
    QVERIFY(lock.open(QIODevice::ReadOnly));
    error.clear();
    QCOMPARE(m_doc->assets()->removeUnreferenced(QStringList() << locked, &error), false);
    QVERIFY2(error.contains("Cannot delete"), qPrintable(error));
    QVERIFY(QFile::exists(locked));
    QVERIFY(m_doc->assets()->origin(locked).isValid());
    lock.close();
    QCOMPARE(m_doc->assets()->removeUnreferenced(QStringList() << locked), true);
    QVERIFY(QFile::exists(locked) == false);
#else
    QSKIP("Open files can only block deletion on Windows");
#endif
}

void MediaAssets_Test::unreferencedIgnoresForeignDirectories()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    QString stored = m_doc->assets()->importFile(writeFile("src/a.wav", "hello"));
    QVERIFY(stored.isEmpty() == false);
    // anything outside the <sha12>/ layout is not the store's business
    writeFile("show.qxw.assets/notahash/x.wav", "x");
    writeFile("show.qxw.assets/0123456789AB/upper.wav", "upper");   // hex, but not lowercase
    QCOMPARE(m_doc->assets()->unreferenced(), QStringList() << stored);
}

void MediaAssets_Test::partialFileCannotBeCreated()
{
    m_doc->setWorkspacePath(m_tmp->path());
    m_doc->assets()->setProjectFile(m_tmp->path() + "/show.qxw");

    // a 250 character file name is legal on every file system in use, but
    // the ".XXXXXX.partial" suffix of the copy in progress pushes the
    // working name past the 255 character component limit
    const QString name = QString(246, 'y') + ".wav";
    QString source = writeFile("src/" + name, "long name");
    if (source.isEmpty() || QFileInfo(source).isFile() == false)
        QSKIP("the file system refuses a 250 character file name");

    QString error;
    QCOMPARE(m_doc->assets()->importFile(source, &error), QString());
    QVERIFY2(error.contains("Cannot write into"), qPrintable(error));
    QVERIFY(QDir(m_doc->assets()->assetsDir()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).isEmpty());
    QCOMPARE(m_doc->assets()->importOrKeep(source), source);
}

QTEST_GUILESS_MAIN(MediaAssets_Test)
