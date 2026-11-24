# VOFA_RTT_Pack

[中文](README.md) | **English**

A self-contained porting pack that streams real-time waveforms to VOFA+ over SEGGER RTT. MCU variables
reach VOFA+ through a J-Link, CMSIS-DAP (DAPLink, v1/v2) or ST-Link probe. No UART is used, and when the
buffer is full new frames are dropped instead of blocking the main loop. Switching probes needs no
firmware change.

The host side is a single script, `start_rtt.bat`. Double-click it and:

- **The probe is detected automatically.** After the probe is unplugged and plugged back in, the script
  reconnects on its own and VOFA+ stays connected.
- **Build and download in one step.** Press F7 in Keil or Build in EIDE. About 2 s after the build the
  script downloads the new firmware and the waveform keeps streaming.
- **The IDE's own download button still works.** As soon as another download tool starts, the script
  releases the probe, and it takes the probe back once that tool is done.

## Requirements

| Item | Requirement | Notes |
|------|-------------|-------|
| OS | Windows 10 / 11 | Uses cmd and the built-in Windows PowerShell 5.1. **No admin rights needed** |
| Host app | VOFA+ | TCP client with the JustFloat protocol |
| J-Link users | SEGGER J-Link Software (tested with V8.64) or the J-Link bundled with EIDE | The script needs `JLink.exe` |
| CMSIS-DAP / ST-Link users | OpenOCD 0.12 or later | The 0.12.0-rc2 bundled with EIDE is tested, so EIDE users need nothing else. Any other OpenOCD works if it is on PATH or set in `OPENOCD` |
| Probe drivers | CMSIS-DAP v2: WinUSB. CMSIS-DAP v1: HID, no driver needed. ST-Link: ST's ST-LINK USB driver (STSW-LINK009). J-Link: installed with the J-Link software | Most CMSIS-DAP v2 probes get WinUSB automatically. If Device Manager shows no driver, install WinUSB with Zadig |
| Build tools | Any: Keil MDK (AC5/AC6), EIDE (GCC/AC6), CMake / Makefile + arm-none-eabi-gcc | The firmware side is plain C |
| Not needed | Python, pyOCD, extra PowerShell modules | |
| Ports | 19021 (for VOFA+), 19031 (internal), OpenOCD's 3333 / 4444 / 6666 | Change `RTTPORT` if 19021 is taken |

## Verification status

| Setup | Status |
|-------|--------|
| STM32G070RB + Keil AC6 + J-Link V864 (previous single-tool script) | Tested |
| STM32G070RB + Horco CMSIS-DAP v2 + this script | Tested: probe detection; 100 / 200 frames/s with no bad frames; build-and-download (including skipping an unchanged build); yielding to and resuming after another OpenOCD that really programs the chip; `flash` mode; recovery after unplugging the probe and after the backend process is killed; VOFA+ stays connected throughout. Build-and-download was tested by writing out an `.axf` / `.elf` to simulate a build, not with a real Keil / EIDE build |
| J-Link + this script | Not tested |
| ST-Link, CMSIS-DAP with only a v1 (HID) interface + this script | Not tested |
| Keil's own CMSIS-DAP driver pressing F8 while the script runs | Not tested, not recommended, see "Downloading while streaming" |
| Keil `Utilities` external tool, EIDE `Custom` flasher (IDE side) | Not tested inside the IDEs (the command lines were tested the way the IDEs call them) |

## Directory layout

```
VOFA_RTT_Pack/
├── SKILL.md                 # Claude Code skill entry, lets the AI do the port step by step
├── README.md                # Chinese README
├── README.en.md             # This file
├── assets/
│   ├── RTT/                 # The six source files to copy into your project
│   │   ├── SEGGER_RTT.c/.h
│   │   ├── SEGGER_RTT_Conf.h
│   │   ├── SEGGER_RTT_printf.c
│   │   └── vofa_rtt.c/.h    # JustFloat framing layer
│   └── scripts/
│       └── start_rtt.bat    # The only host-side script
└── references/
    └── porting.md           # Keil / EIDE / CMake build and download setup (Chinese)
```

## Two ways to use it

**With Claude**: copy the whole folder into a new project and tell Claude "port RTT following
VOFA_RTT_Pack". It reads `SKILL.md` and copies the files, edits the project configuration and sets the
script parameters. You can also copy it to `.claude/skills/vofa-rtt/` to keep it as a permanent skill.

