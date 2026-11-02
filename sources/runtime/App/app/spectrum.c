#include "apps/app_overlay.h"
#include "app/monitor.h"
#include "app/main.h"
//K1 Spectrum Sonic
/* Copyright 2026 Robby
 * https://github.com/Robby69400
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
// ============================================================
// SECTION: Includes
// ============================================================

//#include "debugging.h"

#include "store.h"
#include "app/spectrum.h"
// Same notation as Dual VFO; Single VFO intentionally omits K.
const char scanStepNames[24][6] FP_RESOURCE = {"0.01K", "0.05K", "0.10K", "0.25K", "0.50K", "1.00K",
                                               "1.25K", "2.50K", "5.00K", "6.25K", "8.33K", "9.00K",
                                               "10K",   "12.5K", "15K",   "20K",   "25K",   "30K",
                                               "50K",   "100K",  "125K",  "200K",  "250K",  "500K"};

const uint16_t scanStepValues[24] = {1,    5,    10,   25,    50,    100,   125,   250,
                                     500,  625,  833,  900,   1000,  1250,  1500,  2000,
                                     2500, 3000, 5000, 10000, 12500, 20000, 25000, 50000};

const uint16_t jumpSizes[24] = {1,    5,    10,   25,   50,   3000, 2500, 2500,  2500,  2500,  2500,  2700,
                                3000, 2500, 3000, 4000, 2500, 3000, 5000, 10000, 12500, 20000, 25000, 50000};


#include "nav_invert.h"
#include "driver/backlight.h"
#include "ui/helper.h"
#include "common.h"
#include "action.h"
#include "ui/main.h"
#include "ui/menu.h"
#include "ui/smeter.h"
#include "scheduler.h"
#include "misc.h"
#include "driver/py25q16.h"
#include "app/uart.h"
#include "version.h"
#include "settings.h"
#include "app/app.h"
#include "functions.h"
#include "ui/status.h"
#include "app/spectrum_activity.h"
#include "app/spectrum_header.h"
#include "app/scan_wake.h"
#ifdef ENABLE_FALCON_MESSAGES
#include "app/messages.h"
#endif
// ============================================================
// SECTION: Compile-time configuration
// ============================================================
#define MAX_VISIBLE_LINES 6
#define NoisLvl           40
#define NoiseHysteresis   15
/////////////////////////DEBUG//////////////////////////
//char str[64] = "";sprintf(str, "%d\r\n", Spectrum_state );LogUart(str);

// ============================================================
// SECTION: State variables
// ============================================================
static volatile bool gSpectrumChangeRequested = false;
// Nonzero only during the dedicated *Scan session (lists numbered 01..50).
static uint8_t scanListOnly = 0;
// *Scan uses the normal Spectrum scan engine, with the main VFO display.
static bool vfoScan;
static bool vfoRangeSession;
static bool entryKeyHeld; // the key that opened the scanner, ignored until released
static bool scanWakeWaitingRelease;
static uint16_t vfoScanSide2Ticks;
static SpectrumActivity activity __attribute__((section(".app_scratch"), aligned(4)));
static int16_t spectrumRxDbm = INT16_MIN;
static bool spectrumRxDbmFresh;
static bool sentinelView;
static bool scanOptionsChanged;
static bool scanNoList, scanEmpty;
static bool ScanListVisible(unsigned index);
static uint32_t sentinelIdleTicks, sentinelTimerTick;
static SpectrumListPlot activityLists __attribute__((section(".app_scratch"), aligned(4)));
static uint8_t nextDisplayList;
static uint32_t activityMemoryFrequency;
static bool vfoScanKeysReady;
static uint8_t vfoScanSkipTicks;
static uint8_t vfoScanVfo;
static uint8_t vfoScanMembers[(MR_CHANNELS_MAX + 7) / 8] __attribute__((section(".app_scratch"), aligned(4)));
static uint16_t vfoScanCount;
static uint16_t vfoScanDisplayedChannel;
uint16_t ScanChannel = 0;
static uint16_t statusScanChannel = 0; // display-only current memory identity
static void VfoScanSyncDisplay(void);
static void VfoScanBlacklist(void);

// ============================================================
// SECTION: HISTORY
// ============================================================
uint8_t code = 0;
typedef struct __attribute__((packed)) {
    uint32_t HFreqs;
    uint8_t HBlacklisted;
    uint8_t code; // 0=None, 1-50=CTCSS, 100+=DCS
    uint16_t HTimeS;
} HistoryStruct;

static uint16_t historyListIndex = 0;
static int historyScrollOffset = 0;
static bool gHistoryScan = false;
static uint32_t CodeFreq = 0;
// One slot per visible row, including the extra row of the VFO history.
#define HISTORY_ROW_CACHE (MAX_VISIBLE_LINES + 1)
static uint16_t cachedChannels[HISTORY_ROW_CACHE];    // Stores the numbers of found channels
static uint16_t cachedAbsoluteIdx[HISTORY_ROW_CACHE]; // Remembers which history index they belong to
static uint8_t cacheWriteHead = 0;    // Circular write head
static int lastHistoryScrollOffset = -1;
// ============================================================

static uint16_t indexFs = 0;
static uint16_t TxChannel = 0;
// Keep the selected main VFO intact while LAST RX uses the scanner TX state.
static VFO_Info_t channelTxVfo;
static uint16_t channelTxScreen, channelTxMemory;
static uint8_t channelTxSlot;
static uint8_t MonitorIndex = 0;
#define FMIN 1400000
#define FMAX 116000000
static uint32_t SpectrumRangeStart = FMIN;
static uint32_t SpectrumRangeStop = FMIN;

/////////////////////////////Parameters://///////////////////////////
static int clamp(int v, int min, int max);
uint8_t gVfoScanSQL = 30, gSpectrumSQL = 30, gScanSQLHys;
uint16_t gScanSqlPreview;
void APP_LoadScanSQL(void)
{
    uint8_t data[3];
    PY25Q16_ReadBuffer(ADRESS_PARAMS + 18, data, 3);
    gVfoScanSQL = data[0] == 0xD2 && data[1] <= 77 ? data[1] : 30;
    gSpectrumSQL = data[0] == 0xD2 && data[2] <= 77 ? data[2] : 30;
    uint8_t hys;
    PY25Q16_ReadBuffer(ADRESS_PARAMS + 41, &hys, 1);
    gScanSQLHys = (hys & 0xF0u) == 0xA0u && (hys & 15u) <= 8 ? (hys & 15u) : 0;
}
void APP_SaveScanSQL(void)
{
    const uint8_t data[3] = {0xD2, gVfoScanSQL, gSpectrumSQL};
    PY25Q16_WriteBuffer(ADRESS_PARAMS + 18, data, 3, false);
    const uint8_t hys = 0xA0u | gScanSQLHys;
    PY25Q16_WriteBuffer(ADRESS_PARAMS + 41, &hys, 1, false);
}
void APP_AdjustScanSQL(int direction)
{
    gVfoScanSQL = clamp((int)gVfoScanSQL + direction, 0, 77);
    gScanSqlPreview = 6;
    gRequestSaveSettings = true;
    gUpdateDisplay = true;
}
static uint16_t DelayRssi = 1500;
static uint16_t SpectrumDelay = 0;
static uint16_t MaxListenTime = 0;
static uint32_t RangeStart = FMIN;
static uint32_t RangeStop = 11000000;
static uint8_t rangeModulation;
static bool rangeModConfigured;
static uint16_t SpectrumSleepMs = 0;
static uint8_t Noislvl_OFF = NoisLvl;
static uint8_t Noislvl_ON = NoisLvl - NoiseHysteresis;
// Consecutive noisy RX samples; reset on every RX boundary.
static uint8_t scanNoiseCount;
static uint16_t osdPopupSetting = 500;
static uint16_t UOO_trigger = 15;
static uint8_t AUTO_KEYLOCK = AUTOLOCK_OFF;
static bool SoundBoost = 0;
static uint8_t PttEmission = 0, Ptt2Emission;
static KEY_Code_t spectrumPtt2Key = KEY_INVALID;
static uint8_t scanMenuModulation = UINT8_MAX, scanMenuBandwidth;
static bool gMonitorScan = true;
static uint16_t stringCodeTimer = 0;
#define STRINGCODE_TIMEOUT_MS 2 //( x 5s)

// Physical row order is shared by rendering and input. Only Sentinel exposes
// the record-only trigger; the Spectrum page skips that one row.
enum {
    PARAM_PTT_EMISSION,
    PARAM_PTT2_EMISSION,
    PARAM_MONITOR_SCAN,
    PARAM_SPECTRUM_DELAY,
    PARAM_MAX_LISTEN_TIME,
    PARAM_MODULATION,
    PARAM_LISTEN_BW,
    PARAM_LISTEN_BW_AM,
    PARAM_AUDIO_AM,
    PARAM_SOUND_BOOST,
    PARAM_NOISE_LEVEL_OFF,
    PARAM_RECORD_TRIGGER,
    PARAM_POWER_SAVE,
    PARAM_OSD_POPUP,
    PARAM_AUTO_KEYLOCK,
    PARAM_COUNT
};
static uint16_t SpectrumParameterAt(uint16_t visual)
{
    return visual + (!sentinelView && visual >= PARAM_RECORD_TRIGGER);
}
uint16_t GetMaxVisualRows(void)
{
    return PARAM_COUNT - !sentinelView;
}

////////////////////////////////////////////////////////////////////

Mode Spectrum_state;
static bool Backlight_On = 1;
uint8_t osdPopupIndex = 1;
static const int osdPopupTimes[] = {0, 500, 1000, 3000, 5000, 10000};
#ifdef ENABLE_BENCH
static uint32_t benchTickMs = 0;
static uint16_t benchStepsThisSec = 0;
static uint16_t benchRatePerSec = 0;
static uint32_t benchLapMs = 0;
static uint32_t benchLastLapMs = 0;
static bool benchLapDone = false;
#endif

bool FastSpeed = false;
static uint8_t listenBwAM; // 0: Wide, 1: H4WHN AM Narrow
static bool sentinelMeterDue;
static uint32_t listReferenceParameters = UINT32_MAX;
static uint8_t scanBwOverrides, savedListenBw, savedListenBwAM, savedScanStep;
static bool scanStepOverride;

bool Cleared = 0;
static bool SettingsLoaded = false;
uint8_t gKeylockCountdown = 0;
bool gIsKeylocked = false;
static uint16_t osdPopupTimer = 0;
static uint32_t osdPopupTick;
static uint32_t spectrumElapsedCount = 0;
static uint32_t SpectrumPauseCount = 0;
static bool SPECTRUM_PAUSED;
static uint8_t IndexMaxLT = 0;
static const char *labels[] = {"OFF", "3s", "6s", "10s", "20s", "1m", "5m", "10m", "20m", "30m"};
static const uint16_t listenSteps[] = {0, 3, 6, 10, 20, 60, 300, 600, 1200, 1800}; //in s
#define LISTEN_STEP_COUNT 9
static uint8_t IndexPS = 0;
static const char *labelsPS[] = {"OFF", "200ms", "500ms", "1s", "2s", "5s"};
static const uint16_t PS_Steps[] = {0, 20, 50, 100, 200, 500}; //in 10 ms
#define PS_STEP_COUNT 5
static uint32_t lastReceivingFreq = 0;
static bool gIsPeak = false;
static bool historyListActive = false;
// History opened by the VFO shortcut: no scan and no scan status line.
static bool historySession;
// MESSAGES page of the history screen; the scan pauses while it is shown.
static bool historyMessages;
// Listening on a history entry uses its modulation and filter: the scanner
// modulation is restored when the history closes.
static uint8_t historyModulation;
static uint8_t historyBandwidth = UINT8_MAX;
// Long 1: entry handed to the main VFO once the scanner has closed.
static uint32_t toVfoFrequency;
static uint8_t toVfoProfile;
static uint16_t toVfoCss;
#ifdef ENABLE_FALCON_MESSAGES
static uint16_t msgIndex, msgScroll, msgCount;
static int8_t msgStep = 1; // direction of the last move: session markers are skipped that way
static bool msgOpen;     // the selected message is shown whole
static uint8_t msgAsk;   // popup: MSG_ASK_DELETE (DELETE?) or MSG_ASK_CLEAR (CLEAR ALL?)
enum { MSG_ASK_NONE, MSG_ASK_DELETE, MSG_ASK_CLEAR };
#endif
// Opened from the VFO, and on the MESSAGES page, the history has no scan: its
// page title takes the status line.
static inline __attribute__((always_inline)) bool HistoryOwnsStatus(void)
{
    return historySession || historyMessages;
}
// The MESSAGES page, and the history opened from the VFO outside its own scan:
// no scan, no RX, no LED.
static inline __attribute__((always_inline)) bool HistorySilent(void)
{
    return historyMessages || (historySession && !gHistoryScan);
}
static bool gForceModulation = 0;
static int16_t previousRssi;
static bool rssiReferenceValid;

static uint8_t lastFilterPath = UINT8_MAX;
static uint8_t SpectrumMonitor = 0;
static uint8_t prevSpectrumMonitor = 0;
static bool Key_1_pressed = 0;
static uint16_t WaitSpectrum = 0;
static void DrawF(uint32_t f);
static void ToggleRX(bool on);
static void NextScanStep();

static uint16_t CountValidHistoryItems(void);
static void BuildScanListSlots();
void SCANLISTS_InitUsage(void);
static void RenderHistoryList();
static void RenderScanListSelect();
static void RenderParametersSelect();
static void RenderHistoryMenuSelect(void);
static void OpenScanDetails(void);
typedef struct {
    char left[20];
    char right[20];
} ListRow;

typedef void (*GetListRowFn)(uint16_t index, ListRow *row);
static void DrawCompactSelectionRow(const ListRow *text, uint8_t y, bool selected, uint8_t rightMargin);

static void HandleKeyHistoryMenu(uint8_t key);
static void ExecuteHistoryMenuAction(uint16_t index);
static void UpdateScan();
static void UpdateSpectrumMonitorLeds();
static uint8_t bandListSelectedIndex = 0;
static int bandListScrollOffset = 0;
static void RenderBandSelect();
static void ClearHistory(uint8_t mode);
static void DrawMeter(int);
static void Spectrum_END_TX(void);
static uint8_t scanListSelectedIndex = 0;
static int scanListScrollOffset = 0;
static uint8_t parametersSelectedIndex = 0;
static uint8_t parametersScrollOffset = 0;
static bool parametersStateInitialized = false;
static uint8_t historyMenuSelectedIndex = 0;
static uint8_t historyMenuScrollOffset = 0;
static uint8_t scanListSlotCount = 0;
static bool gScanListHasChannels[MR_CHANNELS_LIST] __attribute__((section(".app_scratch"), aligned(4)));
static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0, 0};
struct FrequencyBandInfo {
    uint32_t lower;
    uint32_t upper;
    uint32_t middle;
};
//static uint8_t refresh = 0; // ALWAYS REQUEST SUBTONE
#define F_MAX        frequencyBandTable[ARRAY_SIZE(frequencyBandTable) - 1].upper
#define Bottom_print 50
//#define UHF_NOISE_FLOOR 0
static uint16_t scanChannelsCount;
static uint8_t monitorChannelsCount;
static void ToggleScanList();
static void SaveSettings(bool notify);
static void SelectScanProfile(bool sentinel);
static void OpenHistoryChannel(void);
static void HistoryToVfo(void);
static const uint16_t RSSI_MAX_VALUE = 255;
static uint16_t savedRegisters[16];
static const uint8_t savedRegisterAddresses[16] = {0x30, 0x37, 0x3D, 0x43, 0x47, 0x48, 0x7E, 0x02,
                                                   0x3F, 0x7B, 0x12, 0x11, 0x14, 0x54, 0x55, 0x75};
static char String[48] __attribute__((section(".app_scratch"), aligned(4)));
static char StringC[10] __attribute__((section(".app_scratch"), aligned(4)));
static bool isKnownChannel = false;
static uint16_t gChannel;
static char channelName[12] __attribute__((section(".app_scratch"), aligned(4)));
static uint16_t lastNamedChannel = 0xFFFF; // identity captured with the displayed RX name
static uint32_t lastNamedFrequency = 0;
static bool postTxListening = false; // RX path open for a reply, not yet a received signal


static bool isInitialized = false;
static bool isListening = true;
// Send the initial RX screen once, then keep Spectrum's display quiet.
static bool rxDisplayPending = false;
void APP_ResourceRedraw(void)
{
    if (!gMenuListCount)
        UI_MENU_InitList();
    rxDisplayPending = true;
    gUpdateDisplay = true;
    gUpdateStatus = true;
}
static bool newScanStart = true;
static bool audioState = true;
static uint8_t bl;
static State currentState = SPECTRUM;
static PeakInfo peak __attribute__((section(".app_scratch"), aligned(4)));
static ScanInfo scanInfo __attribute__((section(".app_scratch"), aligned(4)));
static char latestScanListName[12] __attribute__((section(".app_scratch"), aligned(4)));
static bool refreshScanListName = true;
/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
/****************************************************************************/
static bool IsBlacklisted(uint32_t f);
static void SetState(State state);
static void RenderStatus(void);
static void Render(void);
static void Spectrum_Prepare_Tx(void);
static void Skip();
static NATIVE_CODE(lists) bool GetScanListLabel(uint8_t scanListIndex, char *bufferOut);
static void DeInitSpectrum(void);
static void ToggleRX(bool on);
static void HistoryClose(void);
static void FrequencyDraft(VFO_Info_t *draft, uint32_t f, uint8_t profile, uint16_t css);


/***************************BIG RAM******************************************/
#if defined(ENABLE_LARGE_HISTORY)
#define HISTORY_SIZE 500
#else
#define HISTORY_SIZE 200
#endif

// CHANNEL_LOCATION
#if defined(ENABLE_10K)
#define MAX_SCAN_CHANNELS 10000
#elif defined(ENABLE_4096)
#define MAX_SCAN_CHANNELS (MR_CHANNELS_MAX - MAX_BANDS)
#elif defined(ENABLE_1024)
#define MAX_SCAN_CHANNELS 975
#endif

static bandparameters BParams[MAX_BANDS] __attribute__((section(".app_scratch"), aligned(4)));
typedef struct {
    uint32_t HFreqs;
    bool HBlacklisted;
    uint8_t code;
    uint16_t HTimeS;
    union {
        uint16_t record;
        struct {
            uint16_t receptions : 10;
            uint16_t profile : 4;
            uint16_t inverse : 1;
            uint16_t reserved : 1;
        };
    };
} __attribute__((packed, aligned(2))) RamHistory;
// Two-byte alignment keeps the ten-byte layout and permits native halfword access.
_Static_assert(_Alignof(RamHistory) == 2, "History halfword alignment");
_Static_assert(sizeof(RamHistory) == 10 && __builtin_offsetof(RamHistory, record) == sizeof(HistoryStruct),
               "Packed RAM history and legacy prefix");
#define HISTORY_COUNTS_ADDR  (ADRESS_HISTORY + (HISTORY_SIZE + 1u) * sizeof(HistoryStruct))
#define HISTORY_COUNTS_MAGIC 0x324E4352u
_Static_assert(HISTORY_COUNTS_ADDR + 4 + HISTORY_SIZE * 2 < 0x10000, "History before calibration");
static RamHistory history[HISTORY_SIZE];
#define SetHistoryFreq(idx, freq) (history[(idx)].HFreqs = (freq))
#define GetHistoryFreq(idx)       (history[(idx)].HFreqs)
/****************************************************************************/

SpectrumSettings settings = {
    stepsCount : STEPS_128,
    scanStepIndex : STEP_500kHz,
    rssiTriggerLevelUp : 20,
    listenBw : BK4819_FILTER_BW_WIDE,
    modulationType : false,
    dbMin : -120,
    dbMax : -60,
    scanListEnabled : {0},
    bandEnabled : {0}
};


static uint32_t currentFreq;
static uint8_t rssiHistory[128] __attribute__((section(".app_scratch"), aligned(4)));
static uint8_t nextBandToScanIndex = 0;
static uint32_t ReadScanParameters(uint16_t channel);
static uint8_t ScanListeningBandwidth(uint8_t mode);
static void ResetScanParameterOverrides(void);
static void LoadActiveBands(void);
uint16_t BOARD_gMR_fetchChannel(const uint32_t freq);
static void LoadActiveScanFrequencies(void);

static uint8_t bandCount = 0;
static uint16_t bandChannels[MAX_BANDS] __attribute__((section(".app_scratch"), aligned(4)));
static bool selectionHelp;
static uint16_t validChannelsCount = 0;

int Rssi2DBm(const uint16_t rssi)
{
    return (rssi >> 1) - 160;
}

static int clamp(int v, int min, int max)
{
    return v <= min ? min : (v >= max ? max : v);
}

static void UpdateDBMaxAuto()
{ //Zoom
    scanInfo.rssiMax = 0;
    scanInfo.rssiMin = 65535;
    for (uint8_t i = 0; i < 127; i++) {
        if (rssiHistory[i] > scanInfo.rssiMax) {
            scanInfo.rssiMax = rssiHistory[i];
        } else if (rssiHistory[i] < scanInfo.rssiMin) {
            scanInfo.rssiMin = rssiHistory[i];
        }
    }

    static uint8_t z = 5;
    int newDbMax;
    if (scanInfo.rssiMax > 0) {
        newDbMax = Rssi2DBm(scanInfo.rssiMax);

        if (newDbMax > settings.dbMax + z) {
            settings.dbMax = settings.dbMax + z;
        } else if (newDbMax < settings.dbMax - z) {
            settings.dbMax = settings.dbMax - z;
        } else {
            settings.dbMax = newDbMax;
        }
    }

    if (scanInfo.rssiMin > 0) {
        settings.dbMin = Rssi2DBm(scanInfo.rssiMin);
    }
}


BK4819_FilterBandwidth_t ACTION_NextBandwidth(BK4819_FilterBandwidth_t currentBandwidth, const bool dynamic,
                                              bool increase)
{
    BK4819_FilterBandwidth_t nextBandwidth = (increase && currentBandwidth == BK4819_FILTER_BW_NARROWER)
                                                 ? BK4819_FILTER_BW_WIDE
                                             : (!increase && currentBandwidth == BK4819_FILTER_BW_WIDE)
                                                 ? BK4819_FILTER_BW_NARROWER
                                                 : (increase ? currentBandwidth + 1 : currentBandwidth - 1);

    //BK4819_SetFilterBandwidth(nextBandwidth, dynamic);
    gRequestSaveChannel = 1;
    return nextBandwidth;
}

static uint8_t ListeningFilter(uint8_t mode)
{
    const uint8_t bandwidth = ScanListeningBandwidth(mode);
    return mode == MODULATION_AM ? (bandwidth ? BK4819_FILTER_BW_AM : BK4819_FILTER_BW_WIDE) : bandwidth;
}

const char *bwNames[3] = {"25k", "12.5k", "6.25k"};
static const char *const bwShort[3] = {"WIDE", "NARR", "NARR+"};

typedef struct {
    uint8_t sLevel;   // S-level value
    uint8_t over;     // over S9 value
    int dBmRssi;      // RSSI in dBm
    bool overSquelch; // determines whether signal is over squelch open threshold
} __attribute__((packed)) sLevelAttributes;

sLevelAttributes GetSLevelAttributes(const int16_t rssi, const uint32_t frequency)
{
    sLevelAttributes att;
    att.dBmRssi = Rssi2DBm(rssi) + dBmCorrTable[FREQUENCY_GetBand(frequency)];
    att.sLevel = SmeterLevelFromDbm(att.dBmRssi);
    if (att.sLevel > 9)
        att.sLevel = 9;
    att.over = att.dBmRssi > -93 ? MIN(att.dBmRssi + 93, 40) : 0;
    if (att.dBmRssi > -53)
        att.dBmRssi = -53;
    att.overSquelch = att.sLevel > 5;

    return att;
}

ChannelInfo_t FetchChannelFrequency(const uint16_t Channel)
{
    ChannelInfo_t info;
    PY25Q16_ReadBuffer(ADRESS_CHANNELS + (uint32_t)Channel * 16, &info, sizeof(info));
    if (info.frequency == 0xFFFFFFFF) {
        ChannelInfo_t empty = {0, 0};
        return empty;
    }
    return info;
}

/* Channels read per SPI transfer by FindMemoryChannel: 4 x 16 B = 64 B of RAM. */
#define BLOCK_SIZE 4

typedef struct {
    uint32_t freq;      // 0x00
    uint32_t offset;    // 0x04
    uint8_t rxcode;     // 0x08
    uint8_t txcode;     // 0x09
    uint8_t codeflags;  // 0x0A (bitfield grouping to simplify reading)
    uint8_t mod_dir;    // 0x0B
    uint8_t settings;   // 0x0C
    uint8_t reserved0d; // 0x0D
    uint8_t step;       // 0x0E
    uint8_t unused;     // 0x0F
} __attribute__((packed)) FlashChannel_t;

static uint16_t FindMemoryChannel(uint32_t freq, uint16_t last, uint8_t tolerance)
{
    static FlashChannel_t block[BLOCK_SIZE];
    for (uint32_t start_ch = MR_CHANNEL_FIRST; start_ch <= last; start_ch += BLOCK_SIZE) {
        uint32_t remaining = last - start_ch + 1;
        uint16_t chunk_size = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : (uint16_t)remaining;
        uint32_t channel_offset_index = start_ch - MR_CHANNEL_FIRST;
        uint32_t block_addr = (uint32_t)ADRESS_CHANNELS + (channel_offset_index * sizeof(FlashChannel_t));

        PY25Q16_ReadBuffer(block_addr, (uint8_t *)block, chunk_size * sizeof(FlashChannel_t));

        for (uint16_t k = 0; k < chunk_size; k++) {
            const uint32_t stored = block[k].freq;
            if ((stored > freq ? stored - freq : freq - stored) <= tolerance) {
                return (uint16_t)(start_ch + k);
            }
        }
    }
    return 0xFFFF;
}

uint16_t BOARD_gMR_fetchChannel(const uint32_t freq)
{
    return FindMemoryChannel(freq, MR_CHANNEL_LAST, 0);
}

// Display-only fallback for the 10 Hz rounding of aviation thirds.
// Exact identities and all RX/TX tuning/profile decisions remain unchanged.
static uint16_t ReceivedMemoryIdentity(uint32_t frequency, uint16_t exact)
{
    if (IS_MR_CHANNEL(exact) || Spectrum_state == CHANNEL_MODE || scanInfo.scanStep != 833)
        return exact;
    return FindMemoryChannel(frequency, MAX_SCAN_CHANNELS - 1, 2);
}

// ML and the band selector cannot run concurrently. Share its existing workspace.
static uint16_t mlPriority[2], mlListCount[2], mlRemaining;
static uint8_t mlPriorityPending, mlTurn;
static bool mlKeys, mlRefresh;
void CheckKeys(void);
static uint8_t MLWatchGroup(uint8_t vfo)
{
    const uint16_t ch = gEeprom.ScreenChannel[vfo];
    if (ch >= MR_CHANNELS_MAX - MAX_BANDS)
        return UINT8_MAX;
    const ChannelAttributes_t *att = MR_GetChannelAttributes(ch);
    if (!att || att->band > BAND7_470MHz || att->scanlist > MR_CHANNELS_LIST)
        return UINT8_MAX;
    return att->scanlist;
}

static uint16_t MLPriorityChannel(uint8_t index)
{
    const uint16_t ch = gEeprom.SCANLIST_PRIORITY_CH[index];
    if (ch >= MR_CHANNELS_MAX - MAX_BANDS || (index && ch == gEeprom.SCANLIST_PRIORITY_CH[0]))
        return UINT16_MAX;
    const ChannelAttributes_t *att = MR_GetChannelAttributes(ch);
    if (!att || att->band > BAND7_470MHz)
        return UINT16_MAX;
    const uint8_t group = att->scanlist;
    if (group <= MR_CHANNELS_LIST && (MLWatchGroup(0) == group || MLWatchGroup(1) == group))
        return UINT16_MAX;
    const uint32_t f = FetchChannelFrequency(ch).frequency;
    return f >= FMIN && f <= FMAX && RX_freq_check(f) >= 0 ? ch : UINT16_MAX;
}
#define mlCursor (bandChannels)
#define mlGroup  ((uint8_t *)&bandChannels[2])
#define mlOwner  (((uint8_t *)&bandChannels[2])[2])
static bool MLWatchActive(void)
{
    return vfoScan && scanListOnly == 0x80u;
}
// An RX mode change ends ML WATCH and its reception at once.
void APP_MLWatchStop(void)
{
    if (!MLWatchActive() || !isInitialized)
        return;
    mlKeys = false;
    DeInitSpectrum();
}
bool APP_MLHoldRx(void)
{
    return mlKeys && MLWatchActive() && isListening && mlOwner != (gEeprom.TX_VFO & 1u);
}
static bool AbsoluteSQL(void)
{
    return (vfoScan || !sentinelView) && !historyListActive;
}

// Rise needed to trigger a scan step. Sentinel keeps its DS setting; Spectre,
// the VFO scans and the History opened from Spectre or the VFO use a fixed,
// low value: their sensitivity is the dBm squelch.
static uint8_t ScanTrigger(void)
{
    return sentinelView && !vfoScan && !historySession ? settings.rssiTriggerLevelUp : 5;
}
static __attribute__((noinline, noclone)) uint8_t *ScanSQLSetting(void)
{
    return vfoScan || vfoRangeSession ? &gVfoScanSQL : &gSpectrumSQL;
}
static int ScanThreshold(void)
{
    return (int)*ScanSQLSetting() - 130;
}
static int ScanHoldThreshold(void)
{
    return ScanThreshold() - gScanSQLHys;
}
static __attribute__((noinline)) bool AboveScanSQL(uint16_t raw, uint32_t frequency)
{
    return Rssi2DBm(raw) + dBmCorrTable[FREQUENCY_GetBand(frequency)] >=
           (isListening ? ScanHoldThreshold() : ScanThreshold());
}
bool APP_ScanSqlText(char *text)
{
    if (!gScanSqlPreview && (!vfoScan || MLWatchActive()))
        return false;
    text[0] = 0;
    if (!vfoScan || MLWatchActive() || UI_ScanBlinkVisible())
        sprintf(text, "%d", (int)gVfoScanSQL - 130);
    return true;
}


bool APP_MLWatchEligible(void)
{
    return MLWatchGroup(0) != UINT8_MAX || MLWatchGroup(1) != UINT8_MAX;
}

// Each side retains its own cursor; a duplicate list belongs to A only.
static uint16_t MLWatchNextChannel(void)
{
    while (mlPriorityPending) {
        const unsigned index = 2 - mlPriorityPending--;
        if (mlPriority[index] != UINT16_MAX) {
            mlOwner = gEeprom.TX_VFO & 1u;
            return mlPriority[index];
        }
    }
    // Complete one list, check the two external priorities, then switch lists.
    if (!mlRemaining) {
        mlTurn ^= 1u;
        if (mlGroup[mlTurn] == UINT8_MAX)
            mlTurn ^= 1u;
        mlRemaining = mlListCount[mlTurn];
    }
    mlOwner = mlTurn;
    if (mlRemaining) {
        uint16_t ch = mlCursor[mlOwner];
        for (uint16_t n = 0; n < MR_CHANNELS_MAX - MAX_BANDS; ++n) {
            ch = ch + 1u < MR_CHANNELS_MAX - MAX_BANDS ? ch + 1u : 0u;
            if (!(vfoScanMembers[ch >> 3] & (1u << (ch & 7))))
                continue;
            if (MR_GetChannelAttributes(ch)->scanlist != mlGroup[mlOwner])
                continue;
            mlCursor[mlOwner] = ch;
            if (!--mlRemaining)
                mlPriorityPending = 2;
            return ch;
        }
    }
    return UINT16_MAX;
}

VFO_Info_t *APP_GetMLWatchDisplayVfo(uint8_t vfo)
{
    return MLWatchActive() && isListening && vfo == mlOwner ? &channelTxVfo : NULL;
}

