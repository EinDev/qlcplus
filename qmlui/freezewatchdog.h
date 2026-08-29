/*
  Q Light Controller Plus
  freezewatchdog.h

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

#ifndef FREEZEWATCHDOG_H
#define FREEZEWATCHDOG_H

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <thread>

/**
 * FreezeWatchdog implements a self-contained main-thread hang detector
 * (see docs/agent-reports/2026-08-29-crash-freeze-diagnostics-options.md,
 * option F3).
 *
 * How it works:
 *  - A QTimer, ticking every ~1s on the GUI/main thread, stamps a
 *    std::atomic<qint64> with the current time ("heartbeat").
 *  - A plain worker thread (no Qt event loop needed) wakes up every ~1.5s
 *    and checks how stale that stamp is. If it is older than a generous
 *    threshold *and* no debugger is attached (a breakpoint-suspended app
 *    looks identical to a hang and would otherwise false-positive during
 *    development), the watchdog thread assumes the main thread is
 *    deadlocked/stuck and:
 *      1. writes a small text header (uptime, open project, heartbeat gap)
 *         to a new file under %LOCALAPPDATA%\qlcplus\,
 *      2. spawns `gdb -p <own pid> -batch -ex "thread apply all bt"` via
 *         the raw Win32 API (CreateProcessW - this runs from a thread with
 *         no event loop, so QProcess is not an option) with output
 *         redirected into that same file,
 *      3. shows the blocked (main/GUI) thread's own backtrace text directly
 *         in a custom dialog (IDD_FREEZE_DIALOG, qmlui.rc) built around a
 *         read-only multiline EDIT control - which, unlike a MessageBoxW's
 *         static text (or, as tried and dropped during development,
 *         TaskDialogIndirect's content areas - looked promising but its text
 *         turned out not to be genuinely selectable), natively supports
 *         drag-select and Ctrl+A/Ctrl+C, plus a Copy-to-clipboard button as a
 *         no-selection alternative - so the backtrace can be pasted straight
 *         into a bug report rather than having to go open the file. Falls
 *         back to a plain MessageBoxW if the dialog resource somehow fails
 *         to create. Like MessageBoxW, DialogBoxParamW pumps its own message
 *         loop on the calling thread, so it still works even though the Qt
 *         main thread is the one that's stuck - anything routed through
 *         Qt's own event loop would not.
 *
 * This class deliberately shares nothing with the rest of the app besides
 * the heartbeat atomic and (optionally) the current project path - it must
 * not itself become a source of instability for the very problem it exists
 * to detect.
 *
 * Only fires once per process run; if the heartbeat resumes afterwards, a
 * "recovered after ~Ns" line is appended to the same diagnostic file.
 *
 * Windows-only (the motivating incident and all of CLAUDE.md's toolchain is
 * Windows/MSYS2-MinGW); compiles to a no-op shell on other platforms.
 */
class FreezeWatchdog : public QObject
{
    Q_OBJECT

public:
    explicit FreezeWatchdog(QObject *parent = nullptr);
    ~FreezeWatchdog() override;

    /** Arms the heartbeat timer and starts the watchdog thread. Call once,
     *  from the GUI thread, right before the main event loop starts
     *  running (i.e. right before QApplication::exec()) - calling it any
     *  earlier would count whatever synchronous startup/project-loading
     *  work happens before exec() against the freeze threshold, which is
     *  exactly the kind of legitimate long blocking work this mechanism
     *  must tolerate. */
    void start();

    /** Stops the timer and joins the watchdog thread. Safe to call from
     *  the GUI thread during shutdown; also called from the destructor. */
    void stop();

    /** Thread-safe. Records the currently open project path so a freeze
     *  report can mention what was open when it happened. Pass an empty
     *  string when nothing is open. */
    static void setCurrentProjectPath(const QString &path);

    /**
     * Dev-only deliberate hang trigger, used to verify the watchdog fires
     * end-to-end. Blocks the CALLING thread (intended to be invoked on the
     * GUI thread) for the given number of seconds via QThread::sleep().
     *
     * Gated behind the QLCPLUS_DEBUG_FREEZE environment variable in
     * main.cpp - never wired to any normal user-facing action/menu/flag,
     * so it cannot fire outside a deliberate manual test.
     */
    static void debugBlockMainThread(int seconds);

private slots:
    void onHeartbeatTimer();

private:
    void watchdogLoop();
    void onFreezeDetected(qint64 heartbeatAgeMs);
    void appendRecoveryNote();

    QTimer *m_heartbeatTimer;
    std::thread m_thread;

    std::atomic<bool> m_stopRequested { false };
    std::atomic<qint64> m_lastHeartbeatMs { 0 };
    std::atomic<bool> m_fired { false };
    std::atomic<bool> m_recoveryLogged { false };

    qint64 m_startMs { 0 };
    qint64 m_freezeStartMs { 0 };   // watchdog-thread-only after firing
    QString m_diagnosticFilePath;   // watchdog-thread-only after firing

    // Windows thread ID of the GUI thread, captured via GetCurrentThreadId()
    // in start() (called on that thread). Used to pick the blocked thread's
    // own section out of gdb's "thread apply all bt" output - see
    // extractMainThreadSection() in freezewatchdog.cpp. Plain `unsigned long`
    // (matches DWORD) so this header doesn't need <windows.h>.
    unsigned long m_mainThreadId { 0 };
};

#endif // FREEZEWATCHDOG_H
