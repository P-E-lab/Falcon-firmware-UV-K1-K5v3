/* Falcon messages: the messages received by FMessage, in external flash at
 * 0x012000 (the area F4HWN v7 marks FREE), two 4 KiB sectors used in turn.
 *
 * Each record takes 80 bytes, 51 per sector:
 *   0  type: 'M' message, 'S' session marker, 0xFF free
 *   1  0xFF, 0x00 once deleted (a bit cleared: no erase)
 *   2  sequence number, 16 bits, little endian
 *   4  sender callsign, 2 characters
 *   6  text length
 *   7  0xA5 once the record is complete (written last)
 *   8  text, up to 64 characters
 *  72  arrival number of the newest message so far, 1..32 then 1 again
 *      (a marker repeats it); it starts again at 1 once cleared
 * Records follow each other in a sector; when it is full, the other sector
 * is erased and continues. Writes never use the sector cache: FMessage saves
 * while it runs from the overlay RAM, which is that cache. */
#include <stddef.h>
#include <string.h>

#include "app/messages.h"
#include "apps/app_overlay.h"
#include "driver/py25q16.h"

#define MSG_BASE    0x012000u
#define MSG_SECTOR  0x1000u
#define MSG_SIZE    80u
#define MSG_PER_SEC (MSG_SECTOR / MSG_SIZE) /* 51 */
#define MSG_COMMIT  0xA5u
#define MSG_NUMBER  72u /* offset of the arrival number */
#define MSG_TYPE_MESSAGE 'M'
#define MSG_TYPE_SESSION 'S'

typedef struct {
    uint8_t type, deleted;
    uint16_t seq;
    char cs[2];
    uint8_t len, commit;
} MSG_Head_t;

_Static_assert(sizeof(MSG_Head_t) == 8, "message record header");

static bool sessionPending;

static uint32_t SlotAddress(uint8_t slot)
{
    return MSG_BASE + (slot >= MSG_PER_SEC ? MSG_SECTOR : 0) + (slot % MSG_PER_SEC) * MSG_SIZE;
}

static void ReadHead(uint8_t slot, MSG_Head_t *h)
{
    PY25Q16_ReadPhysical(SlotAddress(slot), h, sizeof *h);
}

/* Used records of a sector (they start at its first slot), and the sequence
 * number of its first one. A sector holding anything else is erased. */
static uint8_t SectorUsed(uint8_t sector, uint16_t *firstSeq)
{
    MSG_Head_t h;
    uint8_t n = 0;
    for (; n < MSG_PER_SEC; n++) {
        ReadHead(sector * MSG_PER_SEC + n, &h);
        if (h.type == 0xFF)
            break;
        if (!n && h.type != MSG_TYPE_MESSAGE && h.type != MSG_TYPE_SESSION) {
            PY25Q16_EraseRaw(MSG_BASE + sector * MSG_SECTOR);
            break;
        }
        if (!n)
            *firstSeq = h.seq;
    }
    return n;
}

typedef struct {
    uint8_t newer, used[2];
} MSG_Layout_t;

/* The sector holding the newest records comes first. */
static void Layout(MSG_Layout_t *l)
{
    uint16_t seq[2] = {0, 0};
    l->used[0] = SectorUsed(0, &seq[0]);
    l->used[1] = SectorUsed(1, &seq[1]);
    l->newer = l->used[1] && (!l->used[0] || (int16_t)(seq[1] - seq[0]) > 0);
}

/* ---- resident: written while FMessage runs ---- */

static void Append(uint8_t type, const char *cs, const char *text, uint8_t len)
{
    MSG_Layout_t l;
    Layout(&l);
    uint8_t sector = l.newer, index = l.used[sector];
    uint16_t seq = 0;
    uint8_t number = 0;
    if (index) {
        const uint8_t slot = sector * MSG_PER_SEC + index - 1u;
        MSG_Head_t last;
        ReadHead(slot, &last);
        seq = last.seq + 1u;
        PY25Q16_ReadPhysical(SlotAddress(slot) + MSG_NUMBER, &number, 1);
        if (number > MSG_SHOWN_MAX)
            number = 0;
    }
    if (type == MSG_TYPE_MESSAGE) /* deleted messages keep their numbers */
        number = number % MSG_SHOWN_MAX + 1u;
    if (index == MSG_PER_SEC) { /* full: continue in the other sector */
        sector ^= 1u;
        index = 0;
        PY25Q16_EraseRaw(MSG_BASE + sector * MSG_SECTOR);
    }
    /* Header, text straight from the caller (no copy on the app's stack),
     * then the commit byte. */
    MSG_Head_t h = {type, 0xFF, seq, {(char)0xFF, (char)0xFF}, len, 0xFF};
    if (cs)
        memcpy(h.cs, cs, 2);
    const uint32_t address = SlotAddress(sector * MSG_PER_SEC + index);
    PY25Q16_ProgramRaw(address, &h, sizeof h);
    if (len)
        PY25Q16_ProgramRaw(address + sizeof h, text, len);
    PY25Q16_ProgramRaw(address + MSG_NUMBER, &number, 1);
    h.commit = MSG_COMMIT;
    PY25Q16_ProgramRaw(address + offsetof(MSG_Head_t, commit), &h.commit, 1);
}

