#ifndef WIFI_STORE_H
#define WIFI_STORE_H

#include <Arduino.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <time.h> // <--- AJOUT POUR L'HEURE

// Stockage très simple en EEPROM: une liste (SSID+PASS) + checksum.
namespace wifi_store {

static constexpr uint32_t MAGIC = 0x57494649u; // 'WIFI'
static constexpr uint8_t VERSION = 1;
static constexpr size_t MAX_NETWORKS = 6;
static constexpr size_t EEPROM_SIZE = 2048;
static constexpr int EEPROM_ADDR = 0;

struct Entry {
    char ssid[33];
    char pass[65];
};

struct Blob {
    uint32_t magic;
    uint8_t version;
    uint8_t count;
    uint16_t reserved;
    Entry entries[MAX_NETWORKS];
    uint32_t checksum;
};

static bool g_loaded = false;
static Blob g_blob{};

static uint32_t fnv1a32(const uint8_t* data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t calc_checksum(const Blob& b) {
    return fnv1a32(reinterpret_cast<const uint8_t*>(&b), sizeof(Blob) - sizeof(uint32_t));
}

static void begin() {
#if defined(ARDUINO_ARCH_RP2040) || defined(ESP32) || defined(ESP8266)
    static bool started = false;
    if (!started) {
        EEPROM.begin(EEPROM_SIZE);
        started = true;
    }
#endif
}

static void clear() {
    begin();
    memset(&g_blob, 0, sizeof(g_blob));
    g_blob.magic = MAGIC;
    g_blob.version = VERSION;
    g_blob.count = 0;
    g_blob.checksum = calc_checksum(g_blob);
    EEPROM.put(EEPROM_ADDR, g_blob);
#if defined(ARDUINO_ARCH_RP2040) || defined(ESP32) || defined(ESP8266)
    EEPROM.commit();
#endif
    g_loaded = true;
}

static void load() {
    begin();
    EEPROM.get(EEPROM_ADDR, g_blob);
    const bool ok = (g_blob.magic == MAGIC) && (g_blob.version == VERSION) && (g_blob.count <= MAX_NETWORKS) &&
                    (g_blob.checksum == calc_checksum(g_blob));
    if (!ok) {
        clear();
        return;
    }
    g_loaded = true;
}

static void save() {
    begin();
    g_blob.checksum = calc_checksum(g_blob);
    EEPROM.put(EEPROM_ADDR, g_blob);
#if defined(ARDUINO_ARCH_RP2040) || defined(ESP32) || defined(ESP8266)
    EEPROM.commit();
#endif
}

static size_t count() {
    if (!g_loaded) load();
    return g_blob.count;
}

static const Entry* get(size_t i) {
    if (!g_loaded) load();
    if (i >= g_blob.count) return nullptr;
    return &g_blob.entries[i];
}

static int find_ssid(const char* ssid) {
    if (!g_loaded) load();
    if (!ssid || !ssid[0]) return -1;
    for (size_t i = 0; i < g_blob.count; i++) {
        if (strncmp(g_blob.entries[i].ssid, ssid, sizeof(g_blob.entries[i].ssid)) == 0) return (int)i;
    }
    return -1;
}

static void promote_index(size_t idx) {
    if (!g_loaded) load();
    if (idx >= g_blob.count || idx == 0) return;
    Entry tmp = g_blob.entries[idx];
    for (size_t i = idx; i > 0; i--) {
        g_blob.entries[i] = g_blob.entries[i - 1];
    }
    g_blob.entries[0] = tmp;
    save();
}

static void upsert_success(const char* ssid, const char* pass) {
    if (!g_loaded) load();
    if (!ssid || !ssid[0] || !pass) return;

    int idx = find_ssid(ssid);
    if (idx < 0) {
        if (g_blob.count < MAX_NETWORKS) {
            idx = (int)g_blob.count;
            g_blob.count++;
        } else {
            idx = (int)(MAX_NETWORKS - 1);
        }
    }

    memset(g_blob.entries[idx].ssid, 0, sizeof(g_blob.entries[idx].ssid));
    memset(g_blob.entries[idx].pass, 0, sizeof(g_blob.entries[idx].pass));
    strncpy(g_blob.entries[idx].ssid, ssid, sizeof(g_blob.entries[idx].ssid) - 1);
    strncpy(g_blob.entries[idx].pass, pass, sizeof(g_blob.entries[idx].pass) - 1);
    save();
    promote_index((size_t)idx);
}

static void remove_ssid(const char* ssid) {
    if (!g_loaded) load();
    int idx = find_ssid(ssid);
    if (idx < 0) return;
    for (size_t i = (size_t)idx; i + 1 < g_blob.count; i++) {
        g_blob.entries[i] = g_blob.entries[i + 1];
    }
    if (g_blob.count > 0) g_blob.count--;
    save();
}

// -------------------- Auto-connect non bloquant --------------------

static bool g_ac_inited = false;
static bool g_ac_connecting = false;
static uint32_t g_ac_since = 0;
static uint32_t g_ac_cooldown_until = 0;
static int g_ac_index = -1;
static int g_ac_status_check_interval = 0;
static int g_last_status = WL_IDLE_STATUS;

static void autoconnect_init() {
    if (!g_loaded) load();
    if (g_ac_inited) return;
    WiFi.mode(WIFI_STA);
    g_ac_inited = true;
    g_last_status = (int)WiFi.status();
}

// Fonction appelée en boucle dans le main loop
static void autoconnect_tick() {
    autoconnect_init();

    const uint32_t now = millis();
    const int st = (int)WiFi.status();

    // DETECTION DE CONNEXION REUSSIE (Transition de "Pas Connecté" à "Connecté")
    if (st == (int)WL_CONNECTED && g_last_status != (int)WL_CONNECTED) {
        
        // 1. Mise à jour de la priorité du réseau
        String s = WiFi.SSID();
        if (s.length() > 0) {
            int idx = find_ssid(s.c_str());
            if (idx >= 0) promote_index((size_t)idx);
        }

        // 2. SYNCHRO HEURE NTP AUTOMATIQUE !
        // Configure l'heure dès que le WiFi revient
        configTime(3600, 3600, "fr.pool.ntp.org", "time.nist.gov");
    }
    g_last_status = st;

    // Si on est connecté, on ne fait rien de plus
    if (st == (int)WL_CONNECTED) {
        g_ac_connecting = false;
        return;
    }

    // Si aucun réseau enregistré ou en pause (cooldown)
    if (g_blob.count == 0) return;
    if (now < g_ac_cooldown_until) return;

    static constexpr uint32_t ATTEMPT_TIMEOUT_MS = 12000;
    static constexpr uint32_t BETWEEN_ATTEMPTS_MS = 2000;

    // Lancement d'une tentative
    if (!g_ac_connecting) {
        g_ac_index = (g_ac_index + 1) % (int)g_blob.count;
        const Entry& e = g_blob.entries[g_ac_index];
        if (e.ssid[0] == '\0') {
            g_ac_cooldown_until = now + BETWEEN_ATTEMPTS_MS;
            return;
        }

        WiFi.disconnect();
        delay(10);
        WiFi.begin(e.ssid, e.pass);
        g_ac_connecting = true;
        g_ac_since = now;
        return;
    }

    // Timeout de la tentative actuelle
    if (g_ac_connecting && (now - g_ac_since) > ATTEMPT_TIMEOUT_MS) {
        // Echec, on passe au suivant
        g_ac_connecting = false;
        g_ac_cooldown_until = now + BETWEEN_ATTEMPTS_MS;
        return;
    }
}

} // namespace wifi_store

#endif