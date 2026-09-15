/*
  Q Light Controller Plus
  crashhandler.h

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

#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <QString>

/**
 * CrashHandler is the crash-time counterpart of FreezeWatchdog: when the
 * process is about to die, it writes a report to %LOCALAPPDATA%\qlcplus\
 * (crash-<timestamp>.txt), captures a gdb backtrace of the crashing thread
 * and shows it in the same copyable dialog the freeze watchdog uses
 * (IDD_CRASH_DIALOG, qmlui.rc) - so a crash produces something pasteable
 * instead of the window just vanishing.
 *
 * Three ways a process dies are hooked, because each bypasses the others:
 *  - Qt fatal messages (Q_ASSERT/Q_ASSERT_X in a Debug build, qFatal()):
 *    Qt calls the installed message handler and then aborts via
 *    __fastfail(), which no exception filter or signal handler ever sees.
 *    The only hook is a QtMessageHandler receiving QtFatalMsg. Ours is
 *    installed unconditionally and chains every message (fatal ones
 *    included, so they still reach the -d log) to whatever handler was
 *    installed before it.
 *  - Hardware/SEH exceptions (access violation, illegal instruction, ...,
 *    i.e. what the same bugs look like in a Release build):
 *    SetUnhandledExceptionFilter(). After reporting, the filter returns
 *    EXCEPTION_CONTINUE_SEARCH so Windows Error Reporting still gets the
 *    exception and writes its own minidump if LocalDumps is configured.
 *  - abort() (uncaught C++ exception via std::terminate, assert(), ...):
 *    a SIGABRT handler.
 *
 * The report itself is produced on a freshly created helper thread while
 * the crashing thread waits on it: a modal dialog pumped on the crashed GUI
 * thread would also deliver paint/timer messages to Qt's own windows and
 * re-enter Qt rendering in whatever corrupted state caused the crash. This
 * keeps the crash case structurally identical to the freeze case, where
 * the dialog also runs off the GUI thread.
 *
 * Safety rails: only one report per process (a second crashing thread just
 * parks forever - the process is going down anyway), a re-entrant crash
 * inside the reporter itself terminates the process immediately, and
 * nothing is shown while a debugger is attached (it gets the crash instead).
 *
 * Known limitation: a stack overflow can't be reported (there is no stack
 * left to run the handler on).
 *
 * Windows-only, like FreezeWatchdog; install() is a no-op elsewhere.
 */
namespace CrashHandler
{
    /** Installs the Qt message handler, the unhandled-exception filter and
     *  the SIGABRT handler. Call once, early in main(), *after* any other
     *  qInstallMessageHandler() call (e.g. the -d log writer) so that one
     *  is what non-fatal messages get chained to. */
    void install();

    /**
     * Dev-only deliberate crash trigger, used to verify the reporter
     * end-to-end. $mode is one of "fatal" (qFatal - the Qt assertion path),
     * "segv" (null pointer write - the SEH path) or "abort" (std::abort -
     * the SIGABRT path). Anything else logs a warning and does nothing.
     *
     * Gated behind the QLCPLUS_DEBUG_CRASH environment variable in main.cpp
     * - never wired to any normal user-facing action/menu/flag, so it
     * cannot fire outside a deliberate manual test.
     */
    void debugTriggerCrash(const QString &mode);
}

#endif // CRASHHANDLER_H
