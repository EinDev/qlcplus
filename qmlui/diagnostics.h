/*
  Q Light Controller Plus
  diagnostics.h

  Copyright (c) Massimo Callegari

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

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <QString>
#include <QtGlobal>

/**
 * Helpers shared by the in-process diagnostic reporters (FreezeWatchdog,
 * CrashHandler): where reports go, how a gdb backtrace of this very process
 * is captured, and the copyable Win32 report dialog. Everything Win32-
 * specific is only declared on Windows; the project-path bookkeeping is
 * portable so callers don't need #ifdefs.
 *
 * Design constraint inherited from FreezeWatchdog: all of this must work
 * without Qt's event loop (the GUI thread may be stuck or mid-crash), so it
 * uses raw Win32 APIs (CreateProcessW, DialogBoxParamW) rather than QProcess
 * or QML/QtWidgets dialogs.
 */
namespace Diagnostics
{
    /** Thread-safe. Records the currently open project path so a report
     *  can mention what was open when it happened. Empty = nothing open. */
    void setCurrentProjectPath(const QString &path);

    /** Thread-safe. The last path passed to setCurrentProjectPath(), or
     *  "(none)" if empty. */
    QString currentProjectPath();

    /** Set by CrashHandler the moment a crash report starts. The crashing
     *  (usually GUI) thread then blocks until the dialog is dismissed, so
     *  its heartbeat stops - FreezeWatchdog checks this to avoid stacking a
     *  "Freeze detected" dialog on top of the crash dialog. */
    void setCrashReportInProgress();
    bool isCrashReportInProgress();

#ifdef Q_OS_WIN
    /** %LOCALAPPDATA%\qlcplus (created on demand), where reports are written. */
    QString reportsDir();

    /** Creates $filePath for writing with an *inheritable* handle (so a
     *  child gdb can write into it). Opaque Win32 HANDLE as void*; compare
     *  against reportFileInvalid() for failure. */
    void *createReportFile(const QString &filePath);

    /** The value createReportFile() returns on failure (INVALID_HANDLE_VALUE). */
    void *reportFileInvalid();

    /** Appends one line (+ CRLF, UTF-8) to a handle from createReportFile(). */
    void writeReportLine(void *hFile, const QString &line);

    /** Closes a handle from createReportFile(). */
    void closeReportFile(void *hFile);

    /** Runs `gdb -p <own pid> -batch -ex "thread apply all bt"` with its
     *  stdout/stderr redirected into $hFile, waiting up to ~30s for it. On
     *  launch failure a note is written into the file instead. */
    void appendGdbAllThreadsBacktrace(void *hFile);

    /** Pulls just one thread's section out of gdb's "thread apply all bt"
     *  output, matching gdb's own "(Thread <pid>.0x<tid>)" header text where
     *  <tid> is the Windows thread ID. Empty if not found. */
    QString extractThreadSection(const QString &fullText, qint64 pid, unsigned long tid);

    /** Reads a whole file back as UTF-8 text ("" on failure). */
    QString readReportFile(const QString &filePath);

    /** Shows one of the modal report dialogs from qmlui.rc (IDD_FREEZE_DIALOG /
     *  IDD_CRASH_DIALOG) with $text in its read-only, copyable edit control.
     *  Pumps its own message loop on the calling thread, so it works even
     *  when Qt's own loop is stuck. Returns false if the dialog could not
     *  be created (caller should fall back to a plain MessageBoxW). */
    bool showReportDialog(int dialogResourceId, const QString &text);
#endif
}

#endif // DIAGNOSTICS_H
