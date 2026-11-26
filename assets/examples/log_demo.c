/*
***********************************************************************
* 文件名：log_demo.c
* 说  明：RTT 下行命令 + 按需打印波形，命令与数据都走 RTT 0 号通道
*
* 观察项由用户在自己的文件里登记成 LogItem 表（见 log_demo.h），本文件不用改。
* 开关用位图保存：每项占 1 bit，收到号就翻转，多项天然可以叠加打印。
*
* 输出调 VOFA_RTT_Send()，具体帧格式由 vofa_rtt.h 的 VOFA_PROTOCOL 决定，
* 本文件两种协议通用。整帧一次性写入 RTT，避免分次写入被拆散导致上位机错位。
*
* 不输出任何文本（不回显开关状态）：FireWater 下文本会被当成数据行，
* JustFloat 下会直接破坏二进制帧对齐，两种协议都不能混入文本。
***********************************************************************
*/

#include "log_demo.h"

/* 开关位图的字数 */
#define LOG_ON_WORDS ((LOG_ITEM_MAX + 31) / 32)

static const LogItem *s_items = 0;
static unsigned s_num = 0;
static uint32_t s_on[LOG_ON_WORDS]; /* bit i 对应表里第 i 项（即 i+1 号） */
static LogCmdHook s_hook = 0;

/* 行缓冲：RTT 下行缓冲仅 BUFFER_SIZE_DOWN(默认16) 字节，命令要跨多次读取拼接。
   多 1 字节放 '\0'，回调拿到的是 C 字符串 */
static char s_line[LOG_LINE_MAX + 1];
static unsigned s_len = 0;
static uint8_t s_drop = 0; /* 当前行已超长，丢到行尾为止 */

void LogInit(const LogItem *items, unsigned num)
{
    unsigned i;

    if (items == 0)
        num = 0;
    if (num > LOG_ITEM_MAX)
        num = LOG_ITEM_MAX;

    s_items = items;
    s_num = num;
    for (i = 0; i < LOG_ON_WORDS; i++)
        s_on[i] = 0;
}

void LogSetCmdHook(LogCmdHook hook)
{
    s_hook = hook;
}

static uint8_t LogIsOn(unsigned idx)
{
    return (uint8_t)((s_on[idx >> 5] >> (idx & 31u)) & 1u);
}

/* 已开启项的通道总数。每次现算而不是另存计数，表在运行时被改也不会算错 */
static unsigned LogChOn(void)
{
    unsigned i;
    unsigned sum = 0;

    for (i = 0; i < s_num; i++) {
        if (LogIsOn(i))
            sum += s_items[i].num;
    }
    return sum;
}

/* 翻转 id 号（从 1 起），0 号全关，超出项数的号忽略 */
static void LogToggle(unsigned id)
{
    unsigned i;
    unsigned idx;
    unsigned num;
    uint32_t bit;

    if (id == 0) {
        for (i = 0; i < LOG_ON_WORDS; i++)
            s_on[i] = 0;
        return;
    }
    if (id > s_num)
        return;

    idx = id - 1u;
    bit = (uint32_t)1u << (idx & 31u);
    num = s_items[idx].num;

    if (LogIsOn(idx)) {
        s_on[idx >> 5] &= ~bit; // 关闭总是生效
    } else if (num >= 1u && num <= VOFA_CH_MAX && LogChOn() + num <= VOFA_CH_MAX) {
        /* 打开后一帧会超 VOFA_CH_MAX 就不开：超了 VOFA_RTT_Send 会整帧丢弃，
           所有曲线一起消失，不如只让这一项开不了 */
        s_on[idx >> 5] |= bit;
    }
}

/* 是否开关命令："AT+" 后只含数字、逗号、空格，且至少一个数字。
   其余格式（如 AT+KP=1.5）留给用户回调 */
static uint8_t LogIsToggleCmd(const char *line, unsigned len)
{
    unsigned i;
    uint8_t hasDigit = 0;

    if (len < 4 || line[0] != 'A' || line[1] != 'T' || line[2] != '+')
        return 0;

    for (i = 3; i < len; i++) {
        if (line[i] >= '0' && line[i] <= '9')
            hasDigit = 1;
        else if (line[i] != ',' && line[i] != ' ')
            return 0;
    }
    return hasDigit;
}

