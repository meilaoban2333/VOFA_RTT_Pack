/*
***********************************************************************
* 文件名：log_demo.h
* 说  明：RTT 下行命令 + 按需打印波形（可选，不依赖它也能用波形输出）
*
* 用途：把要观察的变量登记成一张表，运行时用 VOFA+ / RTT Viewer 发命令动态开关，
*       不用改代码重新烧录就能切换看哪几组波形。
*
* 用法：本文件和 log_demo.c 不用改，在自己的文件里写表、初始化、周期调用：
*
*   #include "log_demo.h"
*
*   static const LogItem s_logItems[] = {
*       LOG_F32(g_angle, 1),     // 1 号：电角度
*       LOG_F32(g_iabc,  3),     // 2 号：三相电流（数组前 3 个元素）
*       LOG_U16(g_adcVbus, 1),   // 3 号：母线 ADC 原始值，发送时转成 float
*       LOG_I32(g_speed, 2),     // 4 号：数组前 2 个元素
*   };
*
*   VOFA_RTT_Init();
*   LogInit(s_logItems, sizeof(s_logItems) / sizeof(s_logItems[0]));
*   LogSetCmdHook(MyCmd);        // 可选：处理非开关类命令
*   ...每 20~50ms 调一次 LogTask();
*
* 编号按表顺序自动分配，第 1 行是 1 号；一帧里的通道顺序 = 表顺序。
*
* 命令格式：AT+号[,号...]，以回车或换行结束，号之间用逗号或空格分隔
*   AT+1      -> 切换 1 号（再发一次即关闭，每号独立翻转）
*   AT+1,2    -> 同时切换 1 号和 2 号，都开着就拼在同一帧里打印
*   AT+0      -> 全部关闭
* 打开某项后一帧总通道数会超过 VOFA_CH_MAX 时，这次打开不生效（保持关闭）。
* 不符合上述格式的非空行（如 AT+KP=1.5）交给 LogSetCmdHook 注册的回调，未注册则丢弃。
*
* 输出走 VOFA_RTT_Send()，帧格式跟随 vofa_rtt.h 的 VOFA_PROTOCOL，两种协议下都不用改。
* 命令走 RTT 0 号【下行】通道，与上行协议无关，JustFloat 下照常可用。
*
* 注意：
*   1. 整数类型发送时转成 float，绝对值超过 16777216 的 32 位整数会丢精度
*      （VOFA+ 只认 float）。
*   2. 登记的变量需自然对齐（普通全局变量都满足）；packed 结构体成员不行，
*      M0/M0+ 上非对齐访问会 HardFault。
*   3. 多通道项读取时不关中断，中断里正在改的数组可能前后通道来自不同时刻。
***********************************************************************
*/

#ifndef __LOG_DEMO_H__
#define __LOG_DEMO_H__

#include <stdint.h>   /* uint8_t / uint32_t，不依赖 HAL 的 main.h */
#include "vofa_rtt.h" /* VOFA_CH_MAX，表项宏的编译期检查要用 */

/* 最多登记多少项，超出的表项被忽略。每项开关占 1 bit，可在编译选项里改 */
#ifndef LOG_ITEM_MAX
#define LOG_ITEM_MAX 64
#endif

/* 一条命令最多多少字符（不含换行），超长的整行作废。回调也受这个长度限制 */
#ifndef LOG_LINE_MAX
#define LOG_LINE_MAX 32
#endif

/* 表项数据类型 */
#define LOG_T_F32  0
#define LOG_T_I32  1
#define LOG_T_U32  2
#define LOG_T_I16  3
#define LOG_T_U16  4
#define LOG_T_I8   5
#define LOG_T_U8   6

/* 一个观察项：从 addr 起连续 num 个 type 类型的元素，每个元素占一个通道 */
typedef struct {
    const volatile void *addr;  /* 变量或数组首地址 */
    uint8_t type;               /* LOG_T_xxx */
    uint8_t num;                /* 连续通道数 */
} LogItem;

/* 表项宏：LOG_F32(变量, 通道数)，变量可以是单个变量或数组。
   编译期检查（不满足时数组长度为负，编译报错）：
     通道数 >= 1、<= VOFA_CH_MAX，且 通道数 × 元素大小 <= sizeof(变量)，
   能拦住"通道数写多了越界读"和"小类型变量用了大类型宏"。
   手工填 LogItem（不走宏）时没有这层检查，运行时通道数为 0 或超 VOFA_CH_MAX 的项开不了 */
#define LOG_ITEM_(var, t, esz, n) \
    { (const volatile void *)&(var), (t), \
      (uint8_t)((n) + 0u * sizeof(char[((n) >= 1 && (n) <= VOFA_CH_MAX && \
                                        (n) * (esz) <= sizeof(var)) ? 1 : -1])) }

#define LOG_F32(var, n)  LOG_ITEM_(var, LOG_T_F32, sizeof(float),    n)
#define LOG_I32(var, n)  LOG_ITEM_(var, LOG_T_I32, sizeof(int32_t),  n)
#define LOG_U32(var, n)  LOG_ITEM_(var, LOG_T_U32, sizeof(uint32_t), n)
#define LOG_I16(var, n)  LOG_ITEM_(var, LOG_T_I16, sizeof(int16_t),  n)
#define LOG_U16(var, n)  LOG_ITEM_(var, LOG_T_U16, sizeof(uint16_t), n)
#define LOG_I8(var, n)   LOG_ITEM_(var, LOG_T_I8,  sizeof(int8_t),   n)
#define LOG_U8(var, n)   LOG_ITEM_(var, LOG_T_U8,  sizeof(uint8_t),  n)

/* 非开关命令回调：line 以 '\0' 结尾、已去掉换行，len 为字符数。在 LogTask() 里调用 */
typedef void (*LogCmdHook)(const char *line, unsigned len);

/* 登记观察项表并清空所有开关。表要在 LogTask() 运行期间一直有效（用 static/全局）。
   items 为空指针时当 0 项；num 超过 LOG_ITEM_MAX 时只登记前 LOG_ITEM_MAX 项 */
void LogInit(const LogItem *items, unsigned num);

/* 注册非开关命令回调，传 0 解除 */
void LogSetCmdHook(LogCmdHook hook);

/* 周期调用：解析下行命令 + 打印已开启项。建议 20~50ms 一次 */
void LogTask(void);

#endif
