/* Copyright 2025 muzkr https://github.com/muzkr
 * Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
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
#if !defined(ENABLE_OVERLAY)
#include "py32f0xx.h"
#endif
#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif
#include "app/uart.h"
#include "board.h"
#include "py32f071_ll_dma.h"
#include "driver/backlight.h"
#include "driver/bk4819.h"
#include "driver/crc.h"
#include "driver/gpio.h"

#if defined(ENABLE_UART)
#include "driver/uart.h"
#endif

#if defined(ENABLE_USB)
#include "driver/vcp.h"
#endif

#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "version.h"

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
#include "driver/mb_flash.h"
#endif

#if defined(ENABLE_OVERLAY)
#include "sram-overlay.h"
#endif

#define UNUSED(x) (void)(x)

#define DMA_INDEX(x, y, z) (((x) + (y)) % (z))

#if defined(ENABLE_UART)
#define DMA_CHANNEL LL_DMA_CHANNEL_2
#endif

// !! Make sure this is correct!
#define MAX_REPLY_SIZE 144

typedef struct {
    uint16_t ID;
    uint16_t Size;
} Header_t;

typedef struct {
    uint8_t Padding[2];
    uint16_t ID;
} Footer_t;

typedef struct {
    Header_t Header;
    uint32_t Timestamp;
} CMD_0514_t;

typedef struct {
    Header_t Header;
    struct {
        char Version[16];
        bool bHasCustomAesKey;
        bool bIsInLockScreen;
        uint8_t Padding[2];
        uint32_t Challenge[4];
    } Data;
} REPLY_0514_t;

typedef struct {
    Header_t Header;
    uint32_t Offset;
    uint8_t Size;
    uint8_t Padding;
    uint32_t Timestamp;
} CMD_051B_t;

typedef struct {
    Header_t Header;
    struct {
        uint32_t Offset;
        uint8_t Size;
        uint8_t Padding;
        uint8_t Data[128];
    } Data;
} REPLY_051B_t;

typedef struct {
    Header_t Header;
    uint32_t Offset;
    uint8_t Size;
    bool bAllowPassword;
    uint32_t Timestamp;
    uint8_t Data[0];
} CMD_051D_t;

typedef struct {
    Header_t Header;
    struct {
        uint16_t Offset;
    } Data;
} REPLY_051D_t;


typedef struct {
    Header_t Header;
    struct {
        bool bIsLocked;
        uint8_t Padding[3];
    } Data;
} REPLY_052D_t;


static const uint8_t Obfuscation[16] = {0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40,
                                        0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80};

typedef union {
    uint8_t Buffer[256];
    struct {
        Header_t Header;
        uint8_t Data[252];
    };
} UART_Command_t __attribute__((aligned(4)));


/* One decoded-command buffer serves both links. A command is always decoded
 * by UART_IsCommandAvailable() and consumed by UART_HandleCommand() for the
 * same port before the other port is polled, so the two never overlap. */
static UART_Command_t gCommand;

#if defined(ENABLE_UART)
static uint32_t UART_Timestamp;
static uint16_t gUART_WriteIndex;
#endif
#if defined(ENABLE_USB)
static uint32_t VCP_Timestamp;
uint16_t VCP_ReadIndex;
#endif

// static bool     bIsEncrypted = true;
#define bIsEncrypted true

#ifdef ENABLE_USB
static void SendReply_VCP(void *pReply, uint16_t Size)
{
    static uint8_t VCP_ReplyBuf[MAX_REPLY_SIZE + sizeof(Header_t) + sizeof(Footer_t)]
        __attribute__((aligned(4)));

    // !!
    if (Size > MAX_REPLY_SIZE) {
        return;
    }

    uint8_t *pBody = VCP_ReplyBuf + sizeof(Header_t);
    uint8_t *pFooter = pBody + Size;

    memcpy(pBody, pReply, Size);
    pReply = pBody;

    if (bIsEncrypted) {
        uint8_t *pBytes = (uint8_t *)pReply;
        unsigned int i;
        for (i = 0; i < Size; i++)
            pBytes[i] ^= Obfuscation[i % 16];
    }

    VCP_ReplyBuf[0] = 0xAB;
    VCP_ReplyBuf[1] = 0xCD;
    VCP_ReplyBuf[2] = (uint8_t)(Size & 0xFFu);
    VCP_ReplyBuf[3] = (uint8_t)(Size >> 8);

    // VCP_Send((uint8_t *)&Header, sizeof(Header));
    // VCP_Send(pReply, Size);

    if (bIsEncrypted) {
        pFooter[0] = Obfuscation[(Size + 0) % 16] ^ 0xFF;
        pFooter[1] = Obfuscation[(Size + 1) % 16] ^ 0xFF;
    } else {
        pFooter[0] = 0xFF;
        pFooter[1] = 0xFF;
    }
    pFooter[2] = 0xDC;
    pFooter[3] = 0xBA;

    // VCP_Send((uint8_t *)&Footer, sizeof(Footer));

    VCP_SendAsync(VCP_ReplyBuf, sizeof(Header_t) + Size + sizeof(Footer_t));
}
#endif // ENABLE_USB

