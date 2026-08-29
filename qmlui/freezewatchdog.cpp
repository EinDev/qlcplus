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

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <vector>
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

#ifdef Q_OS_WIN
// Bound how long we wait for the gdb child so a misbehaving gdb can't wedge
// the watchdog thread itself forever.
constexpr DWORD kGdbWaitMs = 30000;
#endif

QMutex g_projectPathMutex;
QString g_projectPath;

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
    QMutexLocker locker(&g_projectPathMutex);
    g_projectPath = path;
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

namespace {

QString diagnosticsDir()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    QString base = (len > 0 && len < MAX_PATH) ? QString::fromWCharArray(buf, int(len))
                                                : QDir::homePath();
    QString dir = base + QStringLiteral("\\qlcplus");
    QDir().mkpath(dir);
    return dir;
}

// The dev machine's gdb lives at a documented, fixed MSYS2 location (see
// CLAUDE.md) that is normally NOT on PATH for a plain-launched qlcplus5.exe
// (only MSYS2-shell builds/launches put it there). Prefer that known path;
// fall back to bare "gdb" in case PATH does carry it on some other machine.
QString resolveGdbPath()
{
    static const wchar_t *kKnownGdb = L"C:\\msys64\\mingw64\\bin\\gdb.exe";
    if (GetFileAttributesW(kKnownGdb) != INVALID_FILE_ATTRIBUTES)
        return QString::fromWCharArray(kKnownGdb);
    return QStringLiteral("gdb");
}

} // namespace

void FreezeWatchdog::watchdogLoop()
{
    while (!m_stopRequested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 age = now - m_lastHeartbeatMs.load();

        if (!m_fired.load())
        {
            if (age > kFreezeThresholdMs && !IsDebuggerPresent())
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

    const QString dir = diagnosticsDir();
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString filePath = dir + QStringLiteral("\\freeze-%1.txt").arg(timestamp);
    m_diagnosticFilePath = filePath;

    QString projectPath;
    {
        QMutexLocker locker(&g_projectPathMutex);
        projectPath = g_projectPath;
    }
    if (projectPath.isEmpty())
        projectPath = QStringLiteral("(none)");

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hFile = CreateFileW(reinterpret_cast<const wchar_t *>(filePath.utf16()),
                                GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        qWarning().noquote() << "[FreezeWatchdog] could not create diagnostic file" << filePath;
        // Still try the message box - at least the user learns something is wrong.
        MessageBoxW(nullptr,
                    L"QLC+ appears to be frozen, but the watchdog could not write a "
                    L"diagnostic file. See stderr/-d log if available.",
                    L"QLC+ - Freeze detected", MB_OK | MB_ICONWARNING | MB_TOPMOST);
        return;
    }

    auto writeLine = [hFile](const QString &line) {
        const QByteArray utf8 = (line + QStringLiteral("\r\n")).toUtf8();
        DWORD written = 0;
        WriteFile(hFile, utf8.constData(), DWORD(utf8.size()), &written, nullptr);
    };

    writeLine(QStringLiteral("QLC+ freeze watchdog report"));
    writeLine(QStringLiteral("Detected at:      %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    writeLine(QStringLiteral("Process uptime:   %1 s").arg(uptimeMs / 1000));
    writeLine(QStringLiteral("Heartbeat gap:    %1 ms (threshold %2 ms)").arg(heartbeatAgeMs).arg(kFreezeThresholdMs));
    writeLine(QStringLiteral("Open project:     %1").arg(projectPath));
    writeLine(QStringLiteral("PID:              %1").arg(QCoreApplication::applicationPid()));
    writeLine(QString());
    writeLine(QStringLiteral("--- gdb -p %1 -batch -ex \"thread apply all bt\" ---").arg(QCoreApplication::applicationPid()));
    FlushFileBuffers(hFile);

    const QString gdbPath = resolveGdbPath();
    const QString cmdLine = QStringLiteral("\"%1\" -p %2 -batch -ex \"set pagination off\" -ex \"thread apply all bt\"")
                                 .arg(gdbPath)
                                 .arg(QCoreApplication::applicationPid());

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hFile;
    si.hStdError = hFile;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcessW's lpCommandLine must be a mutable buffer.
    std::wstring cmdLineW = cmdLine.toStdWString();
    std::vector<wchar_t> cmdLineBuf(cmdLineW.begin(), cmdLineW.end());
    cmdLineBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(nullptr, cmdLineBuf.data(), nullptr, nullptr,
                              /*bInheritHandles=*/TRUE, CREATE_NO_WINDOW, nullptr,
                              nullptr, &si, &pi);
    if (ok)
    {
        WaitForSingleObject(pi.hProcess, kGdbWaitMs);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
    {
        writeLine(QStringLiteral("(failed to launch gdb, GetLastError=%1)").arg(GetLastError()));
    }

    CloseHandle(hFile);

    const QString msg = QStringLiteral(
        "QLC+ appears to be frozen.\n\n"
        "Diagnostic information has been saved to:\n%1\n\n"
        "You can end the process now, or wait to see if it recovers.")
        .arg(filePath);

    MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(msg.utf16()),
                L"QLC+ - Freeze detected", MB_OK | MB_ICONWARNING | MB_TOPMOST);
}

void FreezeWatchdog::appendRecoveryNote()
{
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