uint16_t GetNextChannelInSelectedScanLists(uint16_t currentChannel, bool direction)
{
    if (vfoScan) {
        if (MLWatchActive())
            return MLWatchNextChannel();
        uint16_t ch = currentChannel;
        for (uint16_t n = 0; n < MR_CHANNELS_MAX; ++n) {
            ch = direction ? (ch + 1) % MR_CHANNELS_MAX : (ch + MR_CHANNELS_MAX - 1) % MR_CHANNELS_MAX;
            if (vfoScanMembers[ch >> 3] & (1u << (ch & 7)))
                return ch;
        }
        return 0xFFFF;
    }
    ChannelAttributes_t cache;
    uint32_t frequency = 0;
    uint16_t ch = currentChannel;
    for (uint16_t count = 0; count < MAX_SCAN_CHANNELS; ++count) {
        ch = direction ? (ch + 1 < MAX_SCAN_CHANNELS ? ch + 1 : 0) : (ch ? ch - 1 : MAX_SCAN_CHANNELS - 1);
        PY25Q16_ReadBuffer(ADRESS_CHANNELS + (uint32_t)ch * 16, &frequency, sizeof frequency);
        if (frequency < FMIN || frequency > FMAX)
            continue;
        MR_LoadChannelAttributesFromFlash(ch, &cache);
        if (scanNoList ? cache.scanlist == 0
                       : (cache.scanlist > 0 && cache.scanlist <= MR_CHANNELS_LIST &&
                          (scanListOnly ? cache.scanlist == scanListOnly
                                        : settings.scanListEnabled[cache.scanlist - 1]))) {
            nextDisplayList = scanNoList ? 1 : cache.scanlist;
            return ch;
        }
    }
    return 0xFFFF; // Aucun canal valide trouvé
}

static char osdPopupText[32] = "";

static void ShowOSDPopup(const char *str)
{
    if (vfoScan)
        return;
    osdPopupTimer = osdPopupSetting;
    osdPopupTick = gGlobalSysTickCounter;
    strncpy(osdPopupText, str, sizeof(osdPopupText) - 1);
    osdPopupText[sizeof(osdPopupText) - 1] = '\0';
    spectrumElapsedCount = 0;
}

// EEPROM list/band names are fixed ten-byte fields, not C strings.
static void ShowScanNamePopup(const char *name)
{
    char text[11];
    unsigned n = 0;
    while (n < 10 && name[n] && (uint8_t)name[n] != 0xff) {
        text[n] = name[n];
        ++n;
    }
    while (n && text[n - 1] == ' ')
        --n;
    text[n] = 0;
    ShowOSDPopup(text);
}

// System ticks are 10 ms. Account for busy scan ticks and popup time too.
// No EEPROM state or additional scheduler is needed for the idle RX timer.
static void SentinelTimerUpdate(void)
{
    const uint32_t now = gGlobalSysTickCounter;
    const uint32_t elapsed = now - sentinelTimerTick;
    sentinelTimerTick = now;
    if (!sentinelView || historyListActive)
        return;
    if (isListening)
        sentinelIdleTicks = 0;
    else
        sentinelIdleTicks += MIN(elapsed, 599900u - sentinelIdleTicks);
}

static void LoadActiveBands(void)
{
    memset(BParams, 0, (MAX_BANDS) * sizeof(bandparameters));
    bandCount = 0;
    for (uint16_t bd = 0; bd < MAX_BANDS; bd++) {
        uint16_t targetChannel = bd + MR_CHANNELS_MAX - MAX_BANDS;
        ChannelInfo_t freqs = FetchChannelFrequency(targetChannel);

        if (freqs.frequency >= FMIN && freqs.frequency <= FMAX) {
            bandChannels[bandCount] = targetChannel;
            gChannel = targetChannel;
            const uint32_t parameters = ReadScanParameters(targetChannel);
            BParams[bandCount].modulationType = (uint8_t)parameters;
            BParams[bandCount].scanStep = (parameters >> 8) & 255u;
            BParams[bandCount].bandwidth = (parameters >> 16) & 1u;
            BParams[bandCount].Startfrequency = freqs.frequency;
            BParams[bandCount].Stopfrequency = freqs.offset;
            PY25Q16_ReadBuffer(ADRESS_CHANNELS_NAMES + (targetChannel * 16), BParams[bandCount].BandName, 8);
            bandCount++;
        }
    }
}


uint8_t CountActiveBands(void)
{
    uint8_t activeCount = 0;
    for (uint8_t i = 0; i < bandCount; i++) {
        if (settings.bandEnabled[i]) {
            activeCount++;
        }
    }
    return activeCount;
}


static void BuildActivityLists(void)
{
    ActivityListLayout(&activityLists);
    uint16_t rank[MR_CHANNELS_LIST] = {0};
    ChannelAttributes_t cache;
    for (uint16_t ch = MR_CHANNEL_FIRST; ch < MAX_SCAN_CHANNELS; ++ch) {
        uint32_t f;
        PY25Q16_ReadBuffer(ADRESS_CHANNELS + (uint32_t)ch * 16, &f, sizeof f);
        if (f < FMIN || f > FMAX)
            continue;
        MR_LoadChannelAttributesFromFlash(ch, &cache);
        const uint8_t list = scanNoList ? (cache.scanlist == 0) : cache.scanlist;
        if (list >= 1 && list <= MR_CHANNELS_LIST && activityLists.count[list - 1])
            ActivityListMember(&activityLists, list - 1, rank[list - 1]++, ch);
    }
}

static void LoadActiveScanFrequencies(void)
{
    if (Spectrum_state == CHANNEL_MODE) {
        if (vfoScan) {
            validChannelsCount = scanChannelsCount = vfoScanCount;
            return;
        }
        validChannelsCount = scanChannelsCount = 0;
        memset(activityLists.count, 0, sizeof activityLists.count);
        listReferenceParameters = UINT32_MAX;
        scanNoList = !scanListOnly;
        for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
            if (settings.scanListEnabled[i] && ScanListVisible(i))
                scanNoList = false;
        uint8_t referenceList = MR_CHANNELS_LIST + 1;
        ChannelAttributes_t cache;
        for (uint16_t ch = MR_CHANNEL_FIRST; ch < MAX_SCAN_CHANNELS; ch++) {
            uint32_t frequency;
            PY25Q16_ReadBuffer(ADRESS_CHANNELS + (uint32_t)ch * 16, &frequency, sizeof(frequency));
            if (frequency >= FMIN && frequency <= FMAX) {
                validChannelsCount++;
                MR_LoadChannelAttributesFromFlash(ch, &cache);
                if (scanNoList ? cache.scanlist == 0
                               : (cache.scanlist > 0 && cache.scanlist <= MR_CHANNELS_LIST &&
                                  (scanListOnly ? cache.scanlist == scanListOnly
                                                : settings.scanListEnabled[cache.scanlist - 1]))) {
                    if (cache.scanlist < referenceList) {
                        referenceList = cache.scanlist;
                        listReferenceParameters = ReadScanParameters(ch);
                    }
                    scanChannelsCount++;
                    ++activityLists.count[scanNoList ? 0 : cache.scanlist - 1];
                }
            }
        }
        BuildActivityLists();
    }

    Spectrum_Prepare_Tx(); // to display ch correctly
}


static void LoadMonitorFrequencies(void)
{
    MONITOR_Rebuild();
    monitorChannelsCount = MONITOR_Count();
}


static const char ns_rf_5[] NATIVE_DATA(rf) = "13_LNAs";
static const char ns_rf_6[] NATIVE_DATA(rf) = "13_LNA";
static const char ns_rf_7[] NATIVE_DATA(rf) = "13_PGA";
static const char ns_rf_8[] NATIVE_DATA(rf) = "13_MIX";
static const char ns_rf_9[] NATIVE_DATA(rf) = "XTAL F Mode Select";
static const char ns_rf_10[] NATIVE_DATA(rf) = "RF Tx Deviation";
static const char ns_rf_11[] NATIVE_DATA(rf) = "Compress AF Tx Ratio";
static const char ns_rf_12[] NATIVE_DATA(rf) = "Compress AF Tx 0 dB";
static const char ns_rf_13[] NATIVE_DATA(rf) = "Compress AF Tx noise";
static const char ns_rf_14[] NATIVE_DATA(rf) = "MIC AGC Disable";
static const char ns_rf_15[] NATIVE_DATA(rf) = "AFC Range Select";
static const char ns_rf_16[] NATIVE_DATA(rf) = "AFC Disable";
static const char ns_rf_17[] NATIVE_DATA(rf) = "AFC Speed";
static const char ns_rf_18[] NATIVE_DATA(rf) = "3kHz AF Resp K Tx";
static const char ns_rf_19[] NATIVE_DATA(rf) = "300Hz AF Resp K Tx";
static const char ns_rf_20[] NATIVE_DATA(rf) = "300Hz AF Resp K Tx";
static const char ns_rf_21[] NATIVE_DATA(rf) = "300Hz AF Resp K Rx";
static const char ns_rf_22[] NATIVE_DATA(rf) = "300Hz AF Resp K Rx";
static const char ns_rf_23[] NATIVE_DATA(rf) = "3kHz AF Resp K Rx";
const RegisterSpec allRegisterSpecs[] NATIVE_DATA(rf) = {
    {ns_rf_5, 0x13, 8, 0b11, 1},
    {ns_rf_6, 0x13, 5, 0b111, 1},
    {ns_rf_7, 0x13, 0, 0b111, 1},
    {ns_rf_8, 0x13, 3, 0b11, 1},
    {ns_rf_9, 0x3C, 6, 0b11, 1},
    // {"-- MIC--",},
    {ns_rf_10, 0x40, 0, 0xFFF, 10},
    {ns_rf_11, 0x29, 14, 0b11, 1},
    {ns_rf_12, 0x29, 7, 0x7F, 1},
    {ns_rf_13, 0x29, 0, 0x7F, 1},
    {ns_rf_14, 0x19, 15, 1, 1},
    // {"----AFC----",},
    {ns_rf_15, 0x73, 11, 0b111, 1},
    {ns_rf_16, 0x73, 4, 1, 1},
    {ns_rf_17, 0x73, 5, 0b111111, 1},
    {ns_rf_18, 0x74, 0, 0xFFFF, 100},
    {ns_rf_19, 0x44, 0, 0xFFFF, 100},
    {ns_rf_20, 0x45, 0, 0xFFFF, 100},
    //  {"--RX FILT--",},
    {ns_rf_21, 0x54, 0, 0xFFFF, 100},
    {ns_rf_22, 0x55, 0, 0xFFFF, 100},
    {ns_rf_23, 0x75, 0, 0xFFFF, 100},
};

#define STILL_REGS_MAX_LINES 3
static uint8_t stillRegSelected = 0;
static uint8_t stillRegScroll = 0;
static bool stillEditRegs = false; // false = edycja czestotliwosci, true = edycja rejestrow

uint16_t statuslineUpdateTimer = 0;

static void RelaunchScan();
static void ResetInterrupts();
static char StringCode[10] = "";
static uint32_t stillFreq = 0;
static uint32_t GetInitialStillFreq(void)
{
    uint32_t f = 0;

    if (historyListActive) {
        f = GetHistoryFreq(historyListIndex);
    } else if (SpectrumMonitor) {
        f = lastReceivingFreq;
    } else if (gIsPeak) {
        f = peak.f;
    } else {
        f = scanInfo.f;
    }

    if (f < FMIN || f > FMAX) {
        if (scanInfo.f >= FMIN && scanInfo.f <= FMAX)
            return scanInfo.f;
        if (currentFreq >= FMIN && currentFreq <= FMAX)
            return currentFreq;
        return SpectrumRangeStart; // ostateczny fallback
    }

    return f;
}

static NATIVE_CODE(rf) uint16_t GetRegMenuValue(uint8_t st)
{
    RegisterSpec s = allRegisterSpecs[st];
    return (BK4819_ReadRegister(s.num) >> s.offset) & s.mask;
}

static NATIVE_CODE(rf) void SetRegMenuValue(uint8_t st, bool add)
{
    uint16_t v = GetRegMenuValue(st);
    RegisterSpec s = allRegisterSpecs[st];

    uint16_t reg = BK4819_ReadRegister(s.num);
    if (add && v <= s.mask - s.inc) {
        v += s.inc;
    } else if (!add && v >= 0 + s.inc) {
        v -= s.inc;
    }
    reg &= ~(s.mask << s.offset);
    BK4819_WriteRegister(s.num, reg | (v << s.offset));
}
static bool PTT_released = 0;
static bool last_ptt_state = 0;

KEY_Code_t GetKey()
{
    KEY_Code_t btn = KEYBOARD_Poll();
    const bool physical = GPIO_IsPttPressed();
    if (vfoScan)
        return physical ? KEY_PTT : btn;
    static KEY_Code_t side = KEY_INVALID;
    static uint16_t sideTicks;
    if (btn != side) {
        side = btn;
        sideTicks = 0;
    }
    if (sideTicks < key_repeat_delay_10ms)
        ++sideTicks;
    if (spectrumPtt2Key == KEY_INVALID && !last_ptt_state && (btn == KEY_SIDE1 || btn == KEY_SIDE2) &&
        !gIsKeylocked && !gReducedService && !SerialConfigInProgress()) {
        const uint8_t slot = btn == KEY_SIDE1 ? 0 : 2;
        const bool shortPtt = *SETTINGS_ShortcutSetting(slot) == ACTION_OPT_PTT2;
        const bool longPtt = *SETTINGS_ShortcutSetting(slot + 1) == ACTION_OPT_PTT2;
        if ((shortPtt && sideTicks >= 2) || (longPtt && sideTicks >= key_repeat_delay_10ms))
            spectrumPtt2Key = btn;
    }
    const bool ptt_pressed = physical || (spectrumPtt2Key != KEY_INVALID && btn == spectrumPtt2Key);

    if (ptt_pressed) {
        btn = KEY_PTT;
        PTT_released = 0;
        SpectrumPauseCount = 2000;
    } else {
        // If PTT was pressed during the previous cycle but is no longer pressed
        if (last_ptt_state) {
            PTT_released = 1;
        }
    }

    last_ptt_state = ptt_pressed; // Update for the next call

    if (gSetting_nav_invert) {
        if (btn == KEY_UP)
            btn = KEY_DOWN;
        else if (btn == KEY_DOWN)
            btn = KEY_UP;
    }
    if (PTT_released) {
        PTT_released = 0;
        Spectrum_END_TX();
        spectrumPtt2Key = KEY_INVALID;
        SPECTRUM_PAUSED = false;
    }

    return btn;
}

static void SetState(State state)
{
    if (currentState == SPECTRUM && state != SPECTRUM)
        scanOptionsChanged = false;
    currentState = state;
    selectionHelp = false;
    if (!SpectrumMonitor && !WaitSpectrum) {
        spectrumElapsedCount = 0;
        Skip();
    }
}

// ============================================================
// SECTION: Radio / hardware functions
// ============================================================

static void BackupRegisters(void)
{
    for (unsigned i = 0; i < 16; ++i)
        savedRegisters[i] = BK4819_ReadRegister(savedRegisterAddresses[i]);
}

static void RestoreRegisters(void)
{
    for (unsigned i = 0; i < 16; ++i)
        BK4819_WriteRegister(savedRegisterAddresses[i], savedRegisters[i]);
}

static void ToggleAFBit(bool on)
{
    uint32_t reg = reg_47_cache;
    reg &= ~(1 << 8);
    if (on)
        reg |= on << 8;
    BK4819_WriteRegister(BK4819_REG_47, reg);
}

static void ToggleAFDAC(bool on)
{
    uint32_t Reg = reg_30_cache;
    Reg &= ~(1 << 9);
    if (on)
        Reg |= (1 << 9);
    BK4819_WriteRegister(BK4819_REG_30, Reg);
}

static void SetF(uint32_t sf)
{
    uint32_t f = sf;
    if (f < FMIN || f > FMAX)
        return;
    if (SPECTRUM_PAUSED)
        return;
    BK4819_SetFrequency(f);
    uint8_t currentFilterPath = (f < 28000000) ? 1 : 0;
    if (currentFilterPath != lastFilterPath)
        BK4819_PickRXFilterPathBasedOnFrequency(f);
    uint16_t reg = reg_30_cache;
    BK4819_WriteRegister(BK4819_REG_30, 0x200); //AF DAC Enable.
    BK4819_WriteRegister(BK4819_REG_30, reg);
    lastFilterPath = currentFilterPath;
}

static void ResetInterrupts()
{
    // disable interupts
    BK4819_WriteRegister(BK4819_REG_3F, 0);
    // reset the interrupt
    BK4819_WriteRegister(BK4819_REG_02, 0);
}

// scan step in 0.01khz
static uint32_t GetScanStep()
{
    return scanStepValues[settings.scanStepIndex];
}

static uint16_t GetStepsCount()
{
    if (vfoScan && vfoRangeSession)
        return 1;
    if (gHistoryScan && historyListActive)
        return CountValidHistoryItems();
    if (scanEmpty)
        return 0;
    if (Spectrum_state == CHANNEL_MODE) {
        return scanChannelsCount;
    }
    if (Spectrum_state == SCAN_RANGE_MODE) {
        return (SpectrumRangeStop - SpectrumRangeStart) / scanInfo.scanStep;
    }
    if (Spectrum_state == SCAN_BAND_MODE) {
        return (SpectrumRangeStop - SpectrumRangeStart) / scanInfo.scanStep;
    }

    return 128 >> settings.stepsCount;
}


bool APP_SpectrumBacklightOff(void)
{
    return isInitialized && !vfoScan && !Backlight_On;
}

static void DeInitSpectrum(void)
{
    /* ML WATCH runs behind the main screen. When an F shortcut ends it, the
     * F modifier belongs to the main screen, which then runs the shortcut
     * exactly as in VFO; the watch resumes when the main screen is idle. */
    if (!MLWatchActive() || kbd.current == KEY_INVALID)
        gWasFKeyPressed = false;
    gScanSqlPreview = 0;
    if (MLWatchActive()) {
        isInitialized = false;
        gIsPeak = false;
        ToggleRX(false);
        RestoreRegisters();
        // The common VFO-scan epilogue restores the foreground once.
        return;
    }

    if (historyListActive)
        HistoryClose();
    SaveSettings(false);
    settings.listenBw = savedListenBw;
    listenBwAM = savedListenBwAM;
    settings.scanStepIndex = savedScanStep;
    scanBwOverrides = 0;
    scanStepOverride = false;
    gForceModulation = false;
    isInitialized = false;
    Backlight_On = true;
    BACKLIGHT_TurnOn();
    historyListActive = false;
    SPECTRUM_PAUSED = false;
    SpectrumPauseCount = 0;
    WaitSpectrum = 0;
    gIsPeak = false;
    SpectrumMonitor = 0;
    isListening = false;
    ToggleRX(false);

    RestoreRegisters();
    gVfoConfigureMode = VFO_CONFIGURE;

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
    gEeprom.CURRENT_STATE = 0;
    SETTINGS_WriteCurrentState();
#endif
    if (gRequestSaveSettings) {
        // Save only AUROG: the live VFO fields here still belong to the scanner.
        APP_SaveScanSQL();
        SETTINGS_SaveAutoRoger();
        gRequestSaveSettings = false;
    }
    SYSTEM_DelayMs(50);
    BK4819_Init();
    SETTINGS_InitEEPROM();
    RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE_RELOAD);
    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);

    // Re-arm the normal RX path after leaving Spectrum. The scanner uses its
    // own BK4819 state and does not pass through the normal listening path.
    // APP_StartListening() restores the software RX state as well as the
    // audio/modulation path; HandleReceive() returns to foreground/dual-watch
    // when no signal is present.
    if (gEeprom.RX_MODE == RX_MODE_DUAL) {
        APP_StartListening(FUNCTION_RECEIVE);

        // The scanner exit must leave the active VFO ready to receive,
        // but must not leave Dual-Watch locked on that VFO.  APP_StartListening()
        // sets gRxVfoIsActive=true because it normally starts a real RX event.
        // Here we are only re-arming the foreground after EXIT, so release the
        // RX lock and let the normal 100 ms Dual-Watch scheduler resume.
        gRxVfoIsActive = false;
        gRxReceptionMode = RX_MODE_NONE;
        gDualVfoSchedule = false;
        gDualVfoCountdown_10ms = 10;
        FUNCTION_Select(FUNCTION_FOREGROUND);
    }
    if (vfoRangeSession) {
        FUNCTION_Select(FUNCTION_FOREGROUND);
        BACKLIGHT_TurnOn();
    }
    if (toVfoFrequency) {
        FrequencyDraft(gTxVfo, toVfoFrequency, toVfoProfile, toVfoCss);
        toVfoFrequency = 0;
        MAIN_FrequencyToVfo();
        APP_ConsumeHeldKey(KEY_1); // the long 1 still held must not type a digit
    }
}

static void DeleteHistoryItem(void)
{
    if (!historyListActive || indexFs == 0)
        return;
    if (historyListIndex >= indexFs) {
        historyListIndex = (indexFs > 0) ? indexFs - 1 : 0;
        if (indexFs == 0)
            return;
    }
    uint16_t indexToDelete = historyListIndex;
    memmove(&history[indexToDelete], &history[indexToDelete + 1],
            (indexFs - indexToDelete - 1) * sizeof *history);
    indexFs--;

    SetHistoryFreq(indexFs, 0);
    history[indexFs].HBlacklisted = 0xFF;
    history[indexFs].code = 0;
    history[indexFs].HTimeS = 0;
    history[indexFs].record = 0;
    if (historyListIndex >= indexFs && indexFs > 0) {
        historyListIndex = indexFs - 1;
    } else if (indexFs == 0) {
        historyListIndex = 0;
    }
    ShowOSDPopup("Deleted");
}


static uint8_t CurrentHistoryProfile(void)
{
    return settings.modulationType | (ScanListeningBandwidth(settings.modulationType) << 2);
}

// Zero is an old history without a stored RX profile.
static uint16_t HistoryCode(uint16_t i)
{
    uint8_t c = history[i].code;
    if (c < 50)
        return (CODE_TYPE_CONTINUOUS_TONE << 8) | c;
    if (c >= 100 && c < 100 + DCS_OPTION_COUNT)
        return ((history[i].inverse ? CODE_TYPE_REVERSE_DIGITAL : CODE_TYPE_DIGITAL) << 8) | (c - 100);
    return 0;
}
static uint8_t HistoryProfile(uint16_t i)
{
    const uint8_t profile = history[i].profile;
    return profile && profile <= 11 && (profile & 3) ? profile - 1 : CurrentHistoryProfile();
}

static void SaveAllHistoryToFreeChannels(void)
{
    uint16_t copied = 0, first = UINT16_MAX;
    for (uint16_t i = 0; i < indexFs; ++i) {
        const uint8_t profile = HistoryProfile(i);
        const int16_t ch =
            CHANNEL_SaveHistoryFrequency(GetHistoryFreq(i), profile & 3, profile >> 2, HistoryCode(i));
        if (ch == -3) {
            ShowOSDPopup("FLASH ERROR");
            return;
        }
        if (ch == -2)
            break; // No ordinary memory remains; never use a reserved band slot.
        if (ch >= 0) {
            if (!copied)
                first = ch;
            ++copied;
        }
    }
    LoadActiveScanFrequencies();
    char text[32];
    if (copied)
        snprintf(text, sizeof text, "COPIED %u CH:%u", copied, first + 1);
    else
        strcpy(text, "NO NEW CHANNELS");
    ShowOSDPopup(text);
    if (osdPopupTimer < 1500)
        osdPopupTimer = 1500; // Always show the actual copy result.
}

static bool historyLoaded = false; // flaga stanu wczytania histotii spectrum

void LoadHistory(void)
{
    HistoryStruct History = {0};
    memset(history, 0, sizeof(history));
    indexFs = 0;
    uint32_t countsMagic;
    PY25Q16_ReadBuffer(HISTORY_COUNTS_ADDR, (uint8_t *)&countsMagic, sizeof countsMagic);

    for (uint16_t position = 0; position < HISTORY_SIZE; position++) {
        PY25Q16_ReadBuffer(ADRESS_HISTORY + position * sizeof(HistoryStruct), (uint8_t *)&History,
                           sizeof(HistoryStruct));
        if (History.HBlacklisted == 0xFF) {
            indexFs = position;
            break;
        }
        if (History.HFreqs) {
            SetHistoryFreq(position, History.HFreqs);
            history[position].HBlacklisted = History.HBlacklisted;
            history[position].code = History.code;
            history[position].HTimeS = History.HTimeS;
            if (countsMagic == HISTORY_COUNTS_MAGIC || countsMagic == 0x314E4352u)
                PY25Q16_ReadBuffer(HISTORY_COUNTS_ADDR + 4 + position * 2,
                                   (uint8_t *)&history[position].record, 2);
            indexFs = position + 1;
        }
    }
}

static void CompactHistory(void)
{
    uint16_t w = 0;
    uint16_t limit = (indexFs > HISTORY_SIZE) ? HISTORY_SIZE : indexFs;

    for (uint16_t r = 0; r < limit; r++) {
        if (GetHistoryFreq(r) == 0)
            continue;
        if (w != r) {
            history[w] = history[r];
        }
        w++;
    }

    for (uint16_t i = w; i < limit; i++) {
        SetHistoryFreq(i, 0);
        history[i].HBlacklisted = 0;
        history[i].code = 0;
        history[i].HTimeS = 0;
        history[i].record = 0;
    }

    indexFs = w;
    if (indexFs == 0) {
        historyListIndex = 0;
        historyScrollOffset = 0;
    } else {
        if (historyListIndex >= indexFs)
            historyListIndex = indexFs - 1;
        if (historyScrollOffset >= indexFs) {
            historyScrollOffset = (indexFs > MAX_VISIBLE_LINES) ? (indexFs - MAX_VISIBLE_LINES) : 0;
        }
    }
}


void SaveHistory(void)
{
    uint32_t startAddr = ADRESS_HISTORY;
    uint32_t endAddr = HISTORY_COUNTS_ADDR + 4 + indexFs * 2;

    for (uint32_t addr = startAddr; addr < endAddr; addr += SECTOR_SIZE) {
        PY25Q16_SectorErase(addr);
    }

    HistoryStruct historyItem;

    PY25Q16_BeginBatch();
    for (uint16_t i = 0; i < indexFs; ++i) {
        PY25Q16_WriteBuffer(ADRESS_HISTORY + i * sizeof(HistoryStruct), (uint8_t *)&history[i],
                            sizeof(HistoryStruct), false);
#if defined(ENABLE_LARGE_HISTORY)
    }
    // The 500-entry configuration spans two cached sectors: keep writes grouped.
    for (uint16_t i = 0; i < indexFs; ++i) {
#endif
        PY25Q16_WriteBuffer(HISTORY_COUNTS_ADDR + 4 + i * 2, (uint8_t *)&history[i].record, 2, false);
    }
    const uint32_t magic = HISTORY_COUNTS_MAGIC;
    PY25Q16_WriteBuffer(HISTORY_COUNTS_ADDR, (const uint8_t *)&magic, sizeof magic, false);

    historyItem.HFreqs = 0;
    historyItem.HBlacklisted = 0xFF;
    historyItem.code = 0;
    historyItem.HTimeS = 0;

    PY25Q16_WriteBuffer(ADRESS_HISTORY + indexFs * sizeof(HistoryStruct), (uint8_t *)&historyItem,
                        sizeof(HistoryStruct), 0);
    PY25Q16_EndBatch();

    ShowOSDPopup("HISTORY SAVED");
}

static void Spectrum_ResumeAfterTx(uint32_t txFreq)
{
    rssiReferenceValid = false;
    if (HistorySilent()) { // no reply listening on a silent history page
        SPECTRUM_PAUSED = false;
        SpectrumPauseCount = 0;
        return;
    }

    /*
     * After PTT, stay on the frequency that was actually used for TX.
     * Do not call Skip() here: Skip() advances the scan immediately.
     * SpectrumDelay temporarily blocks the scanner while RX waits for
     * a possible reply on the TX frequency.
     */
    if (txFreq >= FMIN && txFreq <= FMAX) {
        SPECTRUM_PAUSED = false;
        SpectrumPauseCount = 0;
        scanInfo.f = txFreq;
        peak.f = txFreq;
        lastReceivingFreq = txFreq;

        SetF(txFreq);
        postTxListening = true;
        ToggleRX(true);

        WaitSpectrum = SpectrumDelay;
        spectrumElapsedCount = 0;
    } else {
        SPECTRUM_PAUSED = false;
        SpectrumPauseCount = 0;
        WaitSpectrum = 0;
        Skip();
    }
}

static void Spectrum_END_TX(void)
{
    const uint32_t txFreq = gCurrentVfo->pTX->Frequency;
    if (TX_freq_check(txFreq) != 0) {
        ShowOSDPopup("TX DISABLE");
        return;
    }
    RADIO_SendEndOfTransmission();
    Spectrum_ResumeAfterTx(txFreq);
}

static uint8_t SpectrumTxRole(void)
{
    return spectrumPtt2Key == KEY_INVALID ? PttEmission : Ptt2Emission;
}
static void Spectrum_Prepare_Tx(void)
{
    const uint8_t role = SpectrumTxRole();
    if (!vfoScan) {
        gEeprom.TX_VFO = channelTxSlot;
        gEeprom.ScreenChannel[channelTxSlot] = channelTxScreen;
        gEeprom.MrChannel[channelTxSlot] = channelTxMemory;
        gTxVfo = &gEeprom.VfoInfo[channelTxSlot];
        *gTxVfo = channelTxVfo;
    }
    if (role == 2) {
        const uint32_t frequency = GetHistoryFreq(0);
        TxChannel = BOARD_gMR_fetchChannel(frequency);
        if (IS_MR_CHANNEL(TxChannel)) {
            gEeprom.ScreenChannel[gEeprom.TX_VFO] = TxChannel;
            gEeprom.MrChannel[gEeprom.TX_VFO] = TxChannel;
            RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE_RELOAD);
        } else {
            RADIO_InitInfo(gTxVfo, FREQ_CHANNEL, frequency);
            gTxVfo->Modulation = MODULATION_FM;
            gTxVfo->OUTPUT_POWER = OUTPUT_POWER_HIGH;
            uint16_t css = HistoryCode(0);
            gTxVfo->freq_config_TX.CodeType = css >> 8;
            gTxVfo->freq_config_TX.Code = (uint8_t)css;
        }
    }
    gCurrentVfo = gTxVfo;
}

static void SpectrumTransmit(void)
{
    if (SerialConfigInProgress() || gReducedService)
        return;
    if (SpectrumTxRole() == 2 && !GetHistoryFreq(0)) {
        ShowOSDPopup("NO HISTORY");
        return;
    }
    Spectrum_Prepare_Tx();
    if (TX_freq_check(gCurrentVfo->pTX->Frequency) != 0) {
        ShowOSDPopup("TX DISABLE");
        return;
    }
    WaitSpectrum = 0;
    SPECTRUM_PAUSED = true;
    gIsPeak = false;
    isListening = false;
    SpectrumPauseCount = 2000;
    RADIO_SetTxParameters();
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    if (gEeprom.SCRAMBLING_TYPE > 0)
        BK4819_EnableScramble(gEeprom.SCRAMBLING_TYPE - 1);
    else
        BK4819_DisableScramble();
    if (gSetting_backlight_on_tx_rx & BACKLIGHT_ON_TR_TX)
        BACKLIGHT_TurnOn();
}

static uint16_t GetRssi(void)
{
    uint16_t rssi;
    if (isListening)
        SYSTICK_DelayUs(12000);
    else
        SYSTICK_DelayUs(DelayRssi);
    rssi = BK4819_GetRSSI();
    //if (scanInfo.f > 30000000) {rssi += UHF_NOISE_FLOOR;}
    return rssi;
}

static void ToggleAudio(bool on)
{
    if (on == audioState) {
        return;
    }
    audioState = on;
    if (on) {
        GPIO_EnableAudioPath();
    } else {
        GPIO_DisableAudioPath();
    }
}

static uint16_t CountValidHistoryItems()
{
    return (indexFs > HISTORY_SIZE) ? HISTORY_SIZE : indexFs;
}

// ------------------ CSS detection ------------------

