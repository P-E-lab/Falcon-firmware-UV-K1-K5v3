#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "driver/keyboard.h"
#define MONITOR_LIMIT 50
void MONITOR_Rebuild(void);
void MONITOR_Invalidate(void);
uint8_t MONITOR_Count(void);
uint32_t MONITOR_Frequency(uint8_t index);
void MONITOR_ToggleCurrent(void);
void MONITOR_DrawMarker(uint16_t channel, uint32_t frequency, uint8_t x, uint8_t centerY);
void MONITOR_DrawSmallMarker(uint16_t channel, uint32_t frequency, uint8_t x, uint8_t centerY);

void CHANNEL_OpenVfo(void);
bool CHANNEL_CanRepeat(KEY_Code_t key);
void CHANNEL_OpenSelection(uint16_t channel, bool list);
bool CHANNEL_IsActive(void);
bool CHANNEL_CanLongPress(KEY_Code_t key);
bool CHANNEL_TakeResult(uint16_t *from, uint16_t *to, bool *list);
void CHANNEL_ProcessKeys(KEY_Code_t key, bool pressed, bool held);
void CHANNEL_Display(void);

void CHANNEL_DrawSelectionHelp(bool band);
void CHANNEL_DrawHistoryHelp(bool messages);
void CHANNEL_DrawParametersHelp(void);
void CHANNEL_DrawKeysInfos(void);
void CHANNEL_DrawKeysInfosLabel(const char *);

struct VFO_Info_t;
void CHANNEL_OpenHistory(const struct VFO_Info_t *);
void CHANNEL_DrawRangeField(const char *, const char *, uint8_t, bool);

void CHANNEL_FormatFrequency(char *, uint32_t);
