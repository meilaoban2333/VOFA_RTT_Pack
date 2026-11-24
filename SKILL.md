---
name: vofa-rtt
description: 使用 SEGGER RTT 经 J-Link、CMSIS-DAP（DAPLink，v1/v2）或 ST-Link 向 VOFA+ 上位机推送实时波形。当用户要给 STM32/Cortex-M 工程加 RTT 调试、用 VOFA+ 看波形曲线、移植 RTT 到新工程、用 CMSIS-DAP/OpenOCD 读 RTT、边看波形边下载（编译即下载）、IDE 下载与 RTT 抢探针，或排查 RTT 无数据/端口占用/波形不刷新/断线不恢复时使用。覆盖 Keil MDK、EIDE、CMake 三种构建方式的配置改动。
---

# VOFA+ RTT 波形输出移植

通过 SEGGER RTT 经调试器把 MCU 内部变量以 JustFloat 二进制协议推给 VOFA+，无需占用串口，不阻塞主循环。
支持 J-Link、CMSIS-DAP（DAPLink，v1/v2）、ST-Link，换调试器不用改固件。
主机端只有一个脚本 `assets/scripts/start_rtt.bat`，自动识别调试器、断线自动重连、编译即下载。
运行环境：Windows 10/11 自带的 cmd + PowerShell 5.1；J-Link 需 J-Link 软件，CMSIS-DAP / ST-Link 需
OpenOCD 0.12+（EIDE 自带即可）；不需要 Python，不需要管理员权限。完整清单见 README 的"环境要求"。

## 数据通路

```
MCU 代码  --VOFA_RTT_Send()-->  RTT 0 号上行缓冲(RAM)
         --调试器经 SWD 后台读走-->  底层工具 RTT 服务(127.0.0.1:19031，内部端口)
         --start_rtt.bat 转发-->  127.0.0.1:19021  --TCP-->  VOFA+
```

底层工具由脚本按识别到的调试器选择，固件侧完全相同：

| 调试器 | 底层工具 | 说明 |
|--------|----------|------|
| J-Link | J-Link Commander（`-RTTTelnetPort 19031`） | 需装 J-Link 软件 |
| CMSIS-DAP v1/v2、ST-Link | OpenOCD 0.12+（`rtt server start 19031 0`） | EIDE 自带的即可 |

脚本自己占住 19021 转发，底层工具重启、探针拔插时 VOFA+ 连接不断。
关键约束：**统一用 RTT 0 号上行通道**，`vofa_rtt.h` 里 `VOFA_RTT_BUF_IDX` 不要改。

## 移植总览

移植 = 4 件事，缺一不可：

1. 拷源码 → `assets/RTT/` 六个文件进工程
2. 加编译 → 3 个 .c 加入构建，1 个头文件路径加入 include
3. 改脚本参数 → `start_rtt.bat` 拷到工程根目录，填器件名 / OpenOCD 目标 / RAM 搜索范围
4. 调用 → `main()` 里 `VOFA_RTT_Init()` + 循环里发数据

用户用 CMSIS-DAP / ST-Link 且要边看波形边下载时，再做步骤 7。按下面步骤执行。

## 步骤 1：拷贝源码

把 `assets/RTT/` 下六个文件拷到目标工程，建议放 `User/RTT/`：

| 文件 | 作用 | 需要改吗 |
|------|------|----------|
| `SEGGER_RTT.c/.h` | SEGGER 官方 RTT 核心 | 不改 |
| `SEGGER_RTT_printf.c` | RTT 格式化输出 | 不改 |
| `SEGGER_RTT_Conf.h` | RTT 配置 | **可能要改**，见步骤 4 |
| `vofa_rtt.c/.h` | JustFloat 组帧封装层 | 按需改通道数 |

`vofa_rtt.c` 里 `#include "main.h"` 是为了拿 `HAL_GetTick()`。非 HAL 工程要把
`VOFA_RTT_TestLoop()` 里的 `HAL_GetTick()` 换成自己的毫秒计时源，或直接删掉这个测试函数。

## 步骤 2：加入构建

按目标工程的构建方式选一种。**三种方式的具体改动写在 `references/porting.md`**，
移植时读取该文件，里面有 Keil MDK / EIDE / CMake 的逐项配置和可直接粘贴的片段。

