/*
  Q Light Controller Plus
  mediaassets.h

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

#ifndef MEDIAASSETS_H
#define MEDIAASSETS_H

#include <QObject>
#include <QScopedPointer>
#include <QStringList>

class QTemporaryDir;
class Doc;

/** @addtogroup engine Engine
 * @{
 */

/**
 * Project-local store for the media files (Audio/Video sources) a workspace
 * references. Files imported through importFile() are copied into
 * <workspace dir>/<project>.qxw.assets/<sha1 first 12 hex>/<original basename>
 * so the .qxw and its media can be moved together and the originals deleted.
 *
 * While the project has no file name yet (untitled), imports are staged in a
 * QTemporaryDir; relocateTo() moves the referenced ones into the real store
 * on the first save and repoints the functions at the copies.
 */
class MediaAssets : public QObject
{
    Q_OBJECT
    Q_DISABLE_COPY(MediaAssets)

public:
    explicit MediaAssets(Doc *doc);
    ~MediaAssets();

    /** Absolute path of the .qxw this store belongs to, empty while untitled */
    QString projectFile() const;

    /** Bind the store to a .qxw file WITHOUT moving anything (used on load,
     *  and with an empty path to go back to temporary staging) */
    void setProjectFile(const QString &qxwPath);

    /** True while imports are staged in a temporary directory */
    bool isStaging() const;

    /** Name of the store directory next to the .qxw: "<name>.qxw.assets" */
    QString assetsDirName() const;

    /** Absolute store directory; the staging QTemporaryDir while untitled */
    QString assetsDir() const;

    /** Directory name a .qxw path would use for its store */
    static QString assetsDirNameFor(const QString &qxwPath);

    /**
     * Copy a file into the store, hashing it on the way. Returns the ABSOLUTE
     * stored path, or an empty string on failure (with @error set).
     * A path that is already inside this store is returned unchanged.
     * Identical content already present is not copied again.
     */
    QString importFile(const QString &sourcePath, QString *error = nullptr);

    /** True if @path lies in this store's <sha12>/ layout */
    bool isManaged(const QString &path) const;

    /** Absolute Audio/Video sources referenced by the document (no URLs) */
    QStringList referenced() const;

    /** Files inside the store no function references anymore */
    QStringList unreferenced() const;

    /** Referenced sources that live outside the store */
    QStringList externalSources() const;

    /**
     * Move the store next to @newQxwPath: copies the referenced managed files
     * into the new store directory and relinks the functions to the copies.
     * Saving to the same location is a no-op. The old directory is left as is.
     */
    bool relocateTo(const QString &newQxwPath, QString *error = nullptr);

    /** Longest stored path before a warning is logged (Windows MAX_PATH margin) */
    static const int PathLengthWarning = 240;

signals:
    /** Emitted whenever the store directory changes (bind or relocate) */
    void assetsDirChanged();

private:
    /** Hash @sourcePath while copying it into the store; returns the stored
     *  absolute path. Synchronous for now - kept separate so a background
     *  copy can replace it without changing the public API. */
    QString copyIntoStore(const QString &sourcePath, const QString &storeDir, QString *error);

    /** Same-store check, case-insensitive on Windows */
    static bool samePath(const QString &a, const QString &b);

    /** True if @path is directly inside @storeDir/<sha12>/ */
    static bool isInStore(const QString &path, const QString &storeDir);

    /** Ensure the staging directory exists; returns its path or empty on failure */
    QString stagingDir();

    /** Repoint the function owning @source (Audio or Video) at @target */
    void relinkSource(const QString &source, const QString &target);

private:
    Doc *m_doc;
    QString m_projectFile;
    QScopedPointer<QTemporaryDir> m_stagingDir;
};

/** @} */

#endif
