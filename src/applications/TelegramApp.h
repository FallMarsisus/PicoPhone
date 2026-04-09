#ifndef TELEGRAM_APP_H
#define TELEGRAM_APP_H

#include "App.h"
#include "AppManager.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include "../plugins/lv_t9_keyboard.h"
#include <LittleFS.h>     
#include <ArduinoJson.h>  
#include <vector>
#include <time.h>
#include "../system/Secrets.h"
#include "../system/NetworkErrorHandler.h"
#include "../system/LTE.h"

// --- CONFIG ---
#define TG_CHECK_INTERVAL 3000 
#define TG_MAX_HISTORY 10      // On garde 10 messages

// --- THEME ---
#define TG_COL_BG_LIST  0x18222d
#define TG_COL_BG_CHAT  0x0e1621
#define TG_COL_MSG_IN   0x2b3745
#define TG_COL_MSG_OUT  0x497a9f
#define TG_COL_TEXT     0xFFFFFF
#define TG_COL_TIME     0x8899a6

struct ContactMetadata {
    String chat_id;
    String name;
    String last_msg_preview; 
    bool has_new;
};

class TelegramApp : public App {
private:
    // UI
    lv_obj_t* main_bg;
    lv_obj_t* loader;
    lv_obj_t* view_contacts;
    lv_obj_t* list_cont;
    lv_obj_t* view_chat;
    lv_obj_t* msg_list;
    lv_obj_t* header_title;
    lv_obj_t* keyboard_cont;
    lv_obj_t* ta_visible;

    // Backend
    WiFiClientSecure client;
    UniversalTelegramBot* bot;
    unsigned long last_check = 0;

    bool fs_ok = false;
    bool bot_ready = false;

    // Throttle UI rebuilds
    bool contacts_ui_dirty = false;
    unsigned long contacts_ui_dirty_since = 0;

    // Flash writes are deferred to keep UI responsive
    bool contacts_flash_dirty = false;
    unsigned long contacts_flash_dirty_since = 0;

    struct PendingMsgSave {
        char chat_id[32];
        char text[256];
        bool is_me;
        long ts;
    };
    static constexpr size_t MAX_PENDING_SAVES = 8;
    PendingMsgSave pending_saves[MAX_PENDING_SAVES]{};
    size_t pending_saves_head = 0;
    size_t pending_saves_tail = 0;

    // ------------------ Comms core1 -> core0 (réseau -> UI) ------------------
    enum NetEvtType : uint8_t {
        EVT_INCOMING = 1,
        EVT_SEND_OK = 2,
        EVT_SEND_FAIL = 3,
    };

    struct NetEvt {
        uint8_t type;
        char chat_id[32];
        char from_name[32];
        char text[256];
        long ts;
    };

    static constexpr uint8_t NETQ_SIZE = 10;
    volatile uint8_t netq_head = 0;
    volatile uint8_t netq_tail = 0;
    NetEvt netq[NETQ_SIZE]{};

    bool netq_push(const NetEvt& e) {
        uint8_t tail = __atomic_load_n(&netq_tail, __ATOMIC_RELAXED);
        uint8_t next = (uint8_t)((tail + 1) % NETQ_SIZE);
        uint8_t head = __atomic_load_n(&netq_head, __ATOMIC_ACQUIRE);
        if (next == head) return false; // full
        netq[tail] = e;
        __atomic_store_n(&netq_tail, next, __ATOMIC_RELEASE);
        return true;
    }

    bool netq_pop(NetEvt& e) {
        uint8_t head = __atomic_load_n(&netq_head, __ATOMIC_RELAXED);
        uint8_t tail = __atomic_load_n(&netq_tail, __ATOMIC_ACQUIRE);
        if (head == tail) return false; // empty
        e = netq[head];
        __atomic_store_n(&netq_head, (uint8_t)((head + 1) % NETQ_SIZE), __ATOMIC_RELEASE);
        return true;
    }

    // ------------------ Comms core0 -> core1 (UI -> réseau) ------------------
    volatile bool out_pending = false;
    char out_chat_id[32] = {0};
    char out_text[256] = {0};
    
    // Données RAM
    std::vector<ContactMetadata> contacts;
    int current_contact_idx = -1; 

    // --- GESTION FICHIERS ROBUSTE ---

