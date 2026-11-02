/* Copyright 2026
 *
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef APP_RXTX_LOG_H
#define APP_RXTX_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "functions.h"
#include "radio.h"

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG

/* RX/TX journal. Records and external-flash area are those of the F4HWN
 * RF Log (0x018000, 32 KiB): both firmwares read and append the same log. */

#define RXTX_LOG_VISIBLE_COUNT 512
#define RXTX_LOG_CHANNEL_NONE  0xFFFFu
#define RXTX_LOG_FLAG_TX       (1u << 0)
#define RXTX_LOG_FLAG_MONITOR  (1u << 2)
#define RXTX_LOG_FLAG_SESSION  (1u << 3)
// Bits 5..7 store config bank + 1; zero identifies legacy untagged rows.
#define RXTX_LOG_CONFIG_BANK_SHIFT 5u
#define RXTX_LOG_FILTER_ALL        0u
#define RXTX_LOG_FILTER_RX         1u
#define RXTX_LOG_FILTER_TX         2u
#define RXTX_LOG_SMETER_UNKNOWN    0xFFu

#define RXTX_LOG_PROFILE_NONE      0xFFu

// Leading fields of the 32-byte flash record, in the same order, then the
// Falcon settings kept in its reserved bytes (0xFF in F4HWN rows): modulation
// and bandwidth as a history profile (modulation | narrow << 2) and the code.
typedef struct {
    uint32_t frequency;
    uint32_t trafficSeq;
    uint16_t durationSeconds;
    uint16_t channel;
    uint8_t flags;
    uint8_t sMeter;
    uint8_t battVolt;
    uint8_t profile;
    uint8_t codeType;
    uint8_t code;
} RXTX_LogEntry_t;

void RXTX_LOG_Init(void);
void RXTX_LOG_BeginRx(const VFO_Info_t *vfo, FUNCTION_Type_t function);
void RXTX_LOG_BeginTx(const VFO_Info_t *vfo);
void RXTX_LOG_EndActive(void);
void RXTX_LOG_Tick500ms(void);
// An overlay app runs from the flash write buffer: no record while it runs.
void RXTX_LOG_Suspend(void);
void RXTX_LOG_Resume(void);

/* Viewer (journal module): copies up to `count` rows matching `filter`,
 * starting at row `first` from the newest one, and returns the row total. */
uint16_t RXTX_LOG_Fetch(uint8_t filter, uint16_t first, RXTX_LogEntry_t *rows, uint8_t count);
// Invalidates row `index` of the `filter` view (F4HWN skips it as well).
void RXTX_LOG_Delete(uint8_t filter, uint16_t index);
void RXTX_LOG_Clear(void);

#endif

#endif
