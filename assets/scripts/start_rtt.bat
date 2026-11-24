<# : start_rtt.bat  (batch part, PowerShell reads this block as a comment)
@echo off
setlocal
title RTT Server for VOFA

REM ===========================================================================
REM  SEGGER RTT -> VOFA+ for J-Link, CMSIS-DAP (v1/v2) and ST-Link.
REM  VOFA+ : TCP Client / 127.0.0.1 / 19021 / no handshake / JustFloat
REM
REM  Double click               : start the RTT server. The probe is detected
REM                               automatically and reconnected after unplug.
REM                               With AUTOFLASH=1 a new build output (.axf /
REM                               .elf below this folder, e.g. Keil F7 or EIDE
REM                               Build) is downloaded automatically.
REM  start_rtt.bat flash FILE   : program FILE (.axf/.elf/.hex), e.g. from the
REM                               Keil "Use External Tool for Flash Programming"
REM                               option or an EIDE "Custom" flasher.
REM
REM  Keys in the server window  : P = pause / resume (releases the probe)
REM                               R = reconnect now    Q = quit
REM
REM  This file is a batch + PowerShell hybrid: the lines up to the closing
REM  comment mark below are batch, the rest is PowerShell 5.1 (built into
REM  Windows). ASCII only on purpose: GBK text breaks CMD and PowerShell 5.1.
REM ===========================================================================

REM ---- probe: auto | jlink | cmsis-dap | stlink ----
set PROBE=auto

REM ---- J-Link only: SEGGER device name ----
set DEVICE=STM32G070RB

REM ---- CMSIS-DAP / ST-Link only (OpenOCD) ----
REM  TARGETCFG : OpenOCD target script, e.g. target/stm32f4x.cfg
REM  RAM_START / RAM_SIZE : control block search range, must cover _SEGGER_RTT
set TARGETCFG=target/stm32g0x.cfg
set RAM_START=0x20000000
set RAM_SIZE=0x9000

REM ---- download after each build (CMSIS-DAP / ST-Link only) ----
REM  AUTOFLASH : 1 = download a new .axf / .elf as soon as a build writes it
REM              (through the running server, RTT goes on), 0 = off
REM  FIRMWARE  : empty = watch every *.axf / *.elf below this folder,
REM              or one file to watch (absolute or relative to this folder)
set AUTOFLASH=1
set FIRMWARE=

REM ---- common ----
REM  RESET : 0 = attach to the running firmware (default, no restart on
REM              reconnect), 1 = reset the target each time the probe connects
set SPEED=4000
set RTTPORT=19021
set RESET=0

REM ---- tool paths, leave empty to search automatically ----
set JLINK=
set OPENOCD=

set "RTT_SELF=%~f0"
set "RTT_MODE=%~1"
set "RTT_FILE=%~2"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -Command "iex ([IO.File]::ReadAllText($env:RTT_SELF))"
set RC=%errorlevel%
if /i "%RTT_MODE%"=="flash" exit /b %RC%
if not "%RC%"=="0" pause
exit /b %RC%
#>

# ===========================================================================
#  PowerShell part. Keep it PowerShell 5.1 compatible and ASCII only.
# ===========================================================================
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

# CM_Locate_DevNode (flag 0 = present devices only) costs ~0.1 ms, a WMI
# query ~200 ms and would stall the relay; WMI is only the fallback.
# CreateFile is used to see whether another program holds the probe's HID
# (CMSIS-DAP v1) interface.
$HaveWin32 = $false
try {
    Add-Type -Namespace RttVofa -Name Win32 -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("cfgmgr32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern int CM_Locate_DevNodeW(out uint devInst, string deviceId, uint flags);
[System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode, SetLastError = true)]
public static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFileW(string name, uint access, uint share, System.IntPtr sa, uint disposition, uint flags, System.IntPtr template);
'@
    $HaveWin32 = $true
} catch {}
$GenericRW = [uint32]3221225472     # GENERIC_READ | GENERIC_WRITE
$HidGuid   = '{4d1e55b2-f16f-11cf-88cb-001111000030}'

$ProbeWant   = ('' + $env:PROBE).Trim().ToLower()
if (-not $ProbeWant) { $ProbeWant = 'auto' }
$Device      = $env:DEVICE
$TargetCfg   = $env:TARGETCFG
$RamStart    = $env:RAM_START
$RamSize     = $env:RAM_SIZE
$Speed       = [int]$env:SPEED
$RttPort     = [int]$env:RTTPORT
$DoReset     = ($env:RESET -eq '1')
$AutoFlash   = ($env:AUTOFLASH -ne '0')
$Firmware    = ('' + $env:FIRMWARE).Trim()
$ScriptDir   = Split-Path -Parent $env:RTT_SELF
# The backend (J-Link / OpenOCD) serves RTT on an internal port. This script
# owns RTTPORT and relays, so VOFA+ stays connected while the backend restarts.
$BackendPort = $RttPort + 10
$TclPort     = 6666     # OpenOCD default Tcl RPC port, used for downloads
$StLinkPids  = '3744|3748|374B|374D|374E|374F|3752|3753|3754|3757'