static void SendReply(uint32_t Port, void *pReply, uint16_t Size)
{
#if defined(ENABLE_USB)
    if (Port == UART_PORT_VCP) {
        SendReply_VCP(pReply, Size);
        return;
    }
#endif

    if (bIsEncrypted) {
        uint8_t *pBytes = (uint8_t *)pReply;
        for (unsigned int i = 0; i < Size; i++)
            pBytes[i] ^= Obfuscation[i % 16];
    }

#ifdef ENABLE_UART
    Header_t Header;
    Header.ID = 0xCDAB;
    Header.Size = Size;
    UART_Send(&Header, sizeof(Header));
    UART_Send(pReply, Size);

    Footer_t Footer;
    if (bIsEncrypted) {
        Footer.Padding[0] = Obfuscation[(Size + 0) % 16] ^ 0xFF;
        Footer.Padding[1] = Obfuscation[(Size + 1) % 16] ^ 0xFF;
    } else {
        Footer.Padding[0] = 0xFF;
        Footer.Padding[1] = 0xFF;
    }
    Footer.ID = 0xBADC;
    UART_Send(&Footer, sizeof(Footer));
#endif
}

/* Falcon 1K answers "3.2K": its channels are not at the 4K driver's
 * addresses, so each CHIRP driver accepts only its own edition. */
#ifdef ENABLE_1024
#define FALCON_HELLO "FALCON 7.0/3.2K"
#else
#define FALCON_HELLO "FALCON 7.0/3.2"
#endif

static void SendVersion(uint32_t Port)
{
    REPLY_0514_t Reply;

    Reply.Header.ID = 0x0515;
    Reply.Header.Size = sizeof(Reply.Data);
    /* UVStudio chooses 16/48/64 app slots from the first numeric version,
     * not a capability query. Announce the 64-slot, API-3 app protocol here;
     * Version, boot display, module metadata and EEPROM retain Falcon identity. */
    _Static_assert(sizeof(FALCON_HELLO) <= sizeof(Reply.Data.Version), "device identity");
    strcpy(Reply.Data.Version, FALCON_HELLO);
    Reply.Data.bHasCustomAesKey = bHasCustomAesKey;
    Reply.Data.bIsInLockScreen = bIsInLockScreen;
    Reply.Data.Challenge[0] = gChallenge[0];
    Reply.Data.Challenge[1] = gChallenge[1];
    Reply.Data.Challenge[2] = gChallenge[2];
    Reply.Data.Challenge[3] = gChallenge[3];

    SendReply(Port, &Reply, sizeof(Reply));
}

#ifndef ENABLE_FEAT_F4HWN
static bool IsBadChallenge(const uint32_t *pKey, const uint32_t *pIn, const uint32_t *pResponse)
{
    return false;
}
#endif

// session init, sends back version info and state
// timestamp is a session id really
static void CMD_0514(uint32_t Port, const uint8_t *pBuffer)
{
    const CMD_0514_t *pCmd = (const CMD_0514_t *)pBuffer;

    if (0) {
    }
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART) {
        UART_Timestamp = pCmd->Timestamp;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP) {
        VCP_Timestamp = pCmd->Timestamp;
    }
#endif

#ifdef ENABLE_FMRADIO
    gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
#endif

    gSerialConfigCountDown_500ms = 12; // 6 sec

    // turn the LCD backlight off
    BACKLIGHT_TurnOff();

    SendVersion(Port);
}

#define CALIB_EEPROM_START 0x00B000
#define CALIB_SIZE         0x000200
#define CALIB_FLASH_START  0x010000

#define LOGO_EEPROM_START 0x00C000
#define LOGO_SIZE         0x001000
#define LOGO_FLASH_START  0x011000

