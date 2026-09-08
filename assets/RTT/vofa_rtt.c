#include "vofa_rtt.h"
#include <math.h>
#include <string.h>//memcpy
#include "main.h"//HAL_GetTick

/* 单帧最大字节数：每通道 4 字节小端 float，加 4 字节帧尾 */
#define VOFA_FRAME_MAX      (VOFA_CH_MAX * 4 + 4)

/* JustFloat 帧尾：小端 0x7F800000(+inf)，VOFA+ 以此切分帧边界 */
static const uint8_t VOFA_FrameTail[4] = {0x00, 0x00, 0x80, 0x7F};

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
 * @brief 推送一帧 JustFloat 二进制数据
 * @param data   指向 ch_num 个 float
 * @param ch_num 通道数，需 <= VOFA_CH_MAX
 * @note  整帧一次性写入，避免多次调用 RTT_Write 导致帧被其他输出割裂。
 *        本芯片是小端序，float 内存布局与 JustFloat 要求一致，可直接 memcpy。
 */
void VOFA_RTT_Send(const float *data, unsigned ch_num)
{
	uint8_t  frame[VOFA_FRAME_MAX];
	unsigned len = 0;

	if ((data == 0) || (ch_num == 0) || (ch_num > VOFA_CH_MAX))
	{
		return;
	}

	/*data 由调用方保证是 float 数组，此处按字节整体拷贝，不做任何数值转换*/
	memcpy(&frame[len], data, ch_num * 4u);
	len += ch_num * 4u;

	memcpy(&frame[len], VOFA_FrameTail, sizeof(VOFA_FrameTail));
	len += sizeof(VOFA_FrameTail);

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
