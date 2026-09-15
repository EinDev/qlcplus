/*
  Q Light Controller Plus
  crashhandler.cpp

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

#include "crashhandler.h"
#include "diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QStringList>

#include <atomic>
#include <csignal>
#include <cstdlib>

#ifdef Q_OS_WIN
#include <windows.h>
#include "diagnostics_resource.h"
#endif

namespace {

QtMessageHandler g_previousMessageHandler = nullptr;
qint64 g_installedAtMs = 0;

#ifdef Q_OS_WIN

std::atomic<bool> g_reporting { false };
std::atomic<unsigned long> g_reportingThreadId { 0 };  // the thread that crashed
std::atomic<unsigned long> g_helperThreadId { 0 };     // the thread writing the report

struct ReportJob
{
    QString kind;               // one-line classification, e.g. "Access violation"
    QStringList details;        // extra "Key: value" lines for the report header
    unsigned long crashingTid;  // GetCurrentThreadId() of the thread that crashed
};

// The crashing thread's backtrace always begins with the reporter's own
// plumbing - WaitForSingleObject <- produceReport <- whichever handler caught
// the crash <- the OS/CRT exception dispatch - before reaching the frame that
// actually faulted. Hide those from the dialog so the first frame the user
// sees is the interesting one; the report file keeps the full trace.
QString hideReporterFrames(const QString &section)
{
    static const char *const kMarkers[] = {
        "produceReport", "crashMessageHandler", "unhandledExceptionFilter",
        "abortSignalHandler", "KiUserExceptionDispatcher", "msvcrt!abort",
        "UnhandledExceptionFilter (",
    };
    constexpr int kMaxFramesToScan = 20;

    const QStringList lines = section.split(QLatin1Char('\n'));
    int lastMarkerLine = -1;
    int framesSeen = 0;
    for (int i = 0; i < lines.size() && framesSeen < kMaxFramesToScan; ++i)
    {
        const QString &line = lines.at(i);
        if (!line.startsWith(QLatin1Char('#')))
            continue;
        ++framesSeen;
        for (const char *marker : kMarkers)
        {
            if (line.contains(QLatin1String(marker)))
            {
                lastMarkerLine = i;
                break;
            }
        }
    }
    if (lastMarkerLine < 0)
        return section;

    // Never hide everything: if no frame survives, show the trace as-is.
    bool frameRemains = false;
    for (int i = lastMarkerLine + 1; i < lines.size() && !frameRemains; ++i)
        frameRemains = lines.at(i).startsWith(QLatin1Char('#'));
    if (!frameRemains)
        return section;

    QStringList out;
    if (!lines.isEmpty() && lines.first().startsWith(QLatin1String("Thread ")))
        out << lines.first();
    out << QStringLiteral("(reporter-internal frames hidden - the full trace is in the report file)");
    out += lines.mid(lastMarkerLine + 1);
    return out.join(QLatin1Char('\n'));
}

// Runs on the helper thread. Everything that can block or pump messages
// happens here, never on the crashing thread itself.
DWORD WINAPI reportThreadProc(LPVOID param)
{
    g_helperThreadId = GetCurrentThreadId();

    const ReportJob *job = static_cast<const ReportJob *>(param);
    const qint64 pid = QCoreApplication::applicationPid();
    const qint64 uptimeMs = QDateTime::currentMSecsSinceEpoch() - g_installedAtMs;

    const QString dir = Diagnostics::reportsDir();
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString filePath = dir + QStringLiteral("\\crash-%1.txt").arg(timestamp);

    void *hFile = Diagnostics::createReportFile(filePath);
    if (hFile == Diagnostics::reportFileInvalid())
    {
        const QString msg = QStringLiteral("QLC+ has crashed (%1), and the crash report file could not be written to:\n%2")
                                .arg(job->kind, filePath);
        MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(msg.utf16()),
                    L"QLC+ - Crash detected", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return 0;
    }

    Diagnostics::writeReportLine(hFile, QStringLiteral("QLC+ crash report"));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Detected at:      %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate)));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Process uptime:   %1 s").arg(uptimeMs / 1000));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Open project:     %1").arg(Diagnostics::currentProjectPath()));
    Diagnostics::writeReportLine(hFile, QStringLiteral("PID:              %1").arg(pid));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Crashing thread:  0x%1").arg(qulonglong(job->crashingTid), 0, 16));
    Diagnostics::writeReportLine(hFile, QStringLiteral("Crash type:       %1").arg(job->kind));
    for (const QString &line : job->details)
        Diagnostics::writeReportLine(hFile, line);
    const QStringList snapshot = Diagnostics::processSnapshotLines();
    for (const QString &line : snapshot)
        Diagnostics::writeReportLine(hFile, line);
    Diagnostics::writeReportLine(hFile, QString());
    Diagnostics::appendGdbAllThreadsBacktrace(hFile);
    Diagnostics::closeReportFile(hFile);

    const QString fullReport = Diagnostics::readReportFile(filePath);
    const QString crashingThreadBacktrace = Diagnostics::extractThreadSection(fullReport, pid, job->crashingTid);

    QString expanded;
    bool usedFullDumpFallback = false;
    if (!crashingThreadBacktrace.isEmpty())
    {
        expanded = hideReporterFrames(crashingThreadBacktrace);
    }
    else if (!fullReport.isEmpty())
    {
        usedFullDumpFallback = true;
        constexpr int kFallbackCharLimit = 8000;
        expanded = fullReport.left(kFallbackCharLimit);
        if (fullReport.length() > kFallbackCharLimit)
            expanded += QStringLiteral("\n\n... (truncated - see the full report file for the rest)");
    }
    else
    {
        expanded = QStringLiteral("(no backtrace text available - gdb may have failed to run; see the report file for details, if any)");
    }

    QString content = QStringLiteral("Crash type: %1\n").arg(job->kind);
    for (const QString &line : job->details)
        content += line + QLatin1Char('\n');
    for (const QString &line : snapshot)
        content += line + QLatin1Char('\n');
    content += QStringLiteral("\nA full report (every thread) was saved to:\n%1\n\n"
                              "The process will exit when this dialog is closed.")
                   .arg(filePath);
    if (usedFullDumpFallback)
        content += QStringLiteral("\n\n(Could not isolate the crashing thread's own section below - showing the start of the full multi-thread dump instead.)");

    const bool shownDialog = Diagnostics::showReportDialog(IDD_CRASH_DIALOG,
                                                           content + QStringLiteral("\n\n") + expanded);
    if (!shownDialog)
    {
        const QString msg = QStringLiteral("QLC+ has crashed (%1).\n\nA crash report was saved to:\n%2")
                                .arg(job->kind, filePath);
        MessageBoxW(nullptr, reinterpret_cast<const wchar_t *>(msg.utf16()),
                    L"QLC+ - Crash detected", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
    return 0;
}

// Called on the crashing thread. Hands the report off to a helper thread
// and blocks until the user has dismissed the dialog; the caller then lets
// the original crash proceed (Qt abort / WER / abort()).
void produceReport(const QString &kind, const QStringList &details)
{
    // A debugger wants the raw crash, not our dialog.
    if (IsDebuggerPresent())
        return;

    const unsigned long tid = GetCurrentThreadId();

    bool expected = false;
    if (!g_reporting.compare_exchange_strong(expected, true))
    {
        if (g_reportingThreadId.load() == tid || g_helperThreadId.load() == tid)
        {
            // We crashed again *inside* the reporter - on the crashing
            // thread (a chained handler) or on the helper thread writing
            // the report (e.g. heap corruption biting the report's own
            // allocations). Parking here would leave the crashing thread
            // waiting on the helper forever: a silent hang with no dialog
            // and no WER dump. Nothing more to be gained, stop here.
            TerminateProcess(GetCurrentProcess(), 3);
        }
        // Some other thread is already reporting; the process is going
        // down once its dialog closes. Don't fight over the report.
        Sleep(INFINITE);
    }
    g_reportingThreadId = tid;
    Diagnostics::setCrashReportInProgress();

    ReportJob job;
    job.kind = kind;
    job.details = details;
    job.crashingTid = tid;

    HANDLE hThread = CreateThread(nullptr, 0, reportThreadProc, &job, 0, nullptr);
    if (hThread != nullptr)
    {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }
    else
    {
        // Couldn't get a helper thread - better an in-place report than none.
        reportThreadProc(&job);
    }
}

QString exceptionCodeName(DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:         return QStringLiteral("Access violation");
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return QStringLiteral("Array bounds exceeded");
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return QStringLiteral("Datatype misalignment");
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return QStringLiteral("Floating-point divide by zero");
        case EXCEPTION_FLT_INVALID_OPERATION:    return QStringLiteral("Floating-point invalid operation");
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return QStringLiteral("Illegal instruction");
        case EXCEPTION_IN_PAGE_ERROR:            return QStringLiteral("In-page error");
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return QStringLiteral("Integer divide by zero");
        case EXCEPTION_PRIV_INSTRUCTION:         return QStringLiteral("Privileged instruction");
        case EXCEPTION_STACK_OVERFLOW:           return QStringLiteral("Stack overflow");
        default:                                 return QStringLiteral("Unhandled exception");
    }
}

QString moduleForAddress(const void *addr)
{
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &hMod) || hMod == nullptr)
        return QStringLiteral("(unknown module)");

    wchar_t path[MAX_PATH];
    const DWORD len = GetModuleFileNameW(hMod, path, MAX_PATH);
    const QString name = (len > 0) ? QString::fromWCharArray(path, int(len)) : QStringLiteral("(unnamed module)");
    const quintptr offset = reinterpret_cast<quintptr>(addr) - reinterpret_cast<quintptr>(hMod);
    return QStringLiteral("%1+0x%2").arg(name).arg(qulonglong(offset), 0, 16);
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS *info)
{
    QString kind = QStringLiteral("Unhandled exception");
    QStringList details;

    if (info != nullptr && info->ExceptionRecord != nullptr)
    {
        const EXCEPTION_RECORD *rec = info->ExceptionRecord;
        kind = exceptionCodeName(rec->ExceptionCode);
        details << QStringLiteral("Exception code:   0x%1").arg(qulonglong(rec->ExceptionCode), 8, 16, QLatin1Char('0'));
        details << QStringLiteral("Fault address:    0x%1 (%2)")
                       .arg(qulonglong(reinterpret_cast<quintptr>(rec->ExceptionAddress)), 0, 16)
                       .arg(moduleForAddress(rec->ExceptionAddress));

        if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR)
            && rec->NumberParameters >= 2)
        {
            const char *what = rec->ExceptionInformation[0] == 0 ? "reading"
                             : rec->ExceptionInformation[0] == 1 ? "writing"
                             : "executing";
            details << QStringLiteral("Memory access:    %1 address 0x%2")
                           .arg(QLatin1String(what))
                           .arg(qulonglong(rec->ExceptionInformation[1]), 0, 16);
        }
    }

    produceReport(kind, details);

    // Let Windows Error Reporting see the exception too, so its LocalDumps
    // minidump (if configured on this machine) is still written.
    return EXCEPTION_CONTINUE_SEARCH;
}

void abortSignalHandler(int)
{
    produceReport(QStringLiteral("abort() called"),
                  QStringList() << QStringLiteral("Typically an uncaught C++ exception (std::terminate) or a failed assert()."));
    // Returning lets the default abort() behavior run and end the process.
}

#endif // Q_OS_WIN

void crashMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    // Chain first, so a fatal message still lands in the -d log / stderr
    // before the dialog blocks (and before Qt aborts the process).
    if (g_previousMessageHandler != nullptr)
        g_previousMessageHandler(type, context, msg);

#ifdef Q_OS_WIN
    if (type == QtFatalMsg)
    {
        QStringList details;
        details << QStringLiteral("Message:          %1").arg(msg);
        if (context.file != nullptr)
            details << QStringLiteral("Location:         %1:%2").arg(QLatin1String(context.file)).arg(context.line);
        if (context.function != nullptr)
            details << QStringLiteral("Function:         %1").arg(QLatin1String(context.function));

        produceReport(QStringLiteral("Qt fatal message (assertion failure or qFatal)"), details);
        // Qt aborts the process via __fastfail() right after we return.
    }
#else
    Q_UNUSED(context);
#endif
}

} // namespace

void CrashHandler::install()
{
    g_installedAtMs = QDateTime::currentMSecsSinceEpoch();
    g_previousMessageHandler = qInstallMessageHandler(crashMessageHandler);

#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    std::signal(SIGABRT, abortSignalHandler);
#endif
}

void CrashHandler::debugTriggerCrash(const QString &mode)
{
    qWarning().noquote() << QStringLiteral(
        "[CrashHandler] QLCPLUS_DEBUG_CRASH=%1 is set - deliberately crashing to "
        "test the crash reporter. This must never happen outside a manual dev test.").arg(mode);

    if (mode == QLatin1String("fatal"))
    {
        qFatal("QLCPLUS_DEBUG_CRASH test: deliberate qFatal()");
    }
    else if (mode == QLatin1String("segv"))
    {
        // Read the null through a volatile so the compiler can't fold the
        // write into a trap instruction (which would test a different path).
        static volatile quintptr zero = 0;
        int *p = reinterpret_cast<int *>(zero);
        *p = 42;
    }
    else if (mode == QLatin1String("abort"))
    {
        std::abort();
    }
    else
    {
        qWarning().noquote() << "[CrashHandler] unknown QLCPLUS_DEBUG_CRASH mode, expected fatal|segv|abort";
    }
}
