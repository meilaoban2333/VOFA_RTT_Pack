# VOFA_RTT_Pack 详细手册

**中文** | [English](manual.en.md)

[README](../README.md) 只讲怎么上手，这里是完整说明：环境与验证情况、全部配置项、接口、底层配置、
可选功能、完整排查表和设计要点。Keil / EIDE / CMake 的构建配置见 [porting.md](porting.md)。

目录：

- 环境要求
- 验证情况
- 目录结构
- 配置项一览
- 选哪个协议
- 人工移植五步
- 边看波形边下载
- 脚本的命令行与固定参数
- 接口
- RTT 底层配置（SEGGER_RTT_Conf.h）
- LOG() 文本打印（仅 FireWater）
- 可选：下行命令动态开关观察项
- 常见问题
- 主机测试
- 设计要点

## 环境要求

| 项 | 要求 | 说明 |
|----|------|------|
| 操作系统 | Windows 10 / 11 | 脚本用到 cmd 和系统自带的 Windows PowerShell 5.1，**不需要管理员权限** |
| 上位机 | VOFA+ | TCP 客户端，数据格式选 FireWater 或 JustFloat（与固件的 `VOFA_PROTOCOL` 一致） |
| J-Link 用户 | SEGGER J-Link Software（已用 V8.64）或 EIDE 自带的 J-Link | 脚本要用到 `JLink.exe` |
| CMSIS-DAP / ST-Link 用户 | OpenOCD 0.12 及以上 | EIDE 自带的 0.12.0-rc2 已实测，装了 EIDE 就不用另装；也可以把别的 OpenOCD 放进 PATH 或填 `OPENOCD` |
| 探针驱动 | CMSIS-DAP v2：WinUSB；CMSIS-DAP v1：HID 免驱；ST-Link：ST 官方 ST-LINK USB 驱动（STSW-LINK009）；J-Link：随 J-Link 软件安装 | 多数 CMSIS-DAP v2 探针插上会自动装好 WinUSB，设备管理器里没有就用 Zadig 给它装 WinUSB |
| 编译环境 | 任意：Keil MDK（AC5/AC6）、EIDE（GCC/AC6）、CMake / Makefile + arm-none-eabi-gcc | 固件侧是纯 C |
| 不需要 | Python、pyOCD、额外的 PowerShell 模块 | |
| 占用端口 | 19021（给 VOFA+）、19031（内部）、OpenOCD 的 3333 / 4444 / 6666 | 19021 冲突时改 `RTTPORT` |

## 验证情况

| 组合 | 状态 |
|------|------|
| STM32F407VE（Cortex-M4）、STM32G070RB（Cortex-M0+）+ Keil AC6 + J-Link V864（旧版单工具脚本） | 已实测 |
| 固件组帧、丢帧返回值、下行命令解析 | 主机测试覆盖，见「主机测试」。FireWater 定点转换已改为移位加法除以 10（不调库除法，经全部 2^32 输入穷举验证），并修正了 2^23 以上奇数整数会多 1 的舍入问题；主机测试覆盖，尚未重新上板 |
| STM32G070RB + Horco CMSIS-DAP v2 + 本脚本 | 已实测：自动识别；100 / 200 帧每秒无坏帧；编译即下载（含内容没变时跳过）；另一个 OpenOCD 真实烧录时自动让出并接回；`flash` 模式；拔插探针、杀后台进程后自动恢复；全程 VOFA+ 侧不断开。测试固件用的是 JustFloat，脚本只原样转发字节，与协议无关。编译即下载是用"写出 `.axf` / `.elf`"模拟编译测的，没在 Keil / EIDE 里真实编译 |
| J-Link + 本脚本 | 未实测 |
| ST-Link、只有 v1(HID) 的 CMSIS-DAP + 本脚本 | 未实测 |
| 下行命令（`AT+...`）经本脚本转发到 MCU | 未实测 |
| Keil 自带 CMSIS-DAP 驱动按 F8（脚本运行中） | 未实测，不推荐，见"边看波形边下载" |
| Keil `Utilities` 外部工具、EIDE `Custom` 烧录器的界面配置 | 未在 IDE 界面里实测（命令行已按 IDE 的调用方式实测） |

## 目录结构

