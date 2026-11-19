/* Falcon POCSAG journal, in external flash at PLOG_BASE: PLOG_SECTORS 4 KiB
 * sectors used in turn, the oldest erased when the newest is full.
 *
 * A sector starts with a 12-byte header: "PLG1", the sector's sequence
 * number (one more for each sector started), the sequence number its first
 * record takes. Records follow, each one
 *   0  type: 'P' a page, 'S' a session marker, 0xFF not complete
 *   1  0xFF, 0x00 once deleted (a bit cleared: no erase)
 *   2  payload length, 16 bits, little endian (0xFFFF: free from here)
 *   4  record sequence number, 32 bits
 *   8  payload
 *  +len  the length again, 16 bits: the way back to the record before
 * The length is written first and the type last, so an interrupted record
 * is skipped but never breaks the chain. A record never crosses a sector.
 * Writes never use the sector cache: POCSAG Scan saves while it runs from
 * the overlay RAM, which is that cache. */
#include <stddef.h>
#include <string.h>

#include "app/pocsaglog.h"
#include "apps/app_overlay.h"
#include "driver/py25q16.h"

#define PLOG_MAGIC  0x31474C50u /* "PLG1" */
#define PLOG_HDR    12u
#define PLOG_FREE   0xFFFFu
#define PLOG_NO_SEC 0xFFu

typedef struct {
    uint8_t type, deleted;
    uint16_t len;
    uint32_t seq;
} PLOG_Head_t;

typedef struct {
    uint32_t magic, sseq, first;
} PLOG_Sector_t;

_Static_assert(sizeof(PLOG_Head_t) == 8 && sizeof(PLOG_Sector_t) == PLOG_HDR, "POCSAG journal layout");

/* The newest sector (PLOG_NO_SEC: the journal is empty), its sequence, its
 * free offset and newest record, the next record sequence, and the first
 * one written since power-on. */
static struct {
    bool ready, session;
    uint8_t sector;
    uint16_t end, last;
    uint32_t sseq, rseq, boot;
} pl;

static uint32_t Base(uint8_t sector)
{
    return PLOG_BASE + sector * PLOG_SECTOR;
}

static bool SectorHead(uint8_t sector, PLOG_Sector_t *s)
{
    PY25Q16_ReadPhysical(Base(sector), s, sizeof *s);
    return s->magic == PLOG_MAGIC;
}

/* The records of a sector, walked from its start: the free offset, and the
 * newest record (0: none). */
static uint16_t Walk(uint8_t sector, uint16_t *last, uint32_t *rseq)
{
    uint16_t off = PLOG_HDR;
    *last = 0;
    while (off + sizeof(PLOG_Head_t) <= PLOG_SECTOR) {
        PLOG_Head_t h;
        PY25Q16_ReadPhysical(Base(sector) + off, &h, sizeof h);
        if (h.len == PLOG_FREE || off + sizeof h + h.len + 2u > PLOG_SECTOR)
            break;
        *last = off;
        if (rseq)
            *rseq = h.seq + 1u;
        off += sizeof h + h.len + 2u;
    }
    return off;
}

static void Init(void)
{
    if (pl.ready)
        return;
    pl.ready = true;
    pl.sector = PLOG_NO_SEC;
    PLOG_Sector_t s;
    for (uint8_t i = 0; i < PLOG_SECTORS; i++)
        if (SectorHead(i, &s) && (pl.sector == PLOG_NO_SEC || (int32_t)(s.sseq - pl.sseq) > 0)) {
            pl.sector = i;
            pl.sseq = s.sseq;
            pl.rseq = s.first;
        }
    if (pl.sector != PLOG_NO_SEC)
        pl.end = Walk(pl.sector, &pl.last, &pl.rseq);
    pl.boot = pl.rseq;
}

static void StartSector(uint8_t sector)
{
    PY25Q16_EraseRaw(Base(sector));
    const PLOG_Sector_t s = {PLOG_MAGIC, ++pl.sseq, pl.rseq};
    PY25Q16_ProgramRaw(Base(sector), &s, sizeof s);
    pl.sector = sector;
    pl.end = PLOG_HDR;
    pl.last = 0;
}

