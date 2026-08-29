<#
.SYNOPSIS
  Standalone external freeze/hang monitor for a running qlcplus5.exe - polls the
  window's OS-level "hung" state and, on a confirmed hang, captures an all-thread
  gdb backtrace to a log file. For local dev/test sessions only.

.DESCRIPTION
  Companion to dev-build-run.ps1 / dev-ui-drive.ps1, and to the in-app
  FreezeWatchdog (qmlui/freezewatchdog.{h,cpp}) - see
  docs/agent-reports/2026-08-29-crash-freeze-diagnostics-options.md, option F2.
  This is a separate, zero-app-change tool: it works against the already
  deployed exe, does not require the in-app watchdog to be present or working,
  and can also catch the case where the in-app watchdog itself somehow fails
  to fire. It does not replace F3 and does not talk to it in any way.

  How it works:
    - Polls the target's top-level window every -PollIntervalMs via the same
      Win32 signal Explorer's own "(Not Responding)" detection uses:
      IsHungAppWindow(hwnd) (user32.dll). This is a cheap, non-blocking check -
      it reports whether the window already failed to answer the OS's own
      hang ping, it does not itself wait.
    - Requires -ConsecutiveHangPolls consecutive positive polls before treating
      it as a real hang (rather than one transient blip) - the default of 5
      polls at the default 1s interval means ~5s of continuously-confirmed
      hang, on top of the ~5s latency already built into Windows' own hang
      detection.
    - Skips capturing while a debugger is attached to the target process
      (CheckRemoteDebuggerPresent) - a process suspended at a breakpoint looks
      exactly like a hang from the outside and would otherwise false-positive
      constantly during a debugging session (e.g. while GammaRay or a manual
      gdb/WinDbg session is attached).
    - On a confirmed, undebugged hang: runs
        gdb -p <pid> -batch -ex "set pagination off" -ex "thread apply all bt"
      redirected to a timestamped log file under -OutDir (default
      %LOCALAPPDATA%\qlcplus\, same folder the in-app watchdog uses), then
      prints a clear message to the console. gdb attaching to walk stacks
      only pauses the target for the seconds that takes; it detaches
      afterwards and the app is left exactly as hung/not as it was.
    - After a capture, waits for the hang to actually clear (IsHungAppWindow
      goes false again) before it will capture again - so one long freeze
      produces one backtrace, not one every poll.

  Known blind spot (inherent to anything window-based, not fixable by this
  script): it can only see a hung *window* - i.e. the main/GUI thread's
  message loop stalled. A background thread hanging while the UI stays
  responsive produces no symptom here at all.

.PARAMETER ProcessName
  Name of the target process (without .exe). Default: qlcplus5

.PARAMETER PollIntervalMs
  Milliseconds between IsHungAppWindow polls. Default: 1000

.PARAMETER ConsecutiveHangPolls
  How many consecutive positive polls are required before a hang is treated
  as real and captured. Default: 5

.PARAMETER GdbPath
  Path to gdb.exe. Default: C:\msys64\mingw64\bin\gdb.exe (falls back to bare
  "gdb" - i.e. whatever resolves on PATH - if that fixed path doesn't exist).

.PARAMETER OutDir
  Directory to write freeze-*.txt logs into. Default: $env:LOCALAPPDATA\qlcplus

.PARAMETER GdbTimeoutSec
  Max seconds to wait for the gdb capture itself before giving up on it, so a
  misbehaving gdb can't wedge this script. Default: 30

.EXAMPLE
  .\dev-freeze-watchdog.ps1
  Monitors qlcplus5 with the defaults until Ctrl+C.

.EXAMPLE
  .\dev-freeze-watchdog.ps1 -ConsecutiveHangPolls 3 -PollIntervalMs 500
  More sensitive/faster-to-trigger monitoring for a short deliberate test.
#>
param(
    [string]$ProcessName = "qlcplus5",
    [int]$PollIntervalMs = 1000,
    [int]$ConsecutiveHangPolls = 5,
    [string]$GdbPath = "C:\msys64\mingw64\bin\gdb.exe",
    [string]$OutDir = "$env:LOCALAPPDATA\qlcplus",
    [int]$GdbTimeoutSec = 30
)

$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Runtime.InteropServices;

public class Win32HangCheck
{
    [DllImport("user32.dll")]
    public static extern bool IsHungAppWindow(IntPtr hWnd);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool CheckRemoteDebuggerPresent(IntPtr hProcess, ref bool isDebuggerPresent);

    public const uint PROCESS_QUERY_LIMITED_INFORMATION = 0x1000;
}
"@

if (-not (Test-Path $GdbPath)) {
    Write-Warning "gdb not found at '$GdbPath' - falling back to 'gdb' on PATH."
    $GdbPath = "gdb"
}

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
}

