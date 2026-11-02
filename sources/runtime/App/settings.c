#include "app/spectrum.h"
#include <stddef.h>
#include "app/monitor.h"
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
#include <stddef.h>

#include "frequencies.h"
#include "radio.h"
#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif
#include "driver/bk1080.h"
#include "driver/bk4819.h"
#include "driver/py25q16.h"
#include "misc.h"
#include "settings.h"
#include "ui/menu.h"
#include <stdio.h>
#include "app/spectrum.h"
#include "app/common.h"
#include "app/app.h"

EEPROM_Config_t gEeprom = {0};


// UI indices are compact; stored sound IDs stay compatible with older presets.
const uint8_t gRogerModes[6] = {0, 10, 3, 9, 4, 6};
// Keep the existing EEPROM ALL marker (51) while the UI has 25 lists.
static uint8_t SETTINGS_ScanListFromStorage(uint8_t stored)
{
    return stored == 51 ? MR_CHANNELS_LIST + 1 : stored >= 1 && stored <= MR_CHANNELS_LIST ? stored : 1;
}
static uint8_t SETTINGS_ScanListToStorage(uint8_t selected)
{
    return selected == MR_CHANNELS_LIST + 1 ? 51 : selected <= MR_CHANNELS_LIST ? selected : 1;
}

uint8_t SETTINGS_RogerIndex(uint8_t mode)
{
    for (uint8_t i = 0; i < ARRAY_SIZE(gRogerModes); ++i)
        if (gRogerModes[i] == mode)
            return i;
    return 0;
}

uint8_t *SETTINGS_ShortcutSetting(uint8_t index)
{
    _Static_assert(offsetof(EEPROM_Config_t, KEY_1_LONG_PRESS_ACTION) ==
                       offsetof(EEPROM_Config_t, KEY_1_SHORT_PRESS_ACTION) + 1,
                   "shortcut layout");
    _Static_assert(offsetof(EEPROM_Config_t, KEY_2_SHORT_PRESS_ACTION) ==
                       offsetof(EEPROM_Config_t, KEY_1_SHORT_PRESS_ACTION) + 2,
                   "shortcut layout");
    _Static_assert(offsetof(EEPROM_Config_t, KEY_2_LONG_PRESS_ACTION) ==
                       offsetof(EEPROM_Config_t, KEY_1_SHORT_PRESS_ACTION) + 3,
                   "shortcut layout");
    return (uint8_t *)&gEeprom + (index == 4 ? offsetof(EEPROM_Config_t, KEY_M_LONG_PRESS_ACTION)
                                             : offsetof(EEPROM_Config_t, KEY_1_SHORT_PRESS_ACTION) + index);
}

static void SETTINGS_UpdateByte(uint32_t address, uint8_t offset, uint8_t keep, uint8_t value)
{
    uint8_t data[8];
    PY25Q16_ReadBuffer(address, data, sizeof(data));
    data[offset] = (data[offset] & keep) | value;
    PY25Q16_WriteBuffer(address, data, sizeof(data), false);
}

void SETTINGS_SaveAutoRoger(void)
{
    SETTINGS_UpdateByte(0x00A0C0, 5, 0, gEeprom.AUTO_ROGER);
}

// Tagged additions leave legacy configuration bytes inactive until assigned.
static void SaveExtraPriorities(void)
{
    uint16_t stored[2];
    for (uint8_t i = 0; i < 2; i++)
        stored[i] = gEeprom.SCANLIST_PRIORITY_CH[i + 2] | 0xA000u;
    PY25Q16_WriteBuffer(0xA170, stored, sizeof stored, false);
}

