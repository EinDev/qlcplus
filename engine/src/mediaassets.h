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
#include <QDateTime>
#include <QThread>
#include <QHash>
#include <QList>

class QTemporaryDir;
class Function;
class Doc;

/** @addtogroup engine Engine
 * @{
 */

/**
 * Where a stored media copy came from: the absolute path it was imported
 * from plus that file's size/modification time at import time and the
 * content hash of the copy. Kept in the store's manifest.json, keyed by the
 * stored file, so a function that goes back to an older copy (undo of a
 * Replace/Reload) automatically gets that copy's provenance back.
 */
struct MediaOrigin
{
    QString path;       ///< absolute source path, empty when unknown
    qint64 size = -1;   ///< source size in bytes at import time
    QDateTime mtime;    ///< source modification time at import time
    QString sha1;       ///< full SHA1 hex of the stored content

    bool isValid() const { return path.isEmpty() == false; }
};

/** @} */

/** @addtogroup engine Engine
 * @{
 */

/**
 * Background copy of one file into a store directory, owned and serialized
 * by MediaAssets. It only touches files: the function relink happens in
 * MediaAssets on the main thread once copyFinished() arrives.
 */
class MediaCopyJob : public QThread
{
    Q_OBJECT
    Q_DISABLE_COPY(MediaCopyJob)

public:
    MediaCopyJob(const QString &source, const QString &storeDir, qint64 size, QObject *parent = nullptr);

    QString source() const;
    QString storeDir() const;
    qint64 size() const;

    /** Functions to re-point at the finished copy with the full setters
     *  (a reload of their origin), empty for a plain import */
    QList<quint32> reloadFunctions() const;
    void addReloadFunction(quint32 functionId);

signals:
    /** Bytes copied so far / total bytes */
    void progress(qint64 done, qint64 total);
    /** @target is the stored absolute path (with its full content hash in
     *  @sha1), or empty with @error set */
    void copyFinished(const QString &target, const QString &error, const QString &sha1);

protected:
    void run() override;

private:
    QString m_source;
    QString m_storeDir;
    qint64 m_size;
    QList<quint32> m_reloadFunctions;
};

/** @} */

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

    /**
     * importFile() for UI entry points: never fails the caller. When the
     * copy is impossible (unreadable source, full disk) the warning is
     * logged and @sourcePath itself is returned, so the user's pick is kept
     * as an external reference instead of being lost.
     *
     * Files of backgroundThreshold() bytes or more are not copied here:
     * the (absolute) @sourcePath is returned immediately, a MediaCopyJob
     * copies the file on a worker thread, and every Audio/Video pointing at
     * @sourcePath is relinked to the stored copy when importFinished() fires.
     * A save that happens in between simply writes the external path.
     */
    QString importOrKeep(const QString &sourcePath);

    /** Size from which importOrKeep() copies on a worker thread (50 MB) */
    static const qint64 DefaultBackgroundThreshold = qint64(50) * 1024 * 1024;

    qint64 backgroundThreshold() const;
    void setBackgroundThreshold(qint64 bytes);

    /** True while at least one background copy is queued or running */
    bool hasPendingImports() const;

    /** Sources currently queued or being copied, running one first */
    QStringList pendingImports() const;

    /** Outcome of collectExternal() */
    struct CollectResult
    {
        int copied = 0;     ///< imported and relinked synchronously
        int queued = 0;     ///< handed to a background copy (relinked later)
        int failed = 0;     ///< missing or unreadable sources, left as they were
        QString firstError;
    };

    /**
     * Import every externalSources() entry into the store and relink the
     * functions using it. Sources above backgroundThreshold() are queued
     * like importOrKeep() does; URLs are never touched.
     */
    CollectResult collectExternal();

    /**
     * Delete the given files from the store. Only files inside this store's
     * <sha12>/ layout that no function references are removed; anything
     * else in the list is refused (returns false with @error set) and left
     * alone. Emptied <sha12>/ directories are removed too.
     */
    bool removeUnreferenced(const QStringList &files, QString *error = nullptr);

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

    /*********************************************************************
     * Provenance (manifest.json) and reload from origin
     *********************************************************************/

    /** Name of the provenance file at the store root */
    static QString manifestFileName();

    /** Read a store's manifest.json (key: "<sha12>/<basename>"); empty when
     *  absent or unreadable */
    static QHash<QString, MediaOrigin> readManifest(const QString &storeDir);

    /** Provenance of a stored file (invalid when unknown or @storedPath is
     *  not inside this store) */
    MediaOrigin origin(const QString &storedPath) const;

    /** Absolute Audio/Video source of @function, empty for URLs/other types */
    static QString sourceOf(Function *function);

    /** Provenance of the managed copy an Audio/Video points at */
    MediaOrigin originOf(Function *function) const;

    /** True if @function's copy has a recorded origin that still exists on disk */
    bool originAvailable(Function *function) const;

    /**
     * True if @function's origin file differs from the copy that was taken
     * from it. Size and mtime equal to the recorded ones means unchanged
     * without reading the file; a different size means changed; only an
     * equal size with a different mtime is confirmed by hashing (and the
     * recorded mtime is refreshed when the content turns out identical).
     * A missing origin is "unavailable", not "changed": returns false.
     */
    bool originChanged(Function *function);

    /** Every Audio/Video whose originChanged() is true */
    QList<Function *> changedOrigins();

    enum ReloadStatus
    {
        Reloaded,   ///< a new copy was stored, apply it with applyReload()
        Unchanged,  ///< origin content identical to the current copy
        Queued,     ///< large origin: copied in the background, applied when it lands
        Missing,    ///< no recorded origin, or the origin file is gone
        NotManaged, ///< the function's source is not a stored copy
        Failed      ///< copy error, see the error string
    };

    /**
     * Import @function's origin again. Returns the stored path of the
     * origin's current content (a new <sha12>/ directory when it changed,
     * the function's current copy when not) without touching the function,
     * so a caller can record an undo step first and then applyReload().
     * Queued/Missing/Failed return the function's current source.
     */
    QString importOrigin(Function *function, ReloadStatus *status, QString *error = nullptr);

    /**
     * Point @function at @storedPath with the full setters (decoder/duration
     * rebuilt, BPM reset, name kept), logging the old/new duration. Emits
     * originReloaded(). Does nothing when the path is already current.
     */
    void applyReload(Function *function, const QString &storedPath);

    /** Outcome of reloadChanged() */
    struct ReloadResult
    {
        int reloaded = 0;   ///< re-imported and applied synchronously
        int queued = 0;     ///< large files handed to a background copy
        int unchanged = 0;  ///< origin available and identical
        int missing = 0;    ///< origin recorded but no longer on disk
        int busy = 0;       ///< skipped because the function is running
        int failed = 0;
        QString firstError;
    };

    /** importOrigin() + applyReload() for every changedOrigins() entry that
     *  is not currently running. Not undoable; the old copies stay on disk. */
    ReloadResult reloadChanged();

