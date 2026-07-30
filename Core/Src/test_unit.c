/*
 * test_unit.c
 *
 *  Created on: Jul 28, 2026
 *      Author: domenico
 */


#include "main.h"
#include "i2c.h"
#include "sp110_drv.h"

#include "test_unit.h"

static SP_Handle_t sp;

void test_unit_sp110(void)
{
    uint32_t count = 0;

	SP_Init(&sp, &hi2c1, SP_I2C_ADDR_DEFAULT, SP_RANGE_1_0_INH2O, SP_BW_100HZ);

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
		}

		if (++count >= 20)
		{
			SP_SetAutoZeroEnable(&sp, true);
			SP_SetAutoZeroMode(&sp, SP_AZ_ZTRACK);
			SP_SetRange(&sp, SP_RANGE_10_INH2O);
			SP_ApplyMode(&sp);   /* un solo invio I2C per tutte le modifiche accumulate */
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