**By hand**: follow the five steps below. Details are in `references/porting.md` (Chinese).

## Porting in five steps

### 1. Copy the sources

Copy the six files in `assets/RTT/` into your project, for example `User/RTT/` (any path works).

### 2. Add them to the build

- Compile the three .c files: `SEGGER_RTT.c`, `SEGGER_RTT_printf.c`, `vofa_rtt.c`
- Add the RTT folder to the include path

`references/porting.md` has the exact changes for Keil / EIDE / CMake / Makefile, with GUI steps and
snippets you can paste.

### 3. Copy the script and set its parameters

Copy `start_rtt.bat` to the **project root** (build-and-download watches the `.axf` / `.elf` files below
this folder) and edit the `set` lines at the top. The probe is detected automatically, so only the
chip-related values need filling in:

| Parameter | Default | Used by | Meaning |
|-----------|---------|---------|---------|
| `PROBE` | `auto` | All | `auto` checks for J-Link, then ST-Link, then CMSIS-DAP. With several probes plugged in, set `jlink` / `cmsis-dap` / `stlink` |
| `DEVICE` | `STM32G070RB` | J-Link | SEGGER device name, usually the part number without package/temperature suffix. A wrong name means no connection |
| `TARGETCFG` | `target/stm32g0x.cfg` | CMSIS-DAP / ST-Link | OpenOCD target script, e.g. `target/stm32f4x.cfg` |
| `RAM_START` / `RAM_SIZE` | `0x20000000` / `0x9000` | CMSIS-DAP / ST-Link | Search range for the control block, must cover `_SEGGER_RTT`. The default is the 36 KB SRAM of the G070, change it for other chips. Most reliable: put the `_SEGGER_RTT` address from the `.map` file in `RAM_START` and `0x100` in `RAM_SIZE` |
| `AUTOFLASH` | `1` | CMSIS-DAP / ST-Link | 1 = build-and-download on, 0 = off |
| `FIRMWARE` | empty | CMSIS-DAP / ST-Link | Empty = watch every `.axf` / `.elf` below this folder. If the folder holds several projects, give the one file to download (absolute or relative to this folder) |
| `RESET` | `0` | All | 0 = attach to the running firmware (no MCU restart on reconnect); 1 = reset the target every time the probe connects |
| `SPEED` / `RTTPORT` | `4000` / `19021` | All | SWD clock in kHz / the port VOFA+ connects to |
| `JLINK` / `OPENOCD` | empty | All | Tool paths. Empty = search automatically: J-Link in `C:\Program Files\SEGGER\JLink*`, the EIDE bundle, PATH; OpenOCD in the EIDE bundle, PATH |

### 4. Change the code

```c
#include "vofa_rtt.h"

int main(void)
{
    VOFA_RTT_Init();       // must run before peripherals and interrupts start

    HAL_Init();
    /* ... peripheral init ... */

    while (1)
    {
        VOFA_RTT_TestLoop();   // self-test sine wave, replace with real variables once it works
        // VOFA_RTT_Send2(speed, target);
    }
}
```

In a non-HAL project, replace `HAL_GetTick()` in `VOFA_RTT_TestLoop()` (in `vofa_rtt.c`) with your own
millisecond tick, or delete the test function.

**Throttle the sending.** An unthrottled main loop fills the buffer and frames get dropped. Use the tick
throttling in `TestLoop` as a template, or send from a fixed-period timer callback. JustFloat frames are
tiny; anything up to 500 Hz is easy.

### 5. Start the server and configure VOFA+

Double-click `start_rtt.bat`. It is ready once it prints `probe: ...` and `RTT stream connected` (with
CMSIS-DAP / ST-Link also `RTT Control block found at 0x...`). The window title shows the state and the
data rate (KB/s).

VOFA+ settings:

| Setting | Value |
|---------|-------|
| Data interface | TCP client |
| Server IP | 127.0.0.1 |
| Port | 19021 |
| Handshake data | empty |
| Data format | **JustFloat** |

A 1 Hz sine and cosine mean the link works. After that, just leave the script window open:

- **Probe unplugged and plugged back in**: reconnects on its own, no need to reconnect in VOFA+ (tested).
  Power-cycling only the target board is not tested; press `R` if it does not recover.
