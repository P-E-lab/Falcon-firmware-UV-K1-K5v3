static const char ns_editor_105[] NATIVE_DATA(editor) = "NEW LIST";
static const char ns_editor_106[] NATIVE_DATA(editor) = "EDIT LIST";
static const char ns_editor_107[] NATIVE_DATA(editor) = "%02u";
static const char ns_editor_108[] NATIVE_DATA(editor) = "select >";
static const char ns_editor_109[] NATIVE_DATA(editor) = "------";
static const char ns_editor_110[] NATIVE_DATA(editor) = "NAME:";
static const char ns_editor_111[] NATIVE_DATA(editor) = "LIST:";
/* Shared list operations use existing memory and NOR layouts. No second editor. */
static void ChannelReturn(void);
static void ChannelBox(uint8_t, uint8_t, uint8_t, uint8_t);
static void ChannelDisplay(void);
static bool ListNamed(uint8_t slot)
{
    if (!slot || slot > MR_CHANNELS_LIST)
        return false;
    for (uint8_t i = 0; i < 6; ++i) {
        const uint8_t c = gListName[slot - 1][i];
        if (!c || c == 255)
            break;
        if (c != ' ')
            return true;
    }
    return false;
}
static int ChannelBandFree(void)
{
    for (uint16_t ch = BAND_CHANNEL_FIRST; ch <= MR_CHANNEL_LAST; ++ch)
        if (!MonitorValid(ch) || !MonitorReadFrequency(ch) || MonitorReadFrequency(ch) == UINT32_MAX)
            return ch;
    return -1;
}
static void ListDraft(bool creating)
{
    EditorDraftReset(creating);
    listMember = listTop = UINT16_MAX;
    listMemberRow = 0;
    if (!creating) {
        memcpy(channelEditor.name, gListName[channelEditor.channel - 1], 6);
        channelEditor.nameDefined = EditorNameMask(channelEditor.name, 6);
        ChannelNameEnd();
        channelEditor.attributes.scanlist = channelEditor.channel;
        channelEditor.present = (1u << CHANNEL_LIST);
        if (ListNamed(channelEditor.channel))
            channelEditor.present |= 1u << CHANNEL_NAME;
    }
}
static void ListSlotValue(int direction)
{
    uint8_t slot = channelEditor.attributes.scanlist;
    if (slot > MR_CHANNELS_LIST)
        slot = 0;
    for (uint8_t i = 0; i < MR_CHANNELS_LIST; ++i) {
        slot = direction > 0 ? (slot < MR_CHANNELS_LIST ? slot + 1 : 1)
                             : (slot > 1 ? slot - 1 : MR_CHANNELS_LIST);
        if (!ListNamed(slot) || (!channelEditor.creating && slot == channelEditor.channel)) {
            channelEditor.attributes.scanlist = slot;
            channelEditor.present |= 1u << CHANNEL_LIST;
            return;
        }
    }
    channelEditor.error = "NO FREE SLOT";
}
static void ListWriteName(uint8_t slot, const uint8_t *name)
{
    uint8_t bytes[10] = {0};
    memcpy(bytes, name, 6);
    if (!memcmp(gListName[slot - 1], bytes, 6))
        return;
    PY25Q16_WriteBuffer(0x008900 + (uint32_t)(slot - 1) * 10, bytes, 10, false);
    memcpy(gListName[slot - 1], bytes, 6);
    gListName[slot - 1][6] = 0;
}
// Move only the membership attribute. Delete in data/name/attribute passes:
// each NOR sector is committed once per pass, in physical channel order.
static void ListRewrite(uint8_t from, uint8_t to, bool deleting)
{
    uint8_t affected = 0;
    for (uint8_t v = 0; v < 2; ++v) {
        const uint16_t ch = gEeprom.ScreenChannel[v];
        if (!IS_MR_CHANNEL(ch))
            continue;
        const ChannelAttributes_t *a = MR_GetChannelAttributes(ch);
        if (a->band != 7 && a->scanlist == from) {
            affected |= 1u << v;
            if (deleting)
                MonitorRetarget(ch);
        }
    }
    ChannelAttributes_t block[32];
    for (uint8_t pass = deleting ? 0 : 2; pass < 3; ++pass) {
        PY25Q16_BeginBatch();
        for (uint16_t first = 0; first <= MR_CHANNEL_LAST; first += 32) {
            const uint16_t n = MR_CHANNEL_LAST - first + 1 < 32 ? MR_CHANNEL_LAST - first + 1 : 32;
            PY25Q16_ReadBuffer(ADRESS_CHANNELS_ATTRIBUTES + (uint32_t)first * sizeof *block, block,
                               n * sizeof *block);
            for (uint8_t i = 0; i < n; ++i)
                if (block[i].band != 7 && block[i].scanlist == from) {
                    const uint16_t ch = first + i;
                    if (pass < 2) {
                        uint8_t bytes[16];
                        memset(bytes, pass ? 0 : 255, sizeof bytes);
                        PY25Q16_WriteBuffer((pass ? ADRESS_CHANNELS_NAMES : ADRESS_CHANNELS) +
                                                (uint32_t)ch * 16,
                                            bytes, 16, false);
                    } else {
                        if (deleting) {
                            block[i].__val = 0;
                            block[i].band = 7;
                        } else
                            block[i].scanlist = to;
                        MR_SetChannelAttributes(ch, &block[i]);
                    }
                }
        }
        PY25Q16_EndBatch();
    }
    // Preserve active VFOs' displayed memories while updating their membership.
    if (!deleting)
        for (uint8_t v = 0; v < 2; ++v)
            if (affected & (1u << v)) {
                RADIO_ConfigureChannel(v, VFO_CONFIGURE_RELOAD);
                gFlagReconfigureVfos = true;
            }
    MONITOR_Invalidate();
}
static void ListSave(void)
{
    const uint8_t slot = channelEditor.attributes.scanlist;
    if (!(channelEditor.present & (1u << CHANNEL_LIST)) || !slot || slot > MR_CHANNELS_LIST) {
        channelEditor.error = "SELECT SLOT";
        channelEditor.field = CHANNEL_LIST;
        return;
    }
    if (ListNamed(slot) && (channelEditor.creating || slot != channelEditor.channel)) {
        channelEditor.error = "SLOT USED";
        channelEditor.field = CHANNEL_LIST;
        return;
    }
    if (!EditorNameMask(channelEditor.name, 6)) {
        channelEditor.error = "ENTER NAME";
        channelEditor.field = CHANNEL_NAME;
        return;
    }
    const uint8_t from = channelEditor.channel;
    if (!channelEditor.creating && slot != from)
        ListRewrite(from, slot, false);
    PY25Q16_BeginBatch();
    ListWriteName(slot, channelEditor.name);
    if (!channelEditor.creating && slot != from) {
        const uint8_t empty[10] = {0};
        ListWriteName(from, empty);
    }
    PY25Q16_EndBatch();
    ChannelResult(channelEditor.creating ? UINT16_MAX : from, slot);
    MONITOR_Invalidate();
    ChannelDone(channelEditor.creating ? "ADD" : NULL);
}
static void ListDelete(void)
{
    const uint8_t slot = channelEditor.channel;
    ListRewrite(slot, 0, true);
    const uint8_t empty[10] = {0};
    ListWriteName(slot, empty);
    ChannelResult(slot, UINT16_MAX);
    ChannelDone("DEL");
}
static NATIVE_CODE(editor) void ChannelHelpDisplay(void)
{
    const bool members = channelEditor.list && !channelEditor.creating;
    ChannelBox(members ? 0 : 2, members ? 0 : 3, members ? 127 : 125, 53);
    static const char *const help[] = {"< >:NEXT FIELD",     "1/3: VALUE / CURSOR",  "4/6:LETTER SCROLL",
                                       "7/9:N /SYMB SCROLL", "4 LONG: ADD DEL MONI", "M LONG: MANAGE CH",
                                       "M: SAVE & EXIT",     "EXIT:ERASE HOLD:QUIT"};
    for (uint8_t row = 0; row < (members ? 8 : 6); ++row) {
        const char *text = help[!members && row >= 4 ? row + 2 : row];
        EditorPopupText5x5(text, 8, (members ? 2 : 6) + row * (members ? 6 : 8), strlen(text));
    }
    if (!members)
        for (uint8_t x = 36; x <= 38; ++x)
            for (uint8_t y = 30; y <= 32; ++y)
                if (x != 37 || y != 31)
                    PutPixel(x, y, true);
}
// Walk physical memories on demand; no second channel table or name cache.
static uint16_t ListMemberNext(uint16_t ch, int direction)
{
    int next = ch == UINT16_MAX ? (direction > 0 ? 0 : BAND_CHANNEL_FIRST - 1) : (int)ch + direction;
    for (; next >= 0 && next < BAND_CHANNEL_FIRST; next += direction) {
        const ChannelAttributes_t *a = MR_GetChannelAttributes(next);
        if (!a || a->band == 7 || a->scanlist != channelEditor.channel)
            continue;
        const uint32_t f = MonitorReadFrequency(next);
        if (f >= 1800000u && f <= 130000000u)
            return next;
    }
    return UINT16_MAX;
}
static void ListMemberMove(bool down)
{
    const uint8_t field = channelEditor.field;
    if ((down && field == CHANNEL_LIST) || (!down && field == CHANNEL_NAME)) {
        channelEditor.field = down ? CHANNEL_NAME : CHANNEL_LIST;
        return;
    }
    uint16_t next = ListMemberNext(field == CHANNEL_FIELDS ? listMember : UINT16_MAX, down ? 1 : -1);
    if (next == UINT16_MAX) {
        channelEditor.field = down ? CHANNEL_LIST : CHANNEL_NAME;
        return;
    }
    if (field != CHANNEL_FIELDS) {
        listTop = next;
        listMemberRow = 0;
        if (!down)
            for (unsigned i = 0; i < 2; ++i) {
                uint16_t prev = ListMemberNext(listTop, -1);
                if (prev == UINT16_MAX)
                    break;
                listTop = prev;
                ++listMemberRow;
            }
    } else if (down) {
        if (listMemberRow < 2)
            ++listMemberRow;
        else
            listTop = ListMemberNext(listTop, 1);
    } else if (listMemberRow)
        --listMemberRow;
    else
        listTop = next;
    listMember = next;
    channelEditor.field = CHANNEL_FIELDS;
}
static void ListMemberShortcut(KEY_Code_t key)
{
    if (listMember == UINT16_MAX)
        return;
    if (key == KEY_MENU) {
        ListOpenMember();
        return;
    }
    MONITOR_Rebuild();
    monitorNotice = NULL;
    const uint32_t f = MonitorReadFrequency(listMember);
    if (MonitorFind(f) >= 0)
        MonitorRemove(f);
    else
        MonitorAdd(listMember, NULL);
    channelEditor.error = monitorNotice;
}
static void ListMembersDisplay(void)
{
    // Refresh around deletions/moves without discarding unsaved LIST/NAME edits.
    if (listTop == UINT16_MAX || !MonitorValid(listTop) ||
        MR_GetChannelAttributes(listTop)->scanlist != channelEditor.channel)
        listTop = ListMemberNext(UINT16_MAX, 1);
    uint16_t ch = listTop;
    for (uint8_t row = 0; row < 3 && ch != UINT16_MAX; ++row) {
        const uint8_t y = 28 + row * 7;
        char name[12], text[24], freq[12];
        SETTINGS_FetchChannelName(name, ch);
        snprintf(text, sizeof text, "%03u", ch + 1);
        const uint32_t f = MonitorReadFrequency(ch);
        UI_FormatFrequency(freq, f);
        const uint8_t fx = 126 - (strlen(freq) * 4 - 1);
        EditorText5x5(text, 4, y, 4);
        EditorText5x5("-", 25, y, 1);
        EditorText5x5(name, 34, y, (fx - 36) / 6);
        GUI_DisplaySmallest(freq, fx, y, false, true);
        if (MonitorFind(f) >= 0)
            for (uint8_t py = y + 1; py < y + 3; ++py)
                for (uint8_t x = 0; x < 2; ++x)
                    PutPixel(x, py, true);
        if (channelEditor.field == CHANNEL_FIELDS && row == listMemberRow) {
            listMember = ch;
            EditorInvert(y - 1, 0, 128);
        }
        ch = ListMemberNext(ch, 1);
    }
    if (channelEditor.field == CHANNEL_FIELDS && ch == UINT16_MAX && listTop == UINT16_MAX)
        channelEditor.field = CHANNEL_NAME;
}

