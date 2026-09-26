<#
.SYNOPSIS
  Measure unit-test code coverage (engine/, controlapi/, qmlui/) on this
  Windows/MSYS2 checkout and write HTML + Cobertura + text reports under
  coverage/.

.DESCRIPTION
  Upstream QLC+ ships coverage.sh (lcov, Linux, driven by unittest.sh) and a
  `-Dcoverage=ON` CMake switch (coverage.cmake: -fprofile-arcs
  -ftest-coverage -lgcov). Only the CMake switch is usable here: lcov is not
  installed, and unittest.sh assumes the Linux test layout. This script is
  the Windows equivalent, built on gcovr (pip install gcovr) and MSYS2's own
  gcov, which must match the g++ that compiled the objects (both from
  C:\msys64\mingw64\bin).

  It uses a SEPARATE build directory (default: build-coverage), never build/:
  dev-build-run.ps1 deploys build/ into C:\qlcplus, and instrumented objects
  there would make the shipped app write .gcda files on every exit.

  Steps:
    1. Configure <BuildDir> with -Dcoverage=ON (only if not configured yet).
    2. Build engine_tests, controlapi_tests and qmlui_tests (not the app).
    3. Delete stale *.gcda counters so every run measures from zero.
    4. Stage resources/ into <BuildDir>/resources (same set dev-test-run.ps1
       stages - the automatic copies only happen on a full ALL build).
    5. Run the CTest suites (engine + controlapi, -j1, see dev-test-run.ps1
       for why serial) and the qmlui/test binaries directly (they are not in
       CTest by design - see CLAUDE.md).
    6. Run gcovr: one JSON tracefile over everything, then per-area summaries
       (engine, controlapi, qmlui) derived from it, plus coverage/html/ and
       coverage/coverage.xml (Cobertura, for CI/IDE tooling).

  Unlike upstream's coverage.sh, a failing test does NOT abort the report:
  coverage is measured for whatever ran, the failing suites are listed at the
  end, and the exit code is non-zero so CI still notices.

  What the numbers include: every line hit by ANY test binary. The engine
  number therefore also contains hits from controlapi/qmlui tests that
  exercise engine code (small, but real). Header files (inline getters in
  engine/src/*.h etc.) are counted, like gcovr's default and unlike
  upstream's lcov script, which stripped *.h.

.PARAMETER Filter
  Passed to `ctest -R <Filter>` (engine + controlapi suites only). The qmlui
  binaries always run.

.PARAMETER NoBuild
  Skip the configure/build steps (re-run tests + report on the existing
  instrumented binaries).

.PARAMETER NoTests
  Skip running tests; just regenerate the reports from the .gcda files left
  by a previous run.

.PARAMETER BuildDir
  Instrumented build directory, relative to the repo root. Default:
  build-coverage.

.PARAMETER Open
  Open coverage/html/index.html in the default browser when done.

.EXAMPLE
  .\dev-test-coverage.ps1
  Full run: configure (first time), build, run everything, write reports.

.EXAMPLE
  .\dev-test-coverage.ps1 -Filter scene -NoBuild
  Re-run only the /scene/ suites on the existing build and report.
#>
param(
    [string]$Filter = "",
    [switch]$NoBuild,
    [switch]$NoTests,
    [string]$BuildDir = "build-coverage",
    [switch]$Open
)

$ErrorActionPreference = "Stop"

$RepoRoot      = $PSScriptRoot
$RepoRootMsys  = "<repo>"   # same path, msys2 form; adjust if the repo ever moves
$Bash          = "C:\msys64\usr\bin\bash.exe"
$MingwBin      = "C:\msys64\mingw64\bin"
$Gcov          = "C:/msys64/mingw64/bin/gcov.exe"
$BuildPath     = Join-Path $RepoRoot $BuildDir
$EngineDll     = Join-Path $BuildPath "engine\src"
$AudioDll      = Join-Path $BuildPath "engine\audio"
$CoverageDir   = Join-Path $RepoRoot "coverage"
$TraceFile     = Join-Path $CoverageDir "coverage.json"
$SummaryFile   = Join-Path $CoverageDir "summary.txt"

if (-not (Test-Path $Bash)) {
    Write-Error "MSYS2 bash not found at $Bash. This script expects the MSYS2 MinGW64 toolchain set up for this project."
}
if (-not (Test-Path $Gcov)) {
    Write-Error "gcov not found at $Gcov (pacman -S mingw-w64-x86_64-toolchain)."
}
# Resolve the Windows Python NOW: the test step below puts C:\msys64\mingw64\bin
# first on PATH (for the Qt/MinGW runtime DLLs), and MSYS2 ships its own
# python.exe there, which has no gcovr.
$Python = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $Python) { Write-Error "python not found on PATH (gcovr needs it: pip install gcovr)." }
& $Python -m gcovr --version *> $null
if ($LASTEXITCODE -ne 0) {
    Write-Error "gcovr is not installed for $Python. Install it with: pip install gcovr"
}

function Invoke-MsysBash([string]$Command) {
    # Out-Host: the build output must go to the console, not into the
    # function's return value, or the caller gets "ninja: no work to do." as
    # part of $rc and treats a successful build as a failure.
    & $Bash -lc "export MSYSTEM=MINGW64; export MSYSTEM_CARCH=x86_64; source /etc/profile; cd $RepoRootMsys; $Command" | Out-Host
    return $LASTEXITCODE
}

# --- 1./2. Configure + build the instrumented test binaries -------------------------
if (-not $NoBuild) {
    if (-not (Test-Path (Join-Path $BuildPath "build.ninja"))) {
        Write-Host "==> Configuring $BuildDir with -Dcoverage=ON..." -ForegroundColor Cyan
        $rc = Invoke-MsysBash "cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=C:/qlcplus -Dqmlui=ON -Dcoverage=ON"
        if ($rc -ne 0) { Write-Error "Configure failed (exit $rc)." }
    }
    Write-Host "==> Building engine_tests, controlapi_tests and qmlui_tests (instrumented)..." -ForegroundColor Cyan
    $rc = Invoke-MsysBash "cmake --build $BuildDir --target engine_tests controlapi_tests qmlui_tests -j`$(nproc)"
    if ($rc -ne 0) { Write-Error "Build failed (exit $rc)." }
}

if (-not (Test-Path $BuildPath)) {
    Write-Error "$BuildPath does not exist - run without -NoBuild first."
}

$env:PATH = "$EngineDll;$AudioDll;$MingwBin;" + $env:PATH
$env:QTEST_FUNCTION_TIMEOUT = "30000"
$failedSuites = @()

if (-not $NoTests) {
    # --- 3. Zero the counters ----------------------------------------------------------
    Write-Host "==> Removing stale .gcda counters..." -ForegroundColor Cyan
    Get-ChildItem -Path $BuildPath -Recurse -Filter *.gcda -File | Remove-Item -Force

    # --- 4. Stage resources (merge-copy, see dev-test-run.ps1) ---------------------------
    Write-Host "==> Staging resources..." -ForegroundColor Cyan
    foreach ($d in @("colorfilters", "fixtures", "gobos", "icons", "inputprofiles", "rgbscripts", "schemas")) {
        $dest = Join-Path $BuildPath "resources\$d"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item -Path (Join-Path $RepoRoot "resources\$d\*") -Destination $dest -Recurse -Force -ErrorAction SilentlyContinue
    }

    # --- 5a. CTest suites (engine + controlapi) ----------------------------------------
    Write-Host "==> Running CTest suites (engine + controlapi)..." -ForegroundColor Cyan
    # -j1: parallel runs have shown STATUS_HEAP_CORRUPTION in these WIN32-subsystem
    # binaries (see dev-test-run.ps1). --timeout is generous because -O0 +
    # instrumentation is noticeably slower than the normal Debug build.
    $ctestArgs = @("-j1", "--timeout", "180")
    if ($Filter -ne "") { $ctestArgs += @("-R", $Filter) }
    Push-Location $BuildPath
    try {
        & ctest @ctestArgs
        if ($LASTEXITCODE -ne 0) {
            $failedSuites += & ctest -N --rerun-failed 2>$null |
                Select-String -Pattern '^\s*Test\s+#\d+:\s+(\S+)' |
                ForEach-Object { $_.Matches[0].Groups[1].Value }
        }
    } finally {
        Pop-Location
    }

    # --- 5b. qmlui/test binaries (not in CTest by design) -------------------------------
    Write-Host "==> Running qmlui/test binaries..." -ForegroundColor Cyan
    $qmluiExes = Get-ChildItem -Path (Join-Path $BuildPath "qmlui\test") -Recurse -Filter "*_test.exe" -File
    if ($qmluiExes.Count -eq 0) {
        Write-Warning "No qmlui/test binaries found under $BuildDir\qmlui\test - was qmlui_tests built?"
    }
    foreach ($exe in $qmluiExes) {
        $name = $exe.BaseName
        Push-Location $exe.DirectoryName
        try {
            & $exe.FullName -o "$name-result.txt,txt" | Out-Null
            $status = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        $resultFile = Join-Path $exe.DirectoryName "$name-result.txt"
        $totals = if (Test-Path $resultFile) { (Select-String -Path $resultFile -Pattern '^Totals:').Line } else { "(no result file - crashed before QTestLib could write it)" }
        if ($status -eq 0) {
            Write-Host "    $name  $totals"
        } else {
            Write-Host "    $name  FAILED (exit $status)  $totals" -ForegroundColor Yellow
            $failedSuites += $name
        }
    }
}

# --- 6. gcovr ------------------------------------------------------------------------
Write-Host "==> Generating coverage reports..." -ForegroundColor Cyan
New-Item -ItemType Directory -Force -Path (Join-Path $CoverageDir "html") | Out-Null

# Common exclusions: the test sources themselves, CMake/Qt autogen (moc, qrc),
# and anything inside the build tree. Throw/unreachable branches are excluded
# because Qt's implicit exception edges otherwise swamp the branch numbers.
$gcovrCommon = @(
    "--root", $RepoRoot,
    "--exclude", ".*_test\.cpp$",
    "--exclude", ".*/test/.*",
    "--exclude", ".*_autogen/.*",
    "--exclude", ".*/$BuildDir/.*",
    "--exclude-throw-branches",
    "--exclude-unreachable-branches"
)

