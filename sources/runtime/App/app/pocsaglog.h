/* Falcon POCSAG journal: the pages POCSAG Scan receives, kept in external
 * flash for the history's POCSAG page. */
#ifndef APP_POCSAGLOG_H
#define APP_POCSAGLOG_H

#include <stdbool.h>
#include <stdint.h>

/* 32 KiB of the v7 Apps reserve (slots 64..67), before the 8 KiB kept for
 * the MESSAGES move (0x18A000) and the store banks (0x18C000): 8 sectors
 * used in turn, about 49 pages of 512 characters or 340 of 50. */
#define PLOG_BASE    0x182000u
#define PLOG_SECTOR  0x1000u
#define PLOG_SECTORS 8u
#define PLOG_MAX     1024u /* payload bytes of a record, at most */

/* The payload POCSAG Scan writes and the POCSAG page reads: this header,
 * then the text, one byte a character. */
typedef struct {
    uint32_t ric;      /* 21-bit address                                 */
    uint32_t t;        /* ticks_ms when received (this power-on only)    */
    uint32_t freq;     /* x10 Hz                                         */
    int16_t rssi;      /* dBm                                            */
    uint16_t channel;  /* memory channel, 0xFFFF for the VFO             */
    uint8_t func, pad;
    uint16_t chars;    /* characters of the text                         */
} PLOG_Page_t;

_Static_assert(sizeof(PLOG_Page_t) == 20, "POCSAG journal page header");

/* PLOG_Read results besides a payload length. */
#define PLOG_SESSION (-1) /* a session marker: POCSAG Scan opened again */
#define PLOG_SKIP    (-2) /* deleted or incomplete                       */
#define PLOG_NONE    (-3) /* no record there                             */

/* Resident: the services handed to apps, and the start of an app session. */
uint32_t PLOG_Append(const void *payload, unsigned len); /* its address, 0 if not written */
int PLOG_Read(uint32_t address, void *buf, unsigned max); /* payload length (all of it), or above */
uint32_t PLOG_Prev(uint32_t address); /* 0: the newest record; 0 once past the oldest */
void PLOG_SessionStart(void);

/* Native module "messages": the POCSAG page. */
bool PLOG_SameBoot(uint32_t address); /* written since this power-on: its age is known */
void PLOG_Delete(uint32_t address);
void PLOG_Clear(void);

#endif
