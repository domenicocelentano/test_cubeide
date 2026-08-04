/*
 * test_unit.c
 *
 *  Created on: Jul 28, 2026
 *      Author: domenico
 */


#include "main.h"
#include "inttypes.h"
#include "tim.h"
#include "i2c.h"

#include "sp110_drv.h"

#include "test_unit.h"

static volatile uint32_t ms_tick = 0;

void periodic_elapsed		(TIM_HandleTypeDef *htim);
void test_unit_sp110_init	(void);
void test_unit_sp110		(void);

static SP_Handle_t sp;


void test_unit_init(void)
{
	if (HAL_TIM_RegisterCallback(&htim7, HAL_TIM_PERIOD_ELAPSED_CB_ID, &periodic_elapsed) != HAL_OK)
	{
		Error_Handler();
	}
}


void test_unit(void)
{
	test_unit_init();

	test_unit_sp110_init();

	if (HAL_TIM_Base_Start_IT(&htim7) != HAL_OK)
	{
		Error_Handler();
	}

	test_unit_sp110();
}


/*************************************+ test unit sp110 *********************************************/

void test_unit_sp110_init(void)
{
	SP_Status_t st = SP_Init(&sp, &hi2c1, SP_I2C_ADDR_DEFAULT, SP_RANGE_10_INH2O, SP_BW_100HZ);
	if (st != SP_OK)
	{
		/* Sensore assente, indirzzo errato, bus in stato inatteso... */
		Error_Handler();
	}
}


void test_unit_sp110(void)
{
	static uint16_t count = 0;

	while (true)
	{
		if (SP_DataReady(&sp))
		{
			static int16_t raw;
			static int16_t p10k;

			raw  = SP_GetRawPressure(&sp);
			p10k = SP_GetPressure_inH2Ox10000(&sp); /* parti su 10000 del FS corrente */

			printf("raw:%6hd inH20:%5i\n", raw, p10k);

			if (count++ >= 20)
			{
				HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_SET);

				SP_SetAutoZeroEnable(&sp, false);
				SP_ApplyMode(&sp);   /* un solo invio I2C per tutte le modifiche accumulate */

				uint64_t del = 20000;	while (del--)	;

				SP_SetAutoZeroEnable(&sp, true);
				SP_ApplyMode(&sp);   /* un solo invio I2C per tutte le modifiche accumulate */

				HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_RESET);

				count = 0;

				printf("Autozero\n");
			}
		}
	}
}




#if 0
	while (1)
	{
	    static int16_t raw;
	    static int16_t p10k;

		HAL_Delay(100);

		if (SP_DataReady(&sp))
		{
		    raw  = SP_GetRawPressure(&sp);
		    p10k = SP_GetPressure_inH2Ox10000(&sp); /* parti su 10000 del FS corrente */

		    printf("raw:%6hd inH20:%5i\n", raw, p10k);
		    //printf("raw:%" PRIu16 "inH20:%" PRIu16 "\n", raw, p10k);
		}

		if (++count >= 20)
		{

			//SP_SetRange(&sp, SP_RANGE_10_INH2O);
			//SP_SetAutoZeroMode(&sp, SP_AZ_STANDARD);
			//SP_SetNoiseSuppression(&sp, false);
			//SP_SetAutoZeroEnable(&sp, false);
			//SP_ApplyMode(&sp);   /* un solo invio I2C per tutte le modifiche accumulate */

			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_SET);

			SP_SetAutoZeroEnable(&sp, false);
			SP_ApplyMode(&sp);   /* un solo invio I2C per tutte le modifiche accumulate */

			{
				uint64_t del = 20000;
				while (del--)	;
			}
			//HAL_Delay(1);

			SP_SetAutoZeroEnable(&sp, true);
			SP_ApplyMode(&sp);   /* un solo invio I2C per tutte le modifiche accumulate */

			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_RESET);

			HAL_Delay(2);

			count = 0;

			printf("Autozero\n");
		}

		SP_StartRead(&sp);
	}

	SP_SetRange(&sp, SP_RANGE_2_0_INH2O);
	SP_SetAutoZeroEnable(&sp, true);
	SP_SetAutoZeroMode(&sp, SP_AZ_ZTRACK);
	SP_ApplyMode(&sp);   /* un solo invio I2C per tutte le modifiche accumulate */
}
#endif


static volatile uint16_t cnt10 = 0, cnt100 = 0;

void periodic_elapsed(TIM_HandleTypeDef *htim)
{
	ms_tick++;

	if (cnt10++ >= 10U)
	{
		cnt10 = 0;
		SP_Status_t st = SP_StartRead(&sp);	/* qui è possibile anche controllare il ritorno SP_OK; SP_BUSY; SP_ERROR*/
		(void) st;
	}

	if (cnt100++ >= 100U)
	{
		cnt100 = 0;
		//SP_Status_t st = SP_StartRead(&sp);	/* qui è possibile anche controllare il ritorno SP_OK; SP_BUSY; SP_ERROR*/
		//(void) st;
	}
}













