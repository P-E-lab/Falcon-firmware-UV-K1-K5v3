/* Falcon temporary first-boot installer.
 *
 * Written to a firmware slot (UV Studio "Firmware Slots") or to Main (DFU),
 * it runs once from the internal flash and:
 *   1. finds the firmware slot it was started from;
 *   2. refuses to touch a store bank holding Apps of another firmware;
 *   3. writes the Falcon runtime into that slot and the external store into
 *      the slot's zones (store_layout.h), decompressing its embedded payload;
 *   4. verifies everything, then lets multiboot restore the slot into the
 *      internal flash. The installer is gone after this restore.
 *
 * A journal in the slot's asset zone lets a reset during step 3 or 4 resume
 * into the same slot. Configuration, calibration, Main (when installing into
 * a user slot) and the 64 Apps are never touched. */
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "driver/gpio.h"
#include "driver/mb_flash.h"
#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "driver/systick.h"
#include "apps/app_overlay.h"
#include "pack_inflate.h"
#include "py32f0xx.h"
#include "store_layout.h"

/* ------------------------------------------------------------------------ */
/* Screen: title, progress gauge and one status line, with a tiny font.      */

static volatile uint8_t install_stage;
static uint8_t install_percent;

static const char install_chars[] = " !.0123456789ABCDEFLNOPSUceghinoprtuvw";
static const uint8_t install_font[][6] = {
    {0, 0, 0, 0, 0, 0},        {0, 0, 94, 0, 0, 0},       {0, 0, 96, 96, 0, 0},
    {62, 65, 65, 65, 65, 62},  {0, 64, 66, 127, 64, 64},  {98, 81, 81, 73, 73, 70},
    {34, 65, 73, 73, 73, 54},  {24, 20, 18, 17, 127, 16}, {39, 69, 69, 69, 69, 57},
    {62, 73, 73, 73, 73, 50},  {1, 1, 113, 9, 5, 3},      {54, 73, 73, 73, 73, 54},
    {70, 73, 73, 73, 41, 30},  {126, 9, 9, 9, 9, 126},    {127, 73, 73, 73, 73, 54},
    {62, 65, 65, 65, 65, 34},  {127, 65, 65, 65, 65, 62}, {127, 73, 73, 73, 73, 65},
    {127, 9, 9, 9, 9, 1},      {127, 64, 64, 64, 64, 64}, {127, 2, 4, 8, 16, 127},
    {62, 65, 65, 65, 65, 62},  {127, 17, 17, 17, 17, 14}, {38, 73, 73, 73, 73, 50},
    {63, 64, 64, 64, 64, 63},  {60, 66, 66, 66, 36, 0},   {126, 74, 74, 74, 66, 0},
    {60, 66, 82, 82, 52, 0},   {126, 8, 8, 8, 126, 0},    {0, 0, 66, 126, 66, 0},
    {126, 4, 8, 16, 126, 0},   {60, 66, 66, 66, 60, 0},   {126, 18, 18, 18, 12, 0},
    {126, 18, 18, 18, 108, 0}, {2, 2, 126, 2, 2, 0},      {62, 64, 64, 64, 62, 0},
    {14, 48, 64, 48, 14, 0},   {62, 64, 32, 64, 62, 0},
};

static const uint8_t *install_glyph(char c)
{
    const char *p = strchr(install_chars, c);
    return p ? install_font[p - install_chars] : NULL;
}

static void install_pixel(unsigned x, unsigned y)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT)
        return;
    if (y < 8)
        gStatusLine[x] |= 1u << y;
    else
        gFrameBuffer[y / 8 - 1][x] |= 1u << (y & 7);
}

/* Columns [*left, *right) of a glyph holding ink. */
static void install_ink(const uint8_t *g, unsigned *left, unsigned *right)
{
    unsigned l = 0, r = 6;
    while (l < r && !g[l])
        ++l;
    while (r > l && !g[r - 1])
        --r;
    *left = l;
    *right = r;
}

static unsigned install_advance(char c)
{
    const uint8_t *g = install_glyph(c);
    if (c == ' ' || !g)
        return 4;
    unsigned l, r;
    install_ink(g, &l, &r);
    return r - l + 1;
}

