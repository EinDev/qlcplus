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
#include "mediaassets.h"
#include "audio.h"
#include "video.h"
#include "doc.h"

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

    // no .partial leftovers
    QDir store(m_doc->assets()->assetsDir());
    QCOMPARE(store.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot), QStringList() << hashDirFor("hello"));

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
    QCOMPARE(store.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot).count(), 1);
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

QTEST_GUILESS_MAIN(MediaAssets_Test)
