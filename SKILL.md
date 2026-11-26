---
name: vofa-rtt
description: 使用 SEGGER RTT 经 J-Link、CMSIS-DAP（DAPLink，v1/v2）或 ST-Link 向 VOFA+ 上位机推送实时波形。当用户要给 STM32/Cortex-M 工程加 RTT 调试、用 VOFA+ 看波形曲线、用 RTT 打印日志、移植 RTT 到新工程、用 CMSIS-DAP/OpenOCD 读 RTT、边看波形边下载（编译即下载）、IDE 下载与 RTT 抢探针，或排查 RTT 无数据/端口占用/波形不刷新/断线不恢复时使用。覆盖 Keil MDK、EIDE、CMake 三种构建方式的配置改动。
---

# VOFA+ RTT 波形输出移植

通过 SEGGER RTT 经调试器把 MCU 内部变量推给 VOFA+，无需占用串口，不阻塞主循环。
支持 J-Link、CMSIS-DAP（DAPLink，v1/v2）、ST-Link，换调试器不用改固件。
**FireWater 文本协议和 JustFloat 二进制协议都支持，改一个宏切换。**
FireWater 下还能用 `LOG()` 打印调试文本；两种协议都能用下行通道收命令。

主机端只有一个脚本 `assets/scripts/start_rtt.bat`，自动识别调试器、断线自动重连、编译即下载。
运行环境：Windows 10/11 自带的 cmd + PowerShell 5.1；J-Link 需 J-Link 软件，CMSIS-DAP / ST-Link 需
OpenOCD 0.12+（EIDE 自带即可）；不需要 Python，不需要管理员权限。完整清单见 README 的"环境要求"。

## 数据通路

```
MCU 代码  --VOFA_RTT_Send()-->  RTT 0 号上行缓冲(RAM)
         --调试器经 SWD 后台读走-->  底层工具 RTT 服务(127.0.0.1:19031，内部端口)
         --start_rtt.bat 转发-->  127.0.0.1:19021  --TCP-->  VOFA+
```

反向（可选，用于运行时开关日志项）：

```
VOFA+ 发送框发命令 --TCP 19021--> start_rtt.bat --> 底层工具 --> RTT 0 号下行缓冲
         --SEGGER_RTT_Read()-->  MCU 代码
```

底层工具由脚本按识别到的调试器选择，固件侧完全相同：

| 调试器 | 底层工具 | 说明 |
|--------|----------|------|
| J-Link | J-Link Commander（`-RTTTelnetPort 19031`） | 需装 J-Link 软件 |
| CMSIS-DAP v1/v2、ST-Link | OpenOCD 0.12+（`rtt server start 19031 0`） | EIDE 自带的即可 |

脚本自己占住 19021 转发，底层工具重启、探针拔插时 VOFA+ 连接不断。脚本不解析数据，与协议无关。
关键约束：**统一用 RTT 0 号上行通道**，`vofa_rtt.h` 里 `VOFA_RTT_BUF_IDX` 不要改。

## 两种协议与切换

本包两种协议都实现了，在 `vofa_rtt.h` 改一个宏切换，**发送端调用代码完全不用动**：

```c
/* vofa_rtt.h */
#define VOFA_PROTOCOL   VOFA_PROTO_FIREWATER   // 默认
// #define VOFA_PROTOCOL   VOFA_PROTO_JUSTFLOAT
```

切换后**必须同步改 VOFA+ 上位机的"数据格式"设置**，否则解析不出波形。`start_rtt.bat` 不用改。

| | FireWater（默认） | JustFloat |
|---|---|---|
| 帧格式 | `12.345,-6.789\n` | `[f0:4B][f1:4B][00 00 80 7F]` |
| 帧内容 | ASCII 文本 | 原始二进制小端 float |
| 双通道单帧 | 约 20 字节 | 12 字节 |
| 发送端开销 | float→ASCII 定点转换 | 仅 memcpy |
| 用文本工具能直接看懂 | 能 | 不能，是乱码 |
| 依赖小端序 | 不依赖 | 依赖（ARM Cortex-M 默认满足） |
| `LOG()` | 可用（但会污染波形，见步骤 6） | **编译期禁用** |
| 数值精度 | 受 `VOFA_DECIMALS` 限制，默认 3 位小数 | 完整 float 精度 |
| 超大数值 | 3 位小数时超过约 429 万自动去掉小数，超过 ±4294967295 饱和 | 无此问题 |

