/* V126: filled frequency profile and grouped memory bars.
 * Uses the scanner's existing RSSI samples. No RF or display-bus access here.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define ACTIVITY_TOP         16
#define ACTIVITY_BOTTOM      39
#define ACTIVITY_LEFT        0
#define ACTIVITY_WIDTH       (128 - ACTIVITY_LEFT)
#define ACTIVITY_LIST_BOTTOM 39

typedef struct {
    uint16_t samples[128];
    uint8_t sampled[16];
    uint32_t scaleTick, start, stop;
    int16_t noiseDb, minDb, maxDb;
    uint8_t mode;
    bool initialized, scaleReady;
} SpectrumActivity;

// Compact display map: 128 column boundaries, not a channel-sized RSSI buffer.
// Only the configuration pass reads memories; rendering/sampling use this map.
typedef struct {
    uint16_t count[MR_CHANNELS_LIST], last[ACTIVITY_WIDTH];
    uint8_t start[MR_CHANNELS_LIST], width[MR_CHANNELS_LIST], current;
} SpectrumListPlot;
static void ActivityListLayout(SpectrumListPlot *p)
{
    unsigned total = 0, groups = 0;
    for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
        if (p->count[i]) {
            total += p->count[i];
            ++groups;
        }
    memset(p->width, 0, sizeof p->width);
    memset(p->last, 255, sizeof p->last);
    if (!groups)
        return;
    const unsigned extra = ACTIVITY_WIDTH - (groups - 1) - groups;
    unsigned seen = 0, x = 0;
    for (unsigned i = 0; i < MR_CHANNELS_LIST; ++i)
        if (p->count[i]) {
            const unsigned next = seen + p->count[i];
            p->start[i] = x;
            p->width[i] = 1 + extra * next / total - extra * seen / total;
            x += p->width[i] + 1;
            seen = next;
        }
}
static void ActivityListMember(SpectrumListPlot *p, unsigned list, unsigned rank, uint16_t ch)
{
    if (list >= MR_CHANNELS_LIST || !p->width[list] || rank >= p->count[list])
        return;
    const unsigned first = p->start[list], width = p->width[list];
    const unsigned begin = first + rank * width / p->count[list];
    unsigned end = first + (rank + 1) * width / p->count[list];
    if (end <= begin)
        end = begin + 1;
    for (unsigned x = begin; x < end && x < first + width; ++x)
        p->last[x] = ch;
}

// Display samples use a -192 dBm origin, preserving weak calibrated levels.
// Raw samples and reception thresholds are never changed by this encoding.
static uint16_t ActivityEncode(uint16_t raw, int correction)
{
    if (!raw || raw == UINT16_MAX)
        return 0;
    return (uint16_t)(((int)(raw / 2) + 32 + correction) * 2);
}
static int ActivityDb(uint16_t rssi)
{
    return (int)(rssi / 2) - 192;
}
static bool ActivityValid(uint16_t rssi)
{
    return rssi && rssi != UINT16_MAX;
}
static void ActivityReset(SpectrumActivity *v, uint32_t start, uint32_t stop, uint8_t mode, uint32_t now)
{
    memset(v, 0, sizeof(*v));
    v->start = start;
    v->stop = stop;
    v->mode = mode;
    v->scaleTick = now;
    v->noiseDb = v->minDb = -120;
    v->maxDb = -80;
    v->initialized = true;
}
static void ActivityContext(SpectrumActivity *v, uint32_t start, uint32_t stop, uint8_t mode, uint32_t now)
{
    if (v->initialized && mode == 2 && v->mode == mode) {
        v->start = start;
        v->stop = stop;
        return;
    }
    if (!v->initialized || start != v->start || stop != v->stop || mode != v->mode)
        ActivityReset(v, start, stop, mode, now);
}
static void ActivityRecord(SpectrumActivity *v, unsigned x, uint16_t rssi)
{
    if (x >= 128)
        return;
    if (rssi == UINT16_MAX)
        rssi = 0;
    // Retain a measured transient until the next existing display frame.
    // This is internal sampling, not a drawn peak-hold trace.
    unsigned mask = 1u << (x & 7);
    if (!(v->sampled[x >> 3] & mask) || rssi > v->samples[x]) {
        v->samples[x] = rssi;
    }
    v->sampled[x >> 3] |= mask;
}
static void ActivityFinishSweep(SpectrumActivity *v, uint32_t now)
{
    (void)now;
    if (!v->initialized)
        return;
    // Estimate background from the lower fifth of a completed scan.
    // A flat PMR noise floor can rise; it is not mistaken for activity forever.
    uint8_t histogram[64] = {0};
    unsigned total = 0;
    for (unsigned x = 0; x < 128; ++x) {
        if (!ActivityValid(v->samples[x]))
            continue;
        unsigned bin = v->samples[x] / 8;
        if (bin > 63)
            bin = 63;
        ++histogram[bin];
        ++total;
    }
    if (total) {
        unsigned target = (total + 4) / 5, seen = 0;
        for (unsigned bin = 0; bin < 64; ++bin) {
            seen += histogram[bin];
            if (seen >= target) {
                int floor = (int)bin * 4 - 192;
                v->noiseDb = floor;
                break;
            }
        }
    }
}
static void ActivityUpdate(SpectrumActivity *v, const uint16_t samples[128], uint32_t now)
{
    int low = 32767, high = -32768;
    for (unsigned x = 0; x < 128; ++x) {
        if (!ActivityValid(samples[x]))
            continue;
        int d = ActivityDb(samples[x]);
        if (d < low)
            low = d;
        if (d > high)
            high = d;
    }
    if (low <= high) {
        if (!v->scaleReady) {
            v->noiseDb = low;
            v->minDb = low - 12;
            v->maxDb = high + 6;
            if (v->maxDb < v->minDb + 60)
                v->maxDb = v->minDb + 60;
            v->scaleReady = true;
        } else {
            // Expand for a strong signal; relax only 1 dB per elapsed second.
            if (high + 6 > v->maxDb)
                v->maxDb = high + 6;
            if ((uint32_t)(now - v->scaleTick) >= 100) {
                const int targetMin = v->noiseDb - 12;
                v->minDb += (targetMin > v->minDb) - (targetMin < v->minDb);
                int targetMax = high + 6;
                if (targetMax < v->minDb + 60)
                    targetMax = v->minDb + 60;
                if (v->maxDb > targetMax)
                    --v->maxDb;
                v->scaleTick = now;
            }
            if (v->maxDb < v->minDb + 60)
                v->maxDb = v->minDb + 60;
        }
    }
    memset(v->sampled, 0, sizeof(v->sampled));
}
static unsigned ActivityY(const SpectrumActivity *v, uint16_t rssi)
{
    int d = ActivityDb(rssi);
    if (d <= v->minDb)
        return ACTIVITY_BOTTOM;
    if (d >= v->maxDb)
        return ACTIVITY_TOP;
    return ACTIVITY_BOTTOM - (d - v->minDb) * (ACTIVITY_BOTTOM - ACTIVITY_TOP) / (v->maxDb - v->minDb);
}
static void ActivityPixel(uint8_t fb[7][128], unsigned x, unsigned y)
{
    if (x < 128 && y < 56)
        fb[y >> 3][x] |= 1u << (y & 7);
}
// Every valid RSSI sample is shown, including the noise floor.
static unsigned ActivityLiveY(const SpectrumActivity *v, uint16_t rssi)
{
    return ActivityValid(rssi) ? ActivityY(v, rssi) : ACTIVITY_BOTTOM;
}
static unsigned ActivitySmoothY(const SpectrumActivity *v, const uint16_t samples[128], unsigned x)
{
    unsigned cur = ActivityLiveY(v, samples[x]);
    unsigned left = x ? ActivityLiveY(v, samples[x - 1]) : cur;
    unsigned right = x < 127 ? ActivityLiveY(v, samples[x + 1]) : cur;
    // Weighted smoothing keeps narrow signals visible while softening stair steps.
    unsigned y = (left + 2 * cur + right + 2) / 4;
    if ((cur <= left && cur < right) || (cur < left && cur <= right))
        y = cur;
    else if (cur + 1 < y)
        y = cur + 1;
    return y;
}
static void ActivityDraw(const SpectrumActivity *v, const uint16_t samples[128], uint8_t fb[7][128])
{
    for (unsigned x = 0; x < 128; ++x) {
        const unsigned top = ActivitySmoothY(v, samples, x);
        for (unsigned y = top; y <= ACTIVITY_BOTTOM + 1; ++y)
            ActivityPixel(fb, x, y);
    }
}
static unsigned ActivityFrequencyColumn(uint32_t delta, uint32_t span)
{
    if (!span)
        return 0;
    if (delta >= span)
        return 127;
    // Avoid 64-bit division in the scan loop for very wide frequency ranges.
    if (span > UINT32_MAX / 127) {
        delta /= 128;
        span /= 128;
    }
    return delta * 127 / span;
}
static void ActivityRecordFrequency(SpectrumActivity *v, uint32_t frequency, uint32_t step, uint16_t rssi)
{
    if (frequency < v->start || frequency > v->stop || v->stop <= v->start)
        return;
    const uint32_t span = v->stop - v->start, delta = frequency - v->start;
    // With sparse measurements, each frequency occupies its nearest-step cell.
    const uint32_t half = step / 2;
    const uint32_t low = delta > half ? delta - half : 0;
    const uint32_t high = step - half > span - delta ? span : delta + step - half;
    const unsigned first = ActivityFrequencyColumn(low, span);
    const unsigned last = ActivityFrequencyColumn(high, span);
    for (unsigned x = first; x <= last; ++x)
        ActivityRecord(v, x, rssi);
}
static void ActivityRecordList(SpectrumActivity *v, const SpectrumListPlot *p, uint16_t channel,
                               uint16_t rssi)
{
    if (!p->current || p->current > MR_CHANNELS_LIST)
        return;
    const unsigned list = p->current - 1;
    unsigned left = p->start[list], right = left + p->width[list];
    const unsigned end = right;
    if (left == right)
        return;
    // Binary search the physical channel boundaries of this display group.
    while (left < right) {
        const unsigned mid = (left + right) / 2;
        if (p->last[mid] < channel)
            left = mid + 1;
        else
            right = mid;
    }
    if (left == end)
        return;
    const uint16_t boundary = p->last[left];
    for (unsigned x = left; x < end && p->last[x] == boundary; ++x)
        ActivityRecord(v, x, rssi);
}
static void ActivityDrawLists(const SpectrumActivity *v, const SpectrumListPlot *p, uint8_t fb[7][128])
{
    for (unsigned list = 0; list < MR_CHANNELS_LIST; ++list) {
        const unsigned first = p->start[list], end = first + p->width[list];
        for (unsigned x = first; x < end; ++x) {
            // Leave an inter-memory gap when each memory has room for a bar.
            if (x > first && p->width[list] > 2 * p->count[list] && p->last[x] != p->last[x - 1])
                continue;
            if (ActivityValid(v->samples[x])) {
                const unsigned y = ActivityY(v, v->samples[x]);
                const unsigned top = y;
                for (unsigned py = top; py <= ACTIVITY_LIST_BOTTOM; ++py)
                    ActivityPixel(fb, ACTIVITY_LEFT + x, py);
            }
            ActivityPixel(fb, ACTIVITY_LEFT + x, ACTIVITY_LIST_BOTTOM + 1);
        }
    }
}
