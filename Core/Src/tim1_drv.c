/*
 * tim1_drv.c
 *
 *  Created on: Jul 20, 2026
 *      Author: domenico
 */

#include "main.h"
#include "tim.h"


#define LEN 2000

static volatile uint16_t turb[LEN];
static volatile uint16_t pw = 0;

// Variabili globali/statiche per la gestione dei tempi
volatile uint32_t overflow_count = 0;
volatile uint64_t last_capture_tick = 0;
volatile uint64_t signal_period_ticks = 0;
volatile uint8_t  is_first_capture = 1; // Flag per gestire la prima misura

// ----------------------------------------------------------------------------
// Callback eseguito su TIM1_UP_IRQHandler (Overflow / Update)
// ----------------------------------------------------------------------------
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
        overflow_count++;
    }
}

// ----------------------------------------------------------------------------
// Callback eseguito su TIM1_CC_IRQHandler (Capture)
// ----------------------------------------------------------------------------
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1) {

        // 1. Lettura immediata del registro di Capture e dell'Overflow attuale
        uint32_t ccr = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
        uint32_t current_overflow = overflow_count;

        // 2. Correzione della Race Condition tra Overflow e Capture
        // Se UIF è pendente e il CCR è molto basso, l'overflow si è verificato
        // un istante prima della capture ma l'ISR di Overflow non ha ancora incrementato
        // la variabile `overflow_count`.
        if (__HAL_TIM_GET_FLAG(htim, TIM_FLAG_UPDATE) != RESET) {
            if (ccr < (htim->Instance->ARR / 2)) {
                current_overflow++;
            }
        }

        // 3. Calcolo del tempo assoluto corrente espressi in tick totali
        uint64_t current_capture_tick = ((uint64_t)current_overflow * ((uint64_t)htim->Instance->ARR + 1)) + ccr;

        // 4. Gestione della Prima Volta vs Calcolo del Periodo
        if (is_first_capture)
        {
            // Primo fronte: salviamo il riferimento senza calcolare il periodo
            last_capture_tick = current_capture_tick;
            is_first_capture = 0;
            return;
        }
        else
        {
            // Fronti successivi: calcoliamo il periodo come differenza
            signal_period_ticks = current_capture_tick - last_capture_tick;

            // Aggiorniamo il timestamp precedente per la prossima misura
            last_capture_tick = current_capture_tick;

            // Optional: qui puoi convertire il periodo in microsecondi
            // float period_us = (float)signal_period_ticks * 3.2f;
        }

uint16_t signal_periodic_16;

        if (signal_period_ticks > 0xFFFF)
        {
        	signal_periodic_16 = 0xFFFf;
        }
        else
        {
        	signal_periodic_16 = (uint16_t) signal_period_ticks;
        }

        turb[pw++] = signal_periodic_16;

        if (pw >= LEN)
        {
        	pw = 0;
        }
    }
}

uint16_t tim1_drv_get_value(void)
{
	return turb[pw-2];
}