void MSG_SessionStart(void)
{
    sessionPending = true;
}

/* App service: message points to the sender (2 characters) followed, four
 * bytes on, by the text; len is the text length. The first message of an app
 * session is preceded by a marker. */
void MSG_Save(const void *message, unsigned len)
{
    const char *m = message;
    if (len > MSG_TEXT_MAX)
        len = MSG_TEXT_MAX;
    if (sessionPending) {
        sessionPending = false;
        Append(MSG_TYPE_SESSION, NULL, NULL, 0);
    }
    Append(MSG_TYPE_MESSAGE, m, m + 4, (uint8_t)len);
}

/* ---- native module: the MESSAGES page ---- */

/* The rows, newest first: deleted and incomplete records are skipped, a
 * marker only separates two messages, and the list stops at 32 messages. */
NATIVE_CODE(messages)
uint16_t MSG_Fetch(uint16_t first, MSG_Row_t *rows, uint8_t count, uint16_t selected, uint16_t *ordinal,
                   uint16_t *messages)
{
    MSG_Layout_t l;
    Layout(&l);
    uint16_t row = 0, shown = 0;
    bool marker = false;
    for (uint8_t pass = 0; pass < 2 && shown < MSG_SHOWN_MAX; pass++) {
        const uint8_t sector = pass ? !l.newer : l.newer;
        for (uint8_t i = l.used[sector]; i-- && shown < MSG_SHOWN_MAX;) {
            const uint8_t slot = sector * MSG_PER_SEC + i;
            MSG_Head_t h;
            ReadHead(slot, &h);
            if (h.commit != MSG_COMMIT || h.deleted != 0xFF)
                continue;
            if (h.type == MSG_TYPE_SESSION) {
                marker = shown != 0; /* never above the newest message */
                continue;
            }
            if (h.type != MSG_TYPE_MESSAGE)
                continue;
            for (uint8_t k = marker ? 2 : 1; k--;) {
                const bool isMarker = k == 1;
                if (row >= first && row - first < count) {
                    MSG_Row_t *r = &rows[row - first];
                    r->marker = isMarker;
                    r->slot = slot;
                    if (!isMarker) {
                        memcpy(r->cs, h.cs, 2);
                        r->len = h.len;
                        PY25Q16_ReadPhysical(SlotAddress(slot) + sizeof h, r->text, sizeof r->text);
                        PY25Q16_ReadPhysical(SlotAddress(slot) + MSG_NUMBER, &r->number, 1);
                    }
                }
                if (!isMarker && row == selected && ordinal)
                    *ordinal = shown + 1u;
                ++row;
            }
            marker = false;
            ++shown;
        }
    }
    if (messages)
        *messages = shown;
    return row;
}

NATIVE_CODE(messages) uint8_t MSG_ReadText(uint8_t slot, char *cs, char *text)
{
    MSG_Head_t h;
    ReadHead(slot, &h);
    memcpy(cs, h.cs, 2);
    const uint8_t len = h.len <= MSG_TEXT_MAX ? h.len : MSG_TEXT_MAX;
    PY25Q16_ReadPhysical(SlotAddress(slot) + sizeof h, text, len);
    text[len] = 0;
    return len;
}

NATIVE_CODE(messages) void MSG_Delete(uint8_t slot)
{
    const uint8_t deleted = 0;
    PY25Q16_ProgramRaw(SlotAddress(slot) + offsetof(MSG_Head_t, deleted), &deleted, 1);
}

NATIVE_CODE(messages) void MSG_Clear(void)
{
    PY25Q16_EraseRaw(MSG_BASE);
    PY25Q16_EraseRaw(MSG_BASE + MSG_SECTOR);
}