**默认选 FireWater 的理由是可读性**：波形不对时，用任意 TCP 调试工具连 `127.0.0.1:19021`
（J-Link 下也可以用 RTT Viewer / J-Link Commander 终端）就能看到原始数值，立刻能分清是发送端没发对
还是上位机配错了。代价是格式化开销——本包不调 C 库 `sprintf`，自带定点转换 `VOFA_FloatToStr()`
（只做整数除法取位），但仍不宜在 FOC 电流环等高频中断里调用。

**什么时候切到 JustFloat**：
1. 采样率要求高（kHz 级），格式化开销扛不住；
2. 目标是 Cortex-M0/M0+，无 FPU 且无硬件除法，float 转 ASCII 单通道实测约 28us，
   是 RTT 写入本身(memcpy 约 1~2us)的几十倍——此时格式化而非传输才是真瓶颈
   （28us 是旧实现测的；现已改为每位一次除法、除法用移位加法实现，不再调用库除法，未重测）；
3. 需要完整 float 精度，或数值量程超出定点范围。

**切到 JustFloat 会失去的**：`LOG()` 文本打印（编译期被禁用，原因见步骤 6）、
数据流的可读性。建议的用法是**用 FireWater 调通、确认数值正确，再切 JustFloat 跑正式采集**。

## 移植总览

移植 = 4 件事，缺一不可：

1. 拷源码 → `assets/RTT/` 六个文件进工程
2. 加编译 → 3 个 .c 加入构建，1 个头文件路径加入 include
3. 改脚本参数 → `start_rtt.bat` 拷到工程根目录，填器件名 / OpenOCD 目标 / RAM 搜索范围
4. 调用 → `main()` 里 `VOFA_RTT_Init()` + 循环里发数据

协议默认 FireWater，**先按默认调通再考虑换**——FireWater 的数据用文本工具肉眼可读，
出问题时定位快得多。要换成 JustFloat 见上一节。
用户用 CMSIS-DAP / ST-Link 且要边看波形边下载时，再看步骤 9。按下面步骤执行。

## 步骤 1：拷贝源码

把 `assets/RTT/` 下六个文件拷到目标工程，建议放 `User/RTT/`：

| 文件 | 作用 | 需要改吗 |
|------|------|----------|
| `SEGGER_RTT.c/.h` | SEGGER 官方 RTT 核心 | 不改 |
| `SEGGER_RTT_printf.c` | RTT 格式化输出，**本包已补 `%f` 支持** | 不改 |
| `SEGGER_RTT_Conf.h` | RTT 配置 | **可能要改**，见步骤 4 |
| `vofa_rtt.c/.h` | 双协议组帧封装层 + `LOG()` 宏 | **选协议**，按需改通道数/小数位 |

`assets/examples/` 下另有可选的 `log_demo.c/.h`（下行命令动态开关观察项，用户只在自己文件里写变量表，这两个文件不改），
不需要运行时开关可以不拷，见步骤 7。

