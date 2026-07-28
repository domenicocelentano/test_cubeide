/*
 * flash_veeprom.c
 *
 *  Created on: Jul 23, 2026
 *      Author: domenico
 */

#include "flash_veeprom.h"
#include <string.h>

static volatile veeprom_state_t state = VEEPROM_IDLE;
static volatile uint32_t last_error = 0;

/* Buffer di staging: contiene sempre un blocco allineato di 16 byte
   pronto per essere scritto con una singola chiamata HAL_FLASH_Program_IT */
static uint8_t write_chunk[FLASH_WORD_SIZE];

/* Stato di avanzamento della scrittura in corso (asincrona, a blocchi da 16 byte) */
static uint32_t write_dest_addr_base;   /* indirizzo assoluto di inizio scrittura, allineato */
static uint32_t write_total_bytes;      /* byte totali da scrivere (arrotondati a multiplo di 16) */
static uint32_t write_bytes_done;       /* byte già scritti finora */
static const uint8_t *write_src_ptr;    /* puntatore ai dati sorgente originali (non allineati) */
static uint32_t write_src_len;          /* lunghezza reale dei dati sorgente, in byte */

static volatile bool pending_next_chunk = false;


/* ============ INIT ============ */

void Flash_VEEPROM_Init(void)
{
    HAL_NVIC_SetPriority(FLASH_IRQn, 5, 0);   /* priorità bassa: non compete con interrupt real-time */
    HAL_NVIC_EnableIRQ(FLASH_IRQn);
    state = VEEPROM_IDLE;
}

/* ============ ERASE ============ */

bool Flash_VEEPROM_EraseAsync(void)
{
    FLASH_EraseInitTypeDef EraseInitStruct;
    HAL_StatusTypeDef hstatus;

    if (state == VEEPROM_ERASING || state == VEEPROM_WRITING)
    {
        return false;   /* operazione già in corso */
    }

    HAL_FLASH_Unlock();

    EraseInitStruct.TypeErase    = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.Banks        = FLASH_VEEPROM_BANK;
    EraseInitStruct.Sector       = FLASH_VEEPROM_SECTOR_NB;
    EraseInitStruct.NbSectors    = 1;
    //EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;   /* verifica corrisponda alla tua alimentazione */
    /* Il campo voltagerange non esiste/non serve per H7A3 */

    state = VEEPROM_ERASING;

    hstatus = HAL_FLASHEx_Erase_IT(&EraseInitStruct);
    if (hstatus != HAL_OK)
    {
        state = VEEPROM_ERROR;
        last_error = HAL_FLASH_GetError();
        HAL_FLASH_Lock();
        return false;
    }
    return true;   /* la callback HAL_FLASH_EndOfOperationCallback segnalerà il completamento */
}

/* ============ WRITE ============ */

/* Avvia (o prosegue) la scrittura di un singolo blocco da 16 byte */
static void write_next_chunk(void)
{
    HAL_StatusTypeDef hstatus;
    uint32_t addr = write_dest_addr_base + write_bytes_done;

    /* Prepara il blocco di 16 byte da scrivere: copia i dati reali disponibili,
       riempie il resto con 0xFF (valore "vergine", non altera nulla se il
       settore è stato cancellato prima) */
    memset(write_chunk, 0xFF, FLASH_WORD_SIZE);

    uint32_t src_offset = write_bytes_done;
    if (src_offset < write_src_len)
    {
        uint32_t remaining_src = write_src_len - src_offset;
        uint32_t to_copy = (remaining_src < FLASH_WORD_SIZE) ? remaining_src : FLASH_WORD_SIZE;
        memcpy(write_chunk, write_src_ptr + src_offset, to_copy);
    }

    state = VEEPROM_WRITING;

    hstatus = HAL_FLASH_Program_IT(FLASH_TYPEPROGRAM_FLASHWORD, addr, (uint32_t)write_chunk);
    if (hstatus != HAL_OK)
    {
        state = VEEPROM_ERROR;
        last_error = HAL_FLASH_GetError();
        HAL_FLASH_Lock();
    }
}

