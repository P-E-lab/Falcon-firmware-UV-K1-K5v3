/* Copyright 2026 Armel F4HWN
 * https://github.com/armel
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

#include "apps/app_overlay.h"
#include "apps/app_config_proxy.h"

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_APPS

#include <string.h>
#include <stddef.h> /* offsetof */
#include "py32f0xx.h"

#include "driver/bk4819.h"
#include "driver/bk4819-regs.h"
#ifdef ENABLE_FMRADIO
#include "driver/bk1080.h"
#include "app/fm.h"
#endif
#include "driver/keyboard.h"
#include "driver/mb_flash.h"
#include "driver/py25q16.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "driver/backlight.h"
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
#include "k5viewer.h"
#endif
#include "app/app.h"
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
#include "app/rxtx_log.h"
#endif
#ifdef ENABLE_FALCON_MESSAGES
#include "app/messages.h"
#endif
#include "ui/helper.h"
#include "ui/main.h"
#include "ui/status.h"
#include "board.h"
#include "driver/gpio.h"
#include "ui/battery.h"
#include "app/spectrum.h"
#include "app/uart.h"
#include "driver/vcp.h"
#define APP_SERIAL 1
#include "nav_invert.h"
#include "dcs.h"
#include "functions.h"
#include "frequencies.h"
#include "radio.h"
#include "scheduler.h"
#include "helper/battery.h"
#include "settings.h"
#include "misc.h" /* dBmCorrTable */
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_INFO
#include "version.h"
#include "store.h"
#include "stack_usage.h"

#endif

_Static_assert(sizeof(app_header_t) == 64u && _Alignof(app_header_t) == 4u,
               "app_header_t must stay 64 bytes with word alignment");
_Static_assert(offsetof(app_header_t, magic) == 0u && offsetof(app_header_t, hdr_version) == 4u &&
                   offsetof(app_header_t, abi_major) == 6u && offsetof(app_header_t, api_min) == 7u &&
                   offsetof(app_header_t, code_size) == 8u && offsetof(app_header_t, code_crc32) == 12u &&
                   offsetof(app_header_t, entry_off) == 16u && offsetof(app_header_t, flags) == 18u &&
                   offsetof(app_header_t, name) == 20u && offsetof(app_header_t, version) == 36u &&
                   offsetof(app_header_t, link_vma) == 52u && offsetof(app_header_t, required_caps) == 56u &&
                   offsetof(app_header_t, asset_size) == 60u && offsetof(app_header_t, asset_crc) == 62u,
               "FAP1 field offsets must match existing apps and host tools");
_Static_assert(sizeof(app_api_t) <= UINT16_MAX, "app_api_t size field overflow");
_Static_assert(APP_ASSET_OFFSET + APP_ASSET_MAX == APP_CODE_OFFSET,
               "assets must end where the code sector starts");
_Static_assert(APP_ASSET_MAX <= APP_OVERLAY_MAX, "assets are CRC-checked through the overlay buffer");
_Static_assert(APP_SCREEN_SAVE_LEN == sizeof(gStatusLine) + sizeof(gFrameBuffer),
               "the saved screen is the status line plus the frame buffer");
_Static_assert(APP_SLOT_BASE(APP_SLOT_COUNT) <= 0x001DD000u,
               "overlay app slots overlap the migration journal or Sonic 4K reserve");

enum {
    APP_AVAILABLE_CAPS = APP_CAP_CFG_PROXY | APP_CAP_BEAM2 | APP_CAP_SERIAL
#ifdef ENABLE_FMRADIO
                         | APP_CAP_FM
#endif
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_INFO
                         | APP_CAP_SYSINFO
#endif
};

/* Keep the small zero-initialized state together so callbacks can address it
 * from one base. The nonzero RNG seed stays separate to keep this in .bss. */
#define APP_SHORTCUT_COUNT 6u /* positions used by app_shortcut_index() */

static struct {
    uint16_t asset_size;
    uint16_t cfg_target_offset;
    uint32_t cfg_target_magic;
    uint8_t run_slot;
    uint8_t cfg_len; /* staged length; 0 = nothing to commit */
    uint8_t cfg_target_slot;
    uint8_t cfg_proxy_len;
    bool cfg_proxy;
    bool allow_screen_saver;
    bool screen_saver_wake;
    bool shortcut_launch; /* launched by a held F1, F2 or M */
#ifdef ENABLE_FMRADIO
    bool fm_dirty;
#endif
    bool shortcuts_cached;
    uint8_t shortcut_slots[APP_SHORTCUT_COUNT]; /* per position, 0xFF = none */
    uint8_t slot_revision;
    uint8_t handoff_next, handoff_from, handoff_len;
    bool handoff_unread;
    uint32_t serial_read_total;
    uint8_t serial_evt, serial_enum;
    bool serial_used;
    uint8_t handoff_buf[APP_HANDOFF_MAX];
    uint8_t cfg_buf[APP_CONFIG_PROXY_MAX];
} app_state;

/* The 4 KiB overlay RAM (the SPI flash sector cache) runs either an app or the
 * native code modules. An app cannot be reloaded, so nothing may disturb the
 * overlay while it runs. Native modules are read-only images kept in the
 * external store: whatever reuses the overlay RAM meanwhile (a flash write, an
 * app, another module) simply reloads the active module before returning to
 * it, so modules may write the flash and call each other. */
#define NATIVE_NESTING_MAX 6u
static uint8_t loaded_native;                    /* module image in RAM, 0 = none */
static uint8_t native_stack[NATIVE_NESTING_MAX]; /* active modules, innermost last */
static uint8_t native_depth;
static bool app_running;

bool APP_OverlayBusy(void)
{
    return app_running;
}

bool APP_NativeActive(void)
{
    return native_depth != 0;
}

void APP_InvalidateNative(void)
{
    loaded_native = 0;
}

static bool NativeLoad(uint8_t id)
{
    PY25Q16_EndBatch();
    PY25Q16_InvalidateCache(); /* the overlay RAM is the sector cache */
    if (!STORE_LoadModule(id, PY25Q16_OverlayBuffer()))
        return false;
    __DSB();
    __ISB();
    loaded_native = id;
    return true;
}

/* Called after the overlay RAM was reused: put the active module back before
 * execution returns into it. Its code cannot be missing at that point, so a
 * store that can no longer be read restarts the radio. */
void APP_NativeResume(void)
{
    if (!native_depth || loaded_native == native_stack[native_depth - 1])
        return;
    for (uint8_t attempt = 0; attempt < 3; attempt++)
        if (NativeLoad(native_stack[native_depth - 1]))
            return;
    NVIC_SystemReset();
}
static void app_draw_rect(app_fb_t fb, int16_t x1, int16_t y1, int16_t x2, int16_t y2, bool black)
{
    UI_DrawLineBuffer(fb, x1, y1, x2, y1, black);
    UI_DrawLineBuffer(fb, x1, y2, x2, y2, black);
    UI_DrawLineBuffer(fb, x1, y1, x1, y2, black);
    UI_DrawLineBuffer(fb, x2, y1, x2, y2, black);
}
static void app_audio_scope(uint8_t line, bool active)
{
    if (active && line < 7)
        UI_DrawTxAudioBar(line);
}
extern uint8_t _ebss, __app_stack_floor;
/* Stack left to an app: down to the scan tables it may borrow. */
static uint32_t app_stack_now(void)
{
    uint32_t sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    const uint32_t floor = (uint32_t)&__app_stack_floor;
    return sp > floor ? sp - floor : 0;
}
static VFO_Info_t beam_vfo;
static app_beam_channel_t beam_pending;
static uint16_t beam_slot;
static bool beam_dirty;
bool gAppVfoExit;
uint8_t gAppLastSlot;

/* ---- ABI wrappers: the few resident calls that are not a direct signature match ---- */

static void app_backlight_on(void)
{
    APP_ModalScreenSaverExit();
    BACKLIGHT_TurnOn();
}

static void app_backlight_update(void)
{
    APP_ModalBacklightTick(app_state.allow_screen_saver);
}

static uint8_t app_get_key(void)
{
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    /* Overlay apps run synchronously outside APP_Update(). Keep serial key
     * injection alive while an app owns the foreground loop. */
    K5VIEWER_ParseInput();
#endif
    if (!app_state.serial_used)
        UART_ServiceCommands();
    KEY_Code_t key = KEYBOARD_GetKey();
    if (gSetting_nav_invert && (key == KEY_UP || key == KEY_DOWN))
        key = key == KEY_UP ? KEY_DOWN : KEY_UP;

    if (app_state.screen_saver_wake) {
        if (key == KEY_INVALID)
            app_state.screen_saver_wake = false;
        return APP_KEY_INVALID;
    }

    if (!APP_IsScreenSaverDisplayed()) {
        if (key != KEY_INVALID)
            BACKLIGHT_TurnOn(); /* re-arm BLTime on activity, mirrors ProcessKey():
                                     overlay apps bypass the resident key handler, so
                                     the saver would otherwise fire mid-use. */
        return (uint8_t)key;
    }

    if (key == KEY_INVALID)
        return APP_KEY_SAVER;

    app_backlight_on();
    if (key == KEY_PTT)
        return APP_KEY_PTT;

    app_state.screen_saver_wake = true;
    return APP_KEY_WAKE;
}

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
static void app_blit_full(void)
{
    ST7565_BlitFullScreen();
    /* The normal loop mirrors completed frames after drawing. Overlay apps
     * bypass that loop, so publish the frame from this ABI wrapper. */
    K5VIEWER_Update(false);
}
#endif

