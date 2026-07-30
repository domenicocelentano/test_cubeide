/**
 * @file    sp_sensor.h
 * @brief   Driver I2C non bloccante per sensore di pressione differenziale
 *          Superior Sensor Technology SP110 (Datasheet DS-0002D).
 *          Solo aritmetica intera, nessun float.
 *
 * Modello di comunicazione I2C (par. 10.4.2 del datasheet):
 *  - Scrittura di 1 byte -> aggiorna SOLO il Mode Control Register (pag.10)
 *  - Scrittura di 2 byte -> Mode Control Register + Rate Control Register (pag.11)
 *  - Lettura di 2 byte   -> pressione raw (int16, MSB first)
 *
 * Il Rate Control Register va scritto una sola volta in SP_Init.
 * A runtime si modifica solo il Mode Control Register: i singoli campi
 * (range, banda, auto-zero, soppressione rumore) si impostano con le
 * funzioni inline sotto, che agiscono sulla copia in RAM del registro
 * preservando gli altri bit; SP_ApplyMode() invia poi il byte completo.
 *
 * Conversione in inH2O (Eq.1 pag.11): P = raw/(0.9*32768) * range.
 * Per restare sempre in un int16 sfruttando la massima risoluzione
 * disponibile per QUALSIASI range selezionato, il driver restituisce
 * la pressione in unita' di "range/10000 inH2O" (cioe' parti su 10000
 * del fondo scala corrente, con margine di overrange fino a ~11113).
 * Per il range piu' piccolo (1.0 inH2O) questo equivale esattamente a
 * 1 conteggio = 1/10000 di pollice, come richiesto.
 */

#ifndef SP_SENSOR_H
#define SP_SENSOR_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* Indirizzo di default (piedino SCK a massa -> 0x28 su 7 bit).
 * La HAL richiede l'indirizzo gia' shiftato a 8 bit. */
#define SP_I2C_ADDR_DEFAULT   ((uint16_t)(0x30U << 1))

/* Denominatore di Eq.1: round(0.9 * 32768) */
#define SP_PRESSURE_DENOM     29491U

/* Range di fondo scala SP110 (bit 0-1 del Mode Register, pag.10) */
typedef enum {
    SP_RANGE_1_0_INH2O = 0,
    SP_RANGE_2_0_INH2O = 1,
    SP_RANGE_5_0_INH2O = 2,
    SP_RANGE_10_INH2O  = 3
} SP_Range_t;

/* Frequenza di taglio del filtro BW (bit 2-4 del Mode Register, pag.10) */
typedef enum {
    SP_BW_25HZ  = 0,
    SP_BW_35HZ  = 1,
    SP_BW_50HZ  = 2,
    SP_BW_65HZ  = 3,
    SP_BW_100HZ = 4,
    SP_BW_130HZ = 5,
    SP_BW_180HZ = 6,
    SP_BW_250HZ = 7
} SP_Bandwidth_t;

typedef enum {
    SP_AZ_STANDARD = 0, /* Auto-Zero: cattura singola */
    SP_AZ_ZTRACK   = 1  /* Auto-Zero: cattura + tracking (Z-Track) */
} SP_AzMode_t;

typedef enum {
    SP_OK = 0,
    SP_BUSY,   /* transazione I2C gia' in corso, riprovare al ciclo successivo */
    SP_ERROR
} SP_Status_t;

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint16_t           addr;      /* indirizzo 8-bit per la HAL */
    uint8_t            mode_reg;  /* immagine RAM del Mode Control Register completo */

    volatile bool      xfer_busy;
    volatile bool      data_ready;
    volatile bool      xfer_error;

    uint8_t            tx_buf[2];
    uint8_t            rx_buf[2];
    int16_t            raw_pressure;

    uint32_t           ready_tick; /* tick oltre il quale i filtri sono assestati */
} SP_Handle_t;


uint16_t SP_reg_callback( void (*sp_cb) (SP_Handle_t *sp));


/* ---- Modifica campi del Mode Register (solo RAM, non inviano nulla) ---- */

static inline void SP_SetRange(SP_Handle_t *h, SP_Range_t range)
{
    h->mode_reg = (uint8_t)((h->mode_reg & ~0x03U) | ((uint8_t)range & 0x03U));
}