void SETTINGS_InitEEPROM(void)
{
    uint8_t Data[32] = {0};

    // Reuse the settings signature to initialize this default configuration once.
    // A factory reset clears it; ordinary boots retain every user adjustment.
    char storedVersion[16];
    const char defaultsVersion[16] = "2.3.1.D1";
    PY25Q16_ReadBuffer(0x00A160, storedVersion, sizeof(storedVersion));
    const bool initializeDefaults = memcmp(storedVersion, defaultsVersion, sizeof(storedVersion)) != 0;
    if (initializeDefaults) {
        // Let the existing readers select their native defaults, then save them
        // below. Only general settings are cleared: memories, FM presets, list
        // names, channel attributes, the AES key and RF calibration are retained.
        memset(Data, 0xFF, sizeof(Data));
        if (storedVersion[0] == 'v')
            PY25Q16_WriteBuffer(0x0090E0, Data, 14, false);
        Data[15] = 0; // Start in VFO rather than restoring a scanner session.
        PY25Q16_WriteBuffer(0x00A000, Data, sizeof(Data), false);
        PY25Q16_WriteBuffer(0x00A0A8, Data, 8, false);
        PY25Q16_WriteBuffer(0x00A0C0, Data, 8, false);
        PY25Q16_WriteBuffer(0x00A130, Data, 8, false);
        Data[15] = 0xFF;
        PY25Q16_WriteBuffer(0x00A150, Data, sizeof(Data), false);
        PY25Q16_WriteBuffer(0x00A0B9, dBmCorrTable, sizeof(dBmCorrTable), false);
        memset(Data, 0, sizeof(Data));
        PY25Q16_WriteBuffer(0x00A0C8, Data, sizeof(Data), false);
        for (uint32_t address = 0x009008; address < 0x0090E0; address += 16)
            SETTINGS_UpdateByte(address, 4, (uint8_t)~0x1C, 0);
        SETTINGS_UpdateByte(0x00C020, 5, 0, 0);
        SETTINGS_UpdateByte(0x00C030, 0, 0, 0);
        SETTINGS_UpdateByte(0x00C030, 1, 0, 0xB1);
    }

    // 0E70..0E77
    PY25Q16_ReadBuffer(0x00A000, Data, 8);
    gSetting_set_audio_fm = ((Data[0] & 0x0F) < 5) ? (Data[0] & 0x0F) : 0;
    gSetting_set_audio_am = (((Data[0] >> 4) & 0x0F) < 3) ? ((Data[0] >> 4) & 0x0F) : 0;
    gEeprom.SQUELCH_LEVEL = (Data[1] < 10) ? Data[1] : 1;
    gEeprom.TX_TIMEOUT_TIMER = (Data[2] > 4 && Data[2] < 180) ? Data[2] : 11;
    gEeprom.SCRAMBLING_TYPE = (Data[3] > 0 && Data[3] < 11) ? Data[3] : 0;
    gEeprom.KEY_LOCK = Data[4] != 0xFF && (Data[4] & 1u);
    gEeprom.SET_NAV = (Data[4] & 0xC0u) == 0x40u;
    gEeprom.FlashlightOnRX = (Data[5] & 0x02) ? true : false;
#ifdef ENABLE_VOX
    // F4HWN keeps VOX_SWITCH in this byte; Falcon shares it with a legacy bit.
    gEeprom.VOX_SWITCH = Data[5] != 0xFF && (Data[5] & 0x01);
    gEeprom.VOX_LEVEL = (Data[6] < 10) ? Data[6] : 1;
#endif
    gEeprom.MIC_SENSITIVITY = (Data[7] < 9) ? Data[7] : 3;

    // 0E78..0E7F
    PY25Q16_ReadBuffer(0x00A008, Data, 8);
    gEeprom.BACKLIGHT_MAX = (Data[0] & 0xF) <= 10 ? (Data[0] & 0xF) : 6;
    gEeprom.BACKLIGHT_MIN = (Data[0] >> 4) < gEeprom.BACKLIGHT_MAX ? (Data[0] >> 4) : 0;
    gEeprom.CHANNEL_DISPLAY_MODE = (Data[1] < 4) ? Data[1] : MDF_NAME_FREQ;
    if ((Data[2] & 0xF0u) == 0) {
        gEeprom.RX_MODE = (Data[2] & 4u)   ? ((Data[2] & 8u) ? RX_MODE_ML : RX_MODE_FULL)
                          : (Data[2] & 1u) ? RX_MODE_DUAL
                                           : RX_MODE_SIMPLE;
        gEeprom.TONE_1750 = ((Data[2] >> 1) & 0x01u) != 0;
    } else {
        gEeprom.RX_MODE = RX_MODE_SIMPLE;
        gEeprom.TONE_1750 = true;
    }
    gEeprom.BATTERY_SAVE = (Data[3] < 6) ? Data[3] : 5;
    gEeprom.BACKLIGHT_TIME = (Data[5] < 62) ? Data[5] : 3;
#ifdef ENABLE_FEAT_F4HWN_NARROWER
    gEeprom.TAIL_TONE_ELIMINATION = Data[6] & 0x01;
    gSetting_set_nfm = Data[6] != 0xFF && ((Data[6] >> 1) & 0x01);
#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    gEeprom.VFO_OPEN = ((Data[6] >> 2) & 0x01) != 0;
#endif
#else
    gEeprom.TAIL_TONE_ELIMINATION = (Data[6] < 2) ? Data[6] : false;
#endif
    // PONSCR in bits 3..5, which F4HWN leaves unused.
    gEeprom.POWER_ON_SCREEN =
        Data[6] != 0xFF && ((Data[6] >> 3) & 0x07) < POWER_ON_SCREEN_COUNT ? (Data[6] >> 3) & 0x07 : 0;

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    gEeprom.CURRENT_STATE = Data[7] & 0x07;       // bits 0..2
    gEeprom.CURRENT_LIST = (Data[7] >> 3) & 0x1F; // bits 3..7
#else
    gEeprom.VFO_OPEN = (Data[7] < 2) ? Data[7] : true;
#endif

    // 0x00A010 .. 0x00A01F
    uint16_t Data16[8];
    PY25Q16_ReadBuffer(0x00A010, Data16, sizeof(Data16));

    const uint16_t defaultVfoA = FREQ_CHANNEL;
    const uint16_t defaultVfoB = FREQ_CHANNEL;
    gEeprom.ScreenChannel[0] = IS_VALID_CHANNEL(Data16[0]) ? Data16[0] : defaultVfoA;
    gEeprom.MrChannel[0] = IS_MR_CHANNEL(Data16[1]) ? Data16[1] : MR_CHANNEL_FIRST;
    gEeprom.FreqChannel[0] = IS_FREQ_CHANNEL(Data16[2]) ? Data16[2] : defaultVfoA;
    gEeprom.ScreenChannel[1] = IS_VALID_CHANNEL(Data16[3]) ? Data16[3] : defaultVfoB;
    gEeprom.MrChannel[1] = IS_MR_CHANNEL(Data16[4]) ? Data16[4] : MR_CHANNEL_FIRST;
    gEeprom.FreqChannel[1] = IS_FREQ_CHANNEL(Data16[5]) ? Data16[5] : defaultVfoB;

#ifdef ENABLE_FMRADIO
    { // 0E88..0E8F
        struct {
            uint16_t selFreq;
            uint8_t selChn;
            uint8_t isMrMode : 1;
            uint8_t band : 2;
            //uint8_t  space:2;
        } __attribute__((packed)) fmCfg;
        PY25Q16_ReadBuffer(0x00A020, &fmCfg, 4);

        gEeprom.FM_Band = fmCfg.band;
        //gEeprom.FM_Space = fmCfg.space;
        gEeprom.FM_SelectedFrequency = (fmCfg.selFreq >= BK1080_GetFreqLoLimit(gEeprom.FM_Band) &&
                                        fmCfg.selFreq <= BK1080_GetFreqHiLimit(gEeprom.FM_Band))
                                           ? fmCfg.selFreq
                                           : BK1080_GetFreqLoLimit(gEeprom.FM_Band);

        gEeprom.FM_SelectedChannel = fmCfg.selChn;
        gEeprom.FM_IsMrMode = fmCfg.isMrMode;
    }

    // 0E40..0E67
    PY25Q16_ReadBuffer(0x00A028, gFM_Channels, 40);
    PY25Q16_ReadBuffer(0x00A300, gFM_Channels + 20, 56);
    FM_ConfigureChannelState();
#endif

    // 0E90..0E97
    PY25Q16_ReadBuffer(0x00A0A8, Data, 8);
    gEeprom.KEY_M_LONG_PRESS_ACTION =
        ((Data[0] >> 1) < ACTION_OPT_LEN) ? (Data[0] >> 1) : ACTION_OPT_FLASHLIGHT;
    gEeprom.KEY_1_SHORT_PRESS_ACTION = (Data[1] < ACTION_OPT_LEN) ? Data[1] : ACTION_OPT_SCANLIST_UP;
    gEeprom.KEY_1_LONG_PRESS_ACTION = (Data[2] < ACTION_OPT_LEN) ? Data[2] : ACTION_OPT_MONITOR;
    gEeprom.KEY_2_SHORT_PRESS_ACTION = (Data[3] < ACTION_OPT_LEN) ? Data[3] : ACTION_OPT_SCANLIST_DOWN;
    gEeprom.KEY_2_LONG_PRESS_ACTION = (Data[4] < ACTION_OPT_LEN) ? Data[4] : ACTION_OPT_SINGLE_DUO;
    // Removed MUTE and old Hub/Manager assignments become NONE; retain stored IDs.
    for (uint8_t i = 0; i < 5; ++i) {
        const uint8_t stored = i ? Data[i] : Data[0] >> 1;
        if (stored == ACTION_OPT_MUTE || stored == 21 || stored == 22)
            *SETTINGS_ShortcutSetting(i ? i - 1 : 4) = ACTION_OPT_NONE;
    }

    gEeprom.AUTO_KEYPAD_LOCK = (Data[6] < 41) ? Data[6] : 0;
    gEeprom.POWER_ON_DISPLAY_MODE = (Data[7] < 3) ? Data[7] : POWER_ON_DISPLAY_MODE_MESSAGE;

    // 0E98..0E9F

    // 0EA0..0EA7
    PY25Q16_ReadBuffer(0x00A0A8 + 0x10, Data, 8);
#ifdef ENABLE_RSSI_BAR
    for (uint8_t i = 0; i < 7; i++) {
        int8_t val = (int8_t)Data[i + 1];
        if (val >= -64 && val <= 64)
            dBmCorrTable[i] = val;
    }
#endif

    // 0EA8..0EAF
    PY25Q16_ReadBuffer(0x00A0A8 + 0x18, Data, 8);
    gSetting_set_rxg = (Data[0] < 2) ? Data[0] : true; // SetRxG, as F4HWN v7
    gEeprom.ROGER = gRogerModes[SETTINGS_RogerIndex(Data[1])];
    APP_SetAutoRoger(Data[5] <= 6 ? Data[5] : 0);
    gEeprom.REPEATER_TAIL_TONE_ELIMINATION = (Data[2] < 11) ? Data[2] : 0;
    gEeprom.TX_VFO = (Data[3] < 2) ? Data[3] : 0;
    gEeprom.BATTERY_TYPE = (Data[4] < BATTERY_TYPE_UNKNOWN) ? Data[4] : BATTERY_TYPE_1400_MAH_SONIC;
    if (gEeprom.BATTERY_TYPE == BATTERY_TYPE_RETIRED_FALCON)
        gEeprom.BATTERY_TYPE = BATTERY_TYPE_1400_MAH_SONIC;
    gEeprom.FlashlightOnRX = false;

    // 0F18..0F1F
    PY25Q16_ReadBuffer(0x00A130, Data, 8);

    gEeprom.SCAN_LIST_DEFAULT = SETTINGS_ScanListFromStorage(Data[0] & 0x7F);
    gEeprom.SCAN_LIST_ENABLED = (Data[0] >> 7) & 0x01;

    gEeprom.SCANLIST_PRIORITY_CH[0] = (uint16_t)Data[1] | ((uint16_t)Data[2] << 8);

    gEeprom.SCANLIST_PRIORITY_CH[1] = (uint16_t)Data[3] | ((uint16_t)Data[4] << 8);

    PY25Q16_ReadBuffer(0xA170, Data, 4);
    for (uint8_t i = 0; i < 2; i++) {
        uint16_t v = (uint16_t)Data[i * 2] | ((uint16_t)Data[i * 2 + 1] << 8);
        gEeprom.SCANLIST_PRIORITY_CH[i + 2] = (v & 0xE000u) == 0xA000u ? v & 0x1FFFu : MR_CHANNELS_MAX;
    }
    // 0F40..0F47
    PY25Q16_ReadBuffer(0x00A150, Data, 8);
    gSetting_F_LOCK = (Data[0] < F_LOCK_LEN) ? Data[0] : F_LOCK_136_500;
    gSetting_led_mode = Data[7] == 0xFF ? 1 : (Data[7] & 3u);
    gSetting_battery_text = (Data[7] >> 2) & 3u; // 0..3; erased memory (0xFF) defaults to VOLT + PERCENT
#ifdef ENABLE_AUDIO_BAR
    gSetting_mic_bar = !!(Data[7] & (1u << 4));
#endif
    gSetting_backlight_on_tx_rx = (Data[7] >> 6) & 3u;
    // A separate marker prevents an older saved option enabling Classic.
    gEeprom.METER_CLASSIC = Data[1] == 0xFF || (Data[1] == 0x4D && !!(Data[7] & (1u << 5)));

    // Init list name
    for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i) {
        PY25Q16_ReadBuffer(0x008900 + i * 10, gListName[i], 6);
        gListName[i][6] = 0;
    }

    // Init attr cache
    MR_InitChannelAttributesCache();
    MONITOR_Invalidate();

    // Load and check channel
    for (uint16_t i = 0; i < MR_CHANNELS_MAX + 7; i++) {
        ChannelAttributes_t *att = MR_GetChannelAttributes(i);

        if (att != NULL) {
            if (att->__val == 0xFFFF) {
                att->__val = 0;
                att->band = 0x7;
                MR_SetChannelAttributes(i, att); // ⭐ IMPORTANT: Sauvegarder!
            } else {
                att->exclude = 0;
                MR_SetChannelAttributes(i, att); // ⭐ IMPORTANT: Sauvegarder!
            }
        }
    }

    // 0F30..0F3F
    PY25Q16_ReadBuffer(0x00A138, gCustomAesKey, sizeof(gCustomAesKey));
    bHasCustomAesKey = false;