/* API level 3: a K5Viewer frame without the LCD blit (lcd_feed's apps). */
static void app_viewer_update(void)
{
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    K5VIEWER_Update(false);
#endif
}

#ifdef APP_SERIAL
/* ---- API level 3: serial ports ----
 * USB only for now: APP_SERIAL_KPLUG reports no port (serial_ctl 0).  The
 * first call drops what the main loop left unread, takes the current USB
 * counters as the reference and holds the K5Viewer lock until the app returns
 * (K5VIEWER_LOCK_HELD: the countdown CHIRP uses, frozen), so K5Viewer neither
 * reads the app's bytes as viewer commands nor mixes screen frames into the
 * app's stream, however rarely the app polls the port. */
#define APP_SERIAL_WRITE_CYC (48000000u / 20u) /* 50 ms for the host to take a write */

_Static_assert(sizeof(VCP_RxBuf) == 256u, "the ring indexes wrap as uint8_t");

static uint32_t app_cycles_raw(void);

/* Drop the unread bytes and take the receive total at the new read position,
 * with the USB interrupt held off: a packet stored in between would otherwise
 * be counted on the wrong side.  The ISR may leave the write index at the ring
 * size before wrapping it, hence uint8_t. */
static void app_serial_flush(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    VCP_ReadIndex = (uint8_t)VCP_RxBufPointer;
    app_state.serial_read_total = cdc_acm_rx_total;
    __set_PRIMASK(primask);
}

static bool app_serial_claim(uint8_t port)
{
    if (port != APP_SERIAL_USB)
        return false;
    if (!app_state.serial_used) {
        app_state.serial_used = true;
        app_serial_flush();
        app_state.serial_enum = cdc_acm_config_count;
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
        gUART_LockK5Viewer = K5VIEWER_LOCK_HELD;
#endif
    }
    return true;
}

static uint16_t app_serial_read(uint8_t port, void *buf, uint16_t len)
{
    if (!app_serial_claim(port))
        return 0;
    /* 256 bytes or more since the app's position: the ISR lapped it. */
    const uint32_t total = cdc_acm_rx_total;
    if (total - app_state.serial_read_total >= sizeof(VCP_RxBuf)) {
        app_serial_flush();
        app_state.serial_evt |= APP_SERIAL_LOST;
    }
    /* From the command parser's own read index, so the bytes an app consumed
     * are never parsed as commands once the main loop resumes. */
    const uint8_t w = (uint8_t)VCP_RxBufPointer;
    uint8_t r = (uint8_t)VCP_ReadIndex;
    uint8_t *p = buf;
    uint16_t n = 0;
    while (n < len && r != w)
        p[n++] = VCP_RxBuf[r++];
    VCP_ReadIndex = r;
    app_state.serial_read_total += n;
    return n;
}

static bool app_serial_write(uint8_t port, const void *buf, uint16_t len)
{
    if (!app_serial_claim(port) || !dtr_enable)
        return false;
    if (!len)
        return true;
    if (cdc_acm_start_send(buf, len)) {
        const uint32_t t0 = app_cycles_raw();
        while (ep_tx_busy_flag)
            if (app_cycles_raw() - t0 >= APP_SERIAL_WRITE_CYC)
                goto timeout;
        return true;
    }
timeout:
    cdc_acm_cancel_send();
    app_state.serial_evt |= APP_SERIAL_TIMEOUT;
    return false;
}

static uint8_t app_serial_ctl(uint8_t port, uint32_t baud)
{
    (void)baud;
    if (!app_serial_claim(port))
        return 0;
    uint8_t st = APP_SERIAL_OK | app_state.serial_evt;
    app_state.serial_evt = 0;
    if (dtr_enable)
        st |= APP_SERIAL_HOST;
    const uint8_t c = cdc_acm_config_count;
    if (c != app_state.serial_enum) {
        app_state.serial_enum = c;
        st |= APP_SERIAL_ENUM;
    }
    return st;
}

/* After the app: its half-read frames must not reach the command parser or
 * K5Viewer once the main loop resumes. */
static void app_serial_release(void)
{
    if (!app_state.serial_used)
        return;
    app_state.serial_used = false;
    app_state.serial_evt = 0;
    app_serial_flush();
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    VCP_K5ViewerSync();
    gUART_LockK5Viewer = 0;
#endif
}
#endif


static int8_t app_nav_dir(uint8_t key)
{
    int8_t direction;

    if (key == KEY_UP)
        direction = 1;
    else if (key == KEY_DOWN)
        direction = -1;
    else
        return 0;

    return gEeprom.SET_NAV ? direction : -direction;
}

#ifdef ENABLE_FEAT_F4HWN_OVERLAY_INFO
extern uint8_t _eflash_used;
extern uint8_t _ebss;
#endif
static void app_led(bool on)
{
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on);
}

static void app_play_tone(uint16_t tone, uint16_t ms)
{
    BK4819_PrepareToPlayTone(true);
    GPIO_EnableAudioPath();
    BK4819_PlayToneRaw(tone, ms);
    GPIO_DisableAudioPath();
}

static void app_beam_prepare(void)
{
    RADIO_InitInfo(&beam_vfo, FREQ_CHANNEL, DEFAULT_FREQ);
    beam_vfo.CHANNEL_BANDWIDTH = BANDWIDTH_NARROW;
    beam_vfo.OUTPUT_POWER = OUTPUT_POWER_LOW;
    RADIO_ConfigureSquelchAndOutputPower(&beam_vfo);
    gRxVfo = gTxVfo = gCurrentVfo = &beam_vfo;
    RADIO_SetupRegisters(true);
}
#define BEAM_BYTES(F)                                                                                        \
    F(rx_code, freq_config_RX.Code)                                                                          \
    F(tx_code, freq_config_TX.Code)                                                                          \
    F(tx_offset_direction, TX_OFFSET_FREQUENCY_DIRECTION) F(busy_channel_lock, BUSY_CHANNEL_LOCK)            \
        F(channel_bandwidth, CHANNEL_BANDWIDTH) F(scanlist, SCANLIST_PARTICIPATION) F(compander, Compander)
typedef struct {
    uint8_t wire, vfo;
} beam_field_t;
#define BEAM_ROW(w, v) {offsetof(app_beam_channel_t, w), offsetof(VFO_Info_t, v)},
static const beam_field_t beam_fields[] = {BEAM_BYTES(BEAM_ROW)};
#undef BEAM_ROW
#define BEAM_CHECK(w, v) _Static_assert(sizeof(((VFO_Info_t *)0)->v) == 1, #v);
BEAM_BYTES(BEAM_CHECK)
#undef BEAM_CHECK
static void beam_copy(app_beam_channel_t *wire, VFO_Info_t *vfo, bool to_vfo)
{
    for (unsigned i = 0; i < ARRAY_SIZE(beam_fields); ++i) {
        uint8_t *w = (uint8_t *)wire + beam_fields[i].wire;
        uint8_t *v = (uint8_t *)vfo + beam_fields[i].vfo;
        if (to_vfo)
            *v = *w;
        else
            *w = *v;
    }
}
/* F4HWN wire IDs differ from Falcon EEPROM IDs. Keep translation in the
 * bridge so applications and over-air BEAM payloads use the published ABI. */
static const uint8_t beam_steps[] = {STEP_2_5kHz,  STEP_5kHz,    STEP_6_25kHz, STEP_10kHz,   STEP_12_5kHz,
                                     STEP_25kHz,   STEP_8_33kHz, STEP_0_01kHz, STEP_0_05kHz, STEP_0_1kHz,
                                     STEP_0_25kHz, STEP_0_5kHz,  STEP_1kHz,    STEP_1_25kHz, STEP_9kHz,
                                     STEP_15kHz,   STEP_20kHz,   STEP_30kHz,   STEP_50kHz,   STEP_100kHz,
                                     STEP_125kHz,  STEP_200kHz,  STEP_250kHz,  STEP_500kHz};
