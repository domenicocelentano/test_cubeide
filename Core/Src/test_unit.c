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


static volatile uint32_t test_autozero = 0;
static volatile uint32_t ms_tick = 0;

void periodic_elapsed		(TIM_HandleTypeDef *htim);
void test_unit_sp110_init	(void);
void test_unit_sp110		(void);

static SP_Handle_t sp;




static inline void delay_us(uint16_t us)
{
    // azzera il contatore
    __HAL_TIM_SET_COUNTER(&htim6, 0);

    // attendi fino a quando il contatore raggiunge us
    while (__HAL_TIM_GET_COUNTER(&htim6) < us)
    {
        // loop di attesa
    }
}

void test_unit_init(void)
{
	HAL_TIM_Base_Start(&htim6);

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

extern __IO uint32_t BspButtonState;

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

			if (test_autozero == 1)
			{
				test_autozero = 0;
				printf("Z ");
			}

			printf("raw:%6hd inH20:%5i\n", raw, p10k);
		}

	    if (BspButtonState == BUTTON_PRESSED)
	    {
	    	BspButtonState = BUTTON_RELEASED;

	    	test_autozero = 1;

			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_SET);

			SP_SetAutoZeroEnable(&sp, false);
			while (SP_GetStatus(&sp) == SP_BUSY)	;
			SP_ApplyMode(&sp);

			delay_us(1900);

			while (SP_GetStatus(&sp) == SP_BUSY)	;
			SP_SetAutoZeroEnable(&sp, true);
			SP_ApplyMode(&sp);

			while (SP_GetStatus(&sp) == SP_BUSY)	;

			HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_RESET);
	    }
	}
}

static volatile uint16_t cnt10 = 0, cnt100 = 0, cnt40;

void periodic_elapsed(TIM_HandleTypeDef *htim)
{
	//HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_SET);
	ms_tick++;

	//SP_Status_t st = SP_StartRead(&sp);	/* qui è possibile anche controllare il ritorno SP_OK; SP_BUSY; SP_ERROR*/
	//(void) st;


	if (++cnt40 >= 40U)
	{
		//cnt40 = 0;
		//SP_Status_t st = SP_StartRead(&sp);	/* qui è possibile anche controllare il ritorno SP_OK; SP_BUSY; SP_ERROR*/
		//(void) st;
	}

	if (++cnt10 >= 10U)
	{
		cnt10 = 0;
		//SP_Status_t st = SP_StartRead(&sp);	/* qui è possibile anche controllare il ritorno SP_OK; SP_BUSY; SP_ERROR*/
		//(void) st;
	}

	if (++cnt100 >= 20U)
	{
		cnt100 = 0;
		SP_Status_t st = SP_StartRead(&sp);	/* qui è possibile anche controllare il ritorno SP_OK; SP_BUSY; SP_ERROR*/
		(void) st;
	}

	//HAL_GPIO_WritePin(GPIOE, GPIO_PIN_8, GPIO_PIN_RESET);
}













