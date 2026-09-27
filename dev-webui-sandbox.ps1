<#
.SYNOPSIS
  Run a throwaway, plugin-less copy of QLC+ 5 for web UI / Control API testing without touching
  the live install in C:\qlcplus or the user's running qlcplus5.exe.

.DESCRIPTION
  Creates C:\qlcsandbox\<Name> once (a copy of C:\qlcplus WITHOUT Plugins\, so no DMX can leave the
  machine), overwrites the project-built binaries in it from a build directory (qlcplus5.exe renamed
  to qlc-<Name>.exe so it is distinguishable in Task Manager, plus qlcplusengine.dll), writes a copy
  of the test project with every <Input>/<Output>/<Feedback> patch stripped, and launches it with
  -d --api --webui on the given ports, serving the web UI straight from a source tree. stdout/stderr
  go to a timestamped log in the sandbox directory. Waits until the API port accepts connections.

  Reading from C:\qlcplus is all this script does to the live install. Never point -ApiPort/-WebUiPort
  at 9010/9011 (the live instance).

.PARAMETER Name        Sandbox id (letters/digits/dash), e.g. "efx". Directory C:\qlcsandbox\<Name>.
.PARAMETER BuildDir    CMake build directory holding qmlui\qlcplus5.exe and engine\src\qlcplusengine.dll.
.PARAMETER WebUiRoot   Directory served as the web UI (a worktree's webui\ folder).
.PARAMETER ApiPort     WebSocket Control API port (must not be 9010).
.PARAMETER WebUiPort   HTTP port for the web UI (must not be 9011).
.PARAMETER Project     .qxw to load (default: the SF3 test scene). Copied + patch-stripped into the sandbox.
.PARAMETER Stop        Only kill this sandbox's process (qlc-<Name>.exe) and exit.
.PARAMETER NoLaunch    Prepare the sandbox but don't start it.

.EXAMPLE
  .\dev-webui-sandbox.ps1 -Name efx -BuildDir .\build -WebUiRoot .\webui -ApiPort 9120 -WebUiPort 9121
  .\dev-webui-sandbox.ps1 -Name efx -Stop
#>
param(
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z0-9-]+$')][string]$Name,
    [string]$BuildDir = "",
    [string]$WebUiRoot = "",
    [int]$ApiPort = 0,
    [int]$WebUiPort = 0,
    [string]$Project = "<shows-dir>\SF3.qxw",
    [switch]$Stop,
    [switch]$NoLaunch
)

$ErrorActionPreference = "Stop"
$live = "C:\qlcplus"
$root = "C:\qlcsandbox"
$dest = Join-Path $root $Name
$exeName = "qlc-$Name.exe"
$procName = "qlc-$Name"

Get-Process -Name $procName -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "Stopping running $exeName (pid $($_.Id))"
    Stop-Process -Id $_.Id -Force
}
if ($Stop) { return }

if (-not $BuildDir -or -not $WebUiRoot -or $ApiPort -eq 0 -or $WebUiPort -eq 0) {
    throw "-BuildDir, -WebUiRoot, -ApiPort and -WebUiPort are required unless -Stop is given"
}
if ($ApiPort -eq 9010 -or $WebUiPort -eq 9011 -or $ApiPort -eq 9011 -or $WebUiPort -eq 9010) {
    throw "Ports 9010/9011 belong to the live instance - pick others"
}
$BuildDir = (Resolve-Path $BuildDir).Path
$WebUiRoot = (Resolve-Path $WebUiRoot).Path
$exeSrc = Join-Path $BuildDir "qmlui\qlcplus5.exe"
$dllSrc = Join-Path $BuildDir "engine\src\qlcplusengine.dll"
foreach ($f in @($exeSrc, $dllSrc, $Project, (Join-Path $WebUiRoot "index.html"))) {
    if (-not (Test-Path $f)) { throw "Missing: $f" }
}

if (-not (Test-Path (Join-Path $dest "qt.conf"))) {
    Write-Host "Creating sandbox $dest from $live (without Plugins) ..."
    New-Item -ItemType Directory -Force $dest | Out-Null
    # /E copy subdirs, /XD exclude dirs, /NFL /NDL quiet, /R:1 /W:1 fast fail. Exit codes < 8 are success.
    robocopy $live $dest /E /XD "$live\Plugins" "$live\Web" "$live\WebUI" /NFL /NDL /NJH /NJS /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with $LASTEXITCODE" }
    Remove-Item (Join-Path $dest "qlcplus5.exe") -ErrorAction SilentlyContinue
}
Copy-Item $exeSrc (Join-Path $dest $exeName) -Force
Copy-Item $dllSrc (Join-Path $dest "qlcplusengine.dll") -Force

# Strip every patch so nothing is ever output, even if a plugin somehow loads.
$xml = Get-Content $Project -Raw
$stripped = [regex]::Replace($xml, '^\s*<(Output|Input|Feedback)\b[^>]*/>\s*\r?\n', '', 'Multiline')
$stripped = [regex]::Replace($stripped, '<(Output|Input|Feedback)\b[^>]*>.*?</\1>', '', 'Singleline')
$projectCopy = Join-Path $dest "project.qxw"
Set-Content -Path $projectCopy -Value $stripped -Encoding UTF8 -NoNewline
Write-Host ("Project: {0} ({1} patches stripped)" -f $projectCopy, ([regex]::Matches($xml, '<(Output|Input|Feedback)\b').Count))

if ($NoLaunch) { return }

$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$out = Join-Path $dest "log-$stamp.out.txt"
$err = Join-Path $dest "log-$stamp.err.txt"
$args = @("-d", "-o", $projectCopy, "--api", "--api-port", $ApiPort, "--webui", "--webui-port", $WebUiPort, "--webui-root", $WebUiRoot)
# Input profiles saved/deleted through the API land in the sandbox, never in the user's real
# %UserProfile%\QLC+\InputProfiles (InputOutputMap::userProfileDirectory() honours this variable).
# $env: is process-wide, so restore it right after the child has inherited it - otherwise a
# dev-build-run.ps1 from this same shell would start the LIVE instance on the sandbox folder.
$prevProfileDir = $env:QLCPLUS_USER_INPUTPROFILE_DIR
$env:QLCPLUS_USER_INPUTPROFILE_DIR = Join-Path $dest "InputProfiles"
try {
    $p = Start-Process -FilePath (Join-Path $dest $exeName) -ArgumentList $args -WorkingDirectory $dest `
        -RedirectStandardOutput $out -RedirectStandardError $err -PassThru
} finally {
    if ($null -eq $prevProfileDir) { Remove-Item Env:\QLCPLUS_USER_INPUTPROFILE_DIR -ErrorAction SilentlyContinue }
    else { $env:QLCPLUS_USER_INPUTPROFILE_DIR = $prevProfileDir }
}
Write-Host "Started $exeName pid $($p.Id); log: $out / $err"

$deadline = (Get-Date).AddSeconds(90)
$ready = $false
while ((Get-Date) -lt $deadline) {
    if ($p.HasExited) { throw "Process exited with $($p.ExitCode); see $err" }
    try {
        $c = New-Object Net.Sockets.TcpClient
        $c.Connect("127.0.0.1", $ApiPort); $c.Close(); $ready = $true; break
    } catch { Start-Sleep -Milliseconds 500 }
}
if (-not $ready) { throw "API port $ApiPort not accepting connections after 90 s; see $err" }
Write-Host "Ready: web UI http://localhost:$WebUiPort/  API ws://localhost:$ApiPort/"

exit 0