static void app_beam_get(app_beam_channel_t *out)
{
    memset(out, 0, sizeof *out);
    VFO_Info_t *v = &gEeprom.VfoInfo[gEeprom.TX_VFO];
    beam_copy(out, v, false);
    out->rx_frequency = v->freq_config_RX.Frequency;
    out->tx_offset_frequency = v->TX_OFFSET_FREQUENCY;
    out->rx_codetype = v->freq_config_RX.CodeType;
    out->tx_codetype = v->freq_config_TX.CodeType;
    out->modulation = v->Modulation;
    out->frequency_reverse = v->FrequencyReverse;
    out->step_setting = 4; // published 12.5 kHz default
    for (unsigned i = 0; i < ARRAY_SIZE(beam_steps); ++i)
        if (beam_steps[i] == v->STEP_SETTING)
            out->step_setting = i;
    out->output_power = POWER_ToIndex(v->OUTPUT_POWER) + 1u;
    out->band = v->Band;
    memcpy(out->name, v->Name, sizeof out->name);
}
static uint16_t app_beam_save(const app_beam_channel_t *in)
{
    if (!in || beam_dirty || RX_freq_check(in->rx_frequency) < 0 || in->modulation >= MODULATION_UKNOWN ||
        in->rx_codetype > 3 || in->tx_codetype > 3)
        return UINT16_MAX;
    uint16_t ch = MR_CHANNEL_FIRST;
    while (ch < MR_CHANNELS_MAX - MAX_BANDS && RADIO_CheckValidChannel(ch, false, 0))
        ++ch;
    if (ch >= MR_CHANNELS_MAX - MAX_BANDS)
        return UINT16_MAX;
    beam_pending = *in;
    beam_slot = ch;
    beam_dirty = true;
    return ch;
}
static bool app_vfo_stage(const app_beam_channel_t *in)
{
    if (in == NULL || beam_dirty)
        return false;
    if (RX_freq_check(in->rx_frequency)) /* the radio's RX range, as the keypad */
        return false;
    memcpy(&beam_pending, in, sizeof(beam_pending));
    beam_slot = FREQ_CHANNEL;
    beam_dirty = true;
    gAppVfoExit = true;
    return true;
}

static void app_beam_commit(void)
{
    if (!beam_dirty)
        return;
    beam_dirty = false;
    RADIO_InitInfo(&beam_vfo, beam_slot, beam_pending.rx_frequency);
    beam_copy(&beam_pending, &beam_vfo, true);
    beam_vfo.freq_config_RX.Frequency = beam_pending.rx_frequency;
    beam_vfo.TX_OFFSET_FREQUENCY = beam_pending.tx_offset_frequency;
    beam_vfo.freq_config_RX.CodeType = beam_pending.rx_codetype;
    beam_vfo.freq_config_TX.CodeType = beam_pending.tx_codetype;
    beam_vfo.Modulation = beam_pending.modulation;
    beam_vfo.FrequencyReverse = beam_pending.frequency_reverse;
    beam_vfo.STEP_SETTING = beam_pending.step_setting < ARRAY_SIZE(beam_steps)
                                ? beam_steps[beam_pending.step_setting]
                                : STEP_12_5kHz;
    beam_vfo.OUTPUT_POWER = POWER_FromIndex(beam_pending.output_power >= 1 && beam_pending.output_power <= 7
                                                ? beam_pending.output_power - 1u
                                                : 0u);
    beam_vfo.StepFrequency = gStepFrequencyTable[beam_vfo.STEP_SETTING];
    beam_vfo.SCANLIST_PARTICIPATION = beam_pending.scanlist <= MR_CHANNELS_LIST ? beam_pending.scanlist : 0;
    beam_vfo.CHANNEL_BANDWIDTH = !!beam_pending.channel_bandwidth;
    beam_vfo.Compander = beam_pending.compander & 3;
    beam_vfo.Band = FREQUENCY_GetBand(beam_pending.rx_frequency);
    memcpy(beam_vfo.Name, beam_pending.name, sizeof beam_vfo.Name);
    beam_vfo.Name[sizeof beam_vfo.Name - 1] = 0;
    beam_vfo.ExactFrequency = (beam_slot == FREQ_CHANNEL);
    SETTINGS_SaveChannel(beam_slot, gEeprom.TX_VFO, &beam_vfo, 3);
    if (beam_slot == FREQ_CHANNEL)
        gEeprom.FreqChannel[gEeprom.TX_VFO] = FREQ_CHANNEL;
    else
        gEeprom.MrChannel[gEeprom.TX_VFO] = beam_slot;
    gEeprom.ScreenChannel[gEeprom.TX_VFO] = beam_slot;
    RADIO_ConfigureChannel(gEeprom.TX_VFO, VFO_CONFIGURE_RELOAD);
    RADIO_SelectVfos();
    RADIO_SetupRegisters(true);
    SETTINGS_SaveVfoIndices();
}
static void app_draw_frequency(uint32_t f_10hz, uint8_t x, uint8_t line);
static void app_beam_draw(const char *text)
{
    /* Modal app code owns the shared overlay. Draw with resident services. */
    UI_DisplayClear();
    UI_DisplayStatus();
    app_draw_frequency(gRxVfo->pRX->Frequency, 0, 1);
    UI_PrintStringSmallBold(text, 2, 127, 5);
}

/* ---- radio wrappers ---- */
static int16_t app_rssi_dbm(void)
{
    return BK4819_GetRSSI_dBm() + dBmCorrTable[gRxVfo->Band];
}
static uint16_t app_bk_read(uint8_t r)
{
    return BK4819_ReadRegister((BK4819_REGISTER_t)r);
}
static void app_bk_write(uint8_t r, uint16_t v)
{
    BK4819_WriteRegister((BK4819_REGISTER_t)r, v);
}
static void app_set_af(uint8_t m)
{
    BK4819_SetAF((BK4819_AF_Type_t)m);
}
static void app_audio_path(bool on)
{
    if (on)
        GPIO_EnableAudioPath();
    else
        GPIO_DisableAudioPath();
}
static void app_prepare_tone(void)
{
    BK4819_PrepareToPlayTone(true);
}
static void app_play_tone_raw(uint16_t hz, uint16_t ms)
{
    BK4819_PlayToneRaw(hz, ms);
}
static uint32_t app_rx_freq(void)
{
    return gRxVfo->pRX->Frequency;
}

/* ---- v2 config (deferred, flash-backed) ----
 * Stored per app slot in the header sector, just after the 64-byte header. cfg_load
 * reads flash at launch (ReadBuffer bypasses the overlay cache). cfg_save only stages
 * into RAM - the app runs from the sector cache, so it cannot write flash itself; the
 * loader commits the staged bytes to flash after the app returns (RMW preserves the
 * slot header), tagged with the app's name. Updating an app erases its slot: the
 * tagged config is kept across that erase and handed back to the app of that name
 * only, so an update keeps the app's settings while another app installed in the
 * slot starts from its own defaults. */
#define APP_CFG_OFFSET 0x40u /* config area within the header sector */
#define APP_CFG_OWNER  0x50u /* name of the app the config belongs to */

static void app_cfg_load(uint8_t *buf, uint8_t len)
{
    if (app_state.cfg_proxy) {
        const uint8_t requested = len;
        if (app_state.cfg_target_slot < APP_SLOT_COUNT && len <= app_state.cfg_proxy_len) {
            PY25Q16_ReadBuffer(APP_SLOT_BASE(app_state.cfg_target_slot) + APP_ASSET_OFFSET +
                                   app_state.cfg_target_offset,
                               buf, len);
        } else {
            memset(buf, 0xFF, requested);
        }
        return;
    }

    if (len > APP_CONFIG_LOCAL_MAX)
        len = APP_CONFIG_LOCAL_MAX;
    const uint32_t base = APP_SLOT_BASE(app_state.run_slot);
    char name[APP_NAME_LEN], owner[APP_NAME_LEN];
    PY25Q16_ReadBuffer(base + offsetof(app_header_t, name), name, sizeof(name));
    PY25Q16_ReadBuffer(base + APP_CFG_OWNER, owner, sizeof(owner));
    /* This app's config, or an untagged one saved before the tag existed.
     * Another app's reads as erased flash, i.e. the app's defaults. */
    if ((uint8_t)owner[0] == 0xFFu || !memcmp(owner, name, sizeof(name)))
        PY25Q16_ReadBuffer(base + APP_CFG_OFFSET, buf, len);
    else
        memset(buf, 0xFF, len);
}
static void app_cfg_save(const uint8_t *buf, uint8_t len)
{
    if (app_state.cfg_proxy) {
        uint32_t magic;
        if (app_state.cfg_target_slot >= APP_SLOT_COUNT || len != app_state.cfg_proxy_len ||
            len < sizeof(magic))
            return;
        memcpy(&magic, buf, sizeof(magic));
        if (magic != app_state.cfg_target_magic)
            return;
    } else if (len > APP_CONFIG_LOCAL_MAX) {
        len = APP_CONFIG_LOCAL_MAX;
    }
    memcpy(app_state.cfg_buf, buf, len);
    app_state.cfg_len = len; /* mark dirty; the loader commits after the app returns */
}

