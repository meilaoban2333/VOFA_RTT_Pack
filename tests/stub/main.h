/* 主机测试用的桩：vofa_rtt.c 经 VOFA_TICK_HEADER 包含 main.h 取 HAL_GetTick() 声明 */
#include <stdint.h>
uint32_t HAL_GetTick(void);