```
VOFA_RTT_Pack/
├── SKILL.md                 # Claude Code 技能入口，让 AI 按流程自动移植
├── README.md                # 上手说明
├── README.en.md             # 英文版 README
├── assets/
│   ├── RTT/                 # 直接拷进工程的六个源文件
│   │   ├── SEGGER_RTT.c/.h
│   │   ├── SEGGER_RTT_Conf.h
│   │   ├── SEGGER_RTT_printf.c   # 已补 %f 支持（SEGGER 原版不支持），已修返回值
│   │   └── vofa_rtt.c/.h         # 双协议组帧封装层 + LOG() 宏，协议宏也在这
│   ├── examples/
│   │   └── log_demo.c/.h    # 可选：下行 AT 命令动态开关观察项（只需在自己文件里写变量表）
│   └── scripts/
│       └── start_rtt.bat    # 唯一的主机端脚本
├── references/
│   ├── manual.md            # 详细手册（本文件），英文版 manual.en.md
│   └── porting.md           # Keil / EIDE / CMake 构建配置、下载配置
└── tests/                   # 主机端回归测试，改固件后在 PC 上跑，不用上板
```

## 配置项一览

| 改哪里 | 管什么 | 什么时候要改 | 详见 |
|--------|--------|--------------|------|
| `start_rtt.bat` 开头的 `set` 行 | 调试器类型、芯片、OpenOCD 目标与 RAM 范围、编译即下载、端口、复位、工具路径 | 每个新工程都要核对 | 第 3 步 |
| `vofa_rtt.h` | 协议、最大通道数、小数位、自检波形、毫秒时基 | 换协议、加通道、非 HAL 工程 | 「接口」 |
| `SEGGER_RTT_Conf.h` | 缓冲区大小、通道数、缓冲满时的处理方式、临界区屏蔽优先级、内存段 | RAM 紧张、用 RTOS、H7 等带缓存的芯片 | 「RTT 底层配置」 |
| 自己工程里的 `LogItem` 表（可选） | 要观察的变量、类型、通道数；`log_demo.c/.h` 本身不用改 | 用下行命令时 | 「可选：下行命令」 |
| VOFA+ 界面 | 数据接口、IP、端口、数据格式 | 每次连接 | 第 5 步 |

脚本里还有一些写死的参数（内部端口、各种检测间隔等），见「脚本的命令行与固定参数」。

## 选哪个协议

在 `vofa_rtt.h` 改这一行：

```c
#define VOFA_PROTOCOL   VOFA_PROTO_FIREWATER   // 默认
// #define VOFA_PROTOCOL   VOFA_PROTO_JUSTFLOAT
```

**改完必须同步改 VOFA+ 上位机的"数据格式"**，两边不一致就没波形。`start_rtt.bat` 只原样转发字节，
两种协议都不用改脚本。

| | FireWater（默认） | JustFloat |
|---|---|---|
| 帧格式 | `12.345,-6.789\n` | `[f0:4B][f1:4B][00 00 80 7F]` |
| 双通道单帧 | 约 20 字节 | 12 字节 |
| 发送端开销 | float→ASCII 定点转换 | 仅 memcpy |
| 用文本工具直接可读 | ✅ 能 | ❌ 乱码 |
| `LOG()` 文本打印 | ✅ 可用（会污染波形，需择一） | ❌ 编译期禁用 |
| 数值精度 | 3 位小数（`VOFA_DECIMALS` 可调） | 完整 float |
| 超大数值 | 3 位小数时超过约 429 万自动去掉小数，超过 ±4294967295 饱和 | 无限制 |
| 依赖小端序 | 否 | 是（ARM Cortex-M 满足） |

**建议：先用 FireWater 调通，确认数值正确，再按需切 JustFloat。**
FireWater 的数据用任意 TCP 调试工具连 `127.0.0.1:19021` 就能肉眼看懂（J-Link 下也可以用 RTT Viewer），
出问题时能立刻分清是发送端没发对还是上位机配错了；JustFloat 一片二进制，排查全靠猜。

**该切 JustFloat 的三种情况**：采样率到 kHz 级；目标是 Cortex-M0/M0+（无 FPU 且
无硬件除法，float 转 ASCII 单通道实测约 28us，是 RTT 写入本身的几十倍；28us 是旧实现测的，
现已改为每位一次除法、除法用移位加法实现，不再调用库除法，会快不少，尚未重新实测）；
需要完整 float 精度或数值超出定点量程。

## 人工移植五步

### 1. 拷源码

`assets/RTT/` 六个文件 → 目标工程的 `User/RTT/`（路径可自定）。