_Static_assert(APP_CFG_OFFSET + APP_CONFIG_LOCAL_MAX <= APP_CFG_OWNER &&
                   APP_CFG_OWNER + APP_NAME_LEN <= APP_ASSET_OFFSET,
               "config area overlaps its owner tag or the assets");

/* ---- API level 2: time, randomness, read-only assets ---- */
static uint32_t app_rng_state = 0x2545F491u;

static uint32_t app_ticks_ms(void)
{
    return gGlobalSysTickCounter * 10u;
}

static uint32_t app_rand32(void)
{
    uint32_t x = app_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    app_rng_state = x;
    return x;
}

/* Fold fresh entropy into the persistent state: RSSI noise LSBs, the SysTick
 * phase of the key press that launched the app, and the 10 ms counter. */
static void app_rng_mix(void)
{
    app_rng_state ^=
        ((uint32_t)BK4819_ReadRegister(BK4819_REG_67) << 16) ^ SysTick->VAL ^ gGlobalSysTickCounter;
    if (app_rng_state == 0u)
        app_rng_state = 0x2545F491u;
    app_rand32();
}

static uint16_t app_asset_read(uint16_t offset, void *buf, uint16_t len)
{
    if (offset >= app_state.asset_size)
        return 0;
    if (len > app_state.asset_size - offset)
        len = app_state.asset_size - offset;
    PY25Q16_ReadBuffer(APP_SLOT_BASE(app_state.run_slot) + APP_ASSET_OFFSET + offset, buf, len);
    return len;
}

/* ---- API level 2: integer division ----
 * The run-time helpers the firmware links anyway (App/compact_div.S over
 * libgcc's unsigned division), served as they are: their {quotient, remainder}
 * register pair is declared as one 64-bit return value (low word = r0). */
extern uint64_t __aeabi_idivmod(int32_t n, int32_t d);
extern uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d);

/* ---- v2 battery / backlight ---- */
/* ---- API level 3: number formatting ----
 * One resident copy of the decimal formatter nearly every app carried: the
 * digits come out least significant first, one __aeabi_uidivmod each (GCC
 * would call the divide twice for u % 10 and u / 10), then are copied back in
 * order. Not NUL-terminated, returns the end. */
static char *app_append_u32(char *dst, uint32_t u)
{
    char digits[10];
    char *p = digits + sizeof(digits);
    do {
        const uint64_t qr = __aeabi_uidivmod(u, 10u);
        *--p = (char)('0' + (uint32_t)(qr >> 32));
        u = (uint32_t)qr;
    } while (u);
    do
        *dst++ = *p++;
    while (p < digits + sizeof(digits));
    return dst;
}

static char *app_append_i32(char *dst, int32_t v)
{
    uint32_t u = (uint32_t)v;
    if (v < 0) {
        *dst++ = '-';
        u = 0u - u;
    }
    return app_append_u32(dst, u);
}

/* ---- API level 3: the frequency as the main screen draws it ----
 * ui/main.c with ENABLE_BIG_FREQ: below 1 GHz, "144.800" in the big digits,
 * then the last two digits ("00") in the small font on the lower line, right
 * after them (95 px in all); from 1 GHz, the whole string in the normal font
 * (80 px). The drawing routines do not clip, so x is held where both fit. */
#define APP_FREQ_X_MAX (128u - 95u)

static void app_draw_frequency(uint32_t f_10hz, uint8_t x, uint8_t line)
{
    char s[16];
    UI_FormatFrequency(s, f_10hz);
    char *o = s + strlen(s) - 2; /* the last two digits */
    if (x > APP_FREQ_X_MAX)
        x = APP_FREQ_X_MAX;
    if (f_10hz >= 100000000u) {
        UI_PrintString(s, x, 0, line, 8);
        return;
    }
    UI_PrintStringSmallNormal(o, (uint8_t)(x + (o - s - 1) * 13 + 3), 0, (uint8_t)(line + 1u));
    *o = '\0';
    UI_DisplayFrequency(s, x, line, false);
}

/* ---- API level 3: RX audio capture on PA4 ----
 * The sequence the PA4 apps proved on the radio (EPIRB 406 first): PA4 has no
 * DC reference of its own, so the MCU DAC (channel 1, output buffer off,
 * software trigger) holds it at mid-scale for the whole capture; the ADC reads
 * channel 4 with the sample time the battery channel 8 was given. One app
 * runs at a time, so the snapshot lives here instead of in each overlay. */
#define APP_AFC_ADC_CH_PA4 4u
#define APP_AFC_SMP4_POS   12u
#define APP_AFC_SMP8_POS   24u
#define APP_AFC_DAC_BIAS   ((1u << 0) | (1u << 1) | (1u << 2) | (7u << 3)) /* EN1 BOFF1 TEN1 TSEL1=111 */
#define APP_AFC_BIAS_CODE  2048u

static struct {
    uint32_t moder, rcc, dac, dhr, sqr3, smpr3;
    bool open;
} app_afc;

static void app_afc_select_pa4(void)
{
    ADC1->SMPR3 = (app_afc.smpr3 & ~(7u << APP_AFC_SMP4_POS)) |
                  (((app_afc.smpr3 >> APP_AFC_SMP8_POS) & 7u) << APP_AFC_SMP4_POS);
    ADC1->SQR3 = (app_afc.sqr3 & ~0x1Fu) | APP_AFC_ADC_CH_PA4;
}

static void app_afc_select_battery(void)
{
    ADC1->SQR3 = app_afc.sqr3;
    ADC1->SMPR3 = app_afc.smpr3;
}

static void app_af_capture_open(void)
{
    if (app_afc.open)
        return;
    app_afc.moder = GPIOA->MODER;
    app_afc.rcc = RCC->APBENR1;
    app_afc.dac = DAC1->CR;
    app_afc.dhr = DAC1->DHR12R1;
    app_afc.sqr3 = ADC1->SQR3;
    app_afc.smpr3 = ADC1->SMPR3;
    app_afc.open = true;
    GPIOA->MODER = app_afc.moder | (3u << 8); /* PA4 analog */
    RCC->APBENR1 = app_afc.rcc | RCC_APBENR1_DACEN;
    DAC1->CR = APP_AFC_DAC_BIAS;
    DAC1->DHR12R1 = APP_AFC_BIAS_CODE;
    DAC1->SWTRIGR = 1u; /* DHR -> DOR */
    app_afc_select_pa4();
}

static uint16_t app_af_capture_sample(void)
{
    ADC1->CR2 |= ADC_CR2_SWSTART | ADC_CR2_EXTTRIG;
    for (uint16_t n = 0; !(ADC1->SR & ADC_SR_EOC) && n < 2000u; n++) {
    }
    return (uint16_t)(ADC1->DR & 0x0FFFu);
}

static void app_af_capture_close(void)
{
    if (!app_afc.open)
        return;
    app_afc.open = false;
    app_afc_select_battery();
    DAC1->CR = app_afc.dac;
    DAC1->DHR12R1 = app_afc.dhr;
    RCC->APBENR1 = app_afc.rcc;
    GPIOA->MODER = app_afc.moder;
}

/* ---- API level 3: 48 MHz cycle clock ----
 * Re-read the tick until it is stable around the SysTick read. A reload the
 * interrupt has not served yet leaves the tick one behind: then count it and
 * take a fresh, post-reload phase.
 * At every launch the count is set APP_CYC_DIAGNOSTIC_WRAP_S seconds before
 * its 32-bit wrap, as Linux starts jiffies before theirs: an app that compares
 * it without a signed difference fails a few seconds after it starts, every
 * run, instead of once in a while depending on radio uptime. */
#define APP_CYC_HZ                48000000u
#define APP_CYC_DIAGNOSTIC_WRAP_S 10u
_Static_assert(APP_CYC_DIAGNOSTIC_WRAP_S > 0u && APP_CYC_DIAGNOSTIC_WRAP_S <= INT32_MAX / APP_CYC_HZ,
               "the diagnostic wrap must fit the signed deadline window");
#define APP_CYC_DIAGNOSTIC_START (0u - APP_CYC_HZ * APP_CYC_DIAGNOSTIC_WRAP_S)
static uint32_t app_cyc_base;

static uint32_t app_cycles_raw(void)
{
    uint32_t tick, val;
    do {
        tick = gGlobalSysTickCounter;
        val = SysTick->VAL;
    } while (tick != gGlobalSysTickCounter);
    if (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) {
        val = SysTick->VAL;
        tick++;
    }
    return tick * (SysTick->LOAD + 1u) + (SysTick->LOAD - val);
}

static uint32_t app_cycles_48mhz(void)
{
    return app_cycles_raw() + app_cyc_base;
}


static void app_draw_battery(void)
{
    UI_DrawBattery(gStatusLine + 111, gBatteryDisplayLevel, false);
}
static void app_battery_sample(void)
{
    BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryVoltageIndex], &gBatteryCurrent);
    gBatteryVoltageIndex = (gBatteryVoltageIndex + 1) % ARRAY_SIZE(gBatteryVoltages);
    BATTERY_GetReadings(false);
}