`vofa_rtt.c` 经 `vofa_rtt.h` 末尾的 `VOFA_TICK_HEADER`（默认 `"main.h"`）拿 `VOFA_GET_TICK_MS()`
（默认 `HAL_GetTick()`），只给自检函数 `VOFA_RTT_TestLoop()` 用。非 HAL 工程把这两行改成用户自己的
头文件和毫秒计时函数（返回 `uint32_t`，允许回绕），`vofa_rtt.c` 不用动。

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
| `DEVICE` | `STM32G070RB` | J-Link | SEGGER 器件名（去掉封装/温度后缀，如 `STM32F407VE`、`STM32G070RB`、`STM32H743XI`），**改成用户的芯片**，写错连不上。默认值与下面 OpenOCD 的几项是同一颗 G070 |
| `TARGETCFG` | `target/stm32g0x.cfg` | CMSIS-DAP / ST-Link | OpenOCD `scripts/target/` 下的目标脚本，默认对应 STM32G070RB，**改成用户的芯片**，如 `target/stm32f1x.cfg`、`target/stm32f4x.cfg`、`target/stm32h7x.cfg` |
| `RAM_START` / `RAM_SIZE` | `0x20000000` / `0x9000` | CMSIS-DAP / ST-Link | 控制块搜索范围，**必须覆盖 `_SEGGER_RTT`**。默认是 G070 的 36KB SRAM，换芯片必改（F407VE 用 `0x20000000` / `0x20000`）。最稳：按 `.map` 里 `_SEGGER_RTT` 的地址填 `RAM_START`，`RAM_SIZE` 填 `0x100`（查法见 `references/porting.md`） |
| `AUTOFLASH` | `1` | CMSIS-DAP / ST-Link | 1 = 编译即下载，0 = 关闭 |
| `FIRMWARE` | 空 | CMSIS-DAP / ST-Link | 空 = 监视脚本目录下所有 `.axf` / `.elf`；目录里有多个工程（如 bootloader + app）时填要下载的那个文件 |
| `RESET` | `0` | 全部 | 0 = 附着到正在跑的固件，重连不重启 MCU；1 = 每次连上都复位 |
| `SPEED` / `RTTPORT` | `4000` / `19021` | 全部 | SWD 速率 kHz / VOFA+ 端口，内部端口自动取 `RTTPORT+10` |
| `JLINK` / `OPENOCD` | 空 | 全部 | 工具路径，留空自动找（J-Link：`Program Files\SEGGER\JLink*`、EIDE 自带、PATH；OpenOCD：EIDE 自带、PATH） |

## 步骤 4：核对 RTT 配置

`SEGGER_RTT_Conf.h` 里本包已调整过的项：

```c
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS    (1)     // 只用 0 号上行
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS  (1)     // 只用 0 号下行（收命令用）
#define BUFFER_SIZE_UP                   (2048)  // 波形帧走这里，2K 对两种协议都够
#define BUFFER_SIZE_DOWN                 (16)    // 下行只收短命令，16 字节够
#define SEGGER_RTT_PRINTF_BUFFER_SIZE    (128u)  // LOG() 单行 <=128 字节一次写入（原默认 64）
#define SEGGER_RTT_MODE_DEFAULT   SEGGER_RTT_MODE_NO_BLOCK_SKIP  // 缓冲满丢帧，不阻塞
```

需要按目标平台核对的：

- **协议**：`VOFA_PROTOCOL`（在 `vofa_rtt.h`）默认 `VOFA_PROTO_FIREWATER`。
  改这一个宏即可切 JustFloat，**记得同步改 VOFA+ 的数据格式**。
- **RAM 紧张**（如 8KB 以下）：`BUFFER_SIZE_UP` 降到 512 或 256，代价是高速发送时丢帧。
  FireWater 帧比 JustFloat 大近一倍，缓冲小又必须用 FireWater 时，要同步降低
  发送频率或通道数；换 JustFloat 本身就能省下近一半带宽。
- **多通道多**：`VOFA_CH_MAX`（在 `vofa_rtt.h`）默认 8，加通道时同步调大，两种协议都用它。
  超过 `VOFA_CH_MAX` 的 `VOFA_RTT_Send()` 调用会**整帧丢弃**而不是截断。
