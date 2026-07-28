/*
 * flash_veeprom_test.c
 *
 *  Created on: Jul 23, 2026
 *      Author: domenico
 */


#include "flash_veeprom.h"
#include <stdio.h>   /* se usi printf/UART per il log, altrimenti rimuovi */

/* Timeout di sicurezza per l'attesa di ciascuna operazione asincrona (in ms).
   Serve solo a evitare un blocco infinito in caso di errore hardware imprevisto,
   NON sostituisce la logica IT: l'operazione resta comunque non bloccante
   per il resto del sistema, qui stiamo solo aspettando noi, in questa funzione di test. */
#define VEEPROM_TEST_TIMEOUT_MS   1000U

/* Attende che lo stato passi a DONE o ERROR, con timeout.
   Ritorna true se DONE, false se ERROR o timeout. */
static bool wait_veeprom_done(void)
{
    uint32_t start = HAL_GetTick();

    while (1)
    {
        Flash_VEEPROM_Process();   /* <-- fondamentale: fa proseguire la scrittura a blocchi */

        veeprom_state_t st = Flash_VEEPROM_GetState();

        if (st == VEEPROM_DONE)  return true;
        if (st == VEEPROM_ERROR) return false;

        if ((HAL_GetTick() - start) > VEEPROM_TEST_TIMEOUT_MS) return false;
    }
}

void flash_veeprom_test(void)
{

    Flash_VEEPROM_Init();

#if 1
    /* ===== 1. ERASE ===== */
    if (!Flash_VEEPROM_EraseAsync())
    {
        printf("VEEPROM test: erase non avviato (operazione gia' in corso?)\r\n");
        return;
    }
    if (!wait_veeprom_done())
    {
        printf("VEEPROM test: ERRORE durante erase (timeout o fault)\r\n");
        return;
    }
    printf("VEEPROM test: erase completato\r\n");

#endif

    /* ===== 2. WRITE ===== */
    uint16_t my_values[512];

    for (uint32_t i = 0; i < 512; i++)
    {
        my_values[i] = (uint16_t)(i + 1);   /* 1,2,3,...,512 */
    }

    if (!Flash_VEEPROM_WriteAsync(0, my_values, 512))
    {
        printf("VEEPROM test: write non avviato (parametri invalidi o operazione in corso?)\r\n");
        return;
    }
    if (!wait_veeprom_done())
    {
        printf("VEEPROM test: ERRORE durante write (timeout o fault)\r\n");
        return;
    }
    printf("VEEPROM test: scrittura completata\r\n");

    /* ===== 3. VERIFICA LETTURA ===== */
    uint16_t buf[512];
    Flash_VEEPROM_ReadBuffer(0, buf, 512);

    bool mismatch = false;
    for (uint32_t i = 0; i < 512; i++)
    {
        if (buf[i] != my_values[i])
        {
            printf("VEEPROM test: MISMATCH a indice %lu: scritto=%u letto=%u\r\n",
                   (unsigned long)i, my_values[i], buf[i]);
            mismatch = true;
            break;   /* rimuovi il break se vuoi vedere tutti i mismatch, non solo il primo */
        }
    }

    if (!mismatch)
    {
        printf("VEEPROM test: OK, tutti i 512 valori corrispondono\r\n");
    }
    else
    {
        printf("VEEPROM test: FALLITO, dati non corrispondenti\r\n");
    }
}