# One tracefile over every area of interest; the per-area summaries and the
# HTML/XML reports are derived from it without re-running gcov.
& $Python -m gcovr @gcovrCommon `
    --object-directory $BuildPath `
    --gcov-executable $Gcov `
    --gcov-ignore-errors=no_working_dir_found `
    --filter "engine/src/" --filter "controlapi/src/" --filter "qmlui/" `
    --json $TraceFile
if ($LASTEXITCODE -ne 0) { Write-Error "gcovr failed while collecting coverage data (exit $LASTEXITCODE)." }

& $Python -m gcovr @gcovrCommon --add-tracefile $TraceFile `
    --html-details (Join-Path $CoverageDir "html\index.html") `
    --cobertura (Join-Path $CoverageDir "coverage.xml") `
    --cobertura-pretty
if ($LASTEXITCODE -ne 0) { Write-Error "gcovr failed while writing the HTML/XML reports (exit $LASTEXITCODE)." }

# Per-area numbers: gcovr's --filter also applies when reading a tracefile, so
# each area is a cheap re-summarise of coverage.json (no gcov re-run).
$summaryRows = @()
foreach ($area in @(
        @{ Name = "engine";     Filter = "engine/src/" },
        @{ Name = "controlapi"; Filter = "controlapi/src/" },
        @{ Name = "qmlui";      Filter = "qmlui/" },
        @{ Name = "TOTAL";      Filter = $null })) {
    $areaJson  = Join-Path $CoverageDir "summary-$($area.Name).json"
    $gcovrArgs = @("--add-tracefile", $TraceFile, "--json-summary", $areaJson) + $gcovrCommon
    if ($area.Filter) { $gcovrArgs += @("--filter", $area.Filter) }
    & $Python -m gcovr @gcovrArgs *> $null
    if ($LASTEXITCODE -ne 0) { Write-Error "gcovr failed while summarising $($area.Name) (exit $LASTEXITCODE)." }
    $s = Get-Content $areaJson -Raw | ConvertFrom-Json
    # Invariant culture: "70.7%" regardless of the machine's locale, so the
    # summary is greppable/diffable across machines (the -f operator would
    # otherwise print "70,7%" on a German Windows).
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $fmt = { param($pct, $cov, $tot) [string]::Format($inv, "{0,5:F1}%  ({1}/{2})", $pct, $cov, $tot) }
    $summaryRows += [pscustomobject]@{
        Area      = $area.Name
        Files     = $s.files.Count
        Lines     = & $fmt $s.line_percent     $s.line_covered     $s.line_total
        Functions = & $fmt $s.function_percent $s.function_covered $s.function_total
        Branches  = & $fmt $s.branch_percent   $s.branch_covered   $s.branch_total
    }
}

$summaryText = ($summaryRows | Format-Table -AutoSize | Out-String).TrimEnd()
$summaryText | Set-Content -Path $SummaryFile
Write-Host ""
Write-Host "==> Coverage summary (also in coverage\summary.txt):" -ForegroundColor Green
Write-Host $summaryText
Write-Host ""
Write-Host "    HTML:      coverage\html\index.html"
Write-Host "    Cobertura: coverage\coverage.xml"

if ($failedSuites.Count -gt 0) {
    Write-Host ""
    Write-Host "==> $($failedSuites.Count) test suite(s) FAILED (coverage above still includes whatever they ran):" -ForegroundColor Yellow
    $failedSuites | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
}

if ($Open) { Start-Process (Join-Path $CoverageDir "html\index.html") }

if ($failedSuites.Count -gt 0) { exit 1 }
exit 0