- **小数位**：`VOFA_DECIMALS`（在 `vofa_rtt.h`）默认 3，**仅 FireWater 用到**
  （JustFloat 传完整 float，没有小数位概念，该宏在 JustFloat 下不参与编译）。
  调大帧会变长，`VOFA_NUM_MAX` 已用宏跟着算，不用手改。
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
void VOFA_RTT_Init(void);                                   // 初始化，main 起始调用
unsigned VOFA_RTT_Send1(float ch0);                         // 单通道
unsigned VOFA_RTT_Send2(float ch0, float ch1);              // 双通道
unsigned VOFA_RTT_Send(const float *data, unsigned ch_num); // N 通道，ch_num <= VOFA_CH_MAX
VOFA_SEND(a, b, c);                                         // 任意个通道一行发，不用建数组（需 C99）
void VOFA_RTT_TestLoop(void);                               // 正弦波自检，验证完可删
unsigned VOFA_RTT_SendRaw(const void *buf, unsigned len);   // 原样发字节，不加帧格式，整块写入或整块丢弃
unsigned VOFA_RTT_SendStr(const char *s);                   // 发字符串，整串一次写入（仅 FireWater）
```

这套接口**两种协议下完全一致**，切协议不用改任何调用代码。
`SendStr` 在 JustFloat 下不发送、返回 0（与 `LOG()` 同理，见步骤 6）；`SendRaw` 两种协议都发，
JustFloat 下调用方必须自己保证写的是完整帧。

发送接口都返回写入字节数，**0 表示整帧被丢弃**（缓冲满或参数无效），用户要排查丢帧时可以计数。

用户要发的通道是零散变量时，优先写 `VOFA_SEND(a, b, c)`：参数按 float 转换、每个只求值一次，
个数超过 `VOFA_CH_MAX` 直接编译报错。它依赖 C99 复合字面量，**C90 下不提供**——Keil AC5 工程要先确认
`Options for Target` → `C/C++` → `C99 Mode` 已勾选，没勾就改用数组 + `VOFA_RTT_Send()`，或请用户勾上。

**发送频率必须节流。** 主循环裸跑不节流会以循环速度灌满缓冲导致丢帧和波形失真。
参考 `VOFA_RTT_TestLoop()` 里的 tick 节流写法，或放在固定周期的定时器回调里发。
单帧大小：FireWater 约 (小数位+7)×通道数 字节，JustFloat 是 4×通道数+4 字节。
FireWater 几百 Hz 以内没问题，要再高就换 JustFloat。

## 步骤 6：用 LOG() 打调试文本（仅 FireWater）

`vofa_rtt.h` 提供 `LOG()` 宏，用法与 `printf` 完全一致，走同一个 RTT 0 号通道输出，不占串口。
用任意 TCP 调试工具连 `127.0.0.1:19021` 查看（J-Link 下也可以用 RTT Viewer / J-Link Commander 终端）：

```c
LOG("id=%d speed=%.2f flag=%s\n", id, speed, ok ? "OK" : "NG");
```

支持 `%c %d %u %x %X %s %p %f %F %%`，以及 `-` `0` `+` `#` 标志、字段宽度和精度
（如 `%8.3f`、`%-10s`、`%08.2f`）。

**`%f` 是本包给 `SEGGER_RTT_printf` 补的**——SEGGER 原版完全不支持 `%f`。
实现走定点转换，默认 6 位小数，量程 ±4294967295，超出或 NaN 打印 `Inf`/`NaN` 标记，
不静默回绕。

单条输出不超过 128 字节（`SEGGER_RTT_PRINTF_BUFFER_SIZE`）时一次写入 RTT：中断里发的波形帧插不进
这一行中间，缓冲满时整条丢弃而不是只写半截。更长的行分段写入，内容仍完整但失去这个保证。
不需要格式化的固定文本用 `VOFA_RTT_SendStr()`，整串一次写入、没有格式化开销。

### 两种协议下 LOG 的行为不同

**FireWater 下**：`LOG()` 可用，但与波形共用 0 号通道。VOFA+ 解析时混进来的文本
会被当成数据行，波形上表现为乱跳的点。**采集波形期间不要开 LOG，两者择一使用**
——这只是"脏数据"，不会让解析崩掉。

**JustFloat 下**：`LOG()` 被**编译期禁用**，展开成 `((void)0)`。

为什么必须禁用，而不是像 FireWater 那样"注意别同时用"：JustFloat 是纯二进制流，
VOFA+ 靠 4 字节帧尾 `00 00 80 7F` 切帧、帧内每 4 字节当一个 float。混进 ASCII
文本会有两种后果——文本字节被当成 float 解析出巨大乱跳值；或者文本里凑巧出现
帧尾字节序列，导致帧边界错位，**之后所有通道全部错位**。这比 FireWater 的脏数据
严重得多，且无法靠"小心使用"规避，所以直接在编译期断掉。

