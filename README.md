# VOFA_RTT_Pack

SEGGER RTT + VOFA+ 实时波形输出的自包含移植包。经 J-Link 把 MCU 内部变量推给 VOFA+，
不占用串口，缓冲满自动丢帧不阻塞主循环。除波形外还提供 `LOG()` 文本打印和
RTT 下行命令（运行时开关观察项）。

走 **FireWater** 文本协议，原因和代价见下方「设计要点」。

已在 STM32F407VE（Cortex-M4）、STM32G070RB（Cortex-M0+）+ Keil AC6 + J-Link V864 上验证。

## 目录结构

```
VOFA_RTT_Pack/
├── SKILL.md                 # Claude Code 技能入口，让 AI 按流程自动移植
├── README.md                # 本文件，人工移植看这个
├── assets/
│   ├── RTT/                 # 直接拷进工程的六个源文件
│   │   ├── SEGGER_RTT.c/.h
│   │   ├── SEGGER_RTT_Conf.h
│   │   ├── SEGGER_RTT_printf.c   # 已补 %f 支持（SEGGER 原版不支持）
│   │   └── vofa_rtt.c/.h         # FireWater 组帧封装层 + LOG() 宏
│   ├── examples/
│   │   └── log_demo.c/.h    # 可选：下行 AT 命令动态开关日志项的模板
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

需要运行时开关观察项的，再拷 `assets/examples/log_demo.c/.h`，见「可选：下行命令」。

### 2. 加进构建

- 3 个 .c 加入编译：`SEGGER_RTT.c`、`SEGGER_RTT_printf.c`、`vofa_rtt.c`
- RTT 目录加进头文件搜索路径

Keil / EIDE / CMake / Makefile 的具体改法见 **`references/porting.md`**，
里面有图形界面步骤和可直接粘贴的配置片段。

### 3. 改器件名

两个 bat 拷到工程根目录，**改 `DEVICE`** 为实际芯片：

```bat
set DEVICE=STM32F407VE
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
节流写法，或放固定周期定时器回调里。FireWater 单帧约 (小数位+7)×通道数 字节，
几百 Hz 以内没问题。

### 5. 开服务 + 配 VOFA+

双击 `start_rtt.bat`，等出现 `Connected`，然后 VOFA+ 里配：

| 项 | 值 |
|----|-----|
| 数据接口 | TCP 客户端 |
| 服务器 IP | 127.0.0.1 |
| 网络端口 | 19021 |
| 握手数据 | 留空 |
| 数据格式 | **FireWater** |

看到 1Hz 正弦 + 余弦两条波，说明链路通了。

## 接口

```c
void VOFA_RTT_Init(void);                               // 初始化
void VOFA_RTT_Send1(float ch0);                         // 单通道
void VOFA_RTT_Send2(float ch0, float ch1);              // 双通道
void VOFA_RTT_Send(const float *data, unsigned ch_num); // N 通道
void VOFA_RTT_TestLoop(void);                           // 自检正弦波，可删

LOG("fmt", ...);                                        // printf 风格文本打印
```

`vofa_rtt.h` 里可调：

| 宏 | 默认 | 说明 |
|----|------|------|
| `VOFA_CH_MAX` | 8 | 最大通道数，**超过会整帧丢弃**，加通道要同步调大 |
| `VOFA_DECIMALS` | 3 | 每通道小数位数，调大帧变长 |
| `VOFA_TEST_PERIOD_MS` | 10 | 自检波形周期，即采样率 100Hz |
| `VOFA_SINE_FREQ_HZ` / `VOFA_SINE_AMP` | 1.0 / 100 | 自检正弦的频率和幅值 |

## LOG() 文本打印

用法与 `printf` 完全一致，走 RTT 0 号通道输出到 RTT Viewer / J-Link Commander 终端：

```c
LOG("id=%d speed=%.2f flag=%s\n", id, speed, ok ? "OK" : "NG");
```

支持 `%c %d %u %x %X %s %p %f %F %%`，含 `-` `0` `+` `#` 标志、字段宽度和精度。

**`%f` 是本包给 `SEGGER_RTT_printf` 补的**，SEGGER 原版不支持。定点实现，
默认 6 位小数，量程 ±4294967295，超出或 NaN 打印 `Inf`/`NaN` 标记而不是静默回绕。

⚠️ **LOG 与波形共用 0 号通道**。VOFA+ 按 FireWater 解析时，混进来的文本会被当成
数据行，波形上表现为乱跳的点。**采集波形期间不要开 LOG，两者择一。**

## 可选：下行命令动态开关观察项

`assets/examples/log_demo.c/.h` 是个模板：把观察项分组编号，运行时发命令开关，
不用改代码重烧就能换看哪几组波形。命令走 RTT 下行通道（VOFA+ 发送框或 RTT Viewer 输入行）：

```
AT+1\n      切换 1 号项
AT+1,2\n    同时切换 1、2 号，都开就一起打印
AT+1\n      再发一次同号即关闭（每号独立翻转）
AT+0\n      全部关闭
```

移植时改三处：`log_demo.h` 的 `LOG_ID_xxx` 表、`log_demo.c` 里 `LogPrint()` 的取值代码、
`LOG_CH_MAX`（须 ≤ `VOFA_CH_MAX`）。然后周期调用 `LogTask()`，建议 20~50ms 一次。

## 常见问题

| 现象 | 处理 |
|------|------|
| 端口通了但无数据 | 内核被 halt，在 `J-Link>` 敲 `g` 回车放行 |
| 波形出一小段就停 | 用本包脚本，末尾 `sleep 86400000` 保持连接 |
| 端口占用 / 变成 19022 | 看脚本 banner 的实际端口，VOFA+ 里改成对应值 |
| 数据乱码 | RTT Telnet 同时只允许一个客户端，关掉 J-Scope / RTT Viewer |
| 波形里混入乱跳的点 | `LOG()` 文本混进了数据流，采集时关掉 LOG |
| 某帧完全没出现 | `ch_num > VOFA_CH_MAX` 整帧被丢，调大 `VOFA_CH_MAX` |
| 数值精度不够 / 大数被截到上限 | 调大 `VOFA_DECIMALS`；超量程的先在 MCU 侧缩放 |
| 发 AT 命令没反应 | 没周期调 `LogTask()`，或发送时没带换行 |
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
- **走 FireWater 文本协议，换来可读性**：波形不对时能直接在 RTT Viewer 里看原始数值，
  不用猜是发送端错了还是上位机解析配错了。代价是 float→ASCII 的转换开销。
  本包不调 C 库 `sprintf`（体积大），自带定点转换 `VOFA_FloatToStr()`，只做整数除法取位。
  即便如此**也不要放进电流环等高频中断**。
- **什么时候该改回 JustFloat**：kHz 级采样，或目标是 Cortex-M0/M0+（无 FPU 且无硬件除法，
  float 转 ASCII 单通道实测约 28us，是 RTT 写入本身约 1~2us 的几十倍）。
  改法见 `SKILL.md` 末尾「附：改回 JustFloat」。
- **定点转换不支持科学计数法**：超量程会饱和到上限，NaN/Inf 输出 0 以保持帧结构完整。
  量程不够时在 MCU 侧先缩放再发。
- **bat 脚本故意全 ASCII 无中文**，GBK 中文注释会导致 CMD 解析错乱，别加中文。
- **`SEGGER_RTT_Write()` 内部会进临界区**（M3/M4/M7 走 `BASEPRI`，M0/M0+ 退化成 `PRIMASK`
  关全局中断），不要在高优先级硬实时中断里高频调用。