$Tmp    = Join-Path $env:TEMP ('start_rtt_' + $PID)
$LogOut = $Tmp + '.out.log'
$LogErr = $Tmp + '.err.log'
$NulIn  = $Tmp + '.in'
$JCmd   = $Tmp + '.jlink'
$Buf    = New-Object byte[] 65536

$script:Backend  = $null
$script:Up       = $null     # connection to the backend RTT port
$script:Client   = $null     # VOFA+ connection
$script:Tails    = @()
$script:CbState  = 'unknown' # OpenOCD control block: unknown | missing | found
$script:Seen     = @{}       # de-duplicates repeated backend log lines
$script:Conflict = $false    # another host talks to the same CMSIS-DAP
$script:Busy     = $false    # backend could not open the probe
$script:Watchers = @()
$script:Pending  = @{}       # build output path -> time of its last change
# Hash of the image last programmed through this script, shared by the server
# and "flash" runs (Keil F8 is a separate process). Cleared whenever another
# program may have touched the chip, so a stale record never skips a download.
$LastFile = Join-Path $env:TEMP ('start_rtt_' + $RttPort + '.last')

function Say([string]$msg, [string]$color = 'Gray') {
    Write-Host ('[{0:HH:mm:ss}] {1}' -f (Get-Date), $msg) -ForegroundColor $color
}

# ---------------------------------------------------------------- tools ----
function Find-FirstFile([string[]]$paths) {
    foreach ($p in $paths) {
        if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) { return $p }
    }
    return $null
}

function Find-JLink {
    $c = @($env:JLINK)
    foreach ($root in @("$env:ProgramFiles\SEGGER", "${env:ProgramFiles(x86)}\SEGGER")) {
        $c += Get-ChildItem -LiteralPath $root -Directory -Filter 'JLink*' -ErrorAction SilentlyContinue |
              Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'JLink.exe' }
    }
    $c += Join-Path $env:USERPROFILE '.eide\tools\jlink\JLink.exe'
    $cmd = Get-Command JLink.exe -ErrorAction SilentlyContinue
    if ($cmd) { $c += $cmd.Path }
    return Find-FirstFile $c
}

function Find-OpenOcd {
    $c = @($env:OPENOCD)
    $c += Get-ChildItem -LiteralPath (Join-Path $env:USERPROFILE '.eide\tools') -Directory -Filter 'openocd*' -ErrorAction SilentlyContinue |
          Sort-Object LastWriteTime -Descending | ForEach-Object { Join-Path $_.FullName 'bin\openocd.exe' }
    $cmd = Get-Command openocd.exe -ErrorAction SilentlyContinue
    if ($cmd) { $c += $cmd.Path }
    return Find-FirstFile $c
}

function Stop-Stale([string[]]$names) {
    foreach ($n in $names) {
        Get-Process -Name $n -ErrorAction SilentlyContinue | ForEach-Object {
            try { $_.Kill(); [void]$_.WaitForExit(2000) } catch {}
        }
    }
}

# ---------------------------------------------------------------- probe ----
function Get-BusDesc($dev) {
    # A CMSIS-DAP v1 (HID) probe shows up as a generic "USB Input Device";
    # its product string is only available as the bus reported description.
    try {
        $r = $dev.GetDeviceProperties([string[]]@('DEVPKEY_Device_BusReportedDeviceDesc'))
        return '' + $r.deviceProperties[0].Data
    } catch { return '' }
}

function Find-Probe {
    # Get-WmiObject: ~200 ms here, Get-CimInstance ~1 s on PowerShell 5.1
    $devs = @(Get-WmiObject Win32_PnPEntity -Filter "DeviceID LIKE 'USB\\%'" -ErrorAction SilentlyContinue)
    if ($ProbeWant -eq 'auto' -or $ProbeWant -eq 'jlink') {
        $d = $devs | Where-Object { $_.DeviceID -match '^USB\\VID_1366&PID_[0-9A-F]{4}\\' } | Select-Object -First 1
        if ($d) { return @{ Kind = 'jlink'; Id = $d.DeviceID; Name = 'J-Link'; V2 = $false } }
    }
    if ($ProbeWant -eq 'auto' -or $ProbeWant -eq 'stlink') {
        $d = $devs | Where-Object { $_.DeviceID -match "^USB\\VID_0483&PID_($StLinkPids)\\" } | Select-Object -First 1
        if ($d) { return @{ Kind = 'stlink'; Id = $d.DeviceID; Name = 'ST-Link'; V2 = $false } }
    }
    if ($ProbeWant -eq 'auto' -or $ProbeWant -eq 'cmsis-dap') {
        # v2 (WinUSB): the interface node carries the interface string
        $d = $devs | Where-Object { $_.Name -match 'CMSIS-DAP' } | Select-Object -First 1
        if ($d) { return @{ Kind = 'cmsis-dap'; Id = $d.DeviceID; Name = $d.Name; V2 = $true } }
        foreach ($h in ($devs | Where-Object { $_.Service -eq 'HidUsb' })) {
            $desc = Get-BusDesc $h
            if ($desc -match 'CMSIS-DAP') { return @{ Kind = 'cmsis-dap'; Id = $h.DeviceID; Name = $desc; V2 = $false } }
        }
    }
    return $null
}