static inline void SP_SetBandwidth(SP_Handle_t *h, SP_Bandwidth_t bw)
{
    h->mode_reg = (uint8_t)((h->mode_reg & ~(0x07U << 2)) | (((uint8_t)bw & 0x07U) << 2));
}

static inline void SP_SetAutoZeroEnable(SP_Handle_t *h, bool enable)
{
    h->mode_reg = enable ? (uint8_t)(h->mode_reg | (1U << 5))
                          : (uint8_t)(h->mode_reg & ~(1U << 5));
}

static inline void SP_SetAutoZeroMode(SP_Handle_t *h, SP_AzMode_t mode)
{
    h->mode_reg = (mode == SP_AZ_ZTRACK) ? (uint8_t)(h->mode_reg | (1U << 6))
                                          : (uint8_t)(h->mode_reg & ~(1U << 6));
}

static inline void SP_SetNoiseSuppression(SP_Handle_t *h, bool enable)
{
    h->mode_reg = enable ? (uint8_t)(h->mode_reg | (1U << 7))
                          : (uint8_t)(h->mode_reg & ~(1U << 7));
}

/* ---- Lettura campi correnti (dalla copia RAM) ---- */

static inline SP_Range_t SP_GetRange(const SP_Handle_t *h)
{
    return (SP_Range_t)(h->mode_reg & 0x03U);
}

static inline SP_Bandwidth_t SP_GetBandwidth(const SP_Handle_t *h)
{
    return (SP_Bandwidth_t)((h->mode_reg >> 2) & 0x07U);
}

static inline bool SP_GetAutoZeroEnable(const SP_Handle_t *h)
{
    return (h->mode_reg & (1U << 5)) != 0U;
}

static inline bool SP_GetNoiseSuppression(const SP_Handle_t *h)
{
    return (h->mode_reg & (1U << 7)) != 0U;
}

/* Fondo scala corrente espresso in decimi di pollice (10,20,50,100 = 1.0..10.0 inH2O).
 * Utile se serve risalire al valore assoluto in pollici (vedi SP_GetPressure_inH2Ox10000). */
uint16_t SP_GetRangeTenths(const SP_Handle_t *h);

/* ---- Ciclo di vita / comunicazione ---- */

/**
 * Inizializza il sensore: imposta Mode Register e Rate Register (auto-select,
 * 0x00) con un'unica scrittura bloccante. Da chiamare una sola volta
 * all'avvio, prima che parta lo scheduler periodico.
 */
SP_Status_t SP_Init(SP_Handle_t *h, I2C_HandleTypeDef *hi2c, uint16_t i2c_addr,
                     SP_Range_t range, SP_Bandwidth_t bw);

/**
 * Invia al sensore il Mode Control Register corrente (1 byte, non bloccante).
 * Da chiamare dopo una o piu' SP_Set*() per rendere effettive le modifiche.
 * Il Rate Control Register non viene mai toccato da questa funzione.
 * Ritorna SP_BUSY se il bus e' occupato: riprovare al ciclo successivo.
 */
SP_Status_t SP_ApplyMode(SP_Handle_t *h);

/* Avvia una lettura non bloccante della pressione (2 byte).
 * Da chiamare ad ogni ciclo del sampler (es. ogni 10ms). */
SP_Status_t SP_StartRead(SP_Handle_t *h);

/* True se e' terminata una lettura dall'ultima SP_StartRead. */
bool SP_DataReady(SP_Handle_t *h);

/* True se il tempo di assestamento dei filtri (~60ms da POR) e' trascorso. */
bool SP_IsReady(SP_Handle_t *h);

/* Ultimo campione raw ricevuto (int16, complemento a 2). */
int16_t SP_GetRawPressure(SP_Handle_t *h);

/* Ultimo campione convertito: parti su 10000 del fondo scala corrente
 * (1 conteggio = range/10000 inH2O). Solo interi, sempre entro un int16. */
int16_t SP_GetPressure_inH2Ox10000(SP_Handle_t *h);

/* Da richiamare dalle callback globali della HAL per l'istanza I2C usata. */
void SP_I2C_TxCpltCallback(SP_Handle_t *h);
void SP_I2C_RxCpltCallback(SP_Handle_t *h);
void SP_I2C_ErrorCallback(SP_Handle_t *h);

#endif /* SP_SENSOR_H */
