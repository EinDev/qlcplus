<#
.SYNOPSIS
  Build and run the engine/ QTest suite (60 suites under engine/test/) via CTest.
  For local TDD iteration only.

.DESCRIPTION
  Toolchain: MSYS2 MinGW64 (C:\msys64), Qt6 from pacman. See CLAUDE.md /
  dev-build-run.ps1 for why this project builds via MSYS2 rather than the
  official Qt installer.

  This script exists because, on this checkout, none of the following are true
  out of the box:
    - engine/test/*'s ~60 QTest binaries aren't wired to CTest at all (fixed by
      an `enable_testing()`/`add_test()` addition to engine/test/CMakeLists.txt
      + the root CMakeLists.txt, alongside this script).
    - They resolve fixture/profile/script resources via paths relative to their
      own binary dir (e.g. "../../../resources/fixtures/") - those need to be
      staged under build/resources first. unittest.sh does this, but assumes a
      different (non-CMake, Linux CI) build layout.
    - qlcplusengine.dll (and Qt's own DLLs) need to be on PATH at run time.
    - On Windows, these WIN32-subsystem QTest binaries print SILENTLY under both
      plain console execution and CTest's own output capture (confirmed by hand:
      identical bytes on stdout produce nothing unless you pass QTestLib's own
      `-o <file>,txt` flag). CTest's pass/fail *exit code* is reliable even when
      its captured log is empty - so this script re-runs any failed suite with
      `-o` to actually surface the failure detail, instead of leaving you with
      an unexplained "Failed".

.PARAMETER Filter
  Passed through to `ctest -R <Filter>` to run a subset (regex on test name,
  e.g. "scene|chaser"). Default: run everything.

.PARAMETER NoBuild
  Skip the `engine_tests` build step (just stage resources + run ctest).

.EXAMPLE
  .\dev-test-run.ps1
  Build engine_tests, stage resources, run the full suite.

.EXAMPLE
  .\dev-test-run.ps1 -Filter scene
  Build + run only test suites whose name matches /scene/.
#>
param(
    [string]$Filter = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$RepoRoot     = $PSScriptRoot
$RepoRootMsys = "/d/Projekte/VR/Resonite/qlcplus"   # same path, msys2 form; adjust if the repo ever moves
$Bash         = "C:\msys64\usr\bin\bash.exe"
$BuildDir     = Join-Path $RepoRoot "build"
$EngineDll    = Join-Path $BuildDir "engine\src"
$AudioDll     = Join-Path $BuildDir "engine\audio"
$MingwBin     = "C:\msys64\mingw64\bin"

if (-not (Test-Path $Bash)) {
    Write-Error "MSYS2 bash not found at $Bash. This script expects the MSYS2 MinGW64 toolchain set up for this project."
}

# --- 1. Build the test binaries (fast: only engine_tests + its deps, not a full rebuild) ---
if (-not $NoBuild) {
    Write-Host "==> Building engine_tests..." -ForegroundColor Cyan
    $buildCmd = "export MSYSTEM=MINGW64; source /etc/profile; cd $RepoRootMsys; cmake --build build --target engine_tests -j`$(nproc)"
    & $Bash -lc $buildCmd
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed (exit $LASTEXITCODE)."
    }
}

# --- 2. Stage resources under build/resources (merge-copy: build/resources/fixtures etc.
#         already exist as CMake build dirs from add_subdirectory(resources), so this must
#         merge missing files into them, not skip because the directory already exists) ---
Write-Host "==> Staging resources..." -ForegroundColor Cyan
foreach ($d in @("colorfilters", "fixtures", "gobos", "icons", "inputprofiles", "rgbscripts", "schemas")) {
    $dest = Join-Path $BuildDir "resources\$d"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Copy-Item -Path (Join-Path $RepoRoot "resources\$d\*") -Destination $dest -Recurse -Force -ErrorAction SilentlyContinue
}

# --- 3. Run the suite ---
Write-Host "==> Running tests..." -ForegroundColor Cyan
$env:PATH = "$EngineDll;$AudioDll;$MingwBin;" + $env:PATH
# Safety net: a test that QCOMPARE-fails partway through a manual-MasterTimer-tick
# sequence (see engine/test's own convention of never calling Function::stop() +
# a final tick on early return) can leave a Function registered with a Universe's
# GenericFader; Doc's implicit QObject-child teardown at process exit has then been
# observed to hang rather than crash. QtTest's watchdog turns that into a bounded
# failure instead of hanging the whole suite/dev loop.
$env:QTEST_FUNCTION_TIMEOUT = "30000"

$ctestArgs = @("-j1", "--output-on-failure", "--timeout", "60")   # -j1: parallel runs have shown a
                                                # heap-corruption crash (STATUS_HEAP_CORRUPTION) in
                                                # this Qt/MinGW build's WIN32-subsystem test
                                                # binaries; serial execution avoids it. --timeout is
                                                # ctest's own outer backstop, above QTEST_FUNCTION_TIMEOUT.
if ($Filter -ne "") {
    $ctestArgs += @("-R", $Filter)
}

Push-Location $BuildDir
try {
    & ctest @ctestArgs
    $ctestExit = $LASTEXITCODE

    if ($ctestExit -ne 0) {
        # ctest's own captured output is empty for these binaries (see .DESCRIPTION) - rerun
        # each failed suite directly with QTestLib's -o flag so the actual failure is visible.
        $failedNames = & ctest -N --rerun-failed 2>$null |
            Select-String -Pattern '^\s*Test\s+#\d+:\s+(\S+)' |
            ForEach-Object { $_.Matches[0].Groups[1].Value }

        if ($failedNames) {
            Write-Host ""
            Write-Host "==> Re-running failed suites with verbose QTestLib output:" -ForegroundColor Yellow
            foreach ($name in $failedNames) {
                $dir = $name -replace '_test$', ''
                $exe = Join-Path $BuildDir "engine\test\$dir\$name.exe"
                if (Test-Path $exe) {
                    Write-Host ""
                    Write-Host "---- $name ----" -ForegroundColor Yellow
                    $outFile = Join-Path $env:TEMP "$name-verbose.txt"
                    Push-Location (Split-Path $exe)
                    & $exe -o "$outFile,txt" | Out-Null
                    Pop-Location
                    if (Test-Path $outFile) {
                        Get-Content $outFile
                    } else {
                        Write-Host "(no output file produced - suite likely crashed before QTestLib could write results)"
                    }
                }
            }
        }
    }
} finally {
    Pop-Location
}

exit $ctestExit
