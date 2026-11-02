/* One draft, popup and editor shared by the VFO and scan selection pages. */
#include "ui/main.h"
static const char ns_editor_0[] NATIVE_DATA(editor) = "EDIT";
static const char ns_editor_1[] NATIVE_DATA(editor) = "NEW";
static const char ns_editor_2[] NATIVE_DATA(editor) = "DELETE";
static const char ns_editor_3[] NATIVE_DATA(editor) = "LIST";
static const char ns_editor_4[] NATIVE_DATA(editor) = "BAND";
static const char ns_editor_5[] NATIVE_DATA(editor) = "CHANNEL";
static const char ns_editor_6[] NATIVE_DATA(editor) = "%s %s";
static const char ns_editor_7[] NATIVE_DATA(editor) = "SURE ?!";
static const char ns_editor_8[] NATIVE_DATA(editor) = "Sure ?";
static const char ns_editor_9[] NATIVE_DATA(editor) = "NEW BAND";
static const char ns_editor_10[] NATIVE_DATA(editor) = "EDIT BAND";
static const char ns_editor_11[] NATIVE_DATA(editor) = "NEW CHANNEL";
static const char ns_editor_12[] NATIVE_DATA(editor) = "EDIT CHANNEL";
static const char ns_editor_13[] NATIVE_DATA(editor) = "LIST:";
static const char ns_editor_14[] NATIVE_DATA(editor) = "FREQ:";
static const char ns_editor_15[] NATIVE_DATA(editor) = "NAME:";
static const char ns_editor_16[] NATIVE_DATA(editor) = "MOD:";
static const char ns_editor_17[] NATIVE_DATA(editor) = "BAND:";
static const char ns_editor_18[] NATIVE_DATA(editor) = "POW:";
static const char ns_editor_19[] NATIVE_DATA(editor) = "STEP:";
static const char ns_editor_20[] NATIVE_DATA(editor) = "OffS:";
static const char ns_editor_21[] NATIVE_DATA(editor) = "ODir:";
static const char ns_editor_22[] NATIVE_DATA(editor) = "CTCSS RX:";
static const char ns_editor_23[] NATIVE_DATA(editor) = "CTCSS TX:";
static const char ns_editor_24[] NATIVE_DATA(editor) = "DCS RX:";
static const char ns_editor_25[] NATIVE_DATA(editor) = "DCS TX:";
static const char ns_editor_26[] NATIVE_DATA(editor) = "000.00000  MHz";
static const char ns_editor_27[] NATIVE_DATA(editor) = "--------";
static const char ns_editor_28[] NATIVE_DATA(editor) = "-------";
static const char ns_editor_29[] NATIVE_DATA(editor) = "select >";
static const char ns_editor_30[] NATIVE_DATA(editor) = "Hz";
static const char ns_editor_31[] NATIVE_DATA(editor) = "OFF";
static const char ns_editor_32[] NATIVE_DATA(editor) = "BAND";
static const char ns_editor_33[] NATIVE_DATA(editor) = "NO LIST";
static const char ns_editor_34[] NATIVE_DATA(editor) = "MONITOR";
static const char ns_editor_35[] NATIVE_DATA(editor) = "%02u %.8s";
static const char ns_editor_36[] NATIVE_DATA(editor) = "%u.%05u MHz";
static const char ns_editor_37[] NATIVE_DATA(editor) = "?";
static const char ns_editor_38[] NATIVE_DATA(editor) = "Narrow";
static const char ns_editor_39[] NATIVE_DATA(editor) = "Wide";
static const char ns_editor_40[] NATIVE_DATA(editor) = "?";
static const char ns_editor_41[] NATIVE_DATA(editor) = "?";
static const char ns_editor_42[] NATIVE_DATA(editor) = "+";
static const char ns_editor_43[] NATIVE_DATA(editor) = "-";
static const char ns_editor_44[] NATIVE_DATA(editor) = "Off";
static const char ns_editor_45[] NATIVE_DATA(editor) = "BANDW:";
static const char ns_editor_46[] NATIVE_DATA(editor) = "FSTART:";
static const char ns_editor_47[] NATIVE_DATA(editor) = "FSTOP:";
static const char ns_editor_48[] NATIVE_DATA(editor) = "REVERSE:";
static const char ns_editor_49[] NATIVE_DATA(editor) = "ON";
#include "ui/status.h"
enum { CHANNEL_IDLE, CHANNEL_ACTIONS, CHANNEL_EDIT };
enum {
    CHANNEL_LIST,
    CHANNEL_FREQ,
    CHANNEL_NAME,
    CHANNEL_MOD,
    CHANNEL_BW,
    CHANNEL_POWER,
    CHANNEL_STEP,
    CHANNEL_OFFSET,
    CHANNEL_DIRECTION,
    CHANNEL_REVERSE,
    CHANNEL_CTCSS_RX,
    CHANNEL_CTCSS_TX,
    CHANNEL_DCS_RX,
    CHANNEL_DCS_TX,
    CHANNEL_FIELDS
};
static const uint8_t channelFieldOrder[] = {
    CHANNEL_LIST,     CHANNEL_NAME,   CHANNEL_STEP,   CHANNEL_FREQ,      CHANNEL_MOD,
    CHANNEL_BW,       CHANNEL_POWER,  CHANNEL_OFFSET, CHANNEL_DIRECTION, CHANNEL_REVERSE,
    CHANNEL_CTCSS_RX, CHANNEL_CTCSS_TX, CHANNEL_DCS_RX, CHANNEL_DCS_TX};
static const uint8_t bandFieldOrder[] = {CHANNEL_NAME,   CHANNEL_STEP, CHANNEL_FREQ,
                                         CHANNEL_OFFSET, CHANNEL_MOD,  CHANNEL_BW};
static struct {
    uint8_t data[16], name[16];
    ChannelAttributes_t attributes;
    const char *error;
    uint16_t channel;
    uint16_t present;
    uint8_t state, action, field, cursor, length, dot, nameDefined;
    char digits[9];
    bool vfo, creating, sure, help, list, band, history;
} channelEditor;
static __typeof__(channelEditor) listParent;
static uint16_t listMember = UINT16_MAX, listTop = UINT16_MAX;
static uint8_t listMemberRow;
static bool listChild, listDirty, editorConsumedRelease;
static void ListMemberMove(bool down);
static void ListOpenMember(void);
static void ListMemberShortcut(KEY_Code_t key);


