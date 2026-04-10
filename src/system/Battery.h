#ifndef SYSTEM_BATTERY_H
#define SYSTEM_BATTERY_H

#include <Arduino.h>
#include <hardware/adc.h>
#include <XPowersLib.h>

namespace battery {

// RP2040/Pico: VSYS est généralement connecté à ADC3 (GPIO29) via un pont diviseur ~1/3
static constexpr uint8_t VSYS_ADC_GPIO = 29; // ADC3
static constexpr float ADC_REF_V = 3.3f;
static constexpr float ADC_MAX = 4095.0f;
static constexpr float VSYS_DIVIDER = 3.0f;
static constexpr uint32_t SAMPLE_INTERVAL_MS = 1200;
static constexpr uint32_t POLICY_INTERVAL_MS = 1000;

enum class PowerMode : uint8_t {
    NORMAL = 0,
    SAVER = 1,
    CRITICAL = 2,
};

struct EnergyPolicy {
    PowerMode mode;
    uint8_t brightness_limit_percent;
    uint8_t volume_limit_percent;
    bool lte_low_power;
    bool low_battery;
    bool shutdown_requested;
};

struct Telemetry {
    uint8_t percent;
    float voltage_v;
    bool external_power;
    bool charging;
    bool valid;
};

// État global partagé (inline variables C++17) :
// évite les états divergents entre unités de compilation.
inline XPowersAXP2101* g_pmic = nullptr;
inline bool g_pmic_ready = false;
inline float   g_last_good_v = 0.0f;
inline bool    g_has_good_v = false;
inline uint8_t g_last_percent = 0;
inline bool    g_has_percent = false;
inline uint8_t g_invalid_adc_streak = 0;
inline Telemetry g_last_sample = {100, 4.0f, true, false, false};
inline bool g_has_sample = false;
inline uint32_t g_last_sample_ms = 0;
inline PowerMode g_mode = PowerMode::NORMAL;
inline EnergyPolicy g_policy = {PowerMode::NORMAL, 100, 100, false, false, false};
inline uint32_t g_last_policy_ms = 0;
inline bool g_manual_saver = false;

static inline uint8_t clamp_percent_int(int value) {
    if (value < 0) return 0;
    if (value > 100) return 100;
    return (uint8_t)value;
}

static inline uint8_t min_u8(uint8_t a, uint8_t b) {
    return (a < b) ? a : b;
}

static inline bool attach_pmic(XPowersAXP2101* pmic) {
    g_pmic = pmic;
    g_pmic_ready = (pmic != nullptr);
    if (!g_pmic_ready) {
        return false;
    }

    // Active explicitement les mesures batterie, sinon certains boards renvoient 0.
    g_pmic->enableBattDetection();
    g_pmic->enableBattVoltageMeasure();
    g_has_sample = false;
    return true;
}

static inline void set_manual_saver_enabled(bool enabled) {
    g_manual_saver = enabled;
    g_last_policy_ms = 0;
}

static inline bool is_manual_saver_enabled() {
    return g_manual_saver;
}

static inline bool toggle_manual_saver() {
    set_manual_saver_enabled(!g_manual_saver);
    return g_manual_saver;
}

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

static inline bool read_from_pmic(Telemetry& out) {
    if (!g_pmic_ready || g_pmic == nullptr) {
        return false;
    }

    const int pmic_percent = g_pmic->getBatteryPercent();
    const uint16_t pmic_mv = g_pmic->getBattVoltage();
    const bool external_power = g_pmic->isVbusIn();
    const bool charging = g_pmic->isCharging();

    const bool percent_valid = (pmic_percent >= 0 && pmic_percent <= 100);
    const bool voltage_valid = (pmic_mv >= 2800 && pmic_mv <= 4600);

    if (!percent_valid && !voltage_valid && !external_power) {
        return false;
    }

    uint8_t percent = 100;
    if (percent_valid) {
        percent = (uint8_t)pmic_percent;
    } else if (voltage_valid) {
        percent = percent_from_lipo_volts((float)pmic_mv / 1000.0f);
    } else if (g_has_percent) {
        percent = g_last_percent;
    }

    out.percent = percent;
    out.voltage_v = voltage_valid ? ((float)pmic_mv / 1000.0f) : g_last_good_v;
    out.external_power = external_power;
    out.charging = charging;
    out.valid = true;

    if (voltage_valid) {
        g_last_good_v = out.voltage_v;
        g_has_good_v = true;
    }
    g_last_percent = out.percent;
    g_has_percent = true;
    g_invalid_adc_streak = 0;
    return true;
}

static inline bool read_from_adc(Telemetry& out) {
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

        out.percent = g_last_percent;
        out.voltage_v = v;
        out.external_power = false;
        out.charging = false;
        out.valid = true;
        return true;
    }

