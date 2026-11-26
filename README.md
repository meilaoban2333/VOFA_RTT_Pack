# VOFA_RTT_Pack

**中文** | [English](README.en.md)

把 MCU 里的变量经 SEGGER RTT 实时推给 VOFA+ 画波形的移植包。

- **不占串口**：走调试器的 SWD，缓冲满自动丢帧，不阻塞主循环。
- **换调试器不改固件**：J-Link、CMSIS-DAP（DAPLink v1/v2）、ST-Link 自动识别。
- **编译即下载**：Keil 按 F7 / EIDE 点 Build，约 2 秒后自动下载，波形接着推。
- **双协议**：FireWater（文本）和 JustFloat（二进制），改一个宏切换，发送代码不用动。

全部配置项、接口、底层配置和设计细节见 **[详细手册](references/manual.md)**。

## 环境要求

- Windows 10 / 11，不需要管理员权限，不需要 Python
- 上位机 VOFA+
- J-Link：装 SEGGER J-Link 软件，或用 EIDE 自带的；CMSIS-DAP / ST-Link：OpenOCD 0.12 及以上，EIDE 自带的即可
- 编译环境不限：Keil MDK（AC5/AC6）、EIDE、CMake / Makefile + arm-none-eabi-gcc

探针驱动、占用端口等完整清单见手册「环境要求」。

## 快速开始

**交给 Claude**：把整个文件夹拷到新工程，对 Claude 说"按 VOFA_RTT_Pack 移植 RTT"，它会读 `SKILL.md`
自动完成下面几步。也可以拷到 `.claude/skills/vofa-rtt/` 变成常驻技能。

**人工移植**按下面五步：

**1. 拷源码、加编译**：`assets/RTT/` 六个文件拷进工程（如 `User/RTT/`），三个 `.c`
（`SEGGER_RTT.c`、`SEGGER_RTT_printf.c`、`vofa_rtt.c`）加入编译，这个目录加进头文件搜索路径。
Keil / EIDE / CMake 的具体改法见 [references/porting.md](references/porting.md)。

**2. 改代码**：

```c
#include "vofa_rtt.h"

int main(void)
{
    VOFA_RTT_Init();             // 放在外设和中断启动前
    HAL_Init();
    /* ... 外设初始化 ... */

    while (1)
    {
        VOFA_RTT_TestLoop();     // 自检正弦波，通了再换成自己的变量
        // VOFA_RTT_Send2(speed, target);
    }
}
```

发送要节流：放进固定周期的定时器回调，或参考 `VOFA_RTT_TestLoop()` 的 tick 节流写法；主循环裸跑会灌满缓冲。
非 HAL 工程把 `vofa_rtt.h` 末尾的 `VOFA_TICK_HEADER` / `VOFA_GET_TICK_MS()` 改成自己的毫秒时基。

**3. 拷脚本、改参数**：`assets/scripts/start_rtt.bat` 拷到**工程根目录**（编译即下载监视这个目录下的
`.axf` / `.elf`），改开头的 `set` 行。调试器类型自动识别，只需按调试器改芯片相关的几项：

| 调试器 | 要改的参数 | 例：STM32F407VE |
|---|---|---|
| J-Link | `DEVICE` | `STM32F407VE` |
| CMSIS-DAP / ST-Link | `TARGETCFG`、`RAM_START`、`RAM_SIZE` | `target/stm32f4x.cfg`、`0x20000000`、`0x20000` |

其余参数一般不用动，逐项说明见手册「人工移植五步」第 3 步。

**4. 开服务**：双击 `start_rtt.bat`，看到 `RTT stream connected` 就绪。
窗口里按 `P` 暂停并释放探针（要用 Keil 调试时），`R` 立即重连，`Q` 退出。

**5. 连 VOFA+**：数据接口选 TCP 客户端，IP `127.0.0.1`，端口 `19021`，握手数据留空，
数据格式选 FireWater（与 `VOFA_PROTOCOL` 一致）。看到正弦、余弦两条波就通了。

## 选哪个协议

在 `vofa_rtt.h` 改 `VOFA_PROTOCOL`，**VOFA+ 的数据格式要同步改**，两边不一致就没波形。

- **FireWater（默认）**：文本，用任意 TCP 调试工具就能看懂原始数值，还能用 `LOG()` 打印文本。建议先用它调通。
- **JustFloat**：二进制，开销只有 memcpy、帧更小、完整 float 精度。采样率到 kHz 级、目标是 Cortex-M0/M0+，
  或需要完整精度时再切。

详细对比见手册「选哪个协议」。

## 可选功能

| 功能 | 一句话 | 详见手册 |
|---|---|---|
| `LOG()` 文本打印 | 用法同 `printf`，支持 `%f`；仅 FireWater，会和波形混在同一个数据流里，采集波形时别开 | 「LOG() 文本打印」 |
| 运行时开关观察项 | 把变量登记成一张表，在 VOFA+ 里发 `AT+1`、`AT+1,2` 切换看哪几组波形，不用重新烧录 | 「可选：下行命令动态开关观察项」 |
| 边看波形边下载 | 编译即下载默认开着；EIDE 的下载按钮、Keil 的 F8 也有对应用法 | 「边看波形边下载」 |
| 主机测试 | 改了固件源码后跑 `sh tests/run_tests.sh`，不用上板 | 「主机测试」 |

## 常见问题

| 现象 | 处理 |
|------|------|
| 一直 `waiting for a probe` | 探针没插好或驱动没装；同时插了多种探针时用 `PROBE` 写死 |
| `RTT stream connected` 了但标题速率一直 0 | 固件没跑到 `VOFA_RTT_Init()` 或内核被 halt：按 `R` 重连，或设 `RESET=1` |
| OpenOCD 反复 `rtt: No control block found` | `RAM_START` / `RAM_SIZE` 没覆盖 `_SEGGER_RTT`，按 `.map` 改 |
| J-Link 反复 `backend exited` | `DEVICE` 器件名写错，或目标没上电（上电后会自动连上） |
| 全无波形，或波形乱跳、通道错位 | VOFA+ 的数据格式和 `VOFA_PROTOCOL` 不一致 |
| 波形里混入乱跳的点 | FireWater 下 `LOG()` 文本混进了数据流，采集时关掉 LOG |
| 启动报 `Port 19021 is busy` | 已经开着一个 `start_rtt.bat`，或 J-Link RTT Viewer / J-Scope / Ozone 占着端口 |
| 编译报 undefined reference | `SEGGER_RTT.c` 等文件没加进编译 |

更多情况见手册「常见问题」。

## 验证情况

已实测：STM32G070RB + CMSIS-DAP v2 + 本脚本（自动识别、编译即下载、拔插探针后自动恢复），
以及 STM32F407VE / G070RB + Keil AC6 + J-Link（旧版脚本）。J-Link、ST-Link 配本脚本等组合尚未实测，
逐项情况见手册「验证情况」。
