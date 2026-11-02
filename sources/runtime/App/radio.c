#include "app/spectrum.h"
/* Copyright 2025 OURO.SU
 * https://github.com/igimalek
 */

#include "driver/bk4819-regs.h"
#include <string.h>


#ifdef ENABLE_FMRADIO
#include "app/fm.h"
#endif

#include "dcs.h"
#include "driver/bk4819.h"
#include "driver/py25q16.h"
#include "driver/gpio.h"
#include "driver/system.h"
#include "frequencies.h"
#include "functions.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
#include "app/rxtx_log.h"
#endif
#include "app/app.h"
#include "app/spectrum.h"
#include "settings.h"
#include "ui/menu.h"

//#include "debugging.h"

VFO_Info_t *gTxVfo;
VFO_Info_t *gRxVfo;
VFO_Info_t *gCurrentVfo;
DCS_CodeType_t gCurrentCodeType;
VfoState_t VfoState[2];

const char gModulationStr[MODULATION_UKNOWN][4] = {
    [MODULATION_FM] = "FM",
    [MODULATION_AM] = "AM",
    [MODULATION_USB] = "USB",

};

static void AUDIO_ApplyFMProfile(uint8_t profile)
{ //  | 0x54 || 0x55 |
    static const uint16_t fm_profiles[][2] = {
        {0x9009, 0x3200}, // 0: FLAT
        {0x9009, 0x33A9}, // 1: CLEAN
        {0x9009, 0x3600}, // 2: MID
        {0x8546, 0x3AF0}, // 3: BOOST
        {0x8566, 0x3D00}  // 4: MAX
    };

    if (profile >= ARRAY_SIZE(fm_profiles))
        profile = 0;

    BK4819_WriteRegister(0x54, fm_profiles[profile][0]);
    BK4819_WriteRegister(0x55, fm_profiles[profile][1]);
}

static void AUDIO_ApplyAMProfile(uint8_t profile)
{ //  | 0x2b || 0x2f || 0x54 || 0x55 |
    static const uint16_t am_profiles[][4] = {
        // SHARP (ALPHA test profile) - Narrow IF filter (REG54 bits[14:8]=0, bits[7:0]=9), low IF gain (REG55 bits[11:8]=1, ref=169)
        // Selective and crisp, best adjacent channel rejection, may sound harsh on strong signals
        {0x0300, 0x9990, 0x9009, 0x31A9},

        // STOCK - Narrow IF filter (REG54 bits[14:8]=0, bits[7:0]=9), moderate IF gain (REG55 bits[11:8]=4, ref=180)
        // Selective filter with balanced gain, punchy and detailed, good compromise between rejection and sensitivity
        {0x0500, 0x9990, 0x9009, 0x31A9},

        // OPEN (BRAVO test profile) - Medium-wide IF filter (REG54 bits[14:8]=8, bits[7:0]=70), high IF gain (REG55 bits[11:8]=8, ref=192)
        // Wide and pleasant, better sensitivity on weak signals, may struggle with adjacent channel interference
        {0x0300, 0x9990, 0x8846, 0x38C0}};

    if (profile >= ARRAY_SIZE(am_profiles))
        profile = 0;

    BK4819_WriteRegister(0x2b, am_profiles[profile][0]);
    BK4819_WriteRegister(0x2f, am_profiles[profile][1]);
    BK4819_WriteRegister(0x54, am_profiles[profile][2]);
    BK4819_WriteRegister(0x55, am_profiles[profile][3]);
}

static void AUDIO_ApplyUSBProfile(void)
{
    BK4819_WriteRegister(0x54, 0x9009);
    BK4819_WriteRegister(0x55, 0x31A9);
}

bool RADIO_CheckValidList(uint8_t scanList)
{
    if (scanList == MR_CHANNELS_LIST + 1)
        return true;

    for (uint16_t i = 0; IS_MR_CHANNEL(i); i++) {
        const ChannelAttributes_t *att = MR_GetChannelAttributes(i);
        if (att->scanlist == scanList && att->exclude == false) {
            return true;
        }
    }
    return false;
}

bool RADIO_CheckValidChannel(uint16_t channel, bool checkScanList, uint8_t scanList)
{
    const ChannelAttributes_t *att = MR_GetChannelAttributes(channel);

    // return true if the channel appears valid
    if (!IS_MR_CHANNEL(channel))
        return false;
    if (checkScanList && att->exclude == true)
        return false;
    if (att->band > BAND7_470MHz)
        return false;
    if (!checkScanList ||
        (scanList > MR_CHANNELS_LIST && (att->scanlist != 0 || channel < MR_CHANNELS_MAX - MAX_BANDS)) ||
        (scanList > 0 && att->scanlist == 51))
        return true;
    if ((scanList == 0) || (scanList != att->scanlist)) {
        return false;
    }

    // Exclude priority channels ONLY if SCAN_LIST_ENABLED is active
    // Otherwise, treat them as normal channels in the list
    if (gEeprom.SCAN_LIST_ENABLED) {
        const uint16_t PriorityCh1 = gEeprom.SCANLIST_PRIORITY_CH[0];
        const uint16_t PriorityCh2 = gEeprom.SCANLIST_PRIORITY_CH[1];
        if (PriorityCh1 == channel || PriorityCh2 == channel)
            return false; // Excluded because it's a priority channel and they are enabled
    }

    return true;
}