bool Flash_VEEPROM_WriteAsync(uint32_t byte_offset, const uint16_t *data, uint32_t count)
{
    if (state == VEEPROM_ERASING || state == VEEPROM_WRITING)
    {
        return false;   /* operazione già in corso */
    }
    if (byte_offset % FLASH_WORD_SIZE != 0)
    {
        return false;   /* offset deve essere allineato a 16 byte */
    }

    uint32_t len_bytes = count * sizeof(uint16_t);
    if (byte_offset + len_bytes > FLASH_VEEPROM_SIZE)
    {
        return false;   /* fuori dai limiti del settore riservato */
    }

    HAL_FLASH_Unlock();

    write_dest_addr_base = FLASH_VEEPROM_ADDR + byte_offset;
    write_src_ptr        = (const uint8_t *)data;
    write_src_len        = len_bytes;
    write_bytes_done     = 0;
    /* arrotonda per eccesso a multiplo di 16, per scrivere anche l'ultimo blocco parziale */
    write_total_bytes    = (len_bytes + (FLASH_WORD_SIZE - 1)) & ~(FLASH_WORD_SIZE - 1);

    write_next_chunk();
    return true;
}

/* ============ READ (sincrona, diretta) ============ */

uint16_t Flash_VEEPROM_ReadU16(uint32_t byte_offset)
{
    const uint16_t *p = (const uint16_t *)(FLASH_VEEPROM_ADDR + byte_offset);
    return *p;
}

void Flash_VEEPROM_ReadBuffer(uint32_t byte_offset, uint16_t *dest, uint32_t count)
{
    const uint8_t *src = (const uint8_t *)(FLASH_VEEPROM_ADDR + byte_offset);
    memcpy(dest, src, count * sizeof(uint16_t));
}

/* ============ STATO ============ */

veeprom_state_t Flash_VEEPROM_GetState(void)
{
    return state;
}


static inline void safe_invalidate_dcache(uint32_t addr, uint32_t size)
{
    if (SCB->CCR & SCB_CCR_DC_Msk)   /* D-Cache effettivamente abilitata? */
    {
        SCB_InvalidateDCache_by_Addr((uint32_t *)addr, size);
    }
    /* se la cache è disabilitata, non serve invalidare nulla:
       la memoria è già letta/scritta direttamente, senza cache in mezzo */
}


/* ============ CALLBACK HAL (cuore della gestione asincrona) ============ */

void HAL_FLASH_EndOfOperationCallback(uint32_t ReturnValue)
{
    if (state == VEEPROM_ERASING)
    {
        safe_invalidate_dcache(FLASH_VEEPROM_ADDR, FLASH_VEEPROM_SIZE);
        HAL_FLASH_Lock();
        state = VEEPROM_DONE;
        return;
    }

    if (state == VEEPROM_WRITING)
    {
        write_bytes_done += FLASH_WORD_SIZE;

        if (write_bytes_done < write_total_bytes)
        {
            /* NON richiamare HAL_FLASH_Program_IT qui dentro:
               ci troviamo ancora nel contesto di HAL_FLASH_IRQHandler,
               che non ha ancora ripulito pFlash.ProcedureOnGoing.
               Segnaliamo solo che serve un altro blocco. */
            pending_next_chunk = true;
        }
        else
        {
            safe_invalidate_dcache(write_dest_addr_base, write_total_bytes);
            HAL_FLASH_Lock();
            state = VEEPROM_DONE;
        }
        return;
    }
}

void Flash_VEEPROM_Process(void)
{
    if (pending_next_chunk)
    {
        pending_next_chunk = false;
        write_next_chunk();   /* qui HAL_FLASH_Program_IT viene chiamata fuori dall'IRQ,
                                  lo stato interno HAL è ormai stato ripulito correttamente */
    }
}


void HAL_FLASH_OperationErrorCallback(uint32_t ReturnValue)
{
    last_error = HAL_FLASH_GetError();
    state = VEEPROM_ERROR;
    HAL_FLASH_Lock();
    /* ReturnValue: settore fallito (erase) o indirizzo fallito (program) */
}