    // Si l'ADC est invalide plusieurs cycles, ne jamais retomber à 0%.
    if (g_invalid_adc_streak >= 3) {
        out.percent = g_has_percent ? g_last_percent : 100;
        out.voltage_v = g_has_good_v ? g_last_good_v : 4.0f;
        out.external_power = external_power;
        out.charging = external_power;
        out.valid = true;
        return true;
    }

    // Si alimenté via USB/chargeur, on garde le dernier % batterie fiable.
    if (external_power || glitch_low) {
        out.percent = g_has_percent ? g_last_percent : 100;
        out.voltage_v = g_has_good_v ? g_last_good_v : v;
        out.external_power = external_power;
        out.charging = external_power;
        out.valid = true;
        return true;
    }

    out.percent = percent_from_lipo_volts(v);
    out.voltage_v = v;
    out.external_power = false;
    out.charging = false;
    out.valid = true;
    g_last_percent = out.percent;
    g_has_percent = true;
    return true;
}

static inline const Telemetry& read_sample(bool force = false) {
    begin();

    const uint32_t now = millis();
    if (!force && g_has_sample && (now - g_last_sample_ms) < SAMPLE_INTERVAL_MS) {
        return g_last_sample;
    }

    Telemetry sample = g_has_sample ? g_last_sample : Telemetry{100, 4.0f, true, false, false};
    bool ok = false;
    if (g_pmic_ready) {
        ok = read_from_pmic(sample);
    }
    if (!ok) {
        ok = read_from_adc(sample);
    }

    if (ok) {
        g_last_sample = sample;
        g_has_sample = true;
        g_last_sample_ms = now;
    }
    return g_last_sample;
}

static inline uint8_t read_percent() {
    return read_sample(false).percent;
}

static inline uint16_t read_voltage_mv() {
    const Telemetry& t = read_sample(false);
    if (t.voltage_v <= 0.0f) return 0;
    return (uint16_t)(t.voltage_v * 1000.0f + 0.5f);
}

static inline bool is_external_power() {
    return read_sample(false).external_power;
}

static inline bool is_charging() {
    return read_sample(false).charging;
}

static inline PowerMode compute_mode(const Telemetry& t) {
    if (t.external_power || t.charging) {
        return PowerMode::NORMAL;
    }

    if (g_manual_saver) {
        if (t.percent <= 8) return PowerMode::CRITICAL;
        return PowerMode::SAVER;
    }

    switch (g_mode) {
        case PowerMode::NORMAL:
            if (t.percent <= 20) return PowerMode::SAVER;
            return PowerMode::NORMAL;
        case PowerMode::SAVER:
            if (t.percent <= 8) return PowerMode::CRITICAL;
            if (t.percent >= 20) return PowerMode::NORMAL;
            return PowerMode::SAVER;
        case PowerMode::CRITICAL:
        default:
            if (t.percent >= 14) return PowerMode::SAVER;
            return PowerMode::CRITICAL;
    }
}

static inline void update_energy_policy(bool force = false) {
    const uint32_t now = millis();
    if (!force && (now - g_last_policy_ms) < POLICY_INTERVAL_MS) {
        return;
    }

    const Telemetry& t = read_sample(force);
    if (t.external_power || t.charging) {
        g_manual_saver = false;
    }
    g_mode = compute_mode(t);

    switch (g_mode) {
        case PowerMode::NORMAL:
            g_policy = {PowerMode::NORMAL, 100, 100, false, false, false};
            break;
        case PowerMode::SAVER:
            g_policy = {PowerMode::SAVER, 60, 60, true, true, false};
            break;
        case PowerMode::CRITICAL:
        default:
            g_policy = {PowerMode::CRITICAL, 35, 0, true, true, (!t.external_power && t.percent <= 2)};
            break;
    }

    g_last_policy_ms = now;
}

static inline const EnergyPolicy& get_energy_policy() {
    update_energy_policy(false);
    return g_policy;
}

static inline uint8_t cap_brightness(uint8_t requested_brightness) {
    const EnergyPolicy& p = get_energy_policy();
    uint16_t cap = (uint16_t)p.brightness_limit_percent * 255u / 100u;
    if (cap < 10u) cap = 10u;
    return min_u8(requested_brightness, (uint8_t)cap);
}

static inline uint8_t cap_volume(uint8_t requested_volume) {
    const EnergyPolicy& p = get_energy_policy();
    return min_u8(requested_volume, clamp_percent_int((int)p.volume_limit_percent));
}

static inline const char* power_mode_name(PowerMode mode) {
    switch (mode) {
        case PowerMode::NORMAL: return "normal";
        case PowerMode::SAVER: return "saver";
        case PowerMode::CRITICAL: return "critical";
        default: return "unknown";
    }
}

} // namespace battery

#endif