static void install_text(const char *s, unsigned y)
{
    unsigned width = 0;
    for (const char *p = s; *p; ++p)
        width += install_advance(*p);
    if (width)
        --width;
    unsigned x = (LCD_WIDTH - width) / 2;
    for (; *s; ++s) {
        const uint8_t *g = install_glyph(*s);
        if (*s == ' ' || !g) {
            x += 4;
            continue;
        }
        unsigned l, r;
        install_ink(g, &l, &r);
        for (unsigned c = l; c < r; ++c)
            for (unsigned row = 0; row < 7; ++row)
                if (g[c] & (1u << row))
                    install_pixel(x + c - l, y + row);
        x += r - l + 1;
    }
}

static void install_render(uint8_t error)
{
    memset(gFrameBuffer, 0, sizeof gFrameBuffer);
    memset(gStatusLine, 0, sizeof gStatusLine);
    install_text("FALCON v" VERSION_STRING_2, 16);

    /* Gauge: rounded ends, filled part 0x2d, empty part 0x21. */
    const unsigned left = 10, right = 117, cols = right - left - 3;
    const unsigned filled = install_percent * cols / 100;
    for (unsigned x = left; x <= right; ++x) {
        const uint8_t bits = x == left || x == right           ? 0x0c
                             : x == left + 1 || x == right - 1 ? 0x12
                             : x - left - 2 < filled           ? 0x2d
                                                               : 0x21;
        for (unsigned y = 0; y < 6; ++y)
            if (bits & (1u << y))
                install_pixel(x, 29 + y);
    }

    if (error) {
        static const char hex[] = "0123456789ABCDEF";
        char msg[] = "S 00 E 00";
        msg[2] = hex[install_stage >> 4];
        msg[3] = hex[install_stage & 15];
        msg[7] = hex[error >> 4];
        msg[8] = hex[error & 15];
        install_text(msg, 41);
    } else {
        install_text(install_stage >= 8 ? "Punch it !" : "Powering Up", 41);
    }
    ST7565_BlitFullScreen();
}

static void install_update(uint8_t percent)
{
    if (percent >= install_percent + 2) {
        install_percent = percent;
        install_render(0);
    }
}

static void progress(uint8_t stage, uint8_t error)
{
    static const uint8_t percent_at_stage[] = {0, 0, 1, 2, 3, 5, 95, 97, 100, 100};
    install_stage = stage;
    if (!error && stage < sizeof percent_at_stage)
        install_percent = percent_at_stage[stage];
    install_render(error);
}

/* ------------------------------------------------------------------------ */
/* Installation.                                                            */

#define INS_ERR_APPS 0x10u /* the store bank holds Apps of another firmware */
#define JOURNAL_SIZE 20u

typedef struct {
    uint32_t magic;         /* INSTALL_JOURNAL_MAGIC                      */
    uint32_t installer_crc; /* this installer image                       */
    uint32_t installer_size;
    uint32_t slot;
    uint32_t crc; /* of the four words above                    */
} journal_t;

extern uint8_t _eflash_used;
extern const uint32_t install_meta[8];
extern const uint8_t install_payload[];

enum {
    META_MAGIC,
    META_FW_SIZE,
    META_FW_CRC,
    META_STORE_SIZE,
    META_STORE_CRC,
    META_PACKED_SIZE,
    META_RAW_CRC,
    META_PACKED_CRC
};
#define INSTALL_MAGIC 0x31534946u /* "FIS1", see tools/build_payload.py */

static fp_inflate decoder;
static uint8_t page[256];
static uint8_t store_head[STORE_DATA_OFFSET]; /* written last */
static uint32_t ip, op, page_start, page_len;
static uint32_t fwlen, storelen, slot;
/* The payload holds Thumb BL offsets as absolute targets (thumb_filter in
 * tools/build_payload.py). Inflate produces that filtered stream up to `op`;
 * bytes from `wp` on wait in `pend` until their BL, if any, is complete. */
static uint8_t pend[4];
static uint32_t wp;
static uint8_t cache[16]; /* raw bytes read back from flash */
static uint32_t cache_pos, cache_len;