实际影响：

- **调用点不用加 `#if` 包裹**，禁用后整条语句被编译掉。
- ⚠️ **参数也不会求值**。`LOG("%d", i++)` 在 JustFloat 下不会执行 `i++`，
  所以**别把带副作用的表达式放进 LOG 参数里**。
- JustFloat 下确实需要文本输出时，两条路：临时切回 FireWater 重新编译；
  或自行改用 RTT 1 号通道（把 `SEGGER_RTT_Conf.h` 的
  `SEGGER_RTT_MAX_NUM_UP_BUFFERS` 调到 2，J-Link 下用 RTT Viewer 看 Terminal 1；
  注意 `start_rtt.bat` 只转发 0 号通道，VOFA+ 收不到 1 号）。

## 步骤 7（可选）：下行命令动态开关观察项

`assets/examples/log_demo.c/.h` 让用户把要观察的变量登记成一张 `LogItem` 表，
运行时发命令开关，不用改代码重新烧录就能切换看哪几组波形。**这两个文件本身不用改**，表写在用户自己的文件里。

**两种协议都能用，本文件不用改**：它输出时调的是 `VOFA_RTT_Send()`，帧格式自动
跟随 `VOFA_PROTOCOL`；命令走的是 RTT **下行**通道，与上行波形协议无关，
所以 JustFloat 下 AT 命令照常好使。

命令从 VOFA+ 的发送框发出，经 `start_rtt.bat` 转发进 RTT **下行**通道
（J-Link 下也可以用 RTT Viewer 的输入行；经脚本转发这条路尚未实测）：

```
AT+1\n      -> 切换 1 号项
AT+1,2\n    -> 同时切换 1 号和 2 号，两项都开就一起打印
AT+1\n      -> 再发一次同样的号即关闭该项（每个号独立翻转）
AT+0\n      -> 全部关闭
AT+KP=1.5\n -> 不是上面的格式，交给 LogSetCmdHook 注册的回调（没注册就丢弃）
```

编号按表顺序自动分配，第 1 行是 1 号；一帧里的通道顺序就是表顺序。

移植方法：

1. 拷 `log_demo.c/.h` 进工程，`.c` 加入编译（与步骤 2 同样的做法）
2. 在用户自己的文件里写表，每行一个观察项，宏按变量类型选
   `LOG_F32` / `LOG_I32` / `LOG_U32` / `LOG_I16` / `LOG_U16` / `LOG_I8` / `LOG_U8`，参数是 `(变量, 通道数)`：

   ```c
   #include "log_demo.h"
   static const LogItem s_logItems[] = {
       LOG_F32(g_angle, 1),     // 1 号
       LOG_F32(g_iabc,  3),     // 2 号：数组前 3 个元素
       LOG_U16(g_adcVbus, 1),   // 3 号：整数发送时转成 float
   };
   ```

   通道数为 0、超过 `VOFA_CH_MAX`、或超出变量大小（如 `short` 变量用了 `LOG_F32`）时编译报错
3. `VOFA_RTT_Init()` 之后调 `LogInit(s_logItems, sizeof(s_logItems) / sizeof(s_logItems[0]))`；
   需要自定义命令（如 `AT+KP=1.5`）时再 `LogSetCmdHook(回调)`，回调参数 `(const char *line, unsigned len)`，`line` 以 `'\0'` 结尾
4. 周期调用 `LogTask()`（建议 20~50ms 一次，放主循环节流或定时器回调里）

可配置项（`log_demo.h`，都可在编译选项里覆盖）：`LOG_ITEM_MAX`（最多登记项数，默认 64）、
`LOG_LINE_MAX`（一条命令最多字符数，默认 32）。打开某项后一帧会超过 `VOFA_CH_MAX` 时这次打开不生效，
所以表里可以登记远多于 `VOFA_CH_MAX` 的项，按需开一部分。
绝对值超过 16777216 的 32 位整数转 float 会丢精度；登记的变量要自然对齐（packed 结构体成员不行）。

实现要点，改的时候别破坏：

