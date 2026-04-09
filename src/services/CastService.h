#ifndef SERVICES_CAST_SERVICE_H
#define SERVICES_CAST_SERVICE_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "pico/mutex.h"
#include "../system/BackgroundServices.h"
#include "../system/NotificationCenter.h"
#include "../system/LTE.h"

class CastService : public IBackgroundService {
private:
    // <-- Règle le conflit avec TelegramNotifyService !
    static constexpr unsigned long CAST_POLL_MS = 10000; 

    String castEndpoint = "http://pizero.local:5010"; 
    unsigned long last_check = 0;

    // --- Mutex Natif Pico ---
    mutex_t state_mutex;
    bool mutex_initialized = false;
    
    char current_title[64] = "";
    char current_artist[64] = "Artiste inconnu";
    char current_state[16] = "IDLE";

    // --- Drapeaux de commandes ---
    volatile bool req_toggle = false;
    volatile bool req_next = false;
    volatile bool req_prev = false;

    void send_command(const String& endpoint) {
        if (!LTE::isReadyForData()) return;
        HTTPClient http;
        http.setTimeout(2500);
        http.begin(castEndpoint + endpoint);
        int httpCode = http.GET();
        http.end();
        Serial.printf("[Cast] Commande %s (Code: %d)\n", endpoint.c_str(), httpCode);
    }

    void set_state(const char* t, const char* a, const char* s) {
        if (!mutex_initialized) return;
        
        mutex_enter_blocking(&state_mutex); // On verrouille
        strncpy(current_title, t, 63); current_title[63] = '\0';
        strncpy(current_artist, a, 63); current_artist[63] = '\0';
        strncpy(current_state, s, 15); current_state[15] = '\0';
        mutex_exit(&state_mutex); // On déverrouille
    }

public:
    const char* name() const override { return "CastService"; }

    void begin() override {
        Serial.printf("[Cast] CastService begin\n");
        mutex_init(&state_mutex); // Initialisation du mutex Pico
        mutex_initialized = true;
        last_check = millis() - CAST_POLL_MS; 
    }

    void update1() override {
        if (!LTE::isReadyForData()) return;

        // Dépilement asynchrone des commandes UI
        if (req_toggle) { req_toggle = false; send_command("/toggle"); last_check = millis() - CAST_POLL_MS + 2000; return; }
        if (req_next)   { req_next = false;   send_command("/next");   last_check = millis() - CAST_POLL_MS + 2000; return; }
        if (req_prev)   { req_prev = false;   send_command("/prev");   last_check = millis() - CAST_POLL_MS + 2000; return; }

        const unsigned long now = millis();
        if ((now - last_check) < CAST_POLL_MS) return;
        last_check = now;

        HTTPClient http;
        http.setTimeout(4000);
        http.begin(castEndpoint + "/info");

        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, payload);

            if (!error) {
                const char* title = doc["titre"] | "Aucun titre";
                const char* artist = doc["artiste"] | "Artiste inconnu";
                const char* state = doc["etat"] | "IDLE";

                String old_title = getTitle();
                String old_state = getState();

                if (String(title) != old_title && String(state) == "PLAYING") {
                    set_state(title, artist, state);
                    char body[96];
                    snprintf(body, sizeof(body), "%s", artist);
                    notifications::push("Audio Cast", title, body);
                } 
                else if (String(state) != old_state) {
                    set_state(title, artist, state);
                    if (String(state) == "PAUSED" && String(title) != "Aucun titre") {
                        notifications::push("Audio Cast", "En pause", title);
                    } else if (String(state) == "PLAYING") {
                        notifications::push("Audio Cast", "Lecture reprise", title);
                    }
                } 
                else {
                    set_state(title, artist, state);
                }
            }
        }
        http.end();
    }

    void toggle() { req_toggle = true; }
    void next()   { req_next = true; }
    void prev()   { req_prev = true; }

    // --- GETTERS (avec protection Mutex) ---
    String getTitle() {
        if (!mutex_initialized) return String("...");
        mutex_enter_blocking(&state_mutex);
        String res(current_title);
        mutex_exit(&state_mutex);
        return res;
    }
    
    String getArtist() {
        if (!mutex_initialized) return String("...");
        mutex_enter_blocking(&state_mutex);
        String res(current_artist);
        mutex_exit(&state_mutex);
        return res;
    }
    
    String getState() {
        if (!mutex_initialized) return String("IDLE");
        mutex_enter_blocking(&state_mutex);
        String res(current_state);
        mutex_exit(&state_mutex);
        return res;
    }
    
    bool isMediaActive() {
        String s = getState();
        return (s == "PLAYING" || s == "PAUSED");
    }
};

namespace cast_service {
    inline CastService& instance() {
        static CastService s;
        return s;
    }
}

#endif