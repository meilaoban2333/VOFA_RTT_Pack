# VOFA_RTT_Pack full manual

[中文](manual.md) | **English**

The [README](../README.en.md) covers getting started; this is the full reference: requirements and
verification status, every setting, the API, low-level RTT settings, optional features, the complete FAQ
and design notes. Keil / EIDE / CMake build setup is in [porting.md](porting.md) (Chinese).

Contents:

- Requirements
- Verification status
- Directory layout
- Settings at a glance
- Which protocol
- Porting in five steps
- Downloading while streaming
- Script command line and fixed parameters
- API
- Low-level RTT settings (SEGGER_RTT_Conf.h)
- LOG() text printing (FireWater only)
- Optional: toggling watched items with downlink commands
- FAQ
- Host tests
- Design notes

## Requirements

| Item | Requirement | Notes |
|------|-------------|-------|
| OS | Windows 10 / 11 | Uses cmd and the built-in Windows PowerShell 5.1. **No admin rights needed** |
| Host app | VOFA+ | TCP client; data format FireWater or JustFloat (the same as `VOFA_PROTOCOL` in the firmware) |
| J-Link users | SEGGER J-Link Software (tested with V8.64) or the J-Link bundled with EIDE | The script needs `JLink.exe` |
| CMSIS-DAP / ST-Link users | OpenOCD 0.12 or later | The 0.12.0-rc2 bundled with EIDE is tested, so EIDE users need nothing else. Any other OpenOCD works if it is on PATH or set in `OPENOCD` |
| Probe drivers | CMSIS-DAP v2: WinUSB. CMSIS-DAP v1: HID, no driver needed. ST-Link: ST's ST-LINK USB driver (STSW-LINK009). J-Link: installed with the J-Link software | Most CMSIS-DAP v2 probes get WinUSB automatically. If Device Manager shows no driver, install WinUSB with Zadig |
| Build tools | Any: Keil MDK (AC5/AC6), EIDE (GCC/AC6), CMake / Makefile + arm-none-eabi-gcc | The firmware side is plain C |
| Not needed | Python, pyOCD, extra PowerShell modules | |
| Ports | 19021 (for VOFA+), 19031 (internal), OpenOCD's 3333 / 4444 / 6666 | Change `RTTPORT` if 19021 is taken |

## Verification status

| Setup | Status |
|-------|--------|
| STM32F407VE (Cortex-M4), STM32G070RB (Cortex-M0+) + Keil AC6 + J-Link V864 (previous single-tool script) | Tested |
| Firmware framing, drop return values, downlink command parsing | Covered by the host tests, see "Host tests". The FireWater fixed-point conversion now divides by 10 with shifts and adds (no library division; verified against all 2^32 inputs), and a rounding issue that added 1 to odd integers above 2^23 was fixed; covered by the host tests, not yet re-tested on a board |
| STM32G070RB + Horco CMSIS-DAP v2 + this script | Tested: probe detection; 100 / 200 frames/s with no bad frames; build-and-download (including skipping an unchanged build); yielding to and resuming after another OpenOCD that really programs the chip; `flash` mode; recovery after unplugging the probe and after the backend process is killed; VOFA+ stays connected throughout. The test firmware used JustFloat; the script relays the bytes unchanged and does not depend on the protocol. Build-and-download was tested by writing out an `.axf` / `.elf` to simulate a build, not with a real Keil / EIDE build |
| J-Link + this script | Not tested |
| ST-Link, CMSIS-DAP with only a v1 (HID) interface + this script | Not tested |
| Downlink commands (`AT+...`) relayed to the MCU by this script | Not tested |
| Keil's own CMSIS-DAP driver pressing F8 while the script runs | Not tested, not recommended, see "Downloading while streaming" |
| Keil `Utilities` external tool, EIDE `Custom` flasher (IDE side) | Not tested inside the IDEs (the command lines were tested the way the IDEs call them) |

## Directory layout

