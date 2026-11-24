# VOFA_RTT_Pack

**中文** | [English](README.en.md)

SEGGER RTT + VOFA+ 实时波形输出的自包含移植包。经 J-Link、CMSIS-DAP（DAPLink，v1/v2）或 ST-Link
把 MCU 内部变量推给 VOFA+，不占用串口，缓冲满自动丢帧不阻塞主循环。换调试器不用改固件。

主机端只有一个脚本 `start_rtt.bat`，双击即用：

- **自动识别调试器**，探针拔插后自动重连，VOFA+ 连接不断；
- **编译即下载**：Keil 按 F7 / EIDE 点 Build，编译完约 2 秒自动下载，波形接着推；
- **IDE 自己的下载按钮也能用**：别的下载工具一出现，脚本就让出探针，对方下载完自动接回。

## 环境要求

| 项 | 要求 | 说明 |
|----|------|------|
| 操作系统 | Windows 10 / 11 | 脚本用到 cmd 和系统自带的 Windows PowerShell 5.1，**不需要管理员权限** |
| 上位机 | VOFA+ | 用 TCP 客户端 + JustFloat 协议 |
| J-Link 用户 | SEGGER J-Link Software（已用 V8.64）或 EIDE 自带的 J-Link | 脚本要用到 `JLink.exe` |
| CMSIS-DAP / ST-Link 用户 | OpenOCD 0.12 及以上 | EIDE 自带的 0.12.0-rc2 已实测，装了 EIDE 就不用另装；也可以把别的 OpenOCD 放进 PATH 或填 `OPENOCD` |
| 探针驱动 | CMSIS-DAP v2：WinUSB；CMSIS-DAP v1：HID 免驱；ST-Link：ST 官方 ST-LINK USB 驱动（STSW-LINK009）；J-Link：随 J-Link 软件安装 | 多数 CMSIS-DAP v2 探针插上会自动装好 WinUSB，设备管理器里没有就用 Zadig 给它装 WinUSB |
| 编译环境 | 任意：Keil MDK（AC5/AC6）、EIDE（GCC/AC6）、CMake / Makefile + arm-none-eabi-gcc | 固件侧是纯 C |
| 不需要 | Python、pyOCD、额外的 PowerShell 模块 | |
| 占用端口 | 19021（给 VOFA+）、19031（内部）、OpenOCD 的 3333 / 4444 / 6666 | 19021 冲突时改 `RTTPORT` |

## 验证情况

| 组合 | 状态 |
|------|------|
| STM32G070RB + Keil AC6 + J-Link V864（旧版单工具脚本） | 已实测 |
| STM32G070RB + Horco CMSIS-DAP v2 + 本脚本 | 已实测：自动识别；100 / 200 帧每秒无坏帧；编译即下载（含内容没变时跳过）；另一个 OpenOCD 真实烧录时自动让出并接回；`flash` 模式；拔插探针、杀后台进程后自动恢复；全程 VOFA+ 侧不断开。编译即下载是用"写出 `.axf` / `.elf`"模拟编译测的，没在 Keil / EIDE 里真实编译 |
| J-Link + 本脚本 | 未实测 |
| ST-Link、只有 v1(HID) 的 CMSIS-DAP + 本脚本 | 未实测 |
| Keil 自带 CMSIS-DAP 驱动按 F8（脚本运行中） | 未实测，不推荐，见"边看波形边下载" |
| Keil `Utilities` 外部工具、EIDE `Custom` 烧录器的界面配置 | 未在 IDE 界面里实测（命令行已按 IDE 的调用方式实测） |

## 目录结构

```
VOFA_RTT_Pack/
├── SKILL.md                 # Claude Code 技能入口，让 AI 按流程自动移植
├── README.md                # 本文件，人工移植看这个
├── README.en.md             # 英文版 README
├── assets/
│   ├── RTT/                 # 直接拷进工程的六个源文件
│   │   ├── SEGGER_RTT.c/.h
│   │   ├── SEGGER_RTT_Conf.h
│   │   ├── SEGGER_RTT_printf.c
│   │   └── vofa_rtt.c/.h    # JustFloat 组帧封装层
│   └── scripts/
│       └── start_rtt.bat    # 唯一的主机端脚本
└── references/
    └── porting.md           # Keil / EIDE / CMake 构建配置、下载配置
```

## 两种用法

**给 Claude 用**：整个文件夹拷到新工程，对 Claude 说"按 VOFA_RTT_Pack 移植 RTT"，
它读 `SKILL.md` 自动完成拷文件、改工程配置、改脚本参数。
也可以拷到 `.claude/skills/vofa-rtt/` 变成常驻技能。