#ifndef ENABLE_FEAT_F4HWN
    for (unsigned int i = 0; i < ARRAY_SIZE(gCustomAesKey); i++) {
        if (gCustomAesKey[i] != 0xFFFFFFFFu) {
            bHasCustomAesKey = true;
            return;
        }
    }
#endif

#ifdef ENABLE_FEAT_F4HWN
    // 1FF0..0x1FF7
    // TODO: address TBD
    PY25Q16_ReadBuffer(0x00A158, Data, 8);
    gSetting_set_pwr = (((Data[7] & 0xF0) >> 4) < 7) ? ((Data[7] & 0xF0) >> 4) : 0;
    gSetting_set_ptt = (((Data[7] & 0x0F)) < 2) ? ((Data[7] & 0x0F)) : 0;
    gSetting_set_tot = (((Data[6] & 0xF0) >> 4) < 4) ? ((Data[6] & 0xF0) >> 4) : 0;
    gSetting_set_eot = (((Data[6] & 0x0F)) < 4) ? ((Data[6] & 0x0F)) : 0;
    int tmp = Data[5] == 0xFF ? 2 : (Data[5] & 0xF0) >> 4;

#ifdef ENABLE_FEAT_F4HWN_INV
    gSetting_set_inv = (tmp >> 0) & 0x01;
