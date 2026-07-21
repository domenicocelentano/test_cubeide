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

static volatile TURB turb[LEN];
static volatile uint16_t pw = 0;

static volatile uint16_t capture_old = 0;

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
	if ( (htim->Instance == TIM1) && (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) )
	{
		uint16_t capture = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

		turb[pw].tick = capture;
		turb[pw].period = capture - capture_old;

		if ( ++pw >= LEN )
			pw = 0;

		capture_old = capture;
	}
}