static NATIVE_CODE(editor) void ListEditorDisplay(void)
{
    memset(gFrameBuffer, 0, sizeof gFrameBuffer);
    UI_PrintStringSmallBold(channelEditor.creating ? ns_editor_105 : ns_editor_106, 0, 127, 0);
    for (uint8_t row = 0; row < 2; ++row) {
        const uint8_t field = row ? CHANNEL_NAME : CHANNEL_LIST,
                      y = channelEditor.creating ? 16 + row * 16 : 11 + row * 8;
        char text[12];
        if (!row) {
            if (channelEditor.present & (1u << CHANNEL_LIST))
                snprintf(text, sizeof text, ns_editor_107, channelEditor.attributes.scanlist);
            else
                strcpy(text, ns_editor_108);
        } else if (channelEditor.present & (1u << CHANNEL_NAME))
            ChannelNameText(text, 6);
        else
            strcpy(text, ns_editor_109);
        EditorText5x5(row ? ns_editor_110 : ns_editor_111, 2, y, 5);
        EditorText5x5(text, 38, y, strlen(text));
        if (channelEditor.field == field)
            EditorInvert(y - 1, 0, 128);
        if (field == CHANNEL_NAME && channelEditor.field == field)
            ChannelCursorDot(y);
    }
    if (!channelEditor.creating)
        ListMembersDisplay();
    if (channelEditor.error)
        GUI_DisplaySmallest(channelEditor.error, 2, 50, false, true);
    else
        CHANNEL_DrawKeysInfos();
}