需要运行时开关观察项的，再拷 `assets/examples/log_demo.c/.h`，见「可选：下行命令」。

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
| `DEVICE` | `STM32G070RB` | J-Link | SEGGER 器件名，一般是去掉封装/温度后缀的型号，**改成自己的芯片**（如 F407VET6 填 `STM32F407VE`）。默认值与下面 OpenOCD 的几项是同一颗 G070。写错连不上目标 |
| `TARGETCFG` | `target/stm32g0x.cfg` | CMSIS-DAP / ST-Link | OpenOCD 目标脚本，默认值对应 STM32G070RB，**改成自己的芯片**，如 `target/stm32f4x.cfg` |
| `RAM_START` / `RAM_SIZE` | `0x20000000` / `0x9000` | CMSIS-DAP / ST-Link | 控制块搜索范围，必须覆盖 `_SEGGER_RTT`。默认是 G070 的 36KB SRAM（F407VE 用 `0x20000000` / `0x20000`）。最稳：从 `.map` 查 `_SEGGER_RTT` 地址填 `RAM_START`，`RAM_SIZE` 填 `0x100` |
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

非 HAL 工程把 `vofa_rtt.h` 末尾的 `VOFA_TICK_HEADER`（默认 `"main.h"`）和 `VOFA_GET_TICK_MS()`
（默认 `HAL_GetTick()`）两行改成自己的头文件和毫秒计时函数，`vofa_rtt.c` 不用动。
这两行只给自检函数 `VOFA_RTT_TestLoop()` 用。

**发送必须节流**，主循环裸跑不节流会灌满缓冲导致丢帧。参考 `TestLoop` 的 tick
节流写法，或放固定周期定时器回调里。单帧大小：FireWater 约 (小数位+7)×通道数 字节，
JustFloat 是 4×通道数+4 字节。FireWater 几百 Hz 以内没问题，要更高就切 JustFloat。

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
| 数据格式 | **与 `VOFA_PROTOCOL` 一致**（FireWater 或 JustFloat） |

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

## 脚本的命令行与固定参数

两种运行方式：

| 用法 | 作用 |
|------|------|
| 双击，或命令行 `start_rtt.bat` | 启动 RTT 服务（自动识别调试器、转发、看门狗、编译即下载） |
| `start_rtt.bat flash <固件文件>` | 下载一个固件。支持 `.axf` / `.elf` / `.hex`；`.bin` 不带地址，不支持。脚本正在运行时经它下载、RTT 接着推；没运行时临时起一个 OpenOCD 下载完退出。成功返回 0、失败返回 1（Keil 外部工具据此判断成败）。J-Link 下不支持，直接用 IDE 下载 |

服务窗口里的按键：`P` 暂停并释放探针 / 再按恢复，`R` 立即重连，`Q` 退出。

下面这些是写死在脚本后半段（PowerShell 部分）里的，一般不用动，要改就改那里：

| 项 | 值 | 说明 |
|----|-----|------|
| 内部端口 | `RTTPORT+10`（默认 19031） | 底层工具的 RTT 服务，脚本转发到 `RTTPORT` |
| OpenOCD 端口 | GDB 3333、telnet 4444、Tcl 6666 | `flash` 模式经 Tcl 6666 找正在运行的脚本 |
| 看门狗间隔 | 1.5 秒 | 检查探针是否在位、底层工具是否退出 |
| 底层工具重启等待 | 1 秒 | 底层工具意外退出后多久重启 |
| 让出检测 / 接回等待 | 100 毫秒 / 2 秒 | 多久查一次有没有别的下载工具；对方结束后空闲多久再接回 |
| 控制块重试 | 1 秒 | OpenOCD 没找到控制块时多久重试一次 |
| OpenOCD RTT 轮询 | 10 毫秒 | OpenOCD 自身默认 100 毫秒太慢 |
| 编译输出稳定时间 | 1 秒 | `.axf` / `.elf` 1 秒内没再变化才下载 |
| 下载记录 | `%TEMP%\start_rtt_<RTTPORT>.last` | 最后一次经脚本烧进去的固件哈希，用来跳过重复下载 |
| 底层工具日志 | `%TEMP%\start_rtt_<进程号>.*.log` | 脚本退出时删除 |

**一台电脑同时只能开一个 `start_rtt.bat`**：启动时会结束其他 `JLink.exe` / `openocd.exe`，
OpenOCD 的 Tcl 端口也是固定的，所以不支持同时接多块板各开一个脚本。

## 接口

