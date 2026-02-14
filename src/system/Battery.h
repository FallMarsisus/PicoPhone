#ifndef SYSTEM_BATTERY_H
#define SYSTEM_BATTERY_H

#include <Arduino.h>

namespace battery {

// RP2040/Pico: VSYS est généralement connecté à ADC3 (GPIO29) via un pont diviseur ~1/3
static constexpr uint8_t VSYS_ADC_GPIO = 29; // ADC3
static constexpr float ADC_REF_V = 3.3f;
static constexpr float ADC_MAX = 4095.0f;
static constexpr float VSYS_DIVIDER = 3.0f;

static inline void begin() {
    static bool inited = false;
    if (inited) return;

    // Force la résolution attendue par nos calculs (0..4095)
    analogReadResolution(12);
#if defined(analogReadAveraging)
    analogReadAveraging(16);
#endif
    pinMode(VSYS_ADC_GPIO, INPUT);
    inited = true;
}

static inline float read_vsys_volts_raw(uint8_t samples = 8) {
    begin();

    if (samples < 5) samples = 5;
    if (samples > 25) samples = 25;

    // Moyenne tronquée: ignore min/max pour limiter les glitches (lectures à 0).
    uint32_t acc = 0;
    uint16_t minv = 0xFFFF;
    uint16_t maxv = 0;

    for (uint8_t i = 0; i < samples; i++) {
        const uint16_t v = (uint16_t)analogRead(VSYS_ADC_GPIO);
        acc += v;
        if (v < minv) minv = v;
        if (v > maxv) maxv = v;
        delayMicroseconds(50);
    }

    acc -= minv;
    acc -= maxv;
    const float denom = (float)(samples - 2);
    const float raw = (denom > 0) ? ((float)acc / denom) : (float)acc;
    const float v_adc = (raw * ADC_REF_V) / ADC_MAX;
    return v_adc * VSYS_DIVIDER;
}

static inline uint8_t percent_from_lipo_volts(float v) {
    // Courbe simple LiPo 1S (approx). Clamp pour éviter USB (~5V) => 100%
    if (v >= 4.20f) return 100;
    if (v <= 3.30f) return 0;

    // LUT (volts, %)
    struct Point { float v; uint8_t p; };
    static constexpr Point lut[] = {
        {4.20f, 100},
        {4.10f,  90},
        {4.00f,  80},
        {3.90f,  60},
        {3.80f,  40},
        {3.70f,  20},
        {3.60f,  10},
        {3.50f,   5},
        {3.30f,   0},
    };

    for (size_t i = 0; i + 1 < (sizeof(lut) / sizeof(lut[0])); i++) {
        const auto a = lut[i];
        const auto b = lut[i + 1];
        if (v <= a.v && v >= b.v) {
            const float t = (v - b.v) / (a.v - b.v);
            const float p = (float)b.p + t * ((float)a.p - (float)b.p);
            if (p <= 0.0f) return 0;
            if (p >= 100.0f) return 100;
            return (uint8_t)(p + 0.5f);
        }
    }

    // Fallback
    return 0;
}

static inline uint8_t read_percent() {
    // Lissage léger pour éviter les sauts
    static float filtered = 0.0f;
    static bool has = false;

    static float last_good = 0.0f;
    static bool has_good = false;

    const float v = read_vsys_volts_raw(10);

    // Sur TP4056 + charge, OUT+/VSYS peut ne plus refléter fidèlement la batterie.
    // Une LiPo 1S ne dépasse pas ~4.2V (4.25V max). Au-delà (~5V USB), on "gèle"
    // la dernière valeur batterie connue pour éviter les sauts 100%/0%.
    const bool in_batt_range = (v >= 3.0f && v <= 4.35f);
    const bool glitch_low = (v < 2.8f);
    const bool external_power = (v > 4.35f && v <= 5.5f);

    float v_use;
    if (in_batt_range) {
        v_use = v;
        last_good = v;
        has_good = true;
    } else if (external_power && has_good) {
        v_use = last_good;
    } else if (glitch_low && has_good) {
        v_use = last_good;
    } else {
        // fallback: si on boote déjà branché et sans historique
        v_use = v;
    }

    if (!has) {
        filtered = v_use;
        has = true;
    } else {
        // Filtre plus lent pour une UI stable
        filtered = filtered * 0.95f + v_use * 0.05f;
    }

    return percent_from_lipo_volts(filtered);
}

} // namespace battery

#endif