共同点，无论哪种方式都要做到：
- 3 个 `.c` 参与编译：`SEGGER_RTT.c`、`SEGGER_RTT_printf.c`、`vofa_rtt.c`
- RTT 目录加进头文件搜索路径

## 步骤 3：改脚本参数

把 `assets/scripts/start_rtt.bat` 拷到**工程根目录**（编译即下载监视的是脚本所在目录下的 `.axf` / `.elf`），
改开头的 `set` 行。调试器自动识别，按芯片填参数；用户只用一种调试器时，另一种的参数可以不管：

| 参数 | 默认 | 谁用 | 说明 |
|------|------|------|------|
| `PROBE` | `auto` | 全部 | `auto` 按 J-Link → ST-Link → CMSIS-DAP 识别；多种同时插着时写死 `jlink` / `cmsis-dap` / `stlink` |
| `DEVICE` | `STM32G070RB` | J-Link | SEGGER 器件名（去掉封装/温度后缀，如 `STM32F407VE`、`STM32H743XI`），写错连不上 |
| `TARGETCFG` | `target/stm32g0x.cfg` | CMSIS-DAP / ST-Link | OpenOCD `scripts/target/` 下的目标脚本，如 `target/stm32f1x.cfg`、`target/stm32f4x.cfg`、`target/stm32h7x.cfg` |
| `RAM_START` / `RAM_SIZE` | `0x20000000` / `0x9000` | CMSIS-DAP / ST-Link | 控制块搜索范围，**必须覆盖 `_SEGGER_RTT`**。默认是 G070 的 36KB SRAM，换芯片必改。最稳：按 `.map` 里 `_SEGGER_RTT` 的地址填 `RAM_START`，`RAM_SIZE` 填 `0x100`（查法见 `references/porting.md`） |
| `AUTOFLASH` | `1` | CMSIS-DAP / ST-Link | 1 = 编译即下载，0 = 关闭 |
| `FIRMWARE` | 空 | CMSIS-DAP / ST-Link | 空 = 监视脚本目录下所有 `.axf` / `.elf`；目录里有多个工程（如 bootloader + app）时填要下载的那个文件 |
| `RESET` | `0` | 全部 | 0 = 附着到正在跑的固件，重连不重启 MCU；1 = 每次连上都复位 |
| `SPEED` / `RTTPORT` | `4000` / `19021` | 全部 | SWD 速率 kHz / VOFA+ 端口，内部端口自动取 `RTTPORT+10` |
| `JLINK` / `OPENOCD` | 空 | 全部 | 工具路径，留空自动找（J-Link：`Program Files\SEGGER\JLink*`、EIDE 自带、PATH；OpenOCD：EIDE 自带、PATH） |

## 步骤 4：核对 RTT 配置

`SEGGER_RTT_Conf.h` 里本包已调整过的项：

```c
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS    (1)     // 只用 0 号上行
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS  (1)
#define BUFFER_SIZE_UP                   (2048)  // JustFloat 二进制帧很小，2K 余量充足
#define SEGGER_RTT_MODE_DEFAULT   SEGGER_RTT_MODE_NO_BLOCK_SKIP  // 缓冲满丢帧，不阻塞
```

需要按目标平台核对的：

- **RAM 紧张**（如 8KB 以下）：`BUFFER_SIZE_UP` 降到 512 或 256，代价是高速发送时丢帧。
- **多通道多**：`VOFA_CH_MAX`（在 `vofa_rtt.h`）默认 4，加通道时同步调大。
- **临界区实现**：`SEGGER_RTT_Conf.h` 按 `__ARM_ARCH_*` 宏自动选。Cortex-M0/M0+ 无 `BASEPRI`，
  走 `PRIMASK` 关全局中断；M3/M4/M7 走 `BASEPRI`。ARMCC v5(`__CC_ARM`) 有单独分支。
  用标准 ARM 工具链一般不用动。

## 步骤 5：调用

`main.c`：

