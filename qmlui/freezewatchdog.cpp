/*
  Q Light Controller Plus
  freezewatchdog.cpp

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

#include "freezewatchdog.h"
#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include "diagnostics_resource.h"
#endif

namespace {

// How long (ms) the main-thread heartbeat must go silent before we consider
// the app frozen. Generous on purpose: legitimate long synchronous work
// (loading a huge .qxw, a modal native dialog, a drag loop - all of which
// still pump timers) must not trigger a false positive. Per
// docs/agent-reports/2026-08-29-crash-freeze-diagnostics-options.md section
// F3, 10-15s was suggested; 12s splits that range.
constexpr qint64 kFreezeThresholdMs = 12000;

// How often the watchdog thread wakes up to re-check the heartbeat.
constexpr int kPollIntervalMs = 1500;

// How often the GUI-thread timer refreshes the heartbeat.
constexpr int kHeartbeatIntervalMs = 1000;

} // namespace

FreezeWatchdog::FreezeWatchdog(QObject *parent)
    : QObject(parent)
    , m_heartbeatTimer(new QTimer(this))
{
    m_startMs = QDateTime::currentMSecsSinceEpoch();
    m_lastHeartbeatMs = m_startMs;
    connect(m_heartbeatTimer, &QTimer::timeout, this, &FreezeWatchdog::onHeartbeatTimer);
}

FreezeWatchdog::~FreezeWatchdog()
{
    stop();
}

void FreezeWatchdog::start()
{
    m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
    m_heartbeatTimer->start(kHeartbeatIntervalMs);

#ifdef Q_OS_WIN
    // Captured here (called from the GUI thread) so the watchdog thread can
    // later pick this exact thread's section out of gdb's "thread apply all
    // bt" output - see Diagnostics::extractThreadSection().
    m_mainThreadId = GetCurrentThreadId();
    m_thread = std::thread(&FreezeWatchdog::watchdogLoop, this);
#endif
}

void FreezeWatchdog::stop()
{
    m_heartbeatTimer->stop();
    m_stopRequested = true;
    if (m_thread.joinable())
        m_thread.join();
}

void FreezeWatchdog::onHeartbeatTimer()
{
    m_lastHeartbeatMs = QDateTime::currentMSecsSinceEpoch();
}

void FreezeWatchdog::setCurrentProjectPath(const QString &path)
{
    Diagnostics::setCurrentProjectPath(path);
}

void FreezeWatchdog::debugBlockMainThread(int seconds)
{
    qWarning().noquote() << QStringLiteral(
        "[FreezeWatchdog] QLCPLUS_DEBUG_FREEZE is set - deliberately blocking the "
        "main thread for %1s to test the freeze watchdog. This must never happen "
        "outside a manual dev test.").arg(seconds);
    QThread::sleep(uint(seconds));
}

#ifdef Q_OS_WIN

void FreezeWatchdog::watchdogLoop()
{
    while (!m_stopRequested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 age = now - m_lastHeartbeatMs.load();

        if (!m_fired.load())
        {
            // A crash report in progress parks the crashing (GUI) thread
            // until its dialog is dismissed - that's not a freeze.
            if (age > kFreezeThresholdMs && !IsDebuggerPresent()
                && !Diagnostics::isCrashReportInProgress())
            {
                m_fired = true;
                m_freezeStartMs = now - age;
                onFreezeDetected(age);
            }
        }
        else if (!m_recoveryLogged.load())
        {
            if (age < kFreezeThresholdMs)
            {
                appendRecoveryNote();
                m_recoveryLogged = true;
            }
        }
    }
}

void FreezeWatchdog::onFreezeDetected(qint64 heartbeatAgeMs)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 uptimeMs = now - m_startMs;

    const QString dir = Diagnostics::reportsDir();
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString filePath = dir + QStringLiteral("\\freeze-%1.txt").arg(timestamp);
    m_diagnosticFilePath = filePath;

    void *hFile = Diagnostics::createReportFile(filePath);
    if (hFile == Diagnostics::reportFileInvalid())
    {
        qWarning().noquote() << "[FreezeWatchdog] could not create diagnostic file" << filePath;
        // Still try the message box - at least the user learns something is wrong.
        MessageBoxW(nullptr,
                    L"QLC+ appears to be frozen, but the watchdog could not write a "
                    L"diagnostic file. See stderr/-d log if available.",
                    L"QLC+ - Freeze detected", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return;
    }

    Diagnostics::writeReportLine(hFile, QStringLiteral("QLC+ freeze watchdog report"));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Detected at:      %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Process uptime:   %1 s").arg(uptimeMs / 1000));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Heartbeat gap:    %1 ms (threshold %2 ms)").arg(heartbeatAgeMs).arg(kFreezeThresholdMs));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Open project:     %1").arg(Diagnostics::currentProjectPath()));
    Diagnostics::writeReportLine(hFile, QStringLiteral("PID:              %1").arg(QCoreApplication::applicationPid()));
    Diagnostics::appendProcessSnapshot(hFile);
    Diagnostics::writeReportLine(hFile, QString());
    Diagnostics::appendGdbAllThreadsBacktrace(hFile);
    Diagnostics::closeReportFile(hFile);

    // Read the report back so its text can be shown (selectable/copyable)
    // directly in the dialog below, not just referenced by path.
    const QString fullReport = Diagnostics::readReportFile(filePath);
    const QString mainThreadBacktrace = Diagnostics::extractThreadSection(fullReport, QCoreApplication::applicationPid(), m_mainThreadId);

    QString expanded;
    bool usedFullDumpFallback = false;
    if (!mainThreadBacktrace.isEmpty())
    {
        expanded = mainThreadBacktrace;
    }
    else if (!fullReport.isEmpty())
    {
        // Couldn't isolate the main thread's own section (unexpected gdb
        // output format, thread already gone, etc.) - still show as much of
        // the real text as reasonably fits rather than nothing.
        usedFullDumpFallback = true;
        constexpr int kFallbackCharLimit = 8000;
        expanded = fullReport.left(kFallbackCharLimit);
        if (fullReport.length() > kFallbackCharLimit)
            expanded += QStringLiteral("\n\n... (truncated - see the full report file for the rest)");
    }
    else
    {
        expanded = QStringLiteral("(no backtrace text available - gdb may have failed to run; see the diagnostic file for details, if any)");
    }

    QString content = QStringLiteral(
        "The main thread hasn't responded for about %1 seconds.\n\n"
        "A full diagnostic report (every thread) was saved to:\n%2")
        .arg(heartbeatAgeMs / 1000)
        .arg(filePath);
    if (usedFullDumpFallback)
        content += QStringLiteral("\n\n(Could not isolate the frozen thread's own section below - showing the start of the full multi-thread dump instead.)");

    const bool shownDialog = Diagnostics::showReportDialog(IDD_FREEZE_DIALOG,
                                                           content + QStringLiteral("\n\n") + expanded);
    if (!shownDialog)
    {
        qWarning().noquote() << "[FreezeWatchdog] DialogBoxParamW failed, GetLastError=" << GetLastError();

        const QString msg = QStringLiteral(
            "QLC+ appears to be frozen.\n\n"
            "Diagnostic information has been saved to:\n%1\n\n"
            "You can end the process now, or wait to see if it recovers.")
            .arg(filePath);

        MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(msg.utf16()),
                    L"QLC+ - Freeze detected", MB_OK | MB_ICONWARNING | MB_TOPMOST);
    }
}

void FreezeWatchdog::appendRecoveryNote()
{
    // Note: onFreezeDetected() blocks the watchdog thread for as long as the
    // dialog is on screen, so recovery can't be observed/logged until the
    // user dismisses it, even if the main thread actually resumed earlier.
    if (m_diagnosticFilePath.isEmpty())
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 recoveredAfterMs = now - m_freezeStartMs;

    HANDLE hFile = CreateFileW(reinterpret_cast<const wchar_t *>(m_diagnosticFilePath.utf16()),
                                FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    const QString line = QStringLiteral(
        "\r\n--- Recovered after ~%1 s (heartbeat resumed at %2) ---\r\n")
        .arg(recoveredAfterMs / 1000)
        .arg(QDateTime::currentDateTime().toString(Qt::ISODate));
    const QByteArray utf8 = line.toUtf8();
    DWORD written = 0;
    WriteFile(hFile, utf8.constData(), DWORD(utf8.size()), &written, nullptr);
    CloseHandle(hFile);
}

#else // !Q_OS_WIN

void FreezeWatchdog::watchdogLoop() { }
void FreezeWatchdog::onFreezeDetected(qint64) { }
void FreezeWatchdog::appendRecoveryNote() { }

#endif
