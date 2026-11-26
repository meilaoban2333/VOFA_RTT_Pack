/*
 * 主机端回归测试：在 PC 上编译真实的 SEGGER_RTT.c / vofa_rtt.c / log_demo.c，
 * 直接读 RTT 0 号上行缓冲比对输出字节，不需要板子和调试器。
 * 由 run_tests.sh 按不同宏配置（协议、小数位）各编一次，每种配置只跑对应的用例。
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "vofa_rtt.h"
#include "log_demo.h"

/* VOFA_GET_TICK_MS() 默认是 HAL_GetTick()，声明在 stub/main.h */
uint32_t HAL_GetTick(void) { return 0; }
/* MinGW 定义了 WIN32，SEGGER_RTT_Conf.h 会走 embOS 模拟器的加锁函数，给空实现 */
void OS_SIM_EnterCriticalSection(void) {}
void OS_SIM_LeaveCriticalSection(void) {}

/* 经 -Wl,--wrap 统计 SEGGER_RTT_Write 调用次数，用来判断 LOG 是否一次写完 */
static unsigned g_writes;
unsigned __real_SEGGER_RTT_Write(unsigned i, const void *p, unsigned n);
unsigned __wrap_SEGGER_RTT_Write(unsigned i, const void *p, unsigned n)
{
    g_writes++;
    return __real_SEGGER_RTT_Write(i, p, n);
}

/* log_demo 测试夹具。用非 static 全局：部分配置用不到，static 会报 unused 警告让测试失败 */
float t_a, t_b3[3], t_c, t_d2[2];
const LogItem t_items[] = { LOG_F32(t_a, 1), LOG_F32(t_b3, 3), LOG_F32(t_c, 1), LOG_F32(t_d2, 2) };
int32_t t_i32; uint32_t t_u32; int16_t t_i16; uint16_t t_u16; int8_t t_i8; uint8_t t_u8;
const LogItem t_types[] = { LOG_I32(t_i32, 1), LOG_U32(t_u32, 1), LOG_I16(t_i16, 1),
                            LOG_U16(t_u16, 1), LOG_I8(t_i8, 1),   LOG_U8(t_u8, 1) };
float t_e3[3], t_f3[3], t_g3[3];
const LogItem t_three[] = { LOG_F32(t_e3, 3), LOG_F32(t_f3, 3), LOG_F32(t_g3, 3) };
LogItem t_big[70];   /* 运行时填，测超过 31 项和 LOG_ITEM_MAX 截断 */
float t_big9[9];
LogItem t_bad[3];    /* 运行时手填的坏表项，绕过 LOG_xxx 宏的编译期检查 */
#define T_NUM(a) (sizeof(a) / sizeof((a)[0]))

/* 命令回调：记下最后一行和调用次数 */
char t_hookLine[LOG_LINE_MAX + 1];
unsigned t_hookLen, t_hookCalls;
void t_hook(const char *line, unsigned len)
{
    strcpy(t_hookLine, line);
    t_hookLen = len;
    t_hookCalls++;
}

static int g_fail, g_pass;

/* 模拟调试器把 0 号上行缓冲读空，返回读到的字节数 */
static unsigned drain(unsigned char *out)
{
    SEGGER_RTT_BUFFER_UP *b = &_SEGGER_RTT.aUp[0];
    unsigned n = 0;
    while (b->RdOff != b->WrOff) {
        out[n++] = (unsigned char)b->pBuffer[b->RdOff];
        b->RdOff = (b->RdOff + 1u == b->SizeOfBuffer) ? 0u : b->RdOff + 1u;
    }
    out[n] = 0;
    return n;
}

static void expect_bytes(const char *name, const void *exp, unsigned exp_len)
{
    static unsigned char got[BUFFER_SIZE_UP + 1];
    unsigned n = drain(got);
    if (n == exp_len && memcmp(got, exp, n) == 0) {
        g_pass++;
        printf("  PASS %s\n", name);
    } else {
        unsigned i;
        g_fail++;
        printf("  FAIL %s\n    got(%u):", name, n);
        for (i = 0; i < n && i < 48; i++) printf(" %02X", got[i]);
        printf("\n    exp(%u):", exp_len);
        for (i = 0; i < exp_len && i < 48; i++) printf(" %02X", ((const unsigned char *)exp)[i]);
        printf("\n");
    }
}
#define EXPECT_STR(name, s) expect_bytes(name, s, (unsigned)strlen(s))