```c
#include "vofa_rtt.h"   // 放 USER CODE BEGIN Includes

int main(void)
{
    /* USER CODE BEGIN 1 */
    VOFA_RTT_Init();    // 必须在外设和中断启动前，保证调试器搜索时控制块已就绪
    /* USER CODE END 1 */

    HAL_Init();
    /* ... 外设初始化 ... */

    while (1)
    {
        VOFA_RTT_TestLoop();       // 自检用：发正弦+余弦，验证通了就换成下面的真实变量
        // VOFA_RTT_Send2(实际变量1, 实际变量2);
    }
}
```

可用接口：

```c
void VOFA_RTT_Init(void);                              // 初始化，main 起始调用
void VOFA_RTT_Send1(float ch0);                        // 单通道
void VOFA_RTT_Send2(float ch0, float ch1);             // 双通道
void VOFA_RTT_Send(const float *data, unsigned ch_num); // N 通道，ch_num <= VOFA_CH_MAX
void VOFA_RTT_TestLoop(void);                          // 正弦波自检，验证完可删
```

**发送频率必须节流。** 主循环裸跑不节流会以循环速度灌满缓冲导致丢帧和波形失真。
参考 `VOFA_RTT_TestLoop()` 里的 tick 节流写法，或放在固定周期的定时器回调里发。
JustFloat 单帧仅 4×通道数+4 字节，500Hz 以内都很轻松。

## 步骤 6：开服务 + VOFA+ 配置

双击 `start_rtt.bat`，就绪标志：`probe: ...` → `RTT stream connected`
（OpenOCD 下还有 `RTT Control block found at 0x...`）。窗口标题实时显示状态和 KB/s。

VOFA+ 里：

| 项 | 值 |
|----|-----|
| 数据接口 | TCP 客户端 |
| 服务器 IP | 127.0.0.1 |
| 网络端口 | 19021 |
| 握手数据 | 留空 |
| 数据格式 | **JustFloat** |

JustFloat 帧格式：N 个 4 字节小端 float + 4 字节帧尾 `00 00 80 7F`，帧内字节数除以 4 即通道数。

脚本窗口按键：`P` 暂停并释放探针（给 Keil 调试用）/ 再按恢复，`R` 立即重连，`Q` 退出。
探针拔插后自动重连，不用重开脚本。

## 步骤 7：边看波形边下载

J-Link 不用做这一步：SEGGER 软件允许 IDE 和脚本同时用一个 J-Link，照常下载。

CMSIS-DAP / ST-Link 同一时间只能有一个程序在用探针。**两个程序同时用会让命令和应答串话**
（实测 `CMSIS-DAP command mismatch`），下载中可能写坏 Flash。按推荐顺序：

1. **编译即下载（默认开启，推荐）**：什么都不用配。Keil 按 F7 / EIDE 点 Build，脚本发现新的
   `.axf` / `.elf` 写完后，经自己的 OpenOCD 下载（先 `rtt stop`，烧完复位后重新找控制块），波形接着推。
   内容没变的重复编译自动跳过。要告诉用户：**改完代码按 F7 就行，不要再按 F8**。
2. **IDE 自己的下载按钮（EIDE）**：可以照常点。脚本每 100ms 检查有没有别的 `openocd` / `pyocd` /
   `STM32_Programmer_CLI` 进程，有就让出探针，对方结束 2 秒后接回。对方启动的一瞬间偶尔会报一次错，重试即可。
3. **Keil F8 交给脚本**：`Options for Target` → `Utilities` → `Use External Tool for Flash Programming`，
   `Command` 填 `start_rtt.bat` 完整路径，`Arguments` 填 `flash "#L"`，不勾 `Run Independent`。

**不要**在脚本运行时用 Keil 自带的 CMSIS-DAP 驱动按 F8：Keil 不起单独进程，脚本只能在通信被打断后才让出。
Keil 调试（Ctrl+F5）仍走 Keil 自己的驱动，调试前在脚本窗口按 `P`。

## 故障排查

按现象查，**不要跳过第一条**——绝大多数"没波形"都是内核被 halt 或固件没跑到初始化：