uint16_t RADIO_FindNextChannel(uint16_t Channel, int8_t Direction, bool bCheckScanList, uint8_t VFO)
{
    for (uint16_t i = 0; IS_MR_CHANNEL(i); i++, Channel += Direction) {
        if (Channel == 0xFFFF) {
            Channel = MR_CHANNEL_LAST;
        } else if (!IS_MR_CHANNEL(Channel)) {
            Channel = MR_CHANNEL_FIRST;
        }

        if (RADIO_CheckValidChannel(Channel, bCheckScanList, VFO)) {
            return Channel;
        }
    }

    return 0xFFFF;
}

void RADIO_InitInfo(VFO_Info_t *pInfo, const uint16_t ChannelSave, const uint32_t Frequency)
{
    memset(pInfo, 0, sizeof(*pInfo));

    pInfo->Band = FREQUENCY_GetBand(Frequency);
    pInfo->STEP_SETTING = STEP_12_5kHz;
    pInfo->StepFrequency = gStepFrequencyTable[pInfo->STEP_SETTING];
    pInfo->CHANNEL_SAVE = ChannelSave;
    pInfo->OUTPUT_POWER = OUTPUT_POWER_LOW;
    pInfo->freq_config_RX.Frequency = Frequency;
    pInfo->freq_config_TX.Frequency = Frequency;
    pInfo->pRX = &pInfo->freq_config_RX;
    pInfo->pTX = &pInfo->freq_config_TX;

    if (ChannelSave == FREQ_CHANNEL)
        pInfo->Modulation = MODULATION_AM;
    else
        pInfo->Modulation = MODULATION_FM;

    RADIO_ConfigureSquelchAndOutputPower(pInfo);
}

void RADIO_ValidateAndSetCode(FREQ_Config_t *pFreq_Config, uint8_t tmp)
{
    switch (pFreq_Config->CodeType) {
        default:
        case CODE_TYPE_OFF:
            pFreq_Config->CodeType = CODE_TYPE_OFF;
            tmp = 0;
            break;

        case CODE_TYPE_CONTINUOUS_TONE:
        case CODE_TYPE_DIGITAL:
        case CODE_TYPE_REVERSE_DIGITAL:
            if (tmp > ((pFreq_Config->CodeType == CODE_TYPE_CONTINUOUS_TONE ? ARRAY_SIZE(CTCSS_Options)
                                                                            : DCS_OPTION_COUNT) -
                       1))
                tmp = 0;
            break;
    }
    pFreq_Config->Code = tmp;

    /////////////////////////DEBUG//////////////////////////
    //char str[64] = "";sprintf(str, "Code type %d Code %d\r\n",pFreq_Config->CodeType, tmp );LogUart(str);
}

void RADIO_ConfigureChannel(const unsigned int VFO, const unsigned int configure)
{
    VFO_Info_t *pVfo = &gEeprom.VfoInfo[VFO];
    uint16_t channel = gEeprom.ScreenChannel[VFO];

    if (IS_VALID_CHANNEL(channel)) {

        if (IS_MR_CHANNEL(channel)) {
            channel = RADIO_FindNextChannel(channel, RADIO_CHANNEL_UP, false, VFO);
            if (channel == 0xFFFF) {
                channel = FREQ_CHANNEL;
                gEeprom.ScreenChannel[VFO] = FREQ_CHANNEL;
            } else {
                gEeprom.ScreenChannel[VFO] = channel;
                gEeprom.MrChannel[VFO] = channel;
            }
        }
    } else
        channel = FREQ_CHANNEL;

    ChannelAttributes_t *att = MR_GetChannelAttributes(channel);
    if (att->__val == 0xFFFF) { // invalid/unused channel
        if (IS_MR_CHANNEL(channel)) {
            gEeprom.ScreenChannel[VFO] = FREQ_CHANNEL;
        }

        RADIO_InitInfo(pVfo, channel, frequencyBandTable[0].lower);
        return;
    }

    RADIO_LoadChannel(pVfo, channel, VFO, configure);
}

