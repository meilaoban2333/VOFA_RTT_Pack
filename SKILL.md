---
name: vofa-rtt
description: 使用 SEGGER RTT 向 VOFA+ 上位机推送实时波形。当用户要给 STM32/Cortex-M 工程加 RTT 调试、用 VOFA+ 看波形曲线、用 RTT 打印日志、移植 RTT 到新工程，或排查 RTT 无数据/端口占用/波形不刷新时使用。覆盖 Keil MDK、EIDE、CMake 三种构建方式的配置改动。
---

# VOFA+ RTT 波形输出移植

通过 SEGGER RTT 经 J-Link 把 MCU 内部变量以 FireWater 文本协议推给 VOFA+，
无需占用串口，不阻塞主循环。同一通道还能用 `LOG()` 打印调试文本、用下行通道收命令。

## 数据通路

```
MCU 代码  --VOFA_RTT_Send()-->  RTT 0 号上行缓冲(RAM)
         --J-Link 经 SWD 后台读走-->  RTT Telnet Server(127.0.0.1:19021)
         --TCP-->  VOFA+
```

反向（可选，用于运行时开关日志项）：

```
VOFA+ / RTT Viewer 发命令 --TCP--> RTT Telnet --J-Link--> RTT 0 号下行缓冲
         --SEGGER_RTT_Read()-->  MCU 代码
```

关键约束：**19021 端口固定绑定 RTT 0 号上行通道**，所以必须用通道 0，不能改成 1。

## 协议选型：为什么是 FireWater

本包默认走 **FireWater**（纯文本，逗号分隔，`\n` 结尾）：

```
12.345,-6.789\n
```

对比另一种常见选择 JustFloat（N 个小端 float + 4 字节帧尾 `00 00 80 7F`）：

| | FireWater | JustFloat |
|---|---|---|
| 帧内容 | ASCII 文本 | 原始二进制 |
| 双通道单帧 | 约 20 字节 | 12 字节 |
| 发送端开销 | 需 float→ASCII 转换 | 仅 memcpy |
| RTT Viewer 里能直接看懂 | 能 | 不能，是乱码 |
| 依赖小端序 | 不依赖 | 依赖 |

选 FireWater 的理由是**可读性**：波形不对时能直接在 RTT Viewer / J-Link Commander
终端里看到原始数值，不用猜是发送端错了还是解析配错了。

代价是格式化开销。本包没有调用 C 库 `sprintf`，而是自带定点转换
`VOFA_FloatToStr()`（只做整数除法取位），避免拖进浮点格式化例程。
即便如此，**仍不宜在 FOC 电流环等高频中断里调用**。

**什么时候改回 JustFloat**：采样率要求很高（如 kHz 级）、或目标是 Cortex-M0/M0+
（无 FPU 且无硬件除法，float 转 ASCII 单通道实测约 28us，是 RTT 写入本身的几十倍）。
改法见本文末尾「附：改回 JustFloat」。

## 移植总览

移植 = 4 件事，缺一不可：

1. 拷源码 → `assets/RTT/` 六个文件进工程
2. 加编译 → 3 个 .c 加入构建，1 个头文件路径加入 include
3. 改器件名 → 脚本里的 `DEVICE` 改成实际 MCU
4. 调用 → `main()` 里 `VOFA_RTT_Init()` + 循环里发数据

按下面步骤执行。

## 步骤 1：拷贝源码

把 `assets/RTT/` 下六个文件拷到目标工程，建议放 `User/RTT/`：

| 文件 | 作用 | 需要改吗 |
|------|------|----------|
| `SEGGER_RTT.c/.h` | SEGGER 官方 RTT 核心 | 不改 |
| `SEGGER_RTT_printf.c` | RTT 格式化输出，**本包已补 `%f` 支持** | 不改 |
| `SEGGER_RTT_Conf.h` | RTT 配置 | **可能要改**，见步骤 4 |
| `vofa_rtt.c/.h` | FireWater 组帧封装层 + `LOG()` 宏 | 按需改通道数/小数位 |