// A completed scan-page operation is consumed once by its caller.
static struct {
    uint16_t from, to;
    bool ready, list;
} channelResult;
static void ChannelResult(uint16_t from, uint16_t to)
{
    if (channelEditor.vfo || channelEditor.history || listChild)
        return;
    channelResult.from = from;
    channelResult.to = to;
    channelResult.list = channelEditor.list;
    channelResult.ready = true;
}
bool CHANNEL_TakeResult(uint16_t *from, uint16_t *to, bool *list)
{
    if (!channelResult.ready)
        return false;
    *from = channelResult.from;
    *to = channelResult.to;
    *list = channelResult.list;
    channelResult.ready = false;
    return true;
}
bool CHANNEL_IsActive(void)
{
    return channelEditor.state != CHANNEL_IDLE;
}

// Reuse every VFO glyph; draw the two keypad symbols at the same 5x5 size.
static void EditorText5x5(const char *text, uint8_t x, uint8_t y, uint8_t count)
{
    char upper[24];
    const uint8_t n = count < sizeof(upper) - 1 ? count : sizeof(upper) - 1;
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t c = (uint8_t)text[i];
        upper[i] = c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
        if (!c)
            break;
    }
    upper[n] = 0;
    UI_DrawText5x5(upper, x, y, n);
    static const uint8_t hash[5] = {10, 31, 10, 31, 10}, star[5] = {4, 21, 14, 21, 4},
                         amp[5] = {12, 18, 12, 19, 13};
    for (uint8_t i = 0; i < count && text[i] && x + i * 6 + 4 < 128; ++i) {
        const uint8_t *glyph = text[i] == '#' ? hash : text[i] == '*' ? star : text[i] == '&' ? amp : NULL;
        if (!glyph)
            continue;
        for (uint8_t row = 0; row < 5; ++row)
            for (uint8_t col = 0; col < 5; ++col)
                PutPixel(x + i * 6 + col, y + row, (glyph[row] & (1u << (4 - col))) != 0);
    }
}

// Keep the full-size digits, but give their three-pixel slash a four-pixel advance.
// Only popup key pairs use this spacing; editor grids and VFO glyphs stay unchanged.
static uint8_t EditorPopupAdvance(const char *text, uint8_t i)
{
    return text[i] == ' ' ? 3
           : text[i] == '/' && i && text[i - 1] >= '0' && text[i - 1] <= '9' && text[i + 1] >= '0' &&
                   text[i + 1] <= '9'
               ? 4
               : 6;
}
static void EditorPopupText5x5(const char *text, uint8_t x, uint8_t y, uint8_t count)
{
    for (uint8_t i = 0; i < count && text[i] && x < 123; ++i) {
        const uint8_t advance = EditorPopupAdvance(text, i);
        if (text[i] != ' ') {
            if (advance == 4)
                GUI_DisplaySmallest("/", x, y, false, true);
            else
                EditorText5x5(text + i, x, y, 1);
        }
        x += advance;
    }
}

// Preserve the compact footer text; only # uses the popup glyph.
void CHANNEL_DrawKeysInfosLabel(const char *label)
{
    EditorText5x5("#", 75, 50, 1);
    GUI_DisplaySmallest(label, 128, 50, false, true);
}
void CHANNEL_DrawKeysInfos(void)
{
    CHANNEL_DrawKeysInfosLabel(" Keys Infos");
}

// Retain unfilled positions; the active position is marked only by its white pixel.
static void EditorInvert(uint8_t, uint8_t, uint8_t);
static uint16_t EditorNameMask(const uint8_t *name, uint8_t limit)
{
    uint16_t mask = 0;
    for (uint8_t i = 0; i < limit; ++i)
        if (name[i] && name[i] != 255 && name[i] != ' ')
            mask |= 1u << i;
    return mask;
}
static void ChannelNameEnd(void)
{
    uint8_t cursor = 0, mask = channelEditor.nameDefined;
    while (mask >>= 1)
        ++cursor;
    channelEditor.cursor = cursor;
}
static void ChannelNameText(char *text, uint8_t limit)
{
    for (uint8_t i = 0; i < limit; ++i) {
        const uint8_t c = channelEditor.name[i];
        text[i] = !(channelEditor.nameDefined & (1u << i)) || !c || c == 255 ? '-' : c;
    }
    text[limit] = 0;
}
static void ChannelCursorDot(uint8_t y)
{
    PutPixel((channelEditor.band ? 46 : 40) + channelEditor.cursor * 6, y + 5, false);
}
static void EditorDraftReset(bool creating)
{
    channelEditor.state = CHANNEL_EDIT;
    channelEditor.creating = creating;
    channelEditor.field = channelEditor.band ? CHANNEL_NAME : CHANNEL_LIST;
    channelEditor.cursor = channelEditor.length = channelEditor.dot = 0;
    channelEditor.error = NULL;
    channelEditor.help = false;
    channelEditor.present = 0;
    channelEditor.nameDefined = 0;
    memset(&channelEditor, 0, 32);
    channelEditor.attributes.__val = 0;
}
static void ChannelDone(const char *action);
#include "list_editor.inc"