void RADIO_LoadChannel(VFO_Info_t *pVfo, uint16_t channel, uint8_t VFO, uint8_t configure)
{
    const ChannelAttributes_t attributes = *MR_GetChannelAttributes(channel);
    const ChannelAttributes_t *att = &attributes;
    uint8_t bParticipation;

    if (IS_MR_CHANNEL(channel)) {
        bParticipation = att->scanlist;
    } else {
        bParticipation = 51;
    }
    pVfo->SCANLIST_PARTICIPATION = bParticipation;
    pVfo->CHANNEL_SAVE = channel;

    uint32_t base;
    if (IS_MR_CHANNEL(channel))
        base = ADRESS_CHANNELS + channel * 16;
    else
        base = 0x009000 + ((uint32_t)VFO * 0x10U);

    if (configure == VFO_CONFIGURE_RELOAD || IS_FREQ_CHANNEL(channel)) {
        uint8_t tmp;
        uint8_t data[8];

        // ***************

        if (IS_MR_CHANNEL(channel))
            PY25Q16_ReadBuffer(base + 8, data, sizeof(data));
        else
            SETTINGS_ReadVfoRecord(VFO, 8, data, sizeof(data));

        tmp = data[3] & 0x0F;
        if (tmp > TX_OFFSET_FREQUENCY_DIRECTION_SUB)
            tmp = 0;
        pVfo->TX_OFFSET_FREQUENCY_DIRECTION = tmp;
        tmp = data[3] >> 4;
        if (tmp >= MODULATION_UKNOWN)
            tmp = MODULATION_FM;
        pVfo->Modulation = tmp;

        tmp = data[6];
        if (tmp >= STEP_N_ELEM)
            tmp = STEP_12_5kHz;
        pVfo->STEP_SETTING = tmp;
        pVfo->StepFrequency = gStepFrequencyTable[tmp];

        pVfo->freq_config_RX.CodeType = (data[2] >> 0) & 0x0F;
        pVfo->freq_config_TX.CodeType = (data[2] >> 4) & 0x0F;

        RADIO_ValidateAndSetCode(&pVfo->freq_config_RX, data[0]);
        RADIO_ValidateAndSetCode(&pVfo->freq_config_TX, data[1]);

        pVfo->ExactFrequency = false;
        if (data[4] == 0xFF) {
            pVfo->FrequencyReverse = false;
            pVfo->CHANNEL_BANDWIDTH = BK4819_FILTER_BW_WIDE;
            pVfo->OUTPUT_POWER = OUTPUT_POWER_LOW;
            pVfo->BUSY_CHANNEL_LOCK = false;
        } else {
            const uint8_t d4 = data[4];
            pVfo->FrequencyReverse = !!((d4 >> 0) & 1u);
            pVfo->CHANNEL_BANDWIDTH = !!((d4 >> 1) & 1u);
            pVfo->ExactFrequency = channel == FREQ_CHANNEL && (d4 & 128);
            {
                uint8_t pwr = ((d4 >> 2) & 7u);
                // Guard: X=0 is only valid when stored by this firmware.
                // Old firmware had LOW=0 (no X slot), so EEPROM value 0 -> treat as LOW.
                // X (0) is now a valid stored value — do NOT strip it.
                // Only reject truly invalid values (>= LEN but not X).
                if (pwr >= OUTPUT_POWER_LEN)
                    pwr = OUTPUT_POWER_LOW;
                pVfo->OUTPUT_POWER = pwr;
            }
            pVfo->BUSY_CHANNEL_LOCK = !!((d4 >> 5) & 1u);
        }

        // ***************

        struct {
            uint32_t Frequency;
            uint32_t Offset;
        } __attribute__((packed)) info;
        if (IS_MR_CHANNEL(channel))
            PY25Q16_ReadBuffer(base, &info, sizeof(info));
        else
            SETTINGS_ReadVfoRecord(VFO, 0, &info, sizeof(info));
        if (info.Frequency == 0xFFFFFFFF)
            pVfo->freq_config_RX.Frequency = frequencyBandTable[0].lower;
        else
            pVfo->freq_config_RX.Frequency = info.Frequency;

        if (info.Offset >= _1GHz_in_KHz)
            info.Offset = _1GHz_in_KHz / 100;

        pVfo->TX_OFFSET_FREQUENCY = info.Offset;

        // ***************
    }

    uint32_t frequency = pVfo->freq_config_RX.Frequency;
    if (channel == FREQ_CHANNEL && !pVfo->ExactFrequency)
        frequency = FREQUENCY_RoundToStep(frequency, pVfo->StepFrequency);

    pVfo->freq_config_RX.Frequency = frequency;

    if (!IS_MR_CHANNEL(channel))
        pVfo->TX_OFFSET_FREQUENCY = FREQUENCY_RoundToStep(pVfo->TX_OFFSET_FREQUENCY, pVfo->StepFrequency);

    RADIO_ApplyOffset(pVfo);

    if (IS_MR_CHANNEL(channel)) { // 16 bytes allocated to the channel name but only 10 used, the rest are 0's
        SETTINGS_FetchChannelName(pVfo->Name, channel);
    }

    if (!pVfo->FrequencyReverse) {
        pVfo->pRX = &pVfo->freq_config_RX;
        pVfo->pTX = &pVfo->freq_config_TX;
    } else {
        pVfo->pRX = &pVfo->freq_config_TX;
        pVfo->pTX = &pVfo->freq_config_RX;
    }

    pVfo->Compander = att->compander;

    RADIO_ConfigureSquelchAndOutputPower(pVfo);
}