**人工移植**：按下面五步走，细节查 `references/porting.md`。

## 人工移植五步

### 1. 拷源码

`assets/RTT/` 六个文件 → 目标工程的 `User/RTT/`（路径可自定）。

### 2. 加进构建

- 3 个 .c 加入编译：`SEGGER_RTT.c`、`SEGGER_RTT_printf.c`、`vofa_rtt.c`
- RTT 目录加进头文件搜索路径

Keil / EIDE / CMake / Makefile 的具体改法见 **`references/porting.md`**，
里面有图形界面步骤和可直接粘贴的配置片段。

### 3. 拷脚本、改参数

`start_rtt.bat` 拷到**工程根目录**（编译即下载会监视这个目录下的 `.axf` / `.elf`），
改开头的 `set` 行。调试器自动识别，只需填芯片相关参数：

| 参数 | 默认 | 谁用 | 说明 |
|------|------|------|------|
| `PROBE` | `auto` | 全部 | `auto` 按 J-Link → ST-Link → CMSIS-DAP 顺序识别；同时插了多种时写死 `jlink` / `cmsis-dap` / `stlink` |
| `DEVICE` | `STM32G070RB` | J-Link | SEGGER 器件名，一般是去掉封装/温度后缀的型号。写错连不上目标 |
| `TARGETCFG` | `target/stm32g0x.cfg` | CMSIS-DAP / ST-Link | OpenOCD 目标脚本，如 `target/stm32f4x.cfg` |
| `RAM_START` / `RAM_SIZE` | `0x20000000` / `0x9000` | CMSIS-DAP / ST-Link | 控制块搜索范围，必须覆盖 `_SEGGER_RTT`。默认是 G070 的 36KB SRAM，换芯片必改。最稳：从 `.map` 查 `_SEGGER_RTT` 地址填 `RAM_START`，`RAM_SIZE` 填 `0x100` |
| `AUTOFLASH` | `1` | CMSIS-DAP / ST-Link | 1 = 编译即下载；0 = 关闭 |
| `FIRMWARE` | 空 | CMSIS-DAP / ST-Link | 空 = 监视本目录下所有 `.axf` / `.elf`；目录里有多个工程时填要下载的那个文件（绝对路径或相对本目录） |
| `RESET` | `0` | 全部 | 0 = 附着到正在跑的固件（重连时不重启 MCU）；1 = 每次连上都复位目标 |
| `SPEED` / `RTTPORT` | `4000` / `19021` | 全部 | SWD 速率 kHz / VOFA+ 连接的端口 |
| `JLINK` / `OPENOCD` | 空 | 全部 | 工具路径，留空自动找：J-Link 找 `C:\Program Files\SEGGER\JLink*`、EIDE 自带、PATH；OpenOCD 找 EIDE 自带、PATH |

### 4. 改代码

```c
#include "vofa_rtt.h"

int main(void)
{
    VOFA_RTT_Init();       // 必须在外设和中断启动前

    HAL_Init();
    /* ... 外设初始化 ... */

    while (1)
    {
        VOFA_RTT_TestLoop();   // 自检正弦波，验证通了换成真实变量
        // VOFA_RTT_Send2(speed, target);
    }
}
```

非 HAL 工程要把 `vofa_rtt.c` 里 `VOFA_RTT_TestLoop()` 的 `HAL_GetTick()`
换成自己的毫秒计时源，或直接删掉这个测试函数。

**发送必须节流**，主循环裸跑不节流会灌满缓冲导致丢帧。参考 `TestLoop` 的 tick
节流写法，或放固定周期定时器回调里。JustFloat 单帧很小，500Hz 以内都很轻松。

### 5. 开服务 + 配 VOFA+

双击 `start_rtt.bat`。看到 `probe: ...` 和 `RTT stream connected`（CMSIS-DAP / ST-Link 下还有
`RTT Control block found at 0x...`）就绪，窗口标题实时显示状态和数据速率（KB/s）。

VOFA+ 里配：

| 项 | 值 |
|----|-----|
| 数据接口 | TCP 客户端 |
| 服务器 IP | 127.0.0.1 |
| 网络端口 | 19021 |
| 握手数据 | 留空 |
| 数据格式 | **JustFloat** |

看到 1Hz 正弦 + 余弦两条波，说明链路通了。之后脚本窗口一直开着即可：