// read eeprom
static void CMD_051B(uint32_t Port, const uint8_t *pBuffer)
{
    const CMD_051B_t *pCmd = (const CMD_051B_t *)pBuffer;
    REPLY_051B_t Reply;
    bool bLocked = false;
    if (pCmd->Header.Size != sizeof(*pCmd) - sizeof(Header_t) || pCmd->Size > sizeof(Reply.Data.Data))
        return;


    uint32_t Timestamp = 0;

    if (0) {
    }
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART) {
        Timestamp = UART_Timestamp;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP) {
        Timestamp = VCP_Timestamp;
    }
#endif
    else {
        return;
    }

    if (pCmd->Timestamp != Timestamp)
        return;

    gSerialConfigCountDown_500ms = 12; // 6 sec

#ifdef ENABLE_FMRADIO
    gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
#endif

    memset(&Reply, 0, sizeof(Reply));
    Reply.Header.ID = 0x051C;
    Reply.Header.Size = pCmd->Size + 6;
    Reply.Data.Offset = pCmd->Offset;
    Reply.Data.Size = pCmd->Size;

    if (bHasCustomAesKey)
        bLocked = gIsLocked;

    if (!bLocked) {
        uint32_t addr = pCmd->Offset;
#ifdef ENABLE_4096
        if (addr >= 0x122000u && addr < 0x144000u)
            addr += ADRESS_CHANNELS - 0x122000u;
#endif

        // Translation directe pour la plage de calibration
        if (addr >= CALIB_EEPROM_START && addr < (CALIB_EEPROM_START + CALIB_SIZE)) {
            addr += (CALIB_FLASH_START - CALIB_EEPROM_START);
        }

        PY25Q16_ReadBuffer(addr, Reply.Data.Data, pCmd->Size);
    }

    if (PY25Q16_IsHealthy())
        SendReply(Port, &Reply, pCmd->Size + 10);
}

/* CHIRP's 4K channel window 0x122000..0x143FFF maps to ADRESS_CHANNELS.
 * Falcon 1K has none: its channels are in the config bank, below 0x10000. */
#ifdef ENABLE_4096
#define CHIRP_CHANNEL_WINDOW 1
#else
#define CHIRP_CHANNEL_WINDOW 0
#endif

// write eeprom
static void CMD_051D(uint32_t Port, const uint8_t *pBuffer)
{
    const CMD_051D_t *pCmd = (const CMD_051D_t *)pBuffer;
    REPLY_051D_t Reply;
    bool bReloadEeprom;
    bool bIsLocked;
    if (pCmd->Header.Size < sizeof(*pCmd) - sizeof(Header_t) + pCmd->Size || pCmd->Size > 128)
        return;


    uint32_t Timestamp = 0;

    if (0) {
    }
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART) {
        Timestamp = UART_Timestamp;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP) {
        Timestamp = VCP_Timestamp;
    }
#endif
    else {
        return;
    }

    if (pCmd->Timestamp != Timestamp)
        return;

    const uint32_t offset = pCmd->Offset;
    if (!((offset < 0x10000u && pCmd->Size <= 0x10000u - offset) ||
          (offset >= LOGO_FLASH_START && offset < LOGO_FLASH_START + LOGO_SIZE &&
           pCmd->Size <= LOGO_FLASH_START + LOGO_SIZE - offset) ||
          (CHIRP_CHANNEL_WINDOW && offset >= 0x122000u && offset < 0x144000u &&
           pCmd->Size <= 0x144000u - offset)))
        return;
    gSerialConfigCountDown_500ms = 12; // 6 sec

    bReloadEeprom = false;

#ifdef ENABLE_FMRADIO
    gFmRadioCountdown_500ms = fm_radio_countdown_500ms;
#endif

    Reply.Header.ID = 0x051E;
    Reply.Header.Size = sizeof(Reply.Data);
    Reply.Data.Offset = pCmd->Offset;
    bIsLocked = bHasCustomAesKey ? gIsLocked : false;

    if (!bIsLocked) {
        unsigned int i;
        for (i = 0; i < pCmd->Size; i += 8) {
            uint32_t Offset = pCmd->Offset + i;

            if (Offset >= 0x0F30 && Offset < 0x0F40)
                if (!gIsLocked)
                    bReloadEeprom = true;

            if ((Offset < 0x0E98 || Offset >= 0x0EA0) || !bIsInLockScreen || pCmd->bAllowPassword) {
                uint32_t addr = Offset;
#ifdef ENABLE_4096
                if (addr >= 0x122000u && addr < 0x144000u)
                    addr += ADRESS_CHANNELS - 0x122000u;
#endif

                // Translation directe pour la plage de calibration
                if (addr >= CALIB_EEPROM_START && addr < (CALIB_EEPROM_START + CALIB_SIZE)) {
                    addr += (CALIB_FLASH_START - CALIB_EEPROM_START);
                }

                PY25Q16_WriteBuffer(addr, &pCmd->Data[i], pCmd->Size - i < 8 ? pCmd->Size - i : 8, false);
            }
        }

        if (bReloadEeprom)
            SETTINGS_InitEEPROM();
    }

    if (PY25Q16_IsHealthy())
        SendReply(Port, &Reply, sizeof(Reply));
}