void RADIO_ConfigureSquelchAndOutputPower(VFO_Info_t *pInfo)
{

    // *******************************
    // squelch

    FREQUENCY_Band_t Band = FREQUENCY_GetBand(pInfo->pRX->Frequency);
    // 0x1E60 : 0x1E00
    uint32_t Base = (Band < BAND4_174MHz) ? 0x010060 : 0x010000;

    if (gEeprom.SQUELCH_LEVEL == 0) {          // squelch == 0 (off)
        pInfo->SquelchOpenRSSIThresh = 0;      // 0 ~ 255
        pInfo->SquelchOpenNoiseThresh = 127;   // 127 ~ 0
        pInfo->SquelchCloseGlitchThresh = 255; // 255 ~ 0

        pInfo->SquelchCloseRSSIThresh = 0;    // 0 ~ 255
        pInfo->SquelchCloseNoiseThresh = 127; // 127 ~ 0
        pInfo->SquelchOpenGlitchThresh = 255; // 255 ~ 0
    } else {                                  // squelch >= 1
        Base += gEeprom.SQUELCH_LEVEL;        // my eeprom squelch-1
                                              // VHF   UHF
        uint8_t *sq_ptrs[6] = {
            &pInfo->SquelchOpenRSSIThresh,    //  50    10
            &pInfo->SquelchCloseRSSIThresh,   //  40     5
            &pInfo->SquelchOpenNoiseThresh,   //  65    90
            &pInfo->SquelchCloseNoiseThresh,  //  70   100
            &pInfo->SquelchCloseGlitchThresh, //  90    90
            &pInfo->SquelchOpenGlitchThresh   // 100   100
        };

        for (uint8_t i = 0; i < 6; i++) {
            PY25Q16_ReadBuffer(Base + (i * 0x10), sq_ptrs[i], 1);
        }

        uint16_t noise_open = pInfo->SquelchOpenNoiseThresh;
        uint16_t noise_close = pInfo->SquelchCloseNoiseThresh;

#if ENABLE_SQUELCH_MORE_SENSITIVE
        uint16_t rssi_open = pInfo->SquelchOpenRSSIThresh;
        uint16_t rssi_close = pInfo->SquelchCloseRSSIThresh;
        uint16_t glitch_open = pInfo->SquelchOpenGlitchThresh;
        uint16_t glitch_close = pInfo->SquelchCloseGlitchThresh;
        // make squelch more sensitive
        // note that 'noise' and 'glitch' values are inverted compared to 'rssi' values
        rssi_open = (rssi_open * 1) / 2;
        noise_open = (noise_open * 2) / 1;
        glitch_open = (glitch_open * 2) / 1;

        // ensure the 'close' threshold is lower than the 'open' threshold
        if (rssi_close == rssi_open && rssi_close >= 2)
            rssi_close -= 2;
        if (noise_close == noise_open && noise_close <= 125)
            noise_close += 2;
        if (glitch_close == glitch_open && glitch_close <= 253)
            glitch_close += 2;

        pInfo->SquelchOpenRSSIThresh = MIN(rssi_open, 255);
        pInfo->SquelchCloseRSSIThresh = MIN(rssi_close, 255);
        pInfo->SquelchOpenGlitchThresh = MIN(glitch_open, 255);
        pInfo->SquelchCloseGlitchThresh = MIN(glitch_close, 255);
#endif

        pInfo->SquelchOpenNoiseThresh = MIN(noise_open, 127);
        pInfo->SquelchCloseNoiseThresh = MIN(noise_close, 127);
    }

    // *******************************
    // output power

    Band = FREQUENCY_GetBand(pInfo->pTX->Frequency);

    uint8_t Txp[3];

    const uint8_t power = pInfo->OUTPUT_POWER;
    const uint8_t calibration = power <= OUTPUT_POWER_HIGH ? power : OUTPUT_POWER_LOW;
    PY25Q16_ReadBuffer(0x100D0 + (Band * 16) + calibration * 3, Txp, 3);
    // F4HWN v7 power levels, on the radio's calibration: LOW1..LOW5 divide
    // the low calibration, MID takes 3/4 of the mid one, HIGH adds 24 to the
    // high one.
    static const uint8_t dividers[2][5] = {{20, 15, 10, 8, 6}, {25, 19, 13, 9, 6}};
    for (uint8_t p = 0; p < 3; p++) {
        if (power == OUTPUT_POWER_MID)
            Txp[p] = (Txp[p] * 3u) / 4u;
        else if (power == OUTPUT_POWER_HIGH)
            Txp[p] += 24;
        else
            Txp[p] = (Txp[p] * 4u) / dividers[Band == 2 ? 0 : 1][POWER_ToIndex(power)];
    }


    pInfo->TXP_CalculatedSetting =
        FREQUENCY_CalculateOutputPower(Txp[0], Txp[1], Txp[2], frequencyBandTable[Band].lower,
                                       (frequencyBandTable[Band].lower + frequencyBandTable[Band].upper) / 2,
                                       frequencyBandTable[Band].upper, pInfo->pTX->Frequency);

    // *******************************
}