signals:
    /** @functionId was re-pointed from @oldPath to @newPath after a reload
     *  of its origin; @oldDuration is its total duration before that */
    void originReloaded(quint32 functionId, const QString &oldPath, const QString &newPath, quint32 oldDuration);

    /** Emitted whenever the store directory changes (bind or relocate) */
    void assetsDirChanged();

    /** A background copy of @source (@bytes long) started */
    void importStarted(const QString &source, qint64 bytes);

    /** Progress of the running background copy */
    void importProgress(const QString &source, qint64 done, qint64 total);

    /** A background copy ended: @target is the stored path the functions
     *  were relinked to, or empty with @error set (the external reference
     *  is kept in that case) */
    void importFinished(const QString &source, const QString &target, const QString &error);

    /** hasPendingImports()/pendingImports() changed */
    void pendingImportsChanged();

private slots:
    void slotJobProgress(qint64 done, qint64 total);
    void slotJobFinished(const QString &target, const QString &error, const QString &sha1);

private:
    /** Queue a background copy of @absSource into @storeDir; returns the
     *  (possibly already queued) job so a reload can register its function */
    MediaCopyJob *enqueueJob(const QString &absSource, const QString &storeDir, qint64 size);

    /** Start the first queued job if none is running */
    void startNextJob();

    /** Interrupt and discard every queued/running job (blocks until the
     *  running one has stopped) */
    void cancelPendingImports();

    /** Hash @sourcePath while copying it into the store; returns the stored
     *  absolute path and the full content hash in @sha1. Synchronous for
     *  now - kept separate so a background copy can replace it without
     *  changing the public API. */
    QString copyIntoStore(const QString &sourcePath, const QString &storeDir, QString *error, QString *sha1 = nullptr);

    /** Record that @storedPath (in the current store) was imported from
     *  @sourcePath. When @sourcePath is itself a stored copy with known
     *  provenance (the store moved during a background copy), that
     *  provenance is carried over instead of the intermediate copy's. */
    void recordOrigin(const QString &storedPath, const QString &sourcePath, const QString &sha1);

    /** Manifest key of @storedPath inside @storeDir: "<sha12>/<basename>" */
    static QString manifestKey(const QString &storedPath, const QString &storeDir);

    /** Write @entries as @storeDir's manifest.json (atomically) */
    static bool writeManifest(const QString &storeDir, const QHash<QString, MediaOrigin> &entries);

    /** (Re)load the manifest of the current store if it is not the one in memory */
    void ensureManifest() const;

    /** Write the in-memory manifest to the current store */
    void saveManifest();

    /** Full SHA1 hex of a file on disk, empty on error */
    static QString hashFile(const QString &path);

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
    QList<MediaCopyJob *> m_jobs;   ///< running job first, then the queue
    qint64 m_backgroundThreshold;

    mutable QString m_manifestDir;                  ///< store the in-memory manifest belongs to
    mutable QHash<QString, MediaOrigin> m_manifest; ///< key: manifestKey()

    /** originChanged() verdicts, keyed by manifest key, valid while the
     *  origin's size/mtime stay the ones they were computed for */
    struct ChangeVerdict
    {
        qint64 size = -1;
        QDateTime mtime;
        bool changed = false;
    };
    QHash<QString, ChangeVerdict> m_changeCache;
};

/** @} */

#endif
