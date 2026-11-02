/* Falcon external store reader. Reads use the SPI flash DMA path and every
 * check uses the hardware CRC unit; see store_layout.h for the layout. */
#include <string.h>

#include "store.h"
#include "driver/hwcrc.h"
#include "driver/py25q16.h"
#include "ui/multiboot.h"
#include "misc.h"

extern const uint8_t __fp_resources[], __fp_resources_end[];
extern uint8_t _eflash_used;

#define RUNTIME_BASE        0x08002800u
#define RESOURCE_RETRY_10MS 100u

void APP_ResourceRedraw(void);

static uint32_t runtimeCrc;
static bool runtimeCrcReady;
static bool missing;
static uint32_t resourceOffset; /* verified resources, 0 = not yet    */
static bool resourceFailed;
static uint32_t resourceRetryTick;

static uint32_t RuntimeSize(void)
{
    return (uint32_t)&_eflash_used - RUNTIME_BASE;
}

static bool Read(uint32_t offset, void *dst, uint32_t len)
{
    const uint8_t slot = MB_GetRunningSlot();
    if (slot >= STORE_SLOTS || offset > STORE_SIZE || len > STORE_SIZE - offset)
        return false;
    if (offset < STORE_ASSET_SIZE && offset + len > STORE_ASSET_SIZE)
        return false; /* entries never cross the zones   */
    return PY25Q16_ReadChecked(STORE_Physical(slot, offset), dst, len);
}

/* The store header must name exactly this runtime image. */
static bool HeaderValid(void)
{
    store_header_t h;
    if (!Read(STORE_HEADER_OFFSET, &h, sizeof h))
        return false;
    if (!runtimeCrcReady) {
        runtimeCrc = HWCRC_Compute((const void *)RUNTIME_BASE, RuntimeSize());
        runtimeCrcReady = true;
    }
    const bool valid = h.magic == STORE_MAGIC && h.runtime_size == RuntimeSize() &&
                       h.runtime_crc == runtimeCrc && h.count <= STORE_MAX_ENTRIES;
    missing = !valid;
    return valid;
}

static bool ReadEntry(uint8_t index, store_entry_t *entry)
{
    store_header_t h;
    if (!Read(STORE_HEADER_OFFSET, &h, sizeof h) || index >= h.count)
        return false;
    if (!Read(STORE_HEADER_OFFSET + sizeof h + index * sizeof *entry, entry, sizeof *entry))
        return false;
    return entry->size && !(entry->size & 3u) && entry->offset >= STORE_DATA_OFFSET &&
           entry->offset <= STORE_SIZE && entry->size <= STORE_SIZE - entry->offset;
}

void STORE_Check(void)
{
    (void)HeaderValid();
}

bool STORE_IsMissing(void)
{
    return missing;
}

bool STORE_LoadModule(uint8_t id, uint8_t *dst)
{
    store_entry_t e;
    if (id == STORE_ENTRY_RESOURCES || !HeaderValid() || !ReadEntry(id, &e) || e.size > STORE_MODULE_MAX)
        return false;
    memset(dst + e.size, 0, STORE_MODULE_MAX - e.size);
    return Read(e.offset, dst, e.size) && HWCRC_Compute(dst, e.size) == e.crc;
}

/* Verify the resources entry once, in small chunks (no shared buffer). */
static bool ResourcesVerify(uint32_t *offset)
{
    store_entry_t e;
    const uint32_t size = (uint32_t)(__fp_resources_end - __fp_resources);
    if (!HeaderValid() || !ReadEntry(STORE_ENTRY_RESOURCES, &e) || e.size < size)
        return false;
    uint32_t chunk[16];
    HWCRC_Begin();
    for (uint32_t done = 0; done < e.size; done += sizeof chunk) {
        const uint32_t n = e.size - done < sizeof chunk ? e.size - done : sizeof chunk;
        if (!Read(e.offset + done, chunk, n))
            return false;
        HWCRC_Feed(chunk, n);
    }
    if (HWCRC_Result() != e.crc)
        return false;
    *offset = e.offset;
    return true;
}

void FP_Read(const void *src, void *dst, uint32_t len)
{
    const uint32_t a = (uint32_t)src;
    const uint32_t start = (uint32_t)__fp_resources, end = (uint32_t)__fp_resources_end;

    if (a < start || a >= end) { /* ordinary memory                 */
        memcpy(dst, src, len);
        return;
    }
    if (len > end - a) {
        memset(dst, 0, len);
        return;
    }
    if (!resourceOffset && (!resourceFailed || (int32_t)(gGlobalSysTickCounter - resourceRetryTick) >= 0)) {
        if (ResourcesVerify(&resourceOffset)) {
            const bool recovered = resourceFailed;
            resourceFailed = false;
            if (recovered)
                APP_ResourceRedraw();
        } else {
            resourceOffset = 0;
            resourceFailed = true;
            resourceRetryTick = gGlobalSysTickCounter + RESOURCE_RETRY_10MS;
        }
    }
    if (!resourceOffset || !Read(resourceOffset + (a - start), dst, len)) {
        if (resourceOffset) { /* read error: verify again later  */
            resourceOffset = 0;
            resourceFailed = true;
            resourceRetryTick = gGlobalSysTickCounter + RESOURCE_RETRY_10MS;
        }
        memset(dst, 0, len);
    }
}

uint8_t FP_Byte(const void *src)
{
    uint8_t v;
    FP_Read(src, &v, 1);
    return v;
}

void FP_CopyString(char *dst, const char *src, uint32_t cap)
{
    if (!cap)
        return;
    FP_Read(src, dst, cap);
    dst[cap - 1] = 0;
}
