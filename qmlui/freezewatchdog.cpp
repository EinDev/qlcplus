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
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <cstring>
#include <vector>
#include "freezewatchdog_resource.h"
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
    // Captured here (called from the GUI thread) so the watchdog thread can
    // later pick this exact thread's section out of gdb's "thread apply all
    // bt" output by matching gdb's "(Thread <pid>.0x<tid>)" header text -
    // more robust than trusting gdb's own "Thread N" numbering, which is
    // attach-order dependent, not identity.
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

// Pulls just the blocked (main/GUI) thread's own section out of gdb's
// "thread apply all bt" output, so the freeze dialog can show a short,
// directly-relevant, copyable backtrace instead of every thread (which can
// run to tens of KB across 30+ threads - see the full file for that).
//
// Matches on gdb's own "(Thread <pid>.0x<tid>)" header text, where <tid> is
// literally the Windows thread ID in hex - i.e. exactly what
// GetCurrentThreadId() returned when FreezeWatchdog::start() captured it on
// the GUI thread. This is deliberately not based on gdb's "Thread N"
// numbering, which just reflects attach/enumeration order, not identity.
QString extractMainThreadSection(const QString &fullText, qint64 pid, unsigned long mainTid)
{
    const QString marker = QStringLiteral("Thread %1.0x%2)")
                                .arg(pid)
                                .arg(qulonglong(mainTid), 0, 16);
    const int markerPos = fullText.indexOf(marker);
    if (markerPos < 0)
        return QString();

    int lineStart = fullText.lastIndexOf(QLatin1Char('\n'), markerPos);
    lineStart = (lineStart < 0) ? 0 : lineStart + 1;

    const int nextThreadPos = fullText.indexOf(QStringLiteral("\nThread "), markerPos);
    const int sectionEnd = (nextThreadPos < 0) ? fullText.length() : nextThreadPos;

    return fullText.mid(lineStart, sectionEnd - lineStart).trimmed();
}

// Data handed into the freeze dialog (IDD_FREEZE_DIALOG, qmlui.rc) via
// DialogBoxParamW's lParam; retrieved back in FreezeDialogProc via
// GetWindowLongPtrW(hDlg, DWLP_USER) - the standard pattern for a plain
// (non-C++-class-based) Win32 dialog proc.
struct FreezeDialogData
{
    std::wstring reportText; // combined summary + backtrace, CRLF-normalized
};

INT_PTR CALLBACK FreezeDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        auto *data = reinterpret_cast<FreezeDialogData *>(lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(data));

        HWND hEdit = GetDlgItem(hDlg, IDC_FREEZE_EDIT);
        SetWindowTextW(hEdit, data ? data->reportText.c_str() : L"");
        // Select-all up front so a plain Ctrl+C works immediately, without
        // the user needing to click/drag-select first.
        SetFocus(hEdit);
        SendMessageW(hEdit, EM_SETSEL, 0, -1);
        return FALSE; // we already set focus ourselves
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        case IDC_FREEZE_COPY:
        {
            auto *data = reinterpret_cast<FreezeDialogData *>(GetWindowLongPtrW(hDlg, DWLP_USER));
            if (data && OpenClipboard(hDlg))
            {
                EmptyClipboard();
                const size_t bytes = (data->reportText.size() + 1) * sizeof(wchar_t);
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
                if (hMem)
                {
                    void *dst = GlobalLock(hMem);
                    if (dst)
                    {
                        memcpy(dst, data->reportText.c_str(), bytes);
                        GlobalUnlock(hMem);
                        SetClipboardData(CF_UNICODETEXT, hMem);
                    }
                    else
                    {
                        GlobalFree(hMem);
                    }
                }
                CloseClipboard();
            }
            return TRUE;
        }
        default:
            break;
        }
        break;
    case WM_CLOSE:
        // DialogBoxParamW does NOT close on WM_CLOSE by default (unlike
        // MessageBoxW/TaskDialogIndirect) - without this, Alt-F4/the title
        // bar X/a scripted PostMessage(WM_CLOSE) would all do nothing.
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    default:
        break;
    }
    return FALSE;
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

    // Read the report back so its text can be shown (selectable/copyable)
    // directly in the dialog below, not just referenced by path.
    QString fullReport;
    {
        QFile f(filePath);
        if (f.open(QIODevice::ReadOnly))
            fullReport = QString::fromUtf8(f.readAll());
    }

    const QString mainThreadBacktrace = extractMainThreadSection(fullReport, QCoreApplication::applicationPid(), m_mainThreadId);

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

    // Custom dialog (IDD_FREEZE_DIALOG, qmlui.rc) with a read-only multiline
    // EDIT control, rather than MessageBoxW or (as originally tried)
    // TaskDialogIndirect: a Win32 EDIT control - even ES_READONLY - natively
    // supports drag-select, double-click word-select, and Ctrl+A/Ctrl+C,
    // which is the actual point of this dialog (paste the backtrace straight
    // into a bug report). TaskDialogIndirect's content/expanded-information
    // areas looked like they should support that too, but a live end-to-end
    // test showed their text is NOT selectable, so that approach was
    // dropped. Like MessageBoxW, DialogBoxParamW pumps its own message loop
    // on the calling thread - confirmed working with the Qt main thread's
    // own loop deadlocked (same live test). A Copy-to-clipboard button is
    // included as a no-selection-required alternative.
    QString combined = content + QStringLiteral("\r\n\r\n") + expanded;
    // Normalize to CRLF: an EDIT control renders bare \n as one giant
    // unwrapped line, and gdb's own output (unlike our own writeLine() calls
    // above) is not guaranteed to already be CRLF.
    combined.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    combined.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));

    FreezeDialogData dialogData;
    dialogData.reportText = combined.toStdWString();

    const INT_PTR dlgResult = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_FREEZE_DIALOG),
                                               nullptr, FreezeDialogProc, reinterpret_cast<LPARAM>(&dialogData));
    const bool shownDialog = dlgResult > 0;
    if (!shownDialog)
        qWarning().noquote() << "[FreezeWatchdog] DialogBoxParamW failed, GetLastError=" << GetLastError();

    if (!shownDialog)
    {
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