void RADIO_ApplyOffset(VFO_Info_t *pInfo)
{
    uint32_t Frequency = pInfo->freq_config_RX.Frequency;

    switch (pInfo->TX_OFFSET_FREQUENCY_DIRECTION) {
        case TX_OFFSET_FREQUENCY_DIRECTION_OFF:
            break;
        case TX_OFFSET_FREQUENCY_DIRECTION_ADD:
            Frequency += pInfo->TX_OFFSET_FREQUENCY;
            break;
        case TX_OFFSET_FREQUENCY_DIRECTION_SUB:
            Frequency -= pInfo->TX_OFFSET_FREQUENCY;
            break;
    }

    pInfo->freq_config_TX.Frequency = Frequency;
}

static void RADIO_SelectCurrentVfo(void)
{
    if (gEeprom.RX_MODE != RX_MODE_SIMPLE)
        gCurrentVfo = gRxVfo;
    else
        gCurrentVfo = gTxVfo;
}

void RADIO_SelectVfos(void)
{
    if (APP_MLHoldRx()) {
        gTxVfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
        return;
    }
    APP_FullWatchReset();
    gEeprom.RX_VFO = gEeprom.TX_VFO;

    gTxVfo = &gEeprom.VfoInfo[gEeprom.TX_VFO];
    gRxVfo = &gEeprom.VfoInfo[gEeprom.RX_VFO];

    RADIO_SelectCurrentVfo();
}

void RADIO_SetupRegisters(bool switchToForeground)
{
    if (APP_MLHoldRx())
        return;
    BK4819_FilterBandwidth_t Bandwidth = gRxVfo->CHANNEL_BANDWIDTH;

#ifdef ENABLE_FEAT_F4HWN_NARROWER
    if (Bandwidth == BK4819_FILTER_BW_NARROW && gSetting_set_nfm == 1) {
        Bandwidth = BK4819_FILTER_BW_NARROWER;
    }
#endif

    // Idle VFO surveillance must not mute the broadcast FM speaker.
#ifdef ENABLE_FMRADIO
    if (!(gFmRadioMode && gCurrentFunction == FUNCTION_FOREGROUND && gFM_RestoreCountdown_10ms == 0))
#endif
    {
        GPIO_DisableAudioPath();
        gEnableSpeaker = false;
    }

    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

    if (gRxVfo->Modulation == MODULATION_AM)
        BK4819_SetFilterBandwidth(gRxVfo->CHANNEL_BANDWIDTH == BK4819_FILTER_BW_WIDE ? BK4819_FILTER_BW_WIDE
                                                                                     : BK4819_FILTER_BW_AM,
                                  true);
    else {
        switch (Bandwidth) {
            default:
                Bandwidth = BK4819_FILTER_BW_WIDE;
                [[fallthrough]];
            case BK4819_FILTER_BW_WIDE:
            case BK4819_FILTER_BW_NARROW:
            case BK4819_FILTER_BW_NARROWER:
                BK4819_SetFilterBandwidth(Bandwidth, false);
                break;
        }
    }

    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);

    BK4819_SetupPowerAmplifier(0, 0);

    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);

    while (1) {
        const uint16_t Status = BK4819_ReadRegister(BK4819_REG_0C);
        if ((Status & 1u) == 0) // INTERRUPT REQUEST
            break;

        BK4819_WriteRegister(BK4819_REG_02, 0);
        SYSTEM_DelayMs(1);
    }
    BK4819_WriteRegister(BK4819_REG_3F, 0);

    // mic gain 0.5dB/step 0 to 63
    BK4819_WriteRegister(BK4819_REG_7D, 0xE940 | (gEeprom.MIC_SENSITIVITY_TUNING & 0x3f));

    uint32_t Frequency;
    Frequency = gRxVfo->pRX->Frequency;
    BK4819_SetFrequency(Frequency);
    RADIO_SetModulation(gRxVfo->Modulation); // H4: sync demodulator on every retune

    BK4819_SetupSquelch(gRxVfo->SquelchOpenRSSIThresh, gRxVfo->SquelchCloseRSSIThresh,
                        gRxVfo->SquelchOpenNoiseThresh, gRxVfo->SquelchCloseNoiseThresh,
                        gRxVfo->SquelchCloseGlitchThresh, gRxVfo->SquelchOpenGlitchThresh);

    BK4819_PickRXFilterPathBasedOnFrequency(Frequency);

    // what does this in do ?
    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);

    // AF RX Gain and DAC
    //BK4819_WriteRegister(BK4819_REG_48, 0xB3A8);  // 1011 00 111010 1000
    BK4819_SetRxAudioGain(); // AF DAC Gain (after Gain-1 and Gain-2)

    uint16_t InterruptMask = BK4819_REG_3F_SQUELCH_FOUND | BK4819_REG_3F_SQUELCH_LOST;

    {
        if (gRxVfo->Modulation == MODULATION_FM) { // FM
            uint8_t CodeType = gRxVfo->pRX->CodeType;
            uint8_t Code = gRxVfo->pRX->Code;

            switch (CodeType) {
                default:
                case CODE_TYPE_OFF:
                    BK4819_SetCTCSSFrequency(SQL_TONE);
                    BK4819_SetTailDetection(SQL_TONE); // Default 550 = QS's 55Hz tone method

                    InterruptMask |= BK4819_REG_3F_CxCSS_TAIL;
                    break;

                case CODE_TYPE_CONTINUOUS_TONE:
                    BK4819_SetCTCSSFrequency(CTCSS_Options[Code]);

                    //    BK4819_SetTailDetection(550);       // QS's 55Hz tone method
                    //#else
                    //  BK4819_SetTailDetection(CTCSS_Options[Code]);
                    //#endif

                    InterruptMask |=
                        BK4819_REG_3F_CxCSS_TAIL | BK4819_REG_3F_CTCSS_FOUND | BK4819_REG_3F_CTCSS_LOST;
                    break;

                case CODE_TYPE_DIGITAL:
                case CODE_TYPE_REVERSE_DIGITAL:
                    BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(CodeType, Code));
                    InterruptMask |=
                        BK4819_REG_3F_CxCSS_TAIL | BK4819_REG_3F_CDCSS_FOUND | BK4819_REG_3F_CDCSS_LOST;
                    break;
            }
            if (gEeprom.SCRAMBLING_TYPE > 0)
                BK4819_EnableScramble(gEeprom.SCRAMBLING_TYPE - 1);
            else
                BK4819_DisableScramble();
        }
    }