- **位图翻转**：每项占 1 bit，收到号就翻转，所以多项天然可叠加，同号发两次即关。
- **跨次读取组行**：RTT 下行缓冲只有 `BUFFER_SIZE_DOWN`(默认 16) 字节，
  一条命令可能分几次才读全，所以要自己拼行缓冲，不能一次读到就当完整命令。
- **不回显开关状态**：上行是纯数据流，回一句 "CH1 ON"，FireWater 下会被当成数据行，
  JustFloat 下更会破坏帧对齐，两种协议都不能回显。
- **通道顺序 = 表顺序**：编号就是表里的行号，所以曲线和编号天然对得上。
- **整帧一次性写入**：分次 `VOFA_RTT_Send` 会被拆散，上位机解析错位。
- **通道数随开关变化**：开的项不同，每帧通道数就不同。FireWater 下 VOFA+ 能自动
  适应（按逗号数），JustFloat 下也能（按帧长除以 4），但曲线的对应关系会变，
  切换观察项后记得重新对照表看曲线。

## 步骤 8：开服务 + VOFA+ 配置

双击 `start_rtt.bat`，就绪标志：`probe: ...` → `RTT stream connected`
（OpenOCD 下还有 `RTT Control block found at 0x...`）。窗口标题实时显示状态和 KB/s。

VOFA+ 里：

| 项 | 值 |
|----|-----|
| 数据接口 | TCP 客户端 |
| 服务器 IP | 127.0.0.1 |
| 网络端口 | 19021 |
| 握手数据 | 留空 |
| 数据格式 | **与 `VOFA_PROTOCOL` 一致** |

最后一项必须和固件里的宏对上，否则解析不出波形：

- 固件 `VOFA_PROTO_FIREWATER` → VOFA+ 选 **FireWater**。
  帧格式：各通道十进制数值逗号分隔，`\n` 结尾。VOFA+ 按 `\n` 切帧、按逗号切通道。
- 固件 `VOFA_PROTO_JUSTFLOAT` → VOFA+ 选 **JustFloat**。
  帧格式：N 个 4 字节小端 float + 4 字节帧尾 `00 00 80 7F`。VOFA+ 按帧尾切帧，
  帧内字节数除以 4 即通道数。

两者都无需握手数据。**改了固件的协议宏却忘了改这里，是最常见的"突然没波形"原因。**

脚本窗口按键：`P` 暂停并释放探针（给 Keil 调试用）/ 再按恢复，`R` 立即重连，`Q` 退出。
探针拔插后自动重连，不用重开脚本。

## 步骤 9：边看波形边下载

J-Link 不用做这一步：SEGGER 软件允许 IDE 和脚本同时用一个 J-Link，照常下载。

CMSIS-DAP / ST-Link 同一时间只能有一个程序在用探针。**两个程序同时用会让命令和应答串话**
（实测 `CMSIS-DAP command mismatch`），下载中可能写坏 Flash。按推荐顺序：

1. **编译即下载（默认开启，推荐）**：什么都不用配。Keil 按 F7 / EIDE 点 Build，脚本发现新的
   `.axf` / `.elf` 写完后，经自己的 OpenOCD 下载（先 `rtt stop`，烧完复位后重新找控制块），波形接着推。
   已经烧进去的同一份固件再编译出来会跳过。要告诉用户：**改完代码按 F7 就行，不要再按 F8**。
2. **IDE 自己的下载按钮（EIDE）**：可以照常点。脚本每 100ms 检查有没有别的 `openocd` / `pyocd` /
   `STM32_Programmer_CLI` 进程，有就让出探针，对方结束 2 秒后接回。对方启动的一瞬间偶尔会报一次错，重试即可。
3. **Keil F8 交给脚本**：`Options for Target` → `Utilities` → `Use External Tool for Flash Programming`，
   `Command` 填 `start_rtt.bat` 完整路径，`Arguments` 填 `flash "#L"`，不勾 `Run Independent`。

**不要**在脚本运行时用 Keil 自带的 CMSIS-DAP 驱动按 F8：Keil 不起单独进程，脚本只能在通信被打断后才让出。
Keil 调试（Ctrl+F5）仍走 Keil 自己的驱动，调试前在脚本窗口按 `P`。

## 故障排查