static void expect_uint(const char *name, unsigned got, unsigned exp)
{
    if (got == exp) { g_pass++; printf("  PASS %s\n", name); }
    else { g_fail++; printf("  FAIL %s: got %u, exp %u\n", name, got, exp); }
}
/* 检查回调收到的行内容和长度 */
#define EXPECT_LINE(name, s) \
    expect_uint(name, (strcmp(t_hookLine, s) == 0 && t_hookLen == strlen(s)) ? 1u : 0u, 1u)

/* 模拟上位机发下行命令。下行缓冲只有 BUFFER_SIZE_DOWN 字节，分小段写入，
   每段后调一次 LogTask() 让固件读走，与实际跨多次轮询拼行一致。
   喂完把这期间打印的帧丢掉，调用方再单独调一次 LogTask() 检查开关状态 */
static void feed(const char *s)
{
    unsigned char sink[BUFFER_SIZE_UP + 1];
    unsigned n = (unsigned)strlen(s);
    while (n) {
        unsigned k = (n > 8u) ? 8u : n;
        SEGGER_RTT_WriteDownBuffer(0, s, k);
        LogTask();
        s += k;
        n -= k;
    }
    drain(sink);
}

/* 测缓冲满：先占到只剩 3 字节，再发一帧应整帧丢弃并返回 0 */
static void test_full_buffer(void)
{
    static char fill[BUFFER_SIZE_UP];
    const unsigned keep = (BUFFER_SIZE_UP - 1u) - 3u;   /* 环形缓冲最多存 SIZE-1 字节 */

    memset(fill, 'f', sizeof(fill));
    expect_uint("fill buffer", VOFA_RTT_SendRaw(fill, keep), keep);
    expect_uint("Send1 dropped when full -> 0", VOFA_RTT_Send1(1.0f), 0);
    expect_bytes("full: only fill bytes in buffer", fill, keep);
}