static void UpdateCssDetection(void)
{
    if (settings.modulationType != MODULATION_FM) {
        code = 0xff;
        StringCode[0] = 0;
        stringCodeTimer = 0;
        return;
    }
    BK4819_WriteRegister(BK4819_REG_51, BK4819_REG_51_DISABLE_CxCSS | BK4819_REG_51_GPIO6_PIN2_NORMAL |
                                            BK4819_REG_51_TX_CDCSS_POSITIVE | BK4819_REG_51_MODE_CDCSS |
                                            BK4819_REG_51_CDCSS_23_BIT | BK4819_REG_51_1050HZ_NO_DETECTION |
                                            BK4819_REG_51_AUTO_CDCSS_BW_DISABLE |
                                            BK4819_REG_51_AUTO_CTCSS_BW_DISABLE);
    DCS_CodeType_t type;
    uint8_t option;
    if (!DCS_ReadReceivedCode(&type, &option))
        return;
    const uint8_t detected = option + (type == CODE_TYPE_CONTINUOUS_TONE ? 0 : 100);
    if (CodeFreq != peak.f || code != detected || !StringCode[0] ||
        (type != CODE_TYPE_CONTINUOUS_TONE && (StringCode[4] == 'R') != (type == CODE_TYPE_REVERSE_DIGITAL)))
        rxDisplayPending = true;
    CodeFreq = peak.f;
    DCS_FormatCode(StringCode, type, option, type != CODE_TYPE_CONTINUOUS_TONE);
    code = detected;
    stringCodeTimer = STRINGCODE_TIMEOUT_MS;
}


static void RememberHistoryFrequency(uint32_t f, bool reception)
{
    lastHistoryScrollOffset = -1;
    if (f == 0 || f < FMIN || f > FMAX)
        return;

    uint16_t foundIndex = 0xFFFF;
    uint16_t foundTime = 0, receptions = 0;
    bool foundBlacklisted = false, foundInverse = false;
    uint8_t foundCode = 0xFF;

    // Check whether the frequency already exists in the history
    uint32_t qf = f;
    for (uint16_t i = 0; i < indexFs; i++) {
        if (GetHistoryFreq(i) == qf) {
            foundIndex = i;
            foundTime = history[i].HTimeS;
            receptions = history[i].receptions;
            foundBlacklisted = history[i].HBlacklisted;
            foundCode = history[i].code;
            foundInverse = history[i].inverse; // Keep the already stored code
            break;
        }
    }

    // If a new code was just detected, replace the old one
    if (CodeFreq == f && code != 0xFF) {
        foundCode = code;
        foundInverse = code >= 100 && code < 100 + DCS_OPTION_COUNT && StringCode[4] == 'R';
    }

    if (reception && receptions < 999)
        ++receptions;
    const uint16_t record =
        receptions | ((1 + CurrentHistoryProfile()) << 10) | ((uint16_t)foundInverse << 14);
    bool freezeOrder = historyListActive && (SpectrumMonitor || gHistoryScan);
    if (freezeOrder) {
        if (foundIndex != 0xFFFF) {
            history[foundIndex].record = record;
            history[foundIndex].code = foundCode;
        }
        lastReceivingFreq = f;
        return;
    }

    // Move only the prefix before an existing entry; its suffix stays in place.
    const uint16_t limit = foundIndex != UINT16_MAX ? foundIndex : MIN(indexFs, HISTORY_SIZE - 1);
    memmove(&history[1], &history[0], limit * sizeof *history);

    // Insertion en position 0
    SetHistoryFreq(0, f);
    history[0].HBlacklisted = foundBlacklisted;
    history[0].code = foundCode; // The code is preserved or updated correctly
    history[0].HTimeS = foundTime;
    history[0].record = record;

    if (foundIndex == UINT16_MAX && indexFs < HISTORY_SIZE)
        indexFs++;
    historyListIndex = 0;
    lastReceivingFreq = f;
}

// Reuse the resident audio/AGC profile, and avoid rewriting it at every step.


// Shared pre-v4.3 detection/listening profiles for both scan speeds.
// Closing RX must also restore any registers changed by SoundBoost.
static void ApplyRXProfile(bool listening)
{
    const uint8_t mode = listening ? settings.modulationType : MODULATION_FM;
    uint8_t bandwidth = !listening ? BK4819_FILTER_BW_WIDE : ListeningFilter(mode);
    if (listening && (vfoScan || vfoRangeSession)) {
        const bool narrow =
            vfoRangeSession ? settings.listenBw != 0 : (ReadScanParameters(ScanChannel) >> 16) & 1u;
        bandwidth = mode == MODULATION_AM ? (narrow ? BK4819_FILTER_BW_AM : BK4819_FILTER_BW_WIDE)
                    : narrow ? (gSetting_set_nfm ? BK4819_FILTER_BW_NARROWER : BK4819_FILTER_BW_NARROW)
                             : BK4819_FILTER_BW_WIDE;
    }
    if (listening && historyListActive && SpectrumMonitor && historyBandwidth != UINT8_MAX)
        bandwidth = mode == MODULATION_AM ? (historyBandwidth ? BK4819_FILTER_BW_AM : BK4819_FILTER_BW_WIDE)
                                          : historyBandwidth;
    RADIO_SetModulation(mode);
    BK4819_SetFilterBandwidth(bandwidth, mode == MODULATION_AM);
}

static void FillfreqHistory(void)
{
    UpdateCssDetection();
    RememberHistoryFrequency(peak.f, false);
}

static void ToggleRX(bool on)
{
    scanNoiseCount = 0;
    const bool receivedReply = postTxListening && on && gIsPeak;
    const bool newReception = on && (!isListening || receivedReply);
    if (!on || gIsPeak)
        postTxListening = false;
    // A user pause closes RX even with monitor or scan sleep enabled.
    if (SPECTRUM_PAUSED || (!AbsoluteSQL() && ScanTrigger() == 50))
        return;
    if (!on && SpectrumMonitor == 2) {
        isListening = 1;
        return;
    }
    if (sentinelView && on != isListening) {
        sentinelIdleTicks = 0;
        sentinelTimerTick = gGlobalSysTickCounter;
    }
    if (newReception) {
        StringCode[0] = 0;
        if (gIsPeak && Backlight_On && (gSetting_backlight_on_tx_rx & BACKLIGHT_ON_TR_RX))
            BACKLIGHT_TurnOn();
        rxDisplayPending = true;
        sentinelMeterDue = false;
        spectrumRxDbmFresh = true;
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
        // Match APP_StartListening: reset once at the start of each VFO RX,
        // rather than inheriting the last RX/idle countdown.
        if (vfoScan)
            gRxTimerCountdown_500ms = 7200;
#endif
    }
    if (!on)
        rxDisplayPending = false;
    isListening = on;
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on);
    if (!last_ptt_state) // a scan blink in progress ends with the RX change
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
    if (on) {
        const uint32_t frequency = gIsPeak ? peak.f : scanInfo.f;
        const bool currentMemory = Spectrum_state == CHANNEL_MODE && ScanChannel < MAX_SCAN_CHANNELS &&
                                   FetchChannelFrequency(ScanChannel).frequency == frequency;
        gChannel = (vfoScan || currentMemory) ? ScanChannel : BOARD_gMR_fetchChannel(frequency);
        isKnownChannel = IS_MR_CHANNEL(gChannel);
        if (!postTxListening) {
            lastNamedFrequency = frequency;
            lastNamedChannel = ReceivedMemoryIdentity(frequency, gChannel);
            if (IS_MR_CHANNEL(lastNamedChannel))
                SETTINGS_FetchChannelName(channelName, lastNamedChannel);
            else
                UI_FormatFrequency(channelName, frequency);
        }
        if (isKnownChannel && Spectrum_state == CHANNEL_MODE && !gForceModulation)
            settings.modulationType = (uint8_t)ReadScanParameters(gChannel);
    }

    if (vfoScan) {
        if (MLWatchActive())
            g_SquelchLost = on;
        VfoScanSyncDisplay();
        gUpdateDisplay = true;
        FUNCTION_Select(on ? FUNCTION_RECEIVE : FUNCTION_FOREGROUND);
    }
    if (on && Spectrum_state == SCAN_BAND_MODE) {
        if (!gForceModulation)
            settings.modulationType = BParams[bl].modulationType;
    }

    if (newReception && isInitialized && !vfoScan && !postTxListening)
        RememberHistoryFrequency(gIsPeak ? peak.f : scanInfo.f, true);
    if (on && (isKnownChannel || Spectrum_state == SCAN_BAND_MODE))
        RADIO_SetupAGC(settings.modulationType == MODULATION_AM, false);

    if (on) {
        // rxDisplayPending requests one complete frame. Drawing here over the
        // previous scan frame leaves stale glyphs around the Sentinel meter.
        if (SpectrumMonitor != 1) {
            BK4819_RX_TurnOn();
            SYSTEM_DelayMs(20);
        }
        ApplyRXProfile(true);
        BK4819_WriteRegister(BK4819_REG_3F, BK4819_REG_02_CxCSS_TAIL);
    } else {
        if (SpectrumMonitor != 1)
            ApplyRXProfile(false);
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, 0);
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, 0);
    }

    if (on != audioState) {
        ToggleAudio(on);
        if (SpectrumMonitor != 1) {
            ToggleAFDAC(on);
            ToggleAFBit(on);
        }
        audioState = on;
    }
}

static void UpdateSpectrumMonitorLeds(void)
{
    if (!last_ptt_state)
        return;
    static uint8_t appliedSpectrumMonitor = 0xff;
    if (SpectrumMonitor == 1) {
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
    } else if (appliedSpectrumMonitor == 1) {
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    }

    appliedSpectrumMonitor = SpectrumMonitor;
}

#ifdef ENABLE_BENCH
static void ResetBenchStats(void)
{
    benchTickMs = 0;
    benchStepsThisSec = 0;
    benchRatePerSec = 0;
    benchLapMs = 0;
    benchLastLapMs = 0;
}
#endif


static void ResetScanStats()
{
    scanInfo.rssiMax = scanInfo.rssiMin + 20;
}

static bool InitScan()
{
    if (SpectrumMonitor || WaitSpectrum)
        return true;
    ResetScanStats();
    scanInfo.i = 0;
    peak.i = 0;
    peak.f = 0;

    scanEmpty = Spectrum_state == CHANNEL_MODE ? !scanChannelsCount
                                               : Spectrum_state == SCAN_BAND_MODE && !CountActiveBands();
    if (scanEmpty) {
        scanInfo.f = 0;
        return false;
    }
    bool scanInitializedSuccessfully = false;
    switch (Spectrum_state) {
        case SCAN_BAND_MODE:
            uint8_t checkedBandCount = 0;
            while (checkedBandCount < bandCount) {
                if (settings.bandEnabled[nextBandToScanIndex]) {
                    bl = nextBandToScanIndex;
                    scanInfo.f = BParams[bl].Startfrequency;
                    scanInfo.scanStep =
                        scanStepValues[scanStepOverride ? settings.scanStepIndex : BParams[bl].scanStep];
                    if (!scanStepOverride)
                        settings.scanStepIndex = BParams[bl].scanStep;
                    if (BParams[bl].Startfrequency > 0)
                        SpectrumRangeStart = BParams[bl].Startfrequency;
                    if (BParams[bl].Stopfrequency > 0)
                        SpectrumRangeStop = BParams[bl].Stopfrequency;
                    if (!gForceModulation)
                        settings.modulationType = BParams[bl].modulationType;
                    nextBandToScanIndex = (nextBandToScanIndex + 1) % bandCount;
                    scanInitializedSuccessfully = true;
                    break;
                }
                nextBandToScanIndex = (nextBandToScanIndex + 1) % bandCount;
                checkedBandCount++;
            }
            break;

        case SCAN_RANGE_MODE:
            if (rangeModConfigured && !vfoRangeSession && !gForceModulation)
                settings.modulationType = rangeModulation;
            SpectrumRangeStart = RangeStart;
            SpectrumRangeStop = RangeStop;
            if (SpectrumRangeStart > SpectrumRangeStop)
                SWAP(SpectrumRangeStart, SpectrumRangeStop);
            scanInfo.f = SpectrumRangeStart;
            scanInfo.scanStep = GetScanStep();
            scanInitializedSuccessfully = true;
            break;

        case CHANNEL_MODE:
            if (scanChannelsCount == 0) {
                return false;
            }
            const uint16_t first = GetNextChannelInSelectedScanLists(0, 1);
            statusScanChannel = first;
            activityLists.current = nextDisplayList;
            ScanChannel = first;
            ChannelInfo_t info = FetchChannelFrequency(first);
            activityMemoryFrequency = info.frequency;
            scanInfo.f = info.frequency;
            peak.f = scanInfo.f;
            peak.i = 0;
            break;
    }
    if (!vfoScan)
        ActivityContext(&activity, SpectrumRangeStart, SpectrumRangeStop, Spectrum_state,
                        gGlobalSysTickCounter);

    return scanInitializedSuccessfully;
}

static void ResetModifiers()
{
    memset(StringC, 0, sizeof(StringC));
    for (int i = 0; i < 128; ++i) {
        if (rssiHistory[i] == RSSI_MAX_VALUE)
            rssiHistory[i] = 0;
    }
    LoadActiveScanFrequencies();
    RelaunchScan();
}

static void RelaunchScan()
{
    if (SpectrumMonitor || WaitSpectrum)
        return;
    // Selection changes restart the interval; parameter menus retain it.
    if (currentState != PARAMETERS_SELECT && currentState != RANGE_EDIT) {
        sentinelIdleTicks = 0;
        sentinelTimerTick = gGlobalSysTickCounter;
    }
    rssiReferenceValid = false;
    InitScan();
    if (!vfoScan)
        ActivityReset(&activity, SpectrumRangeStart, SpectrumRangeStop, Spectrum_state,
                      gGlobalSysTickCounter);
    //ToggleRX(false);
    scanInfo.rssiMin = RSSI_MAX_VALUE;
    gIsPeak = false;
#ifdef ENABLE_BENCH
    ResetBenchStats();
#endif
}

uint8_t BK4819_GetExNoiseIndicator(void)
{
    return BK4819_ReadRegister(BK4819_REG_65) & 0x007F;
}

static void UpdateNoiseOff()
{
    if (!Noislvl_OFF)
        return;
    if (BK4819_GetExNoiseIndicator() > Noislvl_OFF) {
        gIsPeak = false;
        if (isListening)
            ToggleRX(false);
    }
}

static void UpdateNoiseOn()
{
    if (!Noislvl_OFF || BK4819_GetExNoiseIndicator() < Noislvl_ON) {
        gIsPeak = true;
        ToggleRX(1);
    }
}

// Retain the existing noise detector only for low effective closing levels.
// Two fresh samples, 200 ms apart, reject one-off noise spikes. No added read
// in the scan/detection path, higher-threshold RX, Sentinel, USB or Monitor.
static bool ScanNoiseClosed(void)
{
    if (SpectrumMonitor == 2 || ScanHoldThreshold() > -114 ||
        (settings.modulationType != MODULATION_AM && settings.modulationType != MODULATION_FM) ||
        !Noislvl_OFF || BK4819_GetExNoiseIndicator() <= Noislvl_OFF) {
        scanNoiseCount = 0;
        return false;
    }
    return !isListening || ++scanNoiseCount >= 2;
}

static void UpdateScanInfo()
{
    if (scanInfo.rssi > scanInfo.rssiMax) {
        scanInfo.rssiMax = scanInfo.rssi;
    }
    if (scanInfo.rssi < scanInfo.rssiMin && scanInfo.rssi > 0) {
        scanInfo.rssiMin = scanInfo.rssi;
    }
}

static __attribute__((noinline)) void RecordActivity(uint32_t frequency, uint16_t channel, uint16_t sample)
{
    if (Spectrum_state == CHANNEL_MODE) {
        if (frequency == activityMemoryFrequency)
            ActivityRecordList(&activity, &activityLists, channel, sample);
    } else
        ActivityRecordFrequency(&activity, frequency, scanInfo.scanStep, sample);
}
static __attribute__((noinline)) void Measure(void)
{
    // Excluded bins must never seed the next frequency with a false zero.
    if (scanInfo.f % 1300000 == 0 || IsBlacklisted(scanInfo.f))
        return;
    int16_t reference = previousRssi;
    uint16_t rssi = scanInfo.rssi = GetRssi();
    UpdateScanInfo();
    if (!rssiReferenceValid) {
        reference = rssi;
        gIsPeak = false;
        rssiReferenceValid = true;
    }
    const bool absolute = AbsoluteSQL();
    const unsigned trigger = ScanTrigger();
    if (!absolute && trigger == 50) {
        if (rssi > reference + UOO_trigger) {
            peak.f = scanInfo.f;
            peak.i = scanInfo.i;
            gIsPeak = false;
            isListening = false;
            FillfreqHistory();
        }
    } else if (!gIsPeak && rssi > reference + trigger) {
        SYSTEM_DelayMs(10);
        const uint16_t confirmed = scanInfo.rssi = GetRssi();
        if (confirmed > reference + trigger && (!absolute || AboveScanSQL(confirmed, scanInfo.f))) {
            peak.f = scanInfo.f;
            peak.i = scanInfo.i - 1;
            gIsPeak = true;
            UpdateNoiseOff();
        }
    }
    if (!gIsPeak || !isListening)
        reference = rssi;
    else if (rssi < reference)
        reference = rssi;

    previousRssi = reference;
    const uint16_t displayRssi = ActivityEncode(scanInfo.rssi, dBmCorrTable[FREQUENCY_GetBand(scanInfo.f)]);
    uint16_t count = GetStepsCount();
    uint16_t i = scanInfo.i;
    if (!vfoScan)
        RecordActivity(scanInfo.f, statusScanChannel, displayRssi);

    static uint8_t pixel;
    if (count > 128) {
        uint32_t diff = (scanInfo.f - SpectrumRangeStart) / 100;
        uint32_t span = (SpectrumRangeStop - SpectrumRangeStart) / 100;
        if (span > 0) {
            pixel = (diff * 127) / span;
            if (pixel < 128) {
                rssiHistory[pixel] = rssi;
            }
        }
    }

    if (count <= 128) {
        uint16_t base = 128 / count;
        uint16_t rem = 128 % count;
        uint16_t start = i * base + (i < rem ? i : rem);
        uint16_t end = (i + 1) * base + ((i + 1) < rem ? (i + 1) : rem);
        if (end > 128)
            end = 128;
        for (uint16_t j = start; j < end; ++j) {
            rssiHistory[j] = rssi;
        }
    }
}

static void UpdateScanStep(bool inc)
{
    scanStepOverride = true;
    if (inc) {
        settings.scanStepIndex =
            (settings.scanStepIndex >= STEP_500kHz) ? STEP_0_01kHz : settings.scanStepIndex + 1;
    } else {
        settings.scanStepIndex =
            (settings.scanStepIndex <= STEP_0_01kHz) ? STEP_500kHz : settings.scanStepIndex - 1;
    }
}

static uint32_t SpectrumStatusParameters(void);
static void ToggleModulation()
{
    if ((Spectrum_state == CHANNEL_MODE || Spectrum_state == SCAN_BAND_MODE) && !gForceModulation) {
        settings.modulationType = (uint8_t)SpectrumStatusParameters();
    }
    if (settings.modulationType < MODULATION_UKNOWN - 1) {
        settings.modulationType++;
    } else {
        settings.modulationType = MODULATION_FM;
    }
    if (isListening)
        RADIO_SetModulation(settings.modulationType);
    BK4819_InitAGC(settings.modulationType);
    if (Spectrum_state == SCAN_RANGE_MODE && rangeModConfigured)
        rangeModulation = settings.modulationType;
    gForceModulation = 1;
    if (!vfoScan)
        scanMenuModulation = settings.modulationType;
}

static void ToggleListeningBW(bool inc)
{
    settings.listenBw = ACTION_NextBandwidth(ScanListeningBandwidth(MODULATION_FM), false, inc);
    scanBwOverrides |= 1;
    if (!vfoScan)
        scanMenuBandwidth = scanBwOverrides;
    if (!isListening)
        ToggleRX(false);
}


static void Skip()
{

    isListening = 0;
    WaitSpectrum = 0;
    spectrumElapsedCount = 0;
    NextScanStep();
    gIsPeak = false;
    ToggleRX(false);
    peak.f = scanInfo.f;
    peak.i = scanInfo.i;
    ToggleAudio(0);
    ToggleAFDAC(0);
    ToggleAFBit(0);
}

static bool IsBlacklisted(uint32_t f)
{
    // Session blacklist removes all copies from the member bitmap; the
    // Spectrum history blacklist remains independent and unchanged.
    if (vfoScan)
        return false;
    uint32_t qf = f;
    for (uint16_t i = 0; i < HISTORY_SIZE; i++) {
        if (GetHistoryFreq(i) == qf && history[i].HBlacklisted) {
            return true;
        }
    }
    return false;
}

static void Blacklist()
{
    if (lastReceivingFreq == 0)
        return;
    Skip();
    uint32_t qf = lastReceivingFreq;
    for (uint16_t i = 0; i < HISTORY_SIZE; i++) {
        if (GetHistoryFreq(i) == qf) {
            history[i].HBlacklisted = true;
            historyListIndex = i;
            return;
        }
    }
    if (indexFs >= HISTORY_SIZE)
        indexFs = HISTORY_SIZE - 1;
    SetHistoryFreq(indexFs, lastReceivingFreq);
    history[indexFs].HBlacklisted = true;
    history[indexFs].HTimeS = history[indexFs].record = 0;
    history[indexFs].code = 0;
    historyListIndex = indexFs;
    ++indexFs;
}

// ============================================================
// SECTION: Display / rendering helpers
// ============================================================

#ifdef ENABLE_PERSIST
static uint8_t iSqrt(uint16_t n)
{
    if (n == 0)
        return 0;
    uint16_t x = n;
    uint16_t y = (x + 1) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return (uint8_t)x;
}

uint8_t Rssi2PX(uint16_t rssi, uint32_t frequency, uint8_t pxMin, uint8_t pxMax)
{
    const int DB_MIN = settings.dbMin << 1;
    const int DB_MAX = settings.dbMax << 1;
    const int DB_RANGE = DB_MAX - DB_MIN;
    const uint8_t PX_RANGE = pxMax - pxMin;
    int dbm = clamp((Rssi2DBm(rssi) + dBmCorrTable[FREQUENCY_GetBand(frequency)]) * 2, DB_MIN, DB_MAX);
    uint8_t linear = (uint8_t)(((dbm - DB_MIN) * PX_RANGE + DB_RANGE / 2) / DB_RANGE);
    uint8_t compressed = iSqrt((uint16_t)linear * PX_RANGE);
    return ((uint16_t)linear + compressed) / 2 + pxMin;
}

#else

static uint16_t Rssi2PX(uint16_t rssi, uint32_t frequency, uint16_t pxMin, uint16_t pxMax)
{
    const int16_t DB_MIN = settings.dbMin << 1;
    const int16_t DB_MAX = settings.dbMax << 1;
    const int16_t DB_RANGE = DB_MAX - DB_MIN;
    const int16_t PX_RANGE = pxMax - pxMin;
    int dbm = clamp((Rssi2DBm(rssi) + dBmCorrTable[FREQUENCY_GetBand(frequency)]) * 2, DB_MIN, DB_MAX);
    return ((dbm - DB_MIN) * PX_RANGE + DB_RANGE / 2) / DB_RANGE + pxMin;
}


#endif


// A common reference for Spectrum/Sentinel, independent of the sweep position.
static uint32_t ScanReferenceParameters(void)
{
    if (vfoScan)
        return UINT32_MAX;
    if (Spectrum_state == CHANNEL_MODE)
        return listReferenceParameters;
    if (Spectrum_state == SCAN_BAND_MODE)
        for (uint8_t i = 0; i < bandCount; i++)
            if (settings.bandEnabled[i])
                return BParams[i].modulationType | ((uint32_t)BParams[i].scanStep << 8) |
                       ((uint32_t)BParams[i].bandwidth << 16);
    return UINT32_MAX;
}
static uint32_t ScanDisplayParameters(void)
{
    if (isListening) {
        if (Spectrum_state == CHANNEL_MODE && isKnownChannel)
            return ReadScanParameters(gChannel);
        if (Spectrum_state == SCAN_BAND_MODE && bl < bandCount)
            return BParams[bl].modulationType | ((uint32_t)BParams[bl].scanStep << 8) |
                   ((uint32_t)BParams[bl].bandwidth << 16);
    }
    return ScanReferenceParameters();
}
static uint8_t ScanListeningBandwidth(uint8_t mode)
{
    const uint32_t p = ScanDisplayParameters();
    if (vfoScan || p == UINT32_MAX || (scanBwOverrides & (mode == MODULATION_AM ? 2 : 1)))
        return mode == MODULATION_AM ? listenBwAM : settings.listenBw;
    if (!(p & (1u << 16)))
        return 0;
    return mode == MODULATION_AM ? 1
                                 : (gSetting_set_nfm ? BK4819_FILTER_BW_NARROWER : BK4819_FILTER_BW_NARROW);
}
static void ResetScanParameterOverrides(void)
{
    scanBwOverrides = vfoScan ? 0 : scanMenuBandwidth;
    scanStepOverride = false;
    gForceModulation = !vfoScan && scanMenuModulation < MODULATION_UKNOWN;
    const uint32_t p = ScanReferenceParameters();
    if (gForceModulation)
        settings.modulationType = scanMenuModulation;
    else if (p != UINT32_MAX)
        settings.modulationType = (uint8_t)p;
    if (p != UINT32_MAX)
        settings.scanStepIndex = (p >> 8) & 255u;
}
static uint32_t SpectrumStatusParameters(void)
{
    uint8_t modulation = settings.modulationType;
    STEP_Setting_t step = settings.scanStepIndex;
    if (last_ptt_state && PttEmission == 0) {
        modulation = gTxVfo->Modulation;
        step = gTxVfo->STEP_SETTING;
    } else {
        const uint32_t p = ScanDisplayParameters();
        if (p != UINT32_MAX) {
            if (!gForceModulation)
                modulation = (uint8_t)p;
            if (!scanStepOverride)
                step = (p >> 8) & 255u;
        }
    }
    if (modulation >= MODULATION_UKNOWN)
        modulation = MODULATION_FM;
    if (step >= STEP_N_ELEM)
        step = STEP_12_5kHz;
    return modulation | ((uint32_t)step << 8) | ((uint32_t)ScanListeningBandwidth(modulation) << 16);
}

static __attribute__((noinline)) void DrawStatus(void)
{
    const char *role = PttEmission == 2 ? "LASTRX " : "CHANNEL ";
    const uint32_t parameters = SpectrumStatusParameters();
    const uint8_t mode = (uint8_t)parameters;
    const char *bw = mode == MODULATION_AM ? (((parameters >> 16) & 255u) ? "NARR" : "WIDE")
                                           : bwShort[(parameters >> 16) & 255u];
    char stepText[6];
    FP_CopyString(stepText, scanStepNames[(parameters >> 8) & 255u], sizeof stepText);
    int pos = sprintf(String, currentState == RANGE_EDIT ? "%s%s " : "%s%s %s %s ", role, bw,
                      gModulationStr[mode], stepText);
    if (SpectrumMonitor)
        strcpy(&String[pos], SpectrumMonitor == 1 ? "FL " : "MO ");
    else if (vfoScan || !sentinelView)
        sprintf(&String[pos], "%d ", ScanThreshold());
    else if (settings.rssiTriggerLevelUp != 50)
        sprintf(&String[pos], "DS%d ", settings.rssiTriggerLevelUp);
    else
        String[pos] = 0;

    GUI_DisplaySmallest(String, 1, 1, true, true);
    BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryCheckCounter++ % 4], &gBatteryCurrent);

    uint16_t voltage =
        (gBatteryVoltages[0] + gBatteryVoltages[1] + gBatteryVoltages[2] + gBatteryVoltages[3]) / 4 * 760 /
        gBatteryCalibration[3];

    unsigned perc = BATTERY_VoltsToPercent(voltage);
    sprintf(String, "%d%%", perc);
    GUI_DisplaySmallest(String, 128, 1, true, true);
    for (unsigned x = 0; x < sizeof(gStatusLine); ++x)
        gStatusLine[x] ^= 0x7F;
}

// ------------------ Frequency string ------------------


// Scanlist footer: retain SL's anchor and the old CH right-hand margin.
// Allocate the remaining space between CH and speed from their actual lengths.
static bool ScanListVisible(unsigned index);
static unsigned ScanSelectionCounts(unsigned *total)
{
    if (Spectrum_state == SCAN_BAND_MODE) {
        *total = bandCount;
        return CountActiveBands();
    }
    unsigned selected = 0;
    *total = 0;
    for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
        if (ScanListVisible(i)) {
            ++*total;
            if (settings.scanListEnabled[i])
                ++selected;
        }
    return selected;
}
static void DrawSelectionFooter(uint8_t selectedCount, uint8_t total, bool bands)
{
    char lists[12], channels[12], speed[10], frequency[18];
    if (total && selectedCount == total)
        snprintf(lists, sizeof(lists), "%s: ALL", bands ? "BAND" : "LIST");
    else
        snprintf(lists, sizeof(lists), "%s:%u/%u", bands ? "BAND" : "LIST", selectedCount, total);
    snprintf(channels, sizeof(channels), "CH:%u", scanChannelsCount);
#ifdef ENABLE_BENCH
    snprintf(speed, sizeof(speed), FastSpeed ? "F%u" : "%u", benchRatePerSec);
#else
    speed[0] = 0;
#endif
    const uint32_t f = last_ptt_state ? gTxVfo->freq_config_TX.Frequency : lastReceivingFreq;
    if (f >= FMIN && f <= FMAX)
        UI_FormatFrequency(frequency, f);
    else
        strcpy(frequency, "---.-----");

    const unsigned listWidth = strlen(lists) * 4 - 1;
    const unsigned channelWidth = strlen(channels) * 4 - 1;
    const unsigned speedWidth = speed[0] ? strlen(speed) * 4 - 1 : 0;
    const unsigned frequencyX = 126 - strlen(frequency) * 4;
    const int available = (int)frequencyX - 2 - listWidth - channelWidth - speedWidth;
    const unsigned gap = available > 0 ? available / 3 : 1;
    const unsigned channelX = 2 + listWidth + gap;
    const unsigned speedX = channelX + channelWidth + gap;
    GUI_DisplaySmallest(lists, 2, Bottom_print, false, true);
    if (!bands)
        GUI_DisplaySmallest(channels, channelX, Bottom_print, false, true);
    GUI_DisplaySmallest(speed, bands ? (128 - speedWidth) / 2 : speedX, Bottom_print, false, true);
    GUI_DisplaySmallest(frequency, 128, Bottom_print, false, true);
}

static void DrawNums()
{
    if (Spectrum_state != SCAN_RANGE_MODE) {
        unsigned total;
        const bool bands = Spectrum_state == SCAN_BAND_MODE;
        const unsigned selected = ScanSelectionCounts(&total);
        DrawSelectionFooter(selected, total, bands);
    } else {
        char text[18];
        UI_FormatFrequency(text, SpectrumRangeStart);
        GUI_DisplaySmallest(text, 2, Bottom_print, false, true);
        UI_FormatFrequency(text, SpectrumRangeStop);
        GUI_DisplaySmallest(text, 128, Bottom_print, false, true);
#ifdef ENABLE_BENCH
        snprintf(text, sizeof(text), FastSpeed ? "F%u" : "%u", benchRatePerSec);
        GUI_DisplaySmallest(text, (128 - (strlen(text) * 4 - 1)) / 2, Bottom_print, false, true);
#endif
    }
}


static void BlitFullScreen(void)
{
    if (currentState == SPECTRUM && !historyListActive && isListening && !rxDisplayPending && !osdPopupTimer)
        return;
    if (isListening && spectrumElapsedCount > 200)
        return; //No refresh in low noise mode
    ST7565_BlitFullScreen();
}

static void BlitStatusLine(void)
{
    if (currentState == SPECTRUM && !historyListActive && isListening && !rxDisplayPending && !osdPopupTimer)
        return;
    if (isListening && spectrumElapsedCount > 200)
        return; //No refresh in low noise mode
    ST7565_BlitStatusLine();
}