```
VOFA_RTT_Pack/
├── SKILL.md                 # Claude Code skill entry, lets the AI do the port step by step
├── README.md                # Getting started (Chinese)
├── README.en.md             # Getting started (English)
├── assets/
│   ├── RTT/                 # The six source files to copy into your project
│   │   ├── SEGGER_RTT.c/.h
│   │   ├── SEGGER_RTT_Conf.h
│   │   ├── SEGGER_RTT_printf.c   # %f support added (the SEGGER original has none), return value fixed
│   │   └── vofa_rtt.c/.h         # dual-protocol framing layer + LOG() macro, protocol macro lives here
│   ├── examples/
│   │   └── log_demo.c/.h    # optional: toggle watched items with downlink AT commands (you only write a variable table)
│   └── scripts/
│       └── start_rtt.bat    # The only host-side script
├── references/
│   ├── manual.en.md         # Full manual (this file); Chinese version manual.md
│   └── porting.md           # Keil / EIDE / CMake build and download setup (Chinese)
└── tests/                   # Host-side regression tests, run on the PC after firmware changes, no board needed
```

## Settings at a glance

| Where | What it controls | When to change it | Details |
|-------|------------------|-------------------|---------|
| The `set` lines at the top of `start_rtt.bat` | Probe type, chip, OpenOCD target and RAM range, build-and-download, ports, reset, tool paths | Check for every new project | Step 3 |
| `vofa_rtt.h` | Protocol, max channels, decimals, self-test waveform, millisecond tick | Switching protocol, adding channels, non-HAL projects | "API" |
| `SEGGER_RTT_Conf.h` | Buffer sizes, channel counts, what happens when the buffer is full, critical-section mask priority, memory sections | Tight RAM, an RTOS, chips with caches such as the H7 | "Low-level RTT settings" |
| A `LogItem` table in your own project (optional) | Variables to watch, their types and channel counts; `log_demo.c/.h` themselves need no changes | When using downlink commands | "Optional: downlink commands" |
| VOFA+ UI | Data interface, IP, port, data format | Every connection | Step 5 |

The script also has some fixed values (internal port, check intervals and so on); see "Script command line
and fixed parameters".

## Which protocol

Change this line in `vofa_rtt.h`:

```c
#define VOFA_PROTOCOL   VOFA_PROTO_FIREWATER   // default
// #define VOFA_PROTOCOL   VOFA_PROTO_JUSTFLOAT
```

**Afterwards change the "data format" in VOFA+ to match**, otherwise there is no waveform. `start_rtt.bat`
relays the bytes unchanged, so the script needs no change for either protocol.

| | FireWater (default) | JustFloat |
|---|---|---|
| Frame format | `12.345,-6.789\n` | `[f0:4B][f1:4B][00 00 80 7F]` |
| Two-channel frame | about 20 bytes | 12 bytes |
| Sender cost | float→ASCII fixed-point conversion | memcpy only |
| Readable in a text tool | ✅ yes | ❌ garbage |
| `LOG()` text printing | ✅ available (pollutes the waveform, use one or the other) | ❌ disabled at compile time |
| Precision | 3 decimals (`VOFA_DECIMALS`) | full float |
| Very large values | with 3 decimals, values above about 4.29 million drop the decimals; beyond ±4294967295 they saturate | no limit |
| Needs little-endian | no | yes (ARM Cortex-M is) |

**Recommendation: get it working with FireWater first, check the values, then switch to JustFloat if
needed.** FireWater data is readable in any TCP tool connected to `127.0.0.1:19021` (with J-Link also in
RTT Viewer), so when something is wrong you can tell at once whether the sender or the host settings are
at fault; JustFloat is a binary blob and debugging it is guesswork.

**Three cases for JustFloat**: sample rates in the kHz range; a Cortex-M0/M0+ target (no FPU and no
hardware divider, so float-to-ASCII took about 28 us per channel, tens of times the RTT write itself;
that was the old implementation; it now does one division per digit with shifts and adds instead of a
library call, which should be much faster, not re-measured yet);
full float precision or values beyond the fixed-point range.

## Porting in five steps

### 1. Copy the sources

Copy the six files in `assets/RTT/` into your project, for example `User/RTT/` (any path works).