```c
void VOFA_RTT_Init(void);                                   // 初始化
unsigned VOFA_RTT_Send1(float ch0);                         // 单通道
unsigned VOFA_RTT_Send2(float ch0, float ch1);              // 双通道
unsigned VOFA_RTT_Send(const float *data, unsigned ch_num); // N 通道
VOFA_SEND(a, b, c);                                         // 任意个通道一行发，不用建数组（需 C99）
void VOFA_RTT_TestLoop(void);                               // 自检正弦波，可删

unsigned VOFA_RTT_SendRaw(const void *buf, unsigned len);   // 原样发字节，不加帧格式
unsigned VOFA_RTT_SendStr(const char *s);                   // 发字符串（仅 FireWater）
LOG("fmt", ...);                                            // printf 风格文本打印（仅 FireWater）
```

这套接口**两种协议下完全一致**，切协议不用改任何调用代码。
`SendStr` 和 `LOG` 在 JustFloat 下不发送任何内容，原因见下方 LOG 一节。

发送接口都返回写入的字节数，**0 表示这一帧被丢弃**（缓冲满或参数无效），可以用来统计丢帧；不关心就忽略。

`VOFA_SEND(...)` 的参数按 float 转换（`int`、`double` 都行），每个只求值一次；参数个数超过 `VOFA_CH_MAX`
时直接编译报错，不会等到运行时整帧丢弃。它依赖 C99 复合字面量：Keil AC5 要勾选
`Options for Target` → `C/C++` → `C99 Mode`，否则这个宏不存在；AC6 / GCC 默认即可。

`vofa_rtt.h` 里可调：

| 宏 | 默认 | 说明 |
|----|------|------|
| `VOFA_PROTOCOL` | `VOFA_PROTO_FIREWATER` | 协议选择，改完记得同步改 VOFA+ 设置 |
| `VOFA_CH_MAX` | 8 | 最大通道数，**超过会整帧丢弃**，加通道要同步调大 |
| `VOFA_DECIMALS` | 3 | 每通道小数位数，调大帧变长。**仅 FireWater 用到** |
| `VOFA_TEST_PERIOD_MS` | 10 | 自检波形周期，即采样率 100Hz |
| `VOFA_SINE_FREQ_HZ` / `VOFA_SINE_AMP` | 1.0 / 100 | 自检正弦的频率和幅值 |
| `VOFA_TICK_HEADER` / `VOFA_GET_TICK_MS()` | `"main.h"` / `HAL_GetTick()` | 自检波形节流用的毫秒时基（返回 `uint32_t`，允许回绕）。非 HAL 工程改成自己的头文件和函数 |

另有两个宏**不要改**：`VOFA_RTT_BUF_IDX`（固定 0，脚本只转发 0 号通道）、`VOFA_NUM_MAX`（由 `VOFA_DECIMALS` 自动算出）。

`vofa_rtt.h` 里的宏没有 `#ifndef` 保护，**只能直接改文件**，不能在工程的预定义宏里覆盖。

## RTT 底层配置（SEGGER_RTT_Conf.h）

这些项都包在 `#ifndef` 里，可以直接改文件，也可以在工程的预定义宏里定义来覆盖
（Keil：`Options for Target` → `C/C++` → `Define`；GCC：`-DBUFFER_SIZE_UP=4096`），这样以后换新版文件也不丢配置。

