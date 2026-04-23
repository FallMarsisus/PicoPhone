#ifndef SMS_APP_H
#define SMS_APP_H

#include "App.h"
#include "AppManager.h"
#include "../plugins/lv_t9_keyboard.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include <time.h>
#include "../system/LTE.h"
#include "../system/NetworkErrorHandler.h"
#include "../system/UnifiedContacts.h"

#define SMS_MAX_HISTORY 20
#define SMS_COL_BG_LIST  0x1C1C1E // Noir iOS
#define SMS_COL_BG_CHAT  0x000000 
#define SMS_COL_MSG_IN   0x3A3A3C // Gris foncé
#define SMS_COL_MSG_OUT  0x34C759 // Vert SMS classique
#define SMS_COL_TEXT     0xFFFFFF
#define SMS_COL_TIME     0xAEAEB2

struct SmsContact {
    String number;
    String name;
    String last_msg_preview; 
    bool has_new;
};

class SmsApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* btn_new_conv;
    lv_obj_t* view_contacts;
    lv_obj_t* list_cont;
    lv_obj_t* view_chat;
    lv_obj_t* msg_list;
    lv_obj_t* header_title;
    lv_obj_t* keyboard_cont;
    lv_obj_t* ta_visible;

    bool fs_ok = false;
    bool creating_conversation = false;
    std::vector<SmsContact> contacts;
    int current_contact_idx = -1; 

    // --- Communication Inter-Coeurs ---
    volatile bool out_pending       = false; // Core 0 set, Core 1 consomme
    volatile bool send_scheduled    = false; // envoye a LTE, attente resultat
    char out_number[32] = {0};
    char out_text[256]  = {0};

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

    int find_contact_index(const String& number) {
        for (size_t i = 0; i < contacts.size(); i++) {
            if (contacts[i].number == number) return (int)i;
        }
        return -1;
    }

    static bool is_number_like(const String& in) {
        if (in.length() == 0) return false;
        bool has_digit = false;
        for (int i = 0; i < in.length(); ++i) {
            char c = in.charAt(i);
            if (c >= '0' && c <= '9') {
                has_digit = true;
                continue;
            }
            if (c == '+' && i == 0) continue;
            if (c == ' ' || c == '-' || c == '(' || c == ')') continue;
            return false;
        }
        return has_digit;
    }

    String resolve_target_number(const String& input) {
        if (is_number_like(input)) {
            return unified_contacts::normalize_phone(input);
        }

        String by_name;
        if (unified_contacts::phone_for_name(input, by_name)) {
            return unified_contacts::normalize_phone(by_name);
        }

        return unified_contacts::normalize_phone(input);
    }

    void save_contacts_to_flash() {
        if (!fs_ok) return;
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto &c : contacts) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = c.number;
            obj["n"] = c.name;
            obj["p"] = c.last_msg_preview;
        }
        File fw = LittleFS.open("/sms_contacts.json", "w");
        if (fw) { serializeJson(arr, fw); fw.close(); }
    }

    void open_contact_idx(int idx) {
        if (idx < 0 || idx >= (int)contacts.size()) return;
        current_contact_idx = idx;
        creating_conversation = false;
        lv_obj_add_flag(view_contacts, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view_chat, LV_OBJ_FLAG_HIDDEN);
        const String display_name = unified_contacts::display_name_for_phone(contacts[idx].number, contacts[idx].name);
        lv_label_set_text(header_title, display_name.c_str());
        lv_textarea_set_placeholder_text(ta_visible, "Message");
        lv_obj_clear_flag(keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ta_visible, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard_cont, ta_visible);
        load_history_to_ui(contacts[idx].number);
        lv_obj_scroll_to_y(msg_list, 10000, LV_ANIM_OFF);
    }

    void begin_new_conversation() {
        current_contact_idx = -1;
        creating_conversation = true;
        lv_obj_clear_flag(view_contacts, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(view_chat, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(header_title, "Nouvelle discussion");
        lv_textarea_set_text(ta_visible, "");
        lv_textarea_set_placeholder_text(ta_visible, "Numero ou nom");
        lv_obj_clear_flag(keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ta_visible, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(keyboard_cont, ta_visible);
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
                const String number = obj["id"].as<String>();
                const String fallback_name = obj["n"].as<String>();
                const String display_name = unified_contacts::display_name_for_phone(number, fallback_name.length() ? fallback_name : number);
                contacts.push_back({number, display_name, obj["p"].as<String>(), false});
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
        while (arr.size() > SMS_MAX_HISTORY) arr.remove(0);

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

    void delete_conversation(String number) {
        if (!fs_ok) return;
        String path = "/sms_" + number + ".json";
        if (LittleFS.exists(path)) LittleFS.remove(path);

        // Remove contact from in-memory list
        for (auto it = contacts.begin(); it != contacts.end(); ++it) {
            if (it->number == number) {
                contacts.erase(it);
                break;
            }
        }
        save_contacts_to_flash();
        
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
        } else if (app->creating_conversation) {
            app->creating_conversation = false;
            lv_textarea_set_text(app->ta_visible, "");
            lv_obj_add_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(app->header_title, "Messages");
        } else {
            AppManager::switchTo(APP_HOME);
        }
    }

    static void new_conversation_event(lv_event_t* e) {
        SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
        app->begin_new_conversation();
    }

    static void btn_reply_event(lv_event_t* e) {
        SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
        lv_obj_clear_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(app->keyboard_cont, app->ta_visible);
        lv_obj_scroll_to_y(app->msg_list, 10000, LV_ANIM_ON);
    }

    static void kb_send_event(lv_event_t* e) {
        SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
        
        // --- SÉCURITÉ 1 : On bloque si un envoi est DÉJÀ en cours ---
        // Évite que l'utilisateur clique 2 fois et fasse crasher le Core 1
        if (app->out_pending) {
            Serial.println("[SmsApp] Envoi deja en cours, veuillez patienter...");
            return; 
        }

        const char* text = lv_textarea_get_text(app->ta_visible);

        if (text && strlen(text) > 0 && app->creating_conversation) {
            String number = app->resolve_target_number(String(text));
            if (number.length() == 0) {
                lv_label_set_text(app->header_title, "Numero invalide");
                return;
            }
            int idx = app->find_contact_index(number);
            if (idx == -1) {
                const String display_name = unified_contacts::display_name_for_phone(number, number);
                app->contacts.push_back({number, display_name, "", false});
                idx = (int)app->contacts.size() - 1;
            }
            app->save_contacts_to_flash();
            lv_obj_add_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
            app->creating_conversation = false;
            app->open_contact_idx(idx);
        } else if (text && strlen(text) > 0 && app->current_contact_idx != -1) {
            const String number = app->contacts[app->current_contact_idx].number;
            
            // --- SÉCURITÉ 2 : Prévention des Buffer Overflows ---
            // On copie et on FORCE le caractère de fin '\0' pour ne pas déborder
            strncpy(app->out_number, number.c_str(), sizeof(app->out_number) - 1);
            app->out_number[sizeof(app->out_number) - 1] = '\0'; 
            
            strncpy(app->out_text, text, sizeof(app->out_text) - 1);
            app->out_text[sizeof(app->out_text) - 1] = '\0';
            
            app->out_pending = true; // Déclenche l'envoi sur Core 1
        }
        
        // On vide la zone de texte immédiatement pour le confort de l'utilisateur
        lv_textarea_set_text(app->ta_visible, "");
    }

    static void kb_cancel_event(lv_event_t* e) {
        SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
        lv_obj_add_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(app->ta_visible, "");
        if (app->creating_conversation) {
            app->creating_conversation = false;
            lv_label_set_text(app->header_title, "Messages");
        }
    }

    void refresh_contact_list_ui() {
        bool stop_flag = false;
        lv_obj_clean(list_cont);
        for (size_t i = 0; i < contacts.size(); i++) {
            // Row wrapper to center the button horizontally
            lv_obj_t* row = lv_obj_create(list_cont);
            lv_obj_set_size(row, lv_pct(100), 85);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_scroll_dir(row, LV_DIR_NONE);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            lv_obj_t* btn = lv_btn_create(row);
            lv_obj_set_width(btn, 300);
            lv_obj_set_height(btn, 80);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xD9D9D9), 0);
            lv_obj_set_style_border_width(btn, 0, 0);
            
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, [](lv_event_t* e){
                if (lv_event_get_target(e) != lv_event_get_current_target(e)) return; // ignore bubbled events from children
                SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
                int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
                app->open_contact_idx(idx);
            }, LV_EVENT_CLICKED, this);

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, contacts[i].name.c_str());
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 4, 0);
            lv_obj_set_style_text_color(lbl, lv_color_black(), 0);

            lv_obj_t* sub = lv_label_create(btn);
            lv_label_set_text(sub, contacts[i].last_msg_preview.c_str());
            lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
            lv_obj_set_width(sub, 190);
            lv_obj_set_style_text_color(sub, lv_color_hex(0xaaaaaa), 0);
            lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 4, 26);

            lv_obj_t* del_btn = lv_btn_create(btn);
            lv_obj_set_size(del_btn, 30, 30);
            lv_obj_align(del_btn, LV_ALIGN_RIGHT_MID, 5, 0);
            lv_obj_set_user_data(del_btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(del_btn, [](lv_event_t* e){
                SmsApp* app = (SmsApp*)lv_event_get_user_data(e);
                int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
                app->delete_conversation(app->contacts[idx].number);
                app->refresh_contact_list_ui();
            }, LV_EVENT_CLICKED, this);
            lv_obj_set_style_bg_color(del_btn, lv_color_hex(0xFF3B30), 0);

            lv_obj_t* del_lbl = lv_label_create(del_btn);
            lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
            lv_obj_set_style_text_color(del_lbl, lv_color_white(), 0); 
            lv_obj_center(del_lbl);
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
            lv_obj_set_style_bg_color(bubble, lv_color_hex(SMS_COL_MSG_OUT), 0);
            lv_obj_align(bubble, LV_ALIGN_TOP_RIGHT, 0, 0);
        } else {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(SMS_COL_MSG_IN), 0);
            lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        lv_obj_t* l_text = lv_label_create(bubble);
        lv_label_set_text(l_text, text);
        lv_label_set_long_mode(l_text, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l_text, lv_pct(100)); 
        lv_obj_set_style_text_color(l_text, lv_color_hex(SMS_COL_TEXT), 0);

        lv_obj_t* l_time = lv_label_create(bubble);
        lv_label_set_text(l_time, timeStr);
        lv_obj_set_style_text_font(l_time, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l_time, lv_color_hex(SMS_COL_TIME), 0);
        lv_obj_align(l_time, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

public:
    void start(lv_obj_t* parent) override {
        fs_ok = LittleFS.begin();
        load_contacts_from_flash();

        main_bg = parent; 
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_white(), 0);

        // Header
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0xE9E9E9), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        header_title = lv_label_create(header);
        lv_label_set_text(header_title, "Messages");
        lv_obj_set_style_text_color(header_title, lv_color_black(), 0);
        lv_obj_center(header_title);
        
        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_back_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_hex(SMS_COL_MSG_OUT), 0); // Fleche Verte
        lv_obj_center(l_back);

        btn_new_conv = lv_btn_create(header);
        lv_obj_set_size(btn_new_conv, 40, 40);
        lv_obj_align(btn_new_conv, LV_ALIGN_RIGHT_MID, 10, 0);
        lv_obj_set_style_bg_opa(btn_new_conv, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_new_conv, new_conversation_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_new = lv_label_create(btn_new_conv);
        lv_label_set_text(l_new, LV_SYMBOL_PLUS);
        lv_obj_set_style_text_color(l_new, lv_color_hex(SMS_COL_MSG_OUT), 0);
        lv_obj_center(l_new);

        // VUE CONTACTS
        view_contacts = lv_obj_create(main_bg);
        lv_obj_set_size(view_contacts, 320, 430);
        lv_obj_align(view_contacts, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_opa(view_contacts, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(view_contacts, 0, 0);
        lv_obj_set_style_pad_all(view_contacts, 0, 0);
        lv_obj_set_style_pad_left(view_contacts, 0, 0);
        lv_obj_set_style_pad_right(view_contacts, 0, 0);
        lv_obj_set_style_pad_top(view_contacts, 10, 0);
        
        list_cont = lv_obj_create(view_contacts);
        lv_obj_set_size(list_cont, lv_pct(100), lv_pct(100));
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(list_cont, 0, 0);
        lv_obj_set_style_pad_left(list_cont, 0, 0);
        lv_obj_set_style_pad_right(list_cont, 0, 0);
        // lv_obj_set_style_pad_gap(list_cont, 12, 0);
        lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_scroll_dir(list_cont, LV_DIR_VER);

        // VUE CHAT
        view_chat = lv_obj_create(main_bg);
        lv_obj_set_size(view_chat, 320, 430);
        lv_obj_align(view_chat, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_color(view_chat, lv_color_white(), 0);
        lv_obj_set_style_border_width(view_chat, 0, 0);
        lv_obj_add_flag(view_chat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view_chat, LV_OBJ_FLAG_SCROLLABLE);

        msg_list = lv_obj_create(view_chat);
        lv_obj_set_size(msg_list, 320, 420-200 - 40); 
        lv_obj_align(msg_list, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(msg_list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(msg_list, 0, 0);
        lv_obj_set_flex_flow(msg_list, LV_FLEX_FLOW_COLUMN);

        lv_obj_t* footer = lv_obj_create(view_chat);
        lv_obj_set_size(footer, 320, 210);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 10);
        lv_obj_set_style_bg_color(footer, lv_color_hex(0xDEDEDE), 0);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
        
        /*
        lv_obj_t* btn_reply = lv_btn_create(footer);
        lv_obj_set_size(btn_reply, 200, 40);
        lv_obj_center(btn_reply);
        lv_obj_add_event_cb(btn_reply, btn_reply_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_rep = lv_label_create(btn_reply);
        lv_label_set_text(l_rep, "iMessage");
        lv_obj_center(l_rep);

        */
        // CLAVIER
        ta_visible = lv_textarea_create(main_bg);
        lv_obj_set_size(ta_visible, 320, 40);
        lv_obj_align(ta_visible, LV_ALIGN_BOTTOM_MID, 0, -200);
        lv_obj_set_style_border_color(ta_visible, lv_color_hex(0xDEDEDE), 0);
        lv_obj_set_style_border_width(ta_visible, 2, 0);
        lv_obj_add_flag(ta_visible, LV_OBJ_FLAG_HIDDEN);


        keyboard_cont = lv_keyboard_create(main_bg);
        lv_obj_add_flag(keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(keyboard_cont, kb_send_event, LV_EVENT_READY, this);
        lv_obj_add_event_cb(keyboard_cont, kb_cancel_event, LV_EVENT_CANCEL, this);
        lv_obj_set_size(keyboard_cont, 320, 200);
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
                save_contacts_to_flash();
            } else {
                add_bubble_to_ui("Erreur d'envoi", true, "!!");
            }
        }
    }

void update1() override {
        // Core 1 : Envoi SMS via LTE (gestionnaire exclusif de Serial1)

        // Etape 1 : scheduler l'envoi aupres de LTE
        if (out_pending && !send_scheduled) {
            Serial.printf("[SmsApp] scheduling SMS vers '%s'\n", out_number);
            if (LTE::scheduleSendSms(out_number, out_text)) {
                send_scheduled = true;
                out_pending    = false;
            } else {
                Serial.println("[SmsApp] LTE occupe, retry au prochain tour");
            }
            return;
        }

        // Etape 2 : attendre que LTE ait termine l'envoi
        if (send_scheduled && LTE::isSendDone()) {
            bool ok = LTE::getSendResult();
            Serial.printf("[SmsApp] Resultat envoi SMS: %s\n", ok ? "OK" : "ECHEC");

            NetEvt ev{};
            ev.type = ok ? EVT_SEND_OK : EVT_SEND_FAIL;
            strncpy(ev.text, out_text, sizeof(ev.text) - 1);
            ev.text[sizeof(ev.text) - 1] = '\0';
            netq_push(ev);

            send_scheduled = false;
        }
    }

    void stop() override { }
};

#endif