    // 1. SAUVEGARDER CONTACTS
    void save_contacts_to_flash() {
        if (!fs_ok) return;
        // Mode "w" écrase le fichier, c'est ce qu'on veut pour l'index
        File f = LittleFS.open("/contacts.json", "w");
        if (!f) return;

        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();

        for (const auto& c : contacts) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = c.chat_id;
            obj["n"] = c.name;
            obj["p"] = c.last_msg_preview;
        }

        serializeJson(arr, f);
        f.close();
    }

    // 2. CHARGER CONTACTS
    void load_contacts_from_flash() {
        if (!fs_ok) return;
        if (!LittleFS.exists("/contacts.json")) return;

        File f = LittleFS.open("/contacts.json", "r");
        if (!f) return;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, f);
        f.close();

        if (!error) {
            contacts.clear();
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject obj : arr) {
                ContactMetadata c;
                c.chat_id = obj["id"].as<String>();
                c.name = obj["n"].as<String>();
                c.last_msg_preview = obj["p"].as<String>();
                c.has_new = false; 
                contacts.push_back(c);
            }
        }
    }

    // 3. SAUVEGARDER MESSAGE (CORRECTION DU BUG D'EFFACEMENT)
    void save_msg_to_flash(String chat_id, String text, bool is_me, long timestamp) {
        if (!fs_ok) return;
        String path = "/" + chat_id + ".json";
        JsonDocument doc;
        
        // A. Lire l'existant
        if (LittleFS.exists(path)) {
            File f = LittleFS.open(path, "r");
            if (f) {
                DeserializationError err = deserializeJson(doc, f);
                f.close();
                if (err) doc.clear(); // Si fichier corrompu, on repart à zéro
            }
        }

        // B. Récupérer le tableau OU le créer s'il n'existe pas
        JsonArray arr;
        if (doc.is<JsonArray>()) {
            arr = doc.as<JsonArray>(); // CORRECTION ICI : "as" ne détruit pas
        } else {
            arr = doc.to<JsonArray>(); // "to" détruit (crée un nouveau)
        }

        // C. Ajouter le message
        JsonObject obj = arr.add<JsonObject>();
        obj["t"] = text;
        obj["m"] = is_me; 
        obj["ts"] = timestamp;

        // D. Limiter l'historique
        while (arr.size() > TG_MAX_HISTORY) {
            arr.remove(0); // Supprime le plus vieux
        }

        // E. Ecrire
        File f = LittleFS.open(path, "w"); // "w" écrase le fichier avec le nouveau JSON complet
        if (f) {
            serializeJson(arr, f);
            f.close();
        } else {
            Serial.println("Erreur ecriture fichier chat");
        }
    }

    // 4. CHARGER HISTORIQUE
    void load_history_to_ui(String chat_id) {
        lv_obj_clean(msg_list); 

        if (!fs_ok) {
            Serial.println("LittleFS indisponible: pas d'historique");
            return;
        }

        String path = "/" + chat_id + ".json";
        if (!LittleFS.exists(path)) {
            Serial.println("Pas d'historique pour ce chat");
            return;
        }

        File f = LittleFS.open(path, "r");
        if (!f) return;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, f);
        f.close();

        if (!error) {
            JsonArray arr = doc.as<JsonArray>();
            for (JsonObject obj : arr) {
                const char* text = obj["t"];
                bool is_me = obj["m"];
                long ts = obj["ts"];
                
                // Formatage Heure
                struct tm* timeinfo;
                time_t t = ts;
                timeinfo = localtime(&t);
                char timeBuf[6];
                if(t > 10000) strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
                else strcpy(timeBuf, "??:??");

                add_bubble_to_ui(text, is_me, timeBuf);
            }
        }
    }

    // --- NAVIGATION ---

    static void go_back_event(lv_event_t* e) {
        TelegramApp* app = (TelegramApp*)lv_event_get_user_data(e);
        if (app->current_contact_idx != -1) {
            app->close_conversation();
        } else {
            AppManager::switchTo(APP_HOME);
        }
    }

    // --- LOGIQUE CLAVIER ---

    static void btn_reply_event(lv_event_t* e) {
        TelegramApp* app = (TelegramApp*)lv_event_get_user_data(e);
        lv_obj_clear_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(app->keyboard_cont, app->ta_visible);
        lv_obj_scroll_to_y(app->msg_list, 10000, LV_ANIM_ON);
    }

    static void kb_send_event(lv_event_t* e) {
        TelegramApp* app = (TelegramApp*)lv_event_get_user_data(e);
        const char* text = lv_textarea_get_text(app->ta_visible);
        
        lv_obj_add_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);

        // Ne jamais faire de réseau ici (callback UI) => on queue l'envoi.
        if (text && strlen(text) > 0 && app->current_contact_idx != -1) {
            // Capture la cible au moment du click (core0)
            const String chat_id = app->contacts[app->current_contact_idx].chat_id;
            memset(app->out_chat_id, 0, sizeof(app->out_chat_id));
            strncpy(app->out_chat_id, chat_id.c_str(), sizeof(app->out_chat_id) - 1);

            memset(app->out_text, 0, sizeof(app->out_text));
            strncpy(app->out_text, text, sizeof(app->out_text) - 1);
            __atomic_store_n(&app->out_pending, true, __ATOMIC_RELEASE);
        }
        lv_textarea_set_text(app->ta_visible, "");
        
    }

    static void kb_cancel_event(lv_event_t* e) {
        TelegramApp* app = (TelegramApp*)lv_event_get_user_data(e);
        lv_textarea_set_text(app->ta_visible, "");
        lv_obj_add_flag(app->keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(app->ta_visible, LV_OBJ_FLAG_HIDDEN);
    }

    void mark_contacts_dirty() {
        contacts_flash_dirty = true;
        contacts_flash_dirty_since = millis();
        contacts_ui_dirty = true;
        contacts_ui_dirty_since = millis();
    }

    void enqueue_msg_save(const String& chat_id, const String& text, bool is_me, long ts) {
        if (!fs_ok) return;
        // Ring buffer drop-oldest if full
        size_t next_tail = (pending_saves_tail + 1) % MAX_PENDING_SAVES;
        if (next_tail == pending_saves_head) {
            pending_saves_head = (pending_saves_head + 1) % MAX_PENDING_SAVES;
        }

        PendingMsgSave& s = pending_saves[pending_saves_tail];
        memset(&s, 0, sizeof(s));
        strncpy(s.chat_id, chat_id.c_str(), sizeof(s.chat_id) - 1);
        strncpy(s.text, text.c_str(), sizeof(s.text) - 1);
        s.is_me = is_me;
        s.ts = ts;
        pending_saves_tail = next_tail;
    }

    void flush_one_flash_op() {
        // Une seule opération par tick pour éviter les stalls
        if (!fs_ok) return;

        // 1) Messages
        if (pending_saves_head != pending_saves_tail) {
            PendingMsgSave& s = pending_saves[pending_saves_head];
            save_msg_to_flash(String(s.chat_id), String(s.text), s.is_me, s.ts);
            pending_saves_head = (pending_saves_head + 1) % MAX_PENDING_SAVES;
            return;
        }

        // 2) Contacts (debounce)
        if (contacts_flash_dirty && (millis() - contacts_flash_dirty_since) > 800) {
            save_contacts_to_flash();
            contacts_flash_dirty = false;
        }
    }

    // --- RECEPTION ---

    void processIncomingMessages() {
        // IMPORTANT: une seule requête réseau par tick, pas de boucle infinie.
        int numNew = bot->getUpdates(bot->last_message_received + 1);
        if (numNew <= 0) return;

        // Traitement CPU: yield entre messages, et aucune écriture flash directe.
        for (int i = 0; i < numNew; i++) {
                String chat_id = String(bot->messages[i].chat_id);
                String text = bot->messages[i].text;
                String name = bot->messages[i].from_name;
                long ts = bot->messages[i].date.toInt(); 
                if(ts == 0) { time_t now; time(&now); ts = now; }

                if (name == "") name = "Inconnu";

                // 1. Chercher dans l'index
                int idx = -1;
                for (size_t k = 0; k < contacts.size(); k++) {
                    if (contacts[k].chat_id == chat_id) { idx = k; break; }
                }

                bool list_needs_save = false;

                // 2. Nouveau Contact ?
                if (idx == -1) {
                    contacts.push_back({chat_id, name, text, true});
                    idx = contacts.size() - 1;
                    list_needs_save = true;
                } else {
                    contacts[idx].last_msg_preview = text;
                    list_needs_save = true;
                }

                // 3. Sauvegardes (différées)
                enqueue_msg_save(chat_id, text, false, ts);
                if(list_needs_save) mark_contacts_dirty();

                // 4. Update UI
                if (current_contact_idx == idx) {
                    char timeBuf[6];
                    struct tm* timeinfo;
                    time_t t = ts;
                    timeinfo = localtime(&t);
                    strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
                    add_bubble_to_ui(text.c_str(), false, timeBuf);
                } else {
                    contacts[idx].has_new = true;
                    mark_contacts_dirty();
                }
        }
    }

    // --- UI HELPERS ---

    void refresh_contact_list_ui() {
        lv_obj_clean(list_cont);

        for (size_t i = 0; i < contacts.size(); i++) {
            lv_obj_t* btn = lv_btn_create(list_cont);
            lv_obj_set_width(btn, lv_pct(100));
            lv_obj_set_height(btn, 65);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x232e3c), 0);
            
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, [](lv_event_t* e){
                lv_obj_t* b = lv_event_get_target(e);
                TelegramApp* app = (TelegramApp*)lv_event_get_user_data(e);
                int idx = (int)(intptr_t)lv_obj_get_user_data(b);
                app->open_conversation(idx);
            }, LV_EVENT_CLICKED, this);

            // Nom
            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, contacts[i].name.c_str());
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 5, 5);

            // Preview
            lv_obj_t* sub = lv_label_create(btn);
            lv_label_set_text(sub, contacts[i].last_msg_preview.c_str());
            lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
            lv_obj_set_width(sub, 190);
            lv_obj_set_style_text_color(sub, lv_color_hex(0xaaaaaa), 0);
            lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 5, -5);

            // Badge
            if (contacts[i].has_new) {
                lv_obj_t* badge = lv_obj_create(btn);
                lv_obj_set_size(badge, 12, 12);
                lv_obj_set_style_radius(badge, 6, 0);
                lv_obj_set_style_bg_color(badge, lv_color_hex(0xFF3B30), 0);
                lv_obj_align(badge, LV_ALIGN_RIGHT_MID, -10, 0);
            }
        }
    }

    void open_conversation(int idx) {
        current_contact_idx = idx;
        contacts[idx].has_new = false;

        lv_obj_add_flag(view_contacts, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view_chat, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(header_title, contacts[idx].name.c_str());

        load_history_to_ui(contacts[idx].chat_id);
        lv_obj_scroll_to_y(msg_list, 10000, LV_ANIM_OFF);
    }

    void close_conversation() {
        current_contact_idx = -1;
        lv_obj_clean(msg_list);

        lv_obj_add_flag(keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ta_visible, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(view_chat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view_contacts, LV_OBJ_FLAG_HIDDEN);
        
        refresh_contact_list_ui();
        lv_label_set_text(header_title, "Telegram");
    }

    void add_bubble_to_ui(const char* text, bool is_me, const char* timeStr) {
        lv_obj_t* bubble = lv_obj_create(msg_list);
        lv_obj_set_width(bubble, lv_pct(85));
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_radius(bubble, 12, 0);
        lv_obj_set_style_pad_all(bubble, 8, 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        
        if (is_me) {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(TG_COL_MSG_OUT), 0);
            lv_obj_align(bubble, LV_ALIGN_TOP_RIGHT, 0, 0);
        } else {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(TG_COL_MSG_IN), 0);
            lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        // Texte
        lv_obj_t* l_text = lv_label_create(bubble);
        lv_label_set_text(l_text, text);
        lv_label_set_long_mode(l_text, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l_text, lv_pct(100)); 
        lv_obj_set_style_text_color(l_text, lv_color_hex(TG_COL_TEXT), 0);
        lv_obj_set_style_text_font(l_text, &lv_font_montserrat_14, 0);

        // Heure
        lv_obj_t* l_time = lv_label_create(bubble);
        lv_label_set_text(l_time, timeStr);
        lv_obj_set_style_text_font(l_time, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l_time, lv_color_hex(TG_COL_TIME), 0);
        lv_obj_align(l_time, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

public:
    TelegramApp() {
        client.setInsecure();
        bot = new UniversalTelegramBot(TG_BOT_TOKEN, client);
    }

    void start(lv_obj_t* parent) override {
        // --- SETUP LITTLEFS (SANS FORMAT AUTO = pas de freeze long) ---
        fs_ok = LittleFS.begin();
        if (!fs_ok) {
            Serial.println("LittleFS Mount Failed (format desactive pour eviter freeze)");
        }

        // Réseau: borne les freezes (TLS/HTTP sync) à une durée maximale raisonnable.
        client.setTimeout(1200);
        if (bot) {
            bot->longPoll = 0; // évite le long-poll bloquant
        }
        bot_ready = (bot != nullptr);
        
        // CHARGER LES DONNEES DE LA FLASH
        load_contacts_from_flash();

        // Réserve pour limiter les reallocs (stutters)
        if (contacts.capacity() < 20) contacts.reserve(20);

        main_bg = parent; 
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(TG_COL_BG_LIST), 0);

        // Header
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x232e3c), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        header_title = lv_label_create(header);
        lv_label_set_text(header_title, "Telegram");
        lv_obj_set_style_text_color(header_title, lv_color_white(), 0);
        lv_obj_center(header_title);
        

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_back_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_white(), 0);
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
        lv_obj_set_style_pad_gap(list_cont, 10, 0);

        // VUE CHAT
        view_chat = lv_obj_create(main_bg);
        lv_obj_set_size(view_chat, 320, 430);
        lv_obj_align(view_chat, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_color(view_chat, lv_color_hex(TG_COL_BG_CHAT), 0);
        lv_obj_set_style_border_width(view_chat, 0, 0);
        lv_obj_add_flag(view_chat, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(view_chat, LV_OBJ_FLAG_SCROLLABLE);

        msg_list = lv_obj_create(view_chat);
        lv_obj_set_size(msg_list, 320, 310); 
        lv_obj_align(msg_list, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(msg_list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(msg_list, 0, 0);
        lv_obj_set_flex_flow(msg_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(msg_list, 10, 0);
        lv_obj_set_style_pad_gap(msg_list, 10, 0);

        lv_obj_t* footer = lv_obj_create(view_chat);
        lv_obj_set_size(footer, 320, 80);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 10);
        lv_obj_set_style_bg_color(footer, lv_color_hex(0x232e3c), 0);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t* btn_reply = lv_btn_create(footer);
        lv_obj_set_size(btn_reply, 200, 40);
        lv_obj_center(btn_reply);
        lv_obj_add_event_cb(btn_reply, btn_reply_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_rep = lv_label_create(btn_reply);
        lv_label_set_text(l_rep, "Message");
        lv_obj_center(l_rep);

        // CLAVIER
        ta_visible = lv_textarea_create(main_bg);
        lv_obj_set_size(ta_visible, 220, 40);
        lv_obj_align(ta_visible, LV_ALIGN_BOTTOM_MID, 0, -240);
        lv_obj_set_style_border_color(ta_visible, lv_color_hex(0x007AFF), 0);
        lv_obj_set_style_border_width(ta_visible, 2, 0);
        lv_obj_add_flag(ta_visible, LV_OBJ_FLAG_HIDDEN);

        keyboard_cont = lv_keyboard_create(main_bg);
        lv_obj_add_flag(keyboard_cont, LV_OBJ_FLAG_HIDDEN);
        
        lv_obj_add_event_cb(keyboard_cont, kb_send_event, LV_EVENT_READY, this);
        lv_obj_add_event_cb(keyboard_cont, kb_cancel_event, LV_EVENT_CANCEL, this);

        loader = lv_spinner_create(main_bg, 1000, 60);
        lv_obj_set_size(loader, 40, 40);
        lv_obj_center(loader);
        lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);

        refresh_contact_list_ui();

        // Schedule first poll
        last_check = millis();
    }
    
    void update() override {
        // 1) Flush flash en douceur (et surtout pas pendant la saisie)
        if (fs_ok) {
            static unsigned long last_flush_ms = 0;
            const unsigned long now = millis();
            const bool keyboard_closed = keyboard_cont && lv_obj_has_flag(keyboard_cont, LV_OBJ_FLAG_HIDDEN);
            const bool ta_closed = ta_visible && lv_obj_has_flag(ta_visible, LV_OBJ_FLAG_HIDDEN);
            if (keyboard_closed && ta_closed && (now - last_flush_ms) > 1800) {
                flush_one_flash_op();
                last_flush_ms = now;
            }
        }

        // 1b) Consommer les events réseau (core1) avec un budget temps
        const uint32_t start_ms = millis();
        NetEvt ev{};
        while (netq_pop(ev)) {
            // Budget: ~12ms pour garder l'UI fluide
            if ((millis() - start_ms) > 12) break;

            const String chat_id = String(ev.chat_id);
            const String text = String(ev.text);
            const String name = String(ev.from_name);
            const long ts = ev.ts;

            if (ev.type == EVT_INCOMING) {
                // Upsert contact
                int idx = -1;
                for (size_t k = 0; k < contacts.size(); k++) {
                    if (contacts[k].chat_id == chat_id) { idx = (int)k; break; }
                }
                if (idx == -1) {
                    contacts.push_back({chat_id, name.length() ? name : String("Inconnu"), text, true});
                    idx = (int)contacts.size() - 1;
                } else {
                    contacts[idx].last_msg_preview = text;
                    if (current_contact_idx != idx) contacts[idx].has_new = true;
                }

                enqueue_msg_save(chat_id, text, false, ts);
                mark_contacts_dirty();

                if (current_contact_idx == idx) {
                    char timeBuf[6];
                    struct tm* timeinfo;
                    time_t t = (time_t)ts;
                    timeinfo = localtime(&t);
                    if(t > 10000) strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
                    else strcpy(timeBuf, "??:??");
                    add_bubble_to_ui(text.c_str(), false, timeBuf);
                }
            } else if (ev.type == EVT_SEND_OK) {
                // Message envoyé: on l'affiche + persiste
                time_t tnow; time(&tnow);
                enqueue_msg_save(chat_id, text, true, (long)tnow);
                // Met à jour le preview (si contact connu)
                for (size_t k = 0; k < contacts.size(); k++) {
                    if (contacts[k].chat_id == chat_id) {
                        contacts[k].last_msg_preview = "Moi: " + text;
                        break;
                    }
                }
                mark_contacts_dirty();

                if (current_contact_idx != -1 && contacts[current_contact_idx].chat_id == chat_id) {
                    char timeBuf[6];
                    struct tm* timeinfo = localtime(&tnow);
                    strftime(timeBuf, sizeof(timeBuf), "%H:%M", timeinfo);
                    add_bubble_to_ui(text.c_str(), true, timeBuf);
                }
            } else if (ev.type == EVT_SEND_FAIL) {
                Serial.println("Echec envoi Telegram");
            }
        }

        // 2) UI refresh throttle (évite rebuild en boucle)
        if (contacts_ui_dirty && (millis() - contacts_ui_dirty_since) > 250) {
            // Rafraîchit la liste seulement si on est sur la vue contacts
            if (current_contact_idx == -1) {
                refresh_contact_list_ui();
            }
            contacts_ui_dirty = false;
        }

        // Note: le réseau (Telegram) tourne sur update1 (core1) pour éviter les freezes UI.
    }

    void update1() override {
        // Core1: uniquement réseau + push events (NE PAS toucher LVGL ici)
        if (!bot_ready) return;
        if (!LTE::isReadyForData()) return;

        const unsigned long now = millis();

        // 1) Envoi sortant
        if (__atomic_exchange_n(&out_pending, false, __ATOMIC_ACQ_REL)) {
            // Copie locale
            char chat_id[32];
            char text[256];
            memcpy(chat_id, out_chat_id, sizeof(chat_id));
            memcpy(text, out_text, sizeof(text));

            const bool ok = bot->sendMessage(String(chat_id), text, "");
            NetEvt ev{};
            ev.type = ok ? EVT_SEND_OK : EVT_SEND_FAIL;
            strncpy(ev.chat_id, chat_id, sizeof(ev.chat_id) - 1);
            strncpy(ev.text, text, sizeof(ev.text) - 1);
            ev.ts = 0;
            netq_push(ev);

            // Décale le polling après un envoi
            last_check = now;
            return;
        }

        // 2) Poll entrant
        if ((now - last_check) < TG_CHECK_INTERVAL) return;
        last_check = now;

        // Poll entrant desactive ici: gere par TelegramNotifyService (global background).
    }

    void stop() override {
        // Flush best-effort (sans boucle)
        flush_one_flash_op();
        if (loader) lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);
    }
};

#endif