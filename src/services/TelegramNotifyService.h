#ifndef SERVICES_TELEGRAM_NOTIFY_SERVICE_H
#define SERVICES_TELEGRAM_NOTIFY_SERVICE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../system/BackgroundServices.h"
#include "../system/LTE.h"
#include "../system/NotificationCenter.h"
#include "../system/Secrets.h"

inline void (*telegram_incoming_cb)(String chat_id, String name, String text, long ts) = nullptr;

class TelegramNotifyService : public IBackgroundService {
private:
    unsigned long last_check = 0;
    long last_update_id = 0; 
    static constexpr unsigned long POLL_MS = 20000; // Vérification toutes les 10 secondes
    bool fs_ok = false;

    // --- FILE D'ATTENTE (Boîte aux lettres Core 1 -> Core 0) ---
    struct TgMsg {
        char chat_id[32];
        char from_name[32];
        char text[128]; // Limité à 128 caractères pour économiser la RAM
        long ts;
    };
    static constexpr uint8_t TG_Q_SIZE = 5;
    volatile uint8_t tg_q_head = 0;
    volatile uint8_t tg_q_tail = 0;
    TgMsg tg_q[TG_Q_SIZE]{};

    bool push_q(const String& cid, const String& name, const String& txt, long t) {
        uint8_t tail = __atomic_load_n(&tg_q_tail, __ATOMIC_RELAXED);
        uint8_t next = (tail + 1) % TG_Q_SIZE;
        if (next == __atomic_load_n(&tg_q_head, __ATOMIC_ACQUIRE)) return false; // Queue pleine
        
        strncpy(tg_q[tail].chat_id, cid.c_str(), 31); tg_q[tail].chat_id[31] = '\0';
        strncpy(tg_q[tail].from_name, name.c_str(), 31); tg_q[tail].from_name[31] = '\0';
        strncpy(tg_q[tail].text, txt.c_str(), 127); tg_q[tail].text[127] = '\0';
        tg_q[tail].ts = t;
        
        __atomic_store_n(&tg_q_tail, next, __ATOMIC_RELEASE);
        return true;
    }

    bool pop_q(TgMsg& out) {
        uint8_t head = __atomic_load_n(&tg_q_head, __ATOMIC_RELAXED);
        uint8_t tail = __atomic_load_n(&tg_q_tail, __ATOMIC_ACQUIRE);
        if (head == tail) return false; // Queue vide
        
        out = tg_q[head];
        __atomic_store_n(&tg_q_head, (head + 1) % TG_Q_SIZE, __ATOMIC_RELEASE);
        return true;
    }

    // --- GESTION FICHIERS (CORE 0 UNIQUEMENT) ---
    void upsert_contact(const String& chat_id, const String& name, const String& preview) {
        if (!fs_ok) return;

        JsonDocument doc;
        JsonArray arr;

        if (LittleFS.exists("/contacts.json")) {
            File fr = LittleFS.open("/contacts.json", "r");
            if (fr) {
                DeserializationError err = deserializeJson(doc, fr);
                fr.close();
                if (err || !doc.is<JsonArray>()) doc.clear();
            }
        }

        if (doc.is<JsonArray>()) arr = doc.as<JsonArray>();
        else arr = doc.to<JsonArray>();

        bool found = false;
        for (JsonObject obj : arr) {
            String id = obj["id"] | "";
            if (id == chat_id) {
                if (name.length()) obj["n"] = name;
                obj["p"] = preview;
                found = true;
                break;
            }
        }

        if (!found) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = chat_id;
            obj["n"] = name;
            obj["p"] = preview;
        }

