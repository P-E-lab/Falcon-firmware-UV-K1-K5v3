/* H4WHN 6.1.0 Full Watch cycle/promotion. Loading uses Sonic's shared
 * channel decoder; standalone scan modes keep their existing engine. */
static VFO_Info_t gFullWatchPriorityVfo[4];
static VFO_Info_t *gFullWatchForegroundVfo;
static VFO_Info_t *gFullWatchBackgroundVfo[4];
static uint8_t gFullWatchCurrentBackground = 0xFFu;
static uint8_t gFullWatchBackgroundCount;
static uint8_t gFullWatchSequenceIndex = 0xFFu;

void APP_FullWatchReset(void)
{
    gFullWatchForegroundVfo = NULL;
    gFullWatchCurrentBackground = 0xFFu;
    gFullWatchBackgroundCount = 0;
    gFullWatchSequenceIndex = 0xFFu;
}

static VFO_Info_t *FullWatchLoadPriority(uint8_t priority, uint16_t channel)
{
    VFO_Info_t *vfo = &gFullWatchPriorityVfo[priority];
    const uint32_t frequency = SETTINGS_FetchChannelFrequency(channel);
    if (frequency < frequencyBandTable[0].lower || frequency >= 130000000u)
        return NULL;
    RADIO_InitInfo(vfo, channel, frequency);
    RADIO_LoadChannel(vfo, channel, 0, VFO_CONFIGURE_RELOAD);
    return vfo;
}

static uint8_t FullWatchReplacementVfo(void)
{
    return gEeprom.TX_VFO;
}

static void FullWatchInitialize(void)
{
    if (gFullWatchForegroundVfo != NULL)
        return;

    gFullWatchForegroundVfo = &gEeprom.VfoInfo[FullWatchReplacementVfo()];
    gFullWatchBackgroundCount = 0;

    for (uint8_t priority = 0; priority < 4; priority++) {
        const uint16_t channel = gEeprom.SCANLIST_PRIORITY_CH[priority];
        bool duplicate = false;
        for (uint8_t i = 0; i < priority; ++i)
            if (channel == gEeprom.SCANLIST_PRIORITY_CH[i])
                duplicate = true;
        if (!IS_MR_CHANNEL(channel) || channel >= MR_CHANNELS_MAX - MAX_BANDS)
            continue;

        const ChannelAttributes_t *attributes = MR_GetChannelAttributes(channel);
        if (attributes == NULL || attributes->band > BAND7_470MHz || duplicate)
            continue;

        VFO_Info_t *vfo = FullWatchLoadPriority(priority, channel);
        if (vfo != NULL)
            gFullWatchBackgroundVfo[gFullWatchBackgroundCount++] = vfo;
    }
}

static void FullWatchAlternate(void)
{
    FullWatchInitialize();
    const uint8_t count = 2u + gFullWatchBackgroundCount;
    const uint8_t vfoBIndex = 1u + (gFullWatchBackgroundCount + 1u) / 2u;
    const uint8_t replacementVfo = FullWatchReplacementVfo();

    if (gFullWatchSequenceIndex >= count) {
        const bool onVfoB =
            gRxVfo == &gEeprom.VfoInfo[1] || (gRxVfo == gFullWatchForegroundVfo && replacementVfo == 1);
        gFullWatchSequenceIndex = onVfoB ? vfoBIndex : 0;
    }

    if (++gFullWatchSequenceIndex >= count)
        gFullWatchSequenceIndex = 0;

    if (gFullWatchSequenceIndex == 0 || gFullWatchSequenceIndex == vfoBIndex) {
        gEeprom.RX_VFO = gFullWatchSequenceIndex == 0 ? 0 : 1;
        gRxVfo =
            gEeprom.RX_VFO == replacementVfo ? gFullWatchForegroundVfo : &gEeprom.VfoInfo[gEeprom.RX_VFO];
        gFullWatchCurrentBackground = 0xFFu;
        return;
    }

    const uint8_t background = gFullWatchSequenceIndex - 1u - (gFullWatchSequenceIndex > vfoBIndex);
    gEeprom.RX_VFO = replacementVfo;
    gRxVfo = gFullWatchBackgroundVfo[background];
    gFullWatchCurrentBackground = background;
}

VFO_Info_t *APP_GetFullWatchDisplayVfo(uint8_t vfo)
{
    if (gEeprom.RX_MODE == RX_MODE_ML)
        return APP_GetMLWatchDisplayVfo(vfo);
    const uint8_t replacementVfo = FullWatchReplacementVfo();

    if (gEeprom.RX_MODE != RX_MODE_FULL || vfo != replacementVfo ||
        gFullWatchForegroundVfo == &gEeprom.VfoInfo[replacementVfo])
        return NULL;

    return gFullWatchForegroundVfo;
}

VFO_Info_t *const *APP_GetFullWatchBackgroundVfos(uint8_t *count)
{
    FullWatchInitialize();
    *count = gFullWatchBackgroundCount;
    return gFullWatchBackgroundVfo;
}

static void FullWatchPromoteCurrentBackground(void)
{
    if (gEeprom.RX_MODE != RX_MODE_FULL || gFullWatchForegroundVfo == NULL ||
        gRxReceptionMode == RX_MODE_NONE || gCssBackgroundScan ||
        gFullWatchCurrentBackground >= gFullWatchBackgroundCount)
        return;

    const uint8_t background = gFullWatchCurrentBackground;
    VFO_Info_t *displaced = gFullWatchForegroundVfo;
    gFullWatchForegroundVfo = gFullWatchBackgroundVfo[background];
    gFullWatchBackgroundVfo[background] = displaced;
    gFullWatchCurrentBackground = 0xFFu;
    gEeprom.RX_VFO = FullWatchReplacementVfo();
    gRxVfo = gFullWatchForegroundVfo;
    gUpdateDisplay = true;
    gUpdateStatus = true;
}
