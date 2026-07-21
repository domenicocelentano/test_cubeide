/*
 * tim1_drv.c
 *
 *  Created on: Jul 20, 2026
 *      Author: domenico
 */

#include "main.h"
#include "tim.h"


typedef struct _tag_turb
{
	uint16_t tick;
	uint16_t period;
}TURB;


#define LEN 2000

TURB turb[LEN];
uint16_t pw = 0;

volatile uint16_t capture_old = 0;
volatile uint16_t period_ticks = 0;

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	if ( (htim->Instance == TIM1) && (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) )
	{
		uint16_t capture = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

		turb[pw].tick = capture;
		turb[pw].period = capture - capture_old;

		if ( pw++ >= LEN )
			pw = 0;

		float periodo_us = period_ticks * (1e6f / 280000000.0f);
		float freq = 280000000.0f / period_ticks;

//		uint32_t frequency = HAL_RCC_GetPCLK1Freq() / (period_ticks);

		capture_old = capture;
	}
}
