<#
.SYNOPSIS
  One-shot Control API WebSocket client for a running qlcplus5.exe.

.DESCRIPTION
  Sends a single JSON-RPC-style request to the Control API (see
  docs/api-spec/qlcplus-api.yaml) over a plain WebSocket, waits for the
  matching {type:"response", id:...} reply (or an ErrorResponse), prints
  the raw JSON, and exits. Built for repeated single-command invocation
  (e.g. from multiple concurrent agents), not as an interactive REPL.

  Every session must send "hello" before anything else is accepted
  (ApiServer::registerSessionMethods, UNAUTHORIZED otherwise) - this
  script always does that automatically first.

  Requires the target instance to have been launched with -Api (see
  dev-build-run.ps1's -Api/-ApiPort switches). Default port 9010 matches
  dev-build-run.ps1's own default.

.PARAMETER Method
  API method name, e.g. "functions.list", "functions.start". Omit to just
  say hello and (optionally) listen for broadcast events.

.PARAMETER ParamsJson
  JSON object string for the request's "params" field. Defaults to "{}".

.PARAMETER RawJson
  Full raw JSON request body (must include type/id/method/params itself).
  Overrides -Method/-ParamsJson when given.

.PARAMETER Uri
  WebSocket URI of the running Control API. Default ws://127.0.0.1:9010.

.PARAMETER TimeoutMs
  How long to wait for the matching response before giving up.

.PARAMETER ListenSeconds
  After receiving the response, keep the socket open and print every
  additional message (broadcast events included) for this many seconds.
  Useful for watching functions.status.changed / core.log while something
  plays.

.PARAMETER Quiet
  Suppress the raw >>/<< line echo; only print the final JSON result (or
  nothing, on timeout, besides the exit code / warning).

.EXAMPLE
  .\dev-ws-client.ps1 -Method functions.list -ParamsJson '{"pathFilter":"Hardstyle/Gobo Spot/Color"}'

.EXAMPLE
  .\dev-ws-client.ps1 -Method functions.start -ParamsJson '{"functionId":"123"}'

.EXAMPLE
  .\dev-ws-client.ps1 -ListenSeconds 10
#>
param(
    [string]$Method,
    [string]$ParamsJson = '{}',
    [string]$RawJson,
    [string]$Uri = 'ws://127.0.0.1:9010',
    [int]$TimeoutMs = 5000,
    [int]$ListenSeconds = 0,
    [switch]$Quiet
)

function New-RequestId {
    return 'ws-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
}

function Send-Text([System.Net.WebSockets.ClientWebSocket]$Socket, [string]$Text) {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    $segment = [System.ArraySegment[byte]]::new($bytes)
    $Socket.SendAsync($segment, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, [System.Threading.CancellationToken]::None).Wait()
    if (-not $Quiet) { Write-Host ">> $Text" -ForegroundColor Cyan }
}

# Reads one full WebSocket message, reassembling fragments across multiple
# ReceiveAsync calls (a 330-preset functions.list reply easily exceeds one
# TCP/WS frame) until EndOfMessage, or returns $null on timeout.
function Receive-FullMessage([System.Net.WebSockets.ClientWebSocket]$Socket, [byte[]]$Buffer, [DateTime]$Deadline) {
    $ms = [System.IO.MemoryStream]::new()
    while ($true) {
        $remainingMs = [int]([Math]::Max(($Deadline - [DateTime]::Now).TotalMilliseconds, 50))
        if ([DateTime]::Now -ge $Deadline) { return $null }
        $readCts = [System.Threading.CancellationTokenSource]::new($remainingMs)
        try {
            $result = $Socket.ReceiveAsync([System.ArraySegment[byte]]::new($Buffer), $readCts.Token).GetAwaiter().GetResult()
        }
        catch [System.OperationCanceledException] {
            return $null
        }
        finally {
            $readCts.Dispose()
        }
        $ms.Write($Buffer, 0, $result.Count)
        if ($result.EndOfMessage) {
            return [System.Text.Encoding]::UTF8.GetString($ms.ToArray())
        }
    }
}

# Reads full messages until one is {type:"response", id:$Id, ...} or the
# timeout elapses. Prints every message seen (including unrelated broadcast
# events) along the way.
function Wait-ForResponse([System.Net.WebSockets.ClientWebSocket]$Socket, [byte[]]$Buffer, [string]$Id, [int]$TimeoutMsLocal) {
    $deadline = [DateTime]::Now.AddMilliseconds([Math]::Max($TimeoutMsLocal, 1))
    while ([DateTime]::Now -lt $deadline) {
        $text = Receive-FullMessage $Socket $Buffer $deadline
        if ($null -eq $text) { break }
        if (-not $Quiet) { Write-Host "<< $text" -ForegroundColor Yellow }
        try {
            $parsed = $text | ConvertFrom-Json
            if ($parsed.type -eq 'response' -and $parsed.id -eq $Id) { return $parsed }
        } catch {}
    }
    return $null
}

$ws = [System.Net.WebSockets.ClientWebSocket]::new()

try {
    $ws.ConnectAsync([Uri]$Uri, [System.Threading.CancellationToken]::None).Wait(5000) | Out-Null
    if ($ws.State -ne [System.Net.WebSockets.WebSocketState]::Open) {
        Write-Error "Failed to connect to $Uri (state: $($ws.State)). Is qlcplus5 running with -Api?"
        exit 1
    }

    $buffer = [byte[]]::new(65536)

    $helloId = New-RequestId
    $helloText = (@{ type = 'request'; id = $helloId; method = 'hello'; params = @{} } | ConvertTo-Json -Compress)
    Send-Text $ws $helloText
    $helloResp = Wait-ForResponse $ws $buffer $helloId $TimeoutMs

    if (-not $helloResp -or $helloResp.ok -ne $true) {
        Write-Error "hello handshake failed or timed out"
        exit 2
    }

    $requestId = $null
    $send = $null
    if ($RawJson) {
        $send = $RawJson
        try { $requestId = ($RawJson | ConvertFrom-Json).id } catch {}
    }
    elseif ($Method) {
        $requestId = New-RequestId
        $envelope = [ordered]@{
            type   = 'request'
            id     = $requestId
            method = $Method
            params = ($ParamsJson | ConvertFrom-Json)
        }
        $send = $envelope | ConvertTo-Json -Depth 20 -Compress
    }

    if ($send) {
        Send-Text $ws $send
        $resp = Wait-ForResponse $ws $buffer $requestId $TimeoutMs
        if (-not $resp) {
            Write-Warning "No matching response for id=$requestId within ${TimeoutMs}ms"
            exit 3
        }
        # Always emit the final result as compact JSON on stdout, even in
        # -Quiet mode - that flag only silences the >>/<< trace lines, it
        # must not swallow the one thing a caller actually wants back.
        Write-Output ($resp | ConvertTo-Json -Depth 20 -Compress)
        if ($resp.ok -ne $true) { exit 4 }
    }

    if ($ListenSeconds -gt 0) {
        $deadline = [DateTime]::Now.AddSeconds($ListenSeconds)
        while ([DateTime]::Now -lt $deadline) {
            $text = Receive-FullMessage $ws $buffer $deadline
            if ($null -eq $text) { break }
            Write-Host "<< $text" -ForegroundColor Yellow
        }
    }
}
finally {
    if ($ws.State -eq [System.Net.WebSockets.WebSocketState]::Open) {
        $ws.CloseAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, 'done', [System.Threading.CancellationToken]::None).Wait(1000) | Out-Null
    }
    $ws.Dispose()
}