#ifdef ENABLE_UART_RW_BK_REGS
static void CMD_0601_ReadBK4819Reg(uint32_t Port, const uint8_t *pBuffer)
{
    typedef struct __attribute__((__packed__)) {
        Header_t header;
        uint8_t reg;
    } CMD_0601_t;

    CMD_0601_t *cmd = (CMD_0601_t *)pBuffer;

    struct __attribute__((__packed__)) {
        Header_t header;
        struct __attribute__((__packed__)) {
            uint8_t reg;
            uint16_t value;
        } data;
    } reply;

    reply.header.ID = 0x0601;
    reply.header.Size = sizeof(reply.data);
    reply.data.reg = cmd->reg;
    reply.data.value = BK4819_ReadRegister(cmd->reg);
    SendReply(Port, &reply, sizeof(reply));
}

static void CMD_0602_WriteBK4819Reg(const uint8_t *pBuffer)
{
    typedef struct __attribute__((__packed__)) {
        Header_t header;
        uint8_t reg;
        uint16_t value;
    } CMD_0602_t;

    CMD_0602_t *cmd = (CMD_0602_t *)pBuffer;
    BK4819_WriteRegister(cmd->reg, cmd->value);
}
#endif

bool UART_IsCommandAvailable(uint32_t Port)
{
    uint16_t Index;
    uint16_t TailIndex;
    uint16_t Size;
    uint16_t Crc;
    uint16_t CommandLength;
    uint16_t DmaLength;
    uint8_t *ReadBuf;
    uint16_t ReadBufSize;
    uint16_t *pReadPointer;
    UART_Command_t *pUART_Command;

    if (0) {
    }
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART) {
        DmaLength = sizeof(UART_DMA_Buffer) - LL_DMA_GetDataLength(DMA1, DMA_CHANNEL);
        ReadBuf = UART_DMA_Buffer;
        ReadBufSize = sizeof(UART_DMA_Buffer);
        pReadPointer = &gUART_WriteIndex;
        pUART_Command = &gCommand;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP) {
        DmaLength = VCP_RxBufPointer;
        ReadBuf = VCP_RxBuf;
        ReadBufSize = sizeof(VCP_RxBuf);
        pReadPointer = &VCP_ReadIndex;
        pUART_Command = &gCommand;
    }
