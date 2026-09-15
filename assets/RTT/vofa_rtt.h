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
 * 支持两种 VOFA+ 协议，用下面的 VOFA_PROTOCOL 宏切换，默认 FireWater。
 * 切换后必须同步改 VOFA+ 上位机的"数据格式"设置，否则解析不出波形。
 *
 * 上位机配置：
 *   数据接口   : TCP 客户端
 *   服务器 IP  : 127.0.0.1
 *   网络端口   : 19021
 *   握手数据   : 留空
 *   数据格式   : 与 VOFA_PROTOCOL 一致（FireWater 或 JustFloat）
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

/* -------------------------- 协议选择 ---------------------------------------
 * VOFA_PROTO_FIREWATER：纯文本，逗号分隔，'\n' 结尾
 *   一帧 = 各通道十进制数值，例如双通道："12.345,-6.789\n"
 *   VOFA+ 按 '\n' 切帧、按逗号切通道，通道数由每帧字段个数决定。
 *   优点：RTT Viewer / 串口助手里能直接看懂数据，排查时不用猜是发送端还是
 *         上位机配错；不依赖字节序；可与 LOG() 共用通道（代价见 LOG 一节）。
 *   代价：float 转 ASCII 的格式化开销。本模块不调 C 库 sprintf(体积大)，
 *         而是自带定点转换 VOFA_FloatToStr()，只做整数除法取位，
 *         避免拖进浮点格式化例程。即便如此，仍不宜在电流环等高频中断里调用。
 *
 * VOFA_PROTO_JUSTFLOAT：二进制小端 float
 *   一帧 = ch_num 个 4 字节小端 float，后接 4 字节固定帧尾 00 00 80 7F，
 *   例如双通道：[f0:4B][f1:4B][00 00 80 7F]
 *   VOFA+ 按帧尾切分，帧内字节数除以 4 即通道数，无需额外握手。
 *   优点：直接 memcpy 原始字节，无任何数值转换，开销仅 memcpy；帧也更小。
 *   代价：流里是二进制，RTT Viewer 看是乱码；依赖小端序（ARM Cortex-M 默认
 *         满足，移植到大端平台要逐通道字节翻转）；不能与 LOG() 共用通道，
 *         故 JustFloat 下 LOG() 被编译期禁用，详见下方 LOG 一节。
 *
 * 怎么选：默认 FireWater，可读性在排查阶段很值。
 *   改用 JustFloat 的两种情形：
 *     1) 采样率要求高（kHz 级），格式化开销扛不住；
 *     2) 目标是 Cortex-M0/M0+，无 FPU 且无硬件除法，float 转 ASCII 单通道
 *        实测约 28us，是 RTT 写入本身(memcpy 约 1~2us)的几十倍——
 *        此时格式化而非传输才是真正的瓶颈。
 * ------------------------------------------------------------------------- */
#define VOFA_PROTO_FIREWATER    0
#define VOFA_PROTO_JUSTFLOAT    1

/* 改这一行切换协议，记得同步改 VOFA+ 上位机的"数据格式" */
#define VOFA_PROTOCOL           VOFA_PROTO_FIREWATER

/* RTT 上行通道索引：RTT Telnet Server(19021) 固定绑 0 号通道，不可改 */
#define VOFA_RTT_BUF_IDX        0
/* 每帧最多几个通道（决定组帧栈缓冲大小），通道多时调大 */
#define VOFA_CH_MAX             8

#if (VOFA_PROTOCOL == VOFA_PROTO_FIREWATER)
/* 每通道输出几位小数，0 表示只输出整数部分（不带小数点）。仅 FireWater 用到 */
#define VOFA_DECIMALS           3
/* 单个数值的最大字符数：符号 1 + 整数位 10 + 小数点 1 + 小数位 VOFA_DECIMALS */
#define VOFA_NUM_MAX            (12 + VOFA_DECIMALS)
#endif

/* 初始化：0 号通道由 SEGGER_RTT_Init 预置，这里只做显式初始化，需在 main() 前段调用 */
void VOFA_RTT_Init(void);

/* 推送一帧数据，data 指向 ch_num 个 float（ch_num <= VOFA_CH_MAX）。
   帧格式由 VOFA_PROTOCOL 决定，调用方式两种协议完全一致 */
void VOFA_RTT_Send(const float *data, unsigned ch_num);

/* 推送单通道的便捷接口 */
void VOFA_RTT_Send1(float ch0);

/* 推送双通道的便捷接口 */
void VOFA_RTT_Send2(float ch0, float ch1);

/* 主循环里调用，内部按 VOFA_TEST_PERIOD_MS 节流后发送正弦波测试数据 */
void VOFA_RTT_TestLoop(void);

/* ---------------------------- 调试文本打印 ---------------------------------
 * LOG(...) 用法与 printf 完全一致，走 RTT 0 号通道输出到 RTT Viewer /
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
 * ★ 与协议的关系（两种协议下行为不同，务必看清）：
 *
 *   FireWater 下：LOG 可用，但与波形共用 0 号通道。VOFA+ 解析时混进来的文本
 *     会被当成数据行，波形上表现为乱跳的点。所以采集波形期间不要开 LOG，
 *     两者择一使用——只是"脏数据"，不会让解析崩掉。
 *
 *   JustFloat 下：LOG 被编译期禁用，展开成 ((void)0)。
 *     原因是 JustFloat 是纯二进制流，VOFA+ 靠 4 字节帧尾 00 00 80 7F 切帧、
 *     帧内每 4 字节当一个 float。混进 ASCII 文本会有两种后果：文本字节被当成
 *     float 解析出巨大乱跳值；或文本里凑巧出现帧尾字节序列导致帧边界错位，
 *     之后所有通道全部错位。这比 FireWater 的"脏数据"严重得多，无法只靠
 *     "别同时用"来规避，故直接在编译期断掉。
 *
 *     LOG 调用点无需加 #if 包裹，禁用后整条语句被编译掉（参数也不会求值，
 *     所以带副作用的表达式如 LOG("%d", i++) 在 JustFloat 下不会执行 i++，
 *     写日志时别把副作用放进参数里）。
 *     JustFloat 下确实需要文本输出时，两条路：临时切回 FireWater 重新编译；
 *     或自行改用 RTT 1 号通道（需把 SEGGER_RTT_Conf.h 的
 *     SEGGER_RTT_MAX_NUM_UP_BUFFERS 调到 2，并用 RTT Viewer 看 Terminal 1，
 *     注意 19021 只送 0 号通道，VOFA+ 收不到 1 号）。
 * ------------------------------------------------------------------------- */
#if (VOFA_PROTOCOL == VOFA_PROTO_FIREWATER)
#define LOG(...)    SEGGER_RTT_printf(VOFA_RTT_BUF_IDX, __VA_ARGS__)
#else
/* JustFloat 二进制流混入文本会破坏帧对齐，故编译期禁用，调用点无需改 */
#define LOG(...)    ((void)0)
#endif

/* 测试波形发送周期(ms)，即采样率 1000/该值 Hz。
   FireWater 双通道单帧约 20 字节，JustFloat 仅 12 字节，10ms(100Hz) 对 2K 缓冲
   两种协议都余量充足 */
#define VOFA_TEST_PERIOD_MS     10
/* 测试正弦波频率(Hz) */
#define VOFA_SINE_FREQ_HZ       1.0f
/* 测试正弦波幅值 */
#define VOFA_SINE_AMP           100.0f

#endif /* __VOFA_RTT_H */