function Test-Present([string]$id) {
    if ($HaveWin32) {
        $inst = [uint32]0
        return ([RttVofa.Win32]::CM_Locate_DevNodeW([ref]$inst, $id, 0) -eq 0)
    }
    $esc = $id.Replace('\', '\\').Replace("'", "\'")
    $d = Get-WmiObject Win32_PnPEntity -Filter "DeviceID='$esc'" -ErrorAction SilentlyContinue
    return ($null -ne $d)
}

function Get-ProbeHidPaths($probe) {
    # device paths of the probe's HID (CMSIS-DAP v1) interfaces
    if ($probe.Kind -ne 'cmsis-dap' -or -not $HaveWin32) { return @() }
    $vp = [regex]::Match($probe.Id, 'VID_[0-9A-Fa-f]{4}&PID_[0-9A-Fa-f]{4}').Value
    if (-not $vp) { return @() }
    return @(Get-WmiObject Win32_PnPEntity -Filter "DeviceID LIKE 'HID\\$vp%'" -ErrorAction SilentlyContinue |
             ForEach-Object { '\\?\' + $_.DeviceID.Replace('\', '#') + '#' + $HidGuid })
}

function Test-HidPathsFree([string[]]$paths) {
    # $false when another program has one of them open (e.g. an IDE debug
    # session on CMSIS-DAP v1): an exclusive open fails with a sharing violation
    foreach ($path in $paths) {
        $fh = [RttVofa.Win32]::CreateFileW($path, $GenericRW, 0, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
        $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
        if ($fh.IsInvalid) { if ($err -eq 32) { return $false } }
        else { $fh.Close() }
    }
    return $true
}

function Test-ProbeHidFree($probe) {
    # starting OpenOCD next to another host would garble both sessions
    return (Test-HidPathsFree (Get-ProbeHidPaths $probe))
}

function Get-ForeignTool {
    # another debug tool started, e.g. the IDE's own download: "name (pid)" or $null
    $own = -1
    if ($script:Backend) { $own = $script:Backend.Id }
    $p = Get-Process -Name 'openocd', 'pyocd', 'STM32_Programmer_CLI' -ErrorAction SilentlyContinue |
         Where-Object { $_.Id -ne $own } | Select-Object -First 1
    if ($p) { return ('{0} (pid {1})' -f $p.ProcessName, $p.Id) }
    return $null
}

# -------------------------------------------------------------- sockets ----
function Connect-Tcp([int]$port, [int]$timeoutMs) {
    $c = New-Object Net.Sockets.TcpClient
    $iar = $c.BeginConnect('127.0.0.1', $port, $null, $null)
    if (-not $iar.AsyncWaitHandle.WaitOne($timeoutMs)) { $c.Close(); return $null }
    try { $c.EndConnect($iar) } catch { $c.Close(); return $null }
    $c.NoDelay = $true
    return $c
}

function Read-Avail($c) {
    # bytes read into $Buf, 0 = nothing yet, $null = peer closed
    $s = $c.Client
    $avail = $s.Available
    if ($avail -gt 0) {
        $n = $c.GetStream().Read($Buf, 0, [Math]::Min($avail, $Buf.Length))
        if ($n -le 0) { return $null }
        return $n
    }
    if ($s.Poll(0, [Net.Sockets.SelectMode]::SelectRead)) { return $null }
    return 0
}

function Close-Up     { if ($script:Up)     { try { $script:Up.Close() }     catch {} ; $script:Up = $null } }
function Close-Client { if ($script:Client) { try { $script:Client.Close() } catch {} ; $script:Client = $null } }

function Invoke-Tcl([string]$text, [int]$timeoutMs) {
    # OpenOCD Tcl RPC: script terminated by 0x1a, reply terminated by 0x1a
    $c = Connect-Tcp $TclPort 1500
    if (-not $c) { throw "OpenOCD Tcl port $TclPort is not reachable" }
    try {
        $s = $c.GetStream()
        $s.ReadTimeout = $timeoutMs
        $enc = [Text.Encoding]::Default     # OpenOCD opens files with the ANSI code page
        $b = $enc.GetBytes($text)
        $s.Write($b, 0, $b.Length)
        $s.WriteByte(0x1a)
        $ms = New-Object IO.MemoryStream
        while ($true) {
            $x = $s.ReadByte()
            if ($x -lt 0 -or $x -eq 0x1a) { break }
            $ms.WriteByte([byte]$x)
        }
        return $enc.GetString($ms.ToArray())
    } finally { $c.Close() }
}

# -------------------------------------------------------------- backend ----
function Get-OcdIfArgs($probe) {
    if ($probe.Kind -eq 'stlink') { return '-f interface/stlink.cfg -c "transport select hla_swd"' }
    $a = '-f interface/cmsis-dap.cfg'
    # Probe with v1 + v2 interfaces: stay on v2. OpenOCD would otherwise fall
    # back to v1 (HID) when v2 is busy and join a session another program owns.
    if ($probe.V2) { $a += ' -c "cmsis_dap_backend usb_bulk"' }
    return $a + ' -c "transport select swd"'
}

function Read-NewLines($t) {
    if (-not (Test-Path -LiteralPath $t.Path)) { return @() }
    $fs = [IO.File]::Open($t.Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        $len = $fs.Length
        if ($len -le $t.Pos) { return @() }
        [void]$fs.Seek($t.Pos, [IO.SeekOrigin]::Begin)
        $bytes = New-Object byte[] ($len - $t.Pos)
        $n = $fs.Read($bytes, 0, $bytes.Length)
        $t.Pos += $n
    } finally { $fs.Dispose() }
    $parts = ($t.Buf + [Text.Encoding]::Default.GetString($bytes, 0, $n)) -split "`r?`n"
    $t.Buf = $parts[$parts.Count - 1]
    if ($parts.Count -le 1) { return @() }
    return $parts[0..($parts.Count - 2)]
}

function Show-BackendLine([string]$line) {
    if (-not $line.Trim()) { return }
    # "** Programming ... **" progress: the download prints it already
    if ($line -match '^\s*\*\* .* \*\*\s*$') { return }
    if ($line -match 'rtt: Control block found at') {
        $script:CbState = 'found'
        $script:Seen = @{}
        Say ('RTT ' + ($line -replace '^.*rtt: ', '')) 'Green'
        return
    }
    if ($line -match 'rtt: No control block found') { $script:CbState = 'missing' }
    # another host on the same probe: its replies end up here ("mismatch"),
    # or its connect sequence breaks our session ("Polling target ... failed")
    if ($line -match 'CMSIS-DAP command mismatch|Polling target .* failed') { $script:Conflict = $true }
    if ($line -match 'unable to find a matching CMSIS-DAP device|open failed|LIBUSB_ERROR_ACCESS') { $script:Busy = $true }
    # retries repeat the same lines every second; show each one once per 30 s
    $now = [DateTime]::UtcNow
    if ($script:Seen.ContainsKey($line) -and ($now - $script:Seen[$line]).TotalSeconds -lt 30) { return }
    $script:Seen[$line] = $now
    $color = 'DarkGray'
    if ($line -match '^\s*(Error|ERROR)|Cannot connect|failed') { $color = 'Red' }
    elseif ($line -match '^\s*(Warn|WARNING)') { $color = 'Yellow' }
    Write-Host ('    | ' + $line) -ForegroundColor $color
}

function Start-Backend($probe) {
    Set-Content -LiteralPath $NulIn -Value '' -Encoding ASCII
    if ($probe.Kind -eq 'jlink') {
        $exe = Find-JLink
        if (-not $exe) { throw 'JLink.exe not found. Install the J-Link software or set JLINK.' }
        # ExitOnError only around "connect": a failed connect (target not
        # powered, wrong DEVICE) ends Commander and the watchdog retries.
        # "sleep" keeps the connection open, Commander drops it at script end.
        $lines = @('ExitOnError 1', 'connect', 'ExitOnError 0')
        if ($DoReset) { $lines += 'r' }
        $lines += @('g', 'sleep 86400000')
        Set-Content -LiteralPath $JCmd -Value $lines -Encoding ASCII
        $arg = "-device $Device -if SWD -speed $Speed -NoGui 1 -RTTTelnetPort $BackendPort -CommandFile `"$JCmd`""
    } else {
        $exe = Find-OpenOcd
        if (-not $exe) { throw 'openocd.exe not found. Install EIDE (or OpenOCD 0.12+) or set OPENOCD.' }
        # = J-Link "g": resume only a halted core, a running one is left alone
        $start = 'if {[[target current] curstate] eq {halted}} {resume}'
        if ($DoReset) { $start = 'reset run' }
        $arg = (Get-OcdIfArgs $probe) + " -f `"$TargetCfg`" -c `"adapter speed $Speed`" -c init" +
               " -c `"$start`" -c `"sleep 200`"" +
               " -c `"rtt setup $RamStart $RamSize {SEGGER RTT}`" -c `"rtt start`"" +
               " -c `"rtt polling_interval 10`" -c `"rtt server start $BackendPort 0`""
    }
    Remove-Item -LiteralPath $LogOut, $LogErr -ErrorAction SilentlyContinue
    $script:Tails = @(@{ Path = $LogOut; Pos = 0; Buf = '' }, @{ Path = $LogErr; Pos = 0; Buf = '' })
    $script:CbState = 'unknown'
    $script:Seen = @{}
    $script:Conflict = $false
    $script:Busy = $false
    $p = Start-Process -FilePath $exe -ArgumentList $arg -NoNewWindow -PassThru `
         -RedirectStandardInput $NulIn -RedirectStandardOutput $LogOut -RedirectStandardError $LogErr
    $null = $p.Handle   # keeps ExitCode readable after the process ends
    Say ('backend started: {0}  (pid {1})' -f $exe, $p.Id) 'Cyan'
    return $p
}

function Stop-Backend {
    if ($script:Backend) {
        try {
            if (-not $script:Backend.HasExited) { $script:Backend.Kill(); [void]$script:Backend.WaitForExit(3000) }
        } catch {}
        $script:Backend = $null
    }
    Close-Up
}

# ---------------------------------------------------------------- flash ----
function Invoke-TclFlash([string]$full) {
    # Program through a running OpenOCD. RTT is stopped first: the flash
    # algorithm runs in target RAM and may overwrite the control block, which
    # is searched again once the new firmware has booted.
    $tclPath = $full.Replace('\', '/')
    $tcl = "rtt stop`n" +
           "set _rc [catch {capture {program {$tclPath} verify reset}} _msg]`n" +
           "sleep 300`n" +
           "catch {rtt start}`n" +
           "set _out `"`$_rc|`$_msg`""
    try { $res = Invoke-Tcl $tcl 300000 } catch { Say ('flash: ' + $_.Exception.Message) 'Red'; return 1 }
    $i = $res.IndexOf('|')
    if ($i -lt 0) { Write-Host $res; Say 'flash: FAILED' 'Red'; return 1 }
    Write-Host $res.Substring($i + 1).TrimEnd()
    if ($res.Substring(0, $i) -eq '0') { Say 'flash: done, RTT continues' 'Green'; return 0 }
    Say 'flash: FAILED' 'Red'
    return 1
}

function Get-LastImage {
    try { return ([IO.File]::ReadAllText($LastFile)).Trim() } catch { return '' }
}

function Set-LastImage([string]$hash) {
    try {
        if ($hash) { [IO.File]::WriteAllText($LastFile, $hash) }
        else { Remove-Item -LiteralPath $LastFile -ErrorAction SilentlyContinue }
    } catch {}
}

function Save-FlashResult([string]$full, [int]$rc) {
    if ($rc -eq 0) { Set-LastImage (Get-ImageHash $full) } else { Set-LastImage '' }
}

function Invoke-Flash([string]$file) {
    if (-not $file -or -not (Test-Path -LiteralPath $file -PathType Leaf)) {
        Say "flash: file not found: $file" 'Red'; return 1
    }
    $full = (Resolve-Path -LiteralPath $file).ProviderPath

    # 1) start_rtt.bat is running with OpenOCD: program through it
    $c = Connect-Tcp $TclPort 1500
    if ($c) {
        $c.Close()
        Say "flash: through the running RTT server: $full" 'Cyan'
        $rc = Invoke-TclFlash $full
        Save-FlashResult $full $rc
        return $rc
    }

    # 2) no server running: plain OpenOCD run
    $probe = Find-Probe
    if (-not $probe) { Say 'flash: no probe found' 'Red'; return 1 }
    if ($probe.Kind -eq 'jlink') {
        Say "flash: J-Link found. Download from the IDE as usual, it works while start_rtt.bat runs." 'Yellow'
        return 1
    }
    if (-not (Test-ProbeHidFree $probe)) { Say 'flash: the probe is in use by another program' 'Red'; return 1 }
    $ocd = Find-OpenOcd
    if (-not $ocd) { Say 'flash: openocd.exe not found' 'Red'; return 1 }
    Say "flash: OpenOCD ($($probe.Kind)): $full" 'Cyan'
    $arg = (Get-OcdIfArgs $probe) + " -f `"$TargetCfg`" -c `"adapter speed $Speed`" -c `"program {$($full.Replace('\', '/'))} verify reset exit`""
    $p = Start-Process -FilePath $ocd -ArgumentList $arg -NoNewWindow -PassThru -Wait
    Save-FlashResult $full $p.ExitCode
    return $p.ExitCode
}

# ------------------------------------------------------------ autoflash ----
# Change notifications only tell which build outputs exist: Windows reports
# size / last-write changes when its cache is flushed, often ~10 s late. The
# candidate files are polled directly instead (a stat costs microseconds).
$script:Cand = @{}           # build output path -> "write ticks|length" last seen

function Get-FileSig([string]$path) {
    $fi = New-Object IO.FileInfo $path
    if (-not $fi.Exists) { return $null }
    return ('{0}|{1}' -f $fi.LastWriteTimeUtc.Ticks, $fi.Length)
}

function Test-OutputName([string]$path) {
    # only linker outputs; the watcher filter alone also matches 8.3 names
    if ($Firmware) { return $true }
    $ext = [IO.Path]::GetExtension($path)
    return ($ext -eq '.axf' -or $ext -eq '.elf')
}

function Find-BuildOutputs {
    # Where-Object, not -Include: -Include is ignored with -LiteralPath on 5.1
    return @(Get-ChildItem -LiteralPath $ScriptDir -Recurse -File -ErrorAction SilentlyContinue |
             Where-Object { $_.Extension -eq '.axf' -or $_.Extension -eq '.elf' })
}

function Add-Candidate([string]$path) {
    # a new file has no signature yet, so its first poll counts as a change
    if ((Test-OutputName $path) -and -not $script:Cand.ContainsKey($path)) { $script:Cand[$path] = '' }
}

function New-Watcher([string]$dir, [string]$filter, [bool]$sub) {
    $w = New-Object IO.FileSystemWatcher $dir, $filter
    $w.IncludeSubdirectories = $sub
    $w.InternalBufferSize = 65536
    $w.NotifyFilter = [IO.NotifyFilters]'FileName, LastWrite'
    # no -Action: events wait in the queue and the main loop pulls them
    foreach ($ev in @('Changed', 'Created', 'Renamed', 'Error')) {
        [void](Register-ObjectEvent -InputObject $w -EventName $ev -SourceIdentifier ('rttfw.' + [guid]::NewGuid()))
    }
    $w.EnableRaisingEvents = $true
    return $w
}

function Start-FirmwareWatch {
    if ($Firmware) {
        $full = [IO.Path]::GetFullPath([IO.Path]::Combine($ScriptDir, $Firmware))
        $dir = Split-Path -Parent $full
        if (-not (Test-Path -LiteralPath $dir -PathType Container)) { Say "autoflash: folder not found: $dir" 'Red'; return }
        $script:Watchers += New-Watcher $dir (Split-Path -Leaf $full) $false
        $script:Cand[$full] = Get-FileSig $full     # existing output = baseline, not downloaded
        Say "autoflash: on, watching $full"
    } else {
        foreach ($pat in @('*.axf', '*.elf')) { $script:Watchers += New-Watcher $ScriptDir $pat $true }
        Find-BuildOutputs | ForEach-Object { $script:Cand[$_.FullName] = Get-FileSig $_.FullName }
        Say ('autoflash: on, watching *.axf / *.elf below {0} ({1} found)' -f $ScriptDir, $script:Cand.Count)
    }
}

function Stop-FirmwareWatch {
    foreach ($w in $script:Watchers) { try { $w.EnableRaisingEvents = $false; $w.Dispose() } catch {} }
    Get-EventSubscriber -ErrorAction SilentlyContinue | Where-Object { $_.SourceIdentifier -like 'rttfw.*' } |
        ForEach-Object { Unregister-Event -SubscriptionId $_.SubscriptionId -ErrorAction SilentlyContinue }
    $script:Watchers = @()
}

function Add-RecentFirmware {
    # watcher buffer overflowed during a big build: pick up recent outputs
    if ($Firmware) { return }   # the single watched file is a candidate already
    $since = (Get-Date).AddSeconds(-60)
    Find-BuildOutputs | Where-Object { $_.LastWriteTime -gt $since } | ForEach-Object { Add-Candidate $_.FullName }
}

function Get-SettledFirmware([long]$now) {
    # newest build output that has not changed for 1 s, else $null
    foreach ($e in @(Get-Event -ErrorAction SilentlyContinue)) {
        if ($e.SourceIdentifier -like 'rttfw.*') {
            $a = $e.SourceEventArgs
            if ($a -is [IO.FileSystemEventArgs]) { Add-Candidate $a.FullPath }
            elseif ($a -is [IO.ErrorEventArgs]) { Add-RecentFirmware }
        }
        Remove-Event -EventIdentifier $e.EventIdentifier -ErrorAction SilentlyContinue
    }
    foreach ($k in @($script:Cand.Keys)) {
        $sig = Get-FileSig $k
        if ($sig -and $sig -ne $script:Cand[$k]) { $script:Cand[$k] = $sig; $script:Pending[$k] = $now }
    }
    if ($script:Pending.Count -eq 0) { return $null }
    $newest = $null; $newestT = -1
    foreach ($k in @($script:Pending.Keys)) {
        $t = $script:Pending[$k]
        if ($now - $t -lt 1000) { return $null }     # the build is still writing
        if ($t -gt $newestT) { $newest = $k; $newestT = $t }
    }
    $script:Pending.Clear()
    return $newest
}

function Get-ImageHash([string]$path) {
    # $null while a writer (the linker) still has the file open
    try { $fs = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read) }
    catch { return $null }
    try { return [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($fs)) }
    finally { $fs.Dispose() }
}

# --------------------------------------------------------------- server ----
function Run-Server {
    Say "VOFA+ : TCP Client / 127.0.0.1 / $RttPort / no handshake / JustFloat" 'Green'
    Say 'Keys  : P = pause / resume (release the probe)   R = reconnect   Q = quit'
    # old single-tool scripts and stale backends would hold the probe or ports
    Stop-Stale @('JLink', 'JLinkGDBServerCL', 'JLinkGDBServer', 'openocd')

    $listener = New-Object Net.Sockets.TcpListener ([Net.IPAddress]::Loopback), $RttPort
    $listener.ExclusiveAddressUse = $true
    try { $listener.Start() } catch {
        Say "Port $RttPort is busy (another start_rtt.bat, RTT Viewer, J-Scope or Ozone?)" 'Red'
        return 1
    }
    if ($AutoFlash) { try { Start-FirmwareWatch } catch { Say ('autoflash: ' + $_.Exception.Message) 'Red' } }
    else { Say 'autoflash: off' }
    Set-LastImage ''    # what is on the chip now is unknown: the first build downloads

    $keys = $true
    try { [void][Console]::KeyAvailable } catch { $keys = $false }

    $sw = [Diagnostics.Stopwatch]::StartNew()
    $state = 'search'           # search | run | wait | pause
    $probe = $null
    $nextProbe = 0; $nextWatch = 0; $nextTail = 0; $nextUp = 0; $nextCb = 0; $nextTitle = 0; $nextFw = 0; $retryAt = 0
    $nextForeign = 0; $nextYield = 0; $freeSince = -1; $hidPaths = @(); $foreign = $null
    $told = $false; $toldBusy = $false; $bytes = 0
    try {
        while ($true) {
            $now = $sw.ElapsedMilliseconds
            $busy = $false

            # ---- VOFA+ side: a new connection replaces the old one ----
            if ($listener.Pending()) {
                Close-Client
                $script:Client = $listener.AcceptTcpClient()
                $script:Client.NoDelay = $true
                $script:Client.SendTimeout = 2000
                Say 'VOFA+ connected' 'Green'
            }

            # ---- backend state machine ----
            if ($state -eq 'search' -and $now -ge $nextProbe) {
                $nextProbe = $now + 1500
                $probe = Find-Probe
                if ($probe -and -not (Test-ProbeHidFree $probe)) {
                    if (-not $toldBusy) { Say 'probe is in use by another program (IDE debug session?), waiting ...' 'Yellow'; $toldBusy = $true }
                } elseif ($probe) {
                    Say ('probe: {0}  [{1}]' -f $probe.Name, $probe.Kind) 'Green'
                    try { $script:Backend = Start-Backend $probe } catch { Say $_.Exception.Message 'Red'; return 1 }
                    $state = 'run'; $told = $false
                    $nextWatch = $now + 1500; $nextUp = $now + 300; $nextCb = $now + 2500
                } elseif (-not $told) {
                    Say 'waiting for a probe (J-Link / CMSIS-DAP / ST-Link) ...' 'Yellow'
                    $told = $true
                }
            }
            elseif ($state -eq 'run') {
                if (-not $script:Up -and $now -ge $nextUp) {
                    $script:Up = Connect-Tcp $BackendPort 200
                    if ($script:Up) { Say 'RTT stream connected' 'Green'; $toldBusy = $false } else { $nextUp = $now + 500 }
                }
                # J-Link shares the probe between programs by itself; CMSIS-DAP /
                # ST-Link do not, so step back when another tool shows up
                if ($probe.Kind -ne 'jlink' -and $now -ge $nextForeign) {
                    $nextForeign = $now + 100   # Get-Process costs a few ms
                    $foreign = Get-ForeignTool
                }
                if ($probe.Kind -ne 'jlink' -and ($script:Conflict -or $foreign)) {
                    $who = 'another program'
                    if ($foreign) { $who = $foreign }
                    Stop-Backend
                    $hidPaths = Get-ProbeHidPaths $probe
                    $state = 'yield'; $freeSince = -1; $foreign = $null; $script:Conflict = $false
                    Set-LastImage ''    # the other tool may program the chip
                    Say "$who is using the probe: RTT paused and probe released, resuming when it is done" 'Yellow'
                }
                elseif ($now -ge $nextWatch) {
                    $nextWatch = $now + 1500
                    if (-not (Test-Present $probe.Id)) {
                        Say 'probe unplugged, waiting for it to come back ...' 'Yellow'
                        Stop-Backend; $state = 'search'; $told = $true; $nextProbe = $now + 1500
                        Set-LastImage ''
                    } elseif ($script:Backend.HasExited) {
                        if ($script:Busy) {
                            if (-not $toldBusy) { Say 'probe is in use by another program (IDE debug session?), retrying ...' 'Yellow'; $toldBusy = $true }
                        } else {
                            Say ('backend exited (code {0}), retrying in 1 s' -f $script:Backend.ExitCode) 'Yellow'
                        }
                        Stop-Backend; $state = 'wait'; $retryAt = $now + 1000
                    }
                }
                # OpenOCD searches for the control block only once per "rtt start"
                if ($state -eq 'run' -and $probe.Kind -ne 'jlink' -and $script:CbState -ne 'found' -and $now -ge $nextCb) {
                    $nextCb = $now + 1000
                    try { [void](Invoke-Tcl 'rtt start' 5000) } catch {}
                }
            }
            elseif ($state -eq 'wait' -and $now -ge $retryAt) {
                $state = 'search'; $nextProbe = 0; $told = $true
            }
            elseif ($state -eq 'yield' -and $now -ge $nextYield) {
                # resume once no other tool runs and nobody holds the HID interface for 2 s
                $nextYield = $now + 500
                $free = (-not (Get-ForeignTool)) -and (Test-HidPathsFree $hidPaths)
                if (-not $free) { $freeSince = -1 }
                elseif ($freeSince -lt 0) { $freeSince = $now }
                elseif ($now - $freeSince -ge 2000) {
                    Say 'probe is free again, resuming' 'Green'
                    $state = 'search'; $nextProbe = 0; $told = $false; $toldBusy = $false
                }
            }

            # ---- download a fresh build output ----
            if ($script:Watchers.Count -gt 0 -and $now -ge $nextFw) {
                $nextFw = $now + 300
                $fw = Get-SettledFirmware $now
                if ($fw -and (Test-Path -LiteralPath $fw -PathType Leaf)) {
                    $rel = $fw
                    if ($fw.StartsWith($ScriptDir, [StringComparison]::OrdinalIgnoreCase)) { $rel = $fw.Substring($ScriptDir.Length).TrimStart('\') }
                    $hash = Get-ImageHash $fw
                    if (-not $hash) {
                        $script:Pending[$fw] = $now
                    } elseif ($hash -eq (Get-LastImage)) {
                        Say "autoflash: $rel is already on the target, skipped" 'DarkGray'
                    } elseif ($state -eq 'run' -and $probe.Kind -eq 'jlink') {
                        Say "autoflash: $rel changed. J-Link: download from the IDE as usual." 'DarkGray'
                    } elseif ($state -ne 'run') {
                        Say "autoflash: $rel changed, not downloaded (server is $state)" 'Yellow'
                    } else {
                        Say "autoflash: $rel changed, downloading ..." 'Cyan'
                        if ((Invoke-TclFlash $fw) -eq 0) { Set-LastImage $hash } else { Set-LastImage '' }
                        $now = $sw.ElapsedMilliseconds
                    }
                }
            }

            # ---- relay: backend -> VOFA+ ----
            if ($script:Up) {
                try { $n = Read-Avail $script:Up } catch { $n = $null }
                if ($null -eq $n) {
                    # OpenOCD 0.12 on Windows drops its RTT link now and then
                    # ("error during read"). Data waits in the target buffer,
                    # so reconnecting quickly loses nothing.
                    Close-Up; $nextUp = $now + 100
                    Say 'RTT stream closed, reconnecting' 'DarkGray'
                } elseif ($n -gt 0) {
                    $busy = $true; $bytes += $n
                    if ($script:Client) {
                        try { $script:Client.GetStream().Write($Buf, 0, $n) } catch { Close-Client; Say 'VOFA+ disconnected' 'Yellow' }
                    }
                }
            }
            # ---- relay: VOFA+ -> backend (RTT down channel) ----
            if ($script:Client) {
                try { $n = Read-Avail $script:Client } catch { $n = $null }
                if ($null -eq $n) {
                    Close-Client; Say 'VOFA+ disconnected' 'Yellow'
                } elseif ($n -gt 0) {
                    $busy = $true
                    if ($script:Up) { try { $script:Up.GetStream().Write($Buf, 0, $n) } catch {} }
                }
            }

            # ---- backend output ----
            if ($now -ge $nextTail) {
                $nextTail = $now + 200
                foreach ($t in $script:Tails) { foreach ($line in (Read-NewLines $t)) { Show-BackendLine $line } }
            }

            # ---- status in the window title ----
            if ($now -ge $nextTitle) {
                $nextTitle = $now + 1000
                $kind = '-'; if ($probe) { $kind = $probe.Kind }
                $vofa = 'VOFA+ off'; if ($script:Client) { $vofa = 'VOFA+ on' }
                try { $host.UI.RawUI.WindowTitle = ('RTT {0} | {1} | {2} | {3} | {4:N1} KB/s' -f $RttPort, $state, $kind, $vofa, ($bytes / 1024.0)) } catch {}
                $bytes = 0
            }

            # ---- keys ----
            if ($keys -and [Console]::KeyAvailable) {
                $k = '' + [Console]::ReadKey($true).Key
                if ($k -eq 'P') {
                    if ($state -eq 'pause') {
                        Say 'resumed' 'Green'; $state = 'search'; $nextProbe = 0; $told = $false; $toldBusy = $false
                    } else {
                        Stop-Backend; $state = 'pause'
                        Set-LastImage ''    # a debug session downloads on its own
                        Say 'paused: probe released (Keil debug etc. can use it now). Press P to resume.' 'Yellow'
                    }
                } elseif ($k -eq 'R' -and $state -ne 'pause') {
                    Say 'reconnecting ...' 'Yellow'
                    Stop-Backend; $state = 'search'; $nextProbe = 0; $told = $false; $toldBusy = $false
                } elseif ($k -eq 'Q') {
                    return 0
                }
            }

            if (-not $busy) { Start-Sleep -Milliseconds 5 }
        }
    } finally {
        Stop-FirmwareWatch
        Stop-Backend
        Close-Client
        $listener.Stop()
        Remove-Item -LiteralPath $LogOut, $LogErr, $NulIn, $JCmd -ErrorAction SilentlyContinue
    }
}

# ----------------------------------------------------------------- main ----
try {
    if (('' + $env:RTT_MODE).ToLower() -eq 'flash') { $rc = Invoke-Flash $env:RTT_FILE }
    else { $rc = Run-Server }
} catch {
    Say ('ERROR: ' + $_.Exception.Message) 'Red'
    $rc = 1
}
exit $rc