按现象查，**不要跳过第一条**——绝大多数"没波形"都是内核被 halt、固件没跑到初始化，或协议没对上：

| 现象 | 原因 | 处理 |
|------|------|------|
| `RTT stream connected` 但标题速率一直 0 | 内核 halt 或 `main()` 没跑到 `VOFA_RTT_Init()` | 按 `R` 重连（会放行 halt 的内核），或设 `RESET=1` |
| **改了协议宏后全无波形** | VOFA+ 的数据格式没跟着改 | 固件 `VOFA_PROTOCOL` 与 VOFA+ 里的格式必须一致 |
| 一直 `waiting for a probe` | 探针没插好 / 驱动没装 / 被识别成别的类型 | 检查设备管理器；多种探针同时插着时用 `PROBE` 写死 |
| `probe is in use by another program, waiting` | Keil 调试等程序开着探针的 HID 接口 | 等它释放，脚本自动接管 |
| `openocd (pid ...) is using the probe` 后一直不恢复 | 有常驻的 OpenOCD（如 EIDE 调试会话） | 结束它，或按 `R` |
| 编译后没自动下载 | 脚本不在工程目录、输出不是 `.axf`/`.elf`、`AUTOFLASH=0`、内容没变被跳过、脚本处于暂停/让出 | 逐项核对；多工程目录用 `FIRMWARE` 指定 |
| OpenOCD 反复 `rtt: No control block found` | 搜索范围没覆盖 `_SEGGER_RTT` | 按 `.map` 改 `RAM_START`/`RAM_SIZE`；脚本每秒自动重试 |
| J-Link 反复 `backend exited` | `DEVICE` 写错，或目标没上电 | 改 `DEVICE`；目标上电后自动连上 |
| OpenOCD 反复 `backend exited` | `TARGETCFG` 不对、目标没上电、探针被别的程序占用 | 看上方 OpenOCD 红色报错；先停掉 IDE 调试会话 |
| 启动报 `Port 19021 is busy` | 已有一个 `start_rtt.bat`，或 RTT Viewer / J-Scope / Ozone 占着 | 关掉它们 |
| Keil F8 报找不到 CMSIS-DAP / ST-Link | 脚本占着探针，Keil 自带下载驱动打不开 | 改按 F7 编译即下载，或按步骤 9 配外部工具 |
| Keil 调试连不上探针 | 脚本占着探针 | 脚本窗口按 `P` 释放 |
| 偶尔一行灰色 `RTT stream closed, reconnecting` | OpenOCD 0.12 在 Windows 上偶发断开内部连接 | 可忽略：0.1 秒内重连，数据留在目标缓冲不丢 |
| 波形全是乱跳巨值 | JustFloat 下混进了文本，或协议选反 | 核对 VOFA+ 格式；检查有无别处直接写 RTT 0 号通道 |
| 波形通道整体错位 | JustFloat 帧边界被破坏 | 同上；确认没有分次写入同一帧 |
| 波形里混入乱跳的点 | FireWater 下 `LOG()` 文本混进数据流 | 采集波形时关掉 LOG，两者共用 0 号通道 |
| `LOG()` 完全没输出 | 当前是 JustFloat，LOG 被编译期禁用 | 这是预期行为；要文本就切回 FireWater（见步骤 6） |
| 切 JustFloat 后某变量不自增 | 副作用写在了 `LOG()` 参数里 | 把 `i++` 之类移出 LOG 参数 |
| 波形卡顿丢点 | 发送太快缓冲溢出 | 降低发送频率、调大 `BUFFER_SIZE_UP`，或换 JustFloat 省带宽；发送接口返回 0 即是被丢的帧，可计数确认 |
| 某帧完全没出现 | `ch_num > VOFA_CH_MAX`，整帧被丢 | 调大 `vofa_rtt.h` 的 `VOFA_CH_MAX` |
| 某通道一直是 0 | 源变量是 Inf/NaN（除零、未初始化等），两种协议都发成 0 | 查该变量的计算 |
| 数值精度不够 | FireWater 的 `VOFA_DECIMALS` 默认 3 位 | 调大 `VOFA_DECIMALS`；或换 JustFloat 得到完整精度 |
| 大数值显示成固定上限 4294967295 | FireWater 下超过 ±4294967295 被饱和（超过约 429 万只是去掉小数，不算饱和） | MCU 侧先缩放（如除以 1000）；或换 JustFloat |
| `%f` 打印出 `Inf`/`NaN` | 超出 ±4294967295 或本身非有限 | 检查源变量，或先缩放 |
| 发 AT 命令没反应 | 没调 `LogInit()`、没调 `LogTask()`，或命令没带 `\n`；号超出表的项数、或打开后会超 `VOFA_CH_MAX` 也不生效 | 确认已登记表并周期调用；VOFA+ 发送框要勾上换行 |
| 挂 Keil 调试后波形停 | 调试器 halt 了内核 | 全速运行后恢复；流式采集时少打断点 |
| `flash` 报 `J-Link found` | J-Link 不需要外部工具下载 | Keil 用回 J-Link 驱动即可 |