int main(void)
{
    VOFA_RTT_Init();

    /* 初始化顺序写反：LogInit 之前调 LogTask 不能有输出，下行里已有的命令被安全吃掉 */
    SEGGER_RTT_WriteDownBuffer(0, "AT+1\n", 5);
    LogTask(); EXPECT_STR("LogTask before LogInit", "");
    LogInit(t_items, T_NUM(t_items));

#if (VOFA_PROTOCOL == VOFA_PROTO_FIREWATER) && (VOFA_DECIMALS == 3)
    printf("[FireWater, VOFA_DECIMALS=3]\n");
    VOFA_RTT_Send2(1.571f, -0.05f);   EXPECT_STR("Send2 basic", "1.571,-0.050\n");
    VOFA_RTT_Send1(0.0f);             EXPECT_STR("Send1 zero", "0.000\n");
    VOFA_RTT_Send1(-0.0f);            EXPECT_STR("Send1 -0.0 has no sign", "0.000\n");
    VOFA_RTT_Send1(-0.0001f);         EXPECT_STR("Send1 tiny negative", "-0.000\n");
    VOFA_RTT_Send1(0.05f);            EXPECT_STR("Send1 leading zero in fraction", "0.050\n");
    VOFA_RTT_Send1(123.4567f);        EXPECT_STR("Send1 rounding", "123.457\n");
    VOFA_RTT_Send1(99.9996f);         EXPECT_STR("Send1 rounding carry", "100.000\n");
    VOFA_RTT_Send1(1000000.0f);       EXPECT_STR("Send1 1e6", "1000000.000\n");
    VOFA_RTT_Send1(NAN);              EXPECT_STR("Send1 NaN -> 0", "0\n");
    VOFA_RTT_Send1(INFINITY);         EXPECT_STR("Send1 Inf -> 0", "0\n");
    VOFA_RTT_Send1(-INFINITY);        EXPECT_STR("Send1 -Inf -> 0", "0\n");
    /* 4294967*1000 舍入成 float 是 4294967040，仍小于 2^32，不应饱和 */
    VOFA_RTT_Send1(4294967.0f);       EXPECT_STR("Send1 just below 2^32", "4294967.040\n");
    VOFA_RTT_Send1(1e30f);            EXPECT_STR("Send1 huge saturates", "4294967.295\n");
    VOFA_RTT_Send1(-1e30f);           EXPECT_STR("Send1 -huge saturates", "-4294967.295\n");

    /* 通道数满 VOFA_CH_MAX 且每个数都是最长写法，验证组帧缓冲够大 */
    {
        float v[VOFA_CH_MAX];
        char exp[VOFA_CH_MAX * 13 + 1];
        unsigned i;
        exp[0] = 0;
        for (i = 0; i < VOFA_CH_MAX; i++) {
            v[i] = -1e30f;
            strcat(exp, i ? ",-4294967.295" : "-4294967.295");
        }
        strcat(exp, "\n");
        expect_uint("Send max channels ret", VOFA_RTT_Send(v, VOFA_CH_MAX), (unsigned)strlen(exp));
        EXPECT_STR("Send max channels bytes", exp);
        expect_uint("Send ch_num > VOFA_CH_MAX -> 0", VOFA_RTT_Send(v, VOFA_CH_MAX + 1), 0);
        expect_uint("Send ch_num 0 -> 0", VOFA_RTT_Send(v, 0), 0);
        expect_uint("Send NULL -> 0", VOFA_RTT_Send(0, 1), 0);
        EXPECT_STR("invalid Send writes nothing", "");
    }

    expect_uint("Send2 ret", VOFA_RTT_Send2(1.571f, -0.05f), 13);
    EXPECT_STR("Send2 ret bytes", "1.571,-0.050\n");

    expect_uint("SendStr ret", VOFA_RTT_SendStr("hello\n"), 6);
    EXPECT_STR("SendStr bytes", "hello\n");
    expect_uint("SendRaw ret", VOFA_RTT_SendRaw("\x01\x00\x02", 3), 3);
    expect_bytes("SendRaw bytes", "\x01\x00\x02", 3);
    expect_uint("SendRaw NULL", VOFA_RTT_SendRaw(0, 5), 0);
    expect_uint("SendStr NULL", VOFA_RTT_SendStr(0), 0);
    EXPECT_STR("NULL writes nothing", "");
    /* 超过缓冲容量：NO_BLOCK_SKIP 整块丢弃 */
    {
        static char big[BUFFER_SIZE_UP + 10];
        memset(big, 'x', sizeof(big));
        expect_uint("SendRaw too big dropped", VOFA_RTT_SendRaw(big, sizeof(big)), 0);
        EXPECT_STR("too big writes nothing", "");
    }
    test_full_buffer();

    /* 100 字节日志行：SEGGER_RTT_PRINTF_BUFFER_SIZE >= 128 时应只调用一次 SEGGER_RTT_Write */
    {
        char line[101];
        memset(line, 'a', 99); line[99] = '\n'; line[100] = 0;
        g_writes = 0;
        LOG("%s", line);
        expect_uint("LOG 100B single write", g_writes, 1);
        EXPECT_STR("LOG 100B bytes", line);
    }

#ifndef VOFA_SEND
#error "VOFA_SEND should be available under C99"
#endif
    {
        int k = 0;
        expect_uint("VOFA_SEND ret", VOFA_SEND(1.0f, 2, -3.5), 19);
        EXPECT_STR("VOFA_SEND mixed types", "1.000,2.000,-3.500\n");
        VOFA_SEND((float)(k++));
        expect_uint("VOFA_SEND evaluates args once", (unsigned)k, 1);
        EXPECT_STR("VOFA_SEND side effect bytes", "0.000\n");
    }

    /* log_demo 下行命令：t_items 的变量都是 0，按通道数判断开了哪些项（1/3/1/2 通道） */
    feed("AT+1\n");          LogTask(); EXPECT_STR("AT+1 -> 1 ch", "0.000\n");
    feed("AT+2,4\n");        LogTask(); EXPECT_STR("AT+2,4 -> 1+3+2 ch",
                                                   "0.000,0.000,0.000,0.000,0.000,0.000\n");
    feed("AT+0\n");          LogTask(); EXPECT_STR("AT+0 -> all off", "");
    /* 超出 0..31 的号不能回绕成小号误触发（旧代码 257 截成 uint8 后是 1） */
    feed("AT+257\n");        LogTask(); EXPECT_STR("AT+257 ignored", "");
    /* 每组用例前先 AT+0 复位，前一条失败时不连带影响后面的判断 */
    feed("AT+0\n");
    feed("AT+0001\n");       LogTask(); EXPECT_STR("AT+0001 -> id 1", "0.000\n");
    feed("AT+1\r\n");        LogTask(); EXPECT_STR("AT+1 again toggles off (CRLF)", "");
    /* 超长行整行作废，行尾残留的 "AT+1" 不能被当成命令执行 */
    feed("AT+0\n");
    feed("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxAT+1\n");
    LogTask(); EXPECT_STR("overlong line tail not executed", "");
    feed("AT+1\n");          LogTask(); EXPECT_STR("resync after overlong line", "0.000\n");
    feed("AT+0\n");          LogTask(); EXPECT_STR("cleanup", "");

    /* 多类型：整数统一转 float 输出 */
    LogInit(t_types, T_NUM(t_types));
    t_i32 = -123456; t_u32 = 3000000u; t_i16 = -300; t_u16 = 65535u; t_i8 = -128; t_u8 = 255u;
    feed("AT+1,2,3,4,5,6\n"); LogTask();
    EXPECT_STR("types -> float", "-123456.000,3000000.000,-300.000,65535.000,-128.000,255.000\n");
    feed("AT+0\n");

    /* 超过 31 项，以及超过 LOG_ITEM_MAX 时只登记前 LOG_ITEM_MAX 项 */
    {
        unsigned i;
        for (i = 0; i < T_NUM(t_big); i++) {
            t_big[i].addr = &t_a;
            t_big[i].type = LOG_T_F32;
            t_big[i].num = 1;
        }
    }
    LogInit(t_big, 40);
    feed("AT+40\n");         LogTask(); EXPECT_STR("id 40 works", "0.000\n");
    feed("AT+41\n");         LogTask(); EXPECT_STR("id beyond num ignored", "0.000\n");
    LogInit(t_big, 70);
    feed("AT+64\n");         LogTask(); EXPECT_STR("id LOG_ITEM_MAX works", "0.000\n");
    feed("AT+65\n");         LogTask(); EXPECT_STR("id beyond LOG_ITEM_MAX ignored", "0.000\n");
    feed("AT+0\n");

    /* 打开后会超 VOFA_CH_MAX 的项拒绝开启；通道顺序 = 表顺序 */
    {
        unsigned i;
        for (i = 0; i < 3; i++) { t_e3[i] = 1.0f; t_f3[i] = 2.0f; t_g3[i] = 3.0f; }
    }
    LogInit(t_three, T_NUM(t_three));
    feed("AT+1,2\n");        LogTask(); EXPECT_STR("6 ch on", "1.000,1.000,1.000,2.000,2.000,2.000\n");
    feed("AT+3\n");          LogTask(); EXPECT_STR("over VOFA_CH_MAX refused",
                                                   "1.000,1.000,1.000,2.000,2.000,2.000\n");
    feed("AT+1\n");          LogTask(); EXPECT_STR("item 1 off", "2.000,2.000,2.000\n");
    feed("AT+3\n");          LogTask(); EXPECT_STR("now fits, table order",
                                                   "2.000,2.000,2.000,3.000,3.000,3.000\n");

    /* 重新 LogInit 清空开关；空指针当 0 项 */
    LogInit(t_items, T_NUM(t_items)); LogTask(); EXPECT_STR("LogInit resets", "");
    LogInit(0, 5); feed("AT+1\n"); LogTask(); EXPECT_STR("LogInit NULL", "");

    /* 运行时手填的坏表项：通道数 0、超 VOFA_CH_MAX 永远开不了，未知类型输出 0 */
    t_bad[0].addr = &t_a;   t_bad[0].type = LOG_T_F32; t_bad[0].num = 0;
    t_bad[1].addr = t_big9; t_bad[1].type = LOG_T_F32; t_bad[1].num = 9;
    t_bad[2].addr = &t_a;   t_bad[2].type = 99;        t_bad[2].num = 1;
    t_a = 1.5f;
    LogInit(t_bad, T_NUM(t_bad));
    feed("AT+1\n");          LogTask(); EXPECT_STR("runtime item num 0 never on", "");
    feed("AT+2\n");          LogTask(); EXPECT_STR("runtime item num > VOFA_CH_MAX never on", "");
    feed("AT+3\n");          LogTask(); EXPECT_STR("runtime item unknown type -> 0", "0.000\n");
    t_a = 0.0f;

    /* 一次读取里含两条命令，两条都要执行 */
    LogInit(t_items, T_NUM(t_items));
    SEGGER_RTT_WriteDownBuffer(0, "AT+1\nAT+2\n", 10);
    LogTask(); EXPECT_STR("two commands in one chunk", "0.000,0.000,0.000,0.000\n");
    feed("AT+0\n");

    /* 非开关命令交给回调 */
    feed("AT+KP=1.5\n");     /* 未设回调：丢弃不崩 */
    LogSetCmdHook(t_hook);
    feed("AT+KP=1.5\n");
    expect_uint("hook called", t_hookCalls, 1);
    EXPECT_LINE("hook line", "AT+KP=1.5");
    feed("AT+1x2\n");        LogTask(); EXPECT_STR("AT+1x2 goes to hook, toggles nothing", "");
    expect_uint("hook AT+1x2", t_hookCalls, 2);
    feed("AT+\n");           expect_uint("AT+ without digit -> hook", t_hookCalls, 3);
    feed("AT+ 1\n");         LogTask(); EXPECT_STR("space separator still toggles", "0.000\n");
    expect_uint("toggle line not hooked", t_hookCalls, 3);
    feed("AT+0\n");
    feed("\r\n");            expect_uint("CRLF empty line not hooked", t_hookCalls, 3);
    feed("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");   /* 33 字符 */
    expect_uint("overlong line not hooked", t_hookCalls, 3);
    feed("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");    /* 恰好 32 字符 */
    expect_uint("hook line exactly LOG_LINE_MAX", t_hookLen, 32);
    LogSetCmdHook(0);
    LogInit(t_items, T_NUM(t_items));
#endif

#if (VOFA_PROTOCOL == VOFA_PROTO_FIREWATER) && (VOFA_DECIMALS == 0)
    printf("[FireWater, VOFA_DECIMALS=0]\n");
    VOFA_RTT_Send2(12.4f, -12.6f);    EXPECT_STR("Send2 int", "12,-13\n");
    VOFA_RTT_Send1(0.0f);             EXPECT_STR("Send1 zero", "0\n");
    VOFA_RTT_Send1(5e9f);             EXPECT_STR("Send1 5e9 saturates", "4294967295\n");
    VOFA_RTT_Send1(4294967040.0f);    EXPECT_STR("Send1 max float < 2^32", "4294967040\n");
    expect_uint("Send1 ret", VOFA_RTT_Send1(7.0f), 2);
    EXPECT_STR("Send1 ret bytes", "7\n");
#endif

#if (VOFA_PROTOCOL == VOFA_PROTO_JUSTFLOAT)
    printf("[JustFloat]\n");
    {
        static const unsigned char exp[] = {
            0x00, 0x00, 0x80, 0x3F,   /* 1.0f */
            0x00, 0x00, 0x00, 0x00,   /* +Inf -> 0，不能与帧尾相同 */
            0x00, 0x00, 0x80, 0x7F    /* 帧尾 */
        };
        expect_uint("Send2 ret", VOFA_RTT_Send2(1.0f, INFINITY), sizeof(exp));
        expect_bytes("Send2 +Inf -> 0", exp, sizeof(exp));
    }
    {
        static const unsigned char exp[] = {
            0x00, 0x00, 0x00, 0x00,   /* NaN -> 0 */
            0x00, 0x00, 0x00, 0x00,   /* -Inf -> 0 */
            0x00, 0x00, 0x80, 0x7F
        };
        VOFA_RTT_Send2(NAN, -INFINITY);
        expect_bytes("Send2 NaN/-Inf -> 0", exp, sizeof(exp));
    }
    {
        static const unsigned char exp[] = {
            0x00, 0x00, 0x20, 0xC1,   /* -10.0f */
            0x00, 0x00, 0x80, 0x7F
        };
        VOFA_RTT_Send1(-10.0f);
        expect_bytes("Send1 normal", exp, sizeof(exp));
        VOFA_SEND(-10);
        expect_bytes("VOFA_SEND int -> float", exp, sizeof(exp));
    }
    {
        static const float v[VOFA_CH_MAX + 1];
        expect_uint("Send ch_num > VOFA_CH_MAX -> 0", VOFA_RTT_Send(v, VOFA_CH_MAX + 1), 0);
    }
    expect_uint("SendStr disabled ret", VOFA_RTT_SendStr("hello\n"), 0);
    EXPECT_STR("SendStr disabled writes nothing", "");
    expect_uint("SendRaw ret", VOFA_RTT_SendRaw("\xAA\x55", 2), 2);
    expect_bytes("SendRaw bytes", "\xAA\x55", 2);
    test_full_buffer();
    {
        /* 下行命令与上行协议无关，JustFloat 下 AT 命令照常可用 */
        static const unsigned char exp[] = { 0, 0, 0, 0, 0x00, 0x00, 0x80, 0x7F };
        feed("AT+1\n"); LogTask(); expect_bytes("AT+1 under JustFloat", exp, sizeof(exp));
        feed("AT+0\n"); LogTask(); EXPECT_STR("AT+0 under JustFloat", "");
    }
#endif

    printf("  => %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