#endif
    else {
        return false;
    }

    // Limit iterations to prevent long loops when buffer is full of non-command data
    uint16_t maxIterations = ReadBufSize + 1;

    while (maxIterations--) {
        if ((*pReadPointer) == DmaLength)
            return false;

        // Find 0xAB with iteration limit
        uint16_t searchLimit = ReadBufSize;
        while ((*pReadPointer) != DmaLength && ReadBuf[*pReadPointer] != 0xABU && searchLimit--)
            *pReadPointer = DMA_INDEX((*pReadPointer), 1, ReadBufSize);

        if (searchLimit == 0) {
            // Too many bytes without finding 0xAB - sync to current position and exit
            *pReadPointer = DmaLength;
            return false;
        }

        if ((*pReadPointer) == DmaLength)
            return false;

        if ((*pReadPointer) < DmaLength)
            CommandLength = DmaLength - (*pReadPointer);
        else
            CommandLength = (DmaLength + ReadBufSize) - (*pReadPointer);

        if (CommandLength < 8)
            return 0;

        if (ReadBuf[DMA_INDEX(*pReadPointer, 1, ReadBufSize)] == 0xCD)
            break;

        *pReadPointer = DMA_INDEX(*pReadPointer, 1, ReadBufSize);
    }

    if (maxIterations == 0) {
        // Safety: too many outer loop iterations
        *pReadPointer = DmaLength;
        return false;
    }

    Index = DMA_INDEX(*pReadPointer, 2, ReadBufSize);
    Size = (ReadBuf[DMA_INDEX(Index, 1, ReadBufSize)] << 8) | ReadBuf[Index];

    if ((Size + 8u) > ReadBufSize) {
        *pReadPointer = DmaLength;
        return false;
    }

    if (CommandLength < (Size + 8))
        return false;

    Index = DMA_INDEX(Index, 2, ReadBufSize);
    TailIndex = DMA_INDEX(Index, Size + 2, ReadBufSize);

    if (ReadBuf[TailIndex] != 0xDC || ReadBuf[DMA_INDEX(TailIndex, 1, ReadBufSize)] != 0xBA) {
        *pReadPointer = DmaLength;
        return false;
    }

    if (TailIndex < Index) {
        const uint16_t ChunkSize = ReadBufSize - Index;
        memcpy(pUART_Command->Buffer, ReadBuf + Index, ChunkSize);
        memcpy(pUART_Command->Buffer + ChunkSize, ReadBuf, TailIndex);
    } else
        memcpy(pUART_Command->Buffer, ReadBuf + Index, TailIndex - Index);

    TailIndex = DMA_INDEX(TailIndex, 2, ReadBufSize);
    if (TailIndex < (*pReadPointer)) {
        memset(ReadBuf + (*pReadPointer), 0, ReadBufSize - (*pReadPointer));
        memset(ReadBuf, 0, TailIndex);
    } else
        memset(ReadBuf + (*pReadPointer), 0, TailIndex - (*pReadPointer));

    *pReadPointer = TailIndex;

    /* --
    if (pUART_Command->Header.ID == 0x0514)
        bIsEncrypted = false;

    if (pUART_Command->Header.ID == 0x6902)
        bIsEncrypted = true;
    -- */

    if (bIsEncrypted) {
        unsigned int i;
        for (i = 0; i < (Size + 2u); i++)
            pUART_Command->Buffer[i] ^= Obfuscation[i % 16];
    }

    Crc = pUART_Command->Buffer[Size] | (pUART_Command->Buffer[Size + 1] << 8);

    return CRC_Calculate(pUART_Command->Buffer, Size) == Crc;
}

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
/* Timestamp latched by the device-info handshake for this serial transport. */
static uint32_t mb_port_timestamp(uint32_t Port)
{
#if defined(ENABLE_UART)
    if (Port == UART_PORT_UART)
        return UART_Timestamp;
#endif
#if defined(ENABLE_USB)
    if (Port == UART_PORT_VCP)
        return VCP_Timestamp;
#endif
    (void)Port;
    return 0;
}
#endif

/* Host protocol lives in the existing external boot block. NOR operations
 * use resident polled routines that leave its execution RAM untouched.
 * Invalidate the shared cache only after the external handler returns. */
typedef struct __attribute__((packed)) {
    Header_t Header;
    uint32_t Address;
    uint8_t Data[9];
} app_raw_reply_t;
static NATIVE_CODE(boot) uint8_t
    app_raw_command(uint32_t port, const UART_Command_t *command, app_raw_reply_t *reply)
{
    uint8_t operation = (command->Header.ID - 0x073A) / 2 + 1;
    uint16_t prefix = 6 + 2 * operation;
    uint16_t size = command->Header.Size;
    if (size < prefix)
        return 255;
    memset(reply, 0, sizeof(*reply));
    reply->Header.ID = command->Header.ID + 1;
    reply->Header.Size = operation == 3 ? 13 : prefix - 3;
    memcpy(&reply->Address, command->Data, 4);
    uint32_t length = 4096;
    if (operation == 2) {
        length = 0;
        memcpy(&length, command->Data + 4, 2);
    }
    if (operation == 3)
        memcpy(&length, command->Data + 4, 4);
    uint8_t *status = reply->Data + reply->Header.Size - 5;
    *status = MB_ERR_AUTH;
    uint32_t timestamp;
    memcpy(&timestamp, command->Data + prefix - 4, 4);
    if (timestamp != mb_port_timestamp(port) || (bHasCustomAesKey && gIsLocked))
        return 0;
    *status = MB_ERR_SIZE;
    uint32_t address = reply->Address;
    if (!length)
        return 0;
    if (operation == 3) {
        if (address >= 0x200000u || length > 0x200000u - address || size != prefix)
            return 0;
        memcpy(reply->Data, &length, 4);
    } else {
        if (address < APP_REGION_BASE || address >= APP_REGION_BASE + APP_SLOT_COUNT * APP_SLOT_STRIDE ||
            length > APP_REGION_BASE + APP_SLOT_COUNT * APP_SLOT_STRIDE - address)
            return 0;
        if (operation == 1) {
            if (size != prefix || (address & 4095u))
                return 0;
        } else {
            if (length > 128 || size != prefix + length)
                return 0;
            memcpy(reply->Data, &length, 2);
        }
    }
    if (operation == 1 || operation == 2) {
        *status = MB_AppProgram(address, operation == 2 ? command->Data + 10 : NULL, length);
    } else {
        uint32_t crc;
        *status = MB_StoreCRC(address, length, &crc);
        if (*status == MB_OK)
            memcpy(reply->Data + 4, &crc, 4);
    }
    return operation;
}