#ifdef ENABLE_VOX
    if (gEeprom.VOX_SWITCH && gCurrentVfo->Modulation == MODULATION_FM
#ifdef ENABLE_FMRADIO
        && !gFmRadioMode
#endif
    ) {
        BK4819_EnableVox(gEeprom.VOX1_THRESHOLD, gEeprom.VOX0_THRESHOLD);
        InterruptMask |= BK4819_REG_3F_VOX_FOUND | BK4819_REG_3F_VOX_LOST;
    } else
        BK4819_DisableVox();
#endif

    // RX expander
    BK4819_SetCompander(RADIO_IsRxCompanderOn(gRxVfo) ? gRxVfo->Compander : 0);
    // Keep the AM-specific AGC profile active after every VFO reconfiguration.
    RADIO_SetupAGC(gRxVfo->Modulation == MODULATION_AM, false);

    BK4819_WriteRegister(BK4819_REG_24, 0);

    // enable/disable BK4819 selected interrupts
    BK4819_WriteRegister(BK4819_REG_3F, InterruptMask);

    FUNCTION_Init();

    if (switchToForeground)
        FUNCTION_Select(FUNCTION_FOREGROUND);
#ifdef ENABLE_FMRADIO
    if (gFmRadioMode && gCurrentFunction == FUNCTION_FOREGROUND)
        FM_SetVfoAudio(false);
#endif
}

static void RADIO_SetTxParametersEx(bool fm)
{
    BK4819_FilterBandwidth_t Bandwidth = gCurrentVfo->CHANNEL_BANDWIDTH;

#ifdef ENABLE_FEAT_F4HWN_NARROWER
    if (Bandwidth == BK4819_FILTER_BW_NARROW && gSetting_set_nfm == 1) {
        Bandwidth = BK4819_FILTER_BW_NARROWER;
    }
#endif

    GPIO_DisableAudioPath();

    gEnableSpeaker = false;

    BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);

    switch (Bandwidth) {
        default:
            Bandwidth = BK4819_FILTER_BW_WIDE;
            [[fallthrough]];
        case BK4819_FILTER_BW_WIDE:
        case BK4819_FILTER_BW_NARROW:
        case BK4819_FILTER_BW_NARROWER:
            BK4819_SetFilterBandwidth(Bandwidth, false);
            break;
    }

    BK4819_SetFrequency(gCurrentVfo->pTX->Frequency);

    // TX compressor
    BK4819_SetCompander(
        (gRxVfo->Modulation == MODULATION_FM && (gRxVfo->Compander == 1 || gRxVfo->Compander >= 3))
            ? gRxVfo->Compander
            : 0);

    BK4819_PrepareTransmit();

    SYSTEM_DelayMs(fm * 10);

    BK4819_PickRXFilterPathBasedOnFrequency(gCurrentVfo->pTX->Frequency);

    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, fm);

    SYSTEM_DelayMs(fm * 5);

    BK4819_SetupPowerAmplifier(gCurrentVfo->TXP_CalculatedSetting, gCurrentVfo->pTX->Frequency);

    SYSTEM_DelayMs(fm * 10);

    switch (gCurrentVfo->pTX->CodeType) {
        default:
        case CODE_TYPE_OFF:
            BK4819_ExitSubAu();
            break;

        case CODE_TYPE_CONTINUOUS_TONE:
            BK4819_SetCTCSSFrequency(CTCSS_Options[gCurrentVfo->pTX->Code]);
            break;

        case CODE_TYPE_DIGITAL:
        case CODE_TYPE_REVERSE_DIGITAL:
            BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(gCurrentVfo->pTX->CodeType, gCurrentVfo->pTX->Code));
            break;
    }
}