static uint32_t crc_update(uint32_t c, const uint8_t *p, uint32_t n)
{
    while (n--) {
        c ^= *p++;
        for (unsigned j = 0; j < 8; j++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    }
    return c;
}

static uint32_t slot_image(void)
{
    return MB_SLOT0_EXT_BASE + slot * MB_SLOT_STRIDE + MB_SLOT_IMG_OFFSET;
}

/* Bytes of the store never written from the payload: the journal page. */
static bool store_skipped(uint32_t offset)
{
    return offset >= STORE_JOURNAL && offset < STORE_ASSET_SIZE;
}

/* Physical address of payload position `pos` (runtime image, then store). */
static uint32_t address(uint32_t pos)
{
    return pos < fwlen ? slot_image() + pos : STORE_Physical((uint8_t)slot, pos - fwlen);
}

static bool in_store_head(uint32_t pos)
{
    return pos >= fwlen && pos < fwlen + STORE_DATA_OFFSET;
}

/* Payload bytes from `pos` that map to one contiguous physical run. */
static uint32_t run_length(uint32_t pos, uint32_t n)
{
    uint32_t end;
    if (pos < fwlen) {
        end = fwlen;
    } else {
        const uint32_t offset = pos - fwlen;
        end = offset < STORE_DATA_OFFSET  ? STORE_DATA_OFFSET
              : offset < STORE_JOURNAL    ? STORE_JOURNAL
              : offset < STORE_ASSET_SIZE ? STORE_ASSET_SIZE
                                          : STORE_SIZE;
        end += fwlen;
    }
    return end - pos < n ? end - pos : n;
}

/* zlib CRC state over `n` payload bytes from `pos`, read back from flash. */
static uint32_t crc_at(uint32_t pos, uint32_t n, uint32_t c)
{
    uint8_t buf[128];
    while (n) {
        const uint32_t k = run_length(pos, n < sizeof buf ? n : sizeof buf);
        if (in_store_head(pos))
            memcpy(buf, &store_head[pos - fwlen], k);
        else if (pos >= fwlen && store_skipped(pos - fwlen))
            memset(buf, 0xFF, k);
        else if (INS_Read(address(pos), buf, k))
            return 0;
        c = crc_update(c, buf, k);
        pos += k;
        n -= k;
    }
    return c;
}

static int input(void *unused)
{
    (void)unused;
    return ip < install_meta[META_PACKED_SIZE] ? install_payload[ip++] : -1;
}

static bool flush_page(void)
{
    if (!page_len)
        return true;
    const bool skip = page_start >= fwlen && store_skipped(page_start - fwlen);
    const bool ok = skip || !INS_Write(address(page_start), page, page_len);
    page_len = 0;
    return ok;
}

/* Writes the raw byte of position `wp`. */
static bool emit(uint8_t b)
{
    const uint32_t total = fwlen + storelen;
    if (in_store_head(wp)) {
        store_head[wp - fwlen] = b;
    } else {
        if (!page_len)
            page_start = wp;
        page[page_len++] = b;
        /* A page ends at a physical 256-byte boundary or at a part end. */
        if ((address(wp) & 255u) == 255u || wp + 1 == fwlen || wp + 1 == total) {
            if (!flush_page())
                return false;
            install_update(5u + (wp + 1u) * 90u / total);
        }
    }
    wp++;
    return true;
}

/* Raw byte of an already written position. */
static bool raw_at(uint32_t at, uint8_t *b)
{
    if (in_store_head(at)) {
        *b = store_head[at - fwlen];
    } else if (page_len && at >= page_start) {
        *b = page[at - page_start];
    } else if (at >= fwlen && store_skipped(at - fwlen)) {
        *b = 0xFF;
    } else {
        if (at - cache_pos >= cache_len) {
            /* Only flushed bytes, which never change, are cached. */
            const uint32_t limit = (page_len ? page_start : wp) - at;
            cache_pos = at;
            cache_len = run_length(at, limit < sizeof cache ? limit : sizeof cache);
            if (INS_Read(address(at), cache, cache_len))
                return false;
        }
        *b = cache[at - cache_pos];
    }
    return true;
}

static bool bl_start(uint8_t second, uint8_t fourth)
{
    return (second & 0xF8u) == 0xF0u && (fourth & 0xF8u) == 0xF8u;
}

/* BL at even position `pos`: absolute target <-> relative offset. */
static void bl_convert(uint8_t *g, uint32_t pos, bool to_absolute)
{
    uint32_t v = (g[1] & 7u) << 19 | (uint32_t)g[0] << 11 | (g[3] & 7u) << 8 | g[2];
    v = to_absolute ? v + ((pos + 4u) >> 1) : v - ((pos + 4u) >> 1);
    g[0] = (uint8_t)(v >> 11);
    g[1] = (uint8_t)(0xF0u | (v >> 19 & 7u));
    g[2] = (uint8_t)v;
    g[3] = (uint8_t)(0xF8u | (v >> 8 & 7u));
}

/* Filtered byte of position `at` < op, as inflate wrote it. */
static bool filtered_at(uint32_t at, uint8_t *b)
{
    if (at >= wp) {
        *b = pend[at & 3u];
        return true;
    }
    /* The BL holding `at` starts at `at` & ~1 or 2 bytes before; a converted
     * BL was written whole, so it ends before wp. */
    const uint32_t even = at & ~1u;
    for (uint32_t i = 0; i < 2u && 2u * i <= even; i++) {
        const uint32_t g = even - 2u * i;
        uint8_t q[4];
        if (g + 3u >= wp)
            continue;
        for (unsigned k = 0; k < 4; k++)
            if (!raw_at(g + k, &q[k]))
                return false;
        if (bl_start(q[1], q[3])) {
            bl_convert(q, g, true);
            *b = q[at - g];
            return true;
        }
    }
    return raw_at(at, b);
}

/* Writes the pending bytes whose BL, if any, is complete (all at `end`). */
static bool drain(bool end)
{
    while (wp < op) {
        const uint32_t n = op - wp;
        if (!(wp & 1u) && !end && n < 2u)
            return true; /* its second byte tells whether a BL starts here */
        if (!(wp & 1u) && n >= 2u && (pend[(wp + 1u) & 3u] & 0xF8u) == 0xF0u) {
            if (n < 4u) {
                if (!end)
                    return true;
            } else if ((pend[(wp + 3u) & 3u] & 0xF8u) == 0xF8u) {
                uint8_t g[4];
                for (unsigned k = 0; k < 4; k++)
                    g[k] = pend[(wp + k) & 3u];
                bl_convert(g, wp, false);
                for (unsigned k = 0; k < 4; k++)
                    if (!emit(g[k]))
                        return false;
                continue;
            }
        }
        if (!emit(pend[wp & 3u]))
            return false;
    }
    return true;
}

/* Inflate output: literal (distance 0) or copy from `distance` bytes back. */
static bool output(void *unused, unsigned distance, unsigned count, uint8_t literal)
{
    (void)unused;
    const uint32_t total = fwlen + storelen;
    if (distance > op || count > total - op)
        return false;
    while (count--) {
        uint8_t b = literal;
        if (distance && !filtered_at(op - distance, &b))
            return false;
        pend[op++ & 3u] = b;
        if (!drain(false))
            return false;
    }
    return true;
}

static uint32_t journal_crc(const journal_t *j)
{
    return MB_Crc32Bytes((const uint8_t *)j, JOURNAL_SIZE - 4);
}

static bool journal_read(uint32_t s, journal_t *j, uint32_t own_crc, uint32_t own_size)
{
    return !INS_Read(STORE_Physical((uint8_t)s, STORE_JOURNAL), j, sizeof *j) &&
           j->magic == INSTALL_JOURNAL_MAGIC && j->installer_crc == own_crc &&
           j->installer_size == own_size && j->slot == s && journal_crc(j) == j->crc;
}

/* Protection: never erase a store bank where another firmware put Apps. */
static bool bank_holds_apps(void)
{
    const uint32_t bank = STORE_Physical((uint8_t)slot, STORE_ASSET_SIZE);
    for (uint32_t off = 0; off < STORE_BANK_SIZE; off += APP_SLOT_STRIDE) {
        uint32_t magic;
        if (INS_Read(bank + off, &magic, sizeof magic) || magic == APP_MAGIC)
            return true;
    }
    return false;
}

/* Returns only on error (or under emulation); success resets the radio. */
__attribute__((noinline)) uint8_t INS_Run(void)
{
    progress(2, 0);
    if (install_meta[META_MAGIC] != INSTALL_MAGIC)
        return MB_ERR_MAGIC;
    fwlen = install_meta[META_FW_SIZE];
    storelen = install_meta[META_STORE_SIZE];
    if (!fwlen || fwlen > MB_INT_APP_SIZE || storelen < STORE_DATA_OFFSET || storelen > STORE_SIZE)
        return MB_ERR_SIZE;
    /* A damaged installer image is refused before anything is erased. */
    if (~crc_update(~0u, install_payload, install_meta[META_PACKED_SIZE]) != install_meta[META_PACKED_CRC])
        return MB_ERR_CRC;

    /* 1. Which slot are we installing? */
    const uint32_t own_size = (uint32_t)&_eflash_used - MB_INT_APP_BASE;
    const uint32_t own_crc = MB_Crc32Bytes((const uint8_t *)MB_INT_APP_BASE, own_size);
    progress(3, 0);
    mb_state_t state;
    journal_t j;
    const mb_mark_status_t ms = MB_ReadActiveState(&state);
    bool from_journal = false;
    if (ms == MB_MARK_VALID && MB_InternalMatchesState(&state)) {
        slot = state.firmware_slot;
    } else {
        slot = 0;
        for (uint32_t s = 0; s < STORE_SLOTS; s++)
            if (journal_read(s, &j, own_crc, own_size)) {
                slot = s;
                from_journal = true;
                break;
            }
        if (!from_journal && ms != MB_MARK_MISSING && ms != MB_MARK_VALID && ms != MB_MARK_LEGACY)
            return MB_ERR_SPI;
    }
    if (slot >= MB_SLOT_COUNT)
        return MB_ERR_SLOT;

    /* 2. Protection of other firmwares' Apps. */
    progress(4, 0);
    if (bank_holds_apps())
        return INS_ERR_APPS;

    /* 3. Journal first, then erase and decompress. */
    const uint32_t asset = STORE_Physical((uint8_t)slot, 0);
    if (INS_Erase(asset, STORE_ASSET_SIZE))
        return MB_ERR_SPI;
    j = (journal_t){INSTALL_JOURNAL_MAGIC, own_crc, own_size, slot, 0};
    j.crc = journal_crc(&j);
    if (INS_Write(asset + STORE_JOURNAL, &j, sizeof j) ||
        INS_Erase(STORE_Physical((uint8_t)slot, STORE_ASSET_SIZE), STORE_BANK_SIZE) ||
        INS_Erase(MB_SLOT0_EXT_BASE + slot * MB_SLOT_STRIDE, MB_SLOT_STRIDE))
        return MB_ERR_SPI;

    progress(5, 0);
    ip = op = wp = page_len = cache_len = 0;
    memset(&decoder, 0, sizeof decoder);
    decoder.input = input;
    decoder.output = output;
    if (!FP_Inflate(&decoder) || !drain(true) || op != fwlen + storelen || wp != op ||
        ip != install_meta[META_PACKED_SIZE])
        return MB_ERR_CRC;

    /* 4. Verify, then commit the store header and the slot header. */
    progress(6, 0);
    const uint32_t fw_state = crc_at(0, fwlen, ~0u);
    if (~fw_state != install_meta[META_FW_CRC] ||
        ~crc_at(fwlen, storelen, fw_state) != install_meta[META_RAW_CRC] ||
        ~crc_at(fwlen, storelen, ~0u) != install_meta[META_STORE_CRC])
        return MB_ERR_CRC;
    if (INS_Write(asset, store_head, sizeof store_head))
        return MB_ERR_SPI;

    mb_slot_header_t h;
    memset(&h, 0, sizeof h);
    h.magic = MB_SLOT_MAGIC;
    h.hdr_version = MB_HDR_VERSION;
    h.flags = MB_FLAG_COMMITTED;
    h.image_size = fwlen;
    h.image_crc32 = install_meta[META_FW_CRC];
    _Static_assert(sizeof(INSTALL_EDITION) <= sizeof h.name, "slot name");
    memcpy(h.name, INSTALL_EDITION, sizeof(INSTALL_EDITION) - 1);
    memcpy(h.fw_version, "v" VERSION_STRING_2, sizeof("v" VERSION_STRING_2));
    if (INS_Write(MB_SLOT0_EXT_BASE + slot * MB_SLOT_STRIDE, &h, sizeof h))
        return MB_ERR_SPI;
    if (MB_ValidateSlot((uint8_t)slot, &h, NULL) != MB_OK || h.image_size != fwlen ||
        h.image_crc32 != install_meta[META_FW_CRC])
        return MB_ERR_CRC;

    progress(8, 0);
    return MB_RestoreSlot((uint8_t)slot, NULL);
}

__attribute__((noinline)) void Main(void)
{
    BOARD_GPIO_Init();
    GPIOF->BSRR = 1u << 8;
    GPIOA->BRR = 1u << 8;
    SYSTICK_Init();
    ST7565_Init();
    progress(1, 0);
    PY25Q16_Init();
    const uint8_t status = INS_Run();
    progress(install_stage, status);
    for (;;) {
        /* After an error, PTT restores Main (slot 0). */
        if (GPIO_IsPttPressed())
            progress(9, MB_RestoreSlot(0, NULL));
        __WFI();
    }
}

void _putchar(char c)
{
    (void)c;
}

/* No native module ever runs in the temporary installer. */
void APP_InvalidateNative(void) {}
bool APP_NativeActive(void)
{
    return false;
}
void APP_NativeResume(void) {}
