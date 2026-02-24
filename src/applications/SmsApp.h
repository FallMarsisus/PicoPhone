#ifndef SMS_APP_H
#define SMS_APP_H

#include "App.h"
#include "AppManager.h"
#include "../plugins/lv_t9_keyboard.h"
#include <LittleFS.h>     
#include <ArduinoJson.h>  
#include <vector>
#include <time.h>

#define MAX_HISTORY 20
#define COL_BG_LIST  0x1C1C1E // Noir iOS
#define COL_BG_CHAT  0x000000 
#define COL_MSG_IN   0x3A3A3C // Gris foncé
#define COL_MSG_OUT  0x34C759 // Vert SMS classique
#define COL_TEXT     0xFFFFFF
#define COL_TIME     0xAEAEB2

struct SmsContact {
    String number;
    String name;
    String last_msg_preview; 
    bool has_new;
};

class SmsApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* view_contacts;
    lv_obj_t* list_cont;
    lv_obj_t* view_chat;
    lv_obj_t* msg_list;
    lv_obj_t* header_title;
    lv_obj_t* keyboard_cont;
    lv_obj_t* ta_visible;

    bool fs_ok = false;
    std::vector<SmsContact> contacts;
    int current_contact_idx = -1; 

    // --- Communication Inter-Coeurs ---
    volatile bool out_pending = false;
    char out_number[32] = {0};
    char out_text[256] = {0};

    enum NetEvtType : uint8_t { EVT_SEND_OK = 1, EVT_SEND_FAIL = 2 };
    struct NetEvt { uint8_t type; char text[256]; };
    volatile uint8_t netq_head = 0, netq_tail = 0;
    NetEvt netq[5]{};

    bool netq_push(const NetEvt& e) {
        uint8_t next = (netq_tail + 1) % 5;
        if (next == netq_head) return false;
        netq[netq_tail] = e;
        netq_tail = next;
        return true;
    }
    bool netq_pop(NetEvt& e) {
        if (netq_head == netq_tail) return false;
        e = netq[netq_head];
        netq_head = (netq_head + 1) % 5;
        return true;
    }

    void load_contacts_from_flash() {
        if (!fs_ok || !LittleFS.exists("/sms_contacts.json")) return;
        File f = LittleFS.open("/sms_contacts.json", "r");
        if (!f) return;
        JsonDocument doc;
        if (!deserializeJson(doc, f)) {
            contacts.clear();
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject obj : arr) {
                contacts.push_back({obj["id"].as<String>(), obj["n"].as<String>(), obj["p"].as<String>(), false});
            }
        }
        f.close();
    }

    void save_msg_to_flash(String number, String text, bool is_me, long ts) {
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
        obj["t"] = text; obj["m"] = is_me; obj["ts"] = ts;
        while (arr.size() > MAX_HISTORY) arr.remove(0);

        File fw = LittleFS.open(path, "w");
        if (fw) { serializeJson(arr, fw); fw.close(); }
    }

    void load_history_to_ui(String number) {
        lv_obj_clean(msg_list); 
        if (!fs_ok || !LittleFS.exists("/sms_" + number + ".json")) return;
        File f = LittleFS.open("/sms_" + number + ".json", "r");
        if (!f) return;
        JsonDocument doc;
        if (!deserializeJson(doc, f)) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject obj : arr) {
                time_t t = obj["ts"];
                struct tm* timeinfo = localtime(&t);
                char timeBuf[6];
                if(t > 10000) strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
                else strcpy(timeBuf, "??:??");
                add_bubble_to_ui(obj["t"], obj["m"], timeBuf);
            }
        }
        f.close();
    }

    static void go_back_event(lv_event_t* e) {
        SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
        if (app->current_contact_idx != -1) {
            app->current_contact_idx = -1;
            lv_obj_clean(app->msg_list);
            lv_obj_add_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(app->view_chat, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(app->view_contacts, LV_OBJ_FLAG_HIDDEN);
            app->refresh_contact_list_ui();
            lv_label_set_text(app->header_title, "Messages");
        } else {
            AppManager::switchTo(APP_HOME);
        }
    }

    static void btn_reply_event(lv_event_t* e) {
        SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
        lv_obj_clear_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
        lv_t9_kb_set_textarea(app->keyboard_cont, app->ta_visible);
        lv_obj_scroll_to_y(app->msg_list, 10000, LV_ANIM_ON);
    }

    static void kb_send_event(lv_event_t* e) {
        SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
        const char* text = lv_textarea_get_text(app->ta_visible);
        
        lv_obj_add_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);

        if (text && strlen(text) > 0 && app->current_contact_idx != -1) {
            const String number = app->contacts[app->current_contact_idx].number;
            strncpy(app->out_number, number.c_str(), sizeof(app->out_number) - 1);
            strncpy(app->out_text, text, sizeof(app->out_text) - 1);
            app->out_pending = true; // Déclenche l'envoi sur Core 1
        }
        lv_textarea_set_text(app->ta_visible, "");
    }

    static void kb_cancel_event(lv_event_t* e) {
        SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
        lv_obj_add_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(app->ta_visible, "");
    }

    void refresh_contact_list_ui() {
        lv_obj_clean(list_cont);
        for (size_t i = 0; i < contacts.size(); i++) {
            lv_obj_t* btn = lv_btn_create(list_cont);
            lv_obj_set_width(btn, lv_pct(100));
            lv_obj_set_height(btn, 65);
            lv_obj_set_style_bg_color(btn, lv_color_hex(COL_BG_LIST), 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, [](lv_event_t* e){
                SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
                int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
                app->current_contact_idx = idx;
                lv_obj_add_flag(app->view_contacts, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(app->view_chat, LV_OBJ_FLAG_HIDDEN);
                lv_label_set_text(app->header_title, app->contacts[idx].name.c_str());
                app->load_history_to_ui(app->contacts[idx].number);
                lv_obj_scroll_to_y(app->msg_list, 10000, LV_ANIM_OFF);
            }, LV_EVENT_CLICKED, this);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, contacts[i].name.c_str());
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 5, 5);

            lv_obj_t* sub = lv_label_create(btn);
            lv_label_set_text(sub, contacts[i].last_msg_preview.c_str());
            lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
            lv_obj_set_width(sub, 190);
            lv_obj_set_style_text_color(sub, lv_color_hex(0xaaaaaa), 0);
            lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 5, -5);
        }
    }

    void add_bubble_to_ui(const char* text, bool is_me, const char* timeStr) {
        lv_obj_t* bubble = lv_obj_create(msg_list);
        lv_obj_set_width(bubble, lv_pct(85));
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(bubble, 15, 0);
        lv_obj_set_style_pad_all(bubble, 10, 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        
        if (is_me) {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(COL_MSG_OUT), 0);
            lv_obj_align(bubble, LV_ALIGN_TOP_RIGHT, 0, 0);
        } else {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(COL_MSG_IN), 0);
            lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        lv_obj_t* l_text = lv_label_create(bubble);
        lv_label_set_text(l_text, text);
        lv_label_set_long_mode(l_text, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l_text, lv_pct(100)); 
        lv_obj_set_style_text_color(l_text, lv_color_hex(COL_TEXT), 0);

        lv_obj_t* l_time = lv_label_create(bubble);
        lv_label_set_text(l_time, timeStr);
        lv_obj_set_style_text_font(l_time, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l_time, lv_color_hex(COL_TIME), 0);
        lv_obj_align(l_time, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

public:
    void start(lv_obj_t* parent) override {
        fs_ok = LittleFS.begin();
        load_contacts_from_flash();

        main_bg = parent; 
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_black(), 0);

        // Header
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        header_title = lv_label_create(header);
        lv_label_set_text(header_title, "Messages");
        lv_obj_set_style_text_color(header_title, lv_color_white(), 0);
        lv_obj_center(header_title);
        
        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_back_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_hex(COL_MSG_OUT), 0); // Fleche Verte
        lv_obj_center(l_back);

        // VUE CONTACTS
        view_contacts = lv_obj_create(main_bg);
        lv_obj_set_size(view_contacts, 320, 430);
        lv_obj_align(view_contacts, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_opa(view_contacts, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(view_contacts, 0, 0);
        
        list_cont = lv_obj_create(view_contacts);
        lv_obj_set_size(list_cont, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(list_cont, 2, 0);

        // VUE CHAT
        view_chat = lv_obj_create(main_bg);
        lv_obj_set_size(view_chat, 320, 430);
        lv_obj_align(view_chat, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_color(view_chat, lv_color_hex(COL_BG_CHAT), 0);
        lv_obj_set_style_border_width(view_chat, 0, 0);
        lv_obj_add_flag(view_chat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view_chat, LV_OBJ_FLAG_SCROLLABLE);

        msg_list = lv_obj_create(view_chat);
        lv_obj_set_size(msg_list, 320, 310); 
        lv_obj_align(msg_list, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(msg_list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(msg_list, 0, 0);
        lv_obj_set_flex_flow(msg_list, LV_FLEX_FLOW_COLUMN);

        lv_obj_t* footer = lv_obj_create(view_chat);
        lv_obj_set_size(footer, 320, 80);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 10);
        lv_obj_set_style_bg_color(footer, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t* btn_reply = lv_btn_create(footer);
        lv_obj_set_size(btn_reply, 200, 40);
        lv_obj_center(btn_reply);
        lv_obj_add_event_cb(btn_reply, btn_reply_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_rep = lv_label_create(btn_reply);
        lv_label_set_text(l_rep, "iMessage");
        lv_obj_center(l_rep);

        // CLAVIER
        ta_visible = lv_textarea_create(main_bg);
        lv_obj_set_size(ta_visible, 220, 40);
        lv_obj_align(ta_visible, LV_ALIGN_BOTTOM_MID, 0, -170);
        lv_obj_set_style_border_color(ta_visible, lv_color_hex(COL_MSG_OUT), 0);
        lv_obj_set_style_border_width(ta_visible, 2, 0);
        lv_obj_add_flag(ta_visible, LV_OBJ_FLAG_HIDDEN);

        keyboard_cont = lv_t9_kb_create(main_bg);
        lv_obj_add_flag(keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(keyboard_cont, kb_send_event, LV_EVENT_READY, this);
        lv_obj_add_event_cb(keyboard_cont, kb_cancel_event, LV_EVENT_CANCEL, this);

        refresh_contact_list_ui();
    }
    
    void update() override {
        // UI Updates (Core 0)
        NetEvt ev{};
        while (netq_pop(ev)) {
            if (ev.type == EVT_SEND_OK) {
                time_t tnow; time(&tnow);
                save_msg_to_flash(String(out_number), String(ev.text), true, (long)tnow);
                
                if (current_contact_idx != -1) {
                    char timeBuf[6];
                    struct tm* timeinfo = localtime(&tnow);
                    strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
                    add_bubble_to_ui(ev.text, true, timeBuf);
                    lv_obj_scroll_to_y(msg_list, 10000, LV_ANIM_ON);
                }
            } else {
                add_bubble_to_ui("Erreur d'envoi", true, "!!");
            }
        }
    }

    void update1() override {
        // Core 1 : Routines d'envoi AT bloquantes
        if (out_pending) {
            Serial.println("[SmsApp] Tentative d'envoi...");

            // 1. ECHAP : Annule tout SMS précédent resté coincé
            Serial1.print((char)27); 
            delay(500);

            // 2. Préparation du destinataire
            Serial1.print("AT+CMGS=\"");
            Serial1.print(out_number);
            Serial1.println("\"");
            
            // 3. On lui laisse VRAIMENT le temps de générer le prompt '>'
            delay(1000); 
            
            // 4. On écrit le texte
            Serial1.print(out_text);
            delay(100);
            
            // 5. On envoie CTRL+Z de manière sûre (char 26)
            Serial1.print((char)26); 

            Serial.println("[SmsApp] Message envoye au module, attente reseau...");

            // Attendre la validation du réseau (ça peut prendre de 2 à 5 secondes)
            delay(4000); 
            
            NetEvt ev{};
            ev.type = EVT_SEND_OK;
            strncpy(ev.text, out_text, sizeof(ev.text) - 1);
            netq_push(ev);

            out_pending = false;
        }
    }

    void stop() override { }
};

#endif