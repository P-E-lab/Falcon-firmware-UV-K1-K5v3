/* Copyright 2025 muzkr
 * https://github.com/muzkr
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include <string.h>
#include "apps/app_overlay.h"

#include "driver/py25q16.h"
#include "driver/spi_guard.h"
#include "driver/gpio.h"
#include "py32f071_ll_bus.h"
#include "py32f071_ll_system.h"
#include "py32f071_ll_spi.h"
#include "py32f071_ll_dma.h"
#include "driver/system.h"
#include "driver/systick.h"
#include "external/printf/printf.h"
#include "misc.h"


// #define DEBUG

#define SPIx       SPI2
#define CHANNEL_RD LL_DMA_CHANNEL_4
#define CHANNEL_WR LL_DMA_CHANNEL_5

#define CS_PIN GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_3)

#define PAGE_SIZE 0x100

static uint32_t SectorCacheAddr = 0x1000000;
#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT_OVERLAY
/* The restore-only RAM stub is copied over this cache immediately before it
 * erases internal flash. A reset always follows, so the cache is never needed
 * again after the overlay becomes active. */
static uint8_t SectorCache[SECTOR_SIZE] __attribute__((section(".bss.mb_workspace"), aligned(4), used));
#else
static uint8_t SectorCache[SECTOR_SIZE];
#endif
uint8_t *PY25Q16_OverlayBuffer(void)
{
    return SectorCache;
}

static uint8_t BlackHole[4] __attribute__((aligned(4)));
static volatile bool TC_Flag;
static bool flash_error, flash_error_pending;
static bool writeBatch, batchDirty, batchErase;
static uint16_t batchFirst, batchLast;
static void FlashFail(void)
{
    flash_error = flash_error_pending = true;
}
bool PY25Q16_IsHealthy(void)
{
    return !flash_error;
}
bool PY25Q16_TakeError(void)
{
    bool error = flash_error_pending;
    flash_error_pending = false;
    return error;
}


#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
/* Active settings-bank base (see py25q16.h). 0 = bank 0 / historical
 * config region, i.e. an identity mapping. */
static uint32_t BankBase = 0;

void PY25Q16_SetBankBase(uint32_t Base)
{
    BankBase = Base;
}

/* Redirect config-region accesses (addr < boundary) into the active bank.
 * Calibration/logo/slots/marker (addr >= boundary) are returned unchanged.
 * BankBase is sector-aligned, so alignment done by callers is preserved. */
static inline uint32_t BankMap(uint32_t Address)
{
    return (Address < PY25Q16_BANK_SHARED_FROM) ? (Address + BankBase) : Address;
}
#else
static inline uint32_t BankMap(uint32_t Address)
{
    return Address;
}
#endif

static inline void CS_Assert()
{
    GPIO_ResetOutputPin(CS_PIN);
}

static inline void CS_Release()
{
    GPIO_SetOutputPin(CS_PIN);
}

