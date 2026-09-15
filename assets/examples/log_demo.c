/*
***********************************************************************
* 文件名：log_demo.c
* 说  明：RTT 下行命令 + 按需打印示例，命令与数据都走 RTT 0 号通道
*
* 开关用位掩码保存：每个项目号占 1 bit，收到号就异或翻转，
* 所以多个项目天然可以叠加打印，同一个号发第二次即关闭。
*
* 输出为 VOFA+ FireWater 纯数据格式（逗号分隔 + '
'），不带任何标签文本，
* 整帧一次性写入 RTT，避免分次写入被拆行导致上位机解析错行。
*
* 本文件是模板：LogPrint() 里的取值代码要换成自己工程的变量，
* 其余命令解析、组帧逻辑可直接复用。
***********************************************************************
*/

#include "log_demo.h"
#include "vofa_rtt.h"

/* 行缓冲：RTT 下行缓冲仅 BUFFER_SIZE_DOWN(默认16) 字节，命令要跨多次读取拼接 */
#define LOG_LINE_MAX 32

static uint32_t s_mask = 0; // 已开启项，bit N 对应 LOG_ID_N

/*
 * 执行一条完成的命令行，line 已去掉 '
'
 * 只认 "AT+" 前缀，后面的数字用非数字字符（逗号、空格等）分隔
 */
static void LogCmdExec(const char *line, uint8_t len)
{
    uint8_t i;
    uint8_t id;
    uint8_t hasDigit;

    /* 前缀不符直接丢弃，避免误触发 */
    if (len < 4 || line[0] != 'A' || line[1] != 'T' || line[2] != '+')
        return;

    i = 3;
    while (i < len) {
        /* 跳过分隔符，逐段取一个十进制数 */
        if (line[i] < '0' || line[i] > '9') {
            i++;
            continue;
        }

        id = 0;
        hasDigit = 0;
        while (i < len && line[i] >= '0' && line[i] <= '9') {
            id = (uint8_t)(id * 10 + (line[i] - '0'));
            hasDigit = 1;
            i++;
        }

        if (!hasDigit)
            continue;

        /* 不回显开关状态：FireWater 是纯数据协议，混入文本会被当成数据行 */
        if (id == LOG_ID_OFF)
            s_mask = 0;
        else if (id < 32)
            s_mask ^= (1UL << id); // 同号再发一次即关闭
    }
}

/*
 * 收下行字节并按 '
' 组行
 * RTT 读取是非阻塞的，没数据就立即返回 0
 */
static void LogCmdPoll(void)
{
    static char s_line[LOG_LINE_MAX];
    static uint8_t s_len = 0;

    char ch;

    while (SEGGER_RTT_Read(VOFA_RTT_BUF_IDX, &ch, 1) > 0) {
        if (ch == '
' || ch == '') {
            if (s_len > 0) {
                LogCmdExec(s_line, s_len);
                s_len = 0;
            }
        } else if (s_len < LOG_LINE_MAX) {
            s_line[s_len++] = ch;
        } else {
            s_len = 0; // 超长行判为无效，丢弃重新同步
        }
    }
}

/* 打印已开启项，同一周期的多项拼成一行输出 */
static void LogPrint(void)
{
    if (s_mask == 0)
        return;

    float ch[LOG_CH_MAX];
    unsigned n = 0;

    /* ------------------------------------------------------------------
     * 以下为示例取值，移植时替换成自己工程的变量。
     * 关键约束：按项目号从小到大依次填通道，通道顺序即上位机的曲线顺序，
     * 顺序乱了曲线对不上号。
     * ------------------------------------------------------------------ */
    if (s_mask & (1UL << LOG_ID_ANGLE))
        ch[n++] = 0.0f; // 换成实际角度变量

    if (s_mask & (1UL << LOG_ID_CUR)) {
        ch[n++] = 0.0f; // 换成实际 U 相电流
        ch[n++] = 0.0f; // V 相
        ch[n++] = 0.0f; // W 相
    }

    if (s_mask & (1UL << LOG_ID_VBUS))
        ch[n++] = 0.0f; // 换成实际母线电压

    if (s_mask & (1UL << LOG_ID_SPEED)) {
        ch[n++] = 0.0f; // 机械转速
        ch[n++] = 0.0f; // 电转速
    }

    /* 整帧一次性写入，避免多次 RTT_Write 被拆行导致上位机解析错行 */
    VOFA_RTT_Send(ch, n);
}

void LogTask(void)
{
    LogCmdPoll();
    LogPrint();
}