- Keys in the window: `P` pauses and releases the probe (before debugging in Keil), `P` again resumes;
  `R` reconnects now; `Q` quits.

## Downloading while streaming

| Method | How | Probes | Status |
|--------|-----|--------|--------|
| **Build-and-download (recommended)** | Nothing to configure. Press F7 in Keil or Build in EIDE; about 2 s after a successful build the script downloads and the waveform keeps streaming | CMSIS-DAP / ST-Link | Tested with simulated build output |
| The IDE's download button | Click download in EIDE as usual: when an `openocd` / `pyocd` process appears the script releases the probe, and takes it back 2 s after the other tool is done | CMSIS-DAP | Tested by simulating with a separate OpenOCD |
| Hand Keil's F8 to the script | `Options for Target` → `Utilities` → `Use External Tool for Flash Programming`; `Command` = full path of `start_rtt.bat`, `Arguments` = `flash "#L"`, leave `Run Independent` unchecked | CMSIS-DAP / ST-Link | Command line tested |
| J-Link | Nothing to change, press F8 as usual. The J-Link software lets several programs share one probe | J-Link | — |

Notes:

- **Build-and-download** only downloads outputs built after the script started. A build identical to the
  last downloaded one (rebuild without changes) is skipped. Nothing is downloaded while the script is
  paused or has yielded the probe. With J-Link nothing is downloaded automatically; use the IDE as usual.
- **The IDE's download button**: in the first instant after the other tool starts, the script may not have
  released the probe yet, so that tool can occasionally report one communication error. Just click
  download again. ST-Link only allows exclusive access, so the IDE's tool may fail to open the probe
  before the script releases it; retry once (not tested).
- **Keil's own CMSIS-DAP driver pressing F8**: Keil does not start a separate process, so the script can
  only release the probe after the communication has already been disturbed. There is a short overlap.
  **Not recommended**; use build-and-download or hand F8 to the script.
- **Debugging in Keil** (Ctrl+F5) still uses Keil's own driver. Press `P` in the script window before
  debugging and `P` again afterwards.

## API

```c
void VOFA_RTT_Init(void);                               // init
void VOFA_RTT_Send1(float ch0);                         // one channel
void VOFA_RTT_Send2(float ch0, float ch1);              // two channels
void VOFA_RTT_Send(const float *data, unsigned ch_num); // N channels
void VOFA_RTT_TestLoop(void);                           // self-test sine wave, can be removed
```

Tunable in `vofa_rtt.h`: `VOFA_CH_MAX` (default 4, max channels), `VOFA_TEST_PERIOD_MS` (default 10 ms),
`VOFA_SINE_FREQ_HZ`, `VOFA_SINE_AMP`.

## FAQ

| Symptom | What to do |
|---------|-----------|
| Stays at `waiting for a probe` | Probe not plugged in properly or driver missing; with several probe types plugged in, set `PROBE` |
| `probe is in use by another program, waiting` | Another program (e.g. a Keil debug session) holds the probe; the script takes over once it is released |
| `openocd (pid ...) is using the probe` | Normal: the IDE is downloading and the script resumes afterwards. If it never resumes, an OpenOCD is still running (e.g. an EIDE debug session); end it or press `R` |
| `RTT stream connected` but the title rate stays at 0 | The firmware has not reached `VOFA_RTT_Init()` or the core is halted: press `R` (it resumes a halted core) or set `RESET=1` |
| OpenOCD keeps saying `rtt: No control block found` | The search range does not cover `_SEGGER_RTT`; fix `RAM_START` / `RAM_SIZE` from the `.map` file (the script retries every second and finds it once the range is right) |
| No download after a build | The script is not in the project folder (or above it), the output is not `.axf` / `.elf`, `AUTOFLASH=0`, the image is unchanged and was skipped, or the script is paused / has yielded the probe |
| A build downloaded a different project | Several projects' outputs are below the folder; set `FIRMWARE` |
| J-Link keeps saying `backend exited` | Wrong `DEVICE` name, or the target is not powered (it connects once powered) |
| `Port 19021 is busy` at startup | Another `start_rtt.bat` is running, or J-Link RTT Viewer / J-Scope / Ozone holds the port |
| Keil F8 cannot find the CMSIS-DAP / ST-Link | The script holds the probe: use build-and-download, or hand F8 to the script as described above |
| An occasional grey `RTT stream closed, reconnecting` | OpenOCD 0.12 on Windows sometimes drops its internal link. The script reconnects within 0.1 s and the data waits in the target buffer, so nothing is lost. Safe to ignore |
| The waveform stops while debugging in Keil | The debugger halted the core, so RTT stops; it resumes when the core runs |
| `undefined reference` at link time | `SEGGER_RTT.c` is not in the build |