/* 逐段取十进制号并翻转 */
static void LogCmdToggle(const char *line, unsigned len)
{
    unsigned i = 3;
    unsigned id;

    while (i < len) {
        if (line[i] < '0' || line[i] > '9') {
            i++;
            continue;
        }

        id = 0;
        while (i < len && line[i] >= '0' && line[i] <= '9') {
            /* 超过 LOG_ITEM_MAX 就不再累加，后面判为无效号；否则长数字会回绕成小号误触发 */
            if (id <= LOG_ITEM_MAX)
                id = id * 10u + (unsigned)(line[i] - '0');
            i++;
        }
        LogToggle(id);
    }
}

/* 执行一条完整的命令行，s_line 已去掉换行 */
static void LogLineExec(void)
{
    s_line[s_len] = '\0';

    if (LogIsToggleCmd(s_line, s_len))
        LogCmdToggle(s_line, s_len);
    else if (s_hook != 0)
        s_hook(s_line, s_len);
}

/*
 * 收下行字节并按换行符组行
 * RTT 读取是非阻塞的，没数据就立即返回 0
 */
static void LogCmdPoll(void)
{
    char buf[16];
    unsigned n;
    unsigned i;
    char ch;

    /* 绝大多数周期没有命令：先无锁判空，省掉 SEGGER_RTT_Read 里的关中断 */
    if (SEGGER_RTT_HASDATA(VOFA_RTT_BUF_IDX) == 0)
        return;

    /* 按块读，每块只进一次临界区 */
    while ((n = SEGGER_RTT_Read(VOFA_RTT_BUF_IDX, buf, sizeof(buf))) > 0) {
        for (i = 0; i < n; i++) {
            ch = buf[i];
            /* '\r' 和 '\n' 都当行结束，兼容上位机只发其中一种；CRLF 间的空行直接跳过 */
            if (ch == '\n' || ch == '\r') {
                if (s_len > 0 && !s_drop)
                    LogLineExec();
                s_len = 0;
                s_drop = 0;
            } else if (s_drop) {
                /* 超长行的剩余部分整段丢弃：若从中间重新开始拼，
                   行尾恰好是 "AT+..." 时会被当成一条新命令执行 */
            } else if (s_len < LOG_LINE_MAX) {
                s_line[s_len++] = ch;
            } else {
                s_len = 0; // 超长行判为无效
                s_drop = 1;
            }
        }
    }
}

/* 读表项第 k 个元素并转成 float，未知类型给 0 */
static float LogReadF(const LogItem *it, unsigned k)
{
    switch (it->type) {
    case LOG_T_F32: return ((const volatile float *)it->addr)[k];
    case LOG_T_I32: return (float)((const volatile int32_t *)it->addr)[k];
    case LOG_T_U32: return (float)((const volatile uint32_t *)it->addr)[k];
    case LOG_T_I16: return (float)((const volatile int16_t *)it->addr)[k];
    case LOG_T_U16: return (float)((const volatile uint16_t *)it->addr)[k];
    case LOG_T_I8:  return (float)((const volatile int8_t *)it->addr)[k];
    case LOG_T_U8:  return (float)((const volatile uint8_t *)it->addr)[k];
    default:        return 0.0f;
    }
}

/* 按表顺序把已开启项拼成一帧输出，通道顺序即上位机的曲线顺序 */
static void LogPrint(void)
{
    /* 声明必须在语句之前：Keil AC5 默认 C90，语句之后再声明会报 #268 */
    float ch[VOFA_CH_MAX];
    unsigned n = 0;
    unsigned i;
    unsigned k;
    const LogItem *it;

    for (i = 0; i < s_num; i++) {
        if (!LogIsOn(i))
            continue;
        it = &s_items[i];
        /* 开启时已保证总数不超；这里再挡一次，防运行时改表写越栈上的 ch[] */
        if (n + it->num > VOFA_CH_MAX)
            break;
        for (k = 0; k < it->num; k++)
            ch[n++] = LogReadF(it, k);
    }

    /* 整帧一次性写入，避免多次 VOFA_RTT_Send 被拆散导致上位机解析错位 */
    if (n > 0)
        VOFA_RTT_Send(ch, n);
}

void LogTask(void)
{
    LogCmdPoll();
    LogPrint();
}