bool APP_AfCaptureYieldAdc(void)
{
    if (!app_afc.open)
        return false;
    app_afc_select_battery();
    return true;
}
void APP_AfCaptureResumeAdc(void)
{
    app_afc_select_pa4();
}

/* ---- v2 TX (beacon) ---- */
static uint8_t app_tx_state(void)
{
    if (TX_freq_check(gTxVfo->pTX->Frequency) != 0)
        return 1; /* TX disable */
    if (gBatteryDisplayLevel == 0)
        return 2; /* battery low */
    if (gBatteryDisplayLevel > 6)
        return 3; /* voltage high */
    if (gTxVfo->Modulation != MODULATION_FM)
        return 1;
    return 0;
}
static void app_tx_tone(uint16_t hz)
{
    BK4819_TransmitTone(false, hz);
}
static void app_tx_mute(bool on)
{
    if (on)
        BK4819_EnterTxMute();
    else
        BK4819_ExitTxMute();
}
static void app_tx_end(void)
{
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, false);
    RADIO_SetupRegisters(true);
}
static void app_tx_carrier(bool on)
{
    BK4819_ToggleGpioOut(BK4819_GPIO1_PIN29_PA_ENABLE, on);
}
static uint32_t app_tx_freq(void)
{
    return gTxVfo->pTX->Frequency;
}
static void app_boot_callsign(char *buf, uint8_t len)
{
    char raw[12];
    uint8_t n = 0;
    PY25Q16_ReadBuffer(0x00A0C8u, raw, sizeof(raw));
    for (uint8_t i = 0; i < sizeof(raw) && (uint8_t)(n + 1) < len; i++) {
        char c = raw[i];
        if (c == '\0' || (uint8_t)c == 0xFFu)
            break;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '/')
            buf[n++] = c;
    }
    buf[n] = '\0';
}

#ifdef ENABLE_FMRADIO
/* ---- v2 broadcast FM (BK1080), sovereign (no BK4819 dual-watch) ---- */
static void app_fm_enter(uint16_t f, uint8_t b)
{
    BK1080_Init(f, b);
    BK4819_PickRXFilterPathBasedOnFrequency(10320000); /* FM band antenna filter */
    GPIO_EnableAudioPath();
    gEnableSpeaker = true;
}
static void app_fm_exit(void)
{
    GPIO_DisableAudioPath();
    gEnableSpeaker = false;
    BK1080_Init0();
    BK4819_PickRXFilterPathBasedOnFrequency(gRxVfo->pRX->Frequency); /* restore RX filter */
}
static int8_t app_fm_valid(uint16_t f, uint16_t lo)
{
    return (int8_t)FM_CheckFrequencyLock(f, lo);
}

static void app_fm_state(app_fm_state_t *s, bool write)
{
    if (write) {
        gEeprom.FM_FrequencyPlaying = s->freq_playing;
        gEeprom.FM_SelectedFrequency = s->sel_freq;
        gEeprom.FM_Band = s->band & 3u;
        gEeprom.FM_IsMrMode = s->is_mr ? true : false;
        gEeprom.FM_SelectedChannel = s->sel_ch;
    } else {
        s->freq_playing = gEeprom.FM_FrequencyPlaying;
        s->sel_freq = gEeprom.FM_SelectedFrequency;
        s->band = gEeprom.FM_Band;
        s->is_mr = gEeprom.FM_IsMrMode;
        s->sel_ch = gEeprom.FM_SelectedChannel;
    }
}
static void app_fm_commit(void)
{
    app_state.fm_dirty = true;
}
#endif

/* Internal callers discard the header on failure, so read directly into their
 * aligned object without another 64-byte stack object and copy. */
static uint8_t app_read_validated_header(uint8_t slot, app_header_t *h)
{
    if (slot >= APP_SLOT_COUNT)
        return APP_ERR_SLOT;

    PY25Q16_ReadBuffer(APP_SLOT_BASE(slot), h, sizeof(*h));

    if (h->magic != APP_MAGIC)
        return APP_ERR_MAGIC;
    if (h->hdr_version != APP_HDR_VERSION)
        return APP_ERR_MAGIC;
    if (h->abi_major != APP_ABI_MAJOR || h->api_min == 0u || h->api_min > APP_API_LEVEL)
        return APP_ERR_ABI;
    if (!(h->flags & APP_FLAG_COMMITTED))
        return APP_ERR_NOT_COMMITTED;
    if (h->required_caps & ~APP_AVAILABLE_CAPS)
        return APP_ERR_CAP;
    if (h->required_caps & APP_CAP_NATIVE)
        return APP_ERR_CAP;
    if (h->code_size < 2u || h->code_size > APP_OVERLAY_MAX ||
        (uint32_t)h->entry_off > h->code_size - 2u || /* leave room for a 2-byte Thumb insn */
        (h->entry_off & 1u) != 0u ||                  /* entry must be Thumb-aligned (even) */
        h->asset_size > APP_ASSET_MAX)
        return APP_ERR_SIZE;

    return APP_OK;
}

/* Resolve an optional configuration-proxy descriptor from the running app's
 * asset prefix.  The descriptor is declarative: resident code knows nothing
 * about APRS or any other editor/target pair.  The target may be the running
 * app itself: a block of its own assets then persists more than 16 bytes. */
static __attribute__((noinline)) void app_config_proxy_resolve(void)
{
    app_state.cfg_proxy = false;
    app_state.cfg_target_slot = 0xFFu;
    app_state.cfg_proxy_len = 0u;
    app_state.cfg_target_offset = 0u;
    app_state.cfg_target_magic = 0u;

    if (app_state.asset_size < sizeof(app_config_proxy_t))
        return;

    app_config_proxy_t proxy;
    PY25Q16_ReadBuffer(APP_SLOT_BASE(app_state.run_slot) + APP_ASSET_OFFSET, &proxy, sizeof(proxy));
    if (proxy.magic != APP_CONFIG_PROXY_MAGIC)
        return;

    /* Once the marker matches, never fall back to the editor's ordinary
     * config area: an invalid or missing target must remain a read-only error. */
    app_state.cfg_proxy = true;
    if (proxy.reserved != 0u || proxy.target_len == 0u || proxy.target_len > APP_CONFIG_PROXY_MAX)
        return;

    for (uint8_t slot = 0; slot < APP_SLOT_COUNT; slot++) {
        app_header_t target;
        if (app_read_validated_header(slot, &target) != APP_OK ||
            memcmp(target.name, proxy.target_name, APP_NAME_LEN) != 0 ||
            proxy.target_offset > target.asset_size ||
            proxy.target_len > target.asset_size - proxy.target_offset)
            continue;

        uint32_t magic;
        PY25Q16_ReadBuffer(APP_SLOT_BASE(slot) + APP_ASSET_OFFSET + proxy.target_offset, &magic,
                           sizeof(magic));
        if (magic != proxy.target_magic)
            continue;

        app_state.cfg_target_slot = slot;
        app_state.cfg_target_offset = proxy.target_offset;
        app_state.cfg_target_magic = proxy.target_magic;
        app_state.cfg_proxy_len = proxy.target_len;
        return;
    }
}

/* ---- hand-off (API level 3) ----
 * handoff() only resolves the target and stages the message; the launch happens
 * in APP_LaunchOverlay once the running app has returned and its config is
 * committed, so the overlay is never overwritten under the running code. */
_Static_assert(APP_HANDOFF_MAX >= 1u && APP_HANDOFF_MAX <= UINT8_MAX,
               "hand-off lengths are uint8_t: a larger mailbox would wrap the clamp");

/* A header's NUL-padded name against a C string, like strncmp over
 * APP_NAME_LEN: never reads name past its NUL, nor past APP_NAME_LEN bytes. */
static bool app_name_is(const char field[APP_NAME_LEN], const char *name)
{
    for (uint8_t i = 0; i < APP_NAME_LEN; i++) {
        if (field[i] != name[i])
            return false;
        if (name[i] == '\0')
            return true;
    }
    return true;
}

static bool app_handoff(const char *name, const void *msg, uint8_t len)
{
    /* The last call wins: start from an empty mailbox, which a failed call
     * leaves as is.  It also drops a message this app has not read. */
    app_state.handoff_next = 0;
    app_state.handoff_len = 0;
    app_state.handoff_unread = false;
    const uint32_t overlay_vma = (uint32_t)PY25Q16_OverlayBuffer();
    for (uint8_t slot = 0; slot < APP_USER_SLOT_COUNT; slot++) {
        app_header_t h;
        if (app_read_validated_header(slot, &h) != APP_OK || h.link_vma != overlay_vma ||
            !app_name_is(h.name, name))
            continue;
        if (len > APP_HANDOFF_MAX)
            len = APP_HANDOFF_MAX; /* bounds the copy into handoff_buf */
        if (len)
            memcpy(app_state.handoff_buf, msg, len);
        app_state.handoff_len = len;
        app_state.handoff_from = app_state.run_slot;
        app_state.handoff_next = slot + 1u;
        return true;
    }
    return false;
}

