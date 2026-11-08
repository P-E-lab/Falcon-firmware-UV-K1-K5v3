/* Copyright 2023 Dual Tachyon
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

#ifndef UI_MAIN_H
#define UI_MAIN_H

#include <stdint.h>
#include <stdbool.h>

enum center_line_t {
    CENTER_LINE_NONE = 0,
    CENTER_LINE_IN_USE,
    CENTER_LINE_AUDIO_BAR,
    CENTER_LINE_AUDIO_SCOPE,
    CENTER_LINE_RSSI,
    CENTER_LINE_CHARGE_DATA
};

enum Vfo_txtr_mode {
    VFO_MODE_NONE = 0,
    VFO_MODE_TX = 1,
    VFO_MODE_RX = 2,
};

typedef enum center_line_t center_line_t;

extern center_line_t center_line;

#ifdef ENABLE_AUDIO_BAR
void UI_DisplayAudioBar(void);
void UI_DrawTxAudioBar(uint8_t line);
#endif
void DisplayRSSIBar(const bool now);
void UI_RefreshDualActivity(void);
bool UI_ScanBlinkVisible(void);
void UI_MAIN_ClearReceivedCode(void);

#ifdef ENABLE_FEAT_F4HWN_AUDIO_SCOPE
void UI_DisplayAudioScope(void);
#endif
void UI_MAIN_TimeSlice500ms(void);
void UI_DisplayMain(void);
/* While a VFO scan scans: 0 the whole screen, 1 the scanned VFO's block (dual
 * VFO), 2 its title and frequency only (frequency and band scans). */
enum { UI_SCAN_FULL, UI_SCAN_BLOCK, UI_SCAN_FREQUENCY };
extern uint8_t gUiScanPartial;
void UI_DrawMenuEdition(void);

#ifdef ENABLE_AGC_SHOW_DATA
void UI_MAIN_PrintAGC(bool force);
#endif

void UI_DrawRxMeter(int16_t dbm, uint8_t line);
void UI_DrawScanGauge(uint8_t line, uint8_t left, uint8_t right, uint32_t index, uint32_t total);
void UI_DrawIdentityBubble(const char *text, uint8_t left, uint8_t top, uint8_t width);
void UI_DrawScanListBubble(uint8_t list, uint8_t top);
void UI_PrintSmallAt(const char *text, uint8_t start, uint8_t end, uint8_t y, bool bold);
uint8_t UI_CompactTextWidth(const char *text);
void UI_DrawCompactText5x5(const char *text, uint8_t x, uint8_t y);
void UI_DrawText5x5(const char *text, uint8_t x, uint8_t y, uint8_t count);

// Draw one row-based 3x5 or 5x5 glyph into the supplied framebuffer.
void UI_DrawRowGlyph(uint8_t (*buffer)[128], const uint8_t *glyph, uint8_t x, uint8_t y, uint8_t width);

#endif