| 宏 | 本包默认 | 说明 |
|----|----------|------|
| `BUFFER_SIZE_UP` | 2048 | 0 号上行缓冲，波形和 `LOG()` 都走这里。RAM 紧张可降到 512 / 256，代价是高速发送时更容易丢帧；CMSIS-DAP 读得比 J-Link 慢，高采样率多通道时可以调大 |
| `BUFFER_SIZE_DOWN` | 16 | 0 号下行缓冲，收下行命令。命令比它长也没关系，`log_demo` 会跨次拼行 |
| `SEGGER_RTT_MAX_NUM_UP_BUFFERS` | 1 | 上行通道数，只用 0 号。想把文本挪到 1 号通道时改成 2（脚本只转发 0 号，1 号要用 J-Link RTT Viewer 看） |
| `SEGGER_RTT_MAX_NUM_DOWN_BUFFERS` | 1 | 下行通道数，只用 0 号 |
| `SEGGER_RTT_PRINTF_BUFFER_SIZE` | 128 | `LOG()` 用的栈上缓冲（SEGGER 原默认 64）。单行不超过它就一次写入，中断里的波形帧插不进行中间；超过则分段发送。调大更稳，但每次 `LOG()` 占更多栈 |
| `SEGGER_RTT_MODE_DEFAULT` | `SEGGER_RTT_MODE_NO_BLOCK_SKIP` | 缓冲满时怎么办：`NO_BLOCK_SKIP` 整帧丢弃（推荐）；`NO_BLOCK_TRIM` 能写多少写多少，会产生半帧，波形协议别用；`BLOCK_IF_FIFO_FULL` 等到有空间，没开脚本时会卡死主循环。**注意 `VOFA_RTT_Init()` 会把 0 号通道强制设成 `NO_BLOCK_SKIP`，改这个宏对 0 号不起作用**，真要改得改 `vofa_rtt.c` 里那一行 |
| `SEGGER_RTT_MAX_INTERRUPT_PRIORITY` | 0x20 | 仅 M3/M4/M7/M33：写 RTT 时用 `BASEPRI` 屏蔽优先级寄存器值 ≥ 它的中断（STM32 的 4 位优先级下即优先级 2~15）。优先级 0、1 的中断不会被屏蔽，**这些中断里不要调用 RTT**。用 FreeRTOS 时一般设成 `configMAX_SYSCALL_INTERRUPT_PRIORITY`。M0/M0+ 没有这项，直接关全局中断 |
| `SEGGER_RTT_MEMCPY_USE_BYTELOOP` | 0 | 1 = 用逐字节拷贝代替 `memcpy`，数据量很小时略快，一般不用动 |
| `SEGGER_RTT_SECTION` / `SEGGER_RTT_BUFFER_SECTION` | 未定义 | 把控制块 / 缓冲放进指定的链接段。用途：固定 `_SEGGER_RTT` 的地址（OpenOCD 的 `RAM_START` 就能填死）；H7 等带 D-Cache 的芯片放进 DTCM 或不可缓存区，避免调试器读到 Cache 里没写回的旧数据 |
| `SEGGER_RTT_CPU_CACHE_LINE_SIZE` / `SEGGER_RTT_UNCACHED_OFF` | 注释掉 | 面向 Cortex-A 等带缓存的系统，Cortex-M 一般不用 |

## LOG() 文本打印（仅 FireWater）

用法与 `printf` 完全一致，走 RTT 0 号通道输出。用任意 TCP 调试工具连 `127.0.0.1:19021` 即可看到
（J-Link 下也可以用 RTT Viewer / J-Link Commander 终端）：

```c
LOG("id=%d speed=%.2f flag=%s\n", id, speed, ok ? "OK" : "NG");
```

支持 `%c %d %u %x %X %s %p %f %F %%`，含 `-` `0` `+` `#` 标志、字段宽度和精度。

**`%f` 是本包给 `SEGGER_RTT_printf` 补的**，SEGGER 原版不支持。定点实现，
默认 6 位小数，量程 ±4294967295，超出或 NaN 打印 `Inf`/`NaN` 标记而不是静默回绕。

单条输出不超过 128 字节（`SEGGER_RTT_PRINTF_BUFFER_SIZE`）时一次写入 RTT：中断里发的波形帧
插不进这一行中间，缓冲满时整条丢弃而不是只写半截。更长的行会分段写入，失去这个保证。
`LOG()` 返回实际写入的字节数，缓冲满有内容被丢弃时返回 -1（SEGGER 原版返回值会把最后一段重复计数，本包已修）。

不需要格式化的固定文本用 `VOFA_RTT_SendStr("motor start\n")`，整串一次写入、没有格式化开销，
规则与 `LOG()` 相同（仅 FireWater）。要发自定义二进制内容用 `VOFA_RTT_SendRaw(buf, len)`，
两种协议都可用，但 JustFloat 下必须自己保证写的是完整帧。

⚠️ **FireWater 下 LOG 与波形共用 0 号通道**，混进来的文本会被当成数据行，
波形上表现为乱跳的点。**采集波形期间不要开 LOG，两者择一。**

⚠️ **JustFloat 下 `LOG()` 被编译期禁用**，展开成 `((void)0)`。因为二进制流里混入
文本会让 VOFA+ 把文本字节当 float 解析，或凑巧撞上帧尾 `00 00 80 7F` 导致此后
所有通道错位——比 FireWater 的脏数据严重得多，无法靠"小心使用"规避。

调用点不用加 `#if`，但注意**参数也不会求值**：`LOG("%d", i++)` 在 JustFloat 下
不会执行 `i++`，**别把副作用写进 LOG 参数**。JustFloat 下要文本输出，就临时切回
FireWater 重新编译，或自行改用 RTT 1 号通道（需把 `SEGGER_RTT_MAX_NUM_UP_BUFFERS`
调到 2，J-Link 下用 RTT Viewer 看 Terminal 1；`start_rtt.bat` 只转发 0 号，VOFA+ 收不到 1 号）。

## 可选：下行命令动态开关观察项

