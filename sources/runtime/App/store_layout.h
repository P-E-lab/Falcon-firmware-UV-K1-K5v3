/* Falcon external store: layout shared by the runtime, the installer and
 * tools/build_payload.py (which reads the #define values below).
 *
 * The store holds what Falcon keeps outside the internal flash: the native
 * code modules (executed one at a time from the 4 KiB overlay RAM) and the
 * read-only resources (fonts, menu texts). Each firmware slot 0..4 owns one
 * store, made of two physical zones seen as one 48 KiB virtual space:
 *
 *   virtual 0x0000..0x3FFF -> the F4HWN v7 asset zone of the slot
 *                             0x1CA000 + slot * 16 KiB (standard v7 location)
 *   virtual 0x4000..0xBFFF -> a 32 KiB bank at the end of the v7 Apps reserve
 *                             0x1A2000 + slot * 32 KiB (Apps slots 80..99)
 *
 * Virtual layout:
 *   0x0000  FWA1 header (32 B), v7 asset-pack compatible; its payload is the
 *           asset-zone part of the store, up to STORE_JOURNAL
 *   0x0020  store header + directory (entry 0 = resources, 1.. = modules)
 *   0x0100  entries, 4-byte aligned, never crossing a zone boundary
 *   0x3F00  installer journal (outside the FWA1 payload)
 *
 * Entry CRCs and the runtime binding use the PY32 hardware CRC unit
 * (CRC-32/MPEG-2 over little-endian 32-bit words), so loading costs no CPU. */
#ifndef STORE_LAYOUT_H
#define STORE_LAYOUT_H

#include <stdint.h>

#define STORE_ASSET_BASE 0x1CA000u /* v7 asset zone of slot 0          */
#define STORE_ASSET_SIZE 0x4000u   /* 16 KiB per firmware slot         */
#define STORE_BANK_BASE  0x1A2000u /* Apps reserve, slots 80..99       */
#define STORE_BANK_SIZE  0x8000u   /* 32 KiB per firmware slot         */
#define STORE_SIZE       (STORE_ASSET_SIZE + STORE_BANK_SIZE)
#define STORE_SLOTS      5u

#define STORE_FWA1_MAGIC      0x31415746u /* "FWA1"                           */
#define STORE_MAGIC           0x32545346u /* "FST2"                           */
#define STORE_HEADER_OFFSET   0x0020u
#define STORE_DATA_OFFSET     0x0100u
#define STORE_JOURNAL         0x3F00u     /* first byte after the FWA1 payload */
#define INSTALL_JOURNAL_MAGIC 0x314A4946u /* "FIJ1", see the installer        */
#define STORE_MAX_ENTRIES     16u
#define STORE_MODULE_MAX      0x1000u /* one module fits the overlay RAM  */

#define STORE_ENTRY_RESOURCES 0u /* entry 0; modules use their id    */

typedef struct {
    uint32_t magic;         /* STORE_FWA1_MAGIC                            */
    uint16_t version;       /* 1                                           */
    uint16_t header_size;   /* 32                                          */
    uint32_t payload_size;  /* bytes after this header in the asset zone   */
    uint32_t payload_crc32; /* zlib CRC-32 of that payload                 */
    uint8_t build_id[16];   /* "FALCON", runtime size and runtime CRC      */
} store_fwa1_t;

typedef struct {
    uint32_t magic;        /* STORE_MAGIC                                 */
    uint32_t runtime_size; /* bytes of the internal-flash image it serves */
    uint32_t runtime_crc;  /* hardware CRC of that image                  */
    uint32_t count;        /* directory entries                           */
} store_header_t;

typedef struct {
    uint32_t offset; /* virtual offset                              */
    uint32_t size;   /* bytes, multiple of 4                        */
    uint32_t crc;    /* hardware CRC of the entry                   */
} store_entry_t;

/* Physical address of a virtual store offset for a firmware slot. */
static inline uint32_t STORE_Physical(uint8_t slot, uint32_t offset)
{
    return offset < STORE_ASSET_SIZE
               ? STORE_ASSET_BASE + (uint32_t)slot * STORE_ASSET_SIZE + offset
               : STORE_BANK_BASE + (uint32_t)slot * STORE_BANK_SIZE + (offset - STORE_ASSET_SIZE);
}

#endif