`SKILL.md` (Chinese) has a longer troubleshooting table.

## Design notes

Worth reading before changing anything:

- **The firmware does not depend on the probe.** On the target side there is only a control block in RAM
  that starts with `"SEGGER RTT"`. Any probe that can read memory while the core runs can read it; J-Link
  and OpenOCD read the same block.
- **The script owns port 19021 and relays.** The backend (J-Link Commander / OpenOCD) serves RTT on the
  internal port `RTTPORT+10` (19031) and the script forwards it to VOFA+. VOFA+ stays connected while the
  backend restarts, the probe is replugged or the probe is yielded.
- **Watchdog**: every 1.5 s the script checks that the probe is present (via the system call
  `CM_Locate_DevNode`, about 0.1 ms, so the relay never stalls). If it is gone the backend is stopped and
  restarted once the probe is back; if the backend exits unexpectedly it is restarted after 1 s.
- **Only one program can use a CMSIS-DAP probe at a time.** On probes with both a v1 (HID) and a v2
  (WinUSB) interface, such as the Horco, a second program comes in through v1, and the commands and
  replies of both get mixed up (seen in testing as `command mismatch`); during a download this can corrupt
  the flash. The script's countermeasures: its own OpenOCD only uses v2 (`cmsis_dap_backend usb_bulk`)
  and retries instead of falling back to v1 when v2 is busy; before starting it checks whether someone
  else has the HID interface open; while running it checks every 100 ms for other `openocd` / `pyocd` /
  `STM32_Programmer_CLI` processes and watches the log for signs of cross-talk, yields at once and takes
  the probe back 2 s after the other side is done. J-Link sharing is handled by the SEGGER software, so
  none of this applies there.
- **Build-and-download**: file change notifications reveal new `.axf` / `.elf` files, and the script then
  polls their modification time and size every 300 ms (Windows reports write/size changes only when its
  cache is flushed, often about 10 s late). It downloads once the file has not changed for 1 s and the
  linker has closed it, and skips an image identical to the one last programmed.
- **`rtt stop` before programming**: the flash algorithm runs in target RAM and may overwrite the control
  block, so the control block is searched again after the reset.
- **RTT channel 0 everywhere.** The J-Link RTT port is bound to up-channel 0; OpenOCD maps channel 0 too.
- **J-Link halts the core on connect**, so the script sends `g` after `connect`. The command file ends with
  `sleep 86400000` to keep the connection (Commander disconnects when its command file ends). Only a
  failed `connect` makes Commander exit (`ExitOnError`), and the watchdog retries, so a target powered up
  later is connected automatically.
- **OpenOCD searches for the control block only once per `rtt start`**, so the script retries every
  second until it is found. OpenOCD does not halt on connect, and the script resumes the core only if it
  is already halted. OpenOCD's default 100 ms polling is too slow; the script uses 10 ms.
- **JustFloat binary, no float-to-text formatting**: the M0+ has no FPU and no hardware divider, so
  converting a float to ASCII takes about 28 us per channel, tens of times the cost of
  `SEGGER_RTT_Write` itself (about 1-2 us). Formatting is the real bottleneck. JustFloat copies the raw
  bytes with memcpy and **relies on little-endian byte order** (the default on ARM Cortex-M).
- **The script is a single batch + PowerShell hybrid file and is deliberately ASCII only.** GBK text breaks
  both CMD and PowerShell 5.1 parsing; do not add Chinese text to it.
- **At startup the script ends leftover `JLink.exe` / `openocd.exe` processes**, which also ends an IDE
  debug session using the same tool.
- **`SEGGER_RTT_Write()` briefly disables interrupts** (on the M0+ via `PRIMASK`, i.e. globally), so do not
  call it at a high rate from high-priority hard real-time interrupts.