| 现象 | 原因 | 处理 |
|------|------|------|
| `RTT stream connected` 但标题速率一直 0 | 内核 halt 或 `main()` 没跑到 `VOFA_RTT_Init()` | 按 `R` 重连（会放行 halt 的内核），或设 `RESET=1` |
| 一直 `waiting for a probe` | 探针没插好 / 驱动没装 / 被识别成别的类型 | 检查设备管理器；多种探针同时插着时用 `PROBE` 写死 |
| `probe is in use by another program, waiting` | Keil 调试等程序开着探针的 HID 接口 | 等它释放，脚本自动接管 |
| `openocd (pid ...) is using the probe` 后一直不恢复 | 有常驻的 OpenOCD（如 EIDE 调试会话） | 结束它，或按 `R` |
| 编译后没自动下载 | 脚本不在工程目录、输出不是 `.axf`/`.elf`、`AUTOFLASH=0`、内容没变被跳过、脚本处于暂停/让出 | 逐项核对；多工程目录用 `FIRMWARE` 指定 |
| OpenOCD 反复 `rtt: No control block found` | 搜索范围没覆盖 `_SEGGER_RTT` | 按 `.map` 改 `RAM_START`/`RAM_SIZE`；脚本每秒自动重试 |
| J-Link 反复 `backend exited` | `DEVICE` 写错，或目标没上电 | 改 `DEVICE`；目标上电后自动连上 |
| OpenOCD 反复 `backend exited` | `TARGETCFG` 不对、目标没上电、探针被别的程序占用 | 看上方 OpenOCD 红色报错；先停掉 IDE 调试会话 |
| 启动报 `Port 19021 is busy` | 已有一个 `start_rtt.bat`，或 RTT Viewer / J-Scope / Ozone 占着 | 关掉它们 |
| Keil F8 报找不到 CMSIS-DAP / ST-Link | 脚本占着探针，Keil 自带下载驱动打不开 | 改按 F7 编译即下载，或按步骤 7 配外部工具 |
| Keil 调试连不上探针 | 脚本占着探针 | 脚本窗口按 `P` 释放 |
| 偶尔一行灰色 `RTT stream closed, reconnecting` | OpenOCD 0.12 在 Windows 上偶发断开内部连接 | 可忽略：0.1 秒内重连，数据留在目标缓冲不丢 |
| 波形卡顿丢点 | 发送太快缓冲溢出 | 降低发送频率，或调大 `BUFFER_SIZE_UP` |
| 挂 Keil 调试后波形停 | 调试器 halt 了内核 | 全速运行后恢复；流式采集时少打断点 |
| `flash` 报 `J-Link found` | J-Link 不需要外部工具下载 | Keil 用回 J-Link 驱动即可 |

## 注意事项

- `SEGGER_RTT_Write()` 内部会短暂关中断（M0+ 走 `PRIMASK` 关全局中断），
  **不要在高优先级硬实时中断里高频调用**，会影响中断响应。
- `vofa_rtt.c` 走 JustFloat 二进制，**故意不做任何浮点转字符串**：Cortex-M0/M0+ 无 FPU
  且无硬件除法，float 转 ASCII 要走软件模拟的浮点加除法，实测单通道约 28us，是
  `SEGGER_RTT_Write` 本身(memcpy 约 1~2us)的几十倍——格式化而非传输才是瓶颈。
  想改回文本协议(FireWater)前先想清楚这个代价，另注意 `SEGGER_RTT_printf` 也不支持
  `%f`（只有 `c/d/u/x/s/p`）。
- JustFloat 直接把 float 内存字节发出去，**依赖目标是小端序**。ARM Cortex-M 默认小端，
  移植到大端平台需要逐通道字节翻转。
- `start_rtt.bat` 是 bat + PowerShell 5.1 混合单文件，**故意全 ASCII 无中文**：GBK 中文会导致 CMD
  和 PowerShell 5.1 解析错乱，改脚本时别加中文。
- 脚本启动时会结束残留的 `JLink.exe` / `openocd.exe`，**会顺带结束 IDE 里正在跑的同名调试会话**。
- 验证情况：CMSIS-DAP v2 + STM32G070 已实测（识别、波形、编译即下载〔模拟编译输出〕、
  另一个 OpenOCD 下载时让出并接回、`flash` 模式、拔插恢复）；J-Link、ST-Link、仅 v1 的 CMSIS-DAP
  走本脚本，以及 Keil / EIDE 里真实编译触发下载，尚未实测，遇到问题按上表排查。