// SENTINEL presentation uses the existing scanner, history and TX
// selection. No second scan engine or extra signal-history buffer.
static void SentinelProgress(uint8_t left, uint8_t right)
{
    uint32_t total = GetStepsCount(), index = scanInfo.i;
    if (Spectrum_state != CHANNEL_MODE)
        ++total;
    if (Spectrum_state == SCAN_BAND_MODE) {
        total = 0;
        for (uint8_t i = 0; i < bandCount; ++i) {
            const uint32_t step = scanStepValues[BParams[i].scanStep];
            if (!step || !settings.bandEnabled[i])
                continue;
            const uint32_t count = (BParams[i].Stopfrequency - BParams[i].Startfrequency) / step + 1;
            if (i < bl)
                index += count;
            total += count;
        }
    }
    UI_DrawScanGauge(3, left, right, index, total);
}

// The shared meter/gauge occupies y20..26; RX starts at y30.
// Preserve adjacent frequency and RX pixels during live meter updates.
static NATIVE_CODE(scan) uint8_t DrawSentinelMeterNative(uint8_t left, uint8_t right)
{
    uint8_t saved[128];
    memcpy(saved, gFrameBuffer[3], sizeof saved);
    memset(gFrameBuffer[3], 0, 128);
#ifdef ENABLE_AUDIO_BAR
    if (last_ptt_state)
        UI_DrawTxAudioBar(3);
    else
#endif
        if (isListening)
        UI_DrawRxMeter(Rssi2DBm(scanInfo.rssi) + dBmCorrTable[FREQUENCY_GetBand(peak.f)], 3);
    else
        SentinelProgress(left, right);
    uint8_t changed = 0;
    for (unsigned x = 0; x < 128; ++x) {
        const uint8_t value = gFrameBuffer[3][x];
        const uint8_t upper = (gFrameBuffer[2][x] & 0x0fu) | (value << 4);
        const uint8_t lower = (saved[x] & 0xf8u) | (value >> 4);
        if (upper != gFrameBuffer[2][x])
            changed |= 1;
        if (lower != saved[x])
            changed |= 2;
        gFrameBuffer[2][x] = upper;
        gFrameBuffer[3][x] = lower;
    }
    return changed;
}
static uint8_t DrawSentinelMeter(uint8_t left, uint8_t right)
{
    if (!APP_NativeBegin(NATIVE_SCAN))
        return 0;
    uint8_t result = DrawSentinelMeterNative(left, right);
    APP_NativeEnd();
    return result;
}


static const char *EmptyScanText(void)
{
    if (vfoScan || historyListActive || SpectrumMonitor || !scanEmpty)
        return NULL;
    return Spectrum_state == CHANNEL_MODE ? "NO CH / LIST" : "NO BAND";
}

static void DrawSentinelFooter(void)
{
    char left[18], speed[10], middle[6];
#ifdef ENABLE_BENCH
    snprintf(speed, sizeof speed, FastSpeed ? "F%u" : "%u", benchRatePerSec);
#else
    speed[0] = 0;
#endif
    const unsigned speedWidth = UI_CompactTextWidth(speed);
    unsigned selected = 0, total = 0;
    const bool bands = Spectrum_state == SCAN_BAND_MODE;
    if (Spectrum_state != SCAN_RANGE_MODE)
        selected = ScanSelectionCounts(&total);
    if (Spectrum_state == SCAN_RANGE_MODE)
        strcpy(left, "RANGE");
    else if (!bands && scanNoList)
        strcpy(left, "NO LIST");
    else if (total && selected == total)
        snprintf(left, sizeof left, "%s ALL", bands ? "BAND" : "LIST");
    else
        snprintf(left, sizeof left, "%s %02u/%02u", bands ? "BAND" : "LIST", selected, total);
    UI_DrawCompactText5x5(left, 2, 49);
    UI_DrawCompactText5x5(speed, 126 - speedWidth, 49);
    const unsigned seconds = sentinelIdleTicks / 100u;
    snprintf(middle, sizeof middle, "%02u:%02u", seconds / 60u, seconds % 60u);
    const unsigned lw = 2 + UI_CompactTextWidth(left), rw = 126 - speedWidth;
    UI_DrawCompactText5x5(middle, lw + (rw - lw - UI_CompactTextWidth(middle)) / 2, 49);
}

// One identity row for RX and the retained VFO TX destination.
static void DrawSentinelIdentity(uint16_t channel, uint32_t frequency, const char *label, uint8_t y)
{
    UI_PrintSmallAt(label, 2, 0, y, false);
    char text[12];
    if (IS_MR_CHANNEL(channel))
        SETTINGS_FetchChannelName(text, channel);
    else if (frequency >= FMIN && frequency <= FMAX)
        UI_FormatFrequency(text, frequency);
    else
        text[0] = 0;
    UI_PrintSmallAt(text, label[3] ? 30 : 21, label[3] ? 98 : 101, y, true);
    if (IS_MR_CHANNEL(channel)) {
        const ChannelAttributes_t *att = MR_GetChannelAttributes(channel);
        if (att)
            UI_DrawScanListBubble(att->scanlist, y);
    }
}

static void DrawSentinel(char *frequency, const char *name)
{
    (void)name;
    const uint8_t length = strlen(frequency), split = length - 2;
    // Same 13-pixel digits, three-pixel dot and two small trailing decimals.
    const uint8_t width = (split - 1) * 13 + 3 + 13;
    const uint8_t left = (126 - width) / 2;
    UI_PrintStringSmallBold(frequency + split, left + width - 13, 0, 1);
    frequency[split] = 0;
    UI_DisplayFrequency(frequency, left, 0, true);
    // Move both the large digits and their small trailing decimals down 2 px.
    for (unsigned x = 0; x < 128; ++x) {
        gFrameBuffer[2][x] = gFrameBuffer[1][x] >> 6;
        gFrameBuffer[1][x] = (gFrameBuffer[1][x] << 2) | (gFrameBuffer[0][x] >> 6);
        gFrameBuffer[0][x] <<= 2;
    }
    uint16_t receptions = 0;
    const uint32_t f = isListening ? peak.f : scanInfo.f;
    for (unsigned i = 0; i < indexFs; ++i)
        if (GetHistoryFreq(i) == f) {
            receptions = history[i].receptions;
            break;
        }
    char text[18];
    snprintf(text, sizeof text, "%02u", receptions);
    UI_DrawText5x5(text, 2, 1, 3);
    const char *modulation = gModulationStr[(uint8_t)SpectrumStatusParameters()];
    if (!last_ptt_state && StringCode[0]) {
        modulation = StringCode + (StringCode[0] == 'D');
        UI_DrawCompactText5x5(modulation, 126 - UI_CompactTextWidth(modulation), 1);
    } else
        UI_DrawText5x5(modulation, 126 - strlen(modulation) * 6, 1, 3);
    DrawSentinelMeter(left + 2, left + width - 1);
    DrawSentinelIdentity(lastNamedChannel, lastNamedFrequency, PttEmission == 0 ? "RX:" : "RXTX", 30);
    if (PttEmission == 0) {
        const uint32_t txFrequency = channelTxVfo.FrequencyReverse ? channelTxVfo.freq_config_RX.Frequency
                                                                   : channelTxVfo.freq_config_TX.Frequency;
        DrawSentinelIdentity(channelTxScreen, txFrequency, "TX:", 39);
    } else if (IS_MR_CHANNEL(lastNamedChannel)) {
        UI_FormatFrequency(text, lastNamedFrequency);
        UI_PrintSmallAt(text, 21, 101, 39, false);
        snprintf(text, sizeof text, "%03u", lastNamedChannel + 1u);
        const unsigned w = UI_CompactTextWidth(text);
        UI_DrawCompactText5x5(text, 104 + (21 - (int)w) / 2, 40);
    } else if (Spectrum_state == SCAN_RANGE_MODE) {
        char range[26];
        UI_FormatFrequency(range, SpectrumRangeStart);
        strcat(range, " - ");
        UI_FormatFrequency(range + strlen(range), SpectrumRangeStop);
        UI_DrawCompactText5x5(range, (128 - UI_CompactTextWidth(range)) / 2, 40);
    }
    DrawSentinelFooter();
}

// Hold the last RX between signals; reuse the scanner's RSSI, without RF reads.
static NATIVE_CODE(scan) bool DrawSpectrumDbmNative(uint32_t frequency)
{
    const int16_t previous = spectrumRxDbm;
    if (isListening && spectrumRxDbmFresh) {
        spectrumRxDbm = Rssi2DBm(scanInfo.rssi) + dBmCorrTable[FREQUENCY_GetBand(frequency)];
        spectrumRxDbmFresh = false;
    }
    {
        char number[8];
        if (spectrumRxDbm == INT16_MIN)
            strcpy(number, "---");
        else
            sprintf(number, "%d", spectrumRxDbm);
        for (unsigned x = 0; x < 78; ++x)
            gFrameBuffer[1][x] &= 7u;
        // Fixed columns: neither the frequency nor the dBm width moves the row.
        UI_DrawText5x5(number, 2, 11, strlen(number));
        UI_DrawText5x5("dbm", 28, 11, 3);
        SpectrumDbm(spectrumRxDbm, 52, gFrameBuffer);
    }
    return spectrumRxDbm != previous;
}
static bool DrawSpectrumDbm(uint32_t frequency)
{
    if (!APP_NativeBegin(NATIVE_SCAN))
        return 0;
    bool result = DrawSpectrumDbmNative(frequency);
    APP_NativeEnd();
    return result;
}


static NATIVE_CODE(scan) void DrawFNative(uint32_t f)
{
    if (vfoScan)
        return;
    static uint32_t fprev;
    if ((f == 0) || f < FMIN || f > FMAX)
        f = fprev;
    else
        fprev = f;
    char freqStr[18];

    if (!last_ptt_state) {
        UI_FormatFrequency(freqStr, f);
    } else {
        UI_FormatFrequency(freqStr, gTxVfo->freq_config_TX.Frequency);
    }
    const char *line2 = channelName;

    if (!sentinelView)
        DrawNums();
    if (sentinelView) {
        DrawSentinel(freqStr, line2);
        return;
    }

    SpectrumHeader(freqStr, line2, gFrameBuffer);
    if (IS_MR_CHANNEL(lastNamedChannel)) {
        const ChannelAttributes_t *att = MR_GetChannelAttributes(lastNamedChannel);
        SpectrumMemoryIdentity(lastNamedChannel, att ? att->scanlist : 0, gFrameBuffer);
    }
    // Common dBm/S row for List, Band and Range; decoded codes stay in Sentinel.
    DrawSpectrumDbm(f);
}
static void DrawF(uint32_t f)
{
    if (APP_NativeBegin(NATIVE_SCAN)) {
        DrawFNative(f);
        APP_NativeEnd();
    }
}


static uint32_t ReadScanParameters(uint16_t channel)
{
    uint8_t data[8];
    PY25Q16_ReadBuffer((uint32_t)ADRESS_CHANNELS + (uint32_t)channel * 16U + 8U, data, sizeof(data));
    const uint8_t mode = data[3] >> 4;
    return (mode < MODULATION_UKNOWN ? mode : MODULATION_FM) |
           ((uint32_t)(data[6] < STEP_N_ELEM ? data[6] : STEP_12_5kHz) << 8) |
           ((uint32_t)((data[4] >> 1) & 1u) << 16);
}


static void NextScanStep()
{
    spectrumElapsedCount = 0;
#ifdef ENABLE_BENCH
    benchLapDone = false;
#endif
    if (scanEmpty)
        return;
    if (vfoScan && vfoRangeSession) {
        if (!scanInfo.i) {
            scanInfo.i = 1;
            return;
        }
        uint32_t f = scanInfo.f + scanInfo.scanStep;
        if (f >= BX4819_band1.upper && f < BX4819_band2.lower)
            f += ((BX4819_band2.lower - f + scanInfo.scanStep - 1) / scanInfo.scanStep) * scanInfo.scanStep;
        if (f > FMAX)
            f = RangeStart;
        scanInfo.f = f;
        return;
    }
    static uint32_t StartF;
    if (Spectrum_state == CHANNEL_MODE) {
        if (scanChannelsCount == 0)
            return;
#ifdef ENABLE_BENCH
        uint16_t prevI = scanInfo.i;
#endif
        if (++scanInfo.i >= scanChannelsCount) {
            scanInfo.i = 0;
            if (!vfoScan)
                ActivityFinishSweep(&activity, gGlobalSysTickCounter);
        }
#ifdef ENABLE_BENCH
        if (scanInfo.i < prevI)
            benchLapDone = true;
#endif

        ScanChannel = GetNextChannelInSelectedScanLists(ScanChannel, 1);
        statusScanChannel = ScanChannel;
        activityLists.current = nextDisplayList;
        ChannelInfo_t info;
        info = FetchChannelFrequency(ScanChannel);
        activityMemoryFrequency = info.frequency;
        scanInfo.f = info.frequency;
        return;
    }
    // FREQUENCY / SCAN_RANGE / SCAN_BAND
#ifdef ENABLE_BENCH
    uint16_t prevI = scanInfo.i;
    uint16_t steps = GetStepsCount();
#endif
    if (scanInfo.i == 0) {
        StartF = SpectrumRangeStart;
        scanInfo.f = StartF;
    } else {
        scanInfo.f += jumpSizes[settings.scanStepIndex];
        if (scanInfo.f >= SpectrumRangeStop) {
            StartF += scanInfo.scanStep;
            scanInfo.f = StartF;
        }
    }
    scanInfo.i++;
#ifdef ENABLE_BENCH
    if (scanInfo.i > steps) {
        if (!vfoScan)
            ActivityFinishSweep(&activity, gGlobalSysTickCounter);
        scanInfo.i = 0;
        ScanChannel = 0;
        newScanStart = true;
        benchLapDone = true; // pełna pętla zakresu/pasma/freq
    } else if (scanInfo.i < prevI) {
        benchLapDone = true;
    }
#else
    if (scanInfo.i > GetStepsCount()) {
        if (!vfoScan)
            ActivityFinishSweep(&activity, gGlobalSysTickCounter);
        scanInfo.i = 0;
        newScanStart = true;
    }
#endif
}

void NextAppMode(void)
{

    switch (Spectrum_state) {
        case CHANNEL_MODE:
            Spectrum_state = SCAN_BAND_MODE;
            break;
        case SCAN_BAND_MODE:
            Spectrum_state = SCAN_RANGE_MODE;
            break;
        default:
            Spectrum_state = CHANNEL_MODE;
            break;
    }
    gSpectrumChangeRequested = true;
    isInitialized = false;
    spectrumElapsedCount = 0;
    WaitSpectrum = 0;
    gIsPeak = false;
    SPECTRUM_PAUSED = false;
    SpectrumPauseCount = 0;
    newScanStart = true;
    ToggleRX(false);
    ShowOSDPopup(sentinelView                       ? (Spectrum_state == CHANNEL_MODE     ? "SENTINEL LIST"
                                                       : Spectrum_state == SCAN_BAND_MODE ? "SENTINEL BAND"
                                                                                          : "SENTINEL RANGE")
                 : Spectrum_state == CHANNEL_MODE   ? "SPECTRE LIST"
                 : Spectrum_state == SCAN_BAND_MODE ? "SPECTRE BAND"
                                                    : "SPECTRE RANGE");
}


static void SetTrigger50()
{
    char triggerText[32];
    if (settings.rssiTriggerLevelUp == 50) {
        sprintf(triggerText, "DYN SQL: OFF");
        gMonitorScan = 0;
    } else {
        sprintf(triggerText, "DYN SQL: %d", settings.rssiTriggerLevelUp);
    }
    ShowOSDPopup(triggerText);
    if (true)
        Skip();
}
static const uint8_t durations[] = {0, 20, 40, 60};


uint8_t GetBandIndexForRow(uint8_t row)
{
    uint8_t pos = 0;
    for (uint8_t i = 0; i < bandCount; i++) {
        if (settings.bandEnabled[i]) {
            if (pos == row)
                return i;
            pos++;
        }
    }
    for (uint8_t i = 0; i < bandCount; i++) {
        if (!settings.bandEnabled[i]) {
            if (pos == row)
                return i;
            pos++;
        }
    }
    return 0;
}

// ============================================================
// SECTION: Per-state keyboard handlers
// ============================================================

static void SelectionMove(uint8_t *selected, int *scroll, uint8_t count, bool down)
{
    if (down) {
        if (*selected < count - 1) {
            ++*selected;
            if (*selected >= *scroll + MAX_VISIBLE_LINES)
                *scroll = *selected - MAX_VISIBLE_LINES + 1;
        } else
            *selected = 0;
    } else {
        if (*selected > 0) {
            --*selected;
            if (*selected < *scroll)
                *scroll = *selected;
        } else
            *selected = count - 1;
    }
}
/* --- BAND_LIST_SELECT: navigate and toggle band enable flags --- */
static void StartScanSelection(void);
static void SelectionMenuShort(void);
static void HandleKeyBandList(uint8_t key)
{
    if (!bandCount) {
        if (key == KEY_EXIT || key == KEY_MENU)
            StartScanSelection();
        return;
    }
    switch (key) {
        case KEY_UP:
        case KEY_DOWN:
            SelectionMove(&bandListSelectedIndex, &bandListScrollOffset, bandCount, key == KEY_DOWN);
            break;
        case KEY_4:
            scanOptionsChanged = true;
            if (bandListSelectedIndex < bandCount) {
                const uint8_t bandIndex = GetBandIndexForRow(bandListSelectedIndex);
                settings.bandEnabled[bandIndex] = !settings.bandEnabled[bandIndex];
                nextBandToScanIndex = bandIndex;
                const bool enabled = settings.bandEnabled[bandIndex];
                bandListSelectedIndex = enabled ? 0 : CountActiveBands();
                for (uint8_t i = 0; i < bandIndex; ++i)
                    bandListSelectedIndex += settings.bandEnabled[i] == enabled;
                if (bandListSelectedIndex < bandListScrollOffset)
                    bandListScrollOffset = bandListSelectedIndex;
                if (bandListSelectedIndex >= bandListScrollOffset + MAX_VISIBLE_LINES)
                    bandListScrollOffset = bandListSelectedIndex - MAX_VISIBLE_LINES + 1;
            }
            break;
        case KEY_MENU:
            SelectionMenuShort();
            break;
        case KEY_STAR:
            scanOptionsChanged = true;
            {
                const bool all = CountActiveBands() == bandCount;
                for (unsigned i = 0; i < bandCount; ++i)
                    settings.bandEnabled[i] = !all;
            }
            break;
        case KEY_EXIT:
            StartScanSelection();
            break;
        default:
            break;
    }
}

/* --- SCANLIST_SELECT: navigate and toggle scan-list enable flags --- */
// Cycle only named, populated lists; keep all editor slots available.
static bool ScanListVisible(unsigned index)
{
    const char *name = gListName[index];
    unsigned c = 0;
    while (c < sizeof(gListName[0]) && name[c] == ' ')
        ++c;
    return gScanListHasChannels[index] || (c < sizeof(gListName[0]) && name[c] && (uint8_t)name[c] != 0xFF);
}
static uint8_t ScanListRowSlot(uint8_t row)
{
    for (uint8_t pass = 0; pass < 2; ++pass)
        for (uint8_t i = 0; i < MR_CHANNELS_LIST; ++i)
            if (ScanListVisible(i) && settings.scanListEnabled[i] == !pass && !row--)
                return i;
    return UINT8_MAX;
}
static void MoveVisibleScanList(bool forward, bool activate)
{
    uint8_t count = 0, row = 0, slot;
    while (count < MR_CHANNELS_LIST && (slot = ScanListRowSlot(count)) != UINT8_MAX) {
        if (slot == scanListSelectedIndex)
            row = count;
        ++count;
    }
    if (count) {
        row = forward ? (row + 1) % count : (row + count - 1) % count;
        scanListSelectedIndex = ScanListRowSlot(row);
        if (activate)
            ToggleScanList(scanListSelectedIndex, 1);
    }
}
// Resolve navigation from the active scan, never from an inventory-page cursor.
// The list/band inventory has its own row order and may reset that cursor.
static void CycleScanSelection(bool forward)
{
    if (Spectrum_state == CHANNEL_MODE) {
        unsigned first = MR_CHANNELS_LIST, count = 0;
        for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
            if (settings.scanListEnabled[i]) {
                if (!count++)
                    first = i;
            }
        if (count) {
            scanListSelectedIndex = first;
            if (count > 1 && IS_MR_CHANNEL(statusScanChannel)) {
                const ChannelAttributes_t *att = MR_GetChannelAttributes(statusScanChannel);
                if (att && att->scanlist && att->scanlist <= MR_CHANNELS_LIST &&
                    settings.scanListEnabled[att->scanlist - 1])
                    scanListSelectedIndex = att->scanlist - 1;
            }
        }
        for (unsigned n = 0; n < MR_CHANNELS_LIST; ++n) {
            scanListSelectedIndex =
                (scanListSelectedIndex + (forward ? 1 : MR_CHANNELS_LIST - 1)) % MR_CHANNELS_LIST;
            if (ScanListVisible(scanListSelectedIndex)) {
                ToggleScanList(scanListSelectedIndex, 1);
                break;
            }
        }
        SetState(SPECTRUM);
        ResetModifiers();
        ShowScanNamePopup(gListName[scanListSelectedIndex]);
    } else if (bandCount) {
        unsigned index = bl;
        if (index >= bandCount || !settings.bandEnabled[index])
            for (index = 0; index + 1 < bandCount && !settings.bandEnabled[index]; ++index) {
            }
        index = (index + (forward ? 1 : bandCount - 1)) % bandCount;
        bandListSelectedIndex = index;
        bandListScrollOffset = index >= MAX_VISIBLE_LINES ? index - MAX_VISIBLE_LINES + 1 : 0;
        memset(settings.bandEnabled, 0, sizeof(settings.bandEnabled));
        settings.bandEnabled[index] = true;
        nextBandToScanIndex = index;
        RelaunchScan();
        ShowScanNamePopup(BParams[index].BandName);
    }
}
static void StartScanSelection(void)
{


    SpectrumMonitor = 0;
    SPECTRUM_PAUSED = false;
    SpectrumPauseCount = 0;
    WaitSpectrum = 0;
    ResetModifiers();
    SetState(SPECTRUM);
    SaveSettings(false);
    newScanStart = false;
}


static void HandleKeyScanList(uint8_t key)
{
    switch (key) {
        case KEY_UP:
        case KEY_DOWN:
            MoveVisibleScanList(key == KEY_DOWN, false);
            break;
        case KEY_4:
            scanOptionsChanged = true;
            if (ScanListVisible(scanListSelectedIndex))
                ToggleScanList(scanListSelectedIndex, 0);
            break;
        case KEY_MENU:
            SelectionMenuShort();
            break;
        case KEY_STAR:
            scanOptionsChanged = true;
            {
                bool all = true;
                for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
                    if (ScanListVisible(i) && !settings.scanListEnabled[i])
                        all = false;
                for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
                    settings.scanListEnabled[i] = !all && ScanListVisible(i);
            }
            break;
        case KEY_EXIT:
            StartScanSelection();
            break;
    }
}


static void ExecuteHistoryMenuAction(uint16_t index)
{
    switch (index) {
        case 0:
            ClearHistory(0);
            ShowOSDPopup("HISTORY CLEARED");
            break;
        case 1:
            ClearHistory(2);
            ShowOSDPopup("BLACKLIST CLEARED");
            break;
        case 2:
            ClearHistory(1);
            ShowOSDPopup("NON-BL CLEARED");
            break;
        case 3:
            SaveHistory();
            break;
        case 4:
            SaveAllHistoryToFreeChannels();
            break;
        default:
            break;
    }
    SetState(SPECTRUM);
}

static void HandleKeyHistoryMenu(uint8_t key)
{
    const uint16_t maxRows = 5;
    switch (key) {
        case KEY_UP:
            historyMenuSelectedIndex =
                (historyMenuSelectedIndex == 0) ? (maxRows - 1) : (historyMenuSelectedIndex - 1);
            break;
        case KEY_DOWN:
            historyMenuSelectedIndex = (historyMenuSelectedIndex + 1) % maxRows;
            break;
        case KEY_MENU:
        case KEY_1:
            ExecuteHistoryMenuAction(historyMenuSelectedIndex);
            break;
        case KEY_EXIT:
            SetState(SPECTRUM);
            break;
        default:
            break;
    }
}

static NATIVE_CODE(lists) void RenderHistoryMenuSelectNative(void)
{
    static const char *const names[] = {"CLEAR HISTORY", "CLEAR BLACKLIST", "CLEAR NON BLACKLIST",
                                        "SAVE HISTORY", "SAVE HIST TO CH"};
    memset(gFrameBuffer, 0, sizeof gFrameBuffer);
    if (HistoryOwnsStatus())
        memset(gStatusLine, 0, sizeof gStatusLine);
    UI_PrintStringSmallBold("HISTORY", 0, 127, 0);
    const char *const *items = names;
    const uint8_t count = 5;
    // Keep the first five rows at the former six-row menu positions.
    for (uint8_t i = 0; i < count; ++i) {
        const char *text = items[i];
        const uint8_t y = 12 + i * 7;
        UI_DrawText5x5(text, 1, y, strlen(text));
        if (i == historyMenuSelectedIndex)
            for (uint8_t py = y - 1; py < y + 6; ++py)
                for (uint8_t x = 0; x < 128; ++x)
                    gFrameBuffer[py / 8][x] ^= 1u << (py % 8);
    }
}
static void RenderHistoryMenuSelect(void)
{
    if (APP_NativeBegin(NATIVE_LISTS)) {
        RenderHistoryMenuSelectNative();
        APP_NativeEnd();
    }
    if (HistoryOwnsStatus())
        ST7565_BlitStatusLine();
}


/* --- PARAMETERS_SELECT: navigate settings, edit values --- */
static void GetParametersRow(uint16_t index, ListRow *row);
static NATIVE_CODE(options) void ChangeParameter(uint8_t key)
{
    bool isKey3 = (key == KEY_3);
    uint16_t realIndex = SpectrumParameterAt(parametersSelectedIndex);
    ListRow before, after;
    GetParametersRow(parametersSelectedIndex, &before);
    switch (realIndex) {
        case PARAM_SPECTRUM_DELAY:
            if (isKey3) {
                if (SpectrumDelay < 65000)
                    SpectrumDelay += (SpectrumDelay < 10000) ? 1000 : 5000;
            } else if (SpectrumDelay >= 1000) {
                SpectrumDelay -= (SpectrumDelay <= 10000) ? 1000 : 5000;
            }
            break;
        case PARAM_MAX_LISTEN_TIME:
            if (isKey3) {
                if (++IndexMaxLT > LISTEN_STEP_COUNT)
                    IndexMaxLT = 0;
            } else {
                if (IndexMaxLT == 0)
                    IndexMaxLT = LISTEN_STEP_COUNT;
                else
                    IndexMaxLT--;
            }
            MaxListenTime = listenSteps[IndexMaxLT];
            break;
        case PARAM_LISTEN_BW_AM:
            listenBwAM = !ScanListeningBandwidth(MODULATION_AM);
            scanBwOverrides |= 2;
            scanMenuBandwidth = scanBwOverrides;
            break;
        case PARAM_LISTEN_BW:
        case PARAM_MODULATION:
            if (true) {
                if (realIndex == PARAM_LISTEN_BW)
                    ToggleListeningBW(isKey3 ? 0 : 1);
                else
                    ToggleModulation();
            }
            break;
        case PARAM_POWER_SAVE:
            if (isKey3) {
                if (++IndexPS > PS_STEP_COUNT)
                    IndexPS = 0;
            } else {
                if (IndexPS == 0)
                    IndexPS = PS_STEP_COUNT;
                else
                    IndexPS--;
            }
            SpectrumSleepMs = PS_Steps[IndexPS];
            break;
        case PARAM_NOISE_LEVEL_OFF:
            Noislvl_OFF = isKey3 ? (Noislvl_OFF >= 80 ? 0 : Noislvl_OFF + 1)
                                 : (Noislvl_OFF <= 0 ? 80 : Noislvl_OFF - 1);
            Noislvl_ON = Noislvl_OFF > NoiseHysteresis ? Noislvl_OFF - NoiseHysteresis : 1;
            break;
        case PARAM_OSD_POPUP:
            osdPopupIndex = isKey3 ? (osdPopupIndex >= 5 ? 0 : osdPopupIndex + 1)
                                   : (osdPopupIndex <= 0 ? 5 : osdPopupIndex - 1);
            osdPopupSetting = osdPopupTimes[osdPopupIndex];
            break;
        case PARAM_RECORD_TRIGGER:
            UOO_trigger = isKey3 ? (UOO_trigger >= 50 ? 0 : UOO_trigger + 1)
                                 : (UOO_trigger <= 0 ? 50 : UOO_trigger - 1);
            break;
        case PARAM_AUTO_KEYLOCK:
            AUTO_KEYLOCK = isKey3 ? (AUTO_KEYLOCK > 2 ? 0 : AUTO_KEYLOCK + 1)
                                  : (AUTO_KEYLOCK <= 0 ? 3 : AUTO_KEYLOCK - 1);
            gKeylockCountdown = durations[AUTO_KEYLOCK];
            break;
        case PARAM_SOUND_BOOST:
            SoundBoost = !SoundBoost;
            break;
        case PARAM_PTT_EMISSION:
            PttEmission ^= 2;
            break;
        case PARAM_PTT2_EMISSION:
            Ptt2Emission ^= 2;
            break;
        case PARAM_MONITOR_SCAN:
            gMonitorScan = !gMonitorScan;
            break;
        case PARAM_AUDIO_AM:
            gSetting_set_audio_am = isKey3 ? (gSetting_set_audio_am >= 2 ? 0 : gSetting_set_audio_am + 1)
                                           : (gSetting_set_audio_am == 0 ? 2 : gSetting_set_audio_am - 1);
            if (isListening)
                RADIO_SetModulation(settings.modulationType);
            else
                ApplyRXProfile(false);
            break;
    }
    GetParametersRow(parametersSelectedIndex, &after);
    if (strcmp(before.right, after.right))
        scanOptionsChanged = true;
}
static void HandleKeyParameters(uint8_t key)
{
    uint16_t maxRows = GetMaxVisualRows();
    if (parametersSelectedIndex >= maxRows)
        parametersSelectedIndex = maxRows - 1;
    switch (key) {
        case KEY_UP:
            parametersSelectedIndex = (parametersSelectedIndex - 1 + maxRows) % maxRows;
            break;
        case KEY_DOWN:
            parametersSelectedIndex = (parametersSelectedIndex + 1) % maxRows;
            break;
        case KEY_1:
        case KEY_3:
            if (APP_NativeBegin(NATIVE_OPTIONS)) {
                ChangeParameter(key);
                APP_NativeEnd();
            }
            break;
        case KEY_7: //Parameters
            SaveSettings(true);
            break;
        case KEY_EXIT: //Parameters
            if (scanOptionsChanged)
                SaveSettings(false);
            ResetModifiers();
            SetState(SPECTRUM);
            break;
        default:
            break;
    }
}

#include "app/range_editor.inc"
static const char ns_rf_0[] NATIVE_DATA(rf) = "S%2d+%2d";
static const char ns_rf_1[] NATIVE_DATA(rf) = "S%2d";
static const char ns_rf_2[] NATIVE_DATA(rf) = "%d dBm";
static const char ns_rf_3[] NATIVE_DATA(rf) = ">%-18s %6u";
static const char ns_rf_4[] NATIVE_DATA(rf) = " %-18s %6u";

/* --- SPECTRUM state: main spectrum view keys, including list entry shortcuts --- */
static void MoveScanRange(bool up)
{
    if (RangeStart < FMIN || RangeStop > FMAX || RangeStart >= RangeStop)
        return;
    const uint32_t width = RangeStop - RangeStart;
    const uint32_t room = up ? FMAX - RangeStop : RangeStart - FMIN;
    const uint32_t delta = room < width ? room : width;
    if (!delta)
        return;
    if (up) {
        RangeStart += delta;
        RangeStop += delta;
    } else {
        RangeStart -= delta;
        RangeStop -= delta;
    }
    RelaunchScan();
}

