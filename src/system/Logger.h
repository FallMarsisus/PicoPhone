#ifndef SYSTEM_LOGGER_H
#define SYSTEM_LOGGER_H

#include <Arduino.h>
#include <pico/mutex.h> // IMPORTANT

class Logger {
private:
    static mutex_t log_mutex;
    static bool initialized;

public:
    static void begin() {
        if (!initialized) {
            mutex_init(&log_mutex);
            Serial.begin(115200);
            initialized = true;
        }
    }

    static bool isOk() { return initialized; }

    static void println(const char* msg) {
        if (!initialized) return;
        mutex_enter_blocking(&log_mutex); // Bloque si l'autre cœur écrit
        Serial.println(msg);
        mutex_exit(&log_mutex);           // Libère
    }

    static void printf(const char* format, ...) {
        if (!initialized) return;
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        mutex_enter_blocking(&log_mutex);
        Serial.print(buffer);
        mutex_exit(&log_mutex);
    }
};

// Dans un fichier .cpp (ou à la fin du .h avec inline/ifdef selon votre architecture) :
mutex_t Logger::log_mutex;
bool Logger::initialized = false;

#endif