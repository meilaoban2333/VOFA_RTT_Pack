@echo off
title J-Link RTT Server for VOFA

REM ===========================================================================
REM  Start J-Link Commander, RTT telnet server listens on 127.0.0.1:19021
REM  VOFA+ : TCP Client / 127.0.0.1 / 19021 / no handshake / FireWater
REM
REM  ASCII only on purpose. GBK comments broke CMD parsing before.
REM ===========================================================================

REM ---- target params ----
REM  CHANGE THIS to your own MCU. Use the SEGGER device name
REM  (usually the part number without package/temp suffix).
set DEVICE=STM32F407VE
set INTERFACE=SWD
set SPEED=4000
set RTTPORT=19021

REM ---- locate JLink.exe (edit JLINK below if installed elsewhere) ----
set JLINK=C:\Program Files\SEGGER\JLink_V864\JLink.exe
if exist "%JLINK%" goto FOUND

REM fallback 1: any JLink_V* under Program Files\SEGGER, newest first
for /f "delims=" %%V in ('dir /b /ad /o-n "C:\Program Files\SEGGER\JLink_V*" 2^>nul') do call :TRY "C:\Program Files\SEGGER\%%V\JLink.exe"
if exist "%JLINK%" goto FOUND

REM fallback 2: PATH lookup
for %%P in (JLink.exe) do if exist "%%~$PATH:P" set JLINK=%%~$PATH:P
if exist "%JLINK%" goto FOUND

echo.
echo  [ERROR] JLink.exe not found.
echo  Edit the JLINK line in this script to your real install path.
echo.
pause
exit /b 1

:TRY
if not exist "%JLINK%" if exist %1 set JLINK=%~1
goto :eof

:FOUND
echo.
echo  ================================================================
echo   J-Link : %JLINK%
echo   Device : %DEVICE%   Interface: %INTERFACE%   Speed: %SPEED% kHz
echo   RTT    : 127.0.0.1:%RTTPORT%
echo  ================================================================
echo.

REM ---- kill stale J-Link, it would keep holding port 19021 ----
taskkill /f /im JLink.exe >nul 2>&1
ping -n 2 127.0.0.1 >nul 2>&1

echo  After "Connected", open VOFA+ :
echo     Data source = TCP Client   IP = 127.0.0.1   Port = %RTTPORT%
echo     Handshake   = none         Protocol = FireWater
echo.
echo  If port 19021 was busy, J-Link falls back to 19022 / 19023 ...
echo  Check the J-Link banner below, or run in another cmd:
echo      netstat -ano ^| findstr LISTENING ^| findstr 1902
echo.
echo  No waveform? The core may be halted. Type  g  at the J-Link^> prompt.
echo.
echo  Close this window to stop the server.
echo  ================================================================
echo.

REM ---- build the command file that connects and releases the core ----
REM  Commander halts the core at its prompt after connecting, so main()
REM  never runs and no RTT data appears. "g" (go) resumes execution.
REM  There is no -NoReset command line option in Commander; the reset
REM  behaviour has to be handled from a command file like this one.
REM  The trailing "sleep" keeps the script IN PROGRESS. Without it
REM  Commander finishes the script, DROPS THE TARGET CONNECTION and
REM  falls back to an idle prompt ("Type connect to establish ..."),
REM  so RTT data stops flowing even though the telnet port stays open.
REM  86400000 ms = 24 h. Close the window to stop the server.
set CMDFILE=%TEMP%\jlink_rtt_%RANDOM%.jlink
> "%CMDFILE%" echo connect
>>"%CMDFILE%" echo g
>>"%CMDFILE%" echo sleep 86400000

"%JLINK%" -device %DEVICE% -if %INTERFACE% -speed %SPEED% -CommandFile "%CMDFILE%"

del "%CMDFILE%" >nul 2>&1

echo.
echo  J-Link exited.
pause