static void HistoryOpen(void)
{
    CompactHistory();
    historyListActive = true;
    historyListIndex = 0;
    historyScrollOffset = 0;
    lastHistoryScrollOffset = -1; // Force preload on initial history list activation
    prevSpectrumMonitor = SpectrumMonitor;
    historyModulation = settings.modulationType;
    gNextTimeslice_history = true;
}

// Both pages close together; the scanner gets its own modulation back.
static void HistoryClose(void)
{
    gHistoryScan = false;
    historyListActive = false;
    historyMessages = false;
#ifdef ENABLE_FALCON_MESSAGES
    msgOpen = false;
    msgAsk = MSG_ASK_NONE;
#endif
    historyBandwidth = UINT8_MAX;
    if (!gForceModulation)
        settings.modulationType = historyModulation;
    SpectrumMonitor = prevSpectrumMonitor;
}

// Stops the receiver, its audio and both LEDs for a silent history page.
static __attribute__((noinline)) void HistoryQuiet(void)
{
    SpectrumMonitor = 0;
    WaitSpectrum = 0;
    gIsPeak = false;
    ToggleRX(false);
    ToggleAudio(false);
    ToggleAFDAC(false);
    ToggleAFBit(false);
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
    BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
}

// Frequency Lock on the selected entry, with the entry's modulation and filter.
// A silent history only selects it.
static __attribute__((noinline)) void ListenHistoryEntry(void)
{
    if (HistorySilent())
        return;
    lastReceivingFreq = GetHistoryFreq(historyListIndex);
    historyBandwidth = UINT8_MAX;
    if (!gForceModulation) {
        const uint8_t profile = HistoryProfile(historyListIndex);
        settings.modulationType = profile & 3;
        historyBandwidth = profile >> 2;
    }
    SpectrumMonitor = 1;
    SetF(lastReceivingFreq);
    ApplyRXProfile(true);
    ToggleAFDAC(true);
    ToggleAFBit(true);
}

#ifdef ENABLE_FALCON_MESSAGES
// * switches between the history and MESSAGES pages. The MESSAGES page stops
// the scan and the receiver: no audio, no RX or scan LED until the history
// returns.
static __attribute__((noinline)) void HistoryTogglePage(void)
{
    historyMessages = !historyMessages;
    if (historyMessages) {
        msgIndex = msgScroll = 0;
        msgStep = 1;
        HistoryQuiet();
    } else
        RenderStatus(); // the scan status line comes back
}

// The message of the selected row (never a marker: the page skips them).
static __attribute__((noinline)) bool MessageSelected(MSG_Row_t *row)
{
    return msgIndex < msgCount && MSG_Fetch(msgIndex, row, 1, 0xFFFF, NULL, NULL) > msgIndex &&
           !row->marker;
}

// List: arrows select, M opens, 6 asks DELETE?, 6 long asks CLEAR ALL?. An
// open message: 6 asks DELETE?, EXIT returns. A popup: M confirms (then the
// history's Deleted bubble), EXIT cancels.
static __attribute__((noinline)) void HandleKeyMessages(uint8_t key)
{
    if (!APP_NativeBegin(NATIVE_MESSAGES))
        return;
    MSG_Row_t row;
    if (msgAsk) {
        if (key == KEY_MENU) {
            if (msgAsk == MSG_ASK_CLEAR) {
                MSG_Clear();
                msgIndex = msgScroll = 0;
            } else if (MessageSelected(&row))
                MSG_Delete(row.slot);
            msgOpen = false;
            ShowOSDPopup("Deleted");
        }
        if (key == KEY_MENU || key == KEY_EXIT)
            msgAsk = MSG_ASK_NONE;
    } else if (key == KEY_EXIT) {
        msgOpen = false;
    } else if (key == KEY_6) {
        if (MessageSelected(&row))
            msgAsk = MSG_ASK_DELETE;
    } else if (!msgOpen && key == KEY_MENU) {
        msgOpen = MessageSelected(&row);
    } else if (!msgOpen && msgCount && (key == KEY_UP || key == KEY_DOWN)) {
        msgStep = key == KEY_DOWN ? 1 : -1;
        if (key == KEY_DOWN)
            msgIndex = msgIndex + 1u >= msgCount ? 0 : msgIndex + 1u;
        else
            msgIndex = msgIndex ? msgIndex - 1u : msgCount - 1u;
    }
    APP_NativeEnd();
}
#endif

static __attribute__((noinline)) void HandleKeySpectrum(uint8_t key)
{
#ifdef ENABLE_FALCON_MESSAGES
    // The open message and the popup take every key but PTT; the list
    // leaves * (page) and EXIT (close) to the history.
    if (historyMessages && key != KEY_PTT &&
        (msgOpen || msgAsk || (key != KEY_STAR && key != KEY_EXIT))) {
        HandleKeyMessages(key);
        return;
    }
#endif
    switch (key) {
        case KEY_2:
            if (!historyListActive && !vfoScan) {
                Backlight_On = !Backlight_On;
                if (Backlight_On)
                    BACKLIGHT_TurnOn();
                else
                    BACKLIGHT_SetBrightness(0);
            }
            break;
        case KEY_5:
            if (!historyListActive)
                OpenScanDetails();
            break;
        case KEY_STAR:
        case KEY_F: {
#ifdef ENABLE_FALCON_MESSAGES
            if (historyListActive) {
                HistoryTogglePage();
                break;
            }
#endif
            const bool up = key == KEY_STAR;
            if (AbsoluteSQL()) {
                uint8_t *sql = ScanSQLSetting();
                *sql = clamp((int)*sql + (up ? 1 : -1), 0, 77);
                if (vfoRangeSession)
                    gRequestSaveSettings = true;
                scanOptionsChanged = true;
                rxDisplayPending = true;
                RenderStatus();
                Render();
            } else {
                const int level = settings.rssiTriggerLevelUp;
                const int step = up ? (level >= 20 ? 5 : 1) : (level <= 20 ? 1 : 5);
                settings.rssiTriggerLevelUp =
                    up ? (level >= 50 ? 0 : level + step) : (level <= 0 ? 50 : level - step);
                SPECTRUM_PAUSED = true;
                SetTrigger50();
            }
            break;
        }
        case KEY_3:
            if (!historyListActive) {
                if (!scanListOnly)
                    FastSpeed = !FastSpeed;
                DelayRssi = (FastSpeed) ? 800 : 1500;
            }
            break;
        case KEY_0: {
            ToggleModulation();
            char modText[32];
            sprintf(modText, "MOD: %s", gModulationStr[settings.modulationType]);
            ShowOSDPopup(modText);
            break;
        }
        case KEY_1:
            if (!historyListActive) {
                Skip();
                if (!vfoRangeSession)
                    ShowOSDPopup("SKIPPED");
            }
            break;
        case KEY_7:
            if (!historyListActive) {
                SaveSettings(true);
            }
            break;
        case KEY_UP:
        case KEY_DOWN: {
            if (SpectrumMonitor == 1 && !historyListActive)
                break;
            const bool forward = key == KEY_DOWN;
            if (historyListActive) {
                lastHistoryScrollOffset = -1;
                const uint16_t count = CountValidHistoryItems();
                if (!count)
                    return;
                if (forward) {
                    if (++historyListIndex >= count) {
                        historyListIndex = 0;
                        historyScrollOffset = 0;
                    }
                } else if (!historyListIndex) {
                    historyListIndex = count - 1;
                    historyScrollOffset = count > MAX_VISIBLE_LINES ? count - MAX_VISIBLE_LINES : 0;
                } else
                    --historyListIndex;
                if (historyListIndex < historyScrollOffset)
                    historyScrollOffset = historyListIndex;
                else if (historyListIndex >= historyScrollOffset + MAX_VISIBLE_LINES)
                    historyScrollOffset = historyListIndex - MAX_VISIBLE_LINES + 1;
                ListenHistoryEntry();
            } else if (Spectrum_state == SCAN_RANGE_MODE)
                MoveScanRange(forward);
            else
                CycleScanSelection(forward);
            break;
        }
        case KEY_9:
            if (!historyListActive)
                HistoryOpen();
            break;

        case KEY_6: // next mode
            if (historyListActive) {
                DeleteHistoryItem();
            } else
                NextAppMode();
            break;
        case KEY_SIDE1:
            if (SPECTRUM_PAUSED)
                return;
            if (++SpectrumMonitor > 2)
                SpectrumMonitor = 0;
            if (SpectrumMonitor == 1) {
                if (lastReceivingFreq < FMIN || lastReceivingFreq > FMAX) {
                    lastReceivingFreq = (scanInfo.f >= FMIN) ? scanInfo.f : SpectrumRangeStart;
                }
                peak.f = lastReceivingFreq;
                scanInfo.f = lastReceivingFreq;
                SetF(lastReceivingFreq);
                ApplyRXProfile(true);
                ToggleAFDAC(true);
                ToggleAFBit(true);
            }
            if (SpectrumMonitor == 2) {
                ToggleRX(1);
            }
            char monitorText[32];
            const char *modes[] = {"NORMAL", "FREQ LOCK", "MONITOR"};
            sprintf(monitorText, "MODE: %s", modes[SpectrumMonitor]);
            ShowOSDPopup(monitorText);
            RenderStatus();
            ST7565_BlitStatusLine();
            break;
        case KEY_SIDE2:
            if (historyListActive) {
                history[historyListIndex].HBlacklisted = !history[historyListIndex].HBlacklisted;
                ShowOSDPopup(history[historyListIndex].HBlacklisted ? "BL ADDED" : "BL REMOVED");
                RenderHistoryList();
                gIsPeak = 0;
                ToggleRX(false);
                ResetScanStats();
                NextScanStep();
            } else {
                Blacklist();
                char Text[32];
                sprintf(Text, "BL %u.%05u", lastReceivingFreq / 100000, lastReceivingFreq % 100000);
                if (!vfoRangeSession)
                    ShowOSDPopup(Text);
            }
            break;
        case KEY_PTT:
            SpectrumTransmit();
            break;
        case KEY_MENU:
            if (historyListActive) {
                historyMenuSelectedIndex = historyMenuScrollOffset = 0;
                SetState(HISTORY_MENU_SELECT);
            } else {
                if (Spectrum_state == SCAN_RANGE_MODE)
                    RangeEditorOpen();
                else {
                    const bool band = Spectrum_state == SCAN_BAND_MODE;
                    SetState(band ? BAND_LIST_SELECT : SCANLIST_SELECT);
                    if (band)
                        bandListSelectedIndex = bandListScrollOffset = 0;
                    else
                        scanListSelectedIndex = scanListScrollOffset = 0;
                }
            }
            break;
        case KEY_EXIT: // History or Spectrum exit
            if (historyListActive) {
                if (gHistoryScan) {
                    gHistoryScan = false;
                    gIsPeak = false;
                    WaitSpectrum = 0;
                    SpectrumMonitor = 0;
                    ToggleRX(false);
                    if (historySession)
                        HistoryQuiet();
                    gNextTimeslice_history = true;
                    break;
                }
                HistoryClose();
                if (historySession) {
                    DeInitSpectrum();
                    break;
                }
                SetF(scanInfo.f);
                SPECTRUM_PAUSED = false;
                StringCode[0] = '\0'; //Erase code
                SetState(SPECTRUM);
                break;
            }
            if (WaitSpectrum)
                WaitSpectrum = 0;
            DeInitSpectrum();
            break;
        default:
            break;
    }
}

static void OpenScanDetails(void)
{

    if (historyListActive)
        scanInfo.f = GetHistoryFreq(historyListIndex);
    SpectrumMonitor = 1;
    SetState(STILL);
    stillFreq = GetInitialStillFreq();
    if (stillFreq >= FMIN && stillFreq <= FMAX) {
        scanInfo.f = stillFreq;
        peak.f = stillFreq;
        SetF(stillFreq);
    }
}
static void SelectionMenuShort(void)
{
    scanOptionsChanged = true;
    if (currentState == SCANLIST_SELECT) {
        memset(settings.scanListEnabled, 0, sizeof settings.scanListEnabled);
        if (ScanListVisible(scanListSelectedIndex))
            settings.scanListEnabled[scanListSelectedIndex] = true;
        StartScanSelection();
    } else if (currentState == BAND_LIST_SELECT) {
        const uint8_t band = GetBandIndexForRow(bandListSelectedIndex);
        memset(settings.bandEnabled, 0, sizeof settings.bandEnabled);
        if (bandListSelectedIndex < bandCount)
            settings.bandEnabled[band] = true;
        nextBandToScanIndex = band;
        StartScanSelection();
    }
}

static void SelectionMenuLong(void)
{
    if (currentState == SCANLIST_SELECT || currentState == BAND_LIST_SELECT) {
        uint16_t ch = UINT16_MAX;
        const bool list = currentState == SCANLIST_SELECT;
        if (list && scanListSelectedIndex < scanListSlotCount)
            ch = scanListSelectedIndex + 1;
        if (!list && bandListSelectedIndex < bandCount)
            ch = bandChannels[GetBandIndexForRow(bandListSelectedIndex)];

        ToggleRX(false);
        gIsPeak = false;
        WaitSpectrum = 0;
        CHANNEL_OpenSelection(ch, list);
    } else {
        osdPopupTimer = 0;
        SetState(PARAMETERS_SELECT);
        if (!parametersStateInitialized) {
            parametersSelectedIndex = parametersScrollOffset = 0;
            parametersStateInitialized = true;
        }
    }
}

// Rebuild only after a committed edit; preserve selection by physical BAND slot.
static void RefreshScanSelection(void)
{
    uint16_t from, to;
    bool list;
    if (!CHANNEL_TakeResult(&from, &to, &list))
        return;
    scanOptionsChanged = true;
    if (list) {
        if (from >= 1 && from <= MR_CHANNELS_LIST && from != to) {
            const bool enabled = settings.scanListEnabled[from - 1];
            settings.scanListEnabled[from - 1] = false;
            if (to >= 1 && to <= MR_CHANNELS_LIST)
                settings.scanListEnabled[to - 1] = enabled;
        }
        SCANLISTS_InitUsage();
        BuildScanListSlots();
        if (to >= 1 && to <= MR_CHANNELS_LIST)
            scanListSelectedIndex = to - 1;
        if (scanListSelectedIndex >= scanListSlotCount)
            scanListSelectedIndex = scanListSlotCount ? scanListSlotCount - 1 : 0;
        scanListScrollOffset = 0;
        validChannelsCount = 0;
        LoadActiveScanFrequencies();
    } else {
        uint32_t enabled = 0;
        const uint16_t first = MR_CHANNELS_MAX - MAX_BANDS;
        for (uint8_t i = 0; i < bandCount; ++i) {
            const uint32_t bit = (uint32_t)1 << (bandChannels[i] - first);
            if (settings.bandEnabled[i])
                enabled |= bit;
        }
        if (from >= first && from <= MR_CHANNEL_LAST && from != to) {
            const uint32_t old = (uint32_t)1 << (from - first);
            const uint32_t next = to >= first && to <= MR_CHANNEL_LAST ? (uint32_t)1 << (to - first) : 0;
            if (enabled & old)
                enabled |= next;
            enabled &= ~old;
        }
        LoadActiveBands();
        memset(settings.bandEnabled, 0, sizeof settings.bandEnabled);
        for (uint8_t i = 0; i < bandCount; ++i) {
            const uint32_t bit = (uint32_t)1 << (bandChannels[i] - first);
            settings.bandEnabled[i] = (enabled & bit) != 0;
        }
        if (bandListSelectedIndex >= bandCount)
            bandListSelectedIndex = bandCount ? bandCount - 1 : 0;
        if (to != UINT16_MAX)
            for (uint8_t row = 0; row < bandCount; ++row)
                if (bandChannels[GetBandIndexForRow(row)] == to)
                    bandListSelectedIndex = row;
        bandListScrollOffset = 0;
        nextBandToScanIndex = bl = 0;
    }
    LoadMonitorFrequencies();
    refreshScanListName = true;
    latestScanListName[0] = 0;
    SaveSettings(false);
}

// ============================================================
// SECTION: Main keyboard dispatcher
// ============================================================

static bool ScanKeyLocked(uint8_t key)
{
    /* Key-lock guard: only KEY_F unlocks */
    if (gIsKeylocked) {
        if (key == KEY_F) {
            gIsKeylocked = false;
            ShowOSDPopup("Unlocked");
            gKeylockCountdown = durations[AUTO_KEYLOCK];
        } else {
            ShowOSDPopup("Unlock:F");
        }
        return true;
    }
    gKeylockCountdown = durations[AUTO_KEYLOCK];
    return false;
}

static void OnKeyDown(uint8_t key)
{
    if (historyListActive)
        gNextTimeslice_history = true;
    if (CHANNEL_IsActive()) {
        const bool ptt = key == KEY_PTT;
        CHANNEL_ProcessKeys(key, true, false);
        if (CHANNEL_IsActive())
            CHANNEL_ProcessKeys(key, false, false);
        RefreshScanSelection();
        if (ptt)
            SpectrumTransmit();
        return;
    }
    if (currentState == SCANLIST_SELECT || currentState == BAND_LIST_SELECT ||
        currentState == PARAMETERS_SELECT || (historyListActive && currentState == SPECTRUM)) {
        if (key == KEY_F) {
            selectionHelp = !selectionHelp;
            return;
        }
        if (selectionHelp) {
            selectionHelp = false;
            RenderStatus(); // the history help covered the status line
#ifdef ENABLE_FALCON_MESSAGES
            // F then an arrow: newest or oldest message.
            if (historyMessages && !msgOpen && currentState == SPECTRUM && (key == KEY_UP || key == KEY_DOWN)) {
                msgIndex = key == KEY_UP || !msgCount ? 0 : msgCount - 1u;
                msgStep = key == KEY_UP ? 1 : -1;
                return;
            }
#endif
            if (currentState != PARAMETERS_SELECT || key != KEY_EXIT)
                return;
        }
    }
    /* Dispatch to the handler for the currently active state */
    switch (currentState) {
        case BAND_LIST_SELECT:
            HandleKeyBandList(key);
            break;
        case SCANLIST_SELECT:
            HandleKeyScanList(key);
            break;
        case PARAMETERS_SELECT:
            HandleKeyParameters(key);
            break;
        case HISTORY_MENU_SELECT:
            HandleKeyHistoryMenu(key);
            break;
        default:
            HandleKeySpectrum(key);
            break;
    }
}

static int16_t storedScanStepIndex = -1;

static NATIVE_CODE(rf) void OnKeyDownStillNative(KEY_Code_t key)
{
    switch (key) {
        case KEY_3:
            ToggleListeningBW(1);
            break;

        case KEY_0:
            ToggleModulation();
            break;
        case KEY_DOWN:
            if (stillEditRegs) {
                SetRegMenuValue(stillRegSelected, true);
            } else if (SpectrumMonitor > 0) {
                uint32_t step = GetScanStep();
                stillFreq += step;
                scanInfo.f = stillFreq;
                peak.f = stillFreq;
                SetF(stillFreq);
            }
            break;
        case KEY_UP:
            if (stillEditRegs) {
                SetRegMenuValue(stillRegSelected, false);
            } else if (SpectrumMonitor > 0) {
                uint32_t step = GetScanStep();
                if (stillFreq > step)
                    stillFreq -= step;
                scanInfo.f = stillFreq;
                peak.f = stillFreq;
                SetF(stillFreq);
            }
            break;
        case KEY_2:
            if (stillEditRegs && stillRegSelected > 0) {
                stillRegSelected--;
            }
            break;
        case KEY_8: //Still
            if (stillEditRegs && stillRegSelected < ARRAY_SIZE(allRegisterSpecs) - 1) {
                stillRegSelected++;
            }
            break;
        case KEY_STAR:
            if (storedScanStepIndex == -1) {
                storedScanStepIndex = settings.scanStepIndex;
            }
            UpdateScanStep(1);
            break;
        case KEY_F:
            if (storedScanStepIndex == -1) {
                storedScanStepIndex = settings.scanStepIndex;
            }
            UpdateScanStep(0);
            break;

        case KEY_5:
        case KEY_6:
            break;


        case KEY_SIDE1:
            SpectrumMonitor++;
            if (SpectrumMonitor > 2)
                SpectrumMonitor = 0;

            if (SpectrumMonitor == 1) {
                if (lastReceivingFreq < FMIN || lastReceivingFreq > FMAX) {
                    lastReceivingFreq = (stillFreq >= FMIN) ? stillFreq : scanInfo.f;
                }
                peak.f = lastReceivingFreq;
                scanInfo.f = lastReceivingFreq;
                SetF(lastReceivingFreq);
                ApplyRXProfile(true);
                ToggleAFDAC(true);
                ToggleAFBit(true);
            }

            if (SpectrumMonitor == 2)
                ToggleRX(1);
            break;


        case KEY_MENU:
            stillEditRegs = !stillEditRegs;
            break;
        case KEY_EXIT: //EXIT from regs
            if (stillEditRegs) {
                stillEditRegs = false;
                break;
            }
            if (storedScanStepIndex != -1) {
                settings.scanStepIndex = storedScanStepIndex;
                storedScanStepIndex = -1;
                scanInfo.scanStep = GetScanStep();
                scanStepOverride = false;
            }
            SpectrumMonitor = 0;
            SetState(SPECTRUM);
            WaitSpectrum = 0; //Prevent coming back to still directly

            break;
        default:
            break;
    }
}


static void RenderStatus()
{
    // The history keys popup rises over the status line: the scan parameters
    // wait until it closes (closing it redraws them).
    if (historyListActive && selectionHelp)
        return;
    if (vfoScan) {
        bool refresh = !isListening || rxDisplayPending;
#ifdef ENABLE_FEAT_F4HWN_RX_TX_TIMER
        static uint16_t lastTimerSeconds;
        const uint16_t seconds = 3600 - gRxTimerCountdown_500ms / 2;
        if (isListening && gSetting_set_tmr && seconds != lastTimerSeconds)
            refresh = true;
        if (refresh)
            lastTimerSeconds = seconds;
#endif
        // Only the small status line is refreshed once per elapsed second.
        // Keep the full-screen RX quieting and the scan/RX engine unchanged.
        if (refresh)
            UI_DisplayStatus();
        if (!isListening && !MLWatchActive())
            gUpdateDisplay = true;
        return;
    }
    if (HistoryOwnsStatus())
        return;
    memset(gStatusLine, 0, sizeof(gStatusLine));
    DrawStatus();
    BlitStatusLine();
}

static void DrawScanLabel(unsigned number, const char *name, unsigned start, unsigned width, bool full)
{
    if (width < 7)
        return;
    char label[16];
    if (width >= 23)
        snprintf(label, sizeof label, "%02u %.*s", number, full ? 10 : 3, name);
    else
        snprintf(label, sizeof label, "%02u", number);
    const unsigned textWidth = strlen(label) * 4 - 1;
    GUI_DisplaySmallest(label, start + (width > textWidth ? (width - textWidth) / 2 : 0), 42, false, true);
}

static NATIVE_CODE(plot) void RenderSpectrumNative()
{
    // Keep the existing settings bookkeeping; graphical scaling is independent.
    UpdateDBMaxAuto();
    // The RX marker and its plotted sample share the existing captured reading.
    const uint16_t rxSample = (spectrumRxDbm + 192) * 2;
    if (isListening && spectrumRxDbm != INT16_MIN) {
        memset(activity.sampled, 0, sizeof activity.sampled);
        RecordActivity(peak.f, gChannel, rxSample);
    }
    ActivityUpdate(&activity, activity.samples, gGlobalSysTickCounter);
    memset(gFrameBuffer[2], 0, 3 * 128);
    for (unsigned x = 0; x < 128; x++)
        gFrameBuffer[5][x] &= ~1u;
    if (Spectrum_state == CHANNEL_MODE) {
        ActivityDrawLists(&activity, &activityLists, gFrameBuffer);
        unsigned groups = scanListOnly ? 1 : 0;
        if (!scanListOnly)
            for (unsigned list = 0; list < MR_CHANNELS_LIST; ++list)
                if (settings.scanListEnabled[list])
                    ++groups;
        if (scanNoList)
            GUI_DisplaySmallest("CH NO LIST", (128 - 39) / 2, 42, false, true);
        if (!scanNoList && groups <= 9)
            for (unsigned list = 0; list < MR_CHANNELS_LIST; ++list)
                DrawScanLabel(list + 1, gListName[list], activityLists.start[list], activityLists.width[list],
                              groups == 1);
    } else {
        const bool bands = Spectrum_state == SCAN_BAND_MODE;
        ActivityDraw(&activity, activity.samples, gFrameBuffer);
        if (bands) {
            unsigned count = CountActiveBands(), rank = 0;
            if (count && count <= 9)
                for (unsigned band = 0; band < bandCount; ++band)
                    if (settings.bandEnabled[band]) {
                        const unsigned start = rank * ACTIVITY_WIDTH / count;
                        const unsigned end = ++rank * ACTIVITY_WIDTH / count;
                        DrawScanLabel(band + 1, BParams[band].BandName, start, end - start, count == 1);
                    }
        }
    }
    if (AbsoluteSQL()) {
        unsigned y = ActivityY(&activity, (ScanThreshold() + 192) * 2);
        if (y < ACTIVITY_TOP + 1)
            y = ACTIVITY_TOP + 1;
        for (unsigned x = 0; x < 128; x += 2)
            gFrameBuffer[y / 8][x] ^= 1u << (y % 8);
    }
}
static void RenderSpectrum()
{
    if (APP_NativeBegin(NATIVE_PLOT)) {
        RenderSpectrumNative();
        APP_NativeEnd();
    }
}


static void DrawMeter(int line)
{
    const uint8_t METER_PAD_LEFT = 4;
    const uint8_t LINE_HEIGHT = 4; // Height of the vertical lines
    const uint8_t Y_START_BIT = 1;
    const uint8_t SPACING = 2; // Space between each vertical line

    // Total width available for the meter
    uint8_t max_width_px = 90;
    const uint32_t frequency = currentState == STILL ? stillFreq : (isListening ? peak.f : scanInfo.f);
    uint8_t fill_px = Rssi2PX(scanInfo.rssi, frequency, 0, max_width_px);

    settings.dbMax = -60;
    settings.dbMin = -120;

    // Clear the current line in the frame buffer
    for (uint8_t px = 0; px < 128; px++) {
        gFrameBuffer[line][px] = 0;
    }

    // Draw alternating vertical lines instead of squares
    for (uint8_t x = 0; x < fill_px; x += SPACING) {
        uint8_t x_pos = METER_PAD_LEFT + x;

        // Boundary check
        if (x_pos >= 128)
            break;

        // Draw a single vertical line at the current X position
        for (uint8_t bit = Y_START_BIT; bit < Y_START_BIT + LINE_HEIGHT; bit++) {
            gFrameBuffer[line][x_pos] |= (1 << bit);
        }
    }
    static char Text[8] = "";
    sprintf(Text, "%d", MIN(Rssi2DBm(scanInfo.rssi) + dBmCorrTable[FREQUENCY_GetBand(frequency)], -53));
    UI_PrintStringSmallNormal(Text, 96, 96, line);
}

static NATIVE_CODE(rf) void RenderStillNative()
{
    char freqStr[18];
    UI_FormatFrequency(freqStr, stillFreq);
    UI_DisplayFrequency(freqStr, 0, 0, 0);
    DrawMeter(2);
    sLevelAttributes sLevelAtt;
    sLevelAtt = GetSLevelAttributes(scanInfo.rssi, stillFreq);

    if (sLevelAtt.over > 0)
        snprintf(String, sizeof(String), ns_rf_0, sLevelAtt.sLevel, sLevelAtt.over);
    else
        snprintf(String, sizeof(String), ns_rf_1, sLevelAtt.sLevel);

    GUI_DisplaySmallest(String, 4, 25, false, true);
    snprintf(String, sizeof(String), ns_rf_2, sLevelAtt.dBmRssi);
    GUI_DisplaySmallest(String, 40, 25, false, true);
    uint8_t total = ARRAY_SIZE(allRegisterSpecs);
    uint8_t lines = STILL_REGS_MAX_LINES;
    if (total < lines)
        lines = total;
    if (stillRegSelected >= total)
        stillRegSelected = total - 1;
    if (stillRegSelected < stillRegScroll)
        stillRegScroll = stillRegSelected;
    if (stillRegSelected >= stillRegScroll + lines)
        stillRegScroll = stillRegSelected - lines + 1;

    for (uint8_t i = 0; i < lines; ++i) {
        uint8_t idx = i + stillRegScroll;
        RegisterSpec s = allRegisterSpecs[idx];
        uint16_t v = GetRegMenuValue(idx);
        char buf[32];
        // Przygotuj tekst do wyświetlenia
        if (stillEditRegs && idx == stillRegSelected)
            snprintf(buf, sizeof(buf), ns_rf_3, s.name, v);
        else
            snprintf(buf, sizeof(buf), ns_rf_4, s.name, v);
        uint8_t y = 32 + i * 8;
        if (stillEditRegs && idx == stillRegSelected) {
            // Najpierw czarny prostokąt na wysokość linii
            for (uint8_t px = 0; px < 128; ++px)
                for (uint8_t py = y; py < y + 6; ++py) // 6 = wysokość fontu 3x5
                    PutPixel(px, py, true);            //
            // Następnie białe litery (fill = true)
            GUI_DisplaySmallest(buf, 0, y, false, false);
        } else {
            // Zwykły tekst: czarne litery na białym
            GUI_DisplaySmallest(buf, 0, y, false, true);
        }
    }
}

static void Render()
{
    if (historyListActive && CHANNEL_IsActive()) {
        CHANNEL_Display();
        return;
    }
    if (vfoScan) {
        if (MLWatchActive() && gScreenToDisplay != DISPLAY_MAIN)
            return;
        if (isListening && !rxDisplayPending && !(MLWatchActive() && gUpdateDisplay))
            return;
        if (MLWatchActive() && !gUpdateDisplay && !rxDisplayPending)
            return;
        VfoScanSyncDisplay();
        UI_DisplayMain();
        gUpdateDisplay = false;
#ifdef ENABLE_RSSI_BAR
        if (isListening)
            DisplayRSSIBar(true);
#endif
        rxDisplayPending = false;
        return;
    }
    if (currentState == SPECTRUM && !historyListActive && isListening && !rxDisplayPending &&
        !osdPopupTimer) {
        if (sentinelView) {
            if (sentinelMeterDue) {
                sentinelMeterDue = false;
                const uint8_t changed = DrawSentinelMeter(0, 0);
                if (changed & 1)
                    ST7565_BlitLine(2);
                if (changed & 2)
                    ST7565_BlitLine(3);
            }
        } else if (DrawSpectrumDbm(peak.f)) {
            ST7565_BlitLine(1);
            ST7565_BlitLine(2);
        }
        return;
    }
    memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

    switch (currentState) {
        case SPECTRUM:
            if (historyListActive) {
                if (gNextTimeslice_history)
                    RenderHistoryList();
                gNextTimeslice_history = 0;
                return;
            }
            const char *empty = EmptyScanText();
            if (empty) {
                if (sentinelView)
                    DrawSentinelFooter();
                UI_DrawText5x5(empty, (128 - strlen(empty) * 6 + 1) / 2, sentinelView ? 13 : 27,
                               strlen(empty));
                break;
            }
            if (isListening) {
                DrawF(peak.f);
            } else {
                if (SpectrumMonitor)
                    DrawF(lastReceivingFreq);
                else
                    DrawF(scanInfo.f);
            }

            // Every composed frame must contain the plot, including a silent
            // Frequency Lock/RX-delay hold after the old two-second cutoff.
            if (!sentinelView)
                RenderSpectrum();
            // One initial RX frame; no periodic full-screen bursts in RX.
            // A pending first frame must still be sent if this tick was late.
            if (isListening && rxDisplayPending) {
                ST7565_BlitFullScreen();
                rxDisplayPending = false;
                return;
            }
            break;
        case RANGE_EDIT:
            RenderRangeEditor();
            ST7565_BlitFullScreen();
            break;
        case STILL:
            if (APP_NativeBegin(NATIVE_RF)) {
                RenderStillNative();
                APP_NativeEnd();
            }
            break;
        case BAND_LIST_SELECT:
        case SCANLIST_SELECT:
            if (currentState == BAND_LIST_SELECT)
                RenderBandSelect();
            else
                RenderScanListSelect();
            if (CHANNEL_IsActive())
                CHANNEL_Display();
            else
                ST7565_BlitFullScreen();
            return;
        case PARAMETERS_SELECT:
            RenderParametersSelect();
            ST7565_BlitFullScreen();
            return;
        case HISTORY_MENU_SELECT:
            RenderHistoryMenuSelect();
            ST7565_BlitFullScreen();
            return;
    }
    BlitFullScreen();
}

