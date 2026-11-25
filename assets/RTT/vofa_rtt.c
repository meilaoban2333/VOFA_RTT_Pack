#include "vofa_rtt.h"
#include <math.h>//sinf/cosf，TestLoop 用
#include <string.h>//memcpy，取 float 位模式
#include VOFA_TICK_HEADER//VOFA_GET_TICK_MS() 的声明，TestLoop 用，见 vofa_rtt.h

#if (VOFA_PROTOCOL == VOFA_PROTO_FIREWATER)

/* 单帧最大字节数：每通道最多 VOFA_NUM_MAX 字节，通道间 1 字节逗号，末尾 1 字节 '\n' */
#define VOFA_FRAME_MAX      (VOFA_CH_MAX * (VOFA_NUM_MAX + 1) + 1)

/* 10^n 的 float 值，用于把小数部分搬到整数域。查表而不是运行时连乘，
   省掉循环和一次整数转 float（M0/M0+ 上都是软件实现）。1e9f 可被 float 精确表示 */
static const float VOFA_Scale[10] = {1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f};

/**
 * @brief 把 float 转成定点十进制 ASCII，写入 buf
 * @param buf 输出缓冲，需至少 VOFA_NUM_MAX 字节，不追加结束符
 * @param v   待转换的值
 * @return    实际写入的字节数
 * @note  只做定点转换，不支持科学计数法：VOFA+ 波形观察的量程有限，
 *        定点足够且避免引入 C 库 sprintf（体积大且可能不可重入）。
 *        非有限值(NaN/Inf)统一输出 0，防止上位机解析出断点。
 *        M0/M0+ 无 FPU 也无硬件除法，浮点比较和整数除法都是库函数调用，
 *        所以符号、NaN/Inf、越界都按位模式判断，每个十进制位只做一次除法。
 */
static unsigned VOFA_FloatToStr(char *buf, float v)
{
	char     rev[VOFA_NUM_MAX];
	unsigned len = 0;
	unsigned n   = 0;
	uint32_t bits;
	uint32_t scaled;
	uint32_t q;
	float    scaled_f;
#if (VOFA_DECIMALS > 0)
	unsigned k;
#endif

	/*经 memcpy 取位模式，避免指针强转违反严格别名规则*/
	memcpy(&bits, &v, 4u);

	/*指数位全 1 即 NaN/Inf，无法定点表示，输出 0 保持帧结构完整*/
	if ((bits & 0x7F800000u) == 0x7F800000u)
	{
		buf[len++] = '0';
		return len;
	}

	/*符号位为 1 且不是 -0.0 才输出负号，与 v < 0.0f 等价*/
	if ((bits & 0x80000000u) && (bits & 0x7FFFFFFFu))
	{
		buf[len++] = '-';
	}
	bits &= 0x7FFFFFFFu;/*清符号位取绝对值*/
	memcpy(&v, &bits, 4u);

	/*四舍五入到目标小数位，整体当一个整数处理。
	  越过 2^32 的 float 转 uint32 是未定义行为，必须先判断。非负 float 的位模式
	  与数值同序，位模式 >= 0x4F800000 即 >= 2^32，免去一次软件浮点比较*/
	scaled_f = v * VOFA_Scale[VOFA_DECIMALS] + 0.5f;
	memcpy(&bits, &scaled_f, 4u);
	scaled = (bits >= 0x4F800000u) ? 0xFFFFFFFFu : (uint32_t)scaled_f;

	/*从最低位起逆序取数：先定长 VOFA_DECIMALS 位小数（不足补零），再整数部分。
	  余数用"原值 - 商*10"求，每位只调一次除法：分开写 /10 和 %10 时，
	  实测 GCC 14 在 M0+ 上会各调一次库函数，并不合并*/
#if (VOFA_DECIMALS > 0)
	for (k = 0; k < VOFA_DECIMALS; k++)
	{
		q = scaled / 10u;
		rev[n++] = (char)('0' + (scaled - q * 10u));
		scaled = q;
	}
	rev[n++] = '.';
#endif
	do
	{
		q = scaled / 10u;
		rev[n++] = (char)('0' + (scaled - q * 10u));
		scaled = q;
	} while (scaled);

	while (n--)
	{
		buf[len++] = rev[n];
	}

	return len;
}