`assets/examples/log_demo.c/.h` 让你把要观察的变量登记成一张表，运行时发命令开关，
不用改代码重烧就能换看哪几组波形。`log_demo.c/.h` 本身不用改，表写在自己的文件里。
命令从 VOFA+ 的发送框发出，经 `start_rtt.bat` 转发到 RTT 下行通道（J-Link 下也可以用 RTT Viewer 输入行）。

```c
#include "log_demo.h"

static const LogItem s_logItems[] = {
    LOG_F32(g_angle, 1),     // 1 号：电角度
    LOG_F32(g_iabc,  3),     // 2 号：三相电流（数组前 3 个元素）
    LOG_U16(g_adcVbus, 1),   // 3 号：母线 ADC 原始值，发送时转成 float
    LOG_I32(g_speed, 2),     // 4 号：数组前 2 个元素
};

/* 可选：处理非开关类命令，line 以 '\0' 结尾、已去掉换行 */
static void MyCmd(const char *line, unsigned len) { /* 自己解析，如 AT+KP=1.5 */ }

int main(void)
{
    VOFA_RTT_Init();
    LogInit(s_logItems, sizeof(s_logItems) / sizeof(s_logItems[0]));
    LogSetCmdHook(MyCmd);
    for (;;) {
        /* 每 20~50ms 调一次 */
        LogTask();
    }
}
```

编号按表顺序自动分配，第 1 行是 1 号；一帧里的通道顺序就是表顺序。命令：

```
AT+1\n      切换 1 号项
AT+1,2\n    同时切换 1、2 号，都开就拼在同一帧里打印
AT+1\n      再发一次同号即关闭（每号独立翻转）
AT+0\n      全部关闭
AT+KP=1.5\n 不是上面的格式，交给 LogSetCmdHook 注册的回调（没注册就丢弃）
```

| 项 | 位置 | 默认 | 说明 |
|----|------|------|------|
| `LOG_F32` / `LOG_I32` / `LOG_U32` / `LOG_I16` / `LOG_U16` / `LOG_I8` / `LOG_U8` | `log_demo.h` | — | 表项宏 `(变量, 通道数)`，变量可以是单个变量或数组。整数发送时转成 float。**编译期检查**：通道数为 0、超过 `VOFA_CH_MAX`、或超出变量大小（如 `short` 变量用了 `LOG_F32`）时编译报错 |
| `LOG_ITEM_MAX` | `log_demo.h` | 64 | 最多登记多少项，超出的表项被忽略；每项开关占 1 bit，可在编译选项里改 |
| `LOG_LINE_MAX` | `log_demo.h` | 32 | 一条命令最多多少字符，超长的整条命令作废（回调也受此限制） |
| 通道上限 | — | `VOFA_CH_MAX` | 打开某项后一帧总通道数会超过 `VOFA_CH_MAX` 时，这次打开不生效。所以表里可以登记远多于 `VOFA_CH_MAX` 的项，按需开一部分 |
| 命令格式 | `log_demo.c` | `AT+` | `AT+` 后只含数字、逗号、空格（至少一个数字）才算开关命令，以回车或换行结束；其余非空行都交给回调 |

注意：

- 绝对值超过 16777216 的 32 位整数转 float 会丢精度（VOFA+ 只认 float）。
- 编译期检查只看大小，**拦不住指针和 `double`**：`LOG_F32` 用在指针或 `double` 变量上能编过，但发出去的数是错的。
  跨文件引用的数组，`extern` 声明里要带长度（如 `extern float g_iabc[3];`），否则编译报错。
- 登记的变量要自然对齐（普通全局变量都满足）；packed 结构体成员不行，M0/M0+ 上非对齐访问会 HardFault。
- 不回显开关状态：上行是纯数据流，混文本会破坏波形。
- `LogTask()` 放在定时器中断里、`LogInit()` 在主循环里调用（包括运行中换表）是安全的。
- **从旧版模板升级**：旧版的 `LOG_ID_xxx`、`LogPrint()`、`LOG_CH_MAX` 已删除。用新的 `log_demo.c/.h` 覆盖后，
  要在自己文件里写表并调 `LogInit()`；只调 `LogTask()` 的老代码照样能编过，但不会再有任何输出。

**两种协议都能用，本文件不用改**：输出调的是 `VOFA_RTT_Send()`，帧格式自动跟随
`VOFA_PROTOCOL`；命令走下行通道，与上行波形协议无关，JustFloat 下 AT 命令照常好使。

## 常见问题