`assets/examples/` 下另有可选的 `log_demo.c/.h`（下行命令动态开关日志项），
不需要运行时开关可以不拷，见步骤 7。

`vofa_rtt.c` 里 `#include "main.h"` 是为了拿 `HAL_GetTick()`。非 HAL 工程要把
`VOFA_RTT_TestLoop()` 里的 `HAL_GetTick()` 换成自己的毫秒计时源，或直接删掉这个测试函数。

## 步骤 2：加入构建

按目标工程的构建方式选一种。**三种方式的具体改动写在 `references/porting.md`**，
移植时读取该文件，里面有 Keil MDK / EIDE / CMake 的逐项配置和可直接粘贴的片段。

共同点，无论哪种方式都要做到：
- 3 个 `.c` 参与编译：`SEGGER_RTT.c`、`SEGGER_RTT_printf.c`、`vofa_rtt.c`
- RTT 目录加进头文件搜索路径

## 步骤 3：改脚本器件名

`assets/scripts/` 下两个 bat 拷到工程根目录，**必须改 `DEVICE`** 为实际芯片：

```bat
set DEVICE=STM32F407VE
```

器件名用 SEGGER 支持列表里的写法（一般是去掉封装/温度后缀的型号，如
`STM32F407VE`、`STM32G070RB`、`STM32H743XI`）。写错会连不上目标。

同时确认 J-Link 安装路径，脚本默认 `C:\Program Files\SEGGER\JLink_V864`，
找不到会自动回退搜索 `JLink_V*` 和 PATH，通常无需改。

两个脚本的区别：

| 脚本 | 用什么 | 什么时候用 |
|------|--------|-----------|
| `start_rtt.bat` | J-Link Commander | **默认用这个**，最简单 |
| `start_rtt_gdb.bat` | J-Link GDB Server | Commander 不稳、或要同时留 GDB 口时用 |

两个都做了同一件关键事：**连接后执行 `g`(go) 放行内核**。J-Link 连上会 halt 内核，
不放行则 `main()` 不跑，RTT 缓冲永远空。

## 步骤 4：核对 RTT 配置

`SEGGER_RTT_Conf.h` 里本包已调整过的项：

```c
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS    (1)     // 只用 0 号上行
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS  (1)     // 只用 0 号下行（收命令用）
#define BUFFER_SIZE_UP                   (2048)  // FireWater 文本帧比二进制大，2K 留余量
#define BUFFER_SIZE_DOWN                 (16)    // 下行只收短命令，16 字节够
#define SEGGER_RTT_MODE_DEFAULT   SEGGER_RTT_MODE_NO_BLOCK_SKIP  // 缓冲满丢帧，不阻塞
```

需要按目标平台核对的：

- **RAM 紧张**（如 8KB 以下）：`BUFFER_SIZE_UP` 降到 512 或 256，代价是高速发送时丢帧。
  注意 FireWater 帧比 JustFloat 大近一倍，降缓冲要同步降发送频率或通道数。
- **多通道多**：`VOFA_CH_MAX`（在 `vofa_rtt.h`）默认 8，加通道时同步调大。
  超过 `VOFA_CH_MAX` 的 `VOFA_RTT_Send()` 调用会**整帧丢弃**而不是截断。
- **小数位**：`VOFA_DECIMALS`（在 `vofa_rtt.h`）默认 3。调大帧会变长，
  `VOFA_NUM_MAX` 已用宏跟着算，不用手改。
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
    VOFA_RTT_Init();    // 必须在外设和中断启动前，保证 J-Link 连接时控制块已就绪
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
FireWater 单帧约 (小数位+7)×通道数 字节，几百 Hz 以内没问题。

## 步骤 6：用 LOG() 打调试文本

`vofa_rtt.h` 提供 `LOG()` 宏，用法与 `printf` 完全一致，走同一个 RTT 0 号通道
输出到 RTT Viewer / J-Link Commander 终端，不占串口：

```c
LOG("id=%d speed=%.2f flag=%s\n", id, speed, ok ? "OK" : "NG");
```

支持 `%c %d %u %x %X %s %p %f %F %%`，以及 `-` `0` `+` `#` 标志、字段宽度和精度
（如 `%8.3f`、`%-10s`、`%08.2f`）。

