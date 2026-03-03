#ifndef SYSTEM_LOGGER_H
#define SYSTEM_LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <stdarg.h>

// ═══════════════════════════════════════════════════════════════════════════
//  Logger – Log dual Serial + LittleFS (/lte_log.txt)
//
//  Utilisation :
//    Logger::begin();          // dans setup() / LTE::init() — wipe + init
//    Logger::println("[LTE] message");
//    Logger::printf("[LTE] val=%d\n", val);
//
//  Le fichier est limité à LOG_MAX_BYTES ; au-delà, il est purgé
//  et un marqueur "--- LOG TRONQUÉ ---" est inséré.
// ═══════════════════════════════════════════════════════════════════════════

class Logger {
public:
    static constexpr const char* LOG_PATH    = "/lte_log.txt";
    static constexpr size_t      LOG_MAX     = 56 * 1024; // 56 KB max

private:
    static bool   s_fs_ok;
    static size_t s_bytes_written;

    static void _append(const char* buf, size_t len) {
        if (!s_fs_ok || len == 0) return;

        // Tronquer si dépassement
        if (s_bytes_written + len > LOG_MAX) {
            File f = LittleFS.open(LOG_PATH, "w");
            if (f) {
                const char* trunc = "\n--- LOG TRONQUÉ (limite 56 KB) ---\n";
                f.print(trunc);
                f.close();
                s_bytes_written = strlen(trunc);
            } else {
                return;
            }
        }

        File f = LittleFS.open(LOG_PATH, "a");
        if (!f) return;
        f.write((const uint8_t*)buf, len);
        f.close();
        s_bytes_written += len;
    }

public:
    // Initialise : monte FS si besoin, wipe le fichier log
    static void begin() {
        if (!LittleFS.begin()) {
            Serial.println("[Logger] ERREUR: LittleFS non disponible");
            s_fs_ok = false;
            return;
        }
        s_fs_ok = true;
        // Wipe à chaque boot
        LittleFS.remove(LOG_PATH);
        File f = LittleFS.open(LOG_PATH, "w");
        if (f) {
            f.println("=== LOG DÉMARRAGE ===");
            s_bytes_written = 21; // len de la ligne ci-dessus
            f.close();
        }
        // Serial.println("[Logger] Log initialisé → " LOG_PATH);
    }

    static void println(const String& s) {
        Serial.println(s);
        String line = s + "\n";
        _append(line.c_str(), line.length());
    }

    static void print(const String& s) {
        Serial.print(s);
        _append(s.c_str(), s.length());
    }

    // printf style (buffer interne 256 octets — suffisant pour les lignes AT)
    static void printf(const char* fmt, ...) {
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n > 0) {
            Serial.print(buf);
            _append(buf, (size_t)min(n, 255));
        }
    }

    static bool isOk()          { return s_fs_ok; }
    static size_t bytesWritten() { return s_bytes_written; }
};

// Définitions statiques
bool   Logger::s_fs_ok        = false;
size_t Logger::s_bytes_written = 0;

#endif // SYSTEM_LOGGER_H
