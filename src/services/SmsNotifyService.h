#ifndef SERVICES_SMS_NOTIFY_SERVICE_H
#define SERVICES_SMS_NOTIFY_SERVICE_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../system/BackgroundServices.h"
#include "../system/NotificationCenter.h"

class SmsNotifyService : public IBackgroundService {
private:
    bool fs_ok = false;
    String serial_buffer = "";
    
    // Machine d'état
    bool is_initialized = false;
    unsigned long last_init_try = 0;
    
    bool waiting_for_msg_text = false;
    String current_incoming_number = "";
    String current_msg_index = ""; // Si vide, c'est un message direct (Flash SMS)

    void upsert_contact(const String& number, const String& preview) {
        if (!fs_ok) return;
        JsonDocument doc;
        JsonArray arr;
        if (LittleFS.exists("/sms_contacts.json")) {
            File fr = LittleFS.open("/sms_contacts.json", "r");
            if (fr) { deserializeJson(doc, fr); fr.close(); }
        }
        if (doc.is<JsonArray>()) arr = doc.as<JsonArray>();
        else arr = doc.to<JsonArray>();

        bool found = false;
        for (JsonObject obj : arr) {
            String id = obj["id"] | "";
            if (id == number) {
                obj["p"] = preview;
                found = true;
                break;
            }
        }
        if (!found) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = number;
            obj["n"] = number;
            obj["p"] = preview;
        }

        File fw = LittleFS.open("/sms_contacts.json", "w");
        if (fw) { serializeJson(arr, fw); fw.close(); }
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
        else arr = doc.to<JsonArray>();

        JsonObject obj = arr.add<JsonObject>();
        obj["t"] = text;
        obj["m"] = false;
        obj["ts"] = ts;

        while (arr.size() > 20) arr.remove(0);

        File fw = LittleFS.open(path, "w");
        if (fw) { serializeJson(arr, fw); fw.close(); }
    }

public:
    const char* name() const override { return "SmsNotifyService"; }

    void begin() override {
        fs_ok = LittleFS.begin();
        is_initialized = false;
    }

    void update1() override {
        // Sécurité au démarrage si le module était déjà allumé
        if (!is_initialized && millis() - last_init_try > 6000) {
            Serial1.println("ATE0"); delay(100);
            Serial1.println("AT+CMEE=2"); delay(100); // <-- MAGIE : Active les erreurs détaillées en texte !
            Serial1.println("AT+CSCS=\"GSM\""); delay(100); 
            Serial1.println("AT+CMGF=1"); delay(100);
            Serial1.println("AT+CNMI=2,1,0,0,0");
            is_initialized = true;
        }

        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                serial_buffer.trim();
                if (serial_buffer.length() == 0) continue;

                Serial.print("[RX GSM] ");
                Serial.println(serial_buffer);

                if (serial_buffer == "SMS Ready" || serial_buffer == "Call Ready") {
                    Serial.println("[SMS] Module pret ! Configuration...");
                    Serial1.println("ATE0"); 
                    delay(200);
                    Serial1.println("AT+CMEE=2"); // <-- MAGIE ICI AUSSI
                    delay(200);
                    Serial1.println("AT+CSCS=\"GSM\""); 
                    delay(200);
                    Serial1.println("AT+CMGF=1"); 
                    delay(200);
                    Serial1.println("AT+CNMI=2,1,0,0,0"); 
                    is_initialized = true;
                }
                else if (waiting_for_msg_text && serial_buffer != "OK") {
                    time_t tnow; time(&tnow);
                    
                    Serial.println("[SMS] Texte recu et sauvegarde !");
                    upsert_contact(current_incoming_number, serial_buffer);
                    append_message(current_incoming_number, serial_buffer, (long)tnow);

                    char body[96];
                    strncpy(body, serial_buffer.c_str(), sizeof(body) - 1);
                    body[sizeof(body) - 1] = '\0';
                    notifications::push((String(LV_SYMBOL_KEYBOARD) + " Nouveau SMS").c_str(), current_incoming_number.c_str(), body);

                    // On efface le SMS de la carte SIM seulement s'il y était stocké !
                    if (current_msg_index.length() > 0) {
                        Serial1.print("AT+CMGD=");
                        Serial1.println(current_msg_index);
                    }
                    waiting_for_msg_text = false;
                }
                // --- NOUVEAU : DÉTECTION DES FLASH SMS (Twitch, Auth, etc) ---
                else if (serial_buffer.startsWith("+CMT:")) {
                    Serial.println("[SMS] Flash SMS / Message direct recu !");
                    int first_quote = serial_buffer.indexOf('"');
                    if (first_quote > 0) {
                        int num_start = first_quote + 1;
                        int num_end = serial_buffer.indexOf('"', num_start);
                        if (num_start > 0 && num_end > num_start) {
                            current_incoming_number = serial_buffer.substring(num_start, num_end);
                            current_msg_index = ""; // Il n'y a pas d'index, il n'est pas sur la SIM
                            waiting_for_msg_text = true; // La ligne suivante sera le code Twitch
                        }
                    }
                }
                // --- DÉTECTION DES SMS NORMAUX (Stockés sur SIM) ---
                else if (serial_buffer.startsWith("+CMTI:")) {
                    int comma = serial_buffer.indexOf(',');
                    if (comma > 0) {
                        current_msg_index = serial_buffer.substring(comma + 1);
                        current_msg_index.trim();
                        Serial1.print("AT+CMGR=");
                        Serial1.println(current_msg_index);
                    }
                }
                else if (serial_buffer.startsWith("+CMGR:")) {
                    int first_quote = serial_buffer.indexOf(',', 7);
                    if (first_quote > 0) {
                        int num_start = serial_buffer.indexOf('"', first_quote) + 1;
                        int num_end = serial_buffer.indexOf('"', num_start);
                        if (num_start > 0 && num_end > num_start) {
                            current_incoming_number = serial_buffer.substring(num_start, num_end);
                            waiting_for_msg_text = true; 
                        }
                    }
                }
                serial_buffer = "";
            } 
            else if (c == '>') {
                Serial.println("[RX GSM] PROMPT > DETECTE");
                serial_buffer = ""; 
            } 
            else if (c != '\r') {
                serial_buffer += c;
            }
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