static bool keyPressedWasConsumedByBacklight = false;

static __attribute__((noinline)) void HandleUserInput(void)
{
    kbd.prev = kbd.current;
    kbd.current = GetKey();
    if (currentState == RANGE_EDIT && kbd.current != KEY_INVALID)
        RangeEditorWake();

    // The key that opened the scanner (a long F1, F2 or MENU, the * of
    // ScanRange) is still held: nothing until it is released, release included.
    if (entryKeyHeld) {
        if (kbd.current == KEY_INVALID)
            entryKeyHeld = false;
        return;
    }

    if (ScanWakeConsume(&scanWakeWaitingRelease, kbd.current,
                        ((vfoScan && !MLWatchActive()) || vfoRangeSession) && currentState == SPECTRUM &&
                            (!vfoScan || vfoScanKeysReady))) {
        vfoScanSkipTicks = 0;
        vfoScanSide2Ticks = 0;
        return;
    }

    if (vfoScan) {
        // Ignore the long *Scan press that entered this blocking scan loop.
        // SK2 is accepted on short release, only while receiving.
        if (!vfoScanKeysReady) {
            if (kbd.current == KEY_INVALID)
                vfoScanKeysReady = true;
            return;
        }
        if (MLWatchActive()) {
            // A receive-dismiss key belongs to this RX, including its release
            // and held repeats. Never hand it off as a VFO tuning/input key.
            if (vfoScanSkipTicks) {
                if (kbd.current == KEY_INVALID)
                    vfoScanSkipTicks = 0;
                return;
            }
            if (isListening && mlOwner == (gEeprom.TX_VFO & 1u) && gScreenToDisplay == DISPLAY_MAIN &&
                !gEeprom.KEY_LOCK && !gWasFKeyPressed &&
                (kbd.current == KEY_1 ||
                 (!gScanSqlPreview && (kbd.current == KEY_UP || kbd.current == KEY_DOWN)))) {
                if (kbd.current == kbd.prev) {
                    vfoScanSkipTicks = 1;
                    Skip();
                }
                return;
            }
            const bool selectedKey = kbd.current != KEY_PTT && kbd.current != KEY_EXIT &&
                                     kbd.current != KEY_SIDE1 && kbd.current != KEY_SIDE2 &&
                                     !(gWasFKeyPressed && kbd.current <= KEY_9 && kbd.current != KEY_4);
            // Keep the normal debouncer/repeat path throughout SQL editing,
            // without leaving and re-entering ML between successive taps.
            const bool sqlKey = kbd.current == KEY_F || ((gWasFKeyPressed || gScanSqlPreview) &&
                                                         (kbd.current == KEY_UP || kbd.current == KEY_DOWN));
            if ((mlKeys || sqlKey || (isListening && mlOwner != (gEeprom.TX_VFO & 1u))) && selectedKey &&
                (gScreenToDisplay == DISPLAY_MAIN || gScreenToDisplay == DISPLAY_CHANNEL)) {
                const uint16_t before = gEeprom.ScreenChannel[gEeprom.TX_VFO & 1u];
                mlKeys = true;
                CheckKeys();
                if (before != gEeprom.ScreenChannel[gEeprom.TX_VFO & 1u])
                    mlRefresh = true;
                if (gScreenToDisplay != DISPLAY_MAIN && gScreenToDisplay != DISPLAY_CHANNEL) {
                    mlKeys = false;
                    kbd.current = KEY_INVALID;
                    DeInitSpectrum();
                    return;
                }
                if (kbd.current == KEY_INVALID && gKeyReading0 == KEY_INVALID && gKeyReading1 == KEY_INVALID)
                    mlKeys = false;
                if (gScreenToDisplay == DISPLAY_CHANNEL && gUpdateDisplay) {
                    CHANNEL_Display();
                    gUpdateDisplay = false;
                }
                return;
            }
            if ((kbd.current != KEY_INVALID && kbd.current == kbd.prev) ||
                (gNextTimeslice_AutoPtt && gEeprom.AUTO_ROGER) || gReducedService
#ifdef ENABLE_UART
                || UART_IsCommandAvailable(UART_PORT_UART)
#endif
#ifdef ENABLE_USB
                || UART_IsCommandAvailable(UART_PORT_VCP)
#endif
            )
                DeInitSpectrum();
            return;
        }
        if (kbd.current == KEY_F || kbd.prev == KEY_F) {
            if (kbd.current == KEY_F && kbd.current != kbd.prev)
                gWasFKeyPressed = !gWasFKeyPressed;
            return;
        }
        if (gWasFKeyPressed && (kbd.current == KEY_UP || kbd.current == KEY_DOWN)) {
            if (kbd.current != kbd.prev) {
                APP_AdjustScanSQL(NAV_DIR(kbd.current == KEY_UP ? -1 : 1) * (gEeprom.SET_NAV ? 1 : -1));
                Render();
            }
            return;
        }
        if (kbd.current == KEY_1 || kbd.prev == KEY_1) {
            if (kbd.current == KEY_1) {
                if (vfoScanSkipTicks < 80)
                    ++vfoScanSkipTicks;
            } else {
                if (kbd.current == KEY_INVALID && vfoScanSkipTicks >= 2 && vfoScanSkipTicks < 80 &&
                    isListening) {
                    Skip();
                    RenderStatus();
                    Render();
                }
                vfoScanSkipTicks = 0;
            }
            return;
        }
        if (kbd.current == KEY_SIDE2 || kbd.prev == KEY_SIDE2) {
            if (kbd.current == KEY_SIDE2) {
                if (vfoScanSide2Ticks < 80)
                    ++vfoScanSide2Ticks;
            } else {
                if (kbd.current == KEY_INVALID && vfoScanSide2Ticks >= 2 && vfoScanSide2Ticks < 80 &&
                    isListening)
                    VfoScanBlacklist();
                vfoScanSide2Ticks = 0;
            }
            return;
        }
        if (kbd.current != KEY_INVALID && kbd.current == kbd.prev) {
            DeInitSpectrum();
        }
        return;
    }

    static uint16_t press_duration = 0;
    static KEY_Code_t last_active_key = KEY_INVALID;
    static bool exitProcessed = false;
    static bool menuHandled = false;
    static uint32_t menuPressedTick;
    KEY_Code_t key_to_process = KEY_INVALID;

    if (kbd.current != KEY_INVALID) {
        if (kbd.current != kbd.prev) {
            // Premier appui sur une nouvelle touche
            press_duration = 0;
            last_active_key = kbd.current;
            exitProcessed = false;
            menuHandled = false;
            menuPressedTick = gGlobalSysTickCounter;

            if (kbd.current == KEY_PTT) {
                key_to_process = KEY_PTT;
            }
        } else {
            // Touche maintenue
            press_duration++;

            // Spectrum EXIT: debounce briefly, act while pressed, once only.
            if (kbd.current == KEY_EXIT && currentState == SPECTRUM && press_duration >= 2 &&
                !exitProcessed) {
                key_to_process = KEY_EXIT;
                exitProcessed = true;
            }

            if (CHANNEL_CanLongPress(kbd.current) && !menuHandled &&
                gGlobalSysTickCounter - menuPressedTick >= key_repeat_delay_10ms && !gIsKeylocked &&
                !keyPressedWasConsumedByBacklight) {
                menuHandled = true;
                if (Backlight_On)
                    BACKLIGHT_TurnOn();
                CHANNEL_ProcessKeys(kbd.current, true, true);
                CHANNEL_ProcessKeys(kbd.current, false, false);
            }
            if (currentState == RANGE_EDIT && kbd.current == KEY_EXIT && !menuHandled &&
                gGlobalSysTickCounter - menuPressedTick >= key_repeat_delay_10ms) {
                menuHandled = true;
                RangeEditorClose();
            }
            // Match the normal long-press delay by elapsed time, even when scanning is busy.
            if ((kbd.current == KEY_MENU ||
                 (historyListActive && (kbd.current == KEY_STAR || kbd.current == KEY_1 ||
                                        (historyMessages && kbd.current == KEY_6)))) &&
                !menuHandled &&
                !selectionHelp && !CHANNEL_IsActive() &&
                (currentState == SCANLIST_SELECT || currentState == BAND_LIST_SELECT ||
                 currentState == SPECTRUM) &&
                gGlobalSysTickCounter - menuPressedTick >= key_repeat_delay_10ms && !gIsKeylocked &&
                !keyPressedWasConsumedByBacklight) {
                menuHandled = true;
                if (Backlight_On)
                    BACKLIGHT_TurnOn();
                if (historyMessages) {
#ifdef ENABLE_FALCON_MESSAGES
                    if (kbd.current == KEY_6 && !msgOpen && !msgAsk) { // 6 long: CLEAR ALL? popup
                        msgAsk = MSG_ASK_CLEAR;
                        gNextTimeslice_history = true;
                    }
#endif
                } else if (historyListActive) {
                    if (kbd.current == KEY_1)
                        HistoryToVfo();
                    else if (kbd.current == KEY_MENU)
                        OpenHistoryChannel();
                    else { // * long: the history page scan
                        gHistoryScan = !gHistoryScan;
                        ShowOSDPopup(gHistoryScan ? "SCAN HISTORY ON" : "SCAN HISTORY OFF");
                        if (gHistoryScan) {
                            gIsPeak = false;
                            SpectrumMonitor = 0;
                        } else if (historySession)
                            HistoryQuiet();
                    }
                    gNextTimeslice_history = true;
                } else
                    SelectionMenuLong();
            }
            const bool repeatAllowed =
                kbd.current == KEY_UP || kbd.current == KEY_DOWN ||
                (CHANNEL_IsActive() ? CHANNEL_CanRepeat(kbd.current)
                                    : ((kbd.current == KEY_1 || kbd.current == KEY_3) &&
                                       (currentState == PARAMETERS_SELECT ||
                                        (currentState == RANGE_EDIT && rangeEditor.field >= 2))));
            if (!menuHandled && !exitProcessed && repeatAllowed) {
                // Répétition automatique lors d'un appui long
                if (press_duration > 80 && (press_duration % 10 == 0)) {
                    if (!keyPressedWasConsumedByBacklight) {
                        key_to_process = kbd.current;
                    }
                }
            }
        }
    } else {
        // Touche relâchée
        if (last_active_key != KEY_INVALID && last_active_key != KEY_PTT && !exitProcessed && !menuHandled) {
            if (!keyPressedWasConsumedByBacklight) {
                // Si le rétroéclairage n'a pas déjà intercepté l'appui long,
                // tout relâchement doit soumettre la touche à traiter (appui court/moyen)
                // All ordinary Spectrum/menu keys accept a brief stable tap.
                // Long-press repeat thresholds and dedicated EXIT/PTT stay intact.
                // EXIT must also accept a tap seen in only one scan tick.
                // A held EXIT was already dispatched once above.
                if (press_duration >= 2 || last_active_key == KEY_EXIT) {
                    key_to_process = last_active_key;
                }
            } else {
                // Réinitialisation après consommation par le rétroéclairage
                keyPressedWasConsumedByBacklight = false;
            }
        }
        press_duration = 0;
        last_active_key = KEY_INVALID;
        exitProcessed = false;
    }

    // Traitement de la touche et allumage systématique si nécessaire
    if (key_to_process != KEY_INVALID) {
        if (Backlight_On) {
            if (!backlightOn && gEeprom.BACKLIGHT_TIME && key_to_process != KEY_2 &&
                !(key_to_process == KEY_EXIT && currentState == SPECTRUM)) {
                BACKLIGHT_TurnOn();
                // A short press is dispatched on release: there is no remaining
                // gesture to consume. Keep the flag only for a held-key repeat.
                keyPressedWasConsumedByBacklight = (kbd.current != KEY_INVALID);
                return; // La première touche allume l'écran et est consommée
            }
            BACKLIGHT_TurnOn();
        }

        if (ScanKeyLocked(key_to_process))
            return;
        switch (currentState) {
            case SPECTRUM:
            case BAND_LIST_SELECT:
            case SCANLIST_SELECT:
            case PARAMETERS_SELECT:
            case HISTORY_MENU_SELECT:
                OnKeyDown(key_to_process);
                break;
            case RANGE_EDIT:
                HandleKeyRangeEditor(key_to_process);
                break;
            case STILL:
                if (key_to_process == KEY_7)
                    SaveSettings(true);
                else if (key_to_process == KEY_PTT)
                    SpectrumTransmit();
                else if (key_to_process == KEY_9)
                    HandleKeySpectrum(KEY_9);
                else if (APP_NativeBegin(NATIVE_RF)) {
                    OnKeyDownStillNative(key_to_process);
                    APP_NativeEnd();
                }
                break;
            default:
                break;
        }
    }
}

static void NextHistoryScanStep()
{
    uint16_t count = CountValidHistoryItems();
    if (count == 0)
        return;
    uint16_t start = historyListIndex;
    do {
        historyListIndex++;
        if (historyListIndex >= count)
            historyListIndex = 0;
        if (historyListIndex == start && history[historyListIndex].HBlacklisted)
            return;
    } while (history[historyListIndex].HBlacklisted);

    if (historyListIndex < historyScrollOffset) {
        historyScrollOffset = historyListIndex;
    } else if (historyListIndex >= historyScrollOffset + MAX_VISIBLE_LINES) {
        historyScrollOffset = historyListIndex - MAX_VISIBLE_LINES + 1;
    }
    scanInfo.f = GetHistoryFreq(historyListIndex);
    spectrumElapsedCount = 0;
}

static uint32_t savedScanF;

static void UpdateScan()
{
    if (EmptyScanText() || SPECTRUM_PAUSED || gIsPeak || SpectrumMonitor || WaitSpectrum || !GetStepsCount() ||
        HistorySilent())
        return;
    if (!gForceModulation) {
        if (MonitorIndex || (gHistoryScan && historyListActive)) {
            const uint16_t channel = BOARD_gMR_fetchChannel(scanInfo.f);
            if (channel < MAX_SCAN_CHANNELS)
                settings.modulationType = (uint8_t)ReadScanParameters(channel);
        } else if (Spectrum_state == CHANNEL_MODE && statusScanChannel < MAX_SCAN_CHANNELS)
            settings.modulationType = (uint8_t)ReadScanParameters(statusScanChannel);
        else if (Spectrum_state == SCAN_BAND_MODE && bl < bandCount)
            settings.modulationType = BParams[bl].modulationType;
        else if (Spectrum_state == SCAN_RANGE_MODE && rangeModConfigured && !vfoRangeSession)
            settings.modulationType = rangeModulation;
    }
    SetF(scanInfo.f);
    Measure();
    if (gIsPeak || SpectrumMonitor || WaitSpectrum)
        return;
#ifdef ENABLE_BENCH
    benchStepsThisSec++;
#endif

    if (!scanListOnly && gMonitorScan && gNextTimeslice_Monitor && monitorChannelsCount) {
        gNextTimeslice_Monitor = false;
        savedScanF = scanInfo.f; // Save before interruption
        MonitorIndex = monitorChannelsCount + 1;
    }
    if (MonitorIndex) {
        --MonitorIndex;
        if (MonitorIndex)
            scanInfo.f = MONITOR_Frequency(MonitorIndex - 1);
        else {
            scanInfo.f = savedScanF;
            NextScanStep();
        }
        return;
    }
    if (gHistoryScan && historyListActive)
        NextHistoryScanStep();
    else
        NextScanStep();
#ifdef ENABLE_BENCH
    if (benchLapDone) {
        benchLastLapMs = benchLapMs;
        benchLapMs = 0;
    }
#endif
    if (SpectrumSleepMs && !scanInfo.i) {
        BK4819_Sleep();
        BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, false);
        SPECTRUM_PAUSED = true;
        SpectrumPauseCount = SpectrumSleepMs;
    }
}


// Keep the periodic RX maintenance out of the hot scan loop.
static __attribute__((noinline)) void UpdateListening(void)
{

    static uint32_t stableFreq = 1;
    static uint16_t stableCount = 0;
    static bool SoundBoostsave = false; // Initialisation

    const bool wasListening = isListening;
    scanInfo.rssi = GetRssi();

    if (SoundBoost != SoundBoostsave) {
        if (SoundBoost) {
            BK4819_WriteRegister(0x54, 0x90D1); // default is 0x9009
            BK4819_WriteRegister(0x55, 0x3271); // default is 0x31a9
            BK4819_WriteRegister(0x75, 0xFC13); // default is 0xF50B
        } else {
            BK4819_WriteRegister(0x54, 0x9009);
            BK4819_WriteRegister(0x55, 0x31a9);
            BK4819_WriteRegister(0x75, 0xF50B);
        }
        SoundBoostsave = SoundBoost;
    }
    if (isListening && peak.f == stableFreq) {
        if (++stableCount >= 2) {
            if (Spectrum_state == CHANNEL_MODE && (PttEmission == 2 || Ptt2Emission == 2))
                TxChannel = gChannel;
            if (!SpectrumMonitor && !postTxListening)
                FillfreqHistory();
            {
                BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, 1);
                BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, 0);
            }
        }
    } else {
        stableFreq = peak.f;
        stableCount = 0;
    }

    if (AbsoluteSQL()) {
        gIsPeak = AboveScanSQL(scanInfo.rssi, peak.f) && !ScanNoiseClosed();
        if (!gIsPeak && isListening)
            ToggleRX(false);
    } else {
        UpdateNoiseOff();
        if (!isListening || postTxListening)
            UpdateNoiseOn();
    }
    if (wasListening && !isListening) {
        WaitSpectrum =
            (!vfoScan && !vfoRangeSession && !historyListActive && !SpectrumMonitor) ? SpectrumDelay : 0;
        if (WaitSpectrum)
            return;
    }
    spectrumElapsedCount += 200;
    if (peak.f >= FMIN && peak.f <= FMAX && gNextTimeslice_HTimeS) {
        gNextTimeslice_HTimeS = 0;
        uint32_t qpeak = peak.f;
        for (uint16_t i = 0; i < indexFs; i++) {
            if (GetHistoryFreq(i) == qpeak) {
                if (history[i].HTimeS < 3600) {
                    history[i].HTimeS += 1;
                } else {
                    history[i].HTimeS = 3599;
                }
                break;
            }
        }
    }
    uint32_t maxCount = (uint32_t)MaxListenTime * 1000;

    if (MaxListenTime && spectrumElapsedCount >= maxCount && !SpectrumMonitor) {
        Skip();
        return;
    }

    // --- Peak handling ---
    if (gIsPeak) {
        WaitSpectrum = SpectrumMonitor ? 0 : SpectrumDelay;
        return;
    }

    if (WaitSpectrum > 61000)
        return;

    if (WaitSpectrum > 200) {
        WaitSpectrum -= 200;
        return;
    }
    // timer expired
    WaitSpectrum = 0;
    ResetScanStats();
}

static __attribute__((noinline)) void Tick(void)
{
    // Use the existing 200-ms scheduler before popup/editor early returns.
    if (gNextTimeslice_listening) {
        gNextTimeslice_listening = 0;

        if ((isListening || SpectrumMonitor || WaitSpectrum) && !HistorySilent())
            UpdateListening();
    }
    if (!isListening && gIsPeak && SpectrumMonitor != 2 && !SPECTRUM_PAUSED && !HistorySilent()) {
        if (SpectrumMonitor != 1)
            SetF(peak.f);
        ToggleRX(true);
    }
    // Editors pause the scanner, but must keep its backlight timeout running.
    if (CHANNEL_IsActive() && !MLWatchActive() && gNextTimeslice_500ms) {
        gNextTimeslice_500ms = false;
        if (gBacklightCountdown_500ms > 0)
            --gBacklightCountdown_500ms;
        if (gEeprom.BACKLIGHT_TIME < 61 && gBacklightCountdown_500ms == 0)
            BACKLIGHT_TurnOff();
    }
    if (gNextTimeslice_10ms) {
        gNextTimeslice_10ms = 0;
        SentinelTimerUpdate();
        HandleUserInput();
        // EXIT has rebuilt normal RX: do not run another scanner operation.
        if (!isInitialized)
            return;
        BACKLIGHT_Update();
        if (CHANNEL_IsActive()) {
            if (gUpdateDisplay) {
                gUpdateDisplay = false;
                RenderStatus();
                Render();
            }
            gNextTimeslice_display = false;
            return;
        }
        if (currentState == RANGE_EDIT) {
            RangeEditorTick();
            return;
        }
        if (osdPopupTimer) {
            // Use elapsed 10 ms system ticks, including ticks missed while busy.
            const uint32_t now = gGlobalSysTickCounter;
            const uint32_t elapsed = now - osdPopupTick;
            osdPopupTick = now;
            if (elapsed >= (osdPopupTimer + 9u) / 10u) {
                osdPopupTimer = 0;
                osdPopupText[0] = '\0';
                // A popup can cover the one-shot RX frame. Rebuild it once,
                // then return to the existing quiet/frozen RX display.
                if (currentState == SPECTRUM && isListening) {
                    if (historyListActive)
                        gNextTimeslice_history = true;
                    else
                        rxDisplayPending = true;
                }
                Render();
            } else {
                osdPopupTimer -= elapsed * 10u;
                UI_DisplayPopup(osdPopupText);
                ST7565_BlitFullScreen();
            }
            UpdateSpectrumMonitorLeds();
            return;
        }
#ifdef ENABLE_BENCH
        if (!isListening && !SPECTRUM_PAUSED && !SpectrumMonitor && !WaitSpectrum) {
            benchTickMs += 10;
            benchLapMs += 10;
            if (benchTickMs >= 1000) {
                benchTickMs -= 1000;
                benchRatePerSec = benchStepsThisSec;
                benchStepsThisSec = 0;
            }
        }
#endif
        if (SpectrumPauseCount)
            SpectrumPauseCount--;
    }
    if (CHANNEL_IsActive() && !MLWatchActive())
        return;
    if (MLWatchActive() && !isListening && (mlRefresh || CHANNEL_IsActive())) {
        mlKeys = false;
        kbd.current = KEY_INVALID;
        DeInitSpectrum();
        return;
    }
    if (currentState == RANGE_EDIT) {
        RangeEditorTick();
        return;
    }
    //static bool toggleFilter = 0;
    if (gNextTimeslice_500ms) {
        if (MLWatchActive())
            APP_TimeSlice500ms();
        else {
            gNextTimeslice_500ms = false;
            if (vfoScan && currentState == SPECTRUM && !osdPopupTimer)
                UI_MAIN_TimeSlice500ms();
            if (sentinelView && isListening && currentState == SPECTRUM && !historyListActive &&
                !osdPopupTimer) {
                sentinelMeterDue = true;
                Render();
            }
            if (gBacklightCountdown_500ms > 0)
                --gBacklightCountdown_500ms;
            if (gEeprom.BACKLIGHT_TIME < 61 && gBacklightCountdown_500ms == 0) {
                BACKLIGHT_TurnOff();
            }

            if (gKeylockCountdown > 0) {
                gKeylockCountdown--;
            }
            if (!vfoScan && AUTO_KEYLOCK && !gKeylockCountdown) {
                if (!gIsKeylocked)
                    ShowOSDPopup("Locked");
                gIsKeylocked = true;
            }
        }
    }

    // A silent history page: keys, backlight and display only; no scan and no RX.
    if (HistorySilent()) {
        if (gNextTimeslice_display) {
            gNextTimeslice_display = 0;
            Render();
        }
        return;
    }

    if (SPECTRUM_PAUSED && (SpectrumPauseCount == 0)) {
        // end of pause
        SPECTRUM_PAUSED = false;
        BK4819_ToggleGpioOut(BK4819_GPIO0_PIN28_RX_ENABLE, true);
        BK4819_RX_TurnOn(); //Wake up
        SYSTEM_DelayMs(10);
    }

    if (EmptyScanText()) {
        if (gNextTimeslice_display) {
            gNextTimeslice_display = false;
            RenderStatus();
            Render();
        }
        return;
    }

    if (newScanStart) {
        newScanStart = false;
        InitScan();
    }

    if (!isListening && currentState == SPECTRUM) {
        UpdateScan();
    }
    if (gNextTimeslice_display) {
        gNextTimeslice_display = 0;
        latestScanListName[0] = '\0';
        RenderStatus();
        Render();
    }
    if (!last_ptt_state) {
        const bool scanning =
            currentState == SPECTRUM && !isListening && !SPECTRUM_PAUSED && !SpectrumMonitor && !WaitSpectrum;
        if (gNextTimeslice_SCAN_LED) {
            gNextTimeslice_SCAN_LED = false;
            if (scanning) { // the scan blink: green and red together, unlike the green RX
                BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, true);
                BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, true);
            }
            if (!sentinelView && !isListening && stringCodeTimer && --stringCodeTimer == 0 && StringCode[0]) {
                StringCode[0] = 0;
                rxDisplayPending = true;
            }
        }
        if (gNextTimeslice_SCAN_LED_OFF) {
            gNextTimeslice_SCAN_LED_OFF = false;
            if (!isListening)
                BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);
            BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, false);
        }
    }

    UpdateSpectrumMonitorLeds();
}

void APP_RunScanner(Mode mode, bool sentinel)
{
    SelectScanProfile(sentinel);
    if (mode <= SCAN_BAND_MODE)
        Spectrum_state = mode;
    APP_RunSpectrum();
}

void APP_RunSentinel(void)
{
    APP_RunScanner((Mode)3, true); // Restore Sentinel's own last mode.
}

// VFO shortcut: the scanner history without its scan.
void APP_RunHistory(void)
{
    historySession = true;
    APP_RunSpectrum();
    historySession = false;
}

#ifdef ENABLE_FALCON_MESSAGES
// VFO shortcut: the history, opened on its MESSAGES page.
static bool messagesSession;
void APP_RunMessages(void)
{
    messagesSession = true;
    APP_RunHistory();
    messagesSession = false;
}
#endif

void APP_RunSpectrum(void)
{
    SETTINGS_SaveVfoNow(); // the scanners reload the VFO from flash on exit
    if (gEeprom.FlashlightOnRX) {
        APP_FalconDisable();
        SETTINGS_SaveSettings(); // Scanner exit reloads EEPROM: persist before entering.
        gRequestSaveSettings = false;
    }
    Backlight_On = true;
    osdPopupTimer = 0; // Do not carry a previous session's confirmation into a new scan.
    const char *entryText = !vfoScan && !scanListOnly && !historySession
                                ? (sentinelView ? (Spectrum_state == CHANNEL_MODE     ? "SENTINEL LIST"
                                                   : Spectrum_state == SCAN_BAND_MODE ? "SENTINEL BAND"
                                                                                      : "SENTINEL RANGE")
                                   : Spectrum_state == CHANNEL_MODE   ? "SPECTRE LIST"
                                   : Spectrum_state == SCAN_BAND_MODE ? "SPECTRE BAND"
                                                                      : "SPECTRE RANGE")
                                : NULL;
    scanOptionsChanged = false;
    savedListenBw = settings.listenBw;
    savedListenBwAM = listenBwAM;
    savedScanStep = settings.scanStepIndex;
    if (!scanListOnly)
        SaveSettings(false);
    scanBwOverrides = 0;
    scanStepOverride = false;
    sentinelIdleTicks = 0;
    sentinelTimerTick = gGlobalSysTickCounter;
    // Keep the previous LCD frame until the complete scanner frame is ready.
    gForceModulation = false;
    lastFilterPath = UINT8_MAX;
    rssiReferenceValid = false;
    channelName[0] = 0;
    lastNamedChannel = UINT16_MAX;
    lastNamedFrequency = 0;
    postTxListening = false;
    spectrumPtt2Key = KEY_INVALID;
    isListening = false;
    isKnownChannel = false; // no stale VFO/previous RX reference on entry
    scanWakeWaitingRelease = false;
    entryKeyHeld = KEYBOARD_Poll() != KEY_INVALID;
    vfoScanSide2Ticks = 0;
    memset(&activity, 0, sizeof(activity));
    if (!vfoScan) {
        channelTxSlot = gEeprom.TX_VFO;
        channelTxScreen = gEeprom.ScreenChannel[channelTxSlot];
        channelTxMemory = gEeprom.MrChannel[channelTxSlot];
        channelTxVfo = gEeprom.VfoInfo[channelTxSlot];
    }
    for (;;) {
        SpectrumMonitor = 0;
        if (!scanListOnly)
            LoadMonitorFrequencies();
        if (!Key_1_pressed)
            LoadSettings();
        Key_1_pressed = 0;
        DelayRssi = FastSpeed ? 800 : 1500;
        if (!vfoScan)
            SCANLISTS_InitUsage();
        LoadActiveScanFrequencies();
        if (!vfoScan)
            LoadActiveBands();
        ResetScanParameterOverrides();
        if ((vfoScan || scanListOnly) && Spectrum_state == CHANNEL_MODE &&
            (!validChannelsCount || (scanListOnly && !scanChannelsCount))) {
            if (gEeprom.RX_MODE == RX_MODE_ML) {
                ACTION_SetRxMode(RX_MODE_DUAL);
                kbd.current = KEY_INVALID;
                return;
            }
            UI_DisplayClear();
            UI_PrintString("NO CHANNELS", 12, 116, 2, 8);
            UI_PrintString("FOUND", 12, 116, 4, 8);
            ST7565_BlitFullScreen();
            SYSTEM_DelayMs(2000);
            return;
        }

#ifdef ENABLE_FEAT_F4HWN_RESUME_STATE
        // Persist the exact scan mode before entering the resumable Spectrum state.
        // This ensures F+6/F+7/F+8 is restored correctly after a power cycle.
        if (!scanListOnly && !historySession) {
            gEeprom.CURRENT_STATE = 4;
            SETTINGS_WriteCurrentState();
        }
#endif
        ResetModifiers();
        BackupRegisters();
        BK4819_WriteRegister(BK4819_REG_30, 0);
        SYSTEM_DelayMs(10);
        BK4819_RX_TurnOn();
        SYSTEM_DelayMs(50);
        // uint8_t CodeType = gTxVfo->pRX->CodeType;
        // uint8_t Code     = gTxVfo->pRX->Code;
        // BK4819_SetCDCSSCodeWord(DCS_GetGolayCodeWord(CodeType, Code));
        ResetInterrupts();
        BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, 0);
        BK4819_ToggleGpioOut(BK4819_GPIO5_PIN1_RED, 0);
        BK4819_WriteRegister(BK4819_REG_47, 0x6042);
        BK4819_WriteRegister(BK4819_REG_48, 0xB3AA); // AF gain
        ToggleRX(true), ToggleRX(false);             // hack to prevent noise when squelch off
        // Startup tuning is not a received signal. No value before the first RX.
        spectrumRxDbm = INT16_MIN;
        spectrumRxDbmFresh = false;
        channelName[0] = 0;
        lastNamedChannel = UINT16_MAX;
        lastNamedFrequency = 0;
        lastFilterPath = UINT8_MAX;
        if (Spectrum_state == SCAN_RANGE_MODE && rangeModConfigured && !vfoRangeSession && !gForceModulation)
            settings.modulationType = rangeModulation;
        else if (!(vfoRangeSession && Spectrum_state == SCAN_RANGE_MODE)) {
            if (vfoScan || (Spectrum_state == SCAN_RANGE_MODE && !gForceModulation))
                settings.modulationType = gTxVfo->Modulation;
            else
                ResetScanParameterOverrides();
        }
        ApplyRXProfile(true);
        isListening = true;
        newScanStart = true;
        RelaunchScan();
        for (int i = 0; i < 128; ++i) {
            rssiHistory[i] = 0;
        }
        isInitialized = true;
        historyListActive = false;
        if (historySession) {
            HistoryOpen();
            HistoryQuiet();
#ifdef ENABLE_FALCON_MESSAGES
            if (messagesSession)
                HistoryTogglePage();
#endif
        }
#ifdef ENABLE_CPU_TEMP
        temp_dc = CpuTemp_ReadDeciCelsius();
        //cpu_hz = CpuInfo_GetClockHz();
#endif
        Skip();
        // Keep creation reachable after the final item has been removed.
        if (!vfoScan && !scanListOnly) {
            SCANLISTS_InitUsage();
            BuildScanListSlots();
        }
        if (!vfoScan) {
            RenderStatus();
            Render();
            if (entryText) {
                ShowOSDPopup(entryText);
                UI_DisplayPopup(entryText);
                ST7565_BlitFullScreen();
                entryText = NULL;
            }
        }
        while (isInitialized) {
            Tick();
        }

        if (gSpectrumChangeRequested) {
            gSpectrumChangeRequested = false;
            RestoreRegisters();
            continue;
        } else {
            // DeInitSpectrum() already rebuilt the normal radio RX path.
            // Do not overwrite it with the scanner register snapshot here.
            break;
        }
        break;
    }
}


