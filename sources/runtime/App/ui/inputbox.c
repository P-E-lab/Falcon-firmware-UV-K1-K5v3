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

#include <string.h>

#include "ui/inputbox.h"

char gInputBox[8];
char inputBoxAscii[9];
uint8_t gInputBoxIndex;

static char frequencyInput[8];
static uint8_t frequencyInputIndex;
static uint8_t frequencyInputDotIndex;
static uint32_t frequencyInputValue;
static char frequencyInputString[11];

void INPUTBOX_Append(const KEY_Code_t Digit)
{
    if (gInputBoxIndex >= sizeof(gInputBox))
        return;

    if (gInputBoxIndex == 0)
        memset(gInputBox, 10, sizeof(gInputBox));

    if (Digit != KEY_INVALID)
        gInputBox[gInputBoxIndex++] = (char)(Digit - KEY_0);
}

const char *INPUTBOX_GetAscii()
{
    for (int i = 0; i < 8; i++) {
        char c = gInputBox[i];
        inputBoxAscii[i] = (c == 10) ? '-' : '0' + c;
    }
    return inputBoxAscii;
}

void INPUTBOX_ResetFrequency(void)
{
    frequencyInputValue = 0;
    memset(frequencyInputString, '-', sizeof(frequencyInputString) - 1);
    frequencyInputString[sizeof(frequencyInputString) - 1] = '\0';
}

void INPUTBOX_FrequencyBegin(void)
{
    frequencyInputIndex = 0;
    frequencyInputDotIndex = 0;
    INPUTBOX_ResetFrequency();
}

uint32_t INPUTBOX_ParseFrequency(const char *digits, uint8_t length, uint8_t dot)
{
    const uint8_t whole = dot ? dot : (length < 3 ? length : 3);
    uint32_t value = 0;
    uint8_t count = whole + 5;
    for (uint8_t i = 0; i < count; ++i)
        value = value * 10 + (i < length ? digits[i] - '0' : 0);
    return value;
}

bool INPUTBOX_FrequencyComplete(void)
{
    return frequencyInputIndex == (frequencyInputDotIndex ? frequencyInputDotIndex + 3 : 6);
}

bool INPUTBOX_Backspace(uint8_t *length, uint8_t *dot)
{
    if (!*length)
        return false;
    if (*dot == *length)
        *dot = 0;
    else
        --*length;
    return true;
}

bool INPUTBOX_FrequencyUpdate(const KEY_Code_t key)
{
    if (key == KEY_STAR) {
        if (frequencyInputIndex == 0 || frequencyInputIndex > 4 || frequencyInputDotIndex != 0)
            return false;
        frequencyInputDotIndex = frequencyInputIndex;
    } else if (key == KEY_EXIT) {
        if (!INPUTBOX_Backspace(&frequencyInputIndex, &frequencyInputDotIndex))
            return false;
    } else if (key >= KEY_0 && key <= KEY_9) {
        if (INPUTBOX_FrequencyComplete())
            return false;
        frequencyInput[frequencyInputIndex++] = '0' + key - KEY_0;
    } else {
        return false;
    }

    INPUTBOX_ResetFrequency();

    const uint8_t whole = frequencyInputDotIndex ? frequencyInputDotIndex : 3;
    uint8_t out = 0;
    for (uint8_t i = 0; i < frequencyInputIndex && out < sizeof(frequencyInputString) - 1; i++) {
        if (i == whole)
            frequencyInputString[out++] = '.';
        frequencyInputString[out++] = frequencyInput[i];
    }
    if (frequencyInputDotIndex && frequencyInputIndex == whole)
        frequencyInputString[out++] = '.';
    frequencyInputString[out] = '\0';

    frequencyInputValue =
        INPUTBOX_ParseFrequency(frequencyInput, frequencyInputIndex, frequencyInputDotIndex);

    return true;
}

bool INPUTBOX_FrequencyIsActive(void)
{
    return frequencyInputIndex != 0;
}

uint32_t INPUTBOX_FrequencyValue(void)
{
    return frequencyInputValue;
}

const char *INPUTBOX_FrequencyGetString(void)
{
    return frequencyInputString;
}