static void SPI_Init()
{
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_SPI2);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
    LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);

    do {
        // SCK: PA0
        // MOSI: PA1
        // MISO: PA2

        LL_GPIO_InitTypeDef InitStruct;
        LL_GPIO_StructInit(&InitStruct);
        InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
        InitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
        InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
        InitStruct.Pull = LL_GPIO_PULL_UP;

        InitStruct.Pin = LL_GPIO_PIN_0;
        InitStruct.Alternate = LL_GPIO_AF8_SPI2;
        LL_GPIO_Init(GPIOA, &InitStruct);

        InitStruct.Pin = LL_GPIO_PIN_1 | LL_GPIO_PIN_2;
        InitStruct.Alternate = LL_GPIO_AF9_SPI2;
        LL_GPIO_Init(GPIOA, &InitStruct);

    } while (0);

    LL_SYSCFG_SetDMARemap(DMA1, CHANNEL_RD, LL_SYSCFG_DMA_MAP_SPI2_RD);
    LL_SYSCFG_SetDMARemap(DMA1, CHANNEL_WR, LL_SYSCFG_DMA_MAP_SPI2_WR);

    NVIC_SetPriority(DMA1_Channel4_5_6_7_IRQn, 1);
    NVIC_EnableIRQ(DMA1_Channel4_5_6_7_IRQn);

    LL_SPI_InitTypeDef InitStruct;
    LL_SPI_StructInit(&InitStruct);
    InitStruct.Mode = LL_SPI_MODE_MASTER;
    InitStruct.TransferDirection = LL_SPI_FULL_DUPLEX;
    InitStruct.ClockPhase = LL_SPI_PHASE_2EDGE;
    InitStruct.ClockPolarity = LL_SPI_POLARITY_HIGH;
    InitStruct.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV2;
    InitStruct.BitOrder = LL_SPI_MSB_FIRST;
    InitStruct.NSS = LL_SPI_NSS_SOFT;
    InitStruct.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE;
    LL_SPI_Init(SPIx, &InitStruct);

    LL_SPI_Enable(SPIx);
}

static void SPI_TransferBuf(const uint8_t *Buf, uint32_t Size, bool read)
{
    if (!Size || flash_error)
        return;
    LL_SPI_Disable(SPIx);
    LL_DMA_DisableChannel(DMA1, CHANNEL_RD);
    LL_DMA_DisableChannel(DMA1, CHANNEL_WR);

    LL_DMA_ClearFlag_GI4(DMA1);

    LL_DMA_ConfigTransfer(DMA1, CHANNEL_RD,                                                  //
                          LL_DMA_DIRECTION_PERIPH_TO_MEMORY                                  //
                              | LL_DMA_MODE_NORMAL                                           //
                              | LL_DMA_PERIPH_NOINCREMENT                                    //
                              | (read ? LL_DMA_MEMORY_INCREMENT : LL_DMA_MEMORY_NOINCREMENT) //
                              | LL_DMA_PDATAALIGN_BYTE                                       //
                              | LL_DMA_MDATAALIGN_BYTE                                       //
                              | (read ? LL_DMA_PRIORITY_MEDIUM : LL_DMA_PRIORITY_LOW)        //
    );

    LL_DMA_ConfigTransfer(DMA1, CHANNEL_WR,                                                  //
                          LL_DMA_DIRECTION_MEMORY_TO_PERIPH                                  //
                              | LL_DMA_MODE_NORMAL                                           //
                              | LL_DMA_PERIPH_NOINCREMENT                                    //
                              | (read ? LL_DMA_MEMORY_NOINCREMENT : LL_DMA_MEMORY_INCREMENT) //
                              | LL_DMA_PDATAALIGN_BYTE                                       //
                              | LL_DMA_MDATAALIGN_BYTE                                       //
                              | (read ? LL_DMA_PRIORITY_MEDIUM : LL_DMA_PRIORITY_LOW)        //
    );

    LL_DMA_SetMemoryAddress(DMA1, CHANNEL_RD, (uint32_t)(read ? Buf : BlackHole));
    LL_DMA_SetPeriphAddress(DMA1, CHANNEL_RD, LL_SPI_DMA_GetRegAddr(SPIx));
    LL_DMA_SetDataLength(DMA1, CHANNEL_RD, Size);

    LL_DMA_SetMemoryAddress(DMA1, CHANNEL_WR, (uint32_t)(read ? BlackHole : Buf));
    LL_DMA_SetPeriphAddress(DMA1, CHANNEL_WR, LL_SPI_DMA_GetRegAddr(SPIx));
    LL_DMA_SetDataLength(DMA1, CHANNEL_WR, Size);

    TC_Flag = false;
    LL_DMA_EnableIT_TC(DMA1, CHANNEL_RD);
    LL_DMA_EnableChannel(DMA1, CHANNEL_RD);
    LL_DMA_EnableChannel(DMA1, CHANNEL_WR);

    LL_SPI_EnableDMAReq_RX(SPIx);
    LL_SPI_Enable(SPIx);
    LL_SPI_EnableDMAReq_TX(SPIx);

    uint32_t remaining = 40000u + Size * 32u;
    while (!TC_Flag && !LL_DMA_IsActiveFlag_TC4(DMA1))
        if (!--remaining) {
            FlashFail();
            break;
        }
    LL_DMA_DisableIT_TC(DMA1, CHANNEL_RD);
    LL_SPI_DisableDMAReq_TX(SPIx);
    LL_SPI_DisableDMAReq_RX(SPIx);
    LL_DMA_DisableChannel(DMA1, CHANNEL_RD);
    LL_DMA_DisableChannel(DMA1, CHANNEL_WR);
    LL_DMA_ClearFlag_GI4(DMA1);
    LL_DMA_ClearFlag_GI5(DMA1);
    if (!SPI_WaitStatus(SPIx, SPI_SR_BSY, 0))
        FlashFail();
    __DMB();
    uint32_t drain = 20000;
    while (LL_SPI_RX_FIFO_EMPTY != LL_SPI_GetRxFIFOLevel(SPIx)) {
        (void)LL_SPI_ReceiveData8(SPIx);
        if (!--drain) {
            FlashFail();
            break;
        }
    }
}