void RADIO_SetTxParameters(void)
{
    RADIO_SetTxParametersEx(true);
}
void RADIO_PrepareTx(void)
{
    RADIO_SetTxParametersEx(false);
}

// RX expander active on this VFO (Compnd RX or TX+RX, FM only)
bool RADIO_IsRxCompanderOn(const VFO_Info_t *pInfo)
{
    return pInfo->Modulation == MODULATION_FM && pInfo->Compander >= 2;
}

void RADIO_SetModulation(ModulationMode_t modulation)
{
    BK4819_SetAF(modulation == MODULATION_USB ? BK4819_AF_BASEBAND2 : BK4819_AF_FM);
    const uint16_t control = BK4819_ReadRegister(0x31);
    BK4819_WriteRegister(0x31, modulation == MODULATION_AM ? control | 1u : control & 0xfffeu);
    BK4819_WriteRegister(0x42, modulation == MODULATION_AM ? 0x6f5c : 0x6b5a);
    BK4819_WriteRegister(0x2a, modulation == MODULATION_AM ? 0x7434 : 0x7400);
    if (modulation == MODULATION_AM) {
        AUDIO_ApplyAMProfile(gSetting_set_audio_am);
        // H4WHN: AM Wide uses the wide RF filter; Narrow uses the AM preset.
        BK4819_SetFilterBandwidth(gRxVfo->CHANNEL_BANDWIDTH == BK4819_FILTER_BW_WIDE ? BK4819_FILTER_BW_WIDE
                                                                                     : BK4819_FILTER_BW_AM,
                                  true);
    } else {
        BK4819_WriteRegister(0x2b, 0);
        BK4819_WriteRegister(0x2f, 0x9890);
        if (modulation == MODULATION_USB)
            AUDIO_ApplyUSBProfile();
        else
            AUDIO_ApplyFMProfile(gSetting_set_audio_fm);
    }
    // REG_28<8>: RX AF noise gate enable (level and HPF1 kept from init), as
    // F4HWN v7. Forced on with the RX expander: SetRxG only applies when the
    // compander is off in RX.
    const bool rxGate = gSetting_set_rxg || RADIO_IsRxCompanderOn(gRxVfo);
    const uint16_t reg28 = BK4819_ReadRegister(BK4819_REG_28);
    BK4819_WriteRegister(BK4819_REG_28, rxGate ? (reg28 | (1u << 8)) : (reg28 & ~(1u << 8)));
    BK4819_SetRegValue(afDacGainRegSpec, 0xF);
    BK4819_WriteRegister(BK4819_REG_3D, modulation == MODULATION_USB ? 0 : 0x2AAB);
    BK4819_SetRegValue(afcDisableRegSpec, modulation != MODULATION_FM);
    RADIO_SetupAGC(modulation == MODULATION_AM, false);
}

void RADIO_SetupAGC(bool listeningAM, bool disable)
{
    static uint8_t lastSettings = 0xFF;
    uint8_t newSettings = (listeningAM << 1) | disable;
    if (lastSettings == newSettings)
        return;
    lastSettings = newSettings;
    BK4819_SetAGC(!disable);
    BK4819_InitAGC(listeningAM);
}

void RADIO_SetVfoState(VfoState_t State)
{
    if (State == VFO_STATE_NORMAL) {
        VfoState[0] = VFO_STATE_NORMAL;
        VfoState[1] = VFO_STATE_NORMAL;
    } else if (State == VFO_STATE_VOLTAGE_HIGH) {
        VfoState[0] = VFO_STATE_VOLTAGE_HIGH;
        VfoState[1] = VFO_STATE_TX_DISABLE;
    } else {
        const unsigned int vfo =
            gEeprom.RX_MODE == RX_MODE_FULL || APP_Ptt2Active() ? gEeprom.RX_VFO : gEeprom.TX_VFO;
        VfoState[vfo] = State;
    }

    gVFOStateResumeCountdown_500ms = (State == VFO_STATE_NORMAL) ? 0 : vfo_state_resume_countdown_500ms;
    gUpdateDisplay = true;
}