#else
    gSetting_set_inv = 0;
#endif
    gSetting_set_lck = (tmp >> 1) & 0x01;
    gSetting_set_met = (tmp >> 2) & 0x01;
    gSetting_set_gui = (tmp >> 3) & 0x01;

    gSetting_set_ctr = Data[5] & 0x0F;

    gSetting_set_tmr = Data[4] & 0x01;
    gSetting_nav_invert = Data[4] != 0xFF && ((Data[4] >> 2) & 0x01);

    // Warning
    // Be aware, Data[3] is use by Spectrum
    // Warning

    // And set special session settings for actions
    gSetting_set_ptt_session = gSetting_set_ptt;
    gEeprom.KEY_LOCK_PTT = gSetting_set_lck;
#endif
    APP_LoadScanSQL();
    if (initializeDefaults) {
        SETTINGS_SaveSettings();
        // Commit last so an interrupted installation retries initialization.
        PY25Q16_WriteBuffer(0x00A160, defaultsVersion, sizeof(defaultsVersion), false);
    }
}

void SETTINGS_LoadCalibration(void)
{
    //  uint8_t Mic;

    // 0x1EC0
    PY25Q16_ReadBuffer(0x010000 + 0xc0, gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[4], gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[5], gEEPROM_RSSI_CALIB[3], 8);
    memcpy(gEEPROM_RSSI_CALIB[6], gEEPROM_RSSI_CALIB[3], 8);

    // 0x1EC8
    PY25Q16_ReadBuffer(0x010000 + 0xc8, gEEPROM_RSSI_CALIB[0], 8);
    memcpy(gEEPROM_RSSI_CALIB[1], gEEPROM_RSSI_CALIB[0], 8);
    memcpy(gEEPROM_RSSI_CALIB[2], gEEPROM_RSSI_CALIB[0], 8);

    // 0x1F40
    PY25Q16_ReadBuffer(0x010000 + 0x140, gBatteryCalibration, 12);
    if (gBatteryCalibration[0] >= 5000) {
        gBatteryCalibration[0] = 1900;
        gBatteryCalibration[1] = 2000;
    }
    gBatteryCalibration[5] = 2300;

#ifdef ENABLE_VOX
    // 0x1F50
    PY25Q16_ReadBuffer(0x010000 + 0x150 + (gEeprom.VOX_LEVEL * 2), &gEeprom.VOX1_THRESHOLD, 2);
    // 0x1F68
    PY25Q16_ReadBuffer(0x010000 + 0x168 + (gEeprom.VOX_LEVEL * 2), &gEeprom.VOX0_THRESHOLD, 2);
#endif

    //PY25Q16_ReadBuffer(0x1F80 + gEeprom.MIC_SENSITIVITY, &Mic, 1);
    //gEeprom.MIC_SENSITIVITY_TUNING = (Mic < 32) ? Mic : 15;
    gEeprom.MIC_SENSITIVITY_TUNING = gMicGain_dB2[gEeprom.MIC_SENSITIVITY];

    {
        struct {
            int16_t BK4819_XtalFreqLow;
            uint16_t EEPROM_1F8A;
            uint16_t EEPROM_1F8C;
            uint8_t VOLUME_GAIN;
            uint8_t DAC_GAIN;
        } __attribute__((packed)) Misc;

        // radio 1 .. 04 00 46 00 50 00 2C 0E
        // radio 2 .. 05 00 46 00 50 00 2C 0E
        // 0x1F88
        PY25Q16_ReadBuffer(0x010000 + 0x188, &Misc, 8);

        gEeprom.BK4819_XTAL_FREQ_LOW = (Misc.BK4819_XtalFreqLow >= -1000 && Misc.BK4819_XtalFreqLow <= 1000)
                                           ? Misc.BK4819_XtalFreqLow
                                           : 0;
        gEEPROM_1F8A = Misc.EEPROM_1F8A & 0x01FF;
        gEEPROM_1F8C = Misc.EEPROM_1F8C & 0x01FF;
        gEeprom.VOLUME_GAIN = (Misc.VOLUME_GAIN < 64) ? Misc.VOLUME_GAIN : 50;
        gEeprom.DAC_GAIN = (Misc.DAC_GAIN < 16) ? Misc.DAC_GAIN : 8;

#ifdef ENABLE_FEAT_F4HWN
        gEeprom.VOLUME_GAIN_BACKUP = gEeprom.VOLUME_GAIN;
#endif

        BK4819_WriteRegister(BK4819_REG_3B, 22656 + gEeprom.BK4819_XTAL_FREQ_LOW);
        //      BK4819_WriteRegister(BK4819_REG_3C, gEeprom.BK4819_XTAL_FREQ_HIGH);
    }
}