static int16_t app_handoff_read(void *buf, uint8_t max, char *from)
{
    if (!app_state.handoff_unread)
        return -1;
    app_state.handoff_unread = false; /* read once */
    const uint8_t len = app_state.handoff_len;
    const uint8_t n = len < max ? len : max; /* bounds the copy into the app's buf */
    if (n)
        memcpy(buf, app_state.handoff_buf, n);
    if (from) {
        PY25Q16_ReadBuffer(APP_SLOT_BASE(app_state.handoff_from) + offsetof(app_header_t, name), from,
                           APP_NAME_LEN);
        from[APP_NAME_LEN] = '\0';
    }
    return len;
}


uint8_t APP_ValidateSlot(uint8_t slot, app_header_t *out_header)
{
    app_header_t h;
    const uint8_t rc = app_read_validated_header(slot, &h);
    /* Preserve the public API: a failed validation leaves out_header intact. */
    if (rc == APP_OK && out_header)
        *out_header = h;
    return rc;
}

uint8_t APP_SlotRevision(void)
{
    return app_state.slot_revision;
}

void APP_NotifySlotChanged(void)
{
    app_state.shortcuts_cached = false;
    app_state.slot_revision++;
}

/* Position of a shortcut ID in shortcut_slots[], -1 if unknown.  Every
 * position goes through SHORTCUT_POS, which refuses to compile one that is not
 * below APP_SHORTCUT_COUNT: a new shortcut takes the next position and raises
 * the count. */
#define SHORTCUT_POS(p)                                                                                      \
    __extension__({                                                                                          \
        _Static_assert((p) < APP_SHORTCUT_COUNT, "shortcut position not below APP_SHORTCUT_COUNT");          \
        (int8_t)(p);                                                                                         \
    })
static int8_t app_shortcut_index(uint8_t shortcut)
{
    if (shortcut == APP_SHORTCUT_FM)
        return SHORTCUT_POS(0);
    if (shortcut == APP_SHORTCUT_FOXHUNT)
        return SHORTCUT_POS(1);
    if (shortcut == APP_SHORTCUT_BEACON)
        return SHORTCUT_POS(2);
    if (shortcut == APP_SHORTCUT_BEAM)
        return SHORTCUT_POS(3);
    if (shortcut == APP_SHORTCUT_TONECALL)
        return SHORTCUT_POS(4);
    if (shortcut == APP_SHORTCUT_FMESSAGE)
        return SHORTCUT_POS(5);
    return -1;
}
_Static_assert(APP_SLOT_COUNT < 0xFFu, "0xFF marks a shortcut no app holds");

static void app_cache_shortcuts(void)
{
    if (app_state.shortcuts_cached)
        return;

    memset(app_state.shortcut_slots, 0xFF, sizeof(app_state.shortcut_slots));
    const uint32_t overlay_vma = (uint32_t)PY25Q16_OverlayBuffer();

    for (uint8_t slot = 0; slot < APP_SLOT_COUNT; slot++) {
        app_header_t h;
        if (app_read_validated_header(slot, &h) != APP_OK || h.link_vma != overlay_vma)
            continue;

        const uint8_t shortcut = (uint8_t)((h.flags & APP_FLAG_SHORTCUT_MASK) >> APP_FLAG_SHORTCUT_SHIFT);
        const int8_t index = app_shortcut_index(shortcut);
        if (index >= 0 && app_state.shortcut_slots[index] == 0xFFu)
            app_state.shortcut_slots[index] = slot; /* the first app wins */
    }

    app_state.shortcuts_cached = true;
}

/* Slot of the app holding a shortcut ID, 0xFF if none. */
static uint8_t app_shortcut_slot(uint8_t shortcut)
{
    const int8_t index = app_shortcut_index(shortcut);
    if (index < 0)
        return 0xFFu;
    app_cache_shortcuts();
    return app_state.shortcut_slots[index];
}

bool APP_OverlayShortcutInstalled(uint8_t shortcut)
{
    return app_shortcut_slot(shortcut) != 0xFFu;
}

uint8_t APP_LaunchOverlayShortcut(uint8_t shortcut)
{
    const uint8_t slot = app_shortcut_slot(shortcut);
    if (slot == 0xFFu)
        return APP_ERR_MAGIC;
    app_state.shortcut_launch = true;
    return APP_LaunchOverlay(slot);
}

uint8_t APP_SlotInfo(uint8_t slot, app_header_t *out_header)
{
    if (slot >= APP_SLOT_COUNT)
        return APP_ERR_SLOT;
    app_header_t local;
    app_header_t *h = out_header ? out_header : &local;
    /* Slot info returns the raw header even when its magic is invalid. */
    PY25Q16_ReadBuffer(APP_SLOT_BASE(slot), h, sizeof(*h));
    return (h->magic == APP_MAGIC) ? APP_OK : APP_ERR_MAGIC;
}

/* APP_FLAG_SAVE_SCREEN: the screen the app left in the display buffers replaces
 * the last APP_SCREEN_SAVE_LEN bytes of its assets. Nothing was staged: those
 * buffers are resident RAM, and the overlay the app just left is free again to
 * hold the header sector while it is patched (screen, asset CRC) and written
 * back with one erase. An unchanged screen costs no write. */
static void app_screen_save(uint32_t base, uint16_t asset_size, uint8_t *ws)
{
    if (asset_size < APP_SCREEN_SAVE_LEN)
        return;
    uint8_t *const assets = ws + APP_ASSET_OFFSET;
    uint8_t *const screen = assets + asset_size - APP_SCREEN_SAVE_LEN;
    PY25Q16_InvalidateCache(); /* the overlay held app code */
    PY25Q16_ReadBuffer(base, ws, APP_SECTOR_SIZE);
    if (!memcmp(screen, gStatusLine, sizeof(gStatusLine)) &&
        !memcmp(screen + sizeof(gStatusLine), gFrameBuffer, sizeof(gFrameBuffer)))
        return;
    memcpy(screen, gStatusLine, sizeof(gStatusLine));
    memcpy(screen + sizeof(gStatusLine), gFrameBuffer, sizeof(gFrameBuffer));
    const uint16_t crc = (uint16_t)MB_Crc32Bytes(assets, asset_size);
    memcpy(ws + offsetof(app_header_t, asset_crc), &crc, sizeof(crc));
    PY25Q16_OverlayWriteSector(base);
}

/* All services are immutable.  Keeping the table in flash avoids rebuilding a
 * roughly quarter-kilobyte automatic object on every launch and removes that
 * object from the launcher's stack frame.  Callbacks must also obey the ABI's
 * no-external-flash-write rule while entry() is running. */
static uint32_t app_memory_read(uint16_t channel, char *name)
{
    if (channel == 0xffffu)
        return MR_CHANNELS_MAX;
    if (channel >= MR_CHANNELS_MAX || !RADIO_CheckValidChannel(channel, false, 0))
        return 0;
    if (name)
        SETTINGS_FetchChannelName(name, channel);
    return SETTINGS_FetchChannelFrequency(channel);
}

