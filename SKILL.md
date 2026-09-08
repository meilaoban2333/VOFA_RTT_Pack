---
name: vofa-rtt
description: 使用 SEGGER RTT 向 VOFA+ 上位机推送实时波形。当用户要给 STM32/Cortex-M 工程加 RTT 调试、用 VOFA+ 看波形曲线、移植 RTT 到新工程，或排查 RTT 无数据/端口占用/波形不刷新时使用。覆盖 Keil MDK、EIDE、CMake 三种构建方式的配置改动。
---

# VOFA+ RTT 波形输出移植

通过 SEGGER RTT 经 J-Link 把 MCU 内部变量以 JustFloat 二进制协议推给 VOFA+，无需占用串口，不阻塞主循环。

## 数据通路

```
MCU 代码  --VOFA_RTT_Send()-->  RTT 0 号上行缓冲(RAM)
         --J-Link 经 SWD 后台读走-->  RTT Telnet Server(127.0.0.1:19021)
         --TCP-->  VOFA+
```

关键约束：**19021 端口固定绑定 RTT 0 号上行通道**，所以必须用通道 0，不能改成 1。

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

## 步骤 3：改脚本器件名

`assets/scripts/` 下两个 bat 拷到工程根目录，**必须改 `DEVICE`** 为实际芯片：

```bat
set DEVICE=STM32G070RB
```

器件名用 SEGGER 支持列表里的写法（一般是去掉封装/温度后缀的型号，如
`STM32G070RB`、`STM32F407VE`、`STM32H743XI`）。写错会连不上目标。

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
JustFloat 单帧仅 4×通道数+4 字节，500Hz 以内都很轻松。

## 步骤 6：VOFA+ 上位机配置

先双击运行 `start_rtt.bat`，等出现 `Connected`，再在 VOFA+ 里：

| 项 | 值 |
|----|-----|
| 数据接口 | TCP 客户端 |
| 服务器 IP | 127.0.0.1 |
| 网络端口 | 19021 |
| 握手数据 | 留空 |
| 数据格式 | **JustFloat** |

JustFloat 帧格式：N 个 4 字节小端 float + 4 字节帧尾 `00 00 80 7F`，帧内字节数除以 4 即通道数。

## 故障排查

按现象查，**不要跳过第一条**——绝大多数"没波形"都是内核被 halt：

| 现象 | 原因 | 处理 |
|------|------|------|
| 端口通了但无数据 | 内核halt在断点，`main()`没跑 | 在 `J-Link>` 提示符敲 `g` 回车 |
| 波形只出一小段就停 | Commander 脚本跑完就断连 | 用本包脚本，末尾 `sleep 86400000` 保持连接 |
| 连不上/端口占用 | 残留 JLink 进程占着 19021 | 本包脚本已自动 `taskkill`；手工查 `netstat -ano \| findstr 1902` |
| 端口变成 19022 | 19021 被占，J-Link 自动顺延 | 看脚本 banner 实际端口，VOFA+ 里改成对应值 |
| 数据乱码/串行 | 多客户端抢同一 RTT | RTT Telnet 同时只允许一个客户端，关掉 J-Scope / RTT Viewer / Ozone |
| 波形卡顿丢点 | 发送太快缓冲溢出 | 降低发送频率，或调大 `BUFFER_SIZE_UP` |
| 挂 Keil 调试后波形停 | GDB/调试器 halt 了内核 | 流式采集时不要同时挂 Keil 调试或 Ozone |
| 连接失败 `Cannot connect` | `DEVICE` 器件名写错 | 核对步骤 3 的器件名写法 |

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
- 两个 bat 脚本**故意全 ASCII 无中文**。之前用 GBK 中文注释会导致 CMD 解析错乱，别加中文注释。