**`%f` 是本包给 `SEGGER_RTT_printf` 补的**——SEGGER 原版完全不支持 `%f`。
实现走定点转换，默认 6 位小数，量程 ±4294967295，超出或 NaN 打印 `Inf`/`NaN` 标记，
不静默回绕。输出长度不受限，内部 64 字节缓冲写满会自动 flush。

**关键冲突**：`LOG()` 与波形共用 0 号通道。VOFA+ 按 FireWater 解析时，
混进来的文本会被当成数据行，导致波形出现乱跳的点。
**采集波形期间不要开 LOG，两者择一使用。**

## 步骤 7（可选）：下行命令动态开关日志项

`assets/examples/log_demo.c/.h` 是一个模板：把要观察的变量分组编号，
运行时发命令开关，不用改代码重新烧录就能切换看哪几组波形。

命令从 RTT **下行**通道进来（VOFA+ 的发送框或 RTT Viewer 的输入行都能发）：

```
AT+1\n      -> 切换 1 号项
AT+1,2\n    -> 同时切换 1 号和 2 号，两项都开就一起打印
AT+1\n      -> 再发一次同样的号即关闭该项（每个号独立翻转）
AT+0\n      -> 全部关闭
```

移植方法：

1. 拷 `log_demo.c/.h` 进工程，`.c` 加入编译（与步骤 2 同样的做法）
2. 改 `log_demo.h` 里的 `LOG_ID_xxx` 表为自己的观察项
3. 改 `log_demo.c` 里 `LogPrint()` 的取值代码为自己的变量
4. 更新 `LOG_CH_MAX` 为全开时的通道总数，并确认 **`LOG_CH_MAX <= VOFA_CH_MAX`**
5. 周期调用 `LogTask()`（建议 20~50ms 一次，放主循环节流或定时器回调里）

实现要点，改的时候别破坏：

- **位掩码翻转**：每个项目号占 1 bit，收到号就异或，所以多项天然可叠加，同号发两次即关。
- **跨次读取组行**：RTT 下行缓冲只有 `BUFFER_SIZE_DOWN`(默认 16) 字节，
  一条命令可能分几次才读全，所以要自己拼行缓冲，不能一次读到就当完整命令。
- **不回显开关状态**：FireWater 是纯数据协议，回一句 "CH1 ON" 会被上位机当成数据行。
- **通道顺序 = 项目号从小到大**：顺序乱了上位机的曲线就对不上号。
- **整帧一次性写入**：分次 `VOFA_RTT_Send` 会被拆行，上位机解析错行。

## 步骤 8：VOFA+ 上位机配置

先双击运行 `start_rtt.bat`，等出现 `Connected`，再在 VOFA+ 里：

| 项 | 值 |
|----|-----|
| 数据接口 | TCP 客户端 |
| 服务器 IP | 127.0.0.1 |
| 网络端口 | 19021 |
| 握手数据 | 留空 |
| 数据格式 | **FireWater** |

FireWater 帧格式：各通道十进制数值逗号分隔，`\n` 结尾。
VOFA+ 按 `\n` 切帧、按逗号切通道，通道数由每帧字段个数决定，无需额外握手。

## 故障排查

按现象查，**不要跳过第一条**——绝大多数"没波形"都是内核被 halt：

