#ifndef SYSTEM_SETTINGS_H
#define SYSTEM_SETTINGS_H

#include <Arduino.h>
#include <EEPROM.h>
#include "Hardware.h"

// Déclaration anticipée: implémentée dans Hardware.h
static inline void audio_amp_enable(bool enable);

/**
 * Paramètres système persistants (stockés en EEPROM à l'offset 1024).
 */
namespace settings {

// IMPORTANT: sur ce projet, GPIO13 = TFT_RST. La backlight est sur GPIO15.
// On utilise TFT_BL si défini par TFT_eSPI / build_flags, sinon fallback à 15.
#if defined(TFT_BL)
static constexpr uint8_t BACKLIGHT_PIN = (uint8_t)TFT_BL;
#else
static constexpr uint8_t BACKLIGHT_PIN = 15;
#endif

static constexpr int SETTINGS_ADDR = 1024;
static constexpr uint32_t SETTINGS_MAGIC = 0x53455454u; // 'SETT'

struct Data {
    uint32_t magic;
    // Verrouillage
    bool pin_enabled;        // PIN actif ?
    char pin_code[7];        // Code PIN (max 6 chiffres + '\0')
    uint32_t lock_timeout_ms; // Timeout avant verrouillage auto (ms)
    // Écran
    uint8_t brightness;      // 0-255
    // Son 
    uint8_t volume;          // 0-100
    // Réservé pour futur usage
    uint8_t _reserved[16];
    // Checksum
    uint32_t checksum;
};

static Data g_data{};
static bool g_loaded = false;

static uint32_t calc_checksum(const Data& d) {
    uint32_t h = 2166136261u;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&d);
    const size_t len = sizeof(Data) - sizeof(uint32_t); // Exclure le checksum
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

static void defaults() {
    memset(&g_data, 0, sizeof(g_data));
    g_data.magic = SETTINGS_MAGIC;
    g_data.pin_enabled = false;
    strncpy(g_data.pin_code, "0000", sizeof(g_data.pin_code));
    g_data.lock_timeout_ms = 30000; // 30 secondes par défaut
    g_data.brightness = 200;
    g_data.volume = 50;
    g_data.checksum = calc_checksum(g_data);
}

// Forward declaration so load() can call save() which is defined later.
static void save();

static void load() {
    if (g_loaded) return;
    // S'assurer que EEPROM est initialisée
    static bool eeprom_ready = false;
    if (!eeprom_ready) {
        EEPROM.begin(2048);
        eeprom_ready = true;
    }
    EEPROM.get(SETTINGS_ADDR, g_data);
    bool ok = (g_data.magic == SETTINGS_MAGIC) &&
              (g_data.checksum == calc_checksum(g_data));
    if (!ok) {
        defaults();
        save();
    } else {
        bool fixed = false;
        if (g_data.brightness < 10 || g_data.brightness > 255) {
            g_data.brightness = 200;
            fixed = true;
        }
        if (g_data.volume > 100) {
            g_data.volume = 50;
            fixed = true;
        }
        if (g_data.lock_timeout_ms > 30UL * 60UL * 1000UL) {
            g_data.lock_timeout_ms = 30000;
            fixed = true;
        }
        size_t pinLen = strnlen(g_data.pin_code, sizeof(g_data.pin_code));
        if (pinLen == 0 || pinLen > 6) {
            strncpy(g_data.pin_code, "0000", sizeof(g_data.pin_code));
            g_data.pin_code[sizeof(g_data.pin_code) - 1] = '\0';
            g_data.pin_enabled = false;
            fixed = true;
        }
        for (size_t i = 0; i < pinLen; i++) {
            if (g_data.pin_code[i] < '0' || g_data.pin_code[i] > '9') {
                strncpy(g_data.pin_code, "0000", sizeof(g_data.pin_code));
                g_data.pin_code[sizeof(g_data.pin_code) - 1] = '\0';
                g_data.pin_enabled = false;
                fixed = true;
                break;
            }
        }
        if (fixed) save();
    }
    g_loaded = true;
}

static void save() {
    g_data.checksum = calc_checksum(g_data);
    EEPROM.put(SETTINGS_ADDR, g_data);
#if defined(ARDUINO_ARCH_RP2040) || defined(ESP32) || defined(ESP8266)
    EEPROM.commit();
#endif
}

// --- Accesseurs ---

static bool isPinEnabled() { load(); return g_data.pin_enabled; }
static void setPinEnabled(bool v) { load(); g_data.pin_enabled = v; save(); }

static const char* getPinCode() { load(); return g_data.pin_code; }
static void setPinCode(const char* code) {
    load();
    memset(g_data.pin_code, 0, sizeof(g_data.pin_code));
    strncpy(g_data.pin_code, code, sizeof(g_data.pin_code) - 1);
    save();
}

static uint32_t getLockTimeout() { load(); return g_data.lock_timeout_ms; }
static void setLockTimeout(uint32_t ms) { load(); g_data.lock_timeout_ms = ms; save(); }

static uint8_t getBrightness() { load(); return g_data.brightness; }
static void setBrightness(uint8_t v) {
    load();
    if (v < 10) v = 10;
    g_data.brightness = v;
    pinMode(BACKLIGHT_PIN, OUTPUT);
    analogWrite(BACKLIGHT_PIN, v);
    save();
}

static uint8_t getVolume() { load(); return g_data.volume; }
static void setVolume(uint8_t v) {
    load();
    if (v > 100) v = 100;
    g_data.volume = v;
    // Applique immédiatement l'état de l'ampli pour éviter une sortie bloquée après mute/unmute.
    audio_amp_enable(v > 0);
    save();
}

// Appliquer la luminosité au démarrage
static void applyBrightness() {
    load();
    uint8_t v = g_data.brightness;
    if (v < 10) v = 200;
    pinMode(BACKLIGHT_PIN, OUTPUT);
    analogWrite(BACKLIGHT_PIN, v);
}

} // namespace settings

#endif