static void SPI_ReadBuf(uint8_t *Buf, uint32_t Size)
{
    SPI_TransferBuf(Buf, Size, true);
}

static void SPI_WriteBuf(const uint8_t *Buf, uint32_t Size)
{
    SPI_TransferBuf(Buf, Size, false);
}

static uint8_t SPI_WriteByte(uint8_t Value)
{
    if (flash_error)
        return 0xff;
    if (!SPI_WaitStatus(SPIx, SPI_SR_TXE, SPI_SR_TXE)) {
        FlashFail();
        return 0xff;
    }
    LL_SPI_TransmitData8(SPIx, Value);
    if (!SPI_WaitStatus(SPIx, SPI_SR_RXNE, SPI_SR_RXNE)) {
        FlashFail();
        return 0xff;
    }
    return LL_SPI_ReceiveData8(SPIx);
}

static void WriteAddr(uint32_t Addr);
static uint8_t ReadStatusReg(uint32_t Which);
static void WaitWIP();
static void WriteEnable();
static void SectorErase(uint32_t Addr);
static void SectorProgram(uint32_t Addr, const uint8_t *Buf, uint32_t Size);
static void PageProgram(uint32_t Addr, const uint8_t *Buf, uint32_t Size);
static void ReadBufferRaw(uint32_t Address, void *pBuffer, uint32_t Size);

void PY25Q16_Init()
{
    CS_Release();
    SPI_Init();
}

static void ReadBufferRaw(uint32_t Address, void *pBuffer, uint32_t Size)
{
    CS_Assert();

    SPI_WriteByte(0x03); // Send read command
    WriteAddr(Address);  // Send address (3 bytes)

    // CRITICAL: Flush RX FIFO before DMA to remove residual data
    uint32_t drain = 20000;
    while (LL_SPI_RX_FIFO_EMPTY != LL_SPI_GetRxFIFOLevel(SPIx)) {
        if (!--drain) {
            FlashFail();
            break;
        }
        LL_SPI_ReceiveData8(SPIx); // Read and discard
    }

    if (!flash_error && Size >= 16) {
        SPI_ReadBuf((uint8_t *)pBuffer, Size);
    } else {
        for (uint32_t i = 0; i < Size; i++) {
            ((uint8_t *)(pBuffer))[i] = SPI_WriteByte(0xff);
        }
    }

    CS_Release();
    if (flash_error)
        memset(pBuffer, 0xff, Size);
}

void PY25Q16_ReadPhysical(uint32_t Address, void *pBuffer, uint32_t Size)
{
    if (!writeBatch)
        flash_error = false;
    WaitWIP();
    ReadBufferRaw(Address, pBuffer, Size);
}

