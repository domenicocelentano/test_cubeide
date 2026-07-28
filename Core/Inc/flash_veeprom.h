/*
 * flash_veeprom.h
 *
 *  Created on: Jul 23, 2026
 *      Author: domenico
 */

#ifndef INC_FLASH_VEEPROM_H_
#define INC_FLASH_VEEPROM_H_

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>


/* ===== Simboli definiti dal linker script (regione VEEPROM) ===== */

extern uint32_t __start_VEEPROMSEG;
extern uint32_t __stop_VEEPROMSEG;

#define FLASH_VEEPROM_BANK          FLASH_BANK_2
#define FLASH_VEEPROM_SECTOR_NB     127U
#define FLASH_VEEPROM_ADDR          ((uint32_t)&__start_VEEPROMSEG)
#define FLASH_VEEPROM_SIZE          8192U          /* 8 KB, un intero settore */
#define FLASH_WORD_SIZE             16U            /* granularità di scrittura H7A3: 128 bit */

typedef enum {
    VEEPROM_IDLE = 0,
    VEEPROM_ERASING,
    VEEPROM_WRITING,
    VEEPROM_DONE,
    VEEPROM_ERROR
} veeprom_state_t;

/* ===== API pubblica ===== */

/* Da chiamare una volta all'avvio, abilita l'interrupt flash */
void Flash_VEEPROM_Init(void);

/* Cancella l'intero settore VEEPROM (8KB). Non bloccante, usa interrupt.
   Ritorna false se un'altra operazione è già in corso. */
bool Flash_VEEPROM_EraseAsync(void);

/* Scrive un array di uint16_t a partire da byte_offset (offset in byte
   rispetto all'inizio di VEEPROM, DEVE essere multiplo di 16).
   count = numero di elementi uint16_t da scrivere.
   Non bloccante, usa interrupt. Ritorna false se parametri invalidi
   o se un'altra operazione è già in corso. */
bool Flash_VEEPROM_WriteAsync(uint32_t byte_offset, const uint16_t *data, uint32_t count);

/* Stato corrente della macchina a stati */
veeprom_state_t Flash_VEEPROM_GetState(void);

/* Lettura diretta (sincrona): la flash è mappata in memoria, si legge
   come un normale array. Sicura SEMPRE, tranne se stai leggendo un
   indirizzo che è ATTUALMENTE in scrittura (stato WRITING sullo stesso offset) */
uint16_t Flash_VEEPROM_ReadU16(uint32_t byte_offset);
void Flash_VEEPROM_ReadBuffer(uint32_t byte_offset, uint16_t *dest, uint32_t count);


/* Da chiamare periodicamente nel main loop: gestisce la prosecuzione
   automatica delle operazioni multi-blocco (es. scrittura a step da 16 byte) */
void Flash_VEEPROM_Process(void);


extern void flash_veeprom_test (void);

#endif /* INC_FLASH_VEEPROM_H_ */