#else  /* VOFA_PROTO_JUSTFLOAT */

/* 单帧最大字节数：每通道 4 字节小端 float，加 4 字节帧尾 */
#define VOFA_FRAME_MAX      (VOFA_CH_MAX * 4 + 4)

/* JustFloat 帧尾：小端 0x7F800000(+inf)，VOFA+ 以此切分帧边界 */
static const uint8_t VOFA_FrameTail[4] = {0x00, 0x00, 0x80, 0x7F};

#endif /* VOFA_PROTOCOL */

/**
 * @brief VOFA+ RTT 初始化
 * @note  0 号通道由 SEGGER_RTT_Init() 自动预置为 "Terminal"，无需 ConfigUpBuffer。
 *        必须在 main() 起始阶段调用，保证 J-Link 连接时控制块已就绪。
 */
void VOFA_RTT_Init(void)
{
	SEGGER_RTT_Init();

	/*0 号通道缓冲满时丢帧，绝不阻塞主循环（默认即为此模式，此处显式声明意图）*/
	SEGGER_RTT_SetFlagsUpBuffer(VOFA_RTT_BUF_IDX, SEGGER_RTT_MODE_NO_BLOCK_SKIP);
}

#if (VOFA_PROTOCOL == VOFA_PROTO_FIREWATER)

/**
 * @brief 推送一帧 FireWater 文本数据
 * @param data   指向 ch_num 个 float
 * @param ch_num 通道数，需 <= VOFA_CH_MAX
 * @return 写入字节数，0 表示参数无效或缓冲空间不足整帧丢弃
 * @note  帧格式 "v0,v1,...,vn\n"，通道间逗号分隔，'\n' 为帧结束符。
 *        整帧一次性写入，避免多次调用 RTT_Write 导致帧被其他输出割裂——
 *        FireWater 是文本协议，帧被割裂会直接让上位机解析错行。
 */
unsigned VOFA_RTT_Send(const float *data, unsigned ch_num)
{
	char     frame[VOFA_FRAME_MAX];
	unsigned len = 0;
	unsigned i;

	if ((data == 0) || (ch_num == 0) || (ch_num > VOFA_CH_MAX))
	{
		return 0;
	}

	for (i = 0; i < ch_num; i++)
	{
		if (i)
		{
			frame[len++] = ',';
		}
		len += VOFA_FloatToStr(&frame[len], data[i]);
	}

	frame[len++] = '\n';

	return SEGGER_RTT_Write(VOFA_RTT_BUF_IDX, frame, len);
}

#else  /* VOFA_PROTO_JUSTFLOAT */

/**
 * @brief 推送一帧 JustFloat 二进制数据
 * @param data   指向 ch_num 个 float
 * @param ch_num 通道数，需 <= VOFA_CH_MAX
 * @return 写入字节数，0 表示参数无效或缓冲空间不足整帧丢弃
 * @note  帧格式 [f0:4B][f1:4B]...[00 00 80 7F]，帧尾是 VOFA+ 的切分依据。
 *        整帧一次性写入，避免多次调用 RTT_Write 导致帧被割裂——二进制流被
 *        割裂比文本更严重，会让之后所有通道错位。
 *        本平台是小端序，float 内存布局与 JustFloat 要求一致，可按字节直接拷贝，
 *        不做格式化，这正是 JustFloat 相比 FireWater 省开销的地方。
 *        唯一的处理是 Inf/NaN 置 0：+Inf 的字节 00 00 80 7F 与帧尾完全相同，
 *        原样发出会让 VOFA+ 在帧中间提前切帧；NaN/-Inf 一并置 0，与 FireWater 行为一致。
 */
