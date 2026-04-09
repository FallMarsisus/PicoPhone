#ifndef SYSTEM_LOGGER_H
#define SYSTEM_LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <stdarg.h>
#include <pico/mutex.h>

// ═══════════════════════════════════════════════════════════════════════════
//  Logger – Log dual Serial + LittleFS (/lte_log.txt)
//  Thread-safe pour utilisation Multicore RP2040/RP2350
// ═══════════════════════════════════════════════════════════════════════════

class Logger {
public:
    static constexpr const char* LOG_PATH    = "/lte_log.txt";
    static constexpr size_t      LOG_MAX     = 56 * 1024; // 56 KB max

private:
    static bool     s_fs_ok;
    static size_t   s_bytes_written;
    static mutex_t  s_log_mutex;
    static bool     s_mutex_init;

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
    static void begin() {
        if (!s_mutex_init) {
            mutex_init(&s_log_mutex);
            s_mutex_init = true;
        }

        mutex_enter_blocking(&s_log_mutex);
        if (!LittleFS.begin()) {
            Serial.println("[Logger] ERREUR: LittleFS non disponible");
            s_fs_ok = false;
            mutex_exit(&s_log_mutex);
            return;
        }
        s_fs_ok = true;
        LittleFS.remove(LOG_PATH);
        File f = LittleFS.open(LOG_PATH, "w");
        if (f) {
            f.println("=== LOG DÉMARRAGE ===");
            s_bytes_written = 21; 
            f.close();
        }
        mutex_exit(&s_log_mutex);
    }

    static void println(const String& s) {
        if (!s_mutex_init) return;
        mutex_enter_blocking(&s_log_mutex);
        Serial.println(s);
        String line = s + "\n";
        _append(line.c_str(), line.length());
        mutex_exit(&s_log_mutex);
    }

    static void print(const String& s) {
        if (!s_mutex_init) return;
        mutex_enter_blocking(&s_log_mutex);
        Serial.print(s);
        _append(s.c_str(), s.length());
        mutex_exit(&s_log_mutex);
    }

    static void printf(const char* fmt, ...) {
        if (!s_mutex_init) return;
        char buf[256];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n > 0) {
            mutex_enter_blocking(&s_log_mutex);
            Serial.print(buf);
            _append(buf, (size_t)min(n, 255));
            mutex_exit(&s_log_mutex);
        }
    }

    static bool isOk()          { return s_fs_ok; }
    static size_t bytesWritten() { return s_bytes_written; }
};

bool    Logger::s_fs_ok        = false;
size_t  Logger::s_bytes_written = 0;
mutex_t Logger::s_log_mutex;
bool    Logger::s_mutex_init   = false;

#endif // SYSTEM_LOGGER_H
