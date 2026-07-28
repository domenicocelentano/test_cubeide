/*
 * tim1_drv.c
 *
 *  Created on: Jul 20, 2026
 *      Author: domenico
 */

#include "main.h"
#include "tim.h"
#include "tim1_drv.h"


typedef struct _tag_turb
{
	uint16_t cc;
	uint64_t cp;
	uint64_t time;
	uint16_t ovf;
}TURB;




static DBG_RING dbg_ring;

#define LEN 1000

static volatile TURB turb[LEN];
static volatile uint16_t pw = 0;
static volatile uint16_t pr = 0;

volatile uint32_t overflow_count = 0;
volatile uint8_t  is_first_capture = 1; // Flag per gestire la prima misura
volatile uint64_t last_capture;

volatile DBG dbg[2048];
volatile uint32_t pw_dbg = 0;
volatile uint32_t pr_dbg = 0;
volatile uint32_t id = 0;


void dbg_push_isr(const DBG *src);

uint32_t dbg_count(void)
{
	return dbg_ring.wr - dbg_ring.rd;
}

static inline uint32_t dbg_empty(void)
{
	return dbg_ring.wr == dbg_ring.rd;
}

static inline uint32_t dbg_full(void)
{
	return (dbg_ring.wr - dbg_ring.wr >= DBG_SIZE);
}

uint32_t dbg_get_overflow(void)
{
	return dbg_ring.overflow_cnt;
}


// ----------------------------------------------------------------------------
// Callback eseguito su TIM1_UP_IRQHandler (Overflow / Update)
// ----------------------------------------------------------------------------
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        overflow_count++;

        DBG dbg;
        dbg.id	= id++;
        dbg.type = 0x01;
        dbg.ovf = overflow_count;
        dbg.time1 = TIM1->CNT;
        dbg.time2 = TIM2->CNT;
        dbg.time1_ovf = 0;
        dbg.period = 0x00;
        dbg.backlog = dbg_count();

        dbg_push_isr(&dbg);
    }
    else if (htim->Instance == TIM2)
    {
    	;
    }
}


void dbg_push_isr(const DBG *src)
{
    uint32_t wr = dbg_ring.wr;
    uint32_t rd = dbg_ring.rd;
    uint32_t pending = wr -rd;

    if ( dbg_full() ==  1 )		return;

    /* buffer pieno */
    if ((wr - rd) >= DBG_SIZE)
    {
    	dbg_ring.overflow_cnt++;
        return;
    }

    dbg_ring.buffer[wr & DBG_MASK] = *src;

    __DMB();

    dbg_ring.wr = wr + 1;

    if (pending > dbg_ring.max_pending)
    {
    	dbg_ring.max_pending = pending;
    }
}

uint16_t dbg_pop(DBG *item)
{
	uint32_t rd = dbg_ring.rd;

	if (dbg_empty() ==  1 )	return 0;

	*item = dbg_ring.buffer[rd & DBG_MASK];

	__DMB();

	dbg_ring.rd = rd + 1;

	return 1;
}

// ----------------------------------------------------------------------------
// Callback eseguito su TIM1_CC_IRQHandler (Capture)
// ----------------------------------------------------------------------------
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
    	uint32_t ccr = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

    	uint64_t current_capture = ((uint64_t)overflow_count * ((uint64_t)htim->Instance->ARR + 1)) + ccr;

    	uint64_t period = current_capture - last_capture;

    	last_capture = current_capture;

    	DBG dbg;
    	dbg.id			= id++;
        dbg.type		= 0x00;
        dbg.ovf			= overflow_count;
        dbg.time1		= TIM1->CNT;
        dbg.time2		= TIM2->CNT;
        dbg.time1_ovf	= current_capture;
        dbg.period		= period;
        dbg.backlog		= dbg_count();

        dbg_push_isr(&dbg);

static uint16_t count = 0;

	if (count++ >= 10 )
	{
		count = 0;
	}
        if (is_first_capture)
        {
        	last_capture = current_capture;
            is_first_capture = 0;
            return;
        }
    }
}
