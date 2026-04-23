#ifndef SERVICES_SMS_NOTIFY_SERVICE_H
#define SERVICES_SMS_NOTIFY_SERVICE_H

// SmsNotifyService - Persiste et notifie les SMS entrants
// REGLE : Ne touche PAS Serial1. Les SMS arrivent via LTE::popIncomingSms().

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../system/BackgroundServices.h"
#include "../system/NotificationCenter.h"
#include "../system/LTE.h"
#include "../system/UnifiedContacts.h"

class SmsNotifyService : public IBackgroundService {
private:
    bool fs_ok = false;

    void upsert_contact(const String& number, const String& preview) {
        if (!fs_ok) return;
        JsonDocument doc;
        JsonArray arr;
        if (LittleFS.exists("/sms_contacts.json")) {
            File fr = LittleFS.open("/sms_contacts.json", "r");
            if (fr) { deserializeJson(doc, fr); fr.close(); }
        }
        if (doc.is<JsonArray>()) arr = doc.as<JsonArray>();
        else                     arr = doc.to<JsonArray>();

        bool found = false;
        for (JsonObject obj : arr) {
            if (String(obj["id"] | "") == number) {
                obj["n"] = unified_contacts::display_name_for_phone(number, String(obj["n"] | number));
                obj["p"] = preview;
                found = true;
                break;
            }
        }
        if (!found) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = number;
            obj["n"]  = unified_contacts::display_name_for_phone(number, number);
            obj["p"]  = preview;
        }
        File fw = LittleFS.open("/sms_contacts.json", "w");
        if (fw) { serializeJson(arr, fw); fw.close(); }
        Serial.printf("[SMS] upsert contact '%s' preview='%.30s'\n",
                      number.c_str(), preview.c_str());
    }

    void append_message(const String& number, const String& text, long ts) {
        if (!fs_ok) return;
        String path = "/sms_" + number + ".json";
        JsonDocument doc;
        JsonArray arr;
        if (LittleFS.exists(path)) {
            File fr = LittleFS.open(path, "r");
            if (fr) { deserializeJson(doc, fr); fr.close(); }
        }
        if (doc.is<JsonArray>()) arr = doc.as<JsonArray>();
        else                     arr = doc.to<JsonArray>();

        JsonObject obj = arr.add<JsonObject>();
        obj["t"]  = text;
        obj["m"]  = false;
        obj["ts"] = ts;
        while (arr.size() > 20) arr.remove(0);

        File fw = LittleFS.open(path, "w");
        if (fw) { serializeJson(arr, fw); fw.close(); }
        Serial.printf("[SMS] message appended for '%s' ts=%ld\n",
                      number.c_str(), ts);
    }

public:
    const char* name() const override { return "SmsNotifyService"; }

    void begin() override {
        fs_ok = LittleFS.begin();
        Serial.printf("[SMS] SmsNotifyService begin (fs_ok=%d)\n", (int)fs_ok);
    }

    // Core 1 : depile les SMS de la queue LTE et les persiste/notifie
    void update1() override {
        char number[32];
        char text[281];
        long ts;

        while (LTE::popIncomingSms(number, text, &ts)) {
            Serial.printf("[SMS] SMS entrant de '%s': \"%.50s\"\n", number, text);

            String num_s  = String(number);
            String text_s = String(text);
            String display_name = unified_contacts::display_name_for_phone(num_s, num_s);

            upsert_contact(num_s, text_s);
            append_message(num_s, text_s, ts);

            char body[96];
            strncpy(body, text, sizeof(body)-1);
            body[sizeof(body)-1] = '\0';

            notifications::push(
                (String(LV_SYMBOL_KEYBOARD) + " Nouveau SMS").c_str(),
                display_name.c_str(),
                body
            );
            Serial.printf("[SMS] SMS traite OK (de='%s')\n", number);
        }
    }
};

namespace sms_service {
    inline SmsNotifyService& instance() {
        static SmsNotifyService s;
        return s;
    }
}

#endif
