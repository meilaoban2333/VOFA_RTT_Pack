# VOFA_RTT_Pack

SEGGER RTT + VOFA+ 实时波形输出的自包含移植包。经 J-Link 把 MCU 内部变量推给 VOFA+，
不占用串口，缓冲满自动丢帧不阻塞主循环。

已在 STM32G070RB（Cortex-M0+）+ Keil AC6 + J-Link V864 上验证。

## 目录结构

```
VOFA_RTT_Pack/
├── SKILL.md                 # Claude Code 技能入口，让 AI 按流程自动移植
├── README.md                # 本文件，人工移植看这个
├── assets/
│   ├── RTT/                 # 直接拷进工程的六个源文件
│   │   ├── SEGGER_RTT.c/.h
│   │   ├── SEGGER_RTT_Conf.h
│   │   ├── SEGGER_RTT_printf.c
│   │   └── vofa_rtt.c/.h    # JustFloat 组帧封装层
│   └── scripts/
│       ├── start_rtt.bat        # 默认用这个，J-Link Commander
│       └── start_rtt_gdb.bat    # 备选，J-Link GDB Server
└── references/
    └── porting.md           # Keil / EIDE / CMake 三种构建方式的逐项配置
```

## 两种用法

**给 Claude 用**：整个文件夹拷到新工程，对 Claude 说"按 VOFA_RTT_Pack 移植 RTT"，
它读 `SKILL.md` 自动完成拷文件、改工程配置、改器件名。
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

### 3. 改器件名

两个 bat 拷到工程根目录，**改 `DEVICE`** 为实际芯片：

```bat
set DEVICE=STM32G070RB
```

用 SEGGER 支持列表的写法，一般是去掉封装/温度后缀的型号。写错连不上目标。

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

双击 `start_rtt.bat`，等出现 `Connected`，然后 VOFA+ 里配：

| 项 | 值 |
|----|-----|
| 数据接口 | TCP 客户端 |
| 服务器 IP | 127.0.0.1 |
| 网络端口 | 19021 |
| 握手数据 | 留空 |
| 数据格式 | **JustFloat** |

看到 1Hz 正弦 + 余弦两条波，说明链路通了。

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
| 端口通了但无数据 | 内核被 halt，在 `J-Link>` 敲 `g` 回车放行 |
| 波形出一小段就停 | 用本包脚本，末尾 `sleep 86400000` 保持连接 |
| 端口占用 / 变成 19022 | 看脚本 banner 的实际端口，VOFA+ 里改成对应值 |
| 数据乱码 | RTT Telnet 同时只允许一个客户端，关掉 J-Scope / RTT Viewer |
| 挂 Keil 调试后波形停 | 采集时别同时挂调试器，GDB 客户端会 halt 内核 |
| `Cannot connect` | bat 里 `DEVICE` 器件名写错 |
| 编译报 undefined reference | `SEGGER_RTT.c` 没加进编译 |

更完整的排查表见 `SKILL.md`。

## 设计要点

理解这几点能避免踩坑，改动前先看：

- **必须用 RTT 通道 0**。19021 端口固定绑 0 号上行通道，改成 1 号收不到数据。
- **J-Link 连接后会 halt 内核**，不执行 `g` 则 `main()` 不跑，RTT 缓冲永远空。
  两个 bat 都已处理，`start_rtt.bat` 靠命令文件里的 `g`，
  `start_rtt_gdb.bat` 分两阶段（Commander 先 reset+go，再用 `-nohalt` 挂 GDB Server）。
- **Commander 脚本跑完会断连**，所以命令文件末尾放 `sleep 86400000`（24h）保持连接。
- **走 JustFloat 二进制，不做浮点转字符串**：M0+ 无 FPU 且无硬件除法，float 转 ASCII
  单通道约 28us，是 `SEGGER_RTT_Write` 本身(约 1~2us)的几十倍，格式化才是真瓶颈。
  JustFloat 直接 memcpy 原始字节，**依赖小端序**（ARM Cortex-M 默认满足）。
- **bat 脚本故意全 ASCII 无中文**，GBK 中文注释会导致 CMD 解析错乱，别加中文。
- **`SEGGER_RTT_Write()` 内部短暂关中断**（M0+ 走 `PRIMASK` 关全局中断），
  不要在高优先级硬实时中断里高频调用。