uint32_t SETTINGS_FetchChannelFrequency(const uint16_t channel)
{
    struct {
        uint32_t frequency;
        uint32_t offset;
    } __attribute__((packed)) info;

    PY25Q16_ReadBuffer(ADRESS_CHANNELS + channel * 16, &info, sizeof(info));

    return info.frequency;
}

void SETTINGS_FetchChannelName(char *s, const uint16_t channel)
{
    if (s == NULL)
        return;
    s[0] = 0;
    if (!RADIO_CheckValidChannel(channel, false, 0))
        return;
    PY25Q16_ReadBuffer(ADRESS_CHANNELS_NAMES + (channel * 16), s, CHANNEL_NAME_LEN);
    int i;
    for (i = 0; i < CHANNEL_NAME_LEN; i++)
        if (s[i] < 32 || s[i] > 127)
            break;               // invalid char
    s[i--] = 0;                  // null term
    while (i >= 0 && s[i] == 32) // trim trailing spaces
        s[i--] = 0;              // null term
    if (s[0] == 0) {
        sprintf(s, "CH%u", channel + 1);
    }
}

void SETTINGS_FactoryReset(bool bIsAll)
{
    PY25Q16_SectorErase(0x00A000);
    if (bIsAll) {
        for (uint32_t addr = 0x000000; addr < 0x00E000; addr += 0x1000) {
            PY25Q16_SectorErase(addr);
        }
#if defined(ENABLE_10K)
        for (uint32_t addr = 0x14C000; addr < 0x19F6C5; addr += 0x1000) {
            PY25Q16_SectorErase(addr);
        }
#elif defined(ENABLE_4096)
        for (uint32_t addr = ADRESS_CHANNELS; addr < ADRESS_CHANNELS_ATTRIBUTES + 0x2000; addr += 0x1000) {
            PY25Q16_SectorErase(addr);
        }
#endif
    }
    // Reset VFO defaults
    RADIO_InitInfo(&gEeprom.VfoInfo[0], FREQ_CHANNEL, 44609775);
    RADIO_InitInfo(&gEeprom.VfoInfo[1], FREQ_CHANNEL, 43350000);
    gEeprom.ScreenChannel[0] = FREQ_CHANNEL;
    gEeprom.ScreenChannel[1] = FREQ_CHANNEL;
    gEeprom.FreqChannel[0] = FREQ_CHANNEL;
    gEeprom.FreqChannel[1] = FREQ_CHANNEL;
    gEeprom.MrChannel[0] = MR_CHANNEL_FIRST;
    gEeprom.MrChannel[1] = MR_CHANNEL_FIRST;
    gEeprom.RX_VFO = 0;
    gEeprom.TX_VFO = 0;
    gEeprom.RX_MODE = RX_MODE_SIMPLE;
    gEeprom.METER_CLASSIC = true;
    gEeprom.TONE_1750 = true;
    SETTINGS_SaveChannel(FREQ_CHANNEL, 0, &gEeprom.VfoInfo[0], 2);
    SETTINGS_SaveChannel(FREQ_CHANNEL, 1, &gEeprom.VfoInfo[1], 2);
    gVfoStateChanged = true;
    gScheduleVfoSave = true;
    SETTINGS_SaveVfoIndicesFlush();
    ClearSettings();
}

#ifdef ENABLE_FMRADIO
void SETTINGS_SaveFM(void)
{
    union {
        struct {
            uint16_t selFreq;
            uint8_t selChn;
            uint8_t isMrMode : 1;
            uint8_t band : 2;
            //uint8_t  space:2;
        };
        uint8_t __raw[8];
    } __attribute__((packed)) fmCfg;

    memset(fmCfg.__raw, 0xFF, sizeof(fmCfg.__raw));
    fmCfg.selChn = gEeprom.FM_SelectedChannel;
    fmCfg.selFreq = gEeprom.FM_SelectedFrequency;
    fmCfg.isMrMode = gEeprom.FM_IsMrMode;
    fmCfg.band = gEeprom.FM_Band;
    // fmCfg.space    = gEeprom.FM_Space;
    // 0E88
    PY25Q16_WriteBuffer(0x00A020, fmCfg.__raw, 8, false);

    // 0E40
    PY25Q16_WriteBuffer(0x00A028, gFM_Channels, 40, false);
    PY25Q16_WriteBuffer(0x00A300, gFM_Channels + 20, 56, false);
}
#endif

/* The VFO state (channel numbers and frequency-mode records) is written 3 s
 * after its last change: a series of steps or channel changes costs one
 * erase of its flash sector instead of one each. Until then a frequency-mode
 * record waits in RAM, where RADIO_LoadChannel reads it. SETTINGS_SaveVfoNow
 * writes at once (PTT, menu, scanners, apps, a PC reading the radio). */
#define VFO_SAVE_DELAY_10MS 300u
static uint8_t vfoRecordPending; // bit n: VFO n's record waits in vfoRecord
static uint8_t vfoRecord[2][16];

void SETTINGS_SaveVfoIndices(void)
{
    gVfoStateChanged = true;
    gVfoSaveCountdown_10ms = VFO_SAVE_DELAY_10MS;
}

