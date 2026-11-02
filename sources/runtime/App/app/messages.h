/* Falcon messages: the messages received by the FMessage app, kept in
 * external flash and shown by the history's MESSAGES page. */
#ifndef APP_MESSAGES_H
#define APP_MESSAGES_H

#include <stdbool.h>
#include <stdint.h>

#define MSG_TEXT_MAX  64u /* characters of a message                     */
#define MSG_SHOWN_MAX 32u /* messages listed, newest first               */
#define MSG_ROWS_MAX  (2u * MSG_SHOWN_MAX) /* with the session markers   */

/* One row of the MESSAGES page: a message, or a session marker (a dotted
 * line: FMessage was opened again). */
typedef struct {
    uint8_t slot;      /* record slot, for opening and deleting           */
    bool marker;
    char cs[2];
    uint8_t len;
    uint8_t number;    /* arrival number, 1..32                          */
    char text[15];     /* the start of the text                          */
} MSG_Row_t;

/* Resident: the service handed to apps, and the start of an app session. */
void MSG_Save(const void *message, unsigned len);
void MSG_SessionStart(void);

/* Native module "messages" (NATIVE_MESSAGES). MSG_Fetch fills up to count
 * rows from row first (newest first) and returns the number of rows; with
 * selected < rows, *ordinal gets the selected message's number (1 = newest)
 * and *messages the number of messages listed. */
uint16_t MSG_Fetch(uint16_t first, MSG_Row_t *rows, uint8_t count, uint16_t selected, uint16_t *ordinal,
                   uint16_t *messages);
uint8_t MSG_ReadText(uint8_t slot, char *cs, char *text); /* returns the length */
void MSG_Delete(uint8_t slot);
void MSG_Clear(void);

#endif