- **探针拔掉再插回**：自动重连，VOFA+ 不用重新点连接（已实测）。
  目标板单独断电再上电的情况未实测，没恢复的话按 `R`。
- 窗口里的按键：`P` 暂停并释放探针（要用 Keil 调试时）/ 再按 `P` 恢复，`R` 立即重连，`Q` 退出。

## 边看波形边下载

| 方式 | 怎么用 | 适用 | 状态 |
|------|--------|------|------|
| **编译即下载（推荐）** | 什么都不用配。Keil 按 F7、EIDE 点 Build，编译成功约 2 秒后脚本自动下载，波形接着推 | CMSIS-DAP / ST-Link | 已用模拟编译输出实测 |
| IDE 自己的下载按钮 | EIDE 照常点下载：脚本发现 `openocd` / `pyocd` 进程出现就让出探针，对方下载完 2 秒后自动接回 | CMSIS-DAP | 已用独立 OpenOCD 模拟实测 |
| Keil F8 交给脚本 | `Options for Target` → `Utilities` → `Use External Tool for Flash Programming`，`Command` 填 `start_rtt.bat` 完整路径，`Arguments` 填 `flash "#L"`，不勾 `Run Independent` | CMSIS-DAP / ST-Link | 命令行已实测 |
| J-Link | 什么都不用改，照常 F8。J-Link 软件允许多个程序共用一个探针 | J-Link | — |

几点说明：

- **编译即下载**只下载脚本启动后新编译出来的输出；内容和上次下载的一样（没改代码重新编译）会自动跳过；
  脚本处于暂停、让出状态时不下载。J-Link 下不自动下载，照常用 IDE 下载。
- **IDE 下载按钮**：对方刚启动的一瞬间脚本可能还没让出，对方偶尔会报一次通信错误，再点一次下载即可。
  ST-Link 是独占访问，IDE 的工具可能在脚本让出前就打不开探针，同样重试一次即可（未实测）。
- **Keil 自带的 CMSIS-DAP 驱动按 F8**：Keil 不起单独的进程，脚本只能在发现通信被打断后才让出，
  中间有短暂重叠，**不推荐**，用编译即下载或"Keil F8 交给脚本"。
- **Keil 调试**（Ctrl+F5）仍走 Keil 自己的驱动，调试前在脚本窗口按 `P`，调试完再按 `P`。

## 接口

```c
void VOFA_RTT_Init(void);                               // 初始化
void VOFA_RTT_Send1(float ch0);                         // 单通道
void VOFA_RTT_Send2(float ch0, float ch1);              // 双通道
void VOFA_RTT_Send(const float *data, unsigned ch_num); // N 通道
void VOFA_RTT_TestLoop(void);                           // 自检正弦波，可删
```

`vofa_rtt.h` 里可调：`VOFA_CH_MAX`(默认4，最大通道数)、
`VOFA_TEST_PERIOD_MS`(默认10ms)、`VOFA_SINE_FREQ_HZ`、`VOFA_SINE_AMP`。

## 常见问题

| 现象 | 处理 |
|------|------|
| 一直 `waiting for a probe` | 探针没插好或驱动没装；同时插了多种探针时用 `PROBE` 写死 |
| `probe is in use by another program, waiting` | 别的程序（如 Keil 调试）占着探针，它释放后脚本自动接管 |
| `openocd (pid ...) is using the probe` | 正常：IDE 正在下载，下载完自动恢复。一直不恢复说明有常驻的 OpenOCD（如 EIDE 调试会话），结束它或按 `R` |
| `RTT stream connected` 了但标题速率一直 0 | 固件没跑到 `VOFA_RTT_Init()` 或内核被 halt：按 `R` 重连（会放行 halt 的内核），或设 `RESET=1` |
| OpenOCD 反复 `rtt: No control block found` | 搜索范围没覆盖 `_SEGGER_RTT`，按 `.map` 改 `RAM_START`/`RAM_SIZE`（脚本每秒自动重试，范围对了会自己找到） |
| 编译后没有自动下载 | 脚本不在工程目录（或其上层）、输出不是 `.axf` / `.elf`、`AUTOFLASH=0`、内容没变被跳过、脚本正处于暂停 / 让出状态 |
| 编译后下载了别的工程 | 目录下有多个工程的输出，用 `FIRMWARE` 指定 |
| J-Link 反复 `backend exited` | `DEVICE` 器件名写错，或目标没上电（上电后会自动连上） |
| 启动报 `Port 19021 is busy` | 已经开着一个 `start_rtt.bat`，或 J-Link RTT Viewer / J-Scope / Ozone 占着端口 |
| Keil F8 报找不到 CMSIS-DAP / ST-Link | 脚本占着探针：改用编译即下载，或按上面配成"Keil F8 交给脚本" |
| 偶尔一行灰色 `RTT stream closed, reconnecting` | OpenOCD 0.12 在 Windows 上偶发断开内部连接，脚本 0.1 秒内重连，数据留在目标缓冲里不丢，可忽略 |
| 挂 Keil 调试后波形停 | 调试器 halt 了内核，RTT 自然停止，全速运行后恢复 |
| 编译报 undefined reference | `SEGGER_RTT.c` 没加进编译 |