## 注意事项

- `SEGGER_RTT_Write()` 内部会进临界区（M3/M4/M7 走 `BASEPRI` 屏蔽可配置优先级中断，
  M0/M0+ 无 `BASEPRI` 退化成 `PRIMASK` 关全局中断），
  **不要在高优先级硬实时中断里高频调用**，会影响中断响应。
- **协议宏与 VOFA+ 设置必须一致**。改了 `VOFA_PROTOCOL` 就要改 VOFA+ 的数据格式，
  这是切协议后"突然没波形"的头号原因。`start_rtt.bat` 只原样转发，与协议无关。
- FireWater 的 float→ASCII 转换有实打实的开销，**不要放进电流环等高频中断**；
  需要极限性能就切 JustFloat（`memcpy` 级开销）。
- `vofa_rtt.c` 的 `VOFA_FloatToStr()`（**仅 FireWater**）只做定点，不支持科学计数法，
  带小数放不下 32 位时（3 位小数即超过约 429 万）自动去掉小数只输出整数，超过 ±4294967295 饱和，
  NaN/Inf 输出 0 以保持帧结构完整。量程不够时在 MCU 侧先缩放，
  或直接换 JustFloat——它传原始 float，没有量程和精度损失。
- **JustFloat 依赖小端序**。ARM Cortex-M 默认小端，满足；移植到大端平台需要
  逐通道字节翻转，本包未做。
- **JustFloat 下 Inf/NaN 也被置 0**：`+Inf` 的字节与帧尾 `00 00 80 7F` 完全相同，原样发出
  会让 VOFA+ 在帧中间提前切帧。波形上某通道恒为 0 时，先查源变量是否除零或溢出。
- `LOG()` 在 FireWater 下与波形共用 0 号通道，同时用会污染数据流；
  在 JustFloat 下被编译期禁用，且**参数不会求值**，别把副作用写进 LOG 参数。
  `VOFA_RTT_SendStr()` 同样只在 FireWater 下发送，但它是真函数，JustFloat 下参数照常求值。
- 改了本包的 `vofa_rtt.c` / `log_demo.c` 后，在本包目录跑 `sh tests/run_tests.sh` 做主机回归
  （需主机 gcc，不用上板），最后一行 `ALL PASSED` 才算通过。`tests/` 不拷进用户工程。
- `start_rtt.bat` 是 bat + PowerShell 5.1 混合单文件，**故意全 ASCII 无中文**：GBK 中文会导致 CMD
  和 PowerShell 5.1 解析错乱，改脚本时别加中文。
- 脚本启动时会结束残留的 `JLink.exe` / `openocd.exe`，**会顺带结束 IDE 里正在跑的同名调试会话**。
- 验证情况：CMSIS-DAP v2 + STM32G070 已实测（识别、波形、编译即下载〔模拟编译输出〕、
  另一个 OpenOCD 下载时让出并接回、`flash` 模式、拔插恢复；测试固件为 JustFloat）；
  J-Link、ST-Link、仅 v1 的 CMSIS-DAP 走本脚本，下行命令经脚本转发，以及 Keil / EIDE 里
  真实编译触发下载，尚未实测，遇到问题按上表排查。
