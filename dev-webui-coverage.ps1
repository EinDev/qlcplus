<#
.SYNOPSIS
  Measure JavaScript coverage of the browser web UI (webui/) by running the headless-Chrome
  end-to-end drivers (webui/tools/e2e/*.js), each against its own plugin-less sandbox, and write
  an HTML report to coverage/webui/index.html.

.DESCRIPTION
  The web UI has no unit tests; the e2e drivers are its only automated tests, so this measures
  what they exercise. For every driver it:
    1. starts the driver's sandbox via dev-webui-sandbox.ps1 (copy of C:\qlcplus without
       Plugins\, patch-stripped SF3 copy, own ports - never 9010/9011, never the live install),
    2. runs the driver under `node -r webui/tools/coverage/hook.js`, which records V8 precise
       coverage for every page the driver opens,
    3. stops the sandbox again.
  Then webui/tools/coverage/report.js maps the coverage back to the source files (Babel compiles
  the .jsx in the browser with inline source maps) and writes the report.

  A sandbox whose process (qlc-<Name>.exe) is already running is SKIPPED, not restarted:
  dev-webui-sandbox.ps1 kills that process first, and it may belong to another session.
  Pass -Force to take it over anyway.

  Not included: fixturedefs-smoke.js (API only, opens no page). vendor/ and _ds_bundle.js are
  excluded from the report. A failing driver does not stop the run; its coverage still counts,
  it is listed at the end and the exit code is non-zero.

  Needs Node >= 22 and Chrome/Edge (see webui/tools/cdp.js). The first run installs the report
  generator (monocart-coverage-reports) into webui/tools/coverage/node_modules (git-ignored,
  never installed with the web UI).

.PARAMETER Drivers   Only run these (efx, rgb, media, vclayout, show, vccue, vclive, fixtures, io, vcinput, fixdefs, partialsff). Default: all.
.PARAMETER BuildDir  CMake build directory with qmlui\qlcplus5.exe (default: build).
.PARAMETER ReportOnly  Skip the drivers; regenerate the report from the last run's raw dumps.
.PARAMETER Force     Restart a sandbox even if its process is already running.
.PARAMETER Open      Open the HTML report when done.

.EXAMPLE
  .\dev-webui-coverage.ps1 -Open
  .\dev-webui-coverage.ps1 -Drivers vccue,vclayout
#>
param(
    [string[]]$Drivers = @(),
    [string]$BuildDir = "build",
    [switch]$ReportOnly,
    [switch]$Force,
    [switch]$Open
)

$ErrorActionPreference = "Stop"
$repo     = $PSScriptRoot
$toolDir  = Join-Path $repo "webui\tools\coverage"
$rawDir   = Join-Path $repo "coverage\webui-raw"
$outDir   = Join-Path $repo "coverage\webui"
$logDir   = Join-Path $rawDir "logs"

# Sandbox name / ports per driver: the same map the drivers default to (see their headers).
$all = @(
    @{ Name = "efx";      Api = 9120; Web = 9121; Script = "efx-collection.js"; Args = @() },
    @{ Name = "rgb";      Api = 9130; Web = 9131; Script = "rgbmatrix.js";      Args = @("9130", "9131", "C:\qlcsandbox\rgb") },
    @{ Name = "media";    Api = 9140; Web = 9141; Script = "media.js";          Args = @("--api", "9140", "--web", "9141") },
    @{ Name = "vclayout"; Api = 9150; Web = 9151; Script = "vc-layout.js";      Args = @() },
    @{ Name = "show";     Api = 9160; Web = 9161; Script = "show.js";           Args = @("9160", "9161", "C:\qlcsandbox\show") },
    @{ Name = "vccue";    Api = 9170; Web = 9171; Script = "vc-cue.js";         Args = @() },
    # vc-live.js talks to [::1] by default (E2E_HOST): Logitech's lghub_updater can hold 127.0.0.1:9180.
    @{ Name = "vclive";   Api = 9180; Web = 9181; Script = "vc-live.js";        Args = @() },
    @{ Name = "fixtures"; Api = 9210; Web = 9211; Script = "fixtures-views.js"; Args = @() },
    @{ Name = "fxmisc";   Api = 9250; Web = 9251; Script = "fixtures-misc.js";  Args = @(); Sandbox = @{ UserModifiersDir = "C:\qlcsandbox\fxmisc\UserModifiers" } },
    @{ Name = "io";       Api = 9190; Web = 9191; Script = "io.js";             Args = @("--api", "9190", "--web", "9191") },
    @{ Name = "vcinput";  Api = 9220; Web = 9221; Script = "vc-input.js";       Args = @() },
    @{ Name = "partialsff"; Api = 9320; Web = 9321; Script = "partials-ff.js";  Args = @() },
    @{ Name = "wizard";   Api = 9270; Web = 9271; Script = "wizard-import.js";  Args = @("9270", "9271", "C:\qlcsandbox\wizard") },
    @{ Name = "tools";    Api = 9260; Web = 9261; Script = "tools-misc.js";     Args = @("--userdir", "C:\qlcsandbox\tools\UserFixtures"); Sandbox = @{ UserFixtureDir = "C:\qlcsandbox\tools\UserFixtures" } },
    @{ Name = "vcshow";   Api = 9310; Web = 9311; Script = "vc-show-leftovers.js"; Args = @("--api", "9310", "--web", "9311", "--sandbox", "C:\qlcsandbox\vcshow") },
    # Sandbox = extra dev-webui-sandbox.ps1 parameters. The fixture editor writes .qxf files, so its
    # user fixture folder must point into the sandbox (the driver refuses otherwise).
    @{ Name = "fixdefs";  Api = 9200; Web = 9201; Script = "fixture-editor.js"; Args = @("--api", "9200", "--web", "9201", "--userdir", "C:\qlcsandbox\fixdefs\UserFixtures");
       Sandbox = @{ UserFixtureDir = "C:\qlcsandbox\fixdefs\UserFixtures" } }
)
# New drivers keep landing in webui/tools/e2e; flag any browser driver (one that calls launch())
# this list does not know yet, so a report never silently leaves one out.
$known = $all | ForEach-Object { $_.Script }
Get-ChildItem (Join-Path $repo "webui\tools\e2e") -Filter *.js | Where-Object {
    $_.Name -notin $known -and (Select-String -Path $_.FullName -SimpleMatch -Pattern "launch(" -Quiet)
} | ForEach-Object {
    Write-Host "==> $($_.Name) is a browser e2e driver this script does not run yet - add it to the driver list (sandbox name, ports, args)." -ForegroundColor Yellow
}
if ($Drivers.Count) {
    $unknown = $Drivers | Where-Object { $_ -notin $all.Name }
    if ($unknown) { throw "Unknown driver(s): $($unknown -join ', '). Known: $($all.Name -join ', ')" }
    $all = $all | Where-Object { $_.Name -in $Drivers }
}

if (-not (Get-Command node -ErrorAction SilentlyContinue)) { throw "node not found on PATH (Node >= 22 needed)." }
if (-not (Test-Path (Join-Path $toolDir "node_modules\monocart-coverage-reports"))) {
    Write-Host "==> Installing the report generator into webui\tools\coverage\node_modules..." -ForegroundColor Cyan
    Push-Location $toolDir
    try { npm install --no-audit --no-fund | Out-Host; if ($LASTEXITCODE -ne 0) { throw "npm install failed" } } finally { Pop-Location }
}

$failed = @(); $skipped = @()
if (-not $ReportOnly) {
    if (Test-Path $rawDir) { Remove-Item -Recurse -Force $rawDir }
    New-Item -ItemType Directory -Force -Path $logDir | Out-Null
    $hook = Join-Path $toolDir "hook.js"
    foreach ($d in $all) {
        if (-not $Force -and (Get-Process -Name "qlc-$($d.Name)" -ErrorAction SilentlyContinue)) {
            Write-Host "==> $($d.Script): sandbox '$($d.Name)' is already running (another session?) - skipped. -Force takes it over." -ForegroundColor Yellow
            $skipped += $d.Name
            continue
        }
        Write-Host "==> $($d.Script) on sandbox '$($d.Name)' ($($d.Api)/$($d.Web))..." -ForegroundColor Cyan
        $sandboxArgs = if ($d.Sandbox) { $d.Sandbox } else { @{} }
        & (Join-Path $repo "dev-webui-sandbox.ps1") -Name $d.Name -BuildDir (Join-Path $repo $BuildDir) `
            -WebUiRoot (Join-Path $repo "webui") -ApiPort $d.Api -WebUiPort $d.Web @sandboxArgs *>&1 |
            ForEach-Object { "$_" } | Out-File -Encoding utf8 (Join-Path $logDir "sandbox-$($d.Name).log")   # *> file = UTF-16 on PS 5
        # dev-webui-sandbox.ps1 calls the sandbox ready once 127.0.0.1:<api> accepts a connection, but
        # another program can hold that exact address (Logitech's lghub_updater on 127.0.0.1:9180), so it
        # can return before QLC+ listens. Wait for this sandbox's own web UI (it serves the config file
        # with its API port) and for the API port on [::1], which only the sandbox binds.
        $deadline = (Get-Date).AddSeconds(90); $up = $false
        while (-not $up -and (Get-Date) -lt $deadline) {
            try {
                $cfg = Invoke-RestMethod -UseBasicParsing -TimeoutSec 2 "http://localhost:$($d.Web)/qlcplus-config.json"
                if ($cfg.apiPort -eq $d.Api) {
                    $c = New-Object Net.Sockets.TcpClient([Net.Sockets.AddressFamily]::InterNetworkV6)
                    try { $c.Connect([Net.IPAddress]::IPv6Loopback, $d.Api); $up = $true } finally { $c.Close() }
                }
            } catch { Start-Sleep -Milliseconds 500 }
        }
        if (-not $up) { Write-Host "    sandbox did not come up on its own ports within 90 s - see the sandbox log" -ForegroundColor Yellow }
        try {
            $shots = Join-Path $rawDir "shots\$($d.Name)"
            $env:QLC_JSCOV_DIR = $rawDir; $env:QLC_JSCOV_TAG = $d.Name
            # Driver-specific overrides (each driver reads its own subset).
            $env:E2E_API_PORT = $d.Api; $env:E2E_WEB_PORT = $d.Web; $env:E2E_OUT = $shots
            $env:QLC_API = "ws://127.0.0.1:$($d.Api)/"; $env:QLC_WEB = "http://localhost:$($d.Web)/"; $env:QLC_SHOTS = $shots
            $driverArgs = $d.Args
            if ($d.Name -in "media", "io", "fixdefs") { $driverArgs += @("--out", $shots) }
            Push-Location $repo
            $saved = $ErrorActionPreference; $ErrorActionPreference = "Continue"
            try { node -r $hook (Join-Path "webui\tools\e2e" $d.Script) @driverArgs 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 (Join-Path $logDir "e2e-$($d.Name).log"); $code = $LASTEXITCODE }
            finally { $ErrorActionPreference = $saved; Pop-Location }
            if ($code -eq 0) { Write-Host "    passed" }
            else { Write-Host "    FAILED (exit $code) - see coverage\webui-raw\logs\e2e-$($d.Name).log" -ForegroundColor Yellow; $failed += $d.Name }
        } finally {
            & (Join-Path $repo "dev-webui-sandbox.ps1") -Name $d.Name -Stop *> $null
        }
    }
    foreach ($v in "QLC_JSCOV_DIR", "QLC_JSCOV_TAG", "E2E_API_PORT", "E2E_WEB_PORT", "E2E_OUT", "QLC_API", "QLC_WEB", "QLC_SHOTS") { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
}

Write-Host "==> Generating the report..." -ForegroundColor Cyan
$saved = $ErrorActionPreference; $ErrorActionPreference = "Continue"
node (Join-Path $toolDir "report.js") $rawDir $outDir 2>&1 | ForEach-Object { "$_" } | Out-Host
$rc = $LASTEXITCODE; $ErrorActionPreference = $saved
if ($rc -ne 0) { throw "report.js failed (exit $rc)" }

Write-Host ""
Write-Host "    HTML: coverage\webui\index.html" -ForegroundColor Green
if ($skipped) { Write-Host "    Skipped (sandbox busy): $($skipped -join ', ')" -ForegroundColor Yellow }
if ($failed)  { Write-Host "    Failed drivers: $($failed -join ', ')" -ForegroundColor Yellow }
if ($Open) { Start-Process (Join-Path $outDir "index.html") }
if ($failed) { exit 1 }
