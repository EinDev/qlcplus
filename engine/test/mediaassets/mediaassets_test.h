/*
  Q Light Controller Plus - Unit test
  mediaassets_test.h

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

#ifndef MEDIAASSETS_TEST_H
#define MEDIAASSETS_TEST_H

#include <QTemporaryDir>
#include <QObject>

class Doc;

class MediaAssets_Test final : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void importCreatesHashDir();
    void sameContentTwice();
    void differentContentSameBasename();
    void sameContentDifferentBasename();
    void importMissingFails();
    void importAlreadyManaged();
    void xmlRoundTrip();
    void relocateCopiesOnlyReferenced();
    void relocateSameLocationIsNoop();
    void stagingThenRelocate();
    void relinkKeepsBpmAndName();
    void unreferencedAfterDelete();
    void normalizeCaseInsensitive();

    // increment 2/3: background copies, collect, cleanup
    void smallFileStaysSynchronous();
    void backgroundImportRelinksLargeFile();
    void backgroundImportDedupesAndRelinksVideo();
    void backgroundImportFollowsRelocate();
    void closingProjectCancelsBackgroundImport();
    void collectExternalImportsAndRelinks();
    void collectExternalQueuesLargeFiles();
    void removeUnreferencedDeletesOnlyStoreFiles();

    // increment 4: provenance and reload from origin
    void originRecordedOnImport();
    void originSurvivesSaveLoadAndRelocate();
    void originChangedAfterRewrite();
    void originTouchedButIdenticalIsUnchanged();
    void reloadDedupesIdenticalContent();
    void missingOriginIsUnavailable();
    void reloadChangedQueuesLargeFiles();
    void removeUnreferencedDropsOrigin();

private:
    /** Overwrite @path with @content and push its mtime clearly past the
     *  old one (same-millisecond rewrites may keep it on some file systems) */
    static void rewriteFile(const QString &path, const QByteArray &content);

    /** Write @content into <m_tmp>/<relativePath>, returns its absolute path */
    QString writeFile(const QString &relativePath, const QByteArray &content);
    /** First 12 hex characters of the SHA1 of @content */
    static QString hashDirFor(const QByteArray &content);
    /** Same, for a file on disk */
    static QString hashDirForFile(const QString &path);
    /** Write @megabytes MB of non-trivial content into <m_tmp>/<relativePath> */
    QString writeLargeFile(const QString &relativePath, int megabytes);

private:
    Doc *m_doc;
    QTemporaryDir *m_tmp;
};

#endif
