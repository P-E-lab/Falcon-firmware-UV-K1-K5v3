/* Center line of the dual-VFO screen in Full Watch and ML WATCH: the mode
 * label; Full Watch flanks it with its priority channels as small bubbles. */
static const char watch_bubble_vfo[] NATIVE_DATA(vfo) = "F%u";
static const char watch_bubble_channel[] NATIVE_DATA(vfo) = "%03u";
static const char watch_label_full[] NATIVE_DATA(vfo) = "<< F WATCH >>";
static const char watch_label_full_tight[] NATIVE_DATA(vfo) = "<<F WATCH>>";
static const char watch_label_ml[] NATIVE_DATA(vfo) = "<<< ML WATCH >>>";

#define FULL_WATCH_ARROW_Y 25u
#define WATCH_CELL         4u /* compact font: 4 px per character        */
#define WATCH_BUBBLE_PAD   3u /* inverse box around a bubble text         */
#define WATCH_BUBBLE_GAP   2u /* blank pixels between two bubbles         */
#define WATCH_LABEL_GAP    4u /* blank pixels between label and bubbles   */

/* ML WATCH: its outermost arrows blink. */
static void UI_MAIN_DrawMLWatchArrows(void)
{
    const bool visible = UI_ScanBlinkVisible();
    GUI_DisplaySmallest("<", 32, FULL_WATCH_ARROW_Y, false, visible);
    GUI_DisplaySmallest(">", 92, FULL_WATCH_ARROW_Y, false, visible);
}

static NATIVE_CODE(vfo) void UI_MAIN_DrawFullWatchPriorities(void)
{
    if ((gEeprom.RX_MODE != RX_MODE_FULL && gEeprom.RX_MODE != RX_MODE_ML) ||
        (gCurrentFunction != FUNCTION_FOREGROUND && gCurrentFunction != FUNCTION_POWER_SAVE) ||
        gCssBackgroundScan)
        return;

    // ML WATCH shows its label only: no priority-channel bubbles.
    const bool ml = gEeprom.RX_MODE == RX_MODE_ML;
    uint8_t count = 0;
    VFO_Info_t *const *vfos = ml ? NULL : APP_GetFullWatchBackgroundVfos(&count);
    if (!ml && count == 0)
        return;

    /* Bubble texts, innermost first on each side; a channel already shown
     * on a VFO gets no bubble (the displaced VFO of a priority swap does). */
    uint16_t onScreen[2];
    for (uint8_t v = 0; v < 2; v++) {
        const VFO_Info_t *info = APP_GetFullWatchDisplayVfo(v);
        onScreen[v] = info ? info->CHANNEL_SAVE : gEeprom.ScreenChannel[v];
    }
    const uint8_t half = (count + 1u) / 2u;
    char text[4][5];
    bool shown[4];
    uint8_t sideWidth[2] = {0, 0};
    for (uint8_t position = 0; position < count; position++) {
        const uint8_t i = position < half ? half - 1u - position : position;
        const VFO_Info_t *vfo = vfos[i];
        const uint16_t channel = vfo->CHANNEL_SAVE;
        shown[position] = channel != onScreen[0] && channel != onScreen[1];
        if (!shown[position])
            continue;
        if (IS_MR_CHANNEL(channel))
            sprintf(text[position], watch_bubble_channel, channel + 1u);
        else
            sprintf(text[position], watch_bubble_vfo, vfo == &gEeprom.VfoInfo[0] ? 1u : 2u);
        sideWidth[position >= half] +=
            strlen(text[position]) * WATCH_CELL + WATCH_BUBBLE_PAD + WATCH_BUBBLE_GAP;
    }

    /* Full Watch keeps the spaced label unless its bubbles would leave the
     * screen (four-digit channels): then the tight label and gap are used. */
    const char *label = ml ? watch_label_ml : watch_label_full;
    uint8_t labelGap = WATCH_LABEL_GAP;
    uint8_t width = strlen(label) * WATCH_CELL - 1u;
    const uint8_t widest = sideWidth[0] > sideWidth[1] ? sideWidth[0] : sideWidth[1];
    if (!ml && (LCD_WIDTH - width) / 2u < widest + labelGap) {
        label = watch_label_full_tight;
        labelGap = WATCH_BUBBLE_GAP;
        width = strlen(label) * WATCH_CELL - 1u;
    }
    const uint8_t labelX = (LCD_WIDTH - width) / 2u;
    GUI_DisplaySmallest(label, labelX, FULL_WATCH_ARROW_Y, false, true);
    if (ml)
        UI_MAIN_DrawMLWatchArrows();

    uint8_t left = labelX - labelGap, right = labelX + width + labelGap;
    for (uint8_t position = 0; position < count; position++) {
        if (!shown[position])
            continue;
        const uint8_t textWidth = strlen(text[position]) * WATCH_CELL;
        uint8_t x;
        if (position < half) {
            x = left - (textWidth + WATCH_BUBBLE_PAD);
            left = x - WATCH_BUBBLE_GAP;
        } else {
            x = right;
            right += textWidth + WATCH_BUBBLE_PAD + WATCH_BUBBLE_GAP;
        }
        GUI_DisplaySmallestInverse(text[position], x + 2u, 3, false, true, x + textWidth + 2u);
    }
}
