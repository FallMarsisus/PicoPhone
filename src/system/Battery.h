#ifndef SYSTEM_BATTERY_H
#define SYSTEM_BATTERY_H

#include <Arduino.h>
#include <hardware/adc.h>

namespace battery {

// RP2040/Pico: VSYS est généralement connecté à ADC3 (GPIO29) via un pont diviseur ~1/3
static constexpr uint8_t VSYS_ADC_GPIO = 29; // ADC3
static constexpr float ADC_REF_V = 3.3f;
static constexpr float ADC_MAX = 4095.0f;
static constexpr float VSYS_DIVIDER = 3.0f;

// État global partagé (inline variables C++17) :
// évite les états divergents entre unités de compilation.
inline float   g_last_good_v = 0.0f;
inline bool    g_has_good_v = false;
inline uint8_t g_last_percent = 0;
inline bool    g_has_percent = false;
inline uint8_t g_invalid_adc_streak = 0;

static inline void begin() {
    static bool inited = false;
    if (inited) return;

    // Force la résolution attendue par nos calculs (0..4095)
    analogReadResolution(12);
#if defined(analogReadAveraging)
    analogReadAveraging(16);
#endif
    // Sur RP2040 : NE PAS appeler pinMode() sur une broche ADC — ça active
    // le buffer digital Schmitt trigger et court-circuite l'entrée analogique.
    // adc_gpio_init() configure correctement la broche en mode ADC (fonction NULL,
    // pas de pulls, pas de buffer digital).
    adc_init();
    adc_gpio_init(VSYS_ADC_GPIO);
    inited = true;
}

static inline float read_vsys_volts_raw(uint8_t samples = 8) {
    begin();
    analogReadResolution(12); // défensif: d'autres modules peuvent modifier la résolution

    if (samples < 5) samples = 5;
    if (samples > 25) samples = 25;

    // Moyenne tronquée: ignore min/max pour limiter les glitches.
    // On combine 2 chemins de lecture:
    //   1) analogRead(GPIO29)
    //   2) ADC brut direct canal 3 (secours si mapping core défaillant)
    uint32_t acc = 0;
    uint16_t minv = 0xFFFF;
    uint16_t maxv = 0;

    for (uint8_t i = 0; i < samples; i++) {
        const uint16_t v_analog = (uint16_t)analogRead(VSYS_ADC_GPIO);
        adc_select_input(3);
        delayMicroseconds(10);
        const uint16_t v_direct = (uint16_t)adc_read();

        uint16_t v = v_analog;
        if (v_direct > v) v = v_direct;
        if (v < 8 && v_direct > 20) v = v_direct;

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
    const float vsys = v_adc * VSYS_DIVIDER;

    static unsigned long last_dbg = 0;
    if (millis() - last_dbg > 15000) {
        last_dbg = millis();
        Serial.printf("[BAT] raw=%.0f v_adc=%.3fV vsys=%.3fV\n", raw, v_adc, vsys);
    }
    return vsys;
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
    // 3 lectures + médiane pour réduire les glitches ADC
    float a = read_vsys_volts_raw(10);
    float b = read_vsys_volts_raw(10);
    float c = read_vsys_volts_raw(10);
    float v = a;
    if ((a <= b && b <= c) || (c <= b && b <= a)) v = b;
    else if ((b <= a && a <= c) || (c <= a && a <= b)) v = a;
    else v = c;

    // Sur TP4056 + charge, OUT+/VSYS peut ne plus refléter fidèlement la batterie.
    // Une LiPo 1S ne dépasse pas ~4.2V (4.25V max). Au-delà (~5V USB), on "gèle"
    // la dernière valeur batterie connue pour éviter les sauts 100%/0%.
    const bool in_batt_range = (v >= 3.0f && v <= 4.35f);
    const bool glitch_low = (v < 2.8f);
    const bool external_power = (v > 4.35f && v <= 5.5f);

    // ADC VSYS clairement invalide (pin flottante/non câblée/mapping KO)
    const bool invalid_adc = (v < 0.8f || v > 6.2f);
    if (invalid_adc) {
        if (g_invalid_adc_streak < 255) g_invalid_adc_streak++;
    } else {
        g_invalid_adc_streak = 0;
    }

    if (in_batt_range) {
        g_last_good_v = v;
        g_has_good_v = true;
        g_last_percent = percent_from_lipo_volts(v);
        g_has_percent = true;
        return g_last_percent;
    }

    // Si l'ADC est invalide plusieurs cycles, ne jamais retomber à 0%.
    // On garde la dernière valeur fiable; à défaut (boot sur alim externe), 100%.
    if (g_invalid_adc_streak >= 3) {
        if (g_has_percent) return g_last_percent;
        return 100;
    }

    // Si alimenté via USB/chargeur, on garde le dernier % batterie fiable
    if (external_power) {
        if (g_has_percent) return g_last_percent;
        return 100;
    }

    // Glitch bas ADC: garder dernière valeur fiable
    if (glitch_low) {
        if (g_has_percent) return g_last_percent;
        return 100;
    }

    // Valeur hors plage sans historique: fallback direct
    return percent_from_lipo_volts(v);
}

} // namespace battery

#endif