| 现象 | 原因 | 处理 |
|------|------|------|
| 端口通了但无数据 | 内核halt在断点，`main()`没跑 | 在 `J-Link>` 提示符敲 `g` 回车 |
| 波形只出一小段就停 | Commander 脚本跑完就断连 | 用本包脚本，末尾 `sleep 86400000` 保持连接 |
| 连不上/端口占用 | 残留 JLink 进程占着 19021 | 本包脚本已自动 `taskkill`；手工查 `netstat -ano \| findstr 1902` |
| 端口变成 19022 | 19021 被占，J-Link 自动顺延 | 看脚本 banner 实际端口，VOFA+ 里改成对应值 |
| 数据乱码/串行 | 多客户端抢同一 RTT | RTT Telnet 同时只允许一个客户端，关掉 J-Scope / RTT Viewer / Ozone |
| 波形里混入乱跳的点 | `LOG()` 文本混进了数据流 | 采集波形时关掉 LOG，两者共用 0 号通道 |
| 波形卡顿丢点 | 发送太快缓冲溢出 | 降低发送频率，或调大 `BUFFER_SIZE_UP` |
| 某帧完全没出现 | `ch_num > VOFA_CH_MAX`，整帧被丢 | 调大 `vofa_rtt.h` 的 `VOFA_CH_MAX` |
| 数值精度不够 | `VOFA_DECIMALS` 默认 3 位 | 调大 `VOFA_DECIMALS`，注意帧会变长 |
| 大数值显示成固定上限 | 超出定点量程被饱和 | 数值先在 MCU 侧缩放（如除以 1000）再发 |
| `%f` 打印出 `Inf`/`NaN` | 超出 ±4294967295 或本身非有限 | 检查源变量，或先缩放 |
| 发 AT 命令没反应 | 没调 `LogTask()`，或命令没带 `\n` | 确认周期调用；VOFA+ 发送框要勾上换行 |
| 挂 Keil 调试后波形停 | GDB/调试器 halt 了内核 | 流式采集时不要同时挂 Keil 调试或 Ozone |
| 连接失败 `Cannot connect` | `DEVICE` 器件名写错 | 核对步骤 3 的器件名写法 |

## 注意事项

- `SEGGER_RTT_Write()` 内部会进临界区（M3/M4/M7 走 `BASEPRI` 屏蔽可配置优先级中断，
  M0/M0+ 无 `BASEPRI` 退化成 `PRIMASK` 关全局中断），
  **不要在高优先级硬实时中断里高频调用**，会影响中断响应。
- FireWater 的 float→ASCII 转换有实打实的开销，**不要放进电流环等高频中断**。
  需要极限性能时改回 JustFloat，见下。
- `vofa_rtt.c` 的 `VOFA_FloatToStr()` **只做定点，不支持科学计数法**，
  超量程会饱和、NaN/Inf 输出 0 以保持帧结构完整。量程不够时在 MCU 侧先缩放。
- `LOG()` 与波形共用 0 号通道，同时用会污染 FireWater 数据流。
- 两个 bat 脚本**故意全 ASCII 无中文**。之前用 GBK 中文注释会导致 CMD 解析错乱，别加中文注释。

## 附：改回 JustFloat

需要极限性能（kHz 级采样、或 M0/M0+ 平台）时，把 `VOFA_RTT_Send()` 换成二进制组帧：

```c
/* 帧尾：小端 0x7F800000(+inf)，VOFA+ 以此切分帧边界 */
static const uint8_t VOFA_FrameTail[4] = {0x00, 0x00, 0x80, 0x7F};

void VOFA_RTT_Send(const float *data, unsigned ch_num)
{
    uint8_t  frame[VOFA_CH_MAX * 4 + 4];
    unsigned len = 0;

    if ((data == 0) || (ch_num == 0) || (ch_num > VOFA_CH_MAX))
        return;

    /* ARM Cortex-M 默认小端，float 内存布局与 JustFloat 一致，可直接 memcpy */
    memcpy(&frame[len], data, ch_num * 4u);
    len += ch_num * 4u;
    memcpy(&frame[len], VOFA_FrameTail, 4u);
    len += 4u;

    SEGGER_RTT_Write(VOFA_RTT_BUF_IDX, frame, len);
}
```

同时要改：

- 加 `#include <string.h>`（memcpy）。`<math.h>` 仍要留着，`TestLoop` 的 `sinf/cosf` 用得到。
- VOFA+ 数据格式改选 **JustFloat**
- 两个 bat 里 banner 的协议提示文字改掉（只是提示，不改也能跑）
- `LOG()` 和 `log_demo` 的输出**绝对不能与波形混用**——
  二进制流里混进文本会直接破坏帧对齐，比 FireWater 时更严重
- JustFloat **依赖小端序**，移植到大端平台要逐通道字节翻转