更完整的排查表见 `SKILL.md`。

## 设计要点

理解这几点能避免踩坑，改动前先看：

- **固件不依赖调试器**。固件侧只是 RAM 里一块以 `"SEGGER RTT"` 开头的控制块，
  任何能在内核运行时后台读内存的探针都能读，J-Link 和 OpenOCD 读的是同一块。
- **脚本自己占住 19021 做转发**。底层工具（J-Link Commander / OpenOCD）的 RTT 服务挪到内部端口
  `RTTPORT+10`（19031），脚本转发给 VOFA+。底层工具重启、探针拔插、让出探针时 VOFA+ 的连接都不断。
- **看门狗**：每 1.5 秒查一次探针是否在位（调用系统 `CM_Locate_DevNode`，约 0.1ms，不卡转发），
  拔掉就停底层工具，插回自动重启；底层工具意外退出 1 秒后重启。
- **一个 CMSIS-DAP 同时只能有一个程序在用**。像 Horco 这类同时有 v1(HID) 和 v2(WinUSB) 接口的探针，
  第二个程序会从 v1 接进来，两边的命令和应答搅在一起（实测会出现 `command mismatch`），下载时可能写坏 Flash。
  脚本的对策：自己的 OpenOCD 只走 v2（`cmsis_dap_backend usb_bulk`），v2 被占就重试、不退回 v1；
  启动前检查 HID 接口有没有被别人打开；运行中每 100ms 检查有没有别的 `openocd` / `pyocd` /
  `STM32_Programmer_CLI` 进程，以及日志里有没有串话迹象，有就立即让出，对方结束 2 秒后接回。
  J-Link 由 SEGGER 软件管理多程序共享，不走这套逻辑。
- **编译即下载**：用文件变更通知发现新的 `.axf` / `.elf`，再每 300ms 直接查文件的修改时间和大小
  （Windows 的"写入/大小变化"通知要等缓存刷盘，常常晚 10 秒左右）。
  文件 1 秒内没再变、链接器也已关闭文件后才下载；按内容哈希跳过没变化的重复编译。
- **下载先 `rtt stop` 再烧录**：烧录算法跑在目标 RAM 里，可能覆盖控制块；烧完复位后再重新搜控制块。
- **统一用 RTT 通道 0**。J-Link 的 RTT 端口固定绑 0 号上行通道；OpenOCD 也映射 0 号。
- **J-Link 连接后会 halt 内核**，脚本在 `connect` 后执行 `g` 放行；命令文件末尾 `sleep 86400000`
  保持连接（Commander 跑完命令文件会断连）。只有 `connect` 失败会让 Commander 退出（`ExitOnError`），
  由看门狗重试，所以目标后上电也能自动连上。
- **OpenOCD 每次 `rtt start` 只搜一次控制块**，脚本在没找到时每秒重试；OpenOCD 连接时不 halt，
  只在内核已经 halt 时才 `resume`。默认轮询 100ms 太慢，脚本改成 10ms。
- **走 JustFloat 二进制，不做浮点转字符串**：M0+ 无 FPU 且无硬件除法，float 转 ASCII
  单通道约 28us，是 `SEGGER_RTT_Write` 本身(约 1~2us)的几十倍，格式化才是真瓶颈。
  JustFloat 直接 memcpy 原始字节，**依赖小端序**（ARM Cortex-M 默认满足）。
- **脚本是 bat + PowerShell 混合的单文件，故意全 ASCII 无中文**：GBK 中文会导致 CMD 和
  PowerShell 5.1 解析错乱，别加中文。
- **脚本启动时会结束残留的 `JLink.exe` / `openocd.exe`**，会顺带结束 IDE 里正在跑的同名调试会话。
- **`SEGGER_RTT_Write()` 内部短暂关中断**（M0+ 走 `PRIMASK` 关全局中断），
  不要在高优先级硬实时中断里高频调用。