void SETTINGS_ReadVfoRecord(uint8_t vfo, uint8_t offset, void *buffer, uint8_t size)
{
    vfo &= 1u;
    if (vfoRecordPending >> vfo & 1u)
        memcpy(buffer, vfoRecord[vfo] + offset, size);
    else
        PY25Q16_ReadBuffer(0x009000 + vfo * 0x10u + offset, buffer, size);
}

void SETTINGS_SaveVfoNow(void)
{
    if (gVfoStateChanged || vfoRecordPending) {
        gVfoSaveCountdown_10ms = 0;
        gScheduleVfoSave = true;
        SETTINGS_SaveVfoIndicesFlush();
    }
}

void SETTINGS_SaveVfoIndicesFlush(void)
{
    if (gScheduleVfoSave) {
        gScheduleVfoSave = false;

        for (uint8_t vfo = 0; vfo < 2; vfo++)
            if (vfoRecordPending >> vfo & 1u)
                PY25Q16_WriteBuffer(0x009000 + vfo * 0x10u, vfoRecord[vfo], 0x10, false);
        vfoRecordPending = 0;

        if (gVfoStateChanged) {
            gVfoStateChanged = false;
            uint16_t Data16[8];
            Data16[0] = gEeprom.ScreenChannel[0];
            Data16[1] = gEeprom.MrChannel[0];
            Data16[2] = gEeprom.FreqChannel[0];
            Data16[3] = gEeprom.ScreenChannel[1];
            Data16[4] = gEeprom.MrChannel[1];
            Data16[5] = gEeprom.FreqChannel[1];
            Data16[6] = 0xFFFF;
            Data16[7] = 0xFFFF;
            PY25Q16_WriteBuffer(0x00A010, Data16, sizeof(Data16), false);
        }
    }
}

void SETTINGS_SaveSettings(void)
{
    APP_SaveScanSQL();
    uint8_t *State;
    uint8_t tmp = 0;
    uint8_t SecBuf[0x50];

    // ----------------------
    // 0e70 - 0e80

    memset(SecBuf, 0xff, 0x10);

    // 0x0E70
    State = SecBuf;
    State[0] = (gSetting_set_audio_fm & 0x0F) | ((gSetting_set_audio_am & 0x0F) << 4);
#ifdef ENABLE_FEAT_F4HWN
    if (gSquelchLevelOriginal < 10)
        State[1] = gSquelchLevelOriginal;
    else
#endif
        State[1] = gEeprom.SQUELCH_LEVEL;
    State[2] = gEeprom.TX_TIMEOUT_TIMER;
    State[3] = gEeprom.SCRAMBLING_TYPE;
    State[4] = (gEeprom.KEY_LOCK ? 1u : 0u) | (gEeprom.SET_NAV ? 0x40u : 0u);
#ifdef ENABLE_VOX
    State[5] = (gEeprom.FlashlightOnRX ? 0x02 : 0x00) | gEeprom.VOX_SWITCH;
    State[6] = gEeprom.VOX_LEVEL;
#else
    State[5] = (gEeprom.FlashlightOnRX ? 0x02 : 0x00);
    State[6] = 0;
#endif
    State[7] = gEeprom.MIC_SENSITIVITY;

    // 0x0E78
    State = SecBuf + 0x8;
    State[0] = (gEeprom.BACKLIGHT_MIN << 4) + gEeprom.BACKLIGHT_MAX;
    State[1] = gEeprom.CHANNEL_DISPLAY_MODE;
    State[2] = (gEeprom.RX_MODE == RX_MODE_ML     ? 12u
                : gEeprom.RX_MODE == RX_MODE_FULL ? 4u
                : gEeprom.RX_MODE == RX_MODE_DUAL ? 1u
                                                  : 0u) |
               (gEeprom.TONE_1750 ? 2u : 0u);
    State[3] = gEeprom.BATTERY_SAVE;

#ifdef ENABLE_FEAT_F4HWN
    if (gBackLight) {
        State[5] = gBacklightTimeOriginal;
    } else {
        State[5] = gEeprom.BACKLIGHT_TIME;
    }
#else
    State[5] = gEeprom.BACKLIGHT_TIME;
#endif

#ifdef ENABLE_FEAT_F4HWN_NARROWER
    State[6] = (gEeprom.TAIL_TONE_ELIMINATION & 0x01) | ((gSetting_set_nfm & 0x01) << 1)
#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
               | ((gEeprom.VFO_OPEN & 0x01) << 2)
#endif
        ;
#else
    State[6] = gEeprom.TAIL_TONE_ELIMINATION;
#endif
    State[6] |= (gEeprom.POWER_ON_SCREEN & 0x07) << 3; // PONSCR

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    State[7] = (gEeprom.CURRENT_STATE & 0x07) | ((gEeprom.SCAN_LIST_DEFAULT & 0x1F) << 3);
#else
    State[7] = gEeprom.VFO_OPEN;
#endif

    PY25Q16_WriteBuffer(0x00A000, SecBuf, 0x10, false);

    // -------------------------
    //  0e90 - 0ee0

    // memset(SecBuf, 0xff, 0x50);
    PY25Q16_ReadBuffer(0x00A0A8, SecBuf, 0x50);

    // 0x0E90
    State = SecBuf;
    State[0] = (State[0] & 1u) | (gEeprom.KEY_M_LONG_PRESS_ACTION << 1);
    State[1] = gEeprom.KEY_1_SHORT_PRESS_ACTION;
    State[2] = gEeprom.KEY_1_LONG_PRESS_ACTION;
    State[3] = gEeprom.KEY_2_SHORT_PRESS_ACTION;
    State[4] = gEeprom.KEY_2_LONG_PRESS_ACTION;
    State[6] = gEeprom.AUTO_KEYPAD_LOCK;
    State[7] = gEeprom.POWER_ON_DISPLAY_MODE;

    // 0x0E98

    // 0x0EA0
    State = SecBuf + 0x10;
    // State[1..7] contain dBm corrections: preserve the bytes read above.

    // 0x0EA8
    State = SecBuf + 0x18;
    State[0] = gSetting_set_rxg;
    State[1] = gEeprom.ROGER;
    State[2] = gEeprom.REPEATER_TAIL_TONE_ELIMINATION;
    State[3] = gEeprom.TX_VFO;
    State[4] = gEeprom.BATTERY_TYPE;
    State[5] = gEeprom.AUTO_ROGER;

    PY25Q16_WriteBuffer(0x00A0A8, SecBuf, 0x50, false);

    // -------------------------
    // 0f18 - 0f20

    memset(SecBuf, 0xff, 0x08);

    // 0x0F18
    State = SecBuf;

    State[0] = (SETTINGS_ScanListToStorage(gEeprom.SCAN_LIST_DEFAULT) & 0x7F) |
               ((gEeprom.SCAN_LIST_ENABLED & 0x01) << 7);

    State[1] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[0] & 0xFF);
    State[2] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[0] >> 8);

    State[3] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[1] & 0xFF);
    State[4] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[1] >> 8);

    PY25Q16_WriteBuffer(0x00A130, SecBuf, 0x08, false);
    SaveExtraPriorities();

    // ---------------------
    // 0f40 - 0f48

    memset(SecBuf, 0xff, 8);

    // 0x0F40
    State = SecBuf;
    State[0] = gSetting_F_LOCK;
    State[1] = 0x4D; // Meter style initialized (reserved Sonic byte)
    State[7] = (State[7] & ~3u) | (gSetting_led_mode & 3u);
    State[7] = (State[7] & ~(3u << 2)) | ((gSetting_battery_text & 3u) << 2);