/* Physical read reporting its own success, without touching the error state
 * of a write batch in progress (store and module loads). */
bool PY25Q16_ReadChecked(uint32_t Address, void *pBuffer, uint32_t Size)
{
    const bool pending = flash_error;
    flash_error = false;
    WaitWIP();
    ReadBufferRaw(Address, pBuffer, Size);
    const bool ok = !flash_error;
    flash_error = pending;
    return ok;
}

void PY25Q16_ReadBuffer(uint32_t Address, void *pBuffer, uint32_t Size)
{
    if (!writeBatch)
        flash_error = false;
    ReadBufferRaw(BankMap(Address), pBuffer, Size);
}

// Like PY25Q16_ReadBuffer, but waits for the flash to be idle first (WIP=0),
// exactly as PY25Q16_WriteBuffer does before its internal reads. A standalone
// read issued while the chip is still busy from a prior program/erase never
// returns the expected data.
void PY25Q16_ReadBufferSafe(uint32_t Address, void *pBuffer, uint32_t Size)
{
    PY25Q16_ReadPhysical(BankMap(Address), pBuffer, Size);
}

// Opt-in only: normal writes retain their existing immediate commit path.

static void FlushWriteBatch(void)
{
    if (!batchDirty || flash_error)
        return;
    WaitWIP();
    if (batchErase) {
        SectorErase(SectorCacheAddr);
        WaitWIP();
    }
    // An erased sector must be restored in full. Without an erase, program
    // only the modified range, avoiding unrelated page-program operations.
    if (batchErase)
        SectorProgram(SectorCacheAddr, SectorCache, SECTOR_SIZE);
    else
        SectorProgram(SectorCacheAddr + batchFirst, SectorCache + batchFirst, batchLast - batchFirst);
    WaitWIP();
    batchDirty = false;
    batchErase = false;
}
void PY25Q16_BeginBatch(void)
{
    if (!writeBatch)
        flash_error = false;
    FlushWriteBatch();
    writeBatch = true;
}
void PY25Q16_EndBatch(void)
{
    FlushWriteBatch();
    writeBatch = false;
    if (flash_error) {
        batchDirty = false;
        batchErase = false;
        SectorCacheAddr = 0x1000000;
    }
}

/* While a native module runs, the sector cache is its code: give it back
 * after each write (immediate commit, no batch kept in the cache). */
static void ResumeNative(void)
{
    if (!APP_NativeActive())
        return;
    FlushWriteBatch();
    SectorCacheAddr = 0x1000000;
    APP_NativeResume();
}

