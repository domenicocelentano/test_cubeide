/**
 * @file sp_sensor.c
 * @brief Vedi sp_sensor.h
 *
 * Gestione concorrenza: piu' SP_Handle_t possono condividere lo stesso
 * bus I2C (piu' sensori sullo stesso hi2c). L'occupazione del bus e il
 * "chi sta comunicando adesso" sono percio' tracciati in un piccolo
 * contesto per-bus (SP_BusCtx_t), non nel singolo SP_Handle_t: l'I2C e'
 * seriale, in un dato istante puo' esserci al massimo UNA transazione
 * in corso sul bus, quindi basta un solo puntatore "attivo" per bus.
 *
 * Le callback HAL (Tx/Rx complete, errore) sono generiche e registrate
 * una sola volta per bus, non una volta per sensore: la prima SP_Init
 * su un dato hi2c crea il contesto e registra le callback; le SP_Init
 * successive sullo stesso hi2c trovano il contesto gia' pronto.
 */

#include "sp_sensor.h"

#define SP_MAX_BUSES   3U /* numero massimo di bus I2C distinti gestiti */

typedef struct {
    I2C_HandleTypeDef *hi2c;
    SP_Handle_t        *active; /* sensore la cui transazione e' in corso, o NULL */
    volatile bool       busy;
} SP_BusCtx_t;

static SP_BusCtx_t sp_buses[SP_MAX_BUSES] = { 0 };

static void receive_callback(I2C_HandleTypeDef *hi2c);
static void transmit_callback(I2C_HandleTypeDef *hi2c);
static void error_callback(I2C_HandleTypeDef *hi2c);

static SP_BusCtx_t *SP_BusFind(I2C_HandleTypeDef *hi2c)
{
    for (uint32_t i = 0; i < SP_MAX_BUSES; i++) {
        if (sp_buses[i].hi2c == hi2c) {
            return &sp_buses[i];
        }
    }
    return NULL;
}

/* Trova il contesto del bus, creandolo (e registrando le callback HAL)
 * al primo utilizzo di quel hi2c. */
static SP_BusCtx_t *SP_BusGetOrCreate(I2C_HandleTypeDef *hi2c)
{
    SP_BusCtx_t *bus = SP_BusFind(hi2c);
    if (bus != NULL) {
        return bus; /* gia' inizializzato da un sensore precedente */
    }

    for (uint32_t i = 0; i < SP_MAX_BUSES; i++) {
        if (sp_buses[i].hi2c == NULL) {
            sp_buses[i].hi2c   = hi2c;
            sp_buses[i].active = NULL;
            sp_buses[i].busy   = false;

            HAL_I2C_RegisterCallback(hi2c, HAL_I2C_MASTER_RX_COMPLETE_CB_ID, receive_callback);
            HAL_I2C_RegisterCallback(hi2c, HAL_I2C_MASTER_TX_COMPLETE_CB_ID, transmit_callback);
            HAL_I2C_RegisterCallback(hi2c, HAL_I2C_ERROR_CB_ID, error_callback);

            return &sp_buses[i];
        }
    }

    return NULL; /* troppi bus distinti: aumentare SP_MAX_BUSES */
}

/* Callback HAL generiche: risalgono al bus, recuperano CHI aveva la
 * transazione in corso, liberano il bus e richiamano il driver. */

static void receive_callback(I2C_HandleTypeDef *hi2c)
{
    SP_BusCtx_t *bus = SP_BusFind(hi2c);
    if (bus == NULL) return;

    SP_Handle_t *h = bus->active;
    bus->active = NULL;
    bus->busy   = false;

    if (h != NULL) {
        SP_I2C_RxCpltCallback(h);
    }
}

static void transmit_callback(I2C_HandleTypeDef *hi2c)
{
    SP_BusCtx_t *bus = SP_BusFind(hi2c);
    if (bus == NULL) return;

    SP_Handle_t *h = bus->active;
    bus->active = NULL;
    bus->busy   = false;

    if (h != NULL) {
        SP_I2C_TxCpltCallback(h);
    }
}

static void error_callback(I2C_HandleTypeDef *hi2c)
{
    SP_BusCtx_t *bus = SP_BusFind(hi2c);
    if (bus == NULL) return;

    SP_Handle_t *h = bus->active;
    bus->active = NULL;
    bus->busy   = false;

    if (h != NULL) {
        SP_I2C_ErrorCallback(h);
    }
}

/* Fondo scala in decimi di pollice per codice di range (bit 0-1 del
 * Mode Register): 10,20,50,100 => 1.0, 2.0, 5.0, 10.0 inH2O. */
static const uint16_t sp_range_tenths[4] = { 10U, 20U, 50U, 100U };

uint16_t SP_GetRangeTenths(const SP_Handle_t *h)
{
    return sp_range_tenths[SP_GetRange(h)];
}