| 现象 | 处理 |
|------|------|
| 一直 `waiting for a probe` | 探针没插好或驱动没装；同时插了多种探针时用 `PROBE` 写死 |
| `probe is in use by another program, waiting` | 别的程序（如 Keil 调试）占着探针，它释放后脚本自动接管 |
| `openocd (pid ...) is using the probe` | 正常：IDE 正在下载，下载完自动恢复。一直不恢复说明有常驻的 OpenOCD（如 EIDE 调试会话），结束它或按 `R` |
| `RTT stream connected` 了但标题速率一直 0 | 固件没跑到 `VOFA_RTT_Init()` 或内核被 halt：按 `R` 重连（会放行 halt 的内核），或设 `RESET=1` |
| OpenOCD 反复 `rtt: No control block found` | 搜索范围没覆盖 `_SEGGER_RTT`，按 `.map` 改 `RAM_START`/`RAM_SIZE`（脚本每秒自动重试，范围对了会自己找到） |
| **改协议宏后全无波形** | VOFA+ 的数据格式没跟着改，两边必须一致 |
| 波形全是乱跳巨值 / 通道错位 | 协议选反，或 JustFloat 下有文本混入数据流 |
| 波形里混入乱跳的点 | FireWater 下 `LOG()` 文本混进了数据流，采集时关掉 LOG |
| `LOG()` 完全没输出 | 当前是 JustFloat，LOG 被编译期禁用，这是预期行为 |
| 某帧完全没出现 | `ch_num > VOFA_CH_MAX` 整帧被丢，调大 `VOFA_CH_MAX`；缓冲满也会整帧丢，发送接口返回 0 即是被丢的帧 |
| 某通道一直是 0 | 源变量是 Inf/NaN（除零、未初始化等），两种协议都会把它发成 0，查这个变量的计算 |
| 数值精度不够 / 大数被截到上限 | 调大 `VOFA_DECIMALS`；或切 JustFloat 得完整 float |
| 发 AT 命令没反应 | 没调 `LogInit()` 登记表、没周期调 `LogTask()`，或发送时没带换行；号超出表的项数，或打开后会超 `VOFA_CH_MAX` 也不生效 |
| 编译后没有自动下载 | 脚本不在工程目录（或其上层）、输出不是 `.axf` / `.elf`、`AUTOFLASH=0`、内容没变被跳过、脚本正处于暂停 / 让出状态 |
| 编译后下载了别的工程 | 目录下有多个工程的输出，用 `FIRMWARE` 指定 |
| J-Link 反复 `backend exited` | `DEVICE` 器件名写错，或目标没上电（上电后会自动连上） |
| 启动报 `Port 19021 is busy` | 已经开着一个 `start_rtt.bat`，或 J-Link RTT Viewer / J-Scope / Ozone 占着端口 |
| Keil F8 报找不到 CMSIS-DAP / ST-Link | 脚本占着探针：改用编译即下载，或按上面配成"Keil F8 交给脚本" |
| 偶尔一行灰色 `RTT stream closed, reconnecting` | OpenOCD 0.12 在 Windows 上偶发断开内部连接，脚本 0.1 秒内重连，数据留在目标缓冲里不丢，可忽略 |
| 挂 Keil 调试后波形停 | 调试器 halt 了内核，RTT 自然停止，全速运行后恢复 |
| 编译报 undefined reference | `SEGGER_RTT.c` 没加进编译 |

更完整的排查表见 `SKILL.md`。

## 主机测试

改了 `vofa_rtt.c` / `log_demo.c` 后，可以先在 PC 上跑回归测试，不用上板：

```sh
sh tests/run_tests.sh
```

需要主机 gcc（Windows 上用 MinGW gcc + Git Bash）。脚本把 `assets/` 下的源码复制到临时目录，
按 FireWater 3 位、0 位、6 位小数和 JustFloat 四种配置各编译运行一次，直接读 RTT 上行缓冲比对输出字节；
另外检查 C90 兼容（模拟 Keil AC5 对声明位置的限制），以及 `VOFA_SEND` 参数超过 `VOFA_CH_MAX`、`LOG_xxx` 表项宏用错时必须编译报错。
最后一行是 `ALL PASSED` 即全部通过。

## 设计要点

理解这几点能避免踩坑，改动前先看：

- **固件不依赖调试器**。固件侧只是 RAM 里一块以 `"SEGGER RTT"` 开头的控制块，
  任何能在内核运行时后台读内存的探针都能读，J-Link 和 OpenOCD 读的是同一块。
