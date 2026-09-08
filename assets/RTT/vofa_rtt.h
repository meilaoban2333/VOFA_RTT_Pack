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
 * 协议格式：JustFloat（二进制小端 float）
 *   一帧 = ch_num 个 4 字节小端 float，后接 4 字节固定帧尾 00 00 80 7F，例如双通道：
 *       [f0:4B][f1:4B][00 00 80 7F]
 *   VOFA+ 按帧尾切分，帧内字节数除以 4 即为通道数，无需额外握手。
 *
 *   之所以不用 FireWater 文本协议：本芯片是 Cortex-M0+，无 FPU 且无硬件除法，
 *   float 转 ASCII 需软件模拟浮点加除法，实测单通道约 28us，是 RTT 写入本身
 *   (memcpy 约 1~2us)的几十倍，格式化而非传输才是真正的瓶颈。JustFloat 直接
 *   memcpy 原始字节，彻底省掉这段开销。
 *
 * 上位机配置：
 *   数据接口   : TCP 客户端
 *   服务器 IP  : 127.0.0.1
 *   网络端口   : 19021
 *   握手数据   : 留空
 *   数据格式   : JustFloat
 *
 * J-Link Commander 侧（必须先开 RTT 服务，否则 19021 不监听）：
 *   J-Link>connect
 *   Device>STM32G070RB      <- 只填器件名
 *   TIF>S
 *   Speed>4000
 *   J-Link>rtt start
 *
 * 注意：
 *   1. RTT Telnet 同一时刻只允许一个客户端，J-Scope / RTT Viewer 需全部关闭。
 *   2. SEGGER_RTT_Write 内部会短暂关全局中断（M0+ 无 BASEPRI，走 PRIMASK），
 *      不要在高优先级硬实时中断里高频调用。
 * ========================================================================== */

/* RTT 上行通道索引：RTT Telnet Server(19021) 固定绑 0 号通道，不可改 */
#define VOFA_RTT_BUF_IDX        0
/* 每帧最多几个通道（决定组帧栈缓冲大小） */
#define VOFA_CH_MAX             4

/* 初始化：0 号通道由 SEGGER_RTT_Init 预置，这里只做显式初始化，需在 main() 前段调用 */
void VOFA_RTT_Init(void);

/* 推送一帧 JustFloat 数据，data 指向 ch_num 个 float（ch_num <= VOFA_CH_MAX） */
void VOFA_RTT_Send(const float *data, unsigned ch_num);

/* 推送单通道的便捷接口 */
void VOFA_RTT_Send1(float ch0);

/* 推送双通道的便捷接口 */
void VOFA_RTT_Send2(float ch0, float ch1);

/* 主循环里调用，内部按 VOFA_TEST_PERIOD_MS 节流后发送正弦波测试数据 */
void VOFA_RTT_TestLoop(void);

/* 测试波形发送周期(ms)，即采样率 1000/该值 Hz。
   JustFloat 双通道单帧仅 12 字节，10ms(100Hz) 对 2K 缓冲余量充足 */
#define VOFA_TEST_PERIOD_MS     10
/* 测试正弦波频率(Hz) */
#define VOFA_SINE_FREQ_HZ       1.0f
/* 测试正弦波幅值 */
#define VOFA_SINE_AMP           100.0f

#endif /* __VOFA_RTT_H */