SP_Status_t SP_Init(SP_Handle_t *h, I2C_HandleTypeDef *hi2c, uint16_t i2c_addr,
                     SP_Range_t range, SP_Bandwidth_t bw)
{
    if ((h == NULL) || (hi2c == NULL)) {
        return SP_ERROR;
    }

    if (SP_BusGetOrCreate(hi2c) == NULL) {
        return SP_ERROR; /* troppi bus distinti, vedi SP_MAX_BUSES */
    }

    h->hi2c         = hi2c;
    h->addr         = i2c_addr;
    h->data_ready   = false;
    h->xfer_error   = false;
    h->raw_pressure = 0;

    /* Configurazione di default: AZ disabilitato, soppressione rumore attiva */
    h->mode_reg = 0;
    SP_SetRange(h, range);
    SP_SetBandwidth(h, bw);
    SP_SetAutoZeroEnable(h, false);
    SP_SetAutoZeroMode(h, SP_AZ_STANDARD);
    SP_SetNoiseSuppression(h, true);

    /* Mode Register + Rate Register (auto-select = 0x00), scritti insieme:
     * il Rate Register va impostato una sola volta, qui. Bloccante: siamo
     * ancora all'avvio, prima che parta lo scheduler periodico. */
    h->tx_buf[0] = h->mode_reg;
    h->tx_buf[1] = 0x00U;

    if (HAL_I2C_Master_Transmit(h->hi2c, h->addr, h->tx_buf, 2U, HAL_MAX_DELAY) != HAL_OK) {
        return SP_ERROR;
    }

    /* tFRD: tempo di assestamento filtri dopo il reset (max 60ms, par.7.2) */
    h->ready_tick = HAL_GetTick() + 60U;

    return SP_OK;
}

SP_Status_t SP_ApplyMode(SP_Handle_t *h)
{
    SP_BusCtx_t *bus = SP_BusFind(h->hi2c);
    if (bus == NULL) {
        return SP_ERROR;
    }

    __disable_irq();
    if (bus->busy) {
        __enable_irq();
        return SP_BUSY;
    }
    bus->busy   = true;
    bus->active = h;
    __enable_irq();

    h->tx_buf[0] = h->mode_reg;

    /* Un solo byte: aggiorna solo il Mode Register, il Rate Register resta invariato. */
    if (HAL_I2C_Master_Transmit_IT(h->hi2c, h->addr, h->tx_buf, 1U) != HAL_OK) {
        bus->busy   = false;
        bus->active = NULL;
        return SP_ERROR;
    }

    return SP_OK;
}

SP_Status_t SP_StartRead(SP_Handle_t *h)
{
    SP_BusCtx_t *bus = SP_BusFind(h->hi2c);
    if (bus == NULL) {
        return SP_ERROR;
    }

    __disable_irq();
    if (bus->busy) {
        __enable_irq();
        return SP_BUSY;
    }
    bus->busy   = true;
    bus->active = h;
    __enable_irq();

    h->data_ready = false;

    if (HAL_I2C_Master_Receive_IT(h->hi2c, h->addr, h->rx_buf, 2U) != HAL_OK) {
        bus->busy   = false;
        bus->active = NULL;
        return SP_ERROR;
    }

    return SP_OK;
}

bool SP_DataReady(SP_Handle_t *h)
{
    return h->data_ready;
}

bool SP_IsReady(SP_Handle_t *h)
{
    return (int32_t)(HAL_GetTick() - h->ready_tick) >= 0;
}

int16_t SP_GetRawPressure(SP_Handle_t *h)
{
    return h->raw_pressure;
}

int16_t SP_GetPressure_inH2Ox10000(SP_Handle_t *h)
{
    /* Nota: il range selezionato si semplifica algebricamente in Eq.1
     * quando l'unita' di uscita e' "range/10000 inH2O" per conteggio:
     *   P = raw/(0.9*32768) * range
     *   P_x10000_su_range = P * (10000/range) = raw * 10000 / (0.9*32768)
     * Il risultato non dipende quindi dal range in questa formula, ma
     * il suo significato fisico si' (vedi SP_GetRangeTenths). */
    int32_t scaled = ((int32_t)h->raw_pressure * 10000) / (int32_t)SP_PRESSURE_DENOM;
    return (int16_t)scaled;
}

void SP_I2C_TxCpltCallback(SP_Handle_t *h)
{
    (void)h; /* nulla da fare lato dati per una scrittura completata */
}

void SP_I2C_RxCpltCallback(SP_Handle_t *h)
{
    h->raw_pressure = (int16_t)(((uint16_t)h->rx_buf[0] << 8) | h->rx_buf[1]);
    h->data_ready   = true;
}

void SP_I2C_ErrorCallback(SP_Handle_t *h)
{
    h->xfer_error = true;
}