function Test-DebuggerAttached([int]$procId) {
    $handle = [Win32HangCheck]::OpenProcess([Win32HangCheck]::PROCESS_QUERY_LIMITED_INFORMATION, $false, [uint32]$procId)
    if ($handle -eq [IntPtr]::Zero) {
        return $false
    }
    try {
        $present = $false
        [void][Win32HangCheck]::CheckRemoteDebuggerPresent($handle, [ref]$present)
        return $present
    } finally {
        [void][Win32HangCheck]::CloseHandle($handle)
    }
}

function Invoke-FreezeCapture([int]$procId) {
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $logFile = Join-Path $OutDir "freeze-extmonitor-$timestamp.txt"

    Write-Host "[dev-freeze-watchdog] Hang confirmed for PID $procId - capturing backtrace to $logFile" -ForegroundColor Yellow

    "dev-freeze-watchdog.ps1 external hang capture" | Out-File -FilePath $logFile -Encoding utf8
    "Detected at: $(Get-Date -Format o)" | Out-File -FilePath $logFile -Append -Encoding utf8
    "PID: $procId" | Out-File -FilePath $logFile -Append -Encoding utf8
    "" | Out-File -FilePath $logFile -Append -Encoding utf8
    "--- gdb -p $procId -batch -ex `"thread apply all bt`" ---" | Out-File -FilePath $logFile -Append -Encoding utf8

    $gdbArgs = @("-p", "$procId", "-batch", "-ex", "set pagination off", "-ex", "thread apply all bt")
    try {
        $proc = Start-Process -FilePath $GdbPath -ArgumentList $gdbArgs `
            -NoNewWindow -PassThru `
            -RedirectStandardOutput "$logFile.stdout.tmp" `
            -RedirectStandardError "$logFile.stderr.tmp"
        $finished = $proc.WaitForExit($GdbTimeoutSec * 1000)
        if (-not $finished) {
            Write-Warning "[dev-freeze-watchdog] gdb did not finish within ${GdbTimeoutSec}s - killing it."
            try { $proc.Kill() } catch {}
        }
    } catch {
        "(failed to launch gdb: $_)" | Out-File -FilePath $logFile -Append -Encoding utf8
        Write-Warning "[dev-freeze-watchdog] Failed to launch gdb: $_"
        return
    }

    foreach ($tmp in @("$logFile.stdout.tmp", "$logFile.stderr.tmp")) {
        if (Test-Path $tmp) {
            Get-Content $tmp | Out-File -FilePath $logFile -Append -Encoding utf8
            Remove-Item $tmp -Force -ErrorAction SilentlyContinue
        }
    }

    Write-Host "[dev-freeze-watchdog] Backtrace saved to: $logFile" -ForegroundColor Green
}

Write-Host "[dev-freeze-watchdog] Monitoring '$ProcessName' every ${PollIntervalMs}ms (needs $ConsecutiveHangPolls consecutive hung polls to fire). Ctrl+C to stop."

$consecutiveHangs = 0
$capturedThisEpisode = $false

while ($true) {
    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue
    if (-not $proc -or $proc.MainWindowHandle -eq [IntPtr]::Zero) {
        if ($proc) {
            # Process exists but has no top-level window yet (still starting up) - not an error.
        } else {
            Write-Host "[dev-freeze-watchdog] '$ProcessName' is not running. Waiting..." -ForegroundColor DarkGray
        }
        $consecutiveHangs = 0
        $capturedThisEpisode = $false
        Start-Sleep -Milliseconds $PollIntervalMs
        continue
    }

    $hung = $false
    try {
        $hung = [Win32HangCheck]::IsHungAppWindow($proc.MainWindowHandle)
    } catch {
        Write-Warning "[dev-freeze-watchdog] IsHungAppWindow call failed: $_"
    }

    if ($hung) {
        $consecutiveHangs++
        if ($consecutiveHangs -eq 1) {
            Write-Host "[dev-freeze-watchdog] Window reported hung (poll 1/$ConsecutiveHangPolls)..." -ForegroundColor DarkYellow
        }

        if (-not $capturedThisEpisode -and $consecutiveHangs -ge $ConsecutiveHangPolls) {
            if (Test-DebuggerAttached -procId $proc.Id) {
                Write-Host "[dev-freeze-watchdog] Debugger detected on PID $($proc.Id) - assuming a deliberate breakpoint, not a real hang. Skipping capture." -ForegroundColor DarkGray
            } else {
                Invoke-FreezeCapture -procId $proc.Id
            }
            $capturedThisEpisode = $true
        }
    } else {
        if ($capturedThisEpisode) {
            Write-Host "[dev-freeze-watchdog] Window responsive again - re-arming." -ForegroundColor Green
        }
        $consecutiveHangs = 0
        $capturedThisEpisode = $false
    }

    Start-Sleep -Milliseconds $PollIntervalMs
}