static void ChannelReturn(void)
{
    if (listChild) {
        channelEditor = listParent;
        listChild = false;
        listDirty = true;
        if (!MonitorValid(listMember) ||
            MR_GetChannelAttributes(listMember)->scanlist != channelEditor.channel) {
            listMember = ListMemberNext(UINT16_MAX, 1);
            listTop = listMember;
            listMemberRow = 0;
            if (listMember == UINT16_MAX)
                channelEditor.field = CHANNEL_NAME;
        }
        gUpdateDisplay = true;
        return;
    }
    const bool vfo = channelEditor.vfo;
    if (listDirty && channelEditor.list)
        ChannelResult(channelEditor.channel, channelEditor.channel);
    listDirty = false;
    memset(&channelEditor, 0, sizeof channelEditor);
    if (vfo)
        gRequestDisplayScreen = DISPLAY_MAIN;
    gUpdateDisplay = true;
}
// One notification path for the three editors, after their storage commit.
static void ChannelDone(const char *action)
{
    char text[8];
    if (action)
        snprintf(text, sizeof text, "%s %s",
                 channelEditor.list   ? "LST"
                 : channelEditor.band ? "BND"
                                      : "CH",
                 action);
    ChannelReturn();
    if (action) {
        UI_DisplayPopup(text);
        ST7565_BlitFullScreen();
        SYSTEM_DelayMs(450);
    }
}
static void ChannelReload(uint16_t ch)
{
    for (uint8_t v = 0; v < 2; ++v)
        if (gEeprom.ScreenChannel[v] == ch) {
            RADIO_ConfigureChannel(v, VFO_CONFIGURE_RELOAD);
            gFlagReconfigureVfos = true;
        }
    MONITOR_Invalidate();
}
static void ChannelDraft(bool creating, bool fromVfo)
{
    if (channelEditor.list) {
        ListDraft(creating);
        return;
    }
    EditorDraftReset(creating);
    channelEditor.present = (1u << CHANNEL_OFFSET) | (1u << CHANNEL_DIRECTION) | (1u << CHANNEL_REVERSE);
    if (!creating) {
        const uint16_t ch = channelEditor.channel;
        PY25Q16_ReadBuffer(ADRESS_CHANNELS + (uint32_t)ch * 16, channelEditor.data, 16);
        PY25Q16_ReadBuffer(ADRESS_CHANNELS_NAMES + (uint32_t)ch * 16, channelEditor.name, 16);
        channelEditor.attributes = *MR_GetChannelAttributes(ch);
        const uint8_t list = channelEditor.attributes.scanlist;
        channelEditor.attributes.scanlist = list == 51                                ? 52
                                            : (list >= 1 && list <= MR_CHANNELS_LIST) ? list
                                            : ch >= BAND_CHANNEL_FIRST                ? 51
                                                                                      : 0;
        channelEditor.present = 1023; /* LIST to REVERSE */
    } else if (listChild) {
        channelEditor.attributes.scanlist = listParent.channel;
        channelEditor.present |= 1u << CHANNEL_LIST;
    } else if (fromVfo) {
        const VFO_Info_t *v = gTxVfo;
        memcpy(channelEditor.data, &v->freq_config_RX.Frequency, 4);
        memcpy(channelEditor.data + 4, &v->TX_OFFSET_FREQUENCY, 4);
        channelEditor.data[8] = v->freq_config_RX.Code;
        channelEditor.data[9] = v->freq_config_TX.Code;
        channelEditor.data[10] = (v->freq_config_TX.CodeType << 4) | v->freq_config_RX.CodeType;
        channelEditor.data[11] = (v->Modulation << 4) | v->TX_OFFSET_FREQUENCY_DIRECTION;
        channelEditor.data[12] = (v->BUSY_CHANNEL_LOCK << 5) | (v->OUTPUT_POWER << 2) |
                                 (v->CHANNEL_BANDWIDTH << 1) | v->FrequencyReverse;
        channelEditor.data[14] = v->STEP_SETTING;
        channelEditor.attributes.band = v->Band;
        channelEditor.attributes.compander = v->Compander;
        // A free VFO supplies radio values, never a memory name/membership.
        channelEditor.attributes.scanlist = 0;
        channelEditor.present = 1023 & ~((1u << CHANNEL_LIST) | (1u << CHANNEL_NAME));
    }
    if (creating && channelEditor.band) {
        channelEditor.attributes.scanlist = 51;
        channelEditor.data[11] = TX_OFFSET_FREQUENCY_DIRECTION_ADD;
        // Band power is hidden; keep the zero-initialized Low value for new records.
        channelEditor.present |= (1u << CHANNEL_LIST) | (1u << CHANNEL_POWER);
    }
    if (channelEditor.band) {
        channelEditor.attributes.scanlist = 51;
        channelEditor.data[11] = (channelEditor.data[11] & 0xf0) | TX_OFFSET_FREQUENCY_DIRECTION_ADD;
    }
    channelEditor.nameDefined = EditorNameMask(channelEditor.name, CHANNEL_NAME_LEN);
    ChannelNameEnd();
    gUpdateDisplay = true;
}
static void ListOpenMember(void)
{
    listParent = channelEditor;
    listChild = true;
    editorConsumedRelease = true;
    memset(&channelEditor, 0, sizeof channelEditor);
    channelEditor.channel = listMember;
    channelEditor.state = CHANNEL_ACTIONS;
}

void CHANNEL_OpenVfo(void)
{
    if (gCurrentFunction == FUNCTION_TRANSMIT)
        return;
    memset(&channelEditor, 0, sizeof channelEditor);
    channelEditor.vfo = true;
    channelEditor.channel = gTxVfo->CHANNEL_SAVE;
    if (IS_MR_CHANNEL(channelEditor.channel)) {
        channelEditor.band = channelEditor.channel >= BAND_CHANNEL_FIRST;
        channelEditor.state = CHANNEL_ACTIONS;
    } else
        ChannelDraft(true, true);
    gRequestDisplayScreen = DISPLAY_CHANNEL;
    gUpdateDisplay = true;
}
void CHANNEL_OpenSelection(uint16_t channel, bool list)
{
    memset(&channelEditor, 0, sizeof channelEditor);
    channelResult.ready = false;
    listChild = listDirty = editorConsumedRelease = false;
    channelEditor.list = list;
    channelEditor.band = !list;
    channelEditor.channel = channel;
    channelEditor.state = CHANNEL_ACTIONS;
    if (channel == UINT16_MAX)
        channelEditor.action = 1;
    gUpdateDisplay = true;
}
static bool ChannelAcceptFrequency(void)
{
    if (!channelEditor.length)
        return true;
    const uint8_t whole =
        channelEditor.dot ? channelEditor.dot : (channelEditor.length < 3 ? channelEditor.length : 3);
    uint32_t f = INPUTBOX_ParseFrequency(channelEditor.digits, channelEditor.length, channelEditor.dot);
    const bool offset = channelEditor.field == CHANNEL_OFFSET;
    if (!offset && channelEditor.length - whole < 5 && (channelEditor.present & (1u << CHANNEL_STEP)) &&
        channelEditor.data[14] < STEP_N_ELEM)
        f = FREQUENCY_RoundToStep(f, gStepFrequencyTable[channelEditor.data[14]]);
    if (offset && channelEditor.attributes.scanlist != 51 ? f > 99999990u : RX_freq_check(f) < 0) {
        channelEditor.error = offset ? "INVALID OFFSET" : "INVALID FREQ";
        return false;
    }
    memcpy(channelEditor.data + (offset ? 4 : 0), &f, 4);
    channelEditor.present |= 1u << channelEditor.field;
    channelEditor.length = channelEditor.dot = 0;
    return true;
}
static void ChannelErase(uint16_t ch)
{
    uint8_t bytes[16];
    memset(bytes, 255, 16);
    PY25Q16_WriteBuffer(ADRESS_CHANNELS + (uint32_t)ch * 16, bytes, 16, false);
    memset(bytes, 0, 16);
    PY25Q16_WriteBuffer(ADRESS_CHANNELS_NAMES + (uint32_t)ch * 16, bytes, 16, false);
    ChannelAttributes_t a = {0};
    a.band = 7;
    MR_SetChannelAttributes(ch, &a);
}
static int ChannelFree(uint8_t list)
{
    uint16_t first = MR_CHANNEL_FIRST;
    if (list >= 1 && list <= MR_CHANNELS_LIST) {
        for (uint16_t ch = MR_CHANNEL_FIRST; ch < BAND_CHANNEL_FIRST; ++ch)
            if (MonitorValid(ch) && MR_GetChannelAttributes(ch)->scanlist == list &&
                MonitorReadFrequency(ch) != UINT32_MAX)
                first = ch + 1;
    }
    int free = MonitorFree(first);
    return free < 0 && first ? MonitorFree(MR_CHANNEL_FIRST) : free;
}
// Shared commit for the interactive editor and the compact History copier.
static bool ChannelWrite(uint16_t ch, const uint8_t *data, const uint8_t *name, const ChannelAttributes_t *a)
{
    PY25Q16_BeginBatch();
    PY25Q16_WriteBuffer(ADRESS_CHANNELS + (uint32_t)ch * 16, data, 16, false);
    PY25Q16_WriteBuffer(ADRESS_CHANNELS_NAMES + (uint32_t)ch * 16, name, 16, false);
    MR_SetChannelAttributes(ch, a);
    PY25Q16_EndBatch();
    return PY25Q16_IsHealthy();
}
int16_t CHANNEL_SaveHistoryFrequency(uint32_t f, uint8_t modulation, uint8_t narrow, uint16_t css)
{
    if (RX_freq_check(f) < 0)
        return -1;
    int16_t free = -2;
    for (uint16_t ch = MR_CHANNEL_FIRST; ch < BAND_CHANNEL_FIRST; ++ch) {
        const uint32_t stored = MonitorReadFrequency(ch);
        const bool valid = MonitorValid(ch) && stored && stored != UINT32_MAX;
        if (valid && stored == f)
            return -1;
        if (!valid && free == -2)
            free = ch;
    }
    if (free < 0)
        return free;
    uint8_t data[16] = {0}, name[16] = {0};
    memcpy(data, &f, sizeof f);
    data[8] = data[9] = (uint8_t)css;
    data[10] = (css >> 8) * 17u;
    data[11] = modulation << 4;
    data[12] = !!narrow << 1;
    data[14] = STEP_12_5kHz;
    const ChannelAttributes_t a = {.band = FREQUENCY_GetBand(f)};
    if (!ChannelWrite(free, data, name, &a))
        return -3;
    MONITOR_Invalidate();
    return free;
}

static void ChannelSave(void)
{
    if (channelEditor.list) {
        ListSave();
        return;
    }
    if (!ChannelAcceptFrequency())
        return;
    uint32_t f;
    memcpy(&f, channelEditor.data, 4);
    if (!(channelEditor.present & (1u << CHANNEL_FREQ)) || RX_freq_check(f) < 0) {
        channelEditor.error = "INVALID FREQ";
        channelEditor.field = CHANNEL_FREQ;
        return;
    }
    // New drafts require an explicit choice for each radio parameter.
    const uint8_t required =
        (1u << CHANNEL_MOD) | (1u << CHANNEL_BW) | (1u << CHANNEL_POWER) | (1u << CHANNEL_STEP);
    if ((channelEditor.present & required) != required) {
        channelEditor.error = "SET VALUES";
        for (uint8_t i = CHANNEL_MOD; i <= CHANNEL_STEP; ++i)
            if (!(channelEditor.present & (1u << i))) {
                channelEditor.field = i;
                break;
            }
        return;
    }
    uint16_t ch = channelEditor.channel;
    const bool band = channelEditor.attributes.scanlist == 51;
    if (band) {
        uint32_t stop;
        memcpy(&stop, channelEditor.data + 4, 4);
        if (RX_freq_check(stop) < 0 || stop <= f) {
            channelEditor.error = "INVALID END";
            channelEditor.field = CHANNEL_OFFSET;
            return;
        }
        channelEditor.data[11] = (channelEditor.data[11] & 0xf0) | TX_OFFSET_FREQUENCY_DIRECTION_ADD;
    }
    if (channelEditor.creating || band != (ch >= BAND_CHANNEL_FIRST)) {
        const int free = band ? ChannelBandFree() : ChannelFree(channelEditor.attributes.scanlist);
        if (free < 0) {
            channelEditor.error = "MEM FULL";
            return;
        }
        ch = free;
    }
    const ChannelAttributes_t old = *MR_GetChannelAttributes(ch);
    if (channelEditor.attributes.scanlist == 52 && (channelEditor.creating || old.scanlist != 51) &&
        MONITOR_Count() >= MONITOR_LIMIT) {
        channelEditor.error = "MONI FULL";
        return;
    }
    if (channelEditor.attributes.scanlist >= 51)
        channelEditor.attributes.scanlist = band ? 0 : 51;
    channelEditor.attributes.band = FREQUENCY_GetBand(f);
    memset(channelEditor.name + CHANNEL_NAME_LEN, 0, band ? 16 - CHANNEL_NAME_LEN : 10 - CHANNEL_NAME_LEN);
    if (!ChannelWrite(ch, channelEditor.data, channelEditor.name, &channelEditor.attributes)) {
        channelEditor.error = "FLASH ERROR";
        return;
    }
    if (!channelEditor.creating && ch != channelEditor.channel) {
        for (uint8_t v = 0; v < 2; ++v)
            if (gEeprom.ScreenChannel[v] == channelEditor.channel) {
                gEeprom.ScreenChannel[v] = gEeprom.MrChannel[v] = ch;
                gRequestSaveVFO = true;
            }
        ChannelErase(channelEditor.channel);
    }
    const bool added = channelEditor.creating;
    ChannelResult(added ? UINT16_MAX : channelEditor.channel, ch);
    ChannelReload(ch);
    channelEditor.band = band;
    ChannelDone(added ? "ADD" : NULL);
}
static void ChannelDelete(void)
{
    if (channelEditor.list) {
        ListDelete();
        return;
    }
    const uint16_t ch = channelEditor.channel;
    // Keep memory mode and the scanlist navigation order when another memory
    // exists. Select before erasing, while the deleted channel still has its list.
    const uint16_t next = NextMemoryByList(ch, -1);
    if (next != UINT16_MAX && next != ch) {
        for (uint8_t v = 0; v < 2; ++v)
            if (gEeprom.ScreenChannel[v] == ch) {
                gEeprom.ScreenChannel[v] = gEeprom.MrChannel[v] = next;
                RADIO_ConfigureChannel(v, VFO_CONFIGURE_RELOAD);
                gRequestSaveVFO = true;
                gFlagReconfigureVfos = true;
            }
    }
    // With no remaining memory, preserve the existing frequency-mode fallback.
    // Only VFOs displaying this physical memory are affected.
    MonitorRetarget(ch);
    ChannelErase(ch);
    ChannelResult(ch, UINT16_MAX);
    MONITOR_Invalidate();
    channelEditor.band = ch >= BAND_CHANNEL_FIRST;
    ChannelDone("DEL");
}
static NATIVE_CODE(editkeys) __attribute__((optimize("Oz"))) void ChannelValue(int direction)
{
    const uint8_t field = channelEditor.field;
    uint8_t value = 0, count = 0;
    if (field >= CHANNEL_CTCSS_RX) {
        const bool tx = field == CHANNEL_CTCSS_TX || field == CHANNEL_DCS_TX;
        const bool digital = field >= CHANNEL_DCS_RX;
        const uint8_t shift = tx ? 4 : 0, code = channelEditor.data[8 + tx];
        const uint8_t type = (channelEditor.data[10] >> shift) & 15;
        const unsigned count = digital ? 2 * DCS_OPTION_COUNT : ARRAY_SIZE(CTCSS_Options);
        unsigned choice = 0;
        if (digital && (type == CODE_TYPE_DIGITAL || type == CODE_TYPE_REVERSE_DIGITAL) &&
            code < DCS_OPTION_COUNT)
            choice = 1 + code + (type == CODE_TYPE_REVERSE_DIGITAL ? DCS_OPTION_COUNT : 0);
        else if (!digital && type == CODE_TYPE_CONTINUOUS_TONE && code < ARRAY_SIZE(CTCSS_Options))
            choice = code + 1;
        choice = direction > 0 ? (choice < count ? choice + 1 : 0) : (choice ? choice - 1 : count);
        uint8_t newType = CODE_TYPE_OFF;
        if (choice) {
            --choice;
            newType = digital ? (choice >= DCS_OPTION_COUNT ? CODE_TYPE_REVERSE_DIGITAL : CODE_TYPE_DIGITAL)
                              : CODE_TYPE_CONTINUOUS_TONE;
            channelEditor.data[8 + tx] = digital ? choice % DCS_OPTION_COUNT : choice;
        } else
            channelEditor.data[8 + tx] = 0;
        channelEditor.data[10] = (channelEditor.data[10] & ~(15u << shift)) | (newType << shift);
        channelEditor.present |= (1u << CHANNEL_CTCSS_RX) | (1u << CHANNEL_CTCSS_TX) |
                                 (1u << CHANNEL_DCS_RX) | (1u << CHANNEL_DCS_TX);
        return;
    }
    if (field == CHANNEL_LIST) {
        if (channelEditor.list) {
            ListSlotValue(direction);
            return;
        }
        value = channelEditor.attributes.scanlist;
        if (!(channelEditor.present & 1))
            value = direction > 0 ? 0 : 1;
        for (uint8_t i = 0; i < 53; ++i) {
            value = direction > 0 ? (value < 52 ? value + 1 : 0) : (value ? value - 1 : 52);
            if (value == 0 || value >= 51 || (value <= MR_CHANNELS_LIST && ListNamed(value)))
                break;
        }
        channelEditor.attributes.scanlist = value;
    } else {
        static const uint8_t fields[CHANNEL_FIELDS][4] NATIVE_DATA(editkeys) = {
            [CHANNEL_MOD] = {11, 4, 15, MODULATION_UKNOWN},
            [CHANNEL_BW] = {12, 1, 1, 2},
            [CHANNEL_POWER] = {12, 2, 7, OUTPUT_POWER_LEN},
            [CHANNEL_STEP] = {14, 0, 255, STEP_N_ELEM},
            [CHANNEL_DIRECTION] = {11, 0, 15, 3},
            [CHANNEL_REVERSE] = {12, 0, 1, 2}};
        if (field >= CHANNEL_FIELDS)
            return;
        const uint8_t *p = fields[field];
        count = p[3];
        if (!count)
            return;
        uint8_t *byte = &channelEditor.data[p[0]];
        value = (*byte >> p[1]) & p[2];
        if (field == CHANNEL_POWER && value < count)
            value = POWER_ToIndex(value);
        if (!(channelEditor.present & (1u << field)) || value >= count)
            value = direction > 0 ? 0 : count - 1;
        else
            value = direction > 0 ? (value + 1 < count ? value + 1 : 0) : (value ? value - 1 : count - 1);
        if (field == CHANNEL_POWER)
            value = POWER_FromIndex(value);
        *byte = (*byte & ~(p[2] << p[1])) | (value << p[1]);
    }
    channelEditor.present |= 1u << field;
}
static NATIVE_CODE(editkeys) void ChannelLetter(KEY_Code_t key)
{
    static const char letters[] NATIVE_DATA(editkeys) = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    static const char symbols[] NATIVE_DATA(editkeys) = " 0123456789-+./#*";
    const char *alphabet = (key == KEY_4 || key == KEY_6) ? letters : symbols;
    const int direction = (key == KEY_6 || key == KEY_9) ? 1 : -1;
    char *current = (char *)channelEditor.name + channelEditor.cursor;
    const char *p =
        (channelEditor.nameDefined & (1u << channelEditor.cursor)) ? strchr(alphabet, *current) : NULL;
    const uint8_t count = strlen(alphabet);
    uint8_t index = p ? p - alphabet : (direction > 0 ? count - 1 : 0);
    index = direction > 0 ? (index + 1) % count : (index ? index - 1 : count - 1);
    // Space-fill gaps so an empty leading position cannot hide later letters.
    const uint8_t limit = channelEditor.list ? 6 : CHANNEL_NAME_LEN;
    for (uint8_t i = 0; i < limit; ++i)
        if (!channelEditor.name[i])
            channelEditor.name[i] = ' ';
    *current = alphabet[index];
    channelEditor.nameDefined |= 1u << channelEditor.cursor;
    memset(channelEditor.name + limit, 0, 10 - limit);
    channelEditor.present |= 1u << CHANNEL_NAME;
}
static NATIVE_CODE(editkeys) bool ChannelEraseInput(void)
{
    if ((channelEditor.field == CHANNEL_FREQ || channelEditor.field == CHANNEL_OFFSET) &&
        INPUTBOX_Backspace(&channelEditor.length, &channelEditor.dot))
        return true;
    if (channelEditor.field == CHANNEL_NAME && channelEditor.nameDefined) {
        const uint8_t limit = channelEditor.list ? 6 : CHANNEL_NAME_LEN;
        if (!(channelEditor.nameDefined & (1u << channelEditor.cursor)))
            ChannelNameEnd();
        memmove(channelEditor.name + channelEditor.cursor, channelEditor.name + channelEditor.cursor + 1,
                limit - channelEditor.cursor - 1);
        channelEditor.name[limit - 1] = 0;
        channelEditor.nameDefined = EditorNameMask(channelEditor.name, limit);
        channelEditor.present |= 1u << CHANNEL_NAME;
        if (channelEditor.cursor)
            --channelEditor.cursor;
        return true;
    }
    return false;
}
static NATIVE_CODE(editkeys) void ChannelProcessKeys(KEY_Code_t key, bool pressed, bool held)
{
    if (!pressed && !held && editorConsumedRelease) {
        editorConsumedRelease = false;
        return;
    }
    gUpdateDisplay = true;
    if (key == KEY_PTT) {
        const bool vfo = channelEditor.vfo;
        ChannelReturn();
        if (vfo)
            GENERIC_Key_PTT(pressed);
        return;
    }
    if (key == KEY_EXIT && pressed && held) {
        ChannelReturn();
        return;
    }
    if (channelEditor.help) {
        if (!pressed && !held)
            channelEditor.help = false;
        return;
    }
    if (CHANNEL_CanLongPress(key) && pressed && held) {
        editorConsumedRelease = true;
        ListMemberShortcut(key);
        return;
    }
    if (channelEditor.vfo && gSetting_nav_invert) {
        if (key == KEY_UP)
            key = KEY_DOWN;
        else if (key == KEY_DOWN)
            key = KEY_UP;
    }
    if (pressed)
        channelEditor.error = NULL;
    if (channelEditor.state == CHANNEL_ACTIONS) {
        if ((key == KEY_UP || key == KEY_DOWN) && pressed && !channelEditor.sure)
            channelEditor.action =
                key == KEY_DOWN ? (channelEditor.action + 1) % 3 : (channelEditor.action + 2) % 3;
        if (pressed || held)
            return;
        if (key == KEY_EXIT) {
            if (channelEditor.sure)
                channelEditor.sure = false;
            else
                ChannelReturn();
        } else if (key == KEY_MENU) {
            if (channelEditor.channel == UINT16_MAX && channelEditor.action != 1)
                channelEditor.error = "NO ITEM";
            else if (channelEditor.action == 2) {
                if (channelEditor.sure)
                    ChannelDelete();
                else
                    channelEditor.sure = true;
            } else if (!channelEditor.list && channelEditor.action != 1 &&
                       !IS_MR_CHANNEL(channelEditor.channel))
                channelEditor.error = "NO CHANNEL";
            else
                ChannelDraft(channelEditor.action == 1, false);
        }
        return;
    }
    if (key == KEY_EXIT && !pressed && !held) {
        if (ChannelEraseInput())
            return;
        ChannelReturn();
        return;
    }
    if (key == KEY_MENU && !pressed && !held) {
        ChannelSave();
        return;
    }
    if (key == KEY_F && !pressed && !held) {
        channelEditor.help = true;
        return;
    }
    if (key == KEY_UP || key == KEY_DOWN) {
        if (pressed) {
            const uint8_t previousField = channelEditor.field;
            if (channelEditor.list) {
                if (channelEditor.creating)
                    channelEditor.field = channelEditor.field == CHANNEL_NAME ? CHANNEL_LIST : CHANNEL_NAME;
                else
                    ListMemberMove(key == KEY_DOWN);
            } else if (ChannelAcceptFrequency()) {
                const uint8_t *order = channelEditor.band ? bandFieldOrder : channelFieldOrder;
                const uint8_t count = channelEditor.band ? ARRAY_SIZE(bandFieldOrder) : CHANNEL_FIELDS;
                uint8_t index = 0;
                while (index < count - 1 && order[index] != channelEditor.field)
                    ++index;
                index = key == KEY_DOWN ? (index + 1) % count : (index + count - 1) % count;
                channelEditor.field = order[index];
            }
            if (channelEditor.field == CHANNEL_NAME && previousField != CHANNEL_NAME)
                ChannelNameEnd();
        }
        return;
    }
    if (channelEditor.field == CHANNEL_FREQ || channelEditor.field == CHANNEL_OFFSET) {
        if (pressed || held)
            return;
        if (key >= KEY_0 && key <= KEY_9) {
            const uint8_t max = channelEditor.dot ? channelEditor.dot + 5 : 8;
            if (channelEditor.length < max && channelEditor.length < sizeof channelEditor.digits)
                channelEditor.digits[channelEditor.length++] = '0' + key - KEY_0;
        } else if (key == KEY_STAR && channelEditor.length && channelEditor.length <= 4 && !channelEditor.dot)
            channelEditor.dot = channelEditor.length;
    } else if (channelEditor.field == CHANNEL_NAME) {
        if (pressed && (key == KEY_4 || key == KEY_6 || key == KEY_7 || key == KEY_9))
            ChannelLetter(key);
        if (pressed && !held && (key == KEY_1 || key == KEY_3)) {
            const uint8_t limit = channelEditor.list ? 6 : CHANNEL_NAME_LEN;
            channelEditor.cursor = key == KEY_3 ? (channelEditor.cursor + 1) % limit
                                                : (channelEditor.cursor + limit - 1) % limit;
        }
    } else if (pressed && (key == KEY_1 || key == KEY_3)) {
        ChannelValue(key == KEY_3 ? 1 : -1);
    }
}
bool CHANNEL_CanLongPress(KEY_Code_t key)
{
    if (key == KEY_EXIT)
        return CHANNEL_IsActive();
    return channelEditor.state == CHANNEL_EDIT && !channelEditor.help && channelEditor.list &&
           !channelEditor.creating && channelEditor.field == CHANNEL_FIELDS &&
           (key == KEY_4 || key == KEY_MENU);
}
bool CHANNEL_CanRepeat(KEY_Code_t key)
{
    return channelEditor.state == CHANNEL_EDIT && !channelEditor.help &&
           channelEditor.field == CHANNEL_NAME &&
           (key == KEY_4 || key == KEY_6 || key == KEY_7 || key == KEY_9);
}
static void ChannelBox(uint8_t x, uint8_t y, uint8_t right, uint8_t bottom)
{
    UI_DrawPopupFrame(x, y, right, bottom, 55);
}
static void EditorInvert(uint8_t y, uint8_t left, uint8_t right)
{
    for (uint8_t py = y; py < y + 7; ++py)
        for (uint8_t x = left; x < right; ++x)
            gFrameBuffer[py / 8][x] ^= 1u << (py % 8);
}
static NATIVE_CODE(editor) void ChannelDisplay(void)
{
    if (channelEditor.state == CHANNEL_ACTIONS) {
        ChannelBox(16, 9, 113, 47);
        static const char *const actions[] NATIVE_DATA(editor) = {ns_editor_0, ns_editor_1, ns_editor_2};
        const char *kind = channelEditor.list ? ns_editor_3 : channelEditor.band ? ns_editor_4 : ns_editor_5;
        for (uint8_t row = 0; row < 3; ++row) {
            char text[16];
            snprintf(text, sizeof text, ns_editor_6, actions[row], kind);
            EditorText5x5(text, 24, 13 + row * 9, strlen(text));
            if (row == channelEditor.action) {
                const uint8_t y = 12 + row * 9;
                EditorInvert(y, 20, 110);
                for (uint8_t x = 20; x <= 109; x += 89) {
                    PutPixel(x, y, false);
                    PutPixel(x, y + 6, false);
                }
            }
        }
        if (channelEditor.sure)
            EditorText5x5(channelEditor.list ? ns_editor_7 : ns_editor_8, 46, 40, channelEditor.list ? 7 : 6);
        else if (channelEditor.error)
            EditorText5x5(channelEditor.error, 34, 40, 10);
    } else if (channelEditor.list) {
        ListEditorDisplay();
    } else {
        memset(gFrameBuffer, 0, sizeof gFrameBuffer);
        UI_PrintStringSmallBold(channelEditor.band ? (channelEditor.creating ? ns_editor_9 : ns_editor_10)
                                                   : (channelEditor.creating ? ns_editor_11 : ns_editor_12),
                                0, 127, 0);
        static const char *const labels[] NATIVE_DATA(editor) = {
            ns_editor_13, ns_editor_14, ns_editor_15, ns_editor_16, ns_editor_17, ns_editor_18, ns_editor_19,
            ns_editor_20, ns_editor_21, ns_editor_48, ns_editor_22, ns_editor_23, ns_editor_24, ns_editor_25};
        const uint8_t *order = channelEditor.band ? bandFieldOrder : channelFieldOrder;
        const uint8_t count = channelEditor.band ? ARRAY_SIZE(bandFieldOrder) : CHANNEL_FIELDS;
        uint8_t selected = 0;
        while (selected < count - 1 && order[selected] != channelEditor.field)
            ++selected;
        const uint8_t first = selected < 6 ? 0 : selected - 5;
        for (uint8_t visible = 0; visible < 6; ++visible) {
            const uint8_t row = order[first + visible];
            char text[24];
            strcpy(text, row == CHANNEL_FREQ || (channelEditor.band && row == CHANNEL_OFFSET) ? ns_editor_26
                         : row == CHANNEL_NAME ? (channelEditor.band ? ns_editor_27 : ns_editor_28)
                                               : ns_editor_29);
            const uint8_t y = 11 + visible * 6;
            if (row >= CHANNEL_CTCSS_RX) {
                const bool tx = row == CHANNEL_CTCSS_TX || row == CHANNEL_DCS_TX;
                const bool digital = row >= CHANNEL_DCS_RX;
                const DCS_CodeType_t type = (channelEditor.data[10] >> (tx ? 4 : 0)) & 15;
                const uint8_t option = channelEditor.data[8 + tx];
                if (option < (digital ? DCS_OPTION_COUNT : ARRAY_SIZE(CTCSS_Options)) &&
                    ((digital && (type == CODE_TYPE_DIGITAL || type == CODE_TYPE_REVERSE_DIGITAL)) ||
                     (!digital && type == CODE_TYPE_CONTINUOUS_TONE))) {
                    DCS_FormatCode(text, type, option, digital);
                    if (!digital)
                        strcat(text, ns_editor_30);
                    else if (type == CODE_TYPE_REVERSE_DIGITAL)
                        text[strlen(text) - 1] = 'I';
                } else
                    strcpy(text, ns_editor_31);
            } else if (channelEditor.present & (1u << row)) {
                if (row == CHANNEL_LIST) {
                    const uint8_t list = channelEditor.attributes.scanlist;
                    if (list == 51)
                        strcpy(text, ns_editor_32);
                    else if (list == 0 || (list > MR_CHANNELS_LIST && list != 51 && list != 52))
                        strcpy(text, ns_editor_33);
                    else if (list == 52)
                        strcpy(text, ns_editor_34);
                    else
                        snprintf(text, sizeof text, ns_editor_35, list, gListName[list - 1]);
                } else if (row == CHANNEL_FREQ || row == CHANNEL_OFFSET) {
                    uint32_t f;
                    memcpy(&f, channelEditor.data + (row == CHANNEL_OFFSET ? 4 : 0), 4);
                    if (row == CHANNEL_FREQ || channelEditor.band)
                        CHANNEL_FormatFrequency(text, f);
                    else
                        snprintf(text, sizeof text, ns_editor_36, f / 100000, f % 100000);
                } else if (row == CHANNEL_NAME)
                    ChannelNameText(text, CHANNEL_NAME_LEN);
                else if (row == CHANNEL_MOD) {
                    const uint8_t mod = channelEditor.data[11] >> 4;
                    strcpy(text, mod < MODULATION_UKNOWN ? gModulationStr[mod] : ns_editor_37);
                } else if (row == CHANNEL_BW)
                    strcpy(text, channelEditor.data[12] & 2 ? ns_editor_38 : ns_editor_39);
                else if (row == CHANNEL_POWER) {
                    const uint8_t power = (channelEditor.data[12] >> 2) & 7;
                    strcpy(text, power < OUTPUT_POWER_LEN ? gPowerNames[power] : ns_editor_40);
                } else if (row == CHANNEL_STEP) {
                    const uint8_t step = channelEditor.data[14];
                    if (step < STEP_N_ELEM)
                        FP_CopyString(text, scanStepNames[step], 6);
                    else
                        strcpy(text, ns_editor_41);
                } else if (row == CHANNEL_REVERSE) {
                    strcpy(text, channelEditor.data[12] & 1 ? ns_editor_49 : ns_editor_31);
                } else if (row == CHANNEL_DIRECTION) {
                    const uint8_t direction = channelEditor.data[11] & 15;
                    strcpy(text, direction == 1   ? ns_editor_42
                                 : direction == 2 ? ns_editor_43
                                                  : ns_editor_44);
                }
            }
            if ((row == CHANNEL_FREQ || row == CHANNEL_OFFSET) && row == channelEditor.field &&
                channelEditor.length) {
                const uint8_t whole = channelEditor.dot ? channelEditor.dot : 3;
                uint8_t out = 0;
                for (uint8_t i = 0; i < channelEditor.length; ++i) {
                    if (i == whole)
                        text[out++] = '.';
                    text[out++] = channelEditor.digits[i];
                }
                if (channelEditor.dot && channelEditor.length == whole)
                    text[out++] = '.';
                text[out++] = '_';
                text[out] = 0;
            }
            const char *label = channelEditor.band && row == CHANNEL_BW       ? ns_editor_45
                                : channelEditor.band && row == CHANNEL_FREQ   ? ns_editor_46
                                : channelEditor.band && row == CHANNEL_OFFSET ? ns_editor_47
                                                                              : labels[row];
            EditorText5x5(label, 2, y, strlen(label));
            EditorText5x5(text, row >= CHANNEL_REVERSE ? 62 : channelEditor.band ? 44 : 38, y, strlen(text));
            if (row == channelEditor.field)
                EditorInvert(y - 1, 0, 128);
            if (row == CHANNEL_NAME && row == channelEditor.field)
                ChannelCursorDot(y);
        }
        if (channelEditor.error)
            EditorText5x5(channelEditor.error, 2, 50, strlen(channelEditor.error));
        else
            CHANNEL_DrawKeysInfos();
    }
    if (channelEditor.help)
        ChannelHelpDisplay();
    ST7565_BlitFullScreen();
}