- **脚本自己占住 19021 做转发**。底层工具（J-Link Commander / OpenOCD）的 RTT 服务挪到内部端口
  `RTTPORT+10`（19031），脚本转发给 VOFA+，VOFA+ 发来的命令也原样转给下行通道。
  底层工具重启、探针拔插、让出探针时 VOFA+ 的连接都不断。脚本不解析数据，与协议无关。
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
  文件 1 秒内没再变、链接器也已关闭文件后才下载；按内容哈希跳过已经烧进去的同一份固件。
- **下载先 `rtt stop` 再烧录**：烧录算法跑在目标 RAM 里，可能覆盖控制块；烧完复位后再重新搜控制块。
- **统一用 RTT 通道 0**。J-Link 的 RTT 端口固定绑 0 号上行通道；OpenOCD 也映射 0 号。
- **J-Link 连接后会 halt 内核**，脚本在 `connect` 后执行 `g` 放行；命令文件末尾 `sleep 86400000`
  保持连接（Commander 跑完命令文件会断连）。只有 `connect` 失败会让 Commander 退出（`ExitOnError`），
  由看门狗重试，所以目标后上电也能自动连上。
- **OpenOCD 每次 `rtt start` 只搜一次控制块**，脚本在没找到时每秒重试；OpenOCD 连接时不 halt，
  只在内核已经 halt 时才 `resume`。默认轮询 100ms 太慢，脚本改成 10ms。
- **默认 FireWater 是为了可读性**：波形不对时能直接用文本工具看原始数值，
  不用猜是发送端错了还是上位机解析配错了。代价是 float→ASCII 的转换开销。
  本包不调 C 库 `sprintf`（体积大），自带定点转换 `VOFA_FloatToStr()`，只做整数取位：
  每个十进制位求一次商（移位加法实现，不调库除法），符号、NaN/Inf、越界都按 float 的位模式判断，在无硬件除法和 FPU 的
  M0/M0+ 上尽量少调库函数。即便如此**也不要放进电流环等高频中断**。
- **JustFloat 省的是格式化开销，不是传输开销**：它按字节拷贝原始 float，只把 Inf/NaN 置 0
  （`+Inf` 的字节与帧尾 `00 00 80 7F` 相同，原样发出会让 VOFA+ 在帧中间提前切帧）。
  在 M0/M0+ 上差距最明显——float 转 ASCII 单通道实测约 28us（旧实现测的；现已改为移位加法除以 10、不调库除法，尚未重测），
  而 RTT 写入本身只要约 1~2us，格式化才是真瓶颈。
- **两种协议共用一套发送接口**，`VOFA_RTT_Send()` 按 `VOFA_PROTOCOL` 条件编译成
  两个实现之一，另一个连同其辅助函数一起被裁掉，不占 Flash 也不会报 unused 警告。
- **FireWater 的定点转换不支持科学计数法**：带 `VOFA_DECIMALS` 位小数的上限约为
  4.29×10^9 / 10^小数位（3 位小数时约 429 万），超过后自动减少小数位：值 ≥ 2^22（约 419 万）时只输出
  整数（这个量级 float 本来就没有小数精度），更小的值（4 位及以上小数才会遇到）保留放得下的最多位；
  超过 ±4294967295 饱和到上限；NaN/Inf 输出 0 以保持帧结构完整。
  量程不够时在 MCU 侧先缩放再发，或直接切 JustFloat。
- **JustFloat 依赖小端序**：ARM Cortex-M 默认小端，满足；大端平台需逐通道字节翻转，
  本包未做。
- **M3/M4/M7/M33 + AC6/GCC 下 SEGGER 默认 `RTT_USE_ASM=1`**：`SEGGER_RTT_WriteSkipNoLock` 会换成汇编版，
  本包没带对应的 `.S` 文件。本包不调用它，不受影响；自己调用它会链接报错，此时在编译选项里定义 `RTT_USE_ASM=0`。
- **JustFloat 下文本与波形绝对不能混流**：文本字节会被当 float 解析，或撞上帧尾
  `00 00 80 7F` 导致此后所有通道错位。所以 `LOG()` 在该协议下被编译期禁用，
  `VOFA_RTT_SendStr()` 也不发送。
- **脚本是 bat + PowerShell 混合的单文件，故意全 ASCII 无中文**：GBK 中文会导致 CMD 和
  PowerShell 5.1 解析错乱，别加中文。
- **脚本启动时会结束残留的 `JLink.exe` / `openocd.exe`**，会顺带结束 IDE 里正在跑的同名调试会话。
- **`SEGGER_RTT_Write()` 内部会进临界区**（M3/M4/M7 走 `BASEPRI`，M0/M0+ 退化成 `PRIMASK`
  关全局中断），不要在高优先级硬实时中断里高频调用。
