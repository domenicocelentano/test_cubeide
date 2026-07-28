/*
 * test_unit.c
 *
 *  Created on: Jul 28, 2026
 *      Author: domenico
 */






#include "main.h"
#include "i2c.h"
#include "test_unit.h"

#include "sp110_drv.h"



static SP_Handle_t sp;


void test_unit_init(void)
{
	SP_Init(&sp, &hi2c1, SP_I2C_ADDR_DEFAULT, SP_RANGE_1_0_INH2O, SP_BW_100HZ);
}


void test_unit_main(void)
{
	while (1)
	{
		HAL_Delay(10);

		if (SP_DataReady(&sp))
		{
		    int16_t raw  = SP_GetRawPressure(&sp);
		    int16_t p10k = SP_GetPressure_inH2Ox10000(&sp); /* parti su 10000 del FS corrente */
		}
		SP_StartRead(&sp);
	}

	SP_SetRange(&sp, SP_RANGE_2_0_INH2O);
	SP_SetAutoZeroEnable(&sp, true);
	SP_SetAutoZeroMode(&sp, SP_AZ_ZTRACK);
	SP_ApplyMode(&sp);   /* un solo invio I2C per tutte le modifiche accumulate */
}