uint8_t APP_GetScanningVfo(void)
{
    // ML keeps the normal idle VFO titles rather than the blinking SCAN label.
    return vfoScan && isInitialized && !MLWatchActive() ? vfoScanVfo : UINT8_MAX;
}

static void VfoScanSyncDisplay(void)
{
    if (vfoRangeSession) {
        VFO_Info_t *v = &gEeprom.VfoInfo[vfoScanVfo];
        v->freq_config_RX.Frequency = scanInfo.f;
        v->Band = FREQUENCY_GetBand(scanInfo.f);
        RADIO_ApplyOffset(v);
        gEeprom.RX_VFO = vfoScanVfo;
        gRxVfo = gCurrentVfo = v;
        return;
    }
    if (!vfoScanCount || ScanChannel >= MR_CHANNELS_MAX)
        return;
    if (MLWatchActive()) {
        if (isListening) {
            if (channelTxVfo.CHANNEL_SAVE != ScanChannel || gRxVfo != &channelTxVfo) {
                RADIO_InitInfo(&channelTxVfo, ScanChannel, FetchChannelFrequency(ScanChannel).frequency);
                RADIO_LoadChannel(&channelTxVfo, ScanChannel, mlOwner, VFO_CONFIGURE_RELOAD);
            }
            gEeprom.RX_VFO = mlOwner;
            gRxVfo = gCurrentVfo = &channelTxVfo;
        } else
            RADIO_SelectVfos();
        return;
    }
    gEeprom.ScreenChannel[vfoScanVfo] = ScanChannel;
    RADIO_ConfigureChannel(vfoScanVfo, VFO_CONFIGURE_RELOAD);
    RADIO_SelectVfos();
    vfoScanDisplayedChannel = ScanChannel;
}

static void VfoScanBlacklist(void)
{
    if (vfoRangeSession) {
        Skip();
        return;
    }
    const uint32_t frequency = scanInfo.f;
    for (uint16_t ch = 0; ch < MR_CHANNELS_MAX; ++ch) {
        const uint8_t mask = 1u << (ch & 7);
        if (!(vfoScanMembers[ch >> 3] & mask))
            continue;
        if (FetchChannelFrequency(ch).frequency == frequency) {
            vfoScanMembers[ch >> 3] &= ~mask;
            --vfoScanCount;
        }
    }
    scanChannelsCount = vfoScanCount;
    if (!vfoScanCount) {
        DeInitSpectrum();
        return;
    }
    Skip();
    RenderStatus();
    Render();
}

void APP_RunCurrentScanList(uint8_t list)
{
    // Selector zero is internal to ML WATCH; ordinary scan shortcuts keep
    // their own behavior even when the background watch mode is selected.
    const bool mlWatch = list == 0;
    const bool frequencyScan = list == SCAN_VFO_FREQUENCY;
    if ((!mlWatch && !frequencyScan && list > MR_CHANNELS_LIST + 1) || scanListOnly)
        return;
    if (frequencyScan && (!IS_FREQ_CHANNEL(gTxVfo->CHANNEL_SAVE) ||
                          RX_freq_check(gTxVfo->pRX->Frequency) < 0 || gTxVfo->STEP_SETTING >= STEP_N_ELEM))
        return;
    if (mlWatch)
        list = MR_CHANNELS_LIST + 1;
    const bool allMemory = list == MR_CHANNELS_LIST + 1;
    LoadSettings();
    if (mlWatch) {
        mlGroup[0] = MLWatchGroup(0);
        mlGroup[1] = MLWatchGroup(1);
        if (mlGroup[1] == mlGroup[0])
            mlGroup[1] = UINT8_MAX;
        mlCursor[0] = mlCursor[1] = MR_CHANNELS_MAX - MAX_BANDS - 1;
        mlOwner = 1;
    }
    bool savedLists[MR_CHANNELS_LIST];
    memcpy(savedLists, settings.scanListEnabled, sizeof(savedLists));
    const bool savedFast = FastSpeed;
    const bool savedMonitor = gMonitorScan;
    const bool savedSentinel = sentinelView;
    sentinelView = false;
    const uint16_t savedDelay = DelayRssi;
    const Mode savedMode = Spectrum_state;
    const uint8_t savedPtt = PttEmission;
    const bool savedKey1 = Key_1_pressed;
    const State savedState = currentState;
    const uint8_t otherVfo = (gEeprom.TX_VFO & 1u) ^ 1u;
    const uint16_t savedOtherChannel = gEeprom.ScreenChannel[otherVfo];
    const uint16_t savedChannel = gEeprom.ScreenChannel[gEeprom.TX_VFO & 1u];
    const uint32_t savedStart = RangeStart, savedStop = RangeStop;
    const uint8_t savedMod = settings.modulationType, savedBw = settings.listenBw,
                  savedStep = settings.scanStepIndex;

    scanListOnly = mlWatch ? 0x80u : list;
    vfoScanVfo = gEeprom.TX_VFO & 1u;
    vfoScanDisplayedChannel = ScanChannel = gEeprom.ScreenChannel[vfoScanVfo];
    memset(vfoScanMembers, 0, sizeof(vfoScanMembers));
    vfoScanCount = 0;
    uint8_t groupsFound = 0;
    uint16_t mlCounts[2] = {0};
    for (uint16_t ch = 0; !frequencyScan && ch < MR_CHANNELS_MAX - MAX_BANDS; ++ch) {
        const ChannelAttributes_t *att = MR_GetChannelAttributes(ch);
        if (!att || att->band > BAND7_470MHz ||
            (allMemory ? att->scanlist == 51 : (att->scanlist != list && att->scanlist != 51)))
            continue;
        if (mlWatch && att->scanlist != mlGroup[0] && att->scanlist != mlGroup[1])
            continue;
        const uint32_t frequency = FetchChannelFrequency(ch).frequency;
        if (frequency < FMIN || frequency > FMAX || RX_freq_check(frequency) < 0)
            continue;
        vfoScanMembers[ch >> 3] |= 1u << (ch & 7);
        ++vfoScanCount;
        if (mlWatch) {
            if (att->scanlist == mlGroup[0]) {
                groupsFound |= 1;
                ++mlCounts[0];
            }
            if (att->scanlist == mlGroup[1]) {
                groupsFound |= 2;
                ++mlCounts[1];
            }
        }
    }
    if (mlWatch)
        for (uint8_t vfo = 0; vfo < 2; ++vfo)
            if (!(groupsFound & (1u << vfo)))
                mlGroup[vfo] = UINT8_MAX;
    if (mlWatch) {
        memcpy(mlListCount, mlCounts, sizeof mlListCount);
        mlRemaining = 0;
        mlPriorityPending = 0;
        mlTurn = 1;
        mlKeys = mlRefresh = false;
        for (uint8_t i = 0; i < 2; i++)
            mlPriority[i] = MLPriorityChannel(i);
    }
    vfoScan = true;
    vfoScanKeysReady = false;
    vfoScanSkipTicks = 0;
    vfoScanSide2Ticks = 0;
    // Avoid reloading and undoing the normal-speed overrides on entry.
    Key_1_pressed = true;
    currentState = SPECTRUM;
    kbd.current = kbd.prev = KEY_STAR;
    osdPopupTimer = 0;
    osdPopupText[0] = '\0';
    PttEmission = 0;
    memset(settings.scanListEnabled, 0, sizeof(settings.scanListEnabled));
    if (!allMemory && !frequencyScan)
        settings.scanListEnabled[list - 1] = true;
    FastSpeed = allMemory || frequencyScan;
    DelayRssi = FastSpeed ? 800 : 1500;
    gMonitorScan = false;
    MonitorIndex = 0;
    validChannelsCount = 0;
    Spectrum_state = frequencyScan ? SCAN_RANGE_MODE : CHANNEL_MODE;
    if (frequencyScan) {
        vfoRangeSession = true;
        RangeStart = gTxVfo->pRX->Frequency;
        RangeStop = FMAX;
        settings.scanStepIndex = gTxVfo->STEP_SETTING;
        settings.listenBw = gTxVfo->CHANNEL_BANDWIDTH;
        settings.modulationType = gTxVfo->Modulation;
    }
    APP_RunSpectrum();

    const KEY_Code_t resumeKey = kbd.current;
    if (mlWatch || kbd.current == KEY_EXIT)
        vfoScanDisplayedChannel = savedChannel;
    vfoScan = false;
    memset(vfoScanMembers, 0, sizeof(vfoScanMembers));
    vfoScanCount = 0;
    Key_1_pressed = savedKey1;
    PttEmission = savedPtt;
    currentState = savedState;
    gEeprom.TX_VFO = vfoScanVfo;
    if (!mlWatch) {
        gEeprom.ScreenChannel[otherVfo] = savedOtherChannel;
        RADIO_ConfigureChannel(otherVfo, VFO_CONFIGURE_RELOAD);
        gEeprom.ScreenChannel[vfoScanVfo] = vfoScanDisplayedChannel;
        RADIO_ConfigureChannel(vfoScanVfo, VFO_CONFIGURE_RELOAD);
    }
    RADIO_SelectVfos();
    if (frequencyScan && resumeKey != KEY_EXIT)
        VfoScanSyncDisplay();
    vfoRangeSession = false;
    RADIO_SetupRegisters(true);
    FUNCTION_Select(FUNCTION_FOREGROUND);
    BACKLIGHT_TurnOn();
    gRxVfoIsActive = false;
    gRxReceptionMode = RX_MODE_NONE;
    gDualVfoSchedule = false;
    gDualVfoCountdown_10ms = 10;
    if (!mlWatch)
        gRequestSaveVFO = true;
    else
        gSchedulePowerSave = false; // continuous scanning must not leave a stale sleep request
    gUpdateDisplay = true;
    gUpdateStatus = true;

    // This shortcut does not overwrite normal Spectrum scan preferences.
    memcpy(settings.scanListEnabled, savedLists, sizeof(savedLists));
    FastSpeed = savedFast;
    DelayRssi = savedDelay;
    gMonitorScan = savedMonitor;
    sentinelView = savedSentinel;
    Spectrum_state = savedMode;
    scanListOnly = 0;
    if (frequencyScan) {
        RangeStart = savedStart;
        RangeStop = savedStop;
        settings.modulationType = savedMod;
        settings.listenBw = savedBw;
        settings.scanStepIndex = savedStep;
    }
    if (mlWatch)
        APP_ResumeScanKey(resumeKey);
}

void APP_RunMLWatch(void)
{
    APP_FalconDisable();
    if (!APP_MLWatchEligible()) {
        ACTION_SetRxMode(RX_MODE_DUAL);
        return;
    }
    APP_RunCurrentScanList(0);
}

void APP_RunAllMemoryScan(void)
{
    // Internal selector 51 means all memories except the Monitor list.
    APP_RunCurrentScanList(IS_FREQ_CHANNEL(gTxVfo->CHANNEL_SAVE) ? SCAN_VFO_FREQUENCY : MR_CHANNELS_LIST + 1);
}

void APP_RunVfoScanRange(void)
{
    const VFO_Info_t *a = &gEeprom.VfoInfo[0];
    const uint32_t frequencyA = a->pRX->Frequency;
    const uint32_t frequencyB = gEeprom.VfoInfo[1].pRX->Frequency;
    if (frequencyA == frequencyB || frequencyA < FMIN || frequencyA > FMAX || frequencyB < FMIN ||
        frequencyB > FMAX || RX_freq_check(frequencyA) < 0 || RX_freq_check(frequencyB) < 0 ||
        a->STEP_SETTING >= ARRAY_SIZE(scanStepValues) ||
        (frequencyA < frequencyB ? frequencyB - frequencyA : frequencyA - frequencyB) <
            scanStepValues[a->STEP_SETTING]) {
        UI_DisplayPopup("INVALID RANGE");
        ST7565_BlitFullScreen();
        SYSTEM_DelayMs(1000);
        return;
    }
    LoadSettings();
    // VFO A/B range entry always starts at the native FastScan speed.
    FastSpeed = true;
    DelayRssi = 800;
    RangeStart = frequencyA < frequencyB ? frequencyA : frequencyB;
    RangeStop = frequencyA < frequencyB ? frequencyB : frequencyA;
    settings.scanStepIndex = a->STEP_SETTING;
    settings.listenBw = a->CHANNEL_BANDWIDTH;
    settings.modulationType = a->Modulation;
    Spectrum_state = SCAN_RANGE_MODE;
    currentState = SPECTRUM;
    // Keep normal ScanRange menus, history, Monitor, TX and speed controls.
    // Only the channel-derived modulation override is bypassed in this session.
    sentinelView = false;
    vfoRangeSession = true;
    kbd.current = kbd.prev = KEY_STAR;
    const bool savedKey1 = Key_1_pressed;
    Key_1_pressed = true;
    APP_RunSpectrum();
    Key_1_pressed = savedKey1;
    vfoRangeSession = false;
}

static void ToggleScanList(int scanListNumber, int single)
{
    if (Spectrum_state == SCAN_BAND_MODE) {
        if (single)
            memset(settings.bandEnabled, 0, sizeof(settings.bandEnabled));
        else
            settings.bandEnabled[scanListNumber - 1] = !settings.bandEnabled[scanListNumber - 1];
    }
    if (Spectrum_state == CHANNEL_MODE) {
        if (single) {
            memset(settings.scanListEnabled, 0, sizeof(settings.scanListEnabled));
        }
        if (scanListNumber < MR_CHANNELS_LIST) {
            settings.scanListEnabled[scanListNumber] = !settings.scanListEnabled[scanListNumber];
            refreshScanListName = true;
        }
    }
}

// ============================================================
// SECTION: EEPROM / Settings persistence
// ============================================================
void SCANLISTS_InitUsage(void)
{
    memset(gScanListHasChannels, 0, sizeof(gScanListHasChannels));

    for (uint16_t ch = MR_CHANNEL_FIRST; ch < MAX_SCAN_CHANNELS; ch++) {
        const ChannelInfo_t info = FetchChannelFrequency(ch);
        if (info.frequency < FMIN || info.frequency > FMAX)
            continue;
        ChannelAttributes_t *att = MR_GetChannelAttributes(ch);
        if (att->scanlist >= 1 && att->scanlist <= MR_CHANNELS_LIST) { // 1..50 = listes numérotées
            gScanListHasChannels[att->scanlist - 1] = true;
        }
        // valeur 51 = Monitor : hors des listes nommées, on l'ignore ici
    }
}


typedef struct {
    int ShowLines;
    uint8_t PttEmission;
    uint8_t listenBw;
    uint64_t bandListFlags; // Bits 0-63: bandEnabled[0..63]
    uint8_t listenBwAM, listenBwAMTag;
    uint8_t reserved16[39]; // Legacy bytes: keep subsequent EEPROM offsets unchanged.
    uint32_t scanListFlags; // Bits 0-31: scanListEnabled[0..31]
    int16_t Trigger;
    uint32_t RangeStart;
    uint32_t RangeStop;
    STEP_Setting_t scanStepIndex;
#ifdef ENABLE_SAVE_REGISTERS
    uint16_t R40; // RF TX Deviation
    uint16_t R29; // AF TX noise compressor, AF TX 0dB compressor, AF TX compression ratio
    uint16_t R19; // Disable MIC AGC
    uint16_t R73; // AFC range select
    uint16_t R10;
    uint16_t savedRegisters[11];
    uint16_t savedRegisters[10];
    uint16_t R13;
    uint16_t savedRegisters[12];
    uint16_t R3C;
    uint16_t savedRegisters[3];
    uint16_t R2B;
#endif
    uint16_t SpectrumDelay;
    uint8_t IndexMaxLT;
    uint8_t IndexPS;
    uint8_t Noislvl_OFF;
    uint16_t UOO_trigger;
    uint8_t osdPopupIndex;
    uint8_t Spectrum_state;
    uint8_t gSetting_set_audio_am;
    uint16_t TxChannel;
    bool Backlight_On;
    bool SoundBoost;
    bool gMonitorScan;
    bool FastSpeed;
    // V97: use trailing reserved padding; old field offsets and size stay fixed.
    uint8_t rangeModulation;
    uint8_t rangeModulationTag;
} SettingsEEPROM;
#ifdef ENABLE_SAVE_REGISTERS
_Static_assert(sizeof(SettingsEEPROM) == 128, "Scan settings layout changed");
#else
_Static_assert(sizeof(SettingsEEPROM) == 104, "Scan settings layout changed");
_Static_assert(__builtin_offsetof(SettingsEEPROM, reserved16) == 18, "SQL persistence offsets changed");
#endif


// The primary fields remain Spectrum's profile; Sentinel uses 20 legacy
// reserved bytes. Only the active profile is expanded into the existing RAM.
typedef struct {
    uint32_t lists, bands, start, stop;
    uint8_t step, mode, modulation, flags;
} ScanProfile;
_Static_assert(sizeof(ScanProfile) == 20, "Scan profile layout changed");
_Static_assert(MAX_BANDS <= 32 && MR_CHANNELS_LIST <= 32, "Scan selection mask overflow");
_Static_assert(sizeof(ScanProfile) + 3 == 23, "Scan profile overlaps SQLHYS");
_Static_assert(23 < sizeof(((SettingsEEPROM *)0)->reserved16), "SQLHYS persistence overflow");

// Reserved bytes 24..26 describe Spectrum's additional choices. Bytes
// 27..38 hold Sentinel's menu. Existing channel/configuration offsets stay fixed.
typedef struct __attribute__((packed)) {
    uint16_t delay;
    uint8_t maxListen, noise, record, power, popup, amAudio, flags, modulation, bw, tag;
} ScanMenuProfile;
_Static_assert(sizeof(ScanMenuProfile) == 12, "Scanner menu profile size");
_Static_assert(27 + sizeof(ScanMenuProfile) == sizeof(((SettingsEEPROM *)0)->reserved16),
               "Scanner menu overlaps settings");
#define SCAN_MENU_TAG 0xB6u

static void ApplyScanMenu(const SettingsEEPROM *d)
{
    ScanMenuProfile p = {.delay = d->SpectrumDelay,
                         .maxListen = d->IndexMaxLT,
                         .noise = d->Noislvl_OFF,
                         .record = d->UOO_trigger,
                         .power = d->IndexPS,
                         .popup = d->osdPopupIndex,
                         .amAudio = d->gSetting_set_audio_am,
                         .flags = (d->PttEmission == 2) | (d->gMonitorScan << 2) | (d->SoundBoost << 3),
                         .modulation = UINT8_MAX,
                         .bw = 0,
                         .tag = SCAN_MENU_TAG};
    if (d->reserved16[26] == SCAN_MENU_TAG) {
        const uint8_t flags = d->reserved16[24];
        p.flags |= (flags & 1u) << 1 | ((flags >> 1) & 3u) << 4;
        p.modulation = d->reserved16[25];
        p.bw = (flags >> 3) & 3u;
    }
    if (sentinelView && d->reserved16[38] == SCAN_MENU_TAG)
        memcpy(&p, d->reserved16 + 27, sizeof p);
    SpectrumDelay = p.delay <= 65000 ? p.delay : 0;
    IndexMaxLT = p.maxListen < ARRAY_SIZE(listenSteps) ? p.maxListen : 0;
    MaxListenTime = listenSteps[IndexMaxLT];
    Noislvl_OFF = p.noise <= 80 ? p.noise : NoisLvl;
    Noislvl_ON = Noislvl_OFF > NoiseHysteresis ? Noislvl_OFF - NoiseHysteresis : 1;
    UOO_trigger = p.record <= 50 ? p.record : 5;
    IndexPS = p.power < ARRAY_SIZE(PS_Steps) ? p.power : 0;
    SpectrumSleepMs = PS_Steps[IndexPS];
    osdPopupIndex = p.popup < ARRAY_SIZE(osdPopupTimes) ? p.popup : 1;
    osdPopupSetting = osdPopupTimes[osdPopupIndex];
    gSetting_set_audio_am = p.amAudio < 3 ? p.amAudio : 0;
    PttEmission = (p.flags & 1u) ? 2 : 0;
    Ptt2Emission = (p.flags & 2u) ? 2 : 0;
    gMonitorScan = (p.flags >> 2) & 1u;
    SoundBoost = (p.flags >> 3) & 1u;
    AUTO_KEYLOCK = (p.flags >> 4) & 3u;
    gKeylockCountdown = durations[AUTO_KEYLOCK];
    scanMenuModulation = p.modulation < MODULATION_UKNOWN ? p.modulation : UINT8_MAX;
    scanMenuBandwidth = p.bw & 3u;
}
static void StoreScanMenu(SettingsEEPROM *d)
{
    if (!sentinelView || Cleared) {
        d->PttEmission = PttEmission;
        d->SpectrumDelay = SpectrumDelay;
        d->IndexMaxLT = IndexMaxLT;
        d->IndexPS = IndexPS;
        d->Noislvl_OFF = Noislvl_OFF;
        d->UOO_trigger = UOO_trigger;
        d->osdPopupIndex = osdPopupIndex;
        d->SoundBoost = SoundBoost;
        d->gMonitorScan = gMonitorScan;
        d->gSetting_set_audio_am = gSetting_set_audio_am;
        d->reserved16[24] = (Ptt2Emission == 2) | (AUTO_KEYLOCK << 1) | (scanMenuBandwidth << 3);
        d->reserved16[25] = scanMenuModulation;
        d->reserved16[26] = SCAN_MENU_TAG;
    }
    if (sentinelView || Cleared) {
        const ScanMenuProfile p = {.delay = SpectrumDelay,
                                   .maxListen = IndexMaxLT,
                                   .noise = Noislvl_OFF,
                                   .record = UOO_trigger,
                                   .power = IndexPS,
                                   .popup = osdPopupIndex,
                                   .amAudio = gSetting_set_audio_am,
                                   .flags = (PttEmission == 2) | ((Ptt2Emission == 2) << 1) |
                                            (gMonitorScan << 2) | (SoundBoost << 3) | (AUTO_KEYLOCK << 4),
                                   .modulation = scanMenuModulation,
                                   .bw = scanMenuBandwidth,
                                   .tag = SCAN_MENU_TAG};
        memcpy(d->reserved16 + 27, &p, sizeof p);
    }
}

static __attribute__((noinline, noclone)) void ProfileToSettings(SettingsEEPROM *d, const ScanProfile *p)
{
    d->scanListFlags = p->lists;
    d->bandListFlags = p->bands;
    d->RangeStart = p->start;
    d->RangeStop = p->stop;
    d->scanStepIndex = p->step;
    d->Spectrum_state = p->mode;
    d->rangeModulation = p->modulation;
    d->rangeModulationTag = p->modulation < MODULATION_UKNOWN ? 0xA7 : 0;
    d->listenBw = p->flags & 3u;
    d->listenBwAM = (p->flags >> 2) & 1u;
    d->listenBwAMTag = 0xB1;
}
static void ApplyScanProfile(SettingsEEPROM *d)
{
    if (sentinelView) {
        ScanProfile p;
        memcpy(&p, d->reserved16 + 3, sizeof p);
        if ((p.flags & 0xF8u) == 0xA0 && p.mode <= SCAN_BAND_MODE && p.step < STEP_N_ELEM)
            ProfileToSettings(d, &p);
    }
    for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
        settings.scanListEnabled[i] = (d->scanListFlags >> i) & 1u;
    const uint32_t bands = d->bandListFlags;
    for (unsigned i = 0; i < MAX_BANDS; ++i)
        settings.bandEnabled[i] = (bands >> i) & 1u;
    settings.listenBw = d->listenBw < 3 ? d->listenBw : 0;
    listenBwAM = d->listenBwAMTag == 0xB1 ? d->listenBwAM == 1 : settings.listenBw != BK4819_FILTER_BW_WIDE;
    if (d->RangeStart >= FMIN && d->RangeStart <= FMAX)
        RangeStart = d->RangeStart;
    if (d->RangeStop >= FMIN && d->RangeStop <= FMAX)
        RangeStop = d->RangeStop;
    settings.scanStepIndex = d->scanStepIndex < STEP_N_ELEM ? d->scanStepIndex : STEP_12_5kHz;
    savedScanStep = settings.scanStepIndex;
    rangeModConfigured = d->rangeModulationTag == 0xA7 && d->rangeModulation < MODULATION_UKNOWN;
    if (rangeModConfigured)
        rangeModulation = d->rangeModulation;
    Spectrum_state = d->Spectrum_state <= SCAN_BAND_MODE ? d->Spectrum_state : CHANNEL_MODE;
    ApplyScanMenu(d);
}
static void SelectScanProfile(bool sentinel)
{
    LoadSettings();
    SettingsEEPROM d;
    PY25Q16_ReadBuffer(ADRESS_PARAMS, &d, sizeof d);
    if (d.reserved16[26] != SCAN_MENU_TAG) {
        sentinelView = false;
        ApplyScanProfile(&d);
        SaveSettings(false);
        PY25Q16_ReadBuffer(ADRESS_PARAMS, &d, sizeof d);
    }
    if ((d.reserved16[3 + __builtin_offsetof(ScanProfile, flags)] & 0xF8u) != 0xA0 ||
        d.reserved16[38] != SCAN_MENU_TAG) {
        // Freeze legacy selections/menu choices once, before either view edits.
        sentinelView = true;
        ApplyScanProfile(&d);
        SaveSettings(false);
        PY25Q16_ReadBuffer(ADRESS_PARAMS, &d, sizeof d);
    }
    sentinelView = sentinel;
    ApplyScanProfile(&d);
}


void LoadSettings()
{
    if (SettingsLoaded)
        return;
    SCANLISTS_InitUsage();
    SettingsEEPROM eepromData = {0};
    PY25Q16_ReadBuffer(ADRESS_PARAMS, &eepromData, sizeof(eepromData));
    if (eepromData.IndexMaxLT >= ARRAY_SIZE(listenSteps) || eepromData.IndexPS >= ARRAY_SIZE(PS_Steps) ||
        eepromData.osdPopupIndex >= ARRAY_SIZE(osdPopupTimes) ||
        (unsigned)eepromData.scanStepIndex >= ARRAY_SIZE(scanStepValues)) {
        ClearSettings();
        PY25Q16_ReadBuffer(ADRESS_PARAMS, &eepromData, sizeof(eepromData));
    }

    sentinelView = eepromData.ShowLines == 2;

#ifdef ENABLE_SAVE_REGISTERS
    BK4819_WriteRegister(BK4819_REG_10, eepromData.R10);
    BK4819_WriteRegister(BK4819_REG_11, eepromData.savedRegisters[11]);
    BK4819_WriteRegister(BK4819_REG_12, eepromData.savedRegisters[10]);
    BK4819_WriteRegister(BK4819_REG_13, eepromData.R13);
    BK4819_WriteRegister(BK4819_REG_14, eepromData.savedRegisters[12]);
#endif

    settings.rssiTriggerLevelUp = eepromData.Trigger;
    ApplyScanProfile(&eepromData);
    BK4819_SetFilterBandwidth(settings.listenBw, false);

    scanListSlotCount = 0;
    Backlight_On = true;
    TxChannel = eepromData.TxChannel;
    FastSpeed = eepromData.FastSpeed;
    DelayRssi = FastSpeed ? 800 : 1500;
#ifdef ENABLE_SAVE_REGISTERS
    BK4819_WriteRegister(BK4819_REG_40, eepromData.R40);
    BK4819_WriteRegister(BK4819_REG_29, eepromData.R29);
    BK4819_WriteRegister(BK4819_REG_19, eepromData.R19);
    BK4819_WriteRegister(BK4819_REG_73, eepromData.R73);
    BK4819_WriteRegister(BK4819_REG_3C, eepromData.R3C);
    BK4819_WriteRegister(BK4819_REG_43, eepromData.savedRegisters[3]);
    BK4819_WriteRegister(BK4819_REG_2B, eepromData.R2B);
#endif
    //0xA001 -OK! SPEED0
    //0x6141-fast ok but too fast
    //0xA141 - NEW GOOD SPEED 10 BAD FOR SUBTONE!!!0x5001-BEST!
    //0xA3C1- AFC SPEED 30
    BK4819_WriteRegister(BK4819_REG_73, 0xA3C1);
    if (!historyLoaded) {
        LoadHistory();
        historyLoaded = true;
    }
    SettingsLoaded = true;
}

static void SaveSettings(bool notify)
{
    if (scanListOnly || vfoRangeSession)
        return;
    SettingsEEPROM eepromData;
    PY25Q16_ReadBuffer(ADRESS_PARAMS, &eepromData, sizeof eepromData);
    if (Cleared)
        memset(&eepromData, 0, sizeof eepromData);
    ScanProfile p = {0};
    for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
        if (settings.scanListEnabled[i])
            p.lists |= UINT32_C(1) << i;
    for (unsigned i = 0; i < MAX_BANDS; ++i)
        if (settings.bandEnabled[i])
            p.bands |= UINT32_C(1) << i;
    p.start = RangeStart;
    p.stop = RangeStop;
    p.step = isInitialized ? savedScanStep : settings.scanStepIndex;
    p.mode = Spectrum_state;
    p.modulation = rangeModConfigured ? rangeModulation : UINT8_MAX;
    const unsigned bw = settings.listenBw;
    const unsigned am = listenBwAM;
    p.flags = 0xA0u | bw | (am << 2);
    if (!sentinelView || Cleared)
        ProfileToSettings(&eepromData, &p);
    if (sentinelView || Cleared)
        memcpy(eepromData.reserved16 + 3, &p, sizeof p);
    eepromData.reserved16[0] = 0xD2;
    eepromData.reserved16[1] = gVfoScanSQL;
    eepromData.reserved16[2] = gSpectrumSQL;
    eepromData.reserved16[23] = 0xA0u | gScanSQLHys;
    eepromData.Trigger = settings.rssiTriggerLevelUp;
    StoreScanMenu(&eepromData);
    eepromData.ShowLines = sentinelView ? 2 : 3;
    eepromData.Backlight_On = Backlight_On;
    eepromData.TxChannel = TxChannel;
    eepromData.FastSpeed = FastSpeed;
#ifdef ENABLE_SAVE_REGISTERS
    eepromData.R40 = BK4819_ReadRegister(BK4819_REG_40);
    eepromData.R29 = BK4819_ReadRegister(BK4819_REG_29);
    eepromData.R19 = BK4819_ReadRegister(BK4819_REG_19);
    eepromData.R73 = BK4819_ReadRegister(BK4819_REG_73);
    eepromData.R10 = BK4819_ReadRegister(BK4819_REG_10);
    eepromData.savedRegisters[11] = BK4819_ReadRegister(BK4819_REG_11);
    eepromData.savedRegisters[10] = BK4819_ReadRegister(BK4819_REG_12);
    eepromData.R13 = BK4819_ReadRegister(BK4819_REG_13);
    eepromData.savedRegisters[12] = BK4819_ReadRegister(BK4819_REG_14);
    eepromData.R3C = BK4819_ReadRegister(BK4819_REG_3C);
    eepromData.savedRegisters[3] = BK4819_ReadRegister(BK4819_REG_43);
    eepromData.R2B = BK4819_ReadRegister(BK4819_REG_2B);
#endif

    PY25Q16_WriteBuffer(ADRESS_PARAMS, ((uint8_t *)&eepromData), sizeof(eepromData), 0);
    if (notify)
        ShowOSDPopup(Cleared ? "DEFAULT SETTINGS" : "PARAMS SAVED");
    Cleared = 0;
}

