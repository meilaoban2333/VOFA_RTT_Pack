# VOFA_RTT_Pack

[中文](README.md) | **English**

A porting pack that streams MCU variables to VOFA+ in real time over SEGGER RTT, so you can plot them as
waveforms.

- **No UART needed**: data goes over the debug probe's SWD; when the buffer is full new frames are dropped
  instead of blocking the main loop.
- **Switching probes needs no firmware change**: J-Link, CMSIS-DAP (DAPLink v1/v2) and ST-Link are detected
  automatically.
- **Build and download in one step**: press F7 in Keil or Build in EIDE; about 2 s later the firmware is
  downloaded and the waveform keeps streaming.
- **Two protocols**: FireWater (text) and JustFloat (binary), switched by one macro; the sending code does
  not change.

Every setting, the API, low-level RTT settings and the design details are in the
**[full manual](references/manual.en.md)**.

## Requirements

- Windows 10 / 11; no administrator rights and no Python needed
- VOFA+ on the host
- J-Link: SEGGER J-Link Software, or the one bundled with EIDE; CMSIS-DAP / ST-Link: OpenOCD 0.12 or later
  (the one bundled with EIDE is fine)
- Any build environment: Keil MDK (AC5/AC6), EIDE, CMake / Makefile + arm-none-eabi-gcc

Probe drivers, ports in use and the rest are listed under "Requirements" in the manual.

## Quick start

**Let Claude do it**: copy the whole folder into your project and tell Claude "port RTT using
VOFA_RTT_Pack"; it reads `SKILL.md` and does the steps below. You can also copy it to
`.claude/skills/vofa-rtt/` to make it a permanent skill.

**By hand**, in five steps:

**1. Copy the sources and add them to the build**: copy the six files in `assets/RTT/` into your project
(e.g. `User/RTT/`), add the three `.c` files (`SEGGER_RTT.c`, `SEGGER_RTT_printf.c`, `vofa_rtt.c`) to the
build, and add the folder to the include path. Keil / EIDE / CMake specifics are in
[references/porting.md](references/porting.md) (Chinese).

**2. Change the code**:

```c
#include "vofa_rtt.h"

int main(void)
{
    VOFA_RTT_Init();             // before peripherals and interrupts start
    HAL_Init();
    /* ... peripheral init ... */

    while (1)
    {
        VOFA_RTT_TestLoop();     // self-test sine wave; replace with your own variables once it works
        // VOFA_RTT_Send2(speed, target);
    }
}
```

Throttle the sending: send from a fixed-period timer callback, or copy the tick throttling in
`VOFA_RTT_TestLoop()`; an unthrottled main loop floods the buffer. In a non-HAL project, change
`VOFA_TICK_HEADER` / `VOFA_GET_TICK_MS()` at the end of `vofa_rtt.h` to your own millisecond tick.

**3. Copy the script and set its parameters**: copy `assets/scripts/start_rtt.bat` to the **project root**
(build-and-download watches this folder for `.axf` / `.elf`) and edit the `set` lines at the top. The probe
type is detected automatically; only the chip-specific values need changing:

| Probe | Parameters to change | Example: STM32F407VE |
|---|---|---|
| J-Link | `DEVICE` | `STM32F407VE` |
| CMSIS-DAP / ST-Link | `TARGETCFG`, `RAM_START`, `RAM_SIZE` | `target/stm32f4x.cfg`, `0x20000000`, `0x20000` |

The other parameters rarely need changing; each one is described in step 3 of "Porting in five steps" in
the manual.

**4. Start the server**: double-click `start_rtt.bat`; it is ready once `RTT stream connected` appears.
In the window, `P` pauses and releases the probe (for a Keil debug session), `R` reconnects at once and
`Q` quits.

**5. Connect VOFA+**: data interface TCP client, IP `127.0.0.1`, port `19021`, handshake empty, data format
FireWater (matching `VOFA_PROTOCOL`). A sine and a cosine wave mean the link works.

## Which protocol

Change `VOFA_PROTOCOL` in `vofa_rtt.h`, and **change the data format in VOFA+ to match**; if they differ
there is no waveform.

- **FireWater (default)**: text, so any TCP tool shows the raw values, and `LOG()` text printing works.
  Get things working with it first.
- **JustFloat**: binary, costs only a memcpy, smaller frames, full float precision. Switch to it for sample
  rates in the kHz range, a Cortex-M0/M0+ target, or when you need full precision.

The detailed comparison is under "Which protocol" in the manual.

## Optional features

| Feature | In one line | In the manual |
|---|---|---|
| `LOG()` text printing | `printf`-style, `%f` supported; FireWater only and shares the data stream with the waveform, so keep it off while capturing | "LOG() text printing (FireWater only)" |
| Toggling watched items at run time | Register your variables in a table and send `AT+1`, `AT+1,2` from VOFA+ to choose which waveforms you see, without reflashing | "Optional: toggling watched items with downlink commands" |
| Downloading while streaming | Build-and-download is on by default; EIDE's download button and Keil's F8 have their own setups | "Downloading while streaming" |
| Host tests | After changing the firmware sources run `sh tests/run_tests.sh`; no board needed | "Host tests" |

## FAQ

| Symptom | What to do |
|---------|-----------|
| Stays at `waiting for a probe` | Probe not plugged in properly or driver missing; with several probe types plugged in, set `PROBE` |
| `RTT stream connected` but the title rate stays at 0 | The firmware has not reached `VOFA_RTT_Init()` or the core is halted: press `R`, or set `RESET=1` |
| OpenOCD keeps saying `rtt: No control block found` | `RAM_START` / `RAM_SIZE` do not cover `_SEGGER_RTT`; fix them from the `.map` file |
| J-Link keeps saying `backend exited` | Wrong `DEVICE` name, or the target is not powered (it connects once powered) |
| No waveform, or jumping values / shifted channels | The VOFA+ data format does not match `VOFA_PROTOCOL` |
| Stray points in the waveform | `LOG()` text mixed into the FireWater stream; turn LOG off while capturing |
| `Port 19021 is busy` at start-up | A `start_rtt.bat` is already running, or J-Link RTT Viewer / J-Scope / Ozone holds the port |
| Build fails with undefined reference | `SEGGER_RTT.c` or another file was not added to the build |

More cases are under "FAQ" in the manual.

## Verification status

Tested: STM32G070RB + CMSIS-DAP v2 + this script (probe detection, build-and-download, recovery after
unplugging the probe), and STM32F407VE / G070RB + Keil AC6 + J-Link (the previous script). J-Link or
ST-Link with this script and other combinations are not tested yet; see "Verification status" in the
manual for each item.