static const app_api_t app_api = {
    .abi_major = APP_ABI_MAJOR,
    .api_level = APP_API_LEVEL,
    .api_size = sizeof(app_api_t),
    .fb = gFrameBuffer,
    .display_clear = UI_DisplayClear,
    .status_clear = UI_StatusClear,
    .draw_line = UI_DrawLineBuffer,
    .draw_rect = app_draw_rect,
    .print_bold = UI_PrintStringSmallBold,
    .print_tiny = GUI_DisplaySmallest,
#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    .blit_full = app_blit_full,
#else
    .blit_full = ST7565_BlitFullScreen,
#endif
    .blit_line = ST7565_BlitLine,
    .blit_status = ST7565_BlitStatusLine,
    .get_key = app_get_key,
    .delay_ms = SYSTEM_DelayMs,
    .play_tone = app_play_tone,
    .led = app_led,
    .print_normal = UI_PrintStringSmallNormal,
    .print_inverse = GUI_DisplaySmallestInverse,
    .display_freq = UI_DisplayFrequency,
    .rssi_dbm = app_rssi_dbm,
    .bk_read = app_bk_read,
    .bk_write = app_bk_write,
    .set_agc = BK4819_SetAGC,
    .set_af = app_set_af,
    .audio_path = app_audio_path,
    .prepare_tone = app_prepare_tone,
    .play_tone_raw = app_play_tone_raw,
    .tones_off_rx = BK4819_TurnsOffTones_TurnsOnRX,
    .rx_freq = app_rx_freq,
    .cfg_load = app_cfg_load,
    .cfg_save = app_cfg_save,
    .draw_battery = app_draw_battery,
    .battery_sample = app_battery_sample,
    .backlight_on = app_backlight_on,
    .backlight_update = app_backlight_update,
    .audio_scope = app_audio_scope,
    .status_line = gStatusLine,
    .tx_state = app_tx_state,
    .tx_set_params = RADIO_SetTxParameters,
    .tx_tone = app_tx_tone,
    .tx_mute = app_tx_mute,
    .tx_end = app_tx_end,
    .tx_carrier = app_tx_carrier,
    .tx_freq = app_tx_freq,
    .boot_callsign = app_boot_callsign,
    .print_string = UI_PrintString,
#ifdef ENABLE_FMRADIO
    .fm_enter = app_fm_enter,
    .fm_exit = app_fm_exit,
    .fm_set_freq = BK1080_SetFrequency,
    .fm_lo = BK1080_GetFreqLoLimit,
    .fm_hi = BK1080_GetFreqHiLimit,
    .fm_mute = BK1080_Mute,
    .fm_valid = app_fm_valid,
    .fm_channels = gFM_Channels,
    .fm_state = app_fm_state,
    .fm_commit = app_fm_commit,
#endif
    .nav_dir = app_nav_dir,
#if 1
    .beam_prepare = app_beam_prepare,
    /* APP_CAP_BEAM2: beam_leave, beam_send, beam_rx and beam_rx_poll stay
     * NULL, the app drives the FSK modem through bk_read/bk_write. */
    .beam_get = app_beam_get,
    .beam_save = app_beam_save,
    .beam_draw = app_beam_draw,
#endif
    .ticks_ms = app_ticks_ms,
    .rand32 = app_rand32,
    .asset_read = app_asset_read,
    .idivmod = __aeabi_idivmod,
    .uidivmod = __aeabi_uidivmod,
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_INFO
    .sys_edition = Edition,
    .sys_version = Version,
    .sys_build_date = __DATE__,
    .sys_build_time = __TIME__,
    .sys_build_commit = "Falcon " VERSION_STRING_2,
    .sys_flash_end = &_eflash_used,
    .sys_ram_end = &_ebss,
    .sys_battery_voltage = &gBatteryVoltageAverage,
    .sys_battery_type = &gEeprom.BATTERY_TYPE,
    .sys_battery_percent = BATTERY_VoltsToPercent,
    .sys_storage_read = PY25Q16_ReadBuffer,
    .sys_stack_free_now = app_stack_now,
    .sys_stack_free_min = STACK_FreeMinimum, /* lowest free stack since boot */
#endif
    .append_i32 = app_append_i32,
    .af_capture_open = app_af_capture_open,
    .af_capture_sample = app_af_capture_sample,
    .af_capture_close = app_af_capture_close,
    .cycles_48mhz = app_cycles_48mhz,
    .draw_frequency = app_draw_frequency,
    .append_u32 = app_append_u32,
    /* Outside the block above: the hand-off is a loader service, available
     * wherever overlay apps are (this whole file), with or without the
     * system information; inside it, an app would call a NULL pointer. */
    .handoff = app_handoff,
    .handoff_read = app_handoff_read,
    .tx_prepare = RADIO_PrepareTx,
#ifdef APP_SERIAL
    .serial_read = app_serial_read,
    .serial_write = app_serial_write,
    .serial_ctl = app_serial_ctl,
#endif
    .blit_page = ST7565_BlitScreen,
    .lcd_feed = ST7565_Feed,
    .viewer_update = app_viewer_update,
#ifdef ENABLE_FEAT_F4HWN_OVERLAY_BEAM
    .vfo_stage = app_vfo_stage,
    .falcon_memory_tag = 0x314d4346u,
    .memory_read = app_memory_read,
#endif
#ifdef ENABLE_FALCON_MESSAGES
    .message_save = MSG_Save,
#endif
};

/* Keep post-exit proxy workspace off the application's live stack. */
static __attribute__((noinline)) void app_commit_config(uint8_t slot, const app_header_t *h)
{
    uint8_t *ws = PY25Q16_OverlayBuffer();
    /* Commit ordinary per-app config, or the bounded target asset selected by
     * a validated proxy descriptor.  Both paths run only after app_main exits. */
    if (app_state.cfg_len) {
        if (app_state.cfg_proxy && app_state.cfg_target_slot < APP_SLOT_COUNT) {
            app_header_t target;
            uint32_t magic;
            const uint8_t target_slot = app_state.cfg_target_slot;
            const uint32_t base = APP_SLOT_BASE(target_slot);
            if (app_read_validated_header(target_slot, &target) == APP_OK &&
                app_state.cfg_target_offset <= target.asset_size &&
                app_state.cfg_len <= target.asset_size - app_state.cfg_target_offset) {
                PY25Q16_ReadBuffer(base + APP_ASSET_OFFSET + app_state.cfg_target_offset, &magic,
                                   sizeof(magic));
                if (magic == app_state.cfg_target_magic) {
                    PY25Q16_ReadBuffer(base + APP_ASSET_OFFSET, ws, target.asset_size);
                    memcpy(ws + app_state.cfg_target_offset, app_state.cfg_buf, app_state.cfg_len);
                    const uint16_t crc = (uint16_t)MB_Crc32Bytes(ws, target.asset_size);
                    PY25Q16_WriteBuffer(base + APP_ASSET_OFFSET + app_state.cfg_target_offset,
                                        app_state.cfg_buf, app_state.cfg_len, false);
                    PY25Q16_WriteBuffer(base + offsetof(app_header_t, asset_crc), &crc, sizeof(crc), false);
                }
            }
        } else if (!app_state.cfg_proxy) {
            const uint32_t base = APP_SLOT_BASE(slot);
            PY25Q16_WriteBuffer(base + APP_CFG_OFFSET, app_state.cfg_buf, app_state.cfg_len, false);
            PY25Q16_WriteBuffer(base + APP_CFG_OWNER, h->name, APP_NAME_LEN, false);
        }
        PY25Q16_InvalidateCache();
    }
}