void PY25Q16_WriteBuffer(uint32_t Address, const void *pBuffer, uint32_t Size, bool Append)
{
    /* Data held in the overlay RAM itself (a module constant) is copied out
     * in small pieces first: the sector cache is about to be reused. */
    const uint8_t *src = pBuffer;
    if (src < SectorCache + SECTOR_SIZE && src + Size > SectorCache) {
        uint8_t piece[32];
        for (uint32_t done = 0; done < Size; done += sizeof piece) {
            const uint32_t n = Size - done < sizeof piece ? Size - done : sizeof piece;
            memcpy(piece, src + done, n);
            PY25Q16_WriteBuffer(Address + done, piece, n, Append && done == 0);
        }
        return;
    }
    if (!writeBatch)
        flash_error = false;
    APP_InvalidateNative();
    if (Append)
        FlushWriteBatch();
    Address = BankMap(Address); /* map once; internal reads use *Raw below */

#ifdef DEBUG
    printf("spi flash write: %06x %ld %d\n", Address, Size, Append);
#endif

    //#ifdef ENABLE_FEAT_F4HWN_DEBUG
    //    gDebug++;
    //#endif

    uint32_t SecIndex = Address / SECTOR_SIZE;
    uint32_t SecAddr = SecIndex * SECTOR_SIZE;
    uint32_t SecOffset = Address % SECTOR_SIZE;
    uint32_t SecSize = SECTOR_SIZE - SecOffset;

    while (Size && !flash_error) {
        // CRITICAL FIX #1: Wait for flash ready before processing each sector
        WaitWIP();
        if (flash_error)
            break;

        if (Size < SecSize) {
            SecSize = Size;
        }

        if (SecAddr != SectorCacheAddr) {
            FlushWriteBatch();
            /* SecAddr is already in mapped space (Address was mapped above), so
             * read raw to avoid mapping a second time. */
            ReadBufferRaw(SecAddr, SectorCache, SECTOR_SIZE);
            if (flash_error)
                break;
            SectorCacheAddr = SecAddr;
        }

        if (0 != memcmp(pBuffer, (char *)SectorCache + SecOffset, SecSize)) {
            bool Erase = false;
            const uint8_t *oldData = SectorCache + SecOffset;
            const uint8_t *newData = (const uint8_t *)pBuffer;

            for (uint32_t i = 0; i < SecSize; i++) {
                // NOR flash programming can only change bits from 1 to 0.
                if ((oldData[i] & newData[i]) != newData[i]) {
                    Erase = true;
                    break;
                }
            }

            memcpy(SectorCache + SecOffset, pBuffer, SecSize);

            if (writeBatch && !Append) {
                if (!batchDirty) {
                    batchFirst = SecOffset;
                    batchLast = SecOffset + SecSize;
                } else {
                    if (SecOffset < batchFirst)
                        batchFirst = SecOffset;
                    if (SecOffset + SecSize > batchLast)
                        batchLast = SecOffset + SecSize;
                }
                batchDirty = true;
                batchErase |= Erase;
            } else if (Erase) {
                FlushWriteBatch();
                SectorErase(SecAddr);

                // CRITICAL FIX #2: Erase takes ~300ms, must complete before program starts
                WaitWIP();

                if (Append) {
                    SectorProgram(SecAddr, SectorCache, SecOffset + SecSize);
                    memset(SectorCache + SecOffset + SecSize, 0xff, SECTOR_SIZE - SecOffset - SecSize);
                } else {
                    SectorProgram(SecAddr, SectorCache, SECTOR_SIZE);
                }
            } else {
                SectorProgram(Address, pBuffer, SecSize);
            }
        }

        Address += SecSize;
        pBuffer += SecSize;
        Size -= SecSize;

        SecAddr += SECTOR_SIZE;
        SecOffset = 0;
        SecSize = SECTOR_SIZE;
    } // while

    // CRITICAL FIX #3: Ensure all writes complete before function returns
    WaitWIP();
    if (flash_error) {
        SectorCacheAddr = 0x1000000;
        batchDirty = false;
    }
    ResumeNative();
}

/* Raw access for data kept outside the sector cache (Falcon messages): they
 * may run while an app or a native module occupies the overlay RAM, which is
 * also the cache, so the cache is only forgotten, never read or written. The
 * address is physical (not banked); programming assumes erased bytes, or only
 * clears bits. */
void PY25Q16_ProgramRaw(uint32_t Address, const void *pBuffer, uint32_t Size)
{
    flash_error = false;
    WaitWIP();
    SectorProgram(Address, pBuffer, Size);
    WaitWIP();
    if (SectorCacheAddr == Address - Address % SECTOR_SIZE)
        SectorCacheAddr = 0x1000000;
}

void PY25Q16_EraseRaw(uint32_t Address)
{
    flash_error = false;
    Address -= Address % SECTOR_SIZE;
    SectorErase(Address);
    if (SectorCacheAddr == Address)
        SectorCacheAddr = 0x1000000;
}