void RADIO_PrepareTX(void)
{
    VfoState_t State = VFO_STATE_NORMAL; // default to OK to TX
    const bool ptt2 = APP_Ptt2Active();
    const uint8_t txIndex = gEeprom.TX_VFO ^ ptt2;
    const bool bDualRxOnOtherVfo = (gEeprom.RX_MODE != RX_MODE_SIMPLE && gEeprom.RX_VFO != txIndex);

    if (gEeprom.RX_MODE == RX_MODE_FULL && !ptt2) {
        gDualVfoCountdown_10ms = 420;
        gDualVfoSchedule = false;
        if (!gRxVfoIsActive) {
            gEeprom.RX_VFO = gEeprom.TX_VFO;
            gRxVfo = APP_GetFullWatchDisplayVfo(gEeprom.TX_VFO);
            if (!gRxVfo)
                gRxVfo = gTxVfo;
            gRxVfoIsActive = true;
        }
        RADIO_SelectCurrentVfo();
    } else if (gEeprom.RX_MODE != RX_MODE_SIMPLE) {
        // Pause dual-watch for the duration of TX and only briefly after TX.
        // The 4.2 s H4HWN hold is intentionally not used here: Sonic must
        // resume monitoring the other VFO almost immediately after TX.
        gDualVfoCountdown_10ms = 10; // Sonic dual: 100 ms hold
        gDualVfoSchedule = false;
        gRxVfoIsActive = true;

        // PTT always transmits on the user-selected TX VFO, even when the
        // other VFO was the one that most recently opened the squelch.
        gEeprom.RX_VFO = txIndex;
        gRxVfo = &gEeprom.VfoInfo[txIndex];
        if (ptt2)
            gTxVfo = gRxVfo;
        gCurrentVfo = gTxVfo;
    } else {
        RADIO_SelectCurrentVfo();
    }

    if (TX_freq_check(gCurrentVfo->pTX->Frequency) != 0) {
        // TX frequency not allowed
        State = VFO_STATE_TX_DISABLE;
        gVfoConfigureMode = VFO_CONFIGURE;
    } else if (SerialConfigInProgress()) {
        // TX is disabled or config upload/download in progress
        State = VFO_STATE_TX_DISABLE;
    } else if (gCurrentVfo->BUSY_CHANNEL_LOCK && gCurrentFunction == FUNCTION_RECEIVE &&
               ((gEeprom.RX_MODE == RX_MODE_FULL && !ptt2) || !bDualRxOnOtherVfo) && g_SquelchLost) {
        // Block TX only while the selected TX VFO is actually still busy.
        // In dual mode, an RX on the other VFO must not suppress PTT.
        State = VFO_STATE_BUSY;
    } else if (gBatteryDisplayLevel == 0) {
        // charge your battery !git co
        State = VFO_STATE_BAT_LOW;
    } else if (gBatteryDisplayLevel > 6) {
        // over voltage .. this is being a pain
        State = VFO_STATE_VOLTAGE_HIGH;
    } else if (gCurrentVfo->Modulation != MODULATION_FM) {
        // not allowed to TX if in AM mode
        State = VFO_STATE_TX_DISABLE;
    }

    if (State != VFO_STATE_NORMAL) {
        // TX not allowed
        RADIO_SetVfoState(State);
        return;
    }

    // TX is allowed
    // In dual-VFO mode the foreground VFO may be the RX VFO.
    // TX must always use the VFO selected for transmission.
    gCurrentVfo = gTxVfo;

    FUNCTION_Select(FUNCTION_TRANSMIT);

#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    RXTX_LOG_BeginTx(gCurrentVfo);
#endif

    gTxTimerCountdown_500ms = 0; // no timeout

    {
        gTxTimerCountdown_500ms = ((gEeprom.TX_TIMEOUT_TIMER + 1) * 5) * 2;

#ifdef ENABLE_FEAT_F4HWN
        gTxTimerCountdownAlert_500ms = gTxTimerCountdown_500ms;
#endif
    }

    gTxTimeoutReached = false;

#ifdef ENABLE_FEAT_F4HWN
    gTxTimeoutReachedAlert = false;
#endif

    gFlagEndTransmission = false;
    gRTTECountdown_10ms = 0;
}

void RADIO_SendCssTail(void)
{
    if (!gEeprom.TAIL_TONE_ELIMINATION)
        return;

    switch (gCurrentVfo->pTX->CodeType) {
        case CODE_TYPE_DIGITAL:
        case CODE_TYPE_REVERSE_DIGITAL:
            BK4819_PlayCDCSSTail();
            break;
        default:
            BK4819_PlayCTCSSTail();
            break;
    }
    SYSTEM_DelayMs(200);
}

void RADIO_SendEndOfTransmission(void)
{
    BK4819_PlayRoger(gEeprom.ROGER);
    // send the CTCSS/DCS tail tone - allows the receivers to mute the usual FM squelch tail/crash
    RADIO_SendCssTail();
    RADIO_SetupRegisters(false);

    if (gEeprom.RX_MODE == RX_MODE_DUAL) {
        gRxVfoIsActive = false;
        gRxReceptionMode = RX_MODE_NONE;
        gDualVfoCountdown_10ms = 10; // Sonic dual: 100 ms hold
        gDualVfoSchedule = false;
    }
}
