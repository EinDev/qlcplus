/*
  Q Light Controller Plus
  mediaassets.cpp

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
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFileInfo>
#include <QDebug>
#include <QDir>

#include <functional>

#include "mediaassets.h"
#include "audio.h"
#include "video.h"
#include "doc.h"

#define KAssetsSuffix QStringLiteral(".assets")
#define KWorkspaceSuffix QStringLiteral(".qxw")
#define KHashDirLength 12
#define KCopyChunkSize (1024 * 1024)

#ifdef Q_OS_WIN
static const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
static const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif

static QString cleanAbsolute(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

static bool isHashDirName(const QString &name)
{
    static const QRegularExpression hex12(QStringLiteral("^[0-9a-f]{12}$"));
    return hex12.match(name).hasMatch();
}

/*****************************************************************************
 * Copy core, shared by the synchronous path and MediaCopyJob
 *****************************************************************************/

/**
 * Hash @sourcePath while copying it into @storeDir/<sha12>/<basename>.
 * Returns the stored absolute path, or empty with @error set. @progress is
 * called after every chunk, @cancelled is polled before every chunk; a
 * cancelled copy returns empty with @error set and leaves nothing behind.
 * Pure file work: safe to run on any thread.
 */
static QString copyFileIntoStore(const QString &sourcePath, const QString &storeDir, QString *error,
                                 const std::function<void(qint64, qint64)> &progress,
                                 const std::function<bool()> &cancelled)
{
    if (QDir().mkpath(storeDir) == false)
    {
        if (error)
            *error = MediaAssets::tr("Cannot create the asset directory %1").arg(storeDir);
        return QString();
    }

    QFile in(sourcePath);
    if (in.open(QIODevice::ReadOnly) == false)
    {
        if (error)
            *error = MediaAssets::tr("Cannot read %1: %2").arg(sourcePath, in.errorString());
        return QString();
    }

    const QString baseName = QFileInfo(sourcePath).fileName();
    const qint64 total = in.size();
    qint64 done = 0;

    // Hash while copying into a partial file on the destination volume, so the
    // final step is a plain rename once the hash (and thus the directory) is known
    QTemporaryFile partial(storeDir + "/" + baseName + ".XXXXXX.partial");
    if (partial.open() == false)
    {
        if (error)
            *error = MediaAssets::tr("Cannot write into %1: %2").arg(storeDir, partial.errorString());
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (in.atEnd() == false)
    {
        if (cancelled && cancelled())
        {
            if (error)
                *error = MediaAssets::tr("Copy of %1 cancelled").arg(sourcePath);
            return QString();
        }

        QByteArray chunk = in.read(KCopyChunkSize);
        if (chunk.isEmpty() && in.error() != QFile::NoError)
        {
            if (error)
                *error = MediaAssets::tr("Error reading %1: %2").arg(sourcePath, in.errorString());
            return QString();
        }
        hash.addData(chunk);
        if (partial.write(chunk) != chunk.size())
        {
            if (error)
                *error = MediaAssets::tr("Error writing into %1: %2").arg(storeDir, partial.errorString());
            return QString();
        }
        done += chunk.size();
        if (progress)
            progress(done, total);
    }
    in.close();
    if (partial.flush() == false)
    {
        if (error)
            *error = MediaAssets::tr("Error writing into %1: %2").arg(storeDir, partial.errorString());
        return QString();
    }
    partial.close();

    const QString hashDir = storeDir + "/" + QString::fromLatin1(hash.result().toHex().left(KHashDirLength));
    const QString target = hashDir + "/" + baseName;

    if (QFile::exists(target))
        return target;   // same content already stored, the partial copy is auto-removed

    if (QDir().mkpath(hashDir) == false)
    {
        if (error)
            *error = MediaAssets::tr("Cannot create the asset directory %1").arg(hashDir);
        return QString();
    }

    partial.setAutoRemove(false);
    if (partial.rename(target) == false)
    {
        if (error)
            *error = MediaAssets::tr("Cannot move %1 into place: %2").arg(target, partial.errorString());
        partial.remove();
        return QString();
    }

    if (target.length() > MediaAssets::PathLengthWarning)
        qWarning() << "MediaAssets: stored path is" << target.length() << "characters long:" << target;

    return target;
}

/*****************************************************************************
 * MediaCopyJob
 *****************************************************************************/

MediaCopyJob::MediaCopyJob(const QString &source, const QString &storeDir, qint64 size, QObject *parent)
    : QThread(parent)
    , m_source(source)
    , m_storeDir(storeDir)
    , m_size(size)
{
}

QString MediaCopyJob::source() const
{
    return m_source;
}

QString MediaCopyJob::storeDir() const
{
    return m_storeDir;
}

qint64 MediaCopyJob::size() const
{
    return m_size;
}

void MediaCopyJob::run()
{
    QString error;
    QString target = copyFileIntoStore(m_source, m_storeDir, &error,
        [this](qint64 done, qint64 total) { emit progress(done, total); },
        [this]() { return isInterruptionRequested(); });

    emit copyFinished(target, error);
}

/*****************************************************************************
 * MediaAssets
 *****************************************************************************/

MediaAssets::MediaAssets(Doc *doc)
    : QObject(doc)
    , m_doc(doc)
    , m_backgroundThreshold(DefaultBackgroundThreshold)
{
}

MediaAssets::~MediaAssets()
{
    cancelPendingImports();
}

QString MediaAssets::projectFile() const
{
    return m_projectFile;
}

void MediaAssets::setProjectFile(const QString &qxwPath)
{
    if (qxwPath.isEmpty())
    {
        m_projectFile.clear();
        // Back to an untitled project: every function is gone by now, so the
        // staged files are no longer open and the directory can be dropped.
        // A background copy still writing into it is stopped first.
        cancelPendingImports();
        m_stagingDir.reset();
    }
    else
    {
        m_projectFile = cleanAbsolute(qxwPath);
    }

    emit assetsDirChanged();
}

bool MediaAssets::isStaging() const
{
    return m_projectFile.isEmpty();
}

QString MediaAssets::assetsDirNameFor(const QString &qxwPath)
{
    QString name = QFileInfo(qxwPath).fileName();
    if (name.endsWith(KWorkspaceSuffix, Qt::CaseInsensitive) == false)
        name += KWorkspaceSuffix;
    return name + KAssetsSuffix;
}

QString MediaAssets::assetsDirName() const
{
    if (isStaging())
        return QString();

    return assetsDirNameFor(m_projectFile);
}

QString MediaAssets::assetsDir() const
{
    if (isStaging())
        return m_stagingDir.isNull() ? QString() : QDir::cleanPath(m_stagingDir->path());

    return QFileInfo(m_projectFile).absolutePath() + "/" + assetsDirName();
}

QString MediaAssets::stagingDir()
{
    if (m_stagingDir.isNull())
    {
        m_stagingDir.reset(new QTemporaryDir(QDir::tempPath() + "/qlcplus-assets-XXXXXX"));
        if (m_stagingDir->isValid() == false)
        {
            qWarning() << "MediaAssets: cannot create a staging directory:" << m_stagingDir->errorString();
            m_stagingDir.reset();
            return QString();
        }
    }

    return QDir::cleanPath(m_stagingDir->path());
}

QString MediaAssets::importFile(const QString &sourcePath, QString *error)
{
    QFileInfo source(sourcePath);
    if (source.exists() == false || source.isFile() == false)
    {
        if (error)
            *error = tr("Media file %1 not found").arg(sourcePath);
        return QString();
    }

    QString absSource = cleanAbsolute(sourcePath);
    QString storeDir = isStaging() ? stagingDir() : assetsDir();
    if (storeDir.isEmpty())
    {
        if (error)
            *error = tr("No asset directory available for %1").arg(sourcePath);
        return QString();
    }

    if (isInStore(absSource, storeDir))
        return absSource;

    return copyIntoStore(absSource, storeDir, error);
}

QString MediaAssets::importOrKeep(const QString &sourcePath)
{
    QFileInfo source(sourcePath);
    if (source.exists() && source.isFile() && source.size() >= m_backgroundThreshold)
    {
        const QString absSource = cleanAbsolute(sourcePath);
        const QString storeDir = isStaging() ? stagingDir() : assetsDir();
        if (storeDir.isEmpty() == false)
        {
            if (isInStore(absSource, storeDir) == false)
                enqueueJob(absSource, storeDir, source.size());
            // the caller points at the source for now; slotJobFinished()
            // relinks it to the copy
            return absSource;
        }
        // no store directory at all: fall through, importFile() reports why
    }

    QString error;
    QString stored = importFile(sourcePath, &error);
    if (stored.isEmpty())
    {
        qWarning() << "MediaAssets: keeping external reference to" << sourcePath << "-" << error;
        return sourcePath;
    }
    return stored;
}

qint64 MediaAssets::backgroundThreshold() const
{
    return m_backgroundThreshold;
}

void MediaAssets::setBackgroundThreshold(qint64 bytes)
{
    m_backgroundThreshold = bytes;
}

QString MediaAssets::copyIntoStore(const QString &sourcePath, const QString &storeDir, QString *error)
{
    return copyFileIntoStore(sourcePath, storeDir, error, nullptr, nullptr);
}

/*****************************************************************************
 * Background copies
 *****************************************************************************/

bool MediaAssets::hasPendingImports() const
{
    return m_jobs.isEmpty() == false;
}

QStringList MediaAssets::pendingImports() const
{
    QStringList list;
    for (MediaCopyJob *job : m_jobs)
        list << job->source();
    return list;
}

void MediaAssets::enqueueJob(const QString &absSource, const QString &storeDir, qint64 size)
{
    for (MediaCopyJob *job : m_jobs)
    {
        if (samePath(job->source(), absSource))
            return;   // already on its way
    }

    MediaCopyJob *job = new MediaCopyJob(absSource, storeDir, size, this);
    connect(job, SIGNAL(progress(qint64,qint64)), this, SLOT(slotJobProgress(qint64,qint64)));
    connect(job, SIGNAL(copyFinished(QString,QString)), this, SLOT(slotJobFinished(QString,QString)));
    m_jobs.append(job);
    emit pendingImportsChanged();

    startNextJob();
}

void MediaAssets::startNextJob()
{
    if (m_jobs.isEmpty())
        return;

    MediaCopyJob *job = m_jobs.first();
    if (job->isRunning() || job->isFinished())
        return;

    emit importStarted(job->source(), job->size());
    job->start();
}

void MediaAssets::slotJobProgress(qint64 done, qint64 total)
{
    MediaCopyJob *job = qobject_cast<MediaCopyJob *>(sender());
    if (job == nullptr)
        return;

    emit importProgress(job->source(), done, total);
}

void MediaAssets::slotJobFinished(const QString &target, const QString &error)
{
    MediaCopyJob *job = qobject_cast<MediaCopyJob *>(sender());
    if (job == nullptr)
        return;

    const QString source = job->source();
    m_jobs.removeOne(job);
    job->wait();
    job->deleteLater();

    if (target.isEmpty())
    {
        qWarning() << "MediaAssets: keeping external reference to" << source << "-" << error;
        emit importFinished(source, QString(), error);
    }
    else
    {
        relinkSource(source, target);
        emit importFinished(source, target, QString());

        // The store moved while the copy ran (first save / Save As of an
        // untitled project): the copy landed in the old directory, so bring
        // it into the current store the same way - this queues again if the
        // file is big, or copies right away if the threshold changed
        if (isManaged(target) == false)
        {
            QString again = importOrKeep(target);
            if (again.isEmpty() == false && samePath(again, target) == false)
                relinkSource(target, again);
        }
    }

    emit pendingImportsChanged();
    startNextJob();
}

void MediaAssets::cancelPendingImports()
{
    if (m_jobs.isEmpty())
        return;

    QList<MediaCopyJob *> jobs = m_jobs;
    m_jobs.clear();

    for (MediaCopyJob *job : jobs)
    {
        // no relink for a cancelled copy, whatever its late signal says
        disconnect(job, nullptr, this, nullptr);
        if (job->isRunning())
        {
            job->requestInterruption();
            job->wait();
        }
        delete job;
    }

    emit pendingImportsChanged();
}

bool MediaAssets::samePath(const QString &a, const QString &b)
{
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), pathCase) == 0;
}

