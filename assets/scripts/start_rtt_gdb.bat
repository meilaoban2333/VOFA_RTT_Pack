@echo off
title J-Link GDB Server RTT for VOFA

REM ===========================================================================
REM  Start J-Link GDB Server, RTT telnet server listens on 127.0.0.1:19021
REM  VOFA+ : TCP Client / 127.0.0.1 / 19021 / no handshake / JustFloat
REM
REM  Why this is a TWO STAGE script:
REM  GDB Server has no option to resume a halted core on its own. Per SEGGER
REM  docs, -nohalt only means "do not halt the core when connecting"; it will
REM  NOT start a core that is already stopped. A core left halted (e.g. after
REM  a flash download or a previous debug session) therefore never reaches
REM  main() and the RTT buffer stays empty forever.
REM
REM  Stage 1 uses Commander to reset and "g" (go) the core, then exits.
REM  Stage 2 attaches GDB Server with -nohalt so the running core is left
REM  alone while RTT data is pumped to port 19021.
REM
REM  ASCII only on purpose. GBK comments broke CMD parsing before.
REM ===========================================================================

REM ---- target params ----
set DEVICE=STM32G070RB
set INTERFACE=SWD
set SPEED=4000
set RTTPORT=19021
set GDBPORT=2331

REM ---- locate the SEGGER install dir (edit SEGGERDIR if installed elsewhere) ----
set SEGGERDIR=C:\Program Files\SEGGER\JLink_V864
if exist "%SEGGERDIR%\JLinkGDBServerCL.exe" goto FOUND

REM fallback: any JLink_V* under Program Files\SEGGER, newest first
for /f "delims=" %%V in ('dir /b /ad /o-n "C:\Program Files\SEGGER\JLink_V*" 2^>nul') do call :TRY "C:\Program Files\SEGGER\%%V"
if exist "%SEGGERDIR%\JLinkGDBServerCL.exe" goto FOUND

echo.
echo  [ERROR] JLinkGDBServerCL.exe not found.
echo  Edit the SEGGERDIR line in this script to your real install path.
echo.
pause
exit /b 1

:TRY
if not exist "%SEGGERDIR%\JLinkGDBServerCL.exe" if exist "%~1\JLinkGDBServerCL.exe" set SEGGERDIR=%~1
goto :eof

:FOUND
set GDBSRV=%SEGGERDIR%\JLinkGDBServerCL.exe
set CMDR=%SEGGERDIR%\JLink.exe

echo.
echo  ================================================================
echo   SEGGER : %SEGGERDIR%
echo   Device : %DEVICE%   Interface: %INTERFACE%   Speed: %SPEED% kHz
echo   RTT    : 127.0.0.1:%RTTPORT%    GDB: %GDBPORT%
echo  ================================================================
echo.

REM ---- kill stale J-Link processes, they would keep holding the ports ----
taskkill /f /im JLinkGDBServerCL.exe >nul 2>&1
taskkill /f /im JLinkGDBServer.exe   >nul 2>&1
taskkill /f /im JLink.exe            >nul 2>&1
ping -n 2 127.0.0.1 >nul 2>&1

REM ===========================  STAGE 1  =====================================
REM  Reset the core and let it run, then disconnect so the J-Link is free.
REM  "exit" IS wanted here: Commander must release the probe for stage 2.
echo  [1/2] Releasing the core via Commander (reset + go) ...
set CMDFILE=%TEMP%\jlink_go_%RANDOM%.jlink
> "%CMDFILE%" echo connect
>>"%CMDFILE%" echo g
>>"%CMDFILE%" echo exit
"%CMDR%" -device %DEVICE% -if %INTERFACE% -speed %SPEED% -CommandFile "%CMDFILE%" >nul 2>&1
del "%CMDFILE%" >nul 2>&1
ping -n 2 127.0.0.1 >nul 2>&1
echo        core is running.
echo.

REM ===========================  STAGE 2  =====================================
echo  [2/2] Starting GDB Server (RTT only, core left running) ...
echo.
echo  Open VOFA+ :
echo     Data source = TCP Client   IP = 127.0.0.1   Port = %RTTPORT%
echo     Handshake   = none         Protocol = JustFloat
echo.
echo  "Waiting for GDB connection" is normal and expected. RTT already
echo  streams at that point, you do NOT need to attach any GDB client.
echo  Do not attach Keil / Ozone while streaming: a GDB client halts
echo  the core and RTT stops.
echo.
echo  If port %RTTPORT% was busy, check the banner below, or run:
echo      netstat -ano ^| findstr LISTENING ^| findstr 1902
echo.
echo  Close this window to stop the server.
echo  ================================================================
echo.

REM  -nohalt  : do not halt the core on connect, do not init CPU registers
REM  -noreset : do not reset on connect, stage 1 already did that
REM  -nogui   : no configuration dialog
"%GDBSRV%" -device %DEVICE% -if %INTERFACE% -speed %SPEED% -rtttelnetport %RTTPORT% -port %GDBPORT% -nogui -nohalt -noreset

echo.
echo  J-Link GDB Server exited.
pause