static void ClearHistory(uint8_t mode)
{
    if (mode == 0) {
        memset(history, 0, sizeof(history));
    }
    if (mode == 1) {
        for (int i = 0; i < HISTORY_SIZE; i++) {
            if (!history[i].HBlacklisted) {
                SetHistoryFreq(i, 0);
                history[i].code = 0;
                history[i].HTimeS = 0;
                history[i].record = 0;
            }
        }
    }
    if (mode == 2) {
        for (int i = 0; i < HISTORY_SIZE; i++) {
            if (history[i].HBlacklisted) {
                SetHistoryFreq(i, 0);
                history[i].HBlacklisted = 0;
                history[i].code = 0;
                history[i].HTimeS = 0;
                history[i].record = 0;
            }
        }
    }
    // Force a reload of the history cache after clearing items
    lastHistoryScrollOffset = -1;
    CompactHistory();
}

void ClearSettings()
{
    rangeModConfigured = false;
    for (int i = 1; i < MR_CHANNELS_LIST; i++) {
        settings.scanListEnabled[i] = 0;
    }
    settings.scanListEnabled[0] = 1;
    settings.rssiTriggerLevelUp = 5;
    settings.listenBw = 0;
    listenBwAM = 0;
    RangeStart = 44600000;
    RangeStop = 44620000;
    PttEmission = 2;
    Ptt2Emission = 0;
    AUTO_KEYLOCK = 0;
    scanMenuModulation = UINT8_MAX;
    scanMenuBandwidth = 0;
    settings.scanStepIndex = STEP_6_25kHz;
    // The view is fixed to Smooth Spectrum.
    DelayRssi = 1500;
    SpectrumDelay = 0;
    MaxListenTime = 0;
    IndexMaxLT = 0;
    IndexPS = 0;
    Backlight_On = 1;
    Noislvl_OFF = NoisLvl;
    Noislvl_ON = NoisLvl - NoiseHysteresis;
    UOO_trigger = 5;
    osdPopupIndex = 1;
    osdPopupSetting = osdPopupTimes[osdPopupIndex];
    Spectrum_state = SCAN_RANGE_MODE;
    SoundBoost = 0;
    gMonitorScan = false;
    TxChannel = 0;
    FastSpeed = 0;
    settings.bandEnabled[0] = 1;
    gSetting_set_audio_am = 0;
    for (uint8_t i = 1; i < MAX_BANDS; i++) {
        settings.bandEnabled[i] = 0;
    }

#ifdef ENABLE_SAVE_REGISTERS
    BK4819_WriteRegister(BK4819_REG_10, 0x0145);
    BK4819_WriteRegister(BK4819_REG_11, 0x01B5);
    BK4819_WriteRegister(BK4819_REG_12, 0x0393);
    BK4819_WriteRegister(BK4819_REG_13, 0x03FF);
    BK4819_WriteRegister(BK4819_REG_14, 0x0019);
    BK4819_WriteRegister(BK4819_REG_40, 13738);
    BK4819_WriteRegister(BK4819_REG_29, 43840);
    BK4819_WriteRegister(BK4819_REG_19, 4161);
    BK4819_WriteRegister(BK4819_REG_73, 18066);
    BK4819_WriteRegister(BK4819_REG_3C, 20360);
    BK4819_WriteRegister(BK4819_REG_43, 13896);
    BK4819_WriteRegister(BK4819_REG_2B, 49152);
#endif

    Cleared = 1;
    SaveSettings(true);
}

// ============================================================
// SECTION: List item text helpers
// ============================================================

static NATIVE_CODE(lists) bool GetScanListLabel(uint8_t scanListIndex, char *bufferOut)
{
    if (scanListIndex >= MR_CHANNELS_LIST)
        return false;

    char nameOrFreq[13];
    memset(nameOrFreq, 0, sizeof(nameOrFreq));
    uint8_t firstChar = (uint8_t)gListName[scanListIndex][0];
    uint8_t i;

    if (firstChar != '\0' && firstChar != 0xFF) {
        for (i = 0; i < 6; i++) {
            char c = gListName[scanListIndex][i];
            if (c == '\0' || (uint8_t)c == 0xFF) {
                break;
            }
            nameOrFreq[i] = c;
        }
        nameOrFreq[i] = '\0';

        while (i > 0 && nameOrFreq[i - 1] == ' ') { // retirer le padding CHIRP
            nameOrFreq[--i] = '\0';
        }
    } else {
        nameOrFreq[0] = '\0'; // Empty/released slots have no synthetic name.
    }

    if (settings.scanListEnabled[scanListIndex]) {
        sprintf(bufferOut, "%02d-%s*", scanListIndex + 1, nameOrFreq);
    } else {
        sprintf(bufferOut, "%02d-%s", scanListIndex + 1, nameOrFreq);
    }
    return true;
}

static void BuildScanListSlots()
{
    // Slots remain available for editing/reuse, even after their list is deleted.
    scanListSlotCount = MR_CHANNELS_LIST;
}


static void GetScanListText(uint16_t displayIndex, char *buffer)
{
    GetScanListLabel(displayIndex, buffer);
}


// === Perform a single scan for the initially visible lines ===
void PreloadHistoryChannels(uint16_t startScrollIndex, uint16_t totalCount)
{
    // 1. Clean cache initialization
    for (uint8_t i = 0; i < HISTORY_ROW_CACHE; i++) {
        cachedChannels[i] = 0xFFFF;
        cachedAbsoluteIdx[i] = 0xFFFF;
    }
    cacheWriteHead = 0;

    if (totalCount == 0)
        return;

    // 2. One pass through all radio channels for the visible block
    for (uint16_t ch = MR_CHANNEL_FIRST; ch <= MR_CHANNEL_LAST; ch++) {
        ChannelInfo_t freqcmp = FetchChannelFrequency(ch);
        if (freqcmp.frequency == 0 || freqcmp.frequency == 0xFFFFFFFF)
            continue;

        uint32_t qfreqcmp = freqcmp.frequency;
        for (uint8_t i = 0; i < HISTORY_ROW_CACHE; i++) {
            uint16_t historyIdx = startScrollIndex + i;
            if (historyIdx >= totalCount)
                break;

            if (GetHistoryFreq(historyIdx) == qfreqcmp && cachedAbsoluteIdx[i] == 0xFFFF) {
                cachedChannels[i] = ch;
                cachedAbsoluteIdx[i] = historyIdx;
            }
        }
    }
}

// === Retrieve the line and handle the rotating cache if the line is out of cache ===
static NATIVE_CODE(lists) void GetHistoryRow(uint16_t index, ListRow *row)
{
    row->left[0] = '\0';
    row->right[0] = '\0';

    uint32_t f = GetHistoryFreq(index);
    if (!f)
        return;

    UI_FormatFrequency(row->left, f);


    uint16_t ch = 0xFFFF;
    bool foundInCache = false;
    for (uint8_t i = 0; i < HISTORY_ROW_CACHE; i++) {
        if (cachedAbsoluteIdx[i] == index) {
            ch = cachedChannels[i];
            foundInCache = true;
            break;
        }
    }
    if (!foundInCache) {
        ch = BOARD_gMR_fetchChannel(f); // Single lookup
        cachedAbsoluteIdx[cacheWriteHead] = index;
        cachedChannels[cacheWriteHead] = ch;
        cacheWriteHead = (cacheWriteHead + 1) % HISTORY_ROW_CACHE; // Advance the circular head
    }
    if (ch != 0xFFFF) {
        SETTINGS_FetchChannelName(row->right, ch);
        row->right[10] = '\0';
    }

    unsigned n = strlen(row->right);
    while (n && row->right[n - 1] == ' ')
        row->right[--n] = 0;
}

/* Scanlist multiselect: "NN-name" on left, "*" on right when enabled */
static NATIVE_CODE(lists) void GetScanListRow(uint16_t displayIndex, ListRow *row)
{
    char buf[20];
    GetScanListText(ScanListRowSlot(displayIndex), buf);
    /* Strip trailing '*' marker and padding spaces added by GetScanListLabel */
    size_t len = strlen(buf);
    bool enabled = (len > 0 && buf[len - 1] == '*');
    if (enabled)
        buf[--len] = '\0';
    while (len > 0 && buf[len - 1] == ' ')
        buf[--len] = '\0';
    strcpy(row->left, buf);
    if (enabled) {
        strcpy(row->right, "<====");
    } else
        row->right[0] = '\0';
}

static NATIVE_CODE(lists) void GetBandRow(uint16_t index, ListRow *row)
{
    uint8_t bandIndex = GetBandIndexForRow((uint8_t)index);
    snprintf(row->left, sizeof(row->left), "%02d-%s", bandIndex + 1, BParams[bandIndex].BandName);
    if (settings.bandEnabled[bandIndex]) {
        strcpy(row->right, "<====");
    } else
        row->right[0] = '\0';
}

static void GetParametersRow(uint16_t index, ListRow *row)
{
    static const char *const names[] = {
        [PARAM_SPECTRUM_DELAY] = "RX DELAY:",
        [PARAM_MAX_LISTEN_TIME] = "MAXLISTENTIME:",
        [PARAM_LISTEN_BW] = "LISTENBW FM:",
        [PARAM_LISTEN_BW_AM] = "LISTENBW AM:",
        [PARAM_MODULATION] = "MODULATION:",
        [PARAM_POWER_SAVE] = "POWER SAVE:",
        [PARAM_NOISE_LEVEL_OFF] = "NOIS LVL OFF:",
        [PARAM_OSD_POPUP] = "POPUPS:",
        [PARAM_RECORD_TRIGGER] = "RECORD TRIG:",
        [PARAM_AUTO_KEYLOCK] = "KEYLOCK:",
        [PARAM_SOUND_BOOST] = "SOUNDBOOST:",
        [PARAM_PTT_EMISSION] = "PTT:",
        [PARAM_PTT2_EMISSION] = "PTT2:",
        [PARAM_MONITOR_SCAN] = "MONITOR CH:",
        [PARAM_AUDIO_AM] = "AM AUDIO:",
    };
    row->right[0] = 0;
    const char *right = NULL;
    const uint16_t realIndex = SpectrumParameterAt(index);
    strcpy(row->left, names[realIndex]);
    switch (realIndex) {
        case PARAM_SPECTRUM_DELAY:
            if (SpectrumDelay < 65000)
                snprintf(row->right, sizeof(row->right), "%uS", SpectrumDelay / 1000);
            else
                right = "OFF";
            break;
        case PARAM_MAX_LISTEN_TIME:
            right = labels[IndexMaxLT];
            break;
        case PARAM_LISTEN_BW:
            right = bwNames[ScanListeningBandwidth(MODULATION_FM)];
            break;
        case PARAM_LISTEN_BW_AM:
            right = ScanListeningBandwidth(MODULATION_AM) ? "NARROW" : "WIDE";
            break;
        case PARAM_MODULATION: {
            const uint8_t modulation = (uint8_t)SpectrumStatusParameters();
            right = gModulationStr[modulation];
            break;
        }
        case PARAM_POWER_SAVE:
            right = labelsPS[IndexPS];
            break;
        case PARAM_NOISE_LEVEL_OFF:
            snprintf(row->right, sizeof(row->right), "%d", Noislvl_OFF);
            break;
        case PARAM_OSD_POPUP:
            if (osdPopupSetting) {
                uint8_t sec = osdPopupSetting / 1000;
                uint8_t dec = (osdPopupSetting % 1000) / 100;
                if (dec)
                    snprintf(row->right, sizeof(row->right), "%d.%dS", sec, dec);
                else
                    snprintf(row->right, sizeof(row->right), "%dS", sec);
            } else {
                right = "OFF";
            }
            break;
        case PARAM_RECORD_TRIGGER:
            snprintf(row->right, sizeof(row->right), "%d", UOO_trigger);
            break;
        case PARAM_AUTO_KEYLOCK:
            if (AUTO_KEYLOCK) {
                snprintf(row->right, sizeof(row->right), "%dS", durations[AUTO_KEYLOCK] / 2);
            } else {
                strcpy(row->left, "KEY UNLOCKED");
            }
            break;
        case PARAM_SOUND_BOOST:
            right = SoundBoost ? "ON" : "OFF";
            break;
        case PARAM_PTT_EMISSION:
        case PARAM_PTT2_EMISSION:
            right =
                (realIndex == PARAM_PTT_EMISSION ? PttEmission : Ptt2Emission) == 2 ? "LAST RX" : "CHANNEL";
            break;
        case PARAM_MONITOR_SCAN:
            if (gMonitorScan)
                right = "ON";
            else
                right = "OFF";
            break;
        case PARAM_AUDIO_AM: {
            const char *amProfileNames[] = {"SHARP", "STOCK", "OPEN"};
            right = amProfileNames[gSetting_set_audio_am % 3];
            break;
        }
        default:
            row->left[0] = '\0';
            break;
    }
    if (right)
        strcpy(row->right, right);
    for (char *p = row->right; *p; ++p)
        if (*p >= 'a' && *p <= 'z')
            *p -= 'a' - 'A';
}

// ============================================================
// SECTION: List screen render functions
// ============================================================

// Compact page header reused by the list inventory and history.
static __attribute__((noinline)) void DrawListTitle(const char *title, const char *counter)
{
    unsigned counterWidth = 0;
    for (unsigned i = 0; counter[i]; ++i) {
        const unsigned char c = counter[i];
        unsigned width = 6;
        if (c > ' ' && c < 127) {
            uint8_t glyph[6];
            FP_Read(gFontSmall[c - ' ' - 1], glyph, 6);
            while (width && !glyph[width - 1])
                --width;
            counterWidth += width + (counter[i + 1] ? 1 : 0);
        } else
            counterWidth += 4;
    }
    const unsigned titleWidth = strlen(title) * 7;
    const uint8_t x = (128 - titleWidth - 4 - counterWidth) / 2;
    UI_PrintStringSmallBold(title, x, 0, 0);
    UI_PrintStringSmallbackground(counter, x + titleWidth + 4, 0, 0, 0);
}

// The two selection pages share the exact text cells and highlight geometry.
// Values can reserve a two-pixel right margin without changing scanlist layout.
static __attribute__((noinline)) void DrawCompactSelectionRow(const ListRow *text, uint8_t y, bool selected,
                                                              uint8_t rightMargin)
{
    UI_DrawText5x5(text->left, 2, y, strlen(text->left));
    if (text->right[0])
        UI_DrawText5x5(text->right, 128 - rightMargin - strlen(text->right) * 6 + (rightMargin != 0), y,
                       strlen(text->right));
    if (selected)
        for (uint8_t py = y - 1; py < y + 6 && py < 50; ++py)
            for (uint8_t x = 0; x < 128; ++x)
                gFrameBuffer[py / 8][x] ^= 1u << (py % 8);
}

// The history pages: rows start under the title, or at the top when the
// title takes the status line (history opened from the VFO).
// These helpers are inlined into the lists and messages modules.
static inline __attribute__((always_inline)) uint8_t HistoryListTop(void)
{
    return HistoryOwnsStatus() ? 1 : 9;
}
static inline __attribute__((always_inline)) uint8_t HistoryListLines(void)
{
    return HistoryOwnsStatus() ? MAX_VISIBLE_LINES + 1 : MAX_VISIBLE_LINES;
}
// List page title; without a scan, the history moves it to the status line.
static inline __attribute__((always_inline)) void DrawPageTitle(const char *title, const char *counter)
{
    memset(gFrameBuffer, 0, sizeof gFrameBuffer);
    DrawListTitle(title, counter);
    if (HistoryOwnsStatus()) {
        memcpy(gStatusLine, gFrameBuffer[0], sizeof gStatusLine);
        memset(gFrameBuffer[0], 0, sizeof gFrameBuffer[0]);
    }
}
// One pixel above and one below the text, on both pages.
static inline __attribute__((always_inline)) void InvertHistoryRows(uint8_t y, uint8_t height)
{
    for (uint8_t py = y - 1; py < y + height * 7 - 1 && py < 50; ++py)
        for (uint8_t px = 0; px < 128; ++px)
            gFrameBuffer[py / 8][px] ^= 1u << (py % 8);
}

// Fixed frequency/name columns, duration right aligned. Long names use row two.
// y=255 measures the same layout without drawing or touching RF registers.
static NATIVE_CODE(lists) uint8_t DrawHistoryRow(uint16_t index, const ListRow *row, uint8_t y)
{
    const uint16_t seconds = history[index].HTimeS;
    char time[8];
    if (seconds > 59)
        sprintf(time, "%02u:%02u", seconds / 60, seconds % 60);
    else
        sprintf(time, "%02u", seconds);
    const uint8_t timeX = 128 - strlen(time) * 6;
    const uint16_t css = HistoryCode(index);
    const bool hasCode = (css >> 8) != CODE_TYPE_OFF;
    const bool longBlacklist = history[index].HBlacklisted && strlen(row->left) > 9;
    if (y != UINT8_MAX) {
        if (history[index].HBlacklisted)
            UI_DrawText5x5("#", 58, y + (longBlacklist ? 7 : 0), 1);
        UI_DrawText5x5(row->left, 1, y, strlen(row->left));
        UI_DrawText5x5(time, timeX, y, strlen(time));
        UI_DrawText5x5(row->right, 64, y, MIN(strlen(row->right), (timeX - 67) / 6));
        if (hasCode) {
            char text[8];
            DCS_FormatCode(text, css >> 8, (uint8_t)css, true);
            UI_DrawText5x5(text, 1, y + 7, strlen(text));
        }
    }
    return 1 + (hasCode || longBlacklist);
}

// Compact selection pages only: preserve the other unified-list screens.
static NATIVE_CODE(lists) void RenderScanSelection(const char *title, const char *counter, uint16_t count,
                                                   uint16_t selected, uint16_t scroll, GetListRowFn getRow)
{
    const bool historyPage = getRow == GetHistoryRow;
    // Opened from the VFO, the title takes the status line: one more row.
    const uint8_t top = HistoryListTop(), lines = HistoryListLines();
    if (selected < scroll)
        scroll = selected;
    if (historyPage) {
        unsigned used = 0;
        for (uint16_t i = scroll; i <= selected && i < count; ++i) {
            ListRow row;
            getRow(i, &row);
            used += DrawHistoryRow(i, &row, UINT8_MAX);
        }
        while (used > lines && scroll < selected) {
            ListRow row;
            getRow(scroll, &row);
            used -= DrawHistoryRow(scroll, &row, UINT8_MAX);
            ++scroll;
        }
        historyScrollOffset = scroll;
        if (historyScrollOffset != lastHistoryScrollOffset) {
            PreloadHistoryChannels(scroll, count);
            lastHistoryScrollOffset = scroll;
        }
    } else if (selected >= scroll + MAX_VISIBLE_LINES)
        scroll = selected - MAX_VISIBLE_LINES + 1;
    DrawPageTitle(title, counter);
    for (uint8_t row = 0; row < (historyPage ? lines : MAX_VISIBLE_LINES) && scroll < count; ++scroll) {
        ListRow text;
        getRow(scroll, &text);
        const uint8_t height = historyPage ? DrawHistoryRow(scroll, &text, UINT8_MAX) : 1;
        if (row + height > (historyPage ? lines : MAX_VISIBLE_LINES))
            break;
        const uint8_t y = (historyPage ? top : 10) + row * 7;
        if (historyPage)
            DrawHistoryRow(scroll, &text, y);
        else
            DrawCompactSelectionRow(&text, y, scroll == selected, 0);
        if (historyPage && scroll == selected)
            InvertHistoryRows(y, height);
        row += height;
    }
    CHANNEL_DrawKeysInfos();
    if (selectionHelp) {
        if (historyPage)
            CHANNEL_DrawHistoryHelp(false);
        else
            CHANNEL_DrawSelectionHelp(currentState == BAND_LIST_SELECT);
    }
}

static NATIVE_CODE(lists) void RenderScanListSelectNative()
{
    if (refreshScanListName) {
        BuildScanListSlots();
        refreshScanListName = false;
    }
    unsigned count = 0, selected = 0, enabled = 0;
    for (uint8_t row = 0; row < MR_CHANNELS_LIST; ++row) {
        const uint8_t slot = ScanListRowSlot(row);
        if (slot == UINT8_MAX)
            break;
        if (slot == scanListSelectedIndex)
            selected = count;
        ++count;
        enabled += settings.scanListEnabled[slot];
    }
    if (count && !ScanListVisible(scanListSelectedIndex))
        scanListSelectedIndex = ScanListRowSlot(0);
    char counter[12];
    snprintf(counter, sizeof counter, "%u/%u", enabled, count);
    RenderScanSelection("SCANLISTS:", counter, count, selected, scanListScrollOffset, GetScanListRow);
}
static void RenderScanListSelect()
{
    if (APP_NativeBegin(NATIVE_LISTS)) {
        RenderScanListSelectNative();
        APP_NativeEnd();
    }
}


static NATIVE_CODE(options) void RenderParametersSelectNative()
{
    const uint16_t count = GetMaxVisualRows();
    if (parametersSelectedIndex >= count)
        parametersSelectedIndex = count - 1;
    if (parametersSelectedIndex < parametersScrollOffset)
        parametersScrollOffset = parametersSelectedIndex;
    if (parametersSelectedIndex >= parametersScrollOffset + MAX_VISIBLE_LINES)
        parametersScrollOffset = parametersSelectedIndex - MAX_VISIBLE_LINES + 1;
    memset(gFrameBuffer, 0, sizeof gFrameBuffer);
    DrawListTitle("PARAMETERS:", "");
    for (uint8_t row = 0; row < MAX_VISIBLE_LINES && parametersScrollOffset + row < count; ++row) {
        const uint16_t index = parametersScrollOffset + row;
        ListRow text;
        GetParametersRow(index, &text);
        DrawCompactSelectionRow(&text, 9 + row * 7, index == parametersSelectedIndex, 2);
    }
    CHANNEL_DrawKeysInfosLabel(" KEYS INFOS");
    if (selectionHelp)
        CHANNEL_DrawParametersHelp();
}
static void RenderParametersSelect()
{
    if (APP_NativeBegin(NATIVE_OPTIONS)) {
        RenderParametersSelectNative();
        APP_NativeEnd();
    }
}


NATIVE_CODE(lists) void RenderBandSelectNative()
{
    char title[24];
    snprintf(title, sizeof(title), "%d/%d", CountActiveBands(), bandCount);
    RenderScanSelection("BAND:", title, bandCount, bandListSelectedIndex, bandListScrollOffset, GetBandRow);
}
void RenderBandSelect()
{
    if (APP_NativeBegin(NATIVE_LISTS)) {
        RenderBandSelectNative();
        APP_NativeEnd();
    }
}


static NATIVE_CODE(lists) void RenderHistoryListNative(void)
{
    const uint16_t count = CountValidHistoryItems();
    char counter[12];
    snprintf(counter, sizeof counter, "%u/%u", count ? historyListIndex + 1 : 0, count);
    RenderScanSelection("HISTORY:", counter, count, historyListIndex, historyScrollOffset, GetHistoryRow);
}

#ifdef ENABLE_FALCON_MESSAGES
// MESSAGES page: the title, rows, fonts and selection of the history pages.
// "01-AB: START OF THE TEXT", newest first; a dotted line marks a new FMessage
// session. M shows the selected message whole, 6 long asks CLEAR ALL?.
static NATIVE_CODE(messages) void RenderMessageNative(void)
{
    MSG_Row_t row;
    char cs[2], text[MSG_TEXT_MAX + 1], title[8];
    if (!MessageSelected(&row)) {
        msgOpen = false;
        return;
    }
    const uint8_t len = MSG_ReadText(row.slot, cs, text);
    memcpy(title, "FROM ", 5);
    memcpy(title + 5, cs, 2);
    title[7] = 0;
    // FROM AB at the top left, in the status line as the page titles.
    memset(gFrameBuffer, 0, sizeof gFrameBuffer);
    UI_PrintStringSmallBold(title, 1, 0, 0);
    memcpy(gStatusLine, gFrameBuffer[0], sizeof gStatusLine);
    memset(gFrameBuffer[0], 0, sizeof gFrameBuffer[0]);
    // Up to four lines of 18 characters, left aligned, from the second line
    // as in FMessage.
    const uint8_t lines = len ? (len + 17u) / 18u : 1u;
    for (uint8_t line = 0; line < lines; ++line) {
        char part[19];
        memcpy(part, text + line * 18u, 18);
        part[18] = 0;
        UI_PrintStringSmallNormal(part, 1, 0, 1u + line);
    }
    GUI_DisplaySmallest("6:DEL", 1, 50, false, true);
    GUI_DisplaySmallest("EXIT:BACK", 92, 50, false, true);
}

// DELETE? or CLEAR ALL? over the list or the message: the frame of the keys
// popups.
static NATIVE_CODE(messages) void RenderAskNative(void)
{
    UI_DrawPopupFrame(14, 12, 114, 40, 55);
    UI_PrintStringSmallBold(msgAsk == MSG_ASK_CLEAR ? "CLEAR ALL ?" : "DELETE ?", 15, 112, 2);
    UI_DrawText5x5("M:YES", 18, 31, 5);
    UI_DrawText5x5("EXIT:NO", 69, 31, 7);
}

static NATIVE_CODE(messages) void RenderMessagesNative(void)
{
    if (msgOpen) {
        RenderMessageNative();
        if (msgOpen) {
            if (msgAsk)
                RenderAskNative();
            return;
        }
    }
    const uint8_t top = HistoryListTop(), lines = HistoryListLines();
    MSG_Row_t rows[MAX_VISIBLE_LINES + 1];
    uint16_t ordinal = 0, messages = 0;
    char text[24];
    msgCount = MSG_Fetch(0, NULL, 0, 0xFFFF, NULL, NULL);
    if (msgIndex >= msgCount) {
        msgIndex = msgCount ? msgCount - 1u : 0;
        msgStep = -1;
    }
    // The selection never rests on a marker: it moves on in the direction of
    // the last move. A marker always lies between two messages.
    for (uint8_t tries = 0; msgCount && tries < 2; ++tries) {
        MSG_Fetch(msgIndex, rows, 1, 0xFFFF, NULL, NULL);
        if (!rows[0].marker)
            break;
        msgIndex = msgStep > 0 ? msgIndex + 1u : msgIndex - 1u;
    }
    if (msgIndex < msgScroll)
        msgScroll = msgIndex;
    else if (msgIndex >= msgScroll + lines)
        msgScroll = msgIndex - lines + 1u;
    if (!msgCount)
        msgIndex = msgScroll = 0;
    MSG_Fetch(msgScroll, rows, lines, msgIndex, &ordinal, &messages);

    snprintf(text, sizeof text, "%u/%u", ordinal, messages);
    DrawPageTitle("MESSAGES:", text);
    for (uint8_t r = 0; r < lines && msgScroll + r < msgCount; ++r) {
        const MSG_Row_t *m = &rows[r];
        const uint8_t y = top + r * 7;
        if (m->marker) {
            for (uint8_t px = 4; px < 124; px += 2)
                gFrameBuffer[(y + 2) / 8][px] |= 1u << ((y + 2) % 8);
        } else {
            // "01-AB: " and the first 14 characters.
            const uint8_t n = m->len < 14u ? m->len : 14u;
            if (m->number && m->number <= MSG_SHOWN_MAX)
                text[0] = '0' + m->number / 10u, text[1] = '0' + m->number % 10u;
            else
                text[0] = text[1] = '-';
            text[2] = '-', text[3] = m->cs[0], text[4] = m->cs[1], text[5] = ':', text[6] = ' ';
            memcpy(text + 7, m->text, n);
            UI_DrawText5x5(text, 2, y, 7 + n);
        }
        if (msgScroll + r == msgIndex)
            InvertHistoryRows(y, 1);
    }
    CHANNEL_DrawKeysInfos();
    if (msgAsk)
        RenderAskNative();
    else if (selectionHelp)
        CHANNEL_DrawHistoryHelp(true);
}
#endif

static void RenderHistoryList(void)
{
#ifdef ENABLE_FALCON_MESSAGES
    const bool messages = historyMessages;
    if (APP_NativeBegin(messages ? NATIVE_MESSAGES : NATIVE_LISTS)) {
        if (messages)
            RenderMessagesNative();
        else
            RenderHistoryListNative();
        APP_NativeEnd();
    }
#else
    if (APP_NativeBegin(NATIVE_LISTS)) {
        RenderHistoryListNative();
        APP_NativeEnd();
    }
#endif
    ST7565_BlitFullScreen();
    if (HistoryOwnsStatus() || selectionHelp) // the history help rises over it
        ST7565_BlitStatusLine();
}


// Simplex settings of a frequency: those of its memory channel when one
// exists, otherwise the history profile. Profile UINT8_MAX keeps the draft's
// own modulation, bandwidth, step and codes.
static void FrequencyDraft(VFO_Info_t *draft, uint32_t f, uint8_t profile, uint16_t css)
{
    draft->freq_config_RX.Frequency = draft->freq_config_TX.Frequency = f;
    draft->pRX = &draft->freq_config_RX;
    draft->pTX = &draft->freq_config_TX;
    draft->TX_OFFSET_FREQUENCY = 0;
    draft->TX_OFFSET_FREQUENCY_DIRECTION = 0;
    draft->FrequencyReverse = false;
    const uint16_t channel = BOARD_gMR_fetchChannel(f);
    if (channel < MR_CHANNELS_MAX - MAX_BANDS) {
        const uint16_t parameters = ReadScanParameters(channel);
        draft->Modulation = (uint8_t)parameters;
        draft->STEP_SETTING = (parameters >> 8) & 255;
        uint8_t width;
        PY25Q16_ReadBuffer(ADRESS_CHANNELS + (uint32_t)channel * 16 + 12, &width, 1);
        draft->CHANNEL_BANDWIDTH = !!(width & 2);
    } else if (profile != UINT8_MAX) {
        draft->Modulation = profile & 3;
        draft->STEP_SETTING = settings.scanStepIndex;
        draft->CHANNEL_BANDWIDTH = draft->Modulation == MODULATION_AM ? profile >> 2 : !!(profile >> 2);
    }
    draft->Band = FREQUENCY_GetBand(f);
    if (profile != UINT8_MAX) {
        draft->freq_config_RX.CodeType = css >> 8;
        draft->freq_config_RX.Code = (uint8_t)css;
        draft->freq_config_TX.CodeType = draft->freq_config_RX.CodeType;
        draft->freq_config_TX.Code = draft->freq_config_RX.Code;
    }
}

// Frequency and settings of the selected history row (none on the MESSAGES
// page).
static __attribute__((noinline)) bool HistorySelection(uint32_t *frequency, uint8_t *profile, uint16_t *css)
{
    const uint16_t count = CountValidHistoryItems();
    uint16_t i = historyListIndex;
    *frequency = i < count ? GetHistoryFreq(i) : 0;
    *profile = UINT8_MAX;
    *css = 0;
#ifdef ENABLE_FALCON_MESSAGES
    if (historyMessages)
        return false; // a message has no frequency
#endif
    if (i < count) {
        *profile = HistoryProfile(i);
        *css = HistoryCode(i);
    }
    return *frequency != 0;
}

// M long: the selected row opens in the channel editor (both pages).
static void OpenHistoryChannel(void)
{
    uint32_t frequency;
    uint8_t profile;
    uint16_t css;
    if (!HistorySelection(&frequency, &profile, &css))
        return;
    VFO_Info_t draft = *gTxVfo;
    FrequencyDraft(&draft, frequency, profile, css);
    CHANNEL_OpenHistory(&draft);
}

// Long 1: the selected row goes to the main VFO, which then takes over.
static __attribute__((noinline)) void HistoryToVfo(void)
{
    if (!HistorySelection(&toVfoFrequency, &toVfoProfile, &toVfoCss))
        return;
    HistoryClose();
    DeInitSpectrum();
}