void UART_HandleCommand(uint32_t Port)
{
    UART_Command_t *pUART_Command;

    if (0) {
    }
#if defined(ENABLE_UART)
    else if (Port == UART_PORT_UART) {
        pUART_Command = &gCommand;
    }
#endif
#if defined(ENABLE_USB)
    else if (Port == UART_PORT_VCP) {
        pUART_Command = &gCommand;
    }
#endif
    else {
        return;
    }

    if (APP_OverlayBusy() && pUART_Command->Header.ID != 0x0514 && pUART_Command->Header.ID != 0x051B &&
        pUART_Command->Header.ID != 0x0730 && pUART_Command->Header.ID != 0x0736 &&
        pUART_Command->Header.ID != 0x0738)
        return;

    // A PC reading or writing the configuration finds the VFO saved.
    if (!APP_OverlayBusy())
        SETTINGS_SaveVfoNow();
    switch (pUART_Command->Header.ID) {
        case 0x0514:
            CMD_0514(Port, pUART_Command->Buffer);
            break;

        case 0x051B:
            CMD_051B(Port, pUART_Command->Buffer);
            break;

        case 0x051D:
            CMD_051D(Port, pUART_Command->Buffer);
            break;

        case 0x051F: // Not implementing non-authentic command
            break;

        case 0x0521: // Not implementing non-authentic command
            break;


        case 0x05DD: // reset
#if defined(ENABLE_OVERLAY)
            overlay_FLASH_RebootToBootloader();
#else
            NVIC_SystemReset();
#endif
            break;

#ifdef ENABLE_FEAT_F4HWN_MULTIBOOT
        // ---- M4 slot management ("Firmware Slots") ------------------------
        case 0x0720: // slot info: read the 64-byte header only (fast, no CRC)
        {
            if (pUART_Command->Header.Size < 1u)
                break;                         // needs Data[0] (slot)
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            mb_slot_header_t hdr;
            memset(&hdr, 0, sizeof(hdr));
            uint8_t status = MB_SlotInfo(slot, &hdr);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t Slot;
                uint8_t Status;
                uint8_t Hdr[sizeof(mb_slot_header_t)];
            } Reply;
            Reply.Header.ID = 0x0721;
            Reply.Header.Size = 2 + sizeof(mb_slot_header_t);
            Reply.Slot = slot;
            Reply.Status = status;
            memcpy(Reply.Hdr, &hdr, sizeof(hdr));
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0722: // slot erase: wipe the whole 128 KiB slot region
        {
            if (pUART_Command->Header.Size < 6u)
                break;                         // needs Data[0] slot + Data[2..5] timestamp
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            uint32_t ts = (uint32_t)pUART_Command->Data[2] | ((uint32_t)pUART_Command->Data[3] << 8) |
                          ((uint32_t)pUART_Command->Data[4] << 16) | ((uint32_t)pUART_Command->Data[5] << 24);
            uint8_t status = (ts != mb_port_timestamp(Port)) ? MB_ERR_AUTH : MB_SlotErase(slot);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t Slot;
                uint8_t Status;
            } Reply;
            Reply.Header.ID = 0x0723;
            Reply.Header.Size = 2;
            Reply.Slot = slot;
            Reply.Status = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0724: // slot write: program bytes at slot+offset (slot pre-erased)
        {
            if (pUART_Command->Header.Size < 12u)
                break;
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            uint32_t offset = (uint32_t)pUART_Command->Data[2] | ((uint32_t)pUART_Command->Data[3] << 8) |
                              ((uint32_t)pUART_Command->Data[4] << 16) |
                              ((uint32_t)pUART_Command->Data[5] << 24);
            uint16_t len = (uint16_t)(pUART_Command->Data[6] | ((uint16_t)pUART_Command->Data[7] << 8));
            uint32_t ts = (uint32_t)pUART_Command->Data[8] | ((uint32_t)pUART_Command->Data[9] << 8) |
                          ((uint32_t)pUART_Command->Data[10] << 16) |
                          ((uint32_t)pUART_Command->Data[11] << 24);
            uint8_t status;
            if (ts != mb_port_timestamp(Port))
                status = MB_ERR_AUTH;
            else if (len > pUART_Command->Header.Size - 12u)
                status = MB_ERR_SIZE;
            else
                status = MB_SlotWrite(slot, offset, &pUART_Command->Data[12], len);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t Slot;
                uint8_t Status;
            } Reply;
            Reply.Header.ID = 0x0725;
            Reply.Header.Size = 2;
            Reply.Slot = slot;
            Reply.Status = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0726: // slot validate: full image CRC-32, no reflash
        {
            if (pUART_Command->Header.Size < 1u)
                break;                         // needs Data[0] (slot)
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            uint32_t crc = 0;
            uint8_t status = MB_ValidateSlot(slot, NULL, &crc);
            struct __attribute__((packed)) {
                Header_t Header;
                uint32_t Crc32; // offset 4: 4-byte aligned, no unaligned store
                uint8_t Slot;
                uint8_t Status;
            } Reply;
            Reply.Header.ID = 0x0727;
            Reply.Header.Size = 6;
            Reply.Crc32 = crc;
            Reply.Slot = slot;
            Reply.Status = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0728: // config reset: wipe the 64 KiB of a config bank (1..4)
        {
            if (pUART_Command->Header.Size < 6u)
                break;                         // needs Data[0] bank + Data[2..5] timestamp
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t bank = pUART_Command->Data[0];
            uint32_t ts = (uint32_t)pUART_Command->Data[2] | ((uint32_t)pUART_Command->Data[3] << 8) |
                          ((uint32_t)pUART_Command->Data[4] << 16) | ((uint32_t)pUART_Command->Data[5] << 24);
            uint8_t status = (ts != mb_port_timestamp(Port)) ? MB_ERR_AUTH : MB_BankErase(bank);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t Bank; // echoes the erased bank (same wire layout as slot replies)
                uint8_t Status;
            } Reply;
            Reply.Header.ID = 0x0729;
            Reply.Header.Size = 2;
            Reply.Bank = bank;
            Reply.Status = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }
#endif


#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS
        // ---- overlay-app slot management ("Apps") -------------------------
        // Parallels the firmware-slot family (0x072x); targets the external-flash
        // Apps region. External flash only, never brick-critical.
        case 0x0730: // app slot info: read the 64-byte header only
        {
            if (pUART_Command->Header.Size < 1u)
                break;                         // needs Data[0] (slot)
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            app_header_t hdr;
            memset(&hdr, 0, sizeof(hdr));
            uint8_t status = APP_SlotInfo(slot, &hdr);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t Slot;
                uint8_t Status;
                uint8_t Hdr[sizeof(app_header_t)];
            } Reply;
            Reply.Header.ID = 0x0731;
            Reply.Header.Size = 2 + sizeof(app_header_t);
            Reply.Slot = slot;
            Reply.Status = status;
            memcpy(Reply.Hdr, &hdr, sizeof(hdr));
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0732: // app slot erase: wipe the 8 KiB slot region, but the app's tagged settings (see APP_SlotErase)
        {
            if (pUART_Command->Header.Size < 6u)
                break;                         // needs Data[0] slot + Data[2..5] timestamp
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            uint32_t ts = (uint32_t)pUART_Command->Data[2] | ((uint32_t)pUART_Command->Data[3] << 8) |
                          ((uint32_t)pUART_Command->Data[4] << 16) | ((uint32_t)pUART_Command->Data[5] << 24);
            uint8_t status = (ts != mb_port_timestamp(Port)) ? APP_ERR_AUTH : APP_SlotErase(slot);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t Slot;
                uint8_t Status;
            } Reply;
            Reply.Header.ID = 0x0733;
            Reply.Header.Size = 2;
            Reply.Slot = slot;
            Reply.Status = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x0734: // app slot write: program bytes at slot+offset (pre-erased)
        {
            if (pUART_Command->Header.Size < 12u)
                break;
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            uint32_t offset = (uint32_t)pUART_Command->Data[2] | ((uint32_t)pUART_Command->Data[3] << 8) |
                              ((uint32_t)pUART_Command->Data[4] << 16) |
                              ((uint32_t)pUART_Command->Data[5] << 24);
            uint16_t len = (uint16_t)(pUART_Command->Data[6] | ((uint16_t)pUART_Command->Data[7] << 8));
            uint32_t ts = (uint32_t)pUART_Command->Data[8] | ((uint32_t)pUART_Command->Data[9] << 8) |
                          ((uint32_t)pUART_Command->Data[10] << 16) |
                          ((uint32_t)pUART_Command->Data[11] << 24);
            uint8_t status;
            if (ts != mb_port_timestamp(Port))
                status = APP_ERR_AUTH;
            else if (len > pUART_Command->Header.Size - 12u)
                status = APP_ERR_SIZE;
            else
                status = APP_SlotWrite(slot, offset, &pUART_Command->Data[12], len);
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t Slot;
                uint8_t Status;
            } Reply;
            Reply.Header.ID = 0x0735;
            Reply.Header.Size = 2;
            Reply.Slot = slot;
            Reply.Status = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }

        case 0x073A:
        case 0x073C:
        case 0x073E: {
            app_raw_reply_t reply;
            if (!APP_NativeBegin(10))
                break;
            uint8_t operation = app_raw_command(Port, pUART_Command, &reply);
            APP_NativeEnd();
            if (operation == 255)
                break;
            if (operation == 1 || operation == 2) {
                PY25Q16_InvalidateCache();
                APP_NotifySlotChanged();
            }
            gSerialConfigCountDown_500ms = 12;
            SendReply(Port, &reply, 4 + reply.Header.Size);
            break;
        }

        case 0x0738: // Authenticated physical read, independent of config banks.
        {
            if (pUART_Command->Header.Size != 10u)
                break;
            uint32_t address, timestamp;
            uint16_t length;
            memcpy(&address, pUART_Command->Data, 4);
            memcpy(&length, pUART_Command->Data + 4, 2);
            memcpy(&timestamp, pUART_Command->Data + 6, 4);
            struct __attribute__((packed)) {
                Header_t Header;
                uint32_t Address;
                uint16_t Size;
                uint8_t Status;
                uint8_t Data[128];
            } Reply;
            Reply.Header.ID = 0x0739;
            Reply.Address = address;
            Reply.Size = 0;
            Reply.Status = MB_ERR_AUTH;
            if (timestamp == mb_port_timestamp(Port) && !(bHasCustomAesKey && gIsLocked)) {
                Reply.Status = MB_ERR_SIZE;
                if (length && length <= sizeof Reply.Data && address < 0x200000u &&
                    length <= 0x200000u - address) {
                    PY25Q16_ReadPhysical(address, Reply.Data, length);
                    if (PY25Q16_IsHealthy()) {
                        Reply.Size = length;
                        Reply.Status = MB_OK;
                    } else
                        Reply.Status = MB_ERR_SPI;
                    gSerialConfigCountDown_500ms = 12;
                }
            }
            Reply.Header.Size = 7 + Reply.Size;
            SendReply(Port, &Reply, 4 + Reply.Header.Size);
            break;
        }
        case 0x0736: // app slot validate: header only (code CRC is checked at launch)
        {
            if (pUART_Command->Header.Size < 1u)
                break;                         // needs Data[0] (slot)
            gSerialConfigCountDown_500ms = 12; // keep serial mode alive (6 s)
            uint8_t slot = pUART_Command->Data[0];
            uint8_t status = APP_ValidateSlot(slot, NULL);
            if (status == APP_OK)
                APP_NotifySlotChanged();
            struct __attribute__((packed)) {
                Header_t Header;
                uint8_t Slot;
                uint8_t Status;
            } Reply;
            Reply.Header.ID = 0x0737;
            Reply.Header.Size = 2;
            Reply.Slot = slot;
            Reply.Status = status;
            SendReply(Port, &Reply, sizeof(Reply));
            break;
        }
#endif

#ifdef ENABLE_UART_RW_BK_REGS
        case 0x0601:
            CMD_0601_ReadBK4819Reg(Port, pUART_Command->Buffer);
            break;

        case 0x0602:
            CMD_0602_WriteBK4819Reg(pUART_Command->Buffer);
            break;
#endif
    } // switch
}

void UART_ServiceCommands(void)
{
#ifdef ENABLE_USB
    if (UART_IsCommandAvailable(UART_PORT_VCP))
        UART_HandleCommand(UART_PORT_VCP);
#endif

#ifdef ENABLE_UART
    if (UART_IsCommandAvailable(UART_PORT_UART))
        UART_HandleCommand(UART_PORT_UART);
#endif
}