unsigned VOFA_RTT_Send(const float *data, unsigned ch_num)
{
	uint8_t  frame[VOFA_FRAME_MAX];
	unsigned len = 0;
	unsigned i;
	uint32_t bits;

	if ((data == 0) || (ch_num == 0) || (ch_num > VOFA_CH_MAX))
	{
		return 0;
	}

	for (i = 0; i < ch_num; i++)
	{
		/*经 memcpy 取位模式，避免指针强转违反严格别名规则*/
		memcpy(&bits, &data[i], 4u);
		/*指数位全 1 即 Inf/NaN，纯整数判断，不依赖 math.h*/
		if ((bits & 0x7F800000u) == 0x7F800000u)
		{
			bits = 0u;
		}
		memcpy(&frame[len], &bits, 4u);
		len += 4u;
	}

	memcpy(&frame[len], VOFA_FrameTail, sizeof(VOFA_FrameTail));
	len += sizeof(VOFA_FrameTail);

	return SEGGER_RTT_Write(VOFA_RTT_BUF_IDX, frame, len);
}

#endif /* VOFA_PROTOCOL */

/**
 * @brief 单通道推送，返回值同 VOFA_RTT_Send
 */
unsigned VOFA_RTT_Send1(float ch0)
{
	return VOFA_RTT_Send(&ch0, 1);
}

/**
 * @brief 双通道推送，返回值同 VOFA_RTT_Send
 */
unsigned VOFA_RTT_Send2(float ch0, float ch1)
{
	float frame[2];

	frame[0] = ch0;
	frame[1] = ch1;

	return VOFA_RTT_Send(frame, 2);
}

/**
 * @brief 原样发送一段字节，不加任何帧格式
 * @return 实际写入字节数，0 表示参数无效或缓冲空间不足被整块丢弃
 * @note  0 号通道是 NO_BLOCK_SKIP 模式，空间不够时 SEGGER_RTT_Write 整块丢弃，
 *        所以只调一次 Write，内容要么完整进缓冲、要么完全不进，不会留下半截
 */
unsigned VOFA_RTT_SendRaw(const void *buf, unsigned len)
{
	if ((buf == 0) || (len == 0))
	{
		return 0;
	}

	return SEGGER_RTT_Write(VOFA_RTT_BUF_IDX, buf, len);
}

/**
 * @brief 发送 '\0' 结尾的字符串，仅 FireWater 下生效
 * @note  JustFloat 是纯二进制流，混入文本会破坏帧对齐，与 LOG() 同理禁用。
 *        保留真函数而不是宏，调用方使用返回值的代码在两种协议下都能编译
 */
unsigned VOFA_RTT_SendStr(const char *s)
{
#if (VOFA_PROTOCOL == VOFA_PROTO_FIREWATER)
	if (s == 0)
	{
		return 0;
	}

	/*WriteString 内部 strlen 后只调一次 Write，整串一次写入*/
	return SEGGER_RTT_WriteString(VOFA_RTT_BUF_IDX, s);
#else
	(void)s;
	return 0;
#endif
}

/**
 * @brief 主循环调用的测试入口，内部做时间节流后发送正弦波
 * @note  主循环是裸跑的，不节流会以循环速度灌满 RTT 缓冲导致丢帧；
 *        按 VOFA_TEST_PERIOD_MS 定时发送才能得到确定的采样率。
 *        相位按固定步长累加，正确性依赖等间隔调用。
 */
void VOFA_RTT_TestLoop(void)
{
	static uint32_t s_last_tick = 0;
	static float    s_phase     = 0.0f;/*当前相位(rad)*/
	/*每次发送的相位增量 = 2π × 频率 × 采样周期*/
	const float phase_step = 6.283185307f * VOFA_SINE_FREQ_HZ * (VOFA_TEST_PERIOD_MS / 1000.0f);
	uint32_t now = VOFA_GET_TICK_MS();

	/*相减比较，天然兼容 tick 溢出回绕*/
	if ((now - s_last_tick) < VOFA_TEST_PERIOD_MS)
	{
		return;
	}
	s_last_tick = now;

	/*CH0 正弦，CH1 余弦(相位差90°)，便于确认多通道解析是否正确*/
	VOFA_RTT_Send2(VOFA_SINE_AMP * sinf(s_phase),
	               VOFA_SINE_AMP * cosf(s_phase));

	/*相位取模防止长时间运行后浮点精度损失*/
	s_phase += phase_step;
	if (s_phase >= 6.283185307f)
	{
		s_phase -= 6.283185307f;
	}
}