static __attribute__((noinline, noclone)) uint8_t app_launch(uint8_t slot)
{
    if (APP_OverlayBusy())
        return APP_ERR_BUSY;
    SETTINGS_SaveVfoNow(); /* before the app takes the sector cache */
    if (slot >= APP_USER_SLOT_COUNT)
        return APP_ERR_SLOT;
    app_header_t h;
    uint8_t rc = app_read_validated_header(slot, &h);
    if (rc != APP_OK)
        return rc;
    if (h.required_caps & APP_CAP_NATIVE)
        return APP_ERR_CAP;

    /* The app's absolute data references only resolve if it runs at the exact
     * VMA it was linked for. The overlay VMA varies with the firmware's RAM
     * layout (per preset/features), so the app records its link VMA and we
     * refuse a mismatch cleanly instead of jumping into misaddressed code. */
    uint8_t *ws = PY25Q16_OverlayBuffer();
    if (h.link_vma != (uint32_t)ws)
        return APP_ERR_VMA;
    APP_InvalidateNative(); /* the app replaces any module image from here on */

    /* Flush and suspend RF logging before the sector cache becomes executable
     * app code. Both a pending RX and an app-owned TX could otherwise write a
     * log entry through the same 4 KiB buffer and overwrite the running app. */
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    RXTX_LOG_Suspend();
#endif

    /* Repurpose the sector cache: drop any cached config sector, load the code
     * straight in (ReadBuffer bypasses the cache), and verify it in RAM before
     * trusting it. Zeroing first leaves the app's .bss clean. The assets are
     * verified first through the same buffer: a slot written by a host that
     * does not know the asset area (older UV Studio) is refused here instead of
     * handing the app unprogrammed flash. */
    PY25Q16_InvalidateCache();
    bool assets_ok = true;
    if (h.asset_size) {
        PY25Q16_ReadBuffer(APP_SLOT_BASE(slot) + APP_ASSET_OFFSET, ws, h.asset_size);
        assets_ok = (uint16_t)MB_Crc32Bytes(ws, h.asset_size) == h.asset_crc;
    }
    memset(ws, 0, APP_OVERLAY_MAX);
    PY25Q16_ReadBuffer(APP_SLOT_BASE(slot) + APP_CODE_OFFSET, ws, h.code_size);

    if (!assets_ok || MB_Crc32Bytes(ws, h.code_size) != h.code_crc32) {
        PY25Q16_InvalidateCache();
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
        RXTX_LOG_Resume();
#endif
        return APP_ERR_CRC;
    }

    /* Ensure every store to the overlay is visible before we branch into it. */
    __DSB();
    __ISB();

    app_state.run_slot = slot; /* for cfg_load / cfg_save / asset_read */
    app_state.asset_size = h.asset_size;
    app_state.cfg_len = 0;
    app_config_proxy_resolve();
    beam_dirty = false;
    app_rng_mix();
#ifdef ENABLE_FMRADIO
    app_state.fm_dirty = false;
#endif

    app_state.allow_screen_saver = (h.flags & APP_FLAG_SCREEN_SAVER) != 0;
    // The key of a shortcut launch is still held: as a key that woke the
    // screen, the app gets nothing of it until it is released.
    app_state.screen_saver_wake = app_state.shortcut_launch && KEYBOARD_GetKey() != KEY_INVALID;
    app_state.shortcut_launch = false;
    APP_ModalScreenSaverExit();
    BACKLIGHT_TurnOn();

#ifdef ENABLE_FEAT_F4HWN_K5VIEWER
    /* The caller enters from a debounced key event, so the resident key state
     * still contains that trigger while the modal app is running. Clear it so
     * K5Viewer is allowed to mirror overlay frames immediately. */
    gKeyReading0 = KEY_INVALID;
    gKeyReading1 = KEY_INVALID;
#endif

    /* Pin RX to the user-selected VFO before the app runs. Under dual watch
     * gRxVfo is whichever VFO the receiver was parked on when F+7 was pressed,
     * so an RF app (FoxHunt, a future S-meter, ...) would measure and display a
     * VFO the user did not pick - sometimes A, sometimes B. Point RX at the
     * selected (TX) VFO and retune so rx_freq(), rssi_dbm() and the tuned
     * hardware all agree on the selected channel. gCurrentVfo follows too:
     * RADIO_SetTxParameters keys it, and dual watch may have left it on the
     * other VFO, so a TX app (APRS TX, Beacon, SSTV) would transmit there while
     * tx_freq() shows the selected one. Save all three pointers:
     * radio apps such as BEAM temporarily replace them while they run. */
    const uint8_t saved_rx_vfo = gEeprom.RX_VFO;
    VFO_Info_t *const saved_rx = gRxVfo;
    VFO_Info_t *const saved_tx = gTxVfo;
    VFO_Info_t *const saved_current = gCurrentVfo;
    gEeprom.RX_VFO = gEeprom.TX_VFO;
    gRxVfo = gTxVfo;
    gCurrentVfo = gTxVfo;
    RADIO_SetupRegisters(true);

#ifdef ENABLE_FALCON_MESSAGES
    MSG_SessionStart(); /* its first saved message is preceded by a marker */
#endif
    app_entry_t entry = (app_entry_t)(((uint32_t)ws + h.entry_off) | 1u);
    app_running = true;
    app_cyc_base = APP_CYC_DIAGNOSTIC_START - app_cycles_raw();
    entry(&app_api);
    app_af_capture_close();
    app_serial_release();
    app_running = false;
    APP_InvalidateNative();

    /* First thing after the app: nothing has drawn over its last screen yet. */
    if (h.flags & APP_FLAG_SAVE_SCREEN)
        app_screen_save(APP_SLOT_BASE(slot), h.asset_size, ws);

    APP_ModalScreenSaverExit();
    app_state.allow_screen_saver = false;
    app_state.screen_saver_wake = false;

    /* Restore the resident RX/dual-watch tuning the app ran on top of. */
    gEeprom.RX_VFO = saved_rx_vfo;
    gRxVfo = saved_rx;
    gTxVfo = saved_tx;
    gCurrentVfo = saved_current;
    RADIO_SetupRegisters(true);

    /* The overlay held app code, not a valid config sector. */
    PY25Q16_InvalidateCache();

    app_beam_commit();

    app_commit_config(slot, &h);
#ifdef ENABLE_FMRADIO
    /* Commit the FM config + 48 channels the app edited (shared with resident FM). */
    if (app_state.fm_dirty) {
        app_state.fm_dirty = false;
        SETTINGS_SaveFM();
        PY25Q16_InvalidateCache();
    }
#endif
#ifdef ENABLE_FEAT_F4HWN_RXTX_LOG
    RXTX_LOG_Resume();
#endif
    return APP_OK;
}

#define APP_HANDOFF_FAST_10MS 30u
#define APP_HANDOFF_FAST_HOPS 8u
bool APP_WaitKeyRelease(void)
{
    uint8_t stable = 0;
    uint8_t exit_left = 100; /* polls of EXIT down before it counts as held */
    while (stable < 10u) {
        const KEY_Code_t key = KEYBOARD_Poll();
        if ((GPIOB->IDR & (1u << 10)) && key == KEY_INVALID)
            stable++;
        else
            stable = 0;
        if (key == KEY_EXIT && exit_left)
            exit_left--;
        SYSTEM_DelayMs(10);
        APP_ModalBacklightTick(true);
    }
    return !exit_left;
}
static uint8_t LaunchChain(uint8_t slot);

/* Apps may be launched from native module code: the module is reloaded
 * before returning to it. */
uint8_t APP_LaunchOverlay(uint8_t slot)
{
    const uint8_t rc = LaunchChain(slot);
    APP_NativeResume();
    return rc;
}

static uint8_t LaunchChain(uint8_t slot)
{
    gAppVfoExit = false; /* result belongs only to this top-level launch */
    uint8_t fast_hops = 0;
    app_state.handoff_unread = false; /* a menu or shortcut launch carries no message */
    for (;;) {
        gAppLastSlot = slot;
        app_state.handoff_next = 0;
        const uint32_t started = gGlobalSysTickCounter;
        const uint8_t rc = app_launch(slot);
        if (rc != APP_OK || !app_state.handoff_next)
            return rc;
        slot = app_state.handoff_next - 1u;
        fast_hops = gGlobalSysTickCounter - started < APP_HANDOFF_FAST_10MS ? fast_hops + 1u : 0u;
        /* The key that ended the sender must not reach the target as a press. */
        if (APP_WaitKeyRelease() || fast_hops >= APP_HANDOFF_FAST_HOPS)
            return APP_CHAIN_ABORTED;
        app_state.handoff_unread = true; /* until read, or the target's own handoff() */
    }
}

uint8_t APP_SlotErase(uint8_t slot)
{
    if (APP_OverlayBusy())
        return APP_ERR_BUSY;
    APP_InvalidateNative();
    if (slot >= APP_SLOT_COUNT)
        return APP_ERR_SLOT;
    uint32_t base = APP_SLOT_BASE(slot);
    /* An update erases the slot before the host rewrites it: keep the config
     * and its owner tag across the erase (an untagged config is given the
     * installed app's name), so that the same app finds its settings again. */
    uint8_t keep[APP_CFG_OWNER + APP_NAME_LEN - APP_CFG_OFFSET];
    uint8_t *const owner = keep + (APP_CFG_OWNER - APP_CFG_OFFSET);
    app_header_t h;
    PY25Q16_ReadBuffer(base + APP_CFG_OFFSET, keep, sizeof(keep));
    if (!PY25Q16_IsHealthy())
        return APP_ERR_SPI;
    if (*owner == 0xFFu && app_read_validated_header(slot, &h) == APP_OK)
        memcpy(owner, h.name, APP_NAME_LEN);
    for (uint32_t off = 0; off < APP_SLOT_STRIDE; off += APP_SECTOR_SIZE) {
        PY25Q16_SectorErase(base + off);
        if (!PY25Q16_IsHealthy())
            return APP_ERR_SPI;
    }
    PY25Q16_InvalidateCache();
    if (*owner != 0xFFu) {
        PY25Q16_WriteBuffer(base + APP_CFG_OFFSET, keep, sizeof(keep), false);
        if (!PY25Q16_IsHealthy())
            return APP_ERR_SPI;
        PY25Q16_InvalidateCache();
    }
    APP_NotifySlotChanged();
    return APP_OK;
}

uint8_t APP_SlotWrite(uint8_t slot, uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (APP_OverlayBusy())
        return APP_ERR_BUSY;
    APP_InvalidateNative();
    if (slot >= APP_SLOT_COUNT)
        return APP_ERR_SLOT;
    if (offset > APP_SLOT_STRIDE || len > APP_SLOT_STRIDE - offset)
        return APP_ERR_SIZE;
    if (offset == 0 && len >= sizeof(app_header_t)) {
        app_header_t h;
        memcpy(&h, data, sizeof h);
        if (h.required_caps & APP_CAP_NATIVE)
            return APP_ERR_CAP;
    }
    PY25Q16_WriteBuffer(APP_SLOT_BASE(slot) + offset, data, len, false);
    if (!PY25Q16_IsHealthy())
        return APP_ERR_SPI;
    PY25Q16_InvalidateCache();
    app_state.shortcuts_cached = false;
    return APP_OK;
}

bool APP_NativeBegin(uint8_t id)
{
    if (app_running || id < 1 || id > NATIVE_COUNT || native_depth >= NATIVE_NESTING_MAX)
        return false;
    if (loaded_native != id && !NativeLoad(id)) {
        APP_NativeResume(); /* the caller's module, if any */
        return false;
    }
    native_stack[native_depth++] = id;
    return true;
}

void APP_NativeEnd(void)
{
    if (!native_depth)
        return;
    native_depth--;
    APP_NativeResume();
}
#endif
