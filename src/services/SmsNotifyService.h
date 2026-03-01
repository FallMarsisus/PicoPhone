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
    String accumulated_sms = ""; 
    
    // Machine d'état
    bool is_initialized = false;
    unsigned long last_init_try = 0;
    
    bool waiting_for_msg_text = false;
    unsigned long msg_receive_start = 0;
    String current_incoming_number = "";
    String current_msg_index = "";

    // NOUVEAU: Vrai parseur type CSV pour extraire proprement les expéditeurs
    String get_csv_field(const String& line, int index) {
        int current_idx = 0;
        bool in_quotes = false;
        String field = "";
        
        for (unsigned int i = 0; i < line.length(); i++) {
            char c = line[i];
            if (c == '"') {
                in_quotes = !in_quotes; // On rentre ou on sort des guillemets
            } else if (c == ',' && !in_quotes) {
                if (current_idx == index) {
                    field.trim();
                    return field;
                }
                current_idx++;
                field = "";
            } else {
                if (current_idx == index) {
                    field += c;
                }
            }
        }
        
        // Pour le tout dernier champ de la ligne
        if (current_idx == index) {
            field.trim();
            return field;
        }
        return "";
    }

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

    // NOUVEAU: Fonction dédiée pour finaliser la réception d'un SMS et tout reset
    void finalize_incoming_sms() {
        time_t tnow; time(&tnow);
        accumulated_sms.trim();
        
        Serial.print("[SMS] Texte complet recu de ");
        Serial.print(current_incoming_number);
        Serial.print(" : ");
        Serial.println(accumulated_sms);

        if (accumulated_sms.length() > 0 && current_incoming_number.length() > 0) {
            upsert_contact(current_incoming_number, accumulated_sms);
            append_message(current_incoming_number, accumulated_sms, (long)tnow);

            char body[96];
            strncpy(body, accumulated_sms.c_str(), sizeof(body) - 1);
            body[sizeof(body) - 1] = '\0';
            notifications::push((String(LV_SYMBOL_KEYBOARD) + " Nouveau SMS").c_str(), current_incoming_number.c_str(), body);
        }

        // On efface le SMS de la SIM si on l'a lu via l'index
        if (current_msg_index.length() > 0) {
            send_at("AT+CMGD=" + current_msg_index);
        }
        
        // Reset des variables
        waiting_for_msg_text = false;
        accumulated_sms = ""; 
        current_msg_index = "";
        current_incoming_number = "";
    }

public:
    const char* name() const override { return "SmsNotifyService"; }

    void send_at(const String &cmd) {
        Serial.print("[SMS->GSM] ");
        Serial.println(cmd);
        Serial1.print(cmd);
        Serial1.print("\r"); 
    }

    void begin() override {
        fs_ok = LittleFS.begin();
        is_initialized = false;
    }

void update1() override {
        // Initialisation du module
        if (!is_initialized && millis() - last_init_try > 6000) {
            send_at("ATE0"); delay(100);
            send_at("AT+CMEE=2"); delay(100); 
            send_at("AT+CSCS=\"GSM\""); delay(100); 
            send_at("AT+CMGF=1"); delay(100);
            send_at("AT+CNMI=2,1,0,0,0"); delay(100);
            
            // NOUVEAU : On ordonne au module d'arrêter de spammer les statuts de connexion (+CGEV) !
            send_at("AT+CGEREP=0,0"); delay(100);
            
            is_initialized = true;
            last_init_try = millis();
        }

        // Timeout de sécurité (3 secondes sans rien recevoir)
        if (waiting_for_msg_text && (millis() - msg_receive_start > 3000)) {
            Serial.println("[SMS] Timeout attente texte, finalisation...");
            finalize_incoming_sms();
        }

        while (Serial1.available()) {
            char c = Serial1.read();

            if (c == '\n') {
                serial_buffer.trim();
                
                // --- SÉCURITÉ ANTI-DÉBORDEMENT ---
                if (waiting_for_msg_text && (serial_buffer.startsWith("+CMTI:") || serial_buffer.startsWith("+CMT:") || serial_buffer.startsWith("+CMGR:"))) {
                    Serial.println("[SMS] SÉCURITÉ : OK manqué, sauvegarde forcée du SMS précédent !");
                    finalize_incoming_sms(); 
                }

                if (waiting_for_msg_text) {
                    if (serial_buffer == "OK") {
                        finalize_incoming_sms(); // Fin normale du SMS
                    }
                    // NOUVEAU : On ignore les URC réseau qui viendraient polluer le texte
                    else if (serial_buffer.startsWith("+CGEV:") || serial_buffer.startsWith("+CREG:") || serial_buffer.startsWith("+CEREG:")) {
                        Serial.println("[SMS] Ignoré : Notification réseau pendant la lecture du SMS");
                    }
                    else if (serial_buffer.length() > 0) { // On évite d'empiler des sauts de lignes vides inutiles
                        if (accumulated_sms.length() > 0) {
                            accumulated_sms += "\n"; 
                        }
                        accumulated_sms += serial_buffer;
                        msg_receive_start = millis(); // Reset du timeout
                    }
                }
                
                // --- TRAITEMENT DES COMMANDES ---
                if (!waiting_for_msg_text && serial_buffer.length() > 0 && serial_buffer != "OK") {
                    Serial.print("[RX GSM] ");
                    Serial.println(serial_buffer);

                    if (serial_buffer == "SMS Ready" || serial_buffer == "Call Ready" || serial_buffer == "PB DONE") {
                        Serial.println("[SMS] Module pret ! Configuration...");
                        send_at("ATE0"); delay(200);
                        send_at("AT+CMGF=1"); delay(200);
                        send_at("AT+CNMI=2,1,0,0,0"); delay(200);
                        send_at("AT+CGEREP=0,0"); // Rappel au cas où
                        is_initialized = true;
                    }
                    else if (serial_buffer.startsWith("+CMT:")) {
                        String data = serial_buffer.substring(5); 
                        current_incoming_number = get_csv_field(data, 0); 
                        current_msg_index = ""; 
                        accumulated_sms = "";
                        waiting_for_msg_text = true; 
                        msg_receive_start = millis();
                    }
                    else if (serial_buffer.startsWith("+CMTI:")) {
                        String data = serial_buffer.substring(6);
                        current_msg_index = get_csv_field(data, 1); 
                        if (current_msg_index.length() > 0) {
                            send_at("AT+CMGR=" + current_msg_index);
                        }
                    }
                    else if (serial_buffer.startsWith("+CMGR:")) {
                        String data = serial_buffer.substring(6);
                        current_incoming_number = get_csv_field(data, 1); 
                        accumulated_sms = "";
                        waiting_for_msg_text = true; 
                        msg_receive_start = millis();
                    }
                }
                serial_buffer = "";
            } 
            else if (c != '\r') {
                serial_buffer += c;
                if (serial_buffer == "> ") {
                    serial_buffer = ""; 
                }
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