#ifdef ENABLE_AUDIO_BAR
    if (!gSetting_mic_bar)
        State[7] &= ~(1u << 4);
#endif
    if (!gEeprom.METER_CLASSIC)
        State[7] &= ~(1u << 5);
    State[7] = (State[7] & ~(3u << 6)) | ((gSetting_backlight_on_tx_rx & 3u) << 6);

    PY25Q16_WriteBuffer(0x00A150, SecBuf, 8, false);

    // ------------------

#ifdef ENABLE_FEAT_F4HWN
    // 0x1FF0
    State = SecBuf;
    // TODO: TBD
    PY25Q16_ReadBuffer(0x00A158, State, 8);

    //memset(State, 0xFF, sizeof(State));

    /*
    tmp = 0;

    if(gSetting_set_tmr == 1)
        tmp = tmp | (1 << 0);

    State[4] = tmp;

    tmp = 0;

    if(gSetting_set_inv == 1)
        tmp = tmp | (1 << 0);
    if (gSetting_set_lck == 1)
        tmp = tmp | (1 << 1);
    if (gSetting_set_met == 1)
        tmp = tmp | (1 << 2);
    if (gSetting_set_gui == 1)
        tmp = tmp | (1 << 3);
    */

    State[4] = (gSetting_set_tmr ? (1 << 0) : 0) | (gSetting_nav_invert << 2);

    tmp =
        (gSetting_set_inv << 0) | (gSetting_set_lck << 1) | (gSetting_set_met << 2) | (gSetting_set_gui << 3);

    State[5] = ((tmp << 4) | (gSetting_set_ctr & 0x0F));
    State[6] = ((gSetting_set_tot << 4) | (gSetting_set_eot & 0x0F));
    State[7] = ((gSetting_set_pwr << 4) | (gSetting_set_ptt & 0x0F));
    gEeprom.KEY_LOCK_PTT = gSetting_set_lck;

    PY25Q16_WriteBuffer(0x00A158, SecBuf, 8, false);
#endif
}

// Shortcut edits preserve every unrelated memory byte and channel attribute.
// These fields use the same layout as RADIO_ConfigureChannel and CHIRP.
const char gPowerShort[OUTPUT_POWER_LEN][3] = {"L1", "M", "H", "L2", "L3", "L4", "L5"};
const char gPowerNames[OUTPUT_POWER_LEN][7] = {"LOW 1", "MID", "HIGH", "LOW 2", "LOW 3", "LOW 4", "LOW 5"};
const char gLowPowerWatts[5][9] = {"< 20mW", "125mW", "250mW", "500mW", "1W"};

void SETTINGS_SaveChannelParameters(uint8_t VFO, const VFO_Info_t *pVFO, uint8_t fields)
{
    const uint16_t channel = pVFO->CHANNEL_SAVE;
    if (IS_FREQ_CHANNEL(channel)) {
        SETTINGS_SaveChannel(channel, VFO, pVFO, 2);
        return;
    }
    if (!IS_MR_CHANNEL(channel))
        return;

    const uint32_t address = (uint32_t)ADRESS_CHANNELS + (uint32_t)channel * 16U + 8U;
    uint8_t data[8];
    PY25Q16_ReadBuffer(address, data, sizeof(data));
    static const uint8_t parameters[5][4] = {{6, 255, 0, offsetof(VFO_Info_t, STEP_SETTING)},
                                             {4, 2, 1, offsetof(VFO_Info_t, CHANNEL_BANDWIDTH)},
                                             {4, 28, 2, offsetof(VFO_Info_t, OUTPUT_POWER)},
                                             {3, 240, 4, offsetof(VFO_Info_t, Modulation)},
                                             {4, 1, 0, offsetof(VFO_Info_t, FrequencyReverse)}};
    for (int i = 4; i >= 0; --i)
        if (fields & (1u << i)) {
            const uint8_t *p = parameters[i];
            const uint8_t value = ((const uint8_t *)pVFO)[p[3]];
            data[p[0]] = (data[p[0]] & ~p[1]) | (value << p[2]);
        }
    PY25Q16_WriteBuffer(address, data, sizeof(data), false);
}

void SETTINGS_SaveChannel(uint16_t Channel, uint8_t VFO, const VFO_Info_t *pVFO, uint8_t Mode)
{
    // 0
    uint32_t OffsetVFO = (uint32_t)ADRESS_CHANNELS + ((uint32_t)Channel * 16U);

    if (IS_FREQ_CHANNEL(Channel)) { // it's a VFO, not a channel
        // 0x0C80
        OffsetVFO = 0x009000 + ((uint32_t)VFO * 0x10U);
    }

    if (Mode >= 2 || IS_FREQ_CHANNEL(Channel)) { // copy VFO to a channel
        typedef union {
            uint8_t _8[8];
            uint32_t _32[2];
        } State_t;

        State_t *State;

        uint8_t Buf[0x10] = {0};

        State = (State_t *)Buf;
        State->_32[0] = pVFO->freq_config_RX.Frequency;
        State->_32[1] = pVFO->TX_OFFSET_FREQUENCY;

        State = (State_t *)(Buf + 0x8);
        State->_8[0] = pVFO->freq_config_RX.Code;
        State->_8[1] = pVFO->freq_config_TX.Code;
        State->_8[2] = (pVFO->freq_config_TX.CodeType << 4) | pVFO->freq_config_RX.CodeType;
        State->_8[3] = (pVFO->Modulation << 4) | pVFO->TX_OFFSET_FREQUENCY_DIRECTION;
        State->_8[4] =
            0 | (pVFO->BUSY_CHANNEL_LOCK << 5) | (pVFO->OUTPUT_POWER << 2) | (pVFO->CHANNEL_BANDWIDTH << 1) |
            ((IS_FREQ_CHANNEL(Channel) && pVFO->ExactFrequency) ? 128 : 0) | (pVFO->FrequencyReverse << 0);
        ;
        State->_8[6] = pVFO->STEP_SETTING;

        if (IS_FREQ_CHANNEL(Channel)) { // written with the VFO state, 3 s later
            memcpy(vfoRecord[VFO & 1u], Buf, sizeof Buf);
            vfoRecordPending |= 1u << (VFO & 1u);
            gVfoSaveCountdown_10ms = VFO_SAVE_DELAY_10MS;
        } else
            PY25Q16_WriteBuffer(OffsetVFO, Buf, 0x10, false);

        SETTINGS_UpdateChannel(Channel, pVFO, true);

        if (IS_MR_CHANNEL(Channel)) {
#ifndef ENABLE_KEEP_MEM_NAME
            // clear/reset the channel name
            SETTINGS_SaveChannelName(Channel, "");
#else
            if (Mode >= 3) {
                SETTINGS_SaveChannelName(Channel, pVFO->Name);
            }
#endif
        }
    }
}

void SETTINGS_SaveBatteryCalibration(const uint16_t *batteryCalibration)
{
    // 0x1F40
    PY25Q16_WriteBuffer(0x010000 + 0x140, batteryCalibration, 12, false);
}

void SETTINGS_SaveChannelName(uint16_t channel, const char *name)
{
    uint32_t offset = channel * 16;
    uint8_t buf[16] = {0};
    memcpy(buf, name, MIN(strlen(name), CHANNEL_NAME_LEN));
    // 0x0F50
    PY25Q16_WriteBuffer(ADRESS_CHANNELS_NAMES + offset, buf, 0x10, false);
}

void SETTINGS_UpdateChannel(uint16_t channel, const VFO_Info_t *pVFO, bool keep)
{
    const ChannelAttributes_t *old = IS_MR_CHANNEL(channel) ? MR_GetChannelAttributes(channel) : NULL;
    const bool monitorChanged = old && (old->scanlist == 51 || (keep && pVFO->SCANLIST_PARTICIPATION == 51));

    ChannelAttributes_t att = {
        .band = 0x7,
        .compander = 0,
        .unused_1 = 0,
        .unused_2 = 0,
        .exclude = 0,
        .scanlist = 0,
    };

    if (keep) {
        att.band = pVFO->Band;
        att.compander = pVFO->Compander;
        att.scanlist = pVFO->SCANLIST_PARTICIPATION;
    }

    MR_SetChannelAttributes(channel, &att);
    if (monitorChanged)
        MONITOR_Invalidate();

    if (IS_MR_CHANNEL(channel) && !keep)
        SETTINGS_SaveChannelName(channel, "");
}

void SETTINGS_WriteBuildOptions(void)
{
    uint8_t State[8];

#ifdef ENABLE_FEAT_F4HWN
    // 0x1FF0
    PY25Q16_ReadBuffer(0x00A158, State, sizeof(State));
#endif

    State[0] = 0
#ifdef ENABLE_VOX
               | (1 << 3)
#endif
#ifdef ENABLE_FMRADIO
               | (1 << 0)
#endif
        ;

    State[1] = 0
#ifdef ENABLE_FLASHLIGHT
               | (1 << 0)
#endif

#ifdef ENABLE_SPECTRUM
               | (1 << 5)
#endif
        ;
    PY25Q16_WriteBuffer(0x00A158, State, sizeof(State), false);
}

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
void SETTINGS_WriteCurrentState(void)
{
    uint8_t State[0x08];
    PY25Q16_ReadBuffer(0x00A008, State, sizeof(State));
    State[7] = (gEeprom.CURRENT_STATE & 0x07) | ((gEeprom.SCAN_LIST_DEFAULT & 0x1F) << 3);
    PY25Q16_WriteBuffer(0x00A008, State, sizeof(State), false);
    PY25Q16_ReadBuffer(0x00A130, State, sizeof(State));
    State[0] = (SETTINGS_ScanListToStorage(gEeprom.SCAN_LIST_DEFAULT) & 0x7F) |
               ((gEeprom.SCAN_LIST_ENABLED & 0x01) << 7);
    State[1] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[0] & 0xFF);
    State[2] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[0] >> 8);
    State[3] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[1] & 0xFF);
    State[4] = (uint8_t)(gEeprom.SCANLIST_PRIORITY_CH[1] >> 8);
    PY25Q16_WriteBuffer(0x00A130, State, sizeof(State), false);
    SaveExtraPriorities();
}
#endif