static uint32_t Write(uint8_t type, const void *payload, unsigned len)
{
    if (len > PLOG_MAX)
        len = PLOG_MAX;
    const unsigned need = sizeof(PLOG_Head_t) + len + 2u;
    if (pl.sector == PLOG_NO_SEC)
        StartSector(0);
    else if (pl.end + need > PLOG_SECTOR)
        StartSector((uint8_t)((pl.sector + 1u) % PLOG_SECTORS));
    const uint32_t a = Base(pl.sector) + pl.end;
    const PLOG_Head_t h = {type, 0xFF, (uint16_t)len, pl.rseq};
    PY25Q16_ProgramRaw(a + 2u, &h.len, 6);
    if (len)
        PY25Q16_ProgramRaw(a + sizeof h, payload, len);
    PY25Q16_ProgramRaw(a + sizeof h + len, &h.len, 2);
    PY25Q16_ProgramRaw(a, &h.type, 1);
    pl.last = pl.end;
    pl.end += need;
    pl.rseq++;
    return a;
}

/* ---- resident: written and read while POCSAG Scan runs ---- */

void PLOG_SessionStart(void)
{
    pl.session = true;
}

/* App service: one page, its payload as POCSAG Scan builds it (PLOG_Page_t
 * then the text). The first page of an app session is preceded by a marker.
 * Blocks while a sector is erased, every 4 KiB of pages (up to ~0.3 s). */
uint32_t PLOG_Append(const void *payload, unsigned len)
{
    Init();
    if (pl.session) {
        pl.session = false;
        Write('S', NULL, 0);
    }
    return Write('P', payload, len);
}

int PLOG_Read(uint32_t address, void *buf, unsigned max)
{
    PLOG_Head_t h;
    if (address < PLOG_BASE || address >= Base(PLOG_SECTORS))
        return PLOG_NONE;
    PY25Q16_ReadPhysical(address, &h, sizeof h);
    if (h.len == PLOG_FREE)
        return PLOG_NONE;
    if (h.type == 'S')
        return PLOG_SESSION;
    if (h.type != 'P' || h.deleted != 0xFF || h.len > PLOG_MAX)
        return PLOG_SKIP;
    /* max = n | offset << 16: n bytes of the payload from offset (Falcon 4.0;
     * an offset of 0 is the call it always was) */
    const unsigned off = max >> 16;
    max &= 0xFFFFu;
    if (off < h.len)
        PY25Q16_ReadPhysical(address + sizeof h + off, buf, h.len - off < max ? h.len - off : max);
    return h.len;
}

/* The record before address (0: the newest), back through the sectors in
 * the order they were started; 0 past the oldest one. */
uint32_t PLOG_Prev(uint32_t address)
{
    Init();
    if (pl.sector == PLOG_NO_SEC)
        return 0;
    uint8_t sector = pl.sector;
    uint32_t sseq = pl.sseq;
    if (!address) {
        if (pl.last)
            return Base(sector) + pl.last;
    } else {
        if (address < PLOG_BASE || address >= Base(PLOG_SECTORS))
            return 0;
        sector = (uint8_t)((address - PLOG_BASE) / PLOG_SECTOR);
        const uint16_t off = (uint16_t)(address - Base(sector));
        if (off > PLOG_HDR) {
            uint16_t len;
            PY25Q16_ReadPhysical(address - 2u, &len, 2);
            return address - 2u - len - sizeof(PLOG_Head_t);
        }
        PLOG_Sector_t s;
        if (!SectorHead(sector, &s))
            return 0;
        sseq = s.sseq;
    }
    /* the newest record of the sector started before this one */
    for (uint8_t tries = 0; tries < PLOG_SECTORS - 1u; tries++) {
        sector = (uint8_t)((sector + PLOG_SECTORS - 1u) % PLOG_SECTORS);
        PLOG_Sector_t s;
        if (sector == pl.sector || !SectorHead(sector, &s) || s.sseq != --sseq)
            return 0;
        uint16_t last;
        Walk(sector, &last, NULL);
        if (last)
            return Base(sector) + last;
    }
    return 0;
}

/* ---- native module: the POCSAG page ---- */

NATIVE_CODE(messages) bool PLOG_SameBoot(uint32_t address)
{
    PLOG_Head_t h;
    PY25Q16_ReadPhysical(address, &h, sizeof h);
    return (int32_t)(h.seq - pl.boot) >= 0;
}

NATIVE_CODE(messages) void PLOG_Delete(uint32_t address)
{
    const uint8_t deleted = 0;
    PY25Q16_ProgramRaw(address + offsetof(PLOG_Head_t, deleted), &deleted, 1);
}

NATIVE_CODE(messages) void PLOG_Clear(void)
{
    for (uint8_t i = 0; i < PLOG_SECTORS; i++) {
        PLOG_Sector_t s;
        if (SectorHead(i, &s) || s.magic != 0xFFFFFFFFu)
            PY25Q16_EraseRaw(Base(i));
    }
    pl.sector = PLOG_NO_SEC;
    pl.end = pl.last = 0;
}
