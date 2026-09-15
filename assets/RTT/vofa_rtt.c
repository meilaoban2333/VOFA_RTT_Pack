#include "vofa_rtt.h"
#include <math.h>//sinf/cosf/isfinite
#include "main.h"//HAL_GetTick

/* 单帧最大字节数：每通道最多 VOFA_NUM_MAX 字节，通道间 1 字节逗号，末尾 1 字节 '\n' */
#define VOFA_FRAME_MAX      (VOFA_CH_MAX * (VOFA_NUM_MAX + 1) + 1)

/* 10^VOFA_DECIMALS，用于把小数部分整体搬到整数域再逐位输出 */
static uint32_t VOFA_Pow10(unsigned n)
{
	uint32_t p = 1u;

	while (n--)
	{
		p *= 10u;
	}

	return p;
}

/**
 * @brief 把 float 转成定点十进制 ASCII，写入 buf
 * @param buf 输出缓冲，需至少 VOFA_NUM_MAX 字节，不追加结束符
 * @param v   待转换的值
 * @return    实际写入的字节数
 * @note  只做定点转换，不支持科学计数法：VOFA+ 波形观察的量程有限，
 *        定点足够且避免引入 C 库 sprintf（体积大且可能不可重入）。
 *        非有限值(NaN/Inf)统一输出 0，防止上位机解析出断点。
 */
static unsigned VOFA_FloatToStr(char *buf, float v)
{
	const uint32_t scale = VOFA_Pow10(VOFA_DECIMALS);
	unsigned len = 0;
	uint32_t ipart;
	uint32_t fpart;
	uint32_t scaled;
	char     rev[12];
	unsigned n = 0;

	/*NaN/Inf 无法定点表示，输出 0 保持帧结构完整*/
	if (!isfinite(v))
	{
		buf[len++] = '0';
		return len;
	}

	if (v < 0.0f)
	{
		buf[len++] = '-';
		v = -v;
	}

	/*四舍五入到目标小数位，再拆成整数/小数两段；
	  先做饱和再转 uint32，避免超量程时的未定义行为*/
	if (v > (float)(0xFFFFFFFFu / scale))
	{
		v = (float)(0xFFFFFFFFu / scale);
	}
	scaled = (uint32_t)(v * (float)scale + 0.5f);
	ipart  = scaled / scale;
	fpart  = scaled % scale;

	/*整数部分：先逆序取十进制位，再翻转写出*/
	do
	{
		rev[n++] = (char)('0' + (ipart % 10u));
		ipart /= 10u;
	} while (ipart);

	while (n--)
	{
		buf[len++] = rev[n];
	}

	/*小数部分：定长 VOFA_DECIMALS 位，高位补零（scale/10 起逐位下降）*/
	if (VOFA_DECIMALS > 0)
	{
		uint32_t div = scale / 10u;

		buf[len++] = '.';
		while (div)
		{
			buf[len++] = (char)('0' + (fpart / div) % 10u);
			div /= 10u;
		}
	}

	return len;
}

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

/**
 * @brief 推送一帧 FireWater 文本数据
 * @param data   指向 ch_num 个 float
 * @param ch_num 通道数，需 <= VOFA_CH_MAX
 * @note  帧格式 "v0,v1,...,vn\n"，通道间逗号分隔，'\n' 为帧结束符。
 *        整帧一次性写入，避免多次调用 RTT_Write 导致帧被其他输出割裂——
 *        FireWater 是文本协议，帧被割裂会直接让上位机解析错行。
 */
void VOFA_RTT_Send(const float *data, unsigned ch_num)
{
	char     frame[VOFA_FRAME_MAX];
	unsigned len = 0;
	unsigned i;

	if ((data == 0) || (ch_num == 0) || (ch_num > VOFA_CH_MAX))
	{
		return;
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

	SEGGER_RTT_Write(VOFA_RTT_BUF_IDX, frame, len);
}

/**
 * @brief 单通道推送
 */
void VOFA_RTT_Send1(float ch0)
{
	VOFA_RTT_Send(&ch0, 1);
}

/**
 * @brief 双通道推送
 */
void VOFA_RTT_Send2(float ch0, float ch1)
{
	float frame[2];

	frame[0] = ch0;
	frame[1] = ch1;

	VOFA_RTT_Send(frame, 2);
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
	uint32_t now = HAL_GetTick();

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
