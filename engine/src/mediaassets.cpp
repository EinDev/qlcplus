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

MediaAssets::MediaAssets(Doc *doc)
    : QObject(doc)
    , m_doc(doc)
{
}

MediaAssets::~MediaAssets()
{
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
        // staged files are no longer open and the directory can be dropped
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

QString MediaAssets::copyIntoStore(const QString &sourcePath, const QString &storeDir, QString *error)
{
    if (QDir().mkpath(storeDir) == false)
    {
        if (error)
            *error = tr("Cannot create the asset directory %1").arg(storeDir);
        return QString();
    }

    QFile in(sourcePath);
    if (in.open(QIODevice::ReadOnly) == false)
    {
        if (error)
            *error = tr("Cannot read %1: %2").arg(sourcePath, in.errorString());
        return QString();
    }

    const QString baseName = QFileInfo(sourcePath).fileName();

    // Hash while copying into a partial file on the destination volume, so the
    // final step is a plain rename once the hash (and thus the directory) is known
    QTemporaryFile partial(storeDir + "/" + baseName + ".XXXXXX.partial");
    if (partial.open() == false)
    {
        if (error)
            *error = tr("Cannot write into %1: %2").arg(storeDir, partial.errorString());
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (in.atEnd() == false)
    {
        QByteArray chunk = in.read(KCopyChunkSize);
        if (chunk.isEmpty() && in.error() != QFile::NoError)
        {
            if (error)
                *error = tr("Error reading %1: %2").arg(sourcePath, in.errorString());
            return QString();
        }
        hash.addData(chunk);
        if (partial.write(chunk) != chunk.size())
        {
            if (error)
                *error = tr("Error writing into %1: %2").arg(storeDir, partial.errorString());
            return QString();
        }
    }
    in.close();
    if (partial.flush() == false)
    {
        if (error)
            *error = tr("Error writing into %1: %2").arg(storeDir, partial.errorString());
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
            *error = tr("Cannot create the asset directory %1").arg(hashDir);
        return QString();
    }

    partial.setAutoRemove(false);
    if (partial.rename(target) == false)
    {
        if (error)
            *error = tr("Cannot move %1 into place: %2").arg(target, partial.errorString());
        partial.remove();
        return QString();
    }

    if (target.length() > PathLengthWarning)
        qWarning() << "MediaAssets: stored path is" << target.length() << "characters long:" << target;

    return target;
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
