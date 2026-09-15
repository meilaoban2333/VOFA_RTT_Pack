#ifndef __VOFA_RTT_H
#define __VOFA_RTT_H

#include "SEGGER_RTT.h"

/* ============================ VOFA+ RTT 波形输出 =============================
 * 用途：通过 SEGGER RTT 向 VOFA+ 上位机推送 float 波形，用于实时观察控制环变量。
 *
 * 数据通路：
 *   MCU --SEGGER_RTT_Write--> RTT 0 号上行缓冲(RAM)
 *       --J-Link 经 SWD 后台读走--> RTT Telnet Server(127.0.0.1:19021) --> VOFA+
 *   注意 19021 端口固定绑定 0 号上行通道，所以本模块必须用通道 0，不能用 1。
 *
 * 协议格式：FireWater（纯文本，逗号分隔）
 *   一帧 = 各通道十进制数值，逗号分隔，以 '\n' 结尾，例如双通道：
 *       "12.345,-6.789\n"
 *   VOFA+ 按 '\n' 切分帧，按逗号切分通道，通道数由每帧的字段个数决定。
 *   文本协议可读性好，RTT Viewer / 串口助手里能直接看懂数据，便于排查。
 *
 *   代价是格式化开销：float 转 ASCII 远比 memcpy 原始字节贵。本模块没有调用
 *   C 库 sprintf(体积大、且不支持 %f 的 SEGGER_RTT_printf 也用不了)，而是自带
 *   定点转换 VOFA_FloatToStr()，只做整数除法取位，避免拖进浮点格式化例程。
 *   即便如此，仍不宜在 FOC 电流环等高频中断里调用，需要极限性能时改回 JustFloat。
 *
 * 上位机配置：
 *   数据接口   : TCP 客户端
 *   服务器 IP  : 127.0.0.1
 *   网络端口   : 19021
 *   握手数据   : 留空
 *   数据格式   : FireWater
 *
 * J-Link Commander 侧（必须先开 RTT 服务，否则 19021 不监听）：
 *   J-Link>connect
 *   Device>STM32F407VE      <- 只填器件名，改成自己的芯片型号
 *   TIF>S
 *   Speed>4000
 *   J-Link>rtt start
 *
 * 注意：
 *   1. RTT Telnet 同一时刻只允许一个客户端，J-Scope / RTT Viewer 需全部关闭。
 *   2. SEGGER_RTT_Write 内部会进临界区（M3/M4/M7 走 BASEPRI 屏蔽可配置优先级中断，
 *      M0/M0+ 无 BASEPRI，退化成 PRIMASK 关全局中断），不要在电流环等高优先级
 *      硬实时中断里高频调用。
 * ========================================================================== */

/* RTT 上行通道索引：RTT Telnet Server(19021) 固定绑 0 号通道，不可改 */
#define VOFA_RTT_BUF_IDX        0
/* 每帧最多几个通道（决定组帧栈缓冲大小），通道多时调大 */
#define VOFA_CH_MAX             8
/* 每通道输出几位小数，0 表示只输出整数部分（不带小数点） */
#define VOFA_DECIMALS           3
/* 单个数值的最大字符数：符号 1 + 整数位 10 + 小数点 1 + 小数位 VOFA_DECIMALS */
#define VOFA_NUM_MAX            (12 + VOFA_DECIMALS)

/* 初始化：0 号通道由 SEGGER_RTT_Init 预置，这里只做显式初始化，需在 main() 前段调用 */
void VOFA_RTT_Init(void);

/* 推送一帧 FireWater 数据，data 指向 ch_num 个 float（ch_num <= VOFA_CH_MAX） */
void VOFA_RTT_Send(const float *data, unsigned ch_num);

/* 推送单通道的便捷接口 */
void VOFA_RTT_Send1(float ch0);

/* 推送双通道的便捷接口 */
void VOFA_RTT_Send2(float ch0, float ch1);

/* 主循环里调用，内部按 VOFA_TEST_PERIOD_MS 节流后发送正弦波测试数据 */
void VOFA_RTT_TestLoop(void);

/* ---------------------------- 调试文本打印 ---------------------------------
 * LOG(...) 用法与 printf 完全一致，直接走 RTT 0 号通道输出到 RTT Viewer /
 * J-Link Commander 终端，不占串口。
 *
 *   LOG("id=%d speed=%.2f flag=%s\n", id, speed, ok ? "OK" : "NG");
 *
 * 支持的格式：%c %d %u %x %X %s %p %f %F %% ，以及 '-' '0' '+' '#' 标志、
 * 字段宽度和精度（如 %8.3f、%-10s、%08.2f）。
 *   %f 是本工程给 SEGGER_RTT_printf 补的（原版不支持），定点输出，
 *   默认 6 位小数，量程 ±4294967295，超出或 NaN 打印 Inf/NaN 标记。
 *
 * 输出长度不受限：内部 64 字节缓冲写满会自动 flush，长字符串照样完整输出。
 *
 * 注意：与波形共用 0 号通道。VOFA+ 走 FireWater 解析时，混进来的文本会被
 *       当成数据行，所以采集波形期间不要开 LOG，两者择一使用。
 * ------------------------------------------------------------------------- */
#define LOG(...)    SEGGER_RTT_printf(VOFA_RTT_BUF_IDX, __VA_ARGS__)

/* 测试波形发送周期(ms)，即采样率 1000/该值 Hz。
   FireWater 双通道单帧约 20 字节(比 JustFloat 的 12 字节大)，10ms(100Hz) 对 2K 缓冲仍充足 */
#define VOFA_TEST_PERIOD_MS     10
/* 测试正弦波频率(Hz) */
#define VOFA_SINE_FREQ_HZ       1.0f
/* 测试正弦波幅值 */
#define VOFA_SINE_AMP           100.0f

#endif /* __VOFA_RTT_H */
