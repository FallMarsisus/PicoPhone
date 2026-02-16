#ifndef SERVICES_TELEGRAM_NOTIFY_SERVICE_H
#define SERVICES_TELEGRAM_NOTIFY_SERVICE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../system/BackgroundServices.h"
#include "../system/NotificationCenter.h"
#include "../system/Secrets.h"

class TelegramNotifyService : public IBackgroundService {
private:
    WiFiClientSecure client;
    UniversalTelegramBot* bot = nullptr;
    unsigned long last_check = 0;
    static constexpr unsigned long POLL_MS = 3500;
    bool fs_ok = false;

    struct ContactItem {
        String id;
        String name;
        String preview;
    };

    bool load_contact_by_id(const String& chat_id, ContactItem& out, int* idx_out = nullptr) {
        if (!fs_ok) return false;
        if (!LittleFS.exists("/contacts.json")) return false;

        File f = LittleFS.open("/contacts.json", "r");
        if (!f) return false;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err || !doc.is<JsonArray>()) return false;

        JsonArray arr = doc.as<JsonArray>();
        int idx = 0;
        for (JsonObject obj : arr) {
            String id = obj["id"] | "";
            if (id == chat_id) {
                out.id = id;
                out.name = obj["n"] | "";
                out.preview = obj["p"] | "";
                if (idx_out) *idx_out = idx;
                return true;
            }
            idx++;
        }
        return false;
    }

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
        client.setInsecure();
        client.setTimeout(1200);
        if (!bot) {
            bot = new UniversalTelegramBot(TG_BOT_TOKEN, client);
            bot->longPoll = 0;
        }
        last_check = millis();
    }

    void update1() override {
        if (!bot) return;
        if (WiFi.status() != WL_CONNECTED) return;

        const unsigned long now = millis();
        if ((now - last_check) < POLL_MS) return;
        last_check = now;

        int num = bot->getUpdates(bot->last_message_received + 1);
        if (num <= 0) return;

        for (int i = 0; i < num; i++) {
            String chat_id = String(bot->messages[i].chat_id);
            String from_name = String(bot->messages[i].from_name);
            String text = String(bot->messages[i].text);
            long ts = bot->messages[i].date.toInt();
            if (ts == 0) {
                time_t tnow;
                time(&tnow);
                ts = (long)tnow;
            }
            if (from_name.length() == 0) from_name = "Telegram";

            upsert_contact(chat_id, from_name, text);
            append_message(chat_id, text, ts);

            char body[96];
            strncpy(body, text.c_str(), sizeof(body) - 1);
            body[sizeof(body) - 1] = '\0';
            notifications::push("Telegram", from_name.c_str(), body);
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