/* The editor keys run as a native module (NATIVE_EDITKEYS). */
void CHANNEL_ProcessKeys(KEY_Code_t key, bool pressed, bool held)
{
    if (CHANNEL_IsActive() && APP_NativeBegin(NATIVE_EDITKEYS)) {
        ChannelProcessKeys(key, pressed, held);
        APP_NativeEnd();
    }
}
void CHANNEL_Display(void)
{
    if (channelEditor.state == CHANNEL_ACTIONS && channelEditor.vfo)
        UI_DisplayMain();
    if (APP_NativeBegin(NATIVE_EDITOR)) {
        if (channelEditor.state == CHANNEL_ACTIONS && listChild) {
            const __typeof__(channelEditor) child = channelEditor;
            channelEditor = listParent;
            ListEditorDisplay();
            channelEditor = child;
        }
        ChannelDisplay();
        APP_NativeEnd();
    }
    if (channelEditor.vfo)
        UI_DisplayStatus();
}

void CHANNEL_OpenHistory(const VFO_Info_t *draft)
{
    memset(&channelEditor, 0, sizeof channelEditor);
    const VFO_Info_t *saved = gTxVfo;
    gTxVfo = (VFO_Info_t *)draft;
    ChannelDraft(true, true);
    gTxVfo = (VFO_Info_t *)saved;
    channelEditor.vfo = false;
    channelEditor.history = true;
    channelResult.ready = false;
    gUpdateDisplay = true;
}

void CHANNEL_FormatFrequency(char *text, uint32_t f)
{
    snprintf(text, 24, f < 100000000u ? "%03u.%05u  MHz" : "%u.%05u MHz", f / 100000, f % 100000);
}
void CHANNEL_DrawRangeField(const char *label, const char *value, uint8_t y, bool selected)
{
    EditorText5x5(label, 2, y, strlen(label));
    EditorText5x5(value, 44, y, strlen(value));
    if (selected)
        EditorInvert(y - 1, 0, 128);
}