void PY25Q16_SectorErase(uint32_t Address)
{
    if (!writeBatch)
        flash_error = false;
    APP_InvalidateNative();
    Address = BankMap(Address);
    Address -= (Address % SECTOR_SIZE);
    SectorErase(Address);
    if (flash_error)
        SectorCacheAddr = 0x1000000;
    if (SectorCacheAddr == Address) {
        memset(SectorCache, 0xff, SECTOR_SIZE);
    }
    ResumeNative();
}

void PY25Q16_InvalidateCache(void)
{
    /* Same "no sector cached" sentinel as the initial value: the next write
     * re-reads its sector from flash instead of trusting SectorCache. */
    APP_InvalidateNative();
    SectorCacheAddr = 0x1000000;
}

static inline void WriteAddr(uint32_t Addr)
{
    SPI_WriteByte(0xff & (Addr >> 16));
    SPI_WriteByte(0xff & (Addr >> 8));
    SPI_WriteByte(0xff & Addr);
}

static uint8_t ReadStatusReg(uint32_t Which)
{
    uint8_t Cmd;
    switch (Which) {
        case 0:
            Cmd = 0x5;
            break;
        case 1:
            Cmd = 0x35;
            break;
        case 2:
            Cmd = 0x15;
            break;
        default:
            return 0;
    }

    CS_Assert();
    SPI_WriteByte(Cmd);
    uint8_t Value = SPI_WriteByte(0xff);
    CS_Release();

    return Value;
}

static void WaitWIP()
{
    for (uint32_t remaining = 100000; remaining && !flash_error; --remaining) {
        if (!(ReadStatusReg(0) & 1u))
            return;
        SYSTICK_DelayUs(10);
    }
    FlashFail();
}

static void WriteEnable()
{
    CS_Assert();
    SPI_WriteByte(0x6);
    CS_Release();
}

static void SectorErase(uint32_t Addr)
{
#ifdef DEBUG
    printf("spi flash sector erase: %06x\n", Addr);
#endif
    WriteEnable();
    WaitWIP();

    if (flash_error)
        return;
    CS_Assert();
    SPI_WriteByte(0x20);
    WriteAddr(Addr);
    CS_Release();

    WaitWIP();
}

static void SectorProgram(uint32_t Addr, const uint8_t *Buf, uint32_t Size)
{
    uint32_t Size1 = PAGE_SIZE - (Addr % PAGE_SIZE);

    while (Size && !flash_error) {
        if (Size < Size1) {
            Size1 = Size;
        }

        PageProgram(Addr, Buf, Size1);

        Addr += Size1;
        Buf += Size1;
        Size -= Size1;

        Size1 = PAGE_SIZE;
    }
}

static void PageProgram(uint32_t Addr, const uint8_t *Buf, uint32_t Size)
{
#ifdef DEBUG
    printf("spi flash page program: %06x %ld\n", Addr, Size);
#endif

    WriteEnable();
    // WaitWIP();

    if (flash_error)
        return;
    CS_Assert();

    SPI_WriteByte(0x2);
    WriteAddr(Addr);

    if (Size >= 16) {
        SPI_WriteBuf(Buf, Size);
    } else {
        for (uint32_t i = 0; i < Size; i++) {
            SPI_WriteByte(Buf[i]);
        }
    }

    CS_Release();

    WaitWIP();
}

void DMA1_Channel4_5_6_7_IRQHandler()
{
    if (LL_DMA_IsActiveFlag_TC4(DMA1) && LL_DMA_IsEnabledIT_TC(DMA1, CHANNEL_RD)) {
        LL_DMA_DisableIT_TC(DMA1, CHANNEL_RD);
        LL_DMA_ClearFlag_TC4(DMA1);
        TC_Flag = true;
    }
}

void PY25Q16_OverlayWriteSector(uint32_t Address)
{
    if (!writeBatch)
        flash_error = false;
    Address = BankMap(Address);
    WaitWIP();
    SectorErase(Address);
    WaitWIP();
    SectorProgram(Address, SectorCache, SECTOR_SIZE);
    WaitWIP();
    PY25Q16_InvalidateCache();
}