// Same box, font and keypad glyphs as the editor help; shared by both inventories.
static void EditorKeysPopup(const char *const *lines, uint8_t count, bool centered)
{
    uint8_t x = 4, y = 2, width = 120, height = 5 + count * 8;
    if (centered) {
        unsigned longest = 0;
        for (uint8_t row = 0; row < count; ++row) {
            unsigned length = 0;
            for (uint8_t i = 0; lines[row][i]; ++i)
                length += EditorPopupAdvance(lines[row], i);
            if (length > longest)
                longest = length;
        }
        width = MIN(longest + 7, 120u);
        height = 6 + count * 8;
        x = (128 - width) / 2;
        y = (48 - height) / 2;
    }
    ChannelBox(x, y, x + width, y + height); // and the shadow
    for (unsigned row = 0; row < count; ++row)
        EditorPopupText5x5(lines[row], x + 3, y + 4 + row * 8, strlen(lines[row]));
}
void CHANNEL_DrawParametersHelp(void)
{
    static const char *const lines[] = {"< >: NEXT FIELD", "1/3: NEXT VALUE", "EXIT: SAVE & EXIT"};
    EditorKeysPopup(lines, 3, true);
}
void CHANNEL_DrawSelectionHelp(uint8_t kind) // 0 lists, 1 bands, 2 BAND SCAN pick
{
    static const char *const pick[] = {"< >: CHOOSE BAND", "M: START SCAN", "M LONG: BAND OPTIONS",
                                       "EXIT: BACK TO VFO"};
    const char *const lines[] = {kind ? "4: SELECT BAND" : "4: SELECT LIST", "M: SELECT & START",
                                 kind ? "M LONG: BAND OPTIONS" : "M LONG: LIST OPTIONS", "*: SELECT ALL",
                                 "EXIT: START SCAN"};
    if (kind == 2)
        EditorKeysPopup(pick, 4, false);
    else
        EditorKeysPopup(lines, 5, false);
}
// Seven lines: the same box rises over the status line (screen rows 0..58).
static void EditorKeysPopupTall(const char *const *lines, uint8_t count)
{
    const uint8_t x = 4, right = 123, bottom = 50; // framebuffer rows; screen rows 8..58
    uint8_t page[LCD_WIDTH];
    // Status-line part: border, first line, drawn through framebuffer row 0.
    memcpy(page, gFrameBuffer[0], sizeof page);
    memcpy(gFrameBuffer[0], gStatusLine, sizeof page);
    UI_DrawPopupFrame(x, 0, right, bottom + 8, 7);
    EditorPopupText5x5(lines[0], x + 3, 3, strlen(lines[0]));
    memcpy(gStatusLine, gFrameBuffer[0], sizeof page);
    memcpy(gFrameBuffer[0], page, sizeof page);
    UI_DrawPopupFrame(x, -8, right, bottom, bottom); // no margin over KEYS INFOS
    for (uint8_t row = 1; row < count; ++row)
        EditorPopupText5x5(lines[row], x + 3, 3 + row * 8 - 8, strlen(lines[row]));
}
void CHANNEL_DrawHistoryHelp(bool messages)
{
    static const char *const historyKeys[] = {"M: MENU",      "M LONG: SAVE FREQ", "* LONG: SCAN HISTORY",
                                          "6: DEL ENTRY", "1 LONG: FREQ TO VFO", "SK2: ADD REM BLACKL.",
                                          "*: MESSAGES"};
    static const char *const messagesKeys[] = {"M: OPEN", "6: DEL ENTRY", "6 LONG: CLEAR ALL", "*: HISTORY"};
    if (messages)
        EditorKeysPopup(messagesKeys, 4, false);
    else
        EditorKeysPopupTall(historyKeys, 7);
}
