/*
  Q Light Controller Plus
  diagnostics.cpp

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

#include "diagnostics.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
// PSAPI_VERSION 1 = the classic psapi.dll exports (linked via psapi in
// qmlui/CMakeLists.txt), independent of whatever _WIN32_WINNT the Qt
// headers happen to set - the default would silently switch to the
// kernel32 K32* variants and change the link requirement.
#define PSAPI_VERSION 1
#include <psapi.h>
#include <tlhelp32.h>
#include <cstring>
#include <string>
#include <vector>
#include "diagnostics_resource.h"
#endif

namespace {

QMutex g_projectPathMutex;
QString g_projectPath;
std::atomic<bool> g_crashReportInProgress { false };

#ifdef Q_OS_WIN
// Bound how long we wait for the gdb child so a misbehaving gdb can't wedge
// the reporting thread forever.
constexpr DWORD kGdbWaitMs = 30000;
#endif

} // namespace

void Diagnostics::setCurrentProjectPath(const QString &path)
{
    QMutexLocker locker(&g_projectPathMutex);
    g_projectPath = path;
}

QString Diagnostics::currentProjectPath()
{
    QMutexLocker locker(&g_projectPathMutex);
    return g_projectPath.isEmpty() ? QStringLiteral("(none)") : g_projectPath;
}

void Diagnostics::setCrashReportInProgress()
{
    g_crashReportInProgress = true;
}

bool Diagnostics::isCrashReportInProgress()
{
    return g_crashReportInProgress.load();
}

#ifdef Q_OS_WIN

QString Diagnostics::reportsDir()
{
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    QString base = (len > 0 && len < MAX_PATH) ? QString::fromWCharArray(buf, int(len))
                                                : QDir::homePath();
    QString dir = base + QStringLiteral("\\qlcplus");
    QDir().mkpath(dir);
    return dir;
}

void *Diagnostics::createReportFile(const QString &filePath)
{
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    return CreateFileW(reinterpret_cast<const wchar_t *>(filePath.utf16()),
                       GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

void *Diagnostics::reportFileInvalid()
{
    return INVALID_HANDLE_VALUE;
}

void Diagnostics::writeReportLine(void *hFile, const QString &line)
{
    const QByteArray utf8 = (line + QStringLiteral("\r\n")).toUtf8();
    DWORD written = 0;
    WriteFile(static_cast<HANDLE>(hFile), utf8.constData(), DWORD(utf8.size()), &written, nullptr);
}

void Diagnostics::closeReportFile(void *hFile)
{
    CloseHandle(static_cast<HANDLE>(hFile));
}

namespace {

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

void Diagnostics::appendGdbAllThreadsBacktrace(void *hFileRaw)
{
    HANDLE hFile = static_cast<HANDLE>(hFileRaw);
    const qint64 pid = QCoreApplication::applicationPid();

    writeReportLine(hFile, QStringLiteral("--- gdb -p %1 -batch -ex \"info sharedlibrary\" -ex \"thread apply all bt\" ---").arg(pid));
    FlushFileBuffers(hFile);

    // "info sharedlibrary" (every module's load address range) goes first:
    // extractThreadSection() takes everything from a thread's header up to
    // the next "\nThread " as that thread's section, and the crashing thread
    // is regularly the last one gdb prints, so anything appended after the
    // backtraces would end up inside its section in the dialog.
    const QString cmdLine = QStringLiteral("\"%1\" -p %2 -batch -ex \"set pagination off\" -ex \"info sharedlibrary\" -ex \"thread apply all bt\"")
                                 .arg(resolveGdbPath())
                                 .arg(pid);

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
        writeReportLine(hFile, QStringLiteral("(failed to launch gdb, GetLastError=%1)").arg(GetLastError()));
    }
}

QStringList Diagnostics::processSnapshotLines()
{
    QStringList lines;
    const auto mb = [](quint64 bytes) { return QString::number(bytes / (1024 * 1024)); };

    PROCESS_MEMORY_COUNTERS_EX pmc;
    ZeroMemory(&pmc, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc), sizeof(pmc)))
    {
        lines << QStringLiteral("Process memory:   private %1 MB, working set %2 MB (peak %3 MB)")
                     .arg(mb(pmc.PrivateUsage), mb(pmc.WorkingSetSize), mb(pmc.PeakWorkingSetSize));
    }

    DWORD handles = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handles);

    // No direct "thread count of this process" API: walk a system thread
    // snapshot and count the ones owned by us.
    int threads = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        const DWORD ownPid = GetCurrentProcessId();
        THREADENTRY32 entry;
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (entry.th32OwnerProcessID == ownPid)
                    threads++;
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    lines << QStringLiteral("Handles/threads:  %1 handles, %2 threads").arg(handles).arg(threads);

    // Commit charge is what a std::bad_alloc actually ran into: a 64-bit
    // process never exhausts its address space, only RAM + page file.
    MEMORYSTATUSEX status;
    ZeroMemory(&status, sizeof(status));
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status))
    {
        lines << QStringLiteral("System memory:    commit %1 / %2 MB used, physical %3 / %4 MB free, load %5%")
                     .arg(mb(status.ullTotalPageFile - status.ullAvailPageFile), mb(status.ullTotalPageFile),
                          mb(status.ullAvailPhys), mb(status.ullTotalPhys))
                     .arg(status.dwMemoryLoad);
    }

    return lines;
}

QString Diagnostics::extractThreadSection(const QString &fullText, qint64 pid, unsigned long tid)
{
    // gdb's per-thread header reads e.g. "Thread 3 (Thread 1234.0x1a2b):" -
    // <tid> is literally the Windows thread ID in hex, which is what
    // GetCurrentThreadId() returns. Deliberately not based on gdb's own
    // "Thread N" numbering, which just reflects attach/enumeration order.
    const QString marker = QStringLiteral("Thread %1.0x%2)")
                                .arg(pid)
                                .arg(qulonglong(tid), 0, 16);
    const int markerPos = fullText.indexOf(marker);
    if (markerPos < 0)
        return QString();

    int lineStart = fullText.lastIndexOf(QLatin1Char('\n'), markerPos);
    lineStart = (lineStart < 0) ? 0 : lineStart + 1;

    const int nextThreadPos = fullText.indexOf(QStringLiteral("\nThread "), markerPos);
    const int sectionEnd = (nextThreadPos < 0) ? fullText.length() : nextThreadPos;

    return fullText.mid(lineStart, sectionEnd - lineStart).trimmed();
}

QString Diagnostics::readReportFile(const QString &filePath)
{
    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly))
        return QString::fromUtf8(f.readAll());
    return QString();
}

namespace {

// Data handed into a report dialog (IDD_*_DIALOG, qmlui.rc) via
// DialogBoxParamW's lParam; retrieved back in ReportDialogProc via
// GetWindowLongPtrW(hDlg, DWLP_USER) - the standard pattern for a plain
// (non-C++-class-based) Win32 dialog proc.
struct ReportDialogData
{
    std::wstring reportText; // CRLF-normalized
};

INT_PTR CALLBACK ReportDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITDIALOG:
    {
        auto *data = reinterpret_cast<ReportDialogData *>(lParam);
        SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(data));

        HWND hEdit = GetDlgItem(hDlg, IDC_DIAG_EDIT);
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
        case IDC_DIAG_COPY:
        {
            auto *data = reinterpret_cast<ReportDialogData *>(GetWindowLongPtrW(hDlg, DWLP_USER));
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

bool Diagnostics::showReportDialog(int dialogResourceId, const QString &text)
{
    // Custom dialog with a read-only multiline EDIT control, rather than
    // MessageBoxW or (as originally tried) TaskDialogIndirect: a Win32 EDIT
    // control - even ES_READONLY - natively supports drag-select,
    // double-click word-select, and Ctrl+A/Ctrl+C, which is the actual point
    // of this dialog (paste the backtrace straight into a bug report).
    // TaskDialogIndirect's content/expanded-information areas looked like
    // they should support that too, but a live end-to-end test showed their
    // text is NOT selectable, so that approach was dropped. Like MessageBoxW,
    // DialogBoxParamW pumps its own message loop on the calling thread -
    // confirmed working with the Qt main thread's own loop deadlocked (same
    // live test). A Copy-to-clipboard button is included as a
    // no-selection-required alternative.
    //
    // Normalize to CRLF: an EDIT control renders bare \n as one giant
    // unwrapped line, and gdb's own output is not guaranteed to be CRLF.
    QString combined = text;
    combined.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    combined.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));

    ReportDialogData dialogData;
    dialogData.reportText = combined.toStdWString();

    const INT_PTR dlgResult = DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(dialogResourceId),
                                               nullptr, ReportDialogProc, reinterpret_cast<LPARAM>(&dialogData));
    return dlgResult > 0;
}

#endif // Q_OS_WIN