        File fw = LittleFS.open("/contacts.json", "w");
        if (!fw) return;
        serializeJson(arr, fw);
        fw.close();
    }

    void append_message(const String& chat_id, const String& text, long ts) {
        if (!fs_ok) return;
        String path = "/" + chat_id + ".json";

        JsonDocument doc;
        JsonArray arr;

        if (LittleFS.exists(path)) {
            File fr = LittleFS.open(path, "r");
            if (fr) {
                DeserializationError err = deserializeJson(doc, fr);
                fr.close();
                if (err || !doc.is<JsonArray>()) doc.clear();
            }
        }

        if (doc.is<JsonArray>()) arr = doc.as<JsonArray>();
        else arr = doc.to<JsonArray>();

        JsonObject obj = arr.add<JsonObject>();
        obj["t"] = text;
        obj["m"] = false;
        obj["ts"] = ts;

        while (arr.size() > 10) arr.remove(0);

        File fw = LittleFS.open(path, "w");
        if (!fw) return;
        serializeJson(arr, fw);
        fw.close();
    }

public:
    const char* name() const override { return "TelegramNotifyService"; }

    void begin() override {
        fs_ok = LittleFS.begin();
        last_check = millis();
    }

    // --- CORE 0 : TRAITEMENT DE LA BOÎTE AUX LETTRES ---
    void update() override {
        TgMsg m;
        // S'il y a de nouveaux messages téléchargés par le Core 1
        while (pop_q(m)) {
            String chat_id(m.chat_id);
            String from_name(m.from_name);
            String text(m.text);

            // On écrit sur LittleFS en toute sécurité depuis le Core 0
            upsert_contact(chat_id, from_name, text);
            append_message(chat_id, text, m.ts);

            // On prévient l'application Telegram si elle est ouverte
            if (telegram_incoming_cb) {
                telegram_incoming_cb(chat_id, from_name, text, m.ts);
            }

            // On lance la notification visuelle en haut de l'écran
            char body[96];
            strncpy(body, text.c_str(), sizeof(body) - 1);
            body[sizeof(body) - 1] = '\0';
            notifications::push(String(String(LV_SYMBOL_GPS) + " Telegram").c_str(), from_name.c_str(), body);
        }
    }

    // --- CORE 1 : RÉSEAU ET TÉLÉCHARGEMENT ---
    void update1() override {
        if (!LTE::isReadyForData()) return;



        const unsigned long now = millis();
        if ((now - last_check) < POLL_MS) return;
        last_check = now;

        String url = "https://api.telegram.org/bot" + String(TG_BOT_TOKEN) + "/getUpdates?offset=" + String(last_update_id + 1) + "&limit=5";
        String resp = LTE::httpGetBlocking(url, "", false);

        if (resp.length() > 0) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, resp);
            
            if (!err && doc.containsKey("ok") && doc["ok"].as<bool>()) {
                JsonArray arr = doc["result"].as<JsonArray>();
                
                for (JsonObject update : arr) {
                    long current_id = update["update_id"].as<long>();
                    if (current_id > last_update_id) {
                        last_update_id = current_id;
                    }

                    if (update.containsKey("message")) {
                        JsonObject msg = update["message"];
                        
                        // SÉCURITÉ : On ignore les messages qui n'ont pas de texte (photos, stickers)
                        if (!msg.containsKey("text")) continue;

                        // SÉCURITÉ : On force le parsing de l'ID 64 bits en String pure
                        String chat_id = msg["chat"]["id"].as<String>();
                        String text = msg["text"].as<String>();
                        
                        String from_name = "Inconnu";
                        if (msg.containsKey("from") && msg["from"].containsKey("first_name")) {
                            from_name = msg["from"]["first_name"].as<String>();
                        }
                        
                        long ts = 0;
                        if (msg.containsKey("date")) {
                            ts = msg["date"].as<long>();
                        } else {
                            time_t tnow; time(&tnow); ts = (long)tnow;
                        }

                        // On glisse le message sécurisé dans la boîte aux lettres pour le Core 0
                        push_q(chat_id, from_name, text, ts);
                    }
                }
            }
        }
    }
};

namespace telegram_service {
    inline TelegramNotifyService& instance() {
        static TelegramNotifyService s;
        return s;
    }
}

#endif