bool MediaAssets::isInStore(const QString &path, const QString &storeDir)
{
    if (storeDir.isEmpty() || path.isEmpty())
        return false;

    const QString prefix = QDir::cleanPath(storeDir) + "/";
    const QString clean = QDir::cleanPath(path);
    if (clean.startsWith(prefix, pathCase) == false)
        return false;

    QStringList rest = clean.mid(prefix.length()).split('/', Qt::SkipEmptyParts);
    return rest.count() == 2 && isHashDirName(rest.at(0));
}

bool MediaAssets::isManaged(const QString &path) const
{
    return isInStore(path, assetsDir());
}

QStringList MediaAssets::referenced() const
{
    QStringList list;

    auto append = [&list](const QString &source)
    {
        if (source.isEmpty() || source.contains("://"))
            return;
        QString abs = cleanAbsolute(source);
        if (list.contains(abs, pathCase) == false)
            list << abs;
    };

    for (Function *f : m_doc->functionsByType(Function::AudioType))
        append(static_cast<Audio *>(f)->getSourceFileName());

    for (Function *f : m_doc->functionsByType(Function::VideoType))
        append(static_cast<Video *>(f)->sourceUrl());

    return list;
}

QStringList MediaAssets::unreferenced() const
{
    QStringList list;
    const QString storeDir = assetsDir();
    if (storeDir.isEmpty())
        return list;

    const QStringList used = referenced();

    QDir store(storeDir);
    for (const QString &dirName : store.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (isHashDirName(dirName) == false)
            continue;

        QDir hashDir(storeDir + "/" + dirName);
        for (const QString &fileName : hashDir.entryList(QDir::Files))
        {
            QString file = hashDir.absoluteFilePath(fileName);
            if (used.contains(file, pathCase) == false)
                list << file;
        }
    }

    return list;
}

QStringList MediaAssets::externalSources() const
{
    QStringList list;
    for (const QString &source : referenced())
    {
        if (isManaged(source) == false)
            list << source;
    }
    return list;
}

MediaAssets::CollectResult MediaAssets::collectExternal()
{
    CollectResult result;

    for (const QString &source : externalSources())
    {
        QFileInfo info(source);
        if (info.exists() == false || info.isFile() == false)
        {
            result.failed++;
            if (result.firstError.isEmpty())
                result.firstError = tr("Media file %1 not found").arg(source);
            continue;
        }

        if (info.size() >= m_backgroundThreshold)
        {
            // same path as importOrKeep(): relinked when the copy lands
            importOrKeep(source);
            result.queued++;
            continue;
        }

        QString error;
        QString stored = importFile(source, &error);
        if (stored.isEmpty())
        {
            qWarning() << "MediaAssets: cannot collect" << source << "-" << error;
            result.failed++;
            if (result.firstError.isEmpty())
                result.firstError = error;
            continue;
        }

        relinkSource(source, stored);
        result.copied++;
    }

    return result;
}

bool MediaAssets::removeUnreferenced(const QStringList &files, QString *error)
{
    const QString storeDir = assetsDir();
    const QStringList used = referenced();
    bool ok = true;

    for (const QString &file : files)
    {
        const QString abs = cleanAbsolute(file);

        // Re-checked here, not trusted from an earlier unreferenced() call:
        // the list may have been shown in a dialog for a while
        if (isInStore(abs, storeDir) == false)
        {
            qWarning() << "MediaAssets: refusing to delete" << abs << "- not a stored media file";
            if (error && error->isEmpty())
                *error = tr("%1 is not inside the project's media store").arg(abs);
            ok = false;
            continue;
        }
        if (used.contains(abs, pathCase))
        {
            qWarning() << "MediaAssets: refusing to delete" << abs << "- still referenced";
            if (error && error->isEmpty())
                *error = tr("%1 is still used by a function").arg(abs);
            ok = false;
            continue;
        }

        if (QFile::exists(abs) && QFile::remove(abs) == false)
        {
            qWarning() << "MediaAssets: cannot delete" << abs;
            if (error && error->isEmpty())
                *error = tr("Cannot delete %1").arg(abs);
            ok = false;
            continue;
        }

        QDir hashDir(QFileInfo(abs).absolutePath());
        if (hashDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty())
            hashDir.removeRecursively();
    }

    return ok;
}

bool MediaAssets::relocateTo(const QString &newQxwPath, QString *error)
{
    const QString newProject = cleanAbsolute(newQxwPath);
    const QString newStore = QFileInfo(newProject).absolutePath() + "/" + assetsDirNameFor(newProject);
    const QString oldStore = assetsDir();

    if (oldStore.isEmpty() || samePath(oldStore, newStore))
    {
        // Nothing staged yet, or saving where the store already lives
        m_projectFile = newProject;
        emit assetsDirChanged();
        return true;
    }

    bool ok = true;
    QString firstError;

    for (const QString &source : referenced())
    {
        if (isInStore(source, oldStore) == false)
            continue;

        const QString relative = QDir::cleanPath(source).mid(QDir::cleanPath(oldStore).length() + 1);
        const QString target = newStore + "/" + relative;

        if (QFile::exists(target) == false)
        {
            if (QDir().mkpath(QFileInfo(target).absolutePath()) == false ||
                QFile::copy(source, target) == false)
            {
                qWarning() << "MediaAssets: cannot copy" << source << "to" << target;
                if (firstError.isEmpty())
                    firstError = tr("Cannot copy %1 to %2").arg(source, target);
                ok = false;
                continue;   // the function keeps pointing at the old copy, which still exists
            }
        }

        if (target.length() > PathLengthWarning)
            qWarning() << "MediaAssets: stored path is" << target.length() << "characters long:" << target;

        relinkSource(source, target);
    }

    // The staging directory (if any) is intentionally kept until the project
    // is closed: a decoder may still hold the old file open on Windows
    m_projectFile = newProject;
    emit assetsDirChanged();

    if (ok == false && error)
        *error = firstError;

    return ok;
}

void MediaAssets::relinkSource(const QString &source, const QString &target)
{
    for (Function *f : m_doc->functionsByType(Function::AudioType))
    {
        Audio *audio = static_cast<Audio *>(f);
        if (samePath(cleanAbsolute(audio->getSourceFileName()), source))
            audio->relinkSource(target);
    }

    for (Function *f : m_doc->functionsByType(Function::VideoType))
    {
        Video *video = static_cast<Video *>(f);
        if (video->sourceUrl().contains("://"))
            continue;
        if (samePath(cleanAbsolute(video->sourceUrl()), source))
            video->relinkSource(target);
    }
}