To toggle watched items at run time, also copy `assets/examples/log_demo.c/.h`; see "Optional: downlink
commands".

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
| `DEVICE` | `STM32G070RB` | J-Link | SEGGER device name, usually the part number without package/temperature suffix. **Change it to your chip** (e.g. `STM32F407VE` for an F407VET6). The default is the same G070 as the OpenOCD settings below. A wrong name means no connection |
| `TARGETCFG` | `target/stm32g0x.cfg` | CMSIS-DAP / ST-Link | OpenOCD target script; the default is for STM32G070RB. **Change it to your chip**, e.g. `target/stm32f4x.cfg` |
| `RAM_START` / `RAM_SIZE` | `0x20000000` / `0x9000` | CMSIS-DAP / ST-Link | Search range for the control block, must cover `_SEGGER_RTT`. The default is the 36 KB SRAM of the G070 (F407VE: `0x20000000` / `0x20000`). Most reliable: put the `_SEGGER_RTT` address from the `.map` file in `RAM_START` and `0x100` in `RAM_SIZE` |
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

In a non-HAL project, change the two lines `VOFA_TICK_HEADER` (default `"main.h"`) and `VOFA_GET_TICK_MS()`
(default `HAL_GetTick()`) at the end of `vofa_rtt.h` to your own header and millisecond tick function;
`vofa_rtt.c` stays as it is. Only the self-test function `VOFA_RTT_TestLoop()` uses them.

**Throttle the sending.** An unthrottled main loop fills the buffer and frames get dropped. Use the tick
throttling in `TestLoop` as a template, or send from a fixed-period timer callback. Frame size: FireWater
is about (decimals+7)×channels bytes, JustFloat is 4×channels+4 bytes. FireWater is fine up to a few
hundred Hz; go to JustFloat for more.

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
| Data format | **the same as `VOFA_PROTOCOL`** (FireWater or JustFloat) |

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

## Script command line and fixed parameters

Two ways to run it:

| Usage | What it does |
|-------|--------------|
| Double-click, or `start_rtt.bat` on the command line | Starts the RTT server (probe detection, relay, watchdog, build-and-download) |
| `start_rtt.bat flash <firmware file>` | Downloads one firmware image. `.axf` / `.elf` / `.hex` are supported; `.bin` has no address and is not. If the script is running, the download goes through it and RTT keeps streaming; otherwise a temporary OpenOCD downloads and exits. Returns 0 on success and 1 on failure (Keil's external tool uses this). Not supported with J-Link; download from the IDE instead |

Keys in the server window: `P` pauses and releases the probe / `P` again resumes, `R` reconnects now,
`Q` quits.

These values are fixed in the second (PowerShell) half of the script. They rarely need changing; edit them
there if you must:

| Item | Value | Meaning |
|------|-------|---------|
| Internal port | `RTTPORT+10` (19031 by default) | The backend's RTT service; the script relays it to `RTTPORT` |
| OpenOCD ports | GDB 3333, telnet 4444, Tcl 6666 | `flash` mode finds the running script through Tcl 6666 |
| Watchdog interval | 1.5 s | Checks that the probe is present and the backend is still running |
| Backend restart delay | 1 s | How long after an unexpected backend exit it is restarted |
| Yield check / resume delay | 100 ms / 2 s | How often to look for other download tools; how long the probe must be idle before taking it back |
| Control block retry | 1 s | How often to retry when OpenOCD has not found the control block |
| OpenOCD RTT polling | 10 ms | OpenOCD's own default of 100 ms is too slow |
| Build output settle time | 1 s | An `.axf` / `.elf` is downloaded only after it has not changed for 1 s |
| Download record | `%TEMP%\start_rtt_<RTTPORT>.last` | Hash of the image last programmed through the script, used to skip repeat downloads |
| Backend logs | `%TEMP%\start_rtt_<process id>.*.log` | Deleted when the script exits |

**Only one `start_rtt.bat` can run per PC**: at startup it ends other `JLink.exe` / `openocd.exe`
processes, and the OpenOCD Tcl port is fixed, so several boards with one script each are not supported.

## API

```c
void VOFA_RTT_Init(void);                                   // init
unsigned VOFA_RTT_Send1(float ch0);                         // one channel
unsigned VOFA_RTT_Send2(float ch0, float ch1);              // two channels
unsigned VOFA_RTT_Send(const float *data, unsigned ch_num); // N channels
VOFA_SEND(a, b, c);                                         // any number of channels in one line, no array (C99)
void VOFA_RTT_TestLoop(void);                               // self-test sine wave, can be removed

unsigned VOFA_RTT_SendRaw(const void *buf, unsigned len);   // send raw bytes, no framing
unsigned VOFA_RTT_SendStr(const char *s);                   // send a string (FireWater only)
LOG("fmt", ...);                                            // printf-style text printing (FireWater only)
```

**The API is identical for both protocols**; switching protocols needs no change in the calling code.
`SendStr` and `LOG` send nothing under JustFloat; see the LOG section below for why.

The send functions return the number of bytes written; **0 means the frame was dropped** (buffer full or
invalid arguments), which you can use to count dropped frames. Ignore it if you do not care.

`VOFA_SEND(...)` converts its arguments to float (`int` and `double` are fine) and evaluates each once.
More arguments than `VOFA_CH_MAX` is a compile error, rather than a frame silently dropped at run time.
It needs C99 compound literals: in Keil AC5 tick `Options for Target` → `C/C++` → `C99 Mode`, otherwise the
macro does not exist; AC6 / GCC work by default.

Tunable in `vofa_rtt.h`:

| Macro | Default | Meaning |
|-------|---------|---------|
| `VOFA_PROTOCOL` | `VOFA_PROTO_FIREWATER` | Protocol selection; change the VOFA+ setting to match |
| `VOFA_CH_MAX` | 8 | Max channels. **A frame with more channels is dropped entirely**; raise it when adding channels |
| `VOFA_DECIMALS` | 3 | Decimals per channel; more makes frames longer. **FireWater only** |
| `VOFA_TEST_PERIOD_MS` | 10 | Self-test period, i.e. 100 Hz sample rate |
| `VOFA_SINE_FREQ_HZ` / `VOFA_SINE_AMP` | 1.0 / 100 | Frequency and amplitude of the self-test sine |
| `VOFA_TICK_HEADER` / `VOFA_GET_TICK_MS()` | `"main.h"` / `HAL_GetTick()` | Millisecond tick for throttling the self-test (returns `uint32_t`, may wrap). Change to your own header and function in a non-HAL project |

Two more macros must **not** be changed: `VOFA_RTT_BUF_IDX` (fixed at 0, the script only relays channel 0)
and `VOFA_NUM_MAX` (derived from `VOFA_DECIMALS`).

The macros in `vofa_rtt.h` have no `#ifndef` guard, so **edit the file itself**; they cannot be overridden
with project-wide defines.

## Low-level RTT settings (SEGGER_RTT_Conf.h)

All of these are wrapped in `#ifndef`, so you can edit the file or override them with project-wide defines
(Keil: `Options for Target` → `C/C++` → `Define`; GCC: `-DBUFFER_SIZE_UP=4096`), which keeps your settings
when you replace the file with a newer version.

| Macro | Default in this pack | Meaning |
|-------|----------------------|---------|
| `BUFFER_SIZE_UP` | 2048 | Up-buffer 0, carrying both the waveform and `LOG()`. With tight RAM, 512 / 256 works at the cost of more dropped frames at high rates; CMSIS-DAP reads slower than J-Link, so raise it for high sample rates with many channels |
| `BUFFER_SIZE_DOWN` | 16 | Down-buffer 0, receiving downlink commands. Longer commands are fine; `log_demo` assembles the line across reads |
| `SEGGER_RTT_MAX_NUM_UP_BUFFERS` | 1 | Number of up-channels; only channel 0 is used. Set 2 to move text to channel 1 (the script only relays channel 0, so view channel 1 in J-Link RTT Viewer) |
| `SEGGER_RTT_MAX_NUM_DOWN_BUFFERS` | 1 | Number of down-channels; only channel 0 is used |
| `SEGGER_RTT_PRINTF_BUFFER_SIZE` | 128 | Stack buffer used by `LOG()` (SEGGER's default is 64). A line that fits is written in one go, so a waveform frame sent from an interrupt cannot land in the middle of it; longer lines are sent in pieces. Larger is safer but every `LOG()` call uses more stack |
| `SEGGER_RTT_MODE_DEFAULT` | `SEGGER_RTT_MODE_NO_BLOCK_SKIP` | What happens when the buffer is full: `NO_BLOCK_SKIP` drops the whole frame (recommended); `NO_BLOCK_TRIM` writes what fits and produces half frames, so do not use it with a waveform protocol; `BLOCK_IF_FIFO_FULL` waits for space and hangs the main loop when the script is not running. **Note that `VOFA_RTT_Init()` forces channel 0 to `NO_BLOCK_SKIP`, so this macro has no effect on channel 0**; change that line in `vofa_rtt.c` if you really need another mode |
| `SEGGER_RTT_MAX_INTERRUPT_PRIORITY` | 0x20 | M3/M4/M7/M33 only: while writing RTT, `BASEPRI` masks interrupts whose priority register value is ≥ this (with STM32's 4 priority bits, priorities 2–15). Priority 0 and 1 interrupts are not masked, **so do not call RTT from them**. With FreeRTOS this is usually set to `configMAX_SYSCALL_INTERRUPT_PRIORITY`. M0/M0+ have no such setting and disable interrupts globally |
| `SEGGER_RTT_MEMCPY_USE_BYTELOOP` | 0 | 1 = copy byte by byte instead of `memcpy`, slightly faster for very small amounts; usually leave it |
| `SEGGER_RTT_SECTION` / `SEGGER_RTT_BUFFER_SECTION` | not defined | Put the control block / buffers into a given linker section. Uses: fix the address of `_SEGGER_RTT` (so OpenOCD's `RAM_START` can be set exactly); on chips with a D-cache such as the H7, put them in DTCM or non-cacheable RAM so the debugger never reads stale data still sitting in the cache |
| `SEGGER_RTT_CPU_CACHE_LINE_SIZE` / `SEGGER_RTT_UNCACHED_OFF` | commented out | For cached systems such as Cortex-A; usually not needed on Cortex-M |

## LOG() text printing (FireWater only)

Works exactly like `printf` and goes out on RTT channel 0. Connect any TCP tool to `127.0.0.1:19021` to
see it (with J-Link also RTT Viewer / the J-Link Commander terminal):

```c
LOG("id=%d speed=%.2f flag=%s\n", id, speed, ok ? "OK" : "NG");
```

Supports `%c %d %u %x %X %s %p %f %F %%`, with the `-` `0` `+` `#` flags, field width and precision.

**`%f` was added to `SEGGER_RTT_printf` by this pack**; the SEGGER original does not support it. It is a
fixed-point implementation with 6 decimals by default and a range of ±4294967295; values beyond that or NaN
print an `Inf`/`NaN` marker instead of silently wrapping.

Output of up to 128 bytes per call (`SEGGER_RTT_PRINTF_BUFFER_SIZE`) is written to RTT in one go: a
waveform frame sent from an interrupt cannot land in the middle of the line, and when the buffer is full the
whole line is dropped rather than half of it. Longer lines are written in pieces and lose this guarantee.
`LOG()` returns the number of bytes actually written, or -1 when the buffer was full and something was
dropped (SEGGER's original return value counts the last piece twice; this package fixes that).

For fixed text that needs no formatting, use `VOFA_RTT_SendStr("motor start\n")`: the whole string is
written at once with no formatting cost, under the same rules as `LOG()` (FireWater only). For custom
binary content use `VOFA_RTT_SendRaw(buf, len)`, which works under both protocols; under JustFloat you must
make sure you write complete frames yourself.

⚠️ **Under FireWater, LOG shares channel 0 with the waveform.** Text that gets mixed in is parsed as data
lines and shows up as stray points. **Do not use LOG while capturing waveforms; pick one.**

⚠️ **Under JustFloat, `LOG()` is disabled at compile time** and expands to `((void)0)`. Text mixed into
the binary stream would make VOFA+ parse text bytes as floats, or happen to hit the frame tail
`00 00 80 7F` and shift every channel from then on — far worse than FireWater's dirty data, and not
something careful use can avoid.

Call sites need no `#if`, but note that **the arguments are not evaluated either**: `LOG("%d", i++)`
does not run `i++` under JustFloat, so **keep side effects out of LOG arguments**. For text output under
JustFloat, switch back to FireWater temporarily, or use RTT channel 1 yourself (raise
`SEGGER_RTT_MAX_NUM_UP_BUFFERS` to 2 and, with J-Link, view Terminal 1 in RTT Viewer; `start_rtt.bat` only
relays channel 0, so VOFA+ never sees channel 1).

## Optional: toggling watched items with downlink commands

`assets/examples/log_demo.c/.h` lets you register the variables you want to watch in a table and toggle
them with commands at run time, so you can change which waveforms you watch without editing code and
reflashing. `log_demo.c/.h` themselves need no changes; the table lives in your own file.
Commands are sent from the VOFA+ send box and `start_rtt.bat` relays them to the RTT downlink channel
(with J-Link also from the RTT Viewer input line).

```c
#include "log_demo.h"

static const LogItem s_logItems[] = {
    LOG_F32(g_angle, 1),     // item 1: electrical angle
    LOG_F32(g_iabc,  3),     // item 2: three phase currents (first 3 array elements)
    LOG_U16(g_adcVbus, 1),   // item 3: raw bus-voltage ADC value, sent as float
    LOG_I32(g_speed, 2),     // item 4: first 2 array elements
};

/* Optional: handle non-toggle commands; line is '\0'-terminated, newline removed */
static void MyCmd(const char *line, unsigned len) { /* parse it yourself, e.g. AT+KP=1.5 */ }

int main(void)
{
    VOFA_RTT_Init();
    LogInit(s_logItems, sizeof(s_logItems) / sizeof(s_logItems[0]));
    LogSetCmdHook(MyCmd);
    for (;;) {
        /* every 20-50 ms */
        LogTask();
    }
}
```

Items are numbered in table order, the first row being item 1; the channel order within a frame is the
table order. Commands:

```
AT+1\n      toggle item 1
AT+1,2\n    toggle items 1 and 2; when both are on they share one frame
AT+1\n      the same number again turns it off (each number toggles independently)
AT+0\n      turn everything off
AT+KP=1.5\n not the format above, so it goes to the LogSetCmdHook callback (dropped if none is set)
```

| Item | Where | Default | Meaning |
|------|-------|---------|---------|
| `LOG_F32` / `LOG_I32` / `LOG_U32` / `LOG_I16` / `LOG_U16` / `LOG_I8` / `LOG_U8` | `log_demo.h` | — | Table-entry macros `(variable, channels)`; the variable can be a scalar or an array. Integers are sent as float. **Compile-time check**: the build fails when the channel count is 0, exceeds `VOFA_CH_MAX`, or exceeds the variable's size (e.g. `LOG_F32` on a `short`) |
| `LOG_ITEM_MAX` | `log_demo.h` | 64 | Maximum number of registered items; extra entries are ignored. One bit per item; can be overridden in the compiler options |
| `LOG_LINE_MAX` | `log_demo.h` | 32 | Maximum characters per command; a longer command is discarded entirely (also applies to the callback) |
| Channel limit | — | `VOFA_CH_MAX` | Turning an item on is ignored when the frame would then exceed `VOFA_CH_MAX` channels, so the table can hold far more items than `VOFA_CH_MAX` and you turn on a subset |
| Command format | `log_demo.c` | `AT+` | A line is a toggle command only if `AT+` is followed by digits, commas and spaces (at least one digit), ending with CR or LF; every other non-empty line goes to the callback |

Notes:

- 32-bit integers with a magnitude above 16777216 lose precision when converted to float (VOFA+ only
  understands float).
- The compile-time check only looks at sizes and **cannot catch pointers or `double`**: `LOG_F32` on a
  pointer or a `double` compiles but sends wrong numbers. An array used from another file needs its length
  in the `extern` declaration (e.g. `extern float g_iabc[3];`), otherwise the build fails.
- Registered variables must be naturally aligned (ordinary globals are); members of packed structs are
  not, and an unaligned access faults on M0/M0+.
- The on/off state is not echoed: the uplink is a pure data stream and text would corrupt the waveform.
- Running `LogTask()` from a timer interrupt while `LogInit()` is called from the main loop (including
  switching tables at run time) is safe.
- **Upgrading from the old template**: `LOG_ID_xxx`, `LogPrint()` and `LOG_CH_MAX` are gone. After
  replacing the files with the new `log_demo.c/.h`, write a table in your own file and call `LogInit()`;
  old code that only calls `LogTask()` still compiles but no longer outputs anything.

**Works with both protocols without changes**: output goes through `VOFA_RTT_Send()`, so the frame format
follows `VOFA_PROTOCOL`; commands use the downlink channel, which is independent of the uplink protocol,
so AT commands work under JustFloat too.

## FAQ

| Symptom | What to do |
|---------|-----------|
| Stays at `waiting for a probe` | Probe not plugged in properly or driver missing; with several probe types plugged in, set `PROBE` |
| `probe is in use by another program, waiting` | Another program (e.g. a Keil debug session) holds the probe; the script takes over once it is released |
| `openocd (pid ...) is using the probe` | Normal: the IDE is downloading and the script resumes afterwards. If it never resumes, an OpenOCD is still running (e.g. an EIDE debug session); end it or press `R` |
| `RTT stream connected` but the title rate stays at 0 | The firmware has not reached `VOFA_RTT_Init()` or the core is halted: press `R` (it resumes a halted core) or set `RESET=1` |
| OpenOCD keeps saying `rtt: No control block found` | The search range does not cover `_SEGGER_RTT`; fix `RAM_START` / `RAM_SIZE` from the `.map` file (the script retries every second and finds it once the range is right) |
| **No waveform at all after changing the protocol macro** | The VOFA+ data format was not changed to match; both sides must agree |
| Wildly jumping huge values / shifted channels | Wrong protocol selected, or text mixed into the JustFloat stream |
| Stray points in the waveform | `LOG()` text mixed into the FireWater stream; turn LOG off while capturing |
| `LOG()` prints nothing at all | The protocol is JustFloat and LOG is disabled at compile time; this is expected |
| A frame never shows up | `ch_num > VOFA_CH_MAX`, the whole frame is dropped; raise `VOFA_CH_MAX`. A full buffer drops whole frames too; a send function returning 0 marks a dropped frame |
| A channel is always 0 | The source variable is Inf/NaN (division by zero, uninitialised, ...); both protocols send it as 0. Check how that variable is computed |
| Not enough precision / large values clipped | Raise `VOFA_DECIMALS`, or switch to JustFloat for full floats |
| AT commands do nothing | `LogInit()` was not called to register the table, `LogTask()` is not called periodically, or the command was sent without a newline; an ID beyond the table, or one that would exceed `VOFA_CH_MAX`, is ignored too |
| No download after a build | The script is not in the project folder (or above it), the output is not `.axf` / `.elf`, `AUTOFLASH=0`, the image is unchanged and was skipped, or the script is paused / has yielded the probe |
| A build downloaded a different project | Several projects' outputs are below the folder; set `FIRMWARE` |
| J-Link keeps saying `backend exited` | Wrong `DEVICE` name, or the target is not powered (it connects once powered) |
| `Port 19021 is busy` at startup | Another `start_rtt.bat` is running, or J-Link RTT Viewer / J-Scope / Ozone holds the port |
| Keil F8 cannot find the CMSIS-DAP / ST-Link | The script holds the probe: use build-and-download, or hand F8 to the script as described above |
| An occasional grey `RTT stream closed, reconnecting` | OpenOCD 0.12 on Windows sometimes drops its internal link. The script reconnects within 0.1 s and the data waits in the target buffer, so nothing is lost. Safe to ignore |
| The waveform stops while debugging in Keil | The debugger halted the core, so RTT stops; it resumes when the core runs |
| `undefined reference` at link time | `SEGGER_RTT.c` is not in the build |

`SKILL.md` (Chinese) has a longer troubleshooting table.

## Host tests

After changing `vofa_rtt.c` / `log_demo.c` you can run the regression tests on the PC first, no board needed:

```sh
sh tests/run_tests.sh
```

This needs a host gcc (on Windows, MinGW gcc + Git Bash). The script copies the sources under `assets/` to a
temporary folder, builds and runs them in four configurations (FireWater with 3, 0 and 6 decimals, and
JustFloat) and compares the bytes in the RTT up buffer. It also checks C90 compatibility
(simulating Keil AC5's rule on where declarations may go) and that `VOFA_SEND` with more arguments than
`VOFA_CH_MAX`, or a misused `LOG_xxx` table macro, fails to compile. A last line of `ALL PASSED` means everything passed.

## Design notes

Worth reading before changing anything:

- **The firmware does not depend on the probe.** On the target side there is only a control block in RAM
  that starts with `"SEGGER RTT"`. Any probe that can read memory while the core runs can read it; J-Link
  and OpenOCD read the same block.
- **The script owns port 19021 and relays.** The backend (J-Link Commander / OpenOCD) serves RTT on the
  internal port `RTTPORT+10` (19031) and the script forwards it to VOFA+; commands from VOFA+ are passed on
  to the downlink channel unchanged. VOFA+ stays connected while the backend restarts, the probe is
  replugged or the probe is yielded. The script does not parse the data, so it does not care about the
  protocol.
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
- **FireWater is the default for readability**: when the waveform looks wrong you can read the raw values
  in a text tool instead of guessing whether the sender or the host's parser is misconfigured. The price is
  the float→ASCII conversion. The pack does not call the C library's `sprintf` (too big) and uses its own
  fixed-point `VOFA_FloatToStr()`, which only does integer digit extraction: one quotient per decimal digit (computed with shifts
  and adds, no library division), and
  the sign, NaN/Inf and overflow checks use the float's bit pattern, so an M0/M0+ without a hardware
  divider or FPU calls as few library routines as possible. Even so, **do not put it in a current loop or
  other high-rate interrupt**.
- **JustFloat saves formatting cost, not transfer cost**: it copies the raw float bytes and only replaces
  Inf/NaN with 0 (the bytes of `+Inf` equal the frame tail `00 00 80 7F`, so sending it as is would make
  VOFA+ cut the frame early). The
  difference is largest on M0/M0+ — float-to-ASCII took about 28 us per channel (measured on the
  old implementation; it now divides by 10 with shifts and adds instead of a library call, not re-measured) while the RTT write itself takes about 1-2 us,
  so formatting is the real bottleneck.
- **Both protocols share one sending API.** `VOFA_RTT_Send()` compiles into one of two implementations
  depending on `VOFA_PROTOCOL`; the other one and its helpers are dropped, costing no flash and raising no
  unused warnings.
- **FireWater's fixed-point conversion has no scientific notation**: the limit with `VOFA_DECIMALS`
  decimals is about 4.29×10^9 / 10^decimals (about 4.29 million with 3 decimals). Above it fewer decimals
  are sent: from 2^22 (about 4.19 million) only the integer (a float that large has no fractional
  precision anyway); smaller values (only reachable with 4 or more decimals) keep as many decimals as
  fit; beyond ±4294967295 the value saturates to the limit. NaN/Inf become 0 to keep the frame structure
  intact. If the range is too small, scale on the MCU before sending, or switch to JustFloat.
- **JustFloat relies on little-endian byte order**: ARM Cortex-M is little-endian by default; big-endian
  platforms would need per-channel byte swapping, which this pack does not do.
- **SEGGER defaults to `RTT_USE_ASM=1` on M3/M4/M7/M33 with AC6/GCC**: `SEGGER_RTT_WriteSkipNoLock` is then
  replaced by an assembly version whose `.S` file this pack does not ship. This pack never calls it, so it is
  unaffected; calling it yourself gives a link error, in which case define `RTT_USE_ASM=0` in the compiler options.
- **Text and waveform must never share a JustFloat stream**: text bytes would be parsed as floats, or hit
  the frame tail `00 00 80 7F` and shift every channel from then on. That is why `LOG()` is disabled at
  compile time for this protocol, and `VOFA_RTT_SendStr()` sends nothing.
- **The script is a single batch + PowerShell hybrid file and is deliberately ASCII only.** GBK text breaks
  both CMD and PowerShell 5.1 parsing; do not add Chinese text to it.
- **At startup the script ends leftover `JLink.exe` / `openocd.exe` processes**, which also ends an IDE
  debug session using the same tool.
- **`SEGGER_RTT_Write()` enters a critical section** (via `BASEPRI` on M3/M4/M7; on M0/M0+ it falls back to
  `PRIMASK`, disabling interrupts globally), so do not call it at a high rate from high-priority hard
  real-time interrupts.
