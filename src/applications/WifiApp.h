#ifndef WIFI_APP_H
#define WIFI_APP_H

#include "App.h"
#include "AppManager.h"
#include "WifiStore.h"
#include "../system/Settings.h"
#include <WiFi.h>
#include <cstring>

// --- COULEURS (THÈME SOMBRE) ---
#define COL_BG       0x000000 // Noir (Fond App)
#define COL_OVERLAY  0x1C1C1E // Gris très fonce (Fond Popup / Header)
#define COL_CARD     0x2C2C2E // Gris fonce (Cartes / boutons)
#define COL_TEXT     0xFFFFFF // Blanc (Texte principal)
#define COL_TEXT_INV 0x000000 // Noir (Texte sur fond clair - si utilise)
#define COL_PRIM     0x0A84FF // Bleu iOS (Accent)
#define COL_ERR      0xFF453A // Rouge iOS (Erreur)
#define COL_SUCCESS  0x34C759 // Vert iOS

class WifiApp : public App {
private:
    lv_obj_t* list;
    lv_obj_t* lv_kb;
    lv_obj_t* ta_pass;
    lv_obj_t* overlay;
    lv_obj_t* sw_wifi = nullptr;
    lv_obj_t* status_lbl = nullptr;
    lv_obj_t* empty_msg_label = nullptr;
    lv_obj_t* search_msg_label = nullptr;
    lv_obj_t* error_msg_label = nullptr;
    lv_obj_t* qr_overlay = nullptr;
    char target_ssid[32];

    // Sauvegarde differee
    bool pending_save = false;
    uint32_t pending_since = 0;
    char pending_ssid[33] = {0};
    char pending_pass[65] = {0};

    // Scan Asynchrone & Timers
    bool is_scanning = false;
    uint32_t scan_start_time = 0;
    uint32_t last_scan_poll = 0;
    
    // --- OPTIMISATION LAG ---
    // On ne verifie le statut WiFi que toutes les 1000ms
    // pour ne pas ralentir le tactile.
    uint32_t last_status_check = 0;
    int cached_status = WL_DISCONNECTED; // On garde le statut en memoire
    bool suppress_switch_event = false;
    bool last_wifi_enabled = true;

    static void disable_scroll_everywhere(lv_obj_t* obj) {
        if (!obj) return;
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    }

    static void go_home(lv_event_t* e) { 
        WiFi.scanDelete(); 
        AppManager::switchTo(APP_HOME); 
    }
    
    static void go_calib(lv_event_t* e) { 
        WiFi.scanDelete();
        AppManager::switchTo(APP_TOUCH_CALIB); 
    }

    static void scan_event(lv_event_t* e) {
        WifiApp* app = (WifiApp*)lv_event_get_user_data(e);
        app->startScan();
    }

    static void sw_wifi_event(lv_event_t* e) {
        WifiApp* app = (WifiApp*)lv_event_get_user_data(e);
        if (!app || app->suppress_switch_event || !app->sw_wifi) return;

        bool enabled = lv_obj_has_state(app->sw_wifi, LV_STATE_CHECKED);
        app->set_wifi_enabled(enabled, true);
    }

    static void open_share_qr_event(lv_event_t* e) {
        WifiApp* app = (WifiApp*)lv_event_get_user_data(e);
        if (app) app->open_wifi_share_qr();
    }

    static void close_qr_overlay_event(lv_event_t* e) {
        WifiApp* app = (WifiApp*)lv_event_get_user_data(e);
        if (app) app->close_wifi_share_qr();
    }

    static void kb_event(lv_event_t* e) {
        WifiApp* app = (WifiApp*)lv_event_get_user_data(e);
        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_READY) { 
            const char* pass = lv_textarea_get_text(app->ta_pass);
            
            if (app->is_scanning) {
                WiFi.scanDelete();
                app->is_scanning = false;
            }

            WiFi.begin(app->target_ssid, pass);

            memset(app->pending_ssid, 0, sizeof(app->pending_ssid));
            memset(app->pending_pass, 0, sizeof(app->pending_pass));
            strncpy(app->pending_ssid, app->target_ssid, sizeof(app->pending_ssid) - 1);
            strncpy(app->pending_pass, pass ? pass : "", sizeof(app->pending_pass) - 1);
            app->pending_save = true;
            app->pending_since = millis();

            app->close_password_prompt();
            app->show_connecting_state(app->target_ssid);
            
            // On force une mise à jour immediate du statut
            app->last_status_check = 0; 
        } 
        else if (code == LV_EVENT_CANCEL) {
            app->close_password_prompt();
        }
    }

    static void ta_click_event(lv_event_t* e) {
        WifiApp* app = (WifiApp*)lv_event_get_user_data(e);
        lv_keyboard_set_textarea(app->lv_kb, app->ta_pass);
    }

    static void list_btn_event(lv_event_t* e) {
        WifiApp* app = (WifiApp*)lv_event_get_user_data(e);
        const char* ssid = lv_list_get_btn_text(app->list, lv_event_get_target(e));
        strcpy(app->target_ssid, ssid);
        app->open_password_prompt(ssid);
    }

    void open_password_prompt(const char* ssid) {
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(ta_pass, "");
        lv_textarea_set_placeholder_text(ta_pass, ssid);
        lv_keyboard_set_textarea(lv_kb, ta_pass);
        disable_scroll_everywhere(overlay);
        disable_scroll_everywhere(ta_pass);
        disable_scroll_everywhere(lv_kb);
    }

    void close_password_prompt() {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(lv_kb, NULL);
        lv_obj_t* sug = lv_obj_get_child(lv_kb, 0);
        if(sug) lv_obj_add_flag(sug, LV_OBJ_FLAG_HIDDEN);
    }

    String escape_qr_field(const String& in) {
        String out;
        out.reserve(in.length() + 8);
        for (size_t i = 0; i < in.length(); i++) {
            char c = in[i];
            if (c == '\\' || c == ';' || c == ',' || c == ':') out += '\\';
            out += c;
        }
        return out;
    }

    void close_wifi_share_qr() {
        if (!qr_overlay) return;
        lv_obj_del(qr_overlay);
        qr_overlay = nullptr;
    }

    void open_wifi_share_qr() {
        close_wifi_share_qr();

        String ssid;
        String pass;
        String auth = "WPA";

        if (WiFi.status() == WL_CONNECTED) {
            ssid = WiFi.SSID();
            int idx = wifi_store::find_ssid(ssid.c_str());
            if (idx >= 0) {
                const auto* entry = wifi_store::get((size_t)idx);
                if (entry) pass = entry->pass;
            }
        } else if (wifi_store::count() > 0) {
            const auto* entry = wifi_store::get(0);
            if (entry) {
                ssid = entry->ssid;
                pass = entry->pass;
            }
        }

        if (ssid.length() == 0) {
            return;
        }

        if (pass.length() == 0) auth = "nopass";

        String payload = "WIFI:T:" + auth + ";S:" + escape_qr_field(ssid) + ";P:" + escape_qr_field(pass) + ";;";

        qr_overlay = lv_obj_create(lv_scr_act());
        lv_obj_set_size(qr_overlay, 320, 480);
        lv_obj_align(qr_overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(qr_overlay, lv_color_hex(0x0B1220), 0);
        lv_obj_set_style_bg_opa(qr_overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(qr_overlay, 0, 0);
        lv_obj_set_style_radius(qr_overlay, 0, 0);

        lv_obj_t* title = lv_label_create(qr_overlay);
        lv_label_set_text(title, "Partager WiFi");
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

        lv_obj_t* subtitle = lv_label_create(qr_overlay);
        String st = "Reseau: " + ssid;
        lv_label_set_text(subtitle, st.c_str());
        lv_obj_set_style_text_color(subtitle, lv_color_hex(0x9FB3D9), 0);
        lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 38);

#if LV_USE_QRCODE
        lv_obj_t* qr = lv_qrcode_create(qr_overlay, 220, lv_color_black(), lv_color_white());
        lv_qrcode_update(qr, payload.c_str(), payload.length());
        lv_obj_center(qr);
        lv_obj_set_style_border_color(qr, lv_color_white(), 0);
        lv_obj_set_style_border_width(qr, 6, 0);
#else
        lv_obj_t* no_qr = lv_label_create(qr_overlay);
        lv_label_set_text(no_qr, "LV_USE_QRCODE non active");
        lv_obj_set_style_text_color(no_qr, lv_color_hex(0xFF6B6B), 0);
        lv_obj_align(no_qr, LV_ALIGN_CENTER, 0, 0);
#endif

        lv_obj_t* close = lv_btn_create(qr_overlay);
        lv_obj_set_size(close, 220, 42);
        lv_obj_align(close, LV_ALIGN_BOTTOM_MID, 0, -16);
        lv_obj_set_style_bg_color(close, lv_color_hex(0x1E3A66), 0);
        lv_obj_add_event_cb(close, close_qr_overlay_event, LV_EVENT_CLICKED, this);
        lv_obj_t* close_lbl = lv_label_create(close);
        lv_label_set_text(close_lbl, "Fermer");
        lv_obj_center(close_lbl);
    }

    void show_connecting_state(const char* ssid) {
        lv_obj_clean(list);
        String s = "Connexion a:\n"; s += ssid; s += "\n...";
        lv_list_add_text(list, s.c_str());
    }

    void sync_switch_state(bool enabled) {
        if (!sw_wifi) return;
        suppress_switch_event = true;
        if (enabled) lv_obj_add_state(sw_wifi, LV_STATE_CHECKED);
        else lv_obj_clear_state(sw_wifi, LV_STATE_CHECKED);
        suppress_switch_event = false;
    }

    void set_wifi_enabled(bool enabled, bool refresh_scan) {
        if (settings::isWifiEnabled() != enabled) {
            settings::setWifiEnabled(enabled);
        }

        if (!enabled) {
            if (is_scanning) {
                WiFi.scanDelete();
                is_scanning = false;
            }
            pending_save = false;
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);

            if (list) {
                lv_obj_clean(list);
                lv_obj_add_flag(list, LV_OBJ_FLAG_HIDDEN);
            }
            if (empty_msg_label) {
                lv_label_set_text(empty_msg_label, "WiFi desactive\nActivez le WiFi pour voir les reseaux disponibles");
                lv_obj_clear_flag(empty_msg_label, LV_OBJ_FLAG_HIDDEN);
            }
            if (search_msg_label) lv_obj_add_flag(search_msg_label, LV_OBJ_FLAG_HIDDEN);
            if (error_msg_label) lv_obj_add_flag(error_msg_label, LV_OBJ_FLAG_HIDDEN);
            if (status_lbl) {
                lv_label_set_text(status_lbl, "WiFi desactive");
                lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x8E8E93), 0);
            }
        } else {
            WiFi.mode(WIFI_STA);
            if (empty_msg_label) lv_obj_add_flag(empty_msg_label, LV_OBJ_FLAG_HIDDEN);
            if (error_msg_label) lv_obj_add_flag(error_msg_label, LV_OBJ_FLAG_HIDDEN);
            if (list) lv_obj_clear_flag(list, LV_OBJ_FLAG_HIDDEN);
            if (refresh_scan) startScan();
        }

        last_wifi_enabled = enabled;
        sync_switch_state(enabled);
    }

    void startScan() {
        if (!settings::isWifiEnabled()) return;
        if (is_scanning) return;

        WiFi.scanDelete();
        if (WiFi.getMode() != WIFI_STA) {
            WiFi.mode(WIFI_STA);
            sleep_ms(20); 
        }

        lv_obj_clean(list);
        lv_obj_set_style_text_color(list, lv_color_hex(COL_TEXT), 0);
        if (search_msg_label) {
            lv_label_set_text(search_msg_label, "Recherche des reseaux WiFi en cours...");
            lv_obj_clear_flag(search_msg_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (error_msg_label) lv_obj_add_flag(error_msg_label, LV_OBJ_FLAG_HIDDEN);

        WiFi.scanNetworks(true); 
        is_scanning = true;
        scan_start_time = millis();
        last_scan_poll = millis();
    }

    void buildListFromScan(int n) {
        lv_obj_clean(list);
        lv_obj_set_style_pad_all(list, 8, 0);
        lv_obj_set_style_pad_gap(list, 6, 0);

        // Section Titre
        auto add_section_title = [&](const char* title) {
            lv_obj_t* l = lv_label_create(list);
            lv_label_set_text(l, title);
            lv_obj_set_style_text_color(l, lv_color_hex(0x8E8E93), 0);
            lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
            lv_obj_set_style_pad_left(l, 10, 0);
            lv_obj_set_style_pad_top(l, 10, 0);
            lv_obj_set_style_pad_bottom(l, 5, 0);
        };

        bool has_known = false;
        if (wifi_store::count() > 0) {
            add_section_title("Reseaux connus");
            for (size_t i = 0; i < wifi_store::count(); i++) {
                auto* e = wifi_store::get(i);
                if (!e || !e->ssid[0]) continue;
                has_known = true;
                lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_SAVE, e->ssid);
                lv_obj_add_event_cb(btn, list_btn_event, LV_EVENT_CLICKED, this);
                lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x232326), 0);
                lv_obj_set_style_text_color(btn, lv_color_hex(COL_TEXT), 0);
                lv_obj_set_style_radius(btn, 8, 0);
                lv_obj_set_style_border_width(btn, 0, 0);
                lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0x38383A), 0);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_set_style_pad_left(btn, 8, 0);
                lv_obj_set_style_pad_right(btn, 8, 0);
                lv_obj_set_style_pad_top(btn, 8, 0);
                lv_obj_set_style_pad_bottom(btn, 8, 0);
                lv_obj_t* icon = lv_label_create(btn);
                lv_label_set_text(icon, LV_SYMBOL_WIFI);
                lv_obj_set_style_text_color(icon, lv_color_hex(0x34C759), 0);
                lv_obj_align(icon, LV_ALIGN_RIGHT_MID, 0, 0);
            }
        }

        bool has_found = false;
        if (n > 0) {
            add_section_title("Reseaux detectes");
            for (int i = 0; i < n; ++i) {
                if(strlen(WiFi.SSID(i)) == 0) continue;
                has_found = true;
                lv_obj_t* btn = lv_list_add_btn(list, NULL, WiFi.SSID(i));
                lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x1C1C1E), 0);
                lv_obj_set_style_text_color(btn, lv_color_hex(COL_TEXT), 0);
                lv_obj_set_style_radius(btn, 8, 0);
                lv_obj_set_style_border_width(btn, 0, 0);
                lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0x38383A), 0);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_set_style_pad_left(btn, 8, 0);
                lv_obj_set_style_pad_right(btn, 8, 0);
                lv_obj_set_style_pad_top(btn, 8, 0);
                lv_obj_set_style_pad_bottom(btn, 8, 0);
                lv_obj_t* icon = lv_label_create(btn);
                lv_label_set_text(icon, LV_SYMBOL_WIFI);
                lv_obj_set_style_text_color(icon, lv_color_hex(0x0A84FF), 0);
                lv_obj_align(icon, LV_ALIGN_RIGHT_MID, 0, 0);
                lv_obj_add_event_cb(btn, list_btn_event, LV_EVENT_CLICKED, this);
            }
        }

        if (!has_known && !has_found) {
            lv_obj_t* l = lv_label_create(list);
            lv_label_set_text(l, "Aucun reseau trouve");
            lv_obj_set_style_text_color(l, lv_color_hex(0x8E8E93), 0);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
        }

        if (search_msg_label) lv_obj_add_flag(search_msg_label, LV_OBJ_FLAG_HIDDEN);
        if (error_msg_label) lv_obj_add_flag(error_msg_label, LV_OBJ_FLAG_HIDDEN);
    }

public:
    void start(lv_obj_t* parent) override {
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        disable_scroll_everywhere(parent);

        // --- HEADER ---
        lv_obj_t* header = lv_obj_create(parent);
        lv_obj_set_size(header, 320, 56);
        lv_obj_set_style_bg_color(header, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        disable_scroll_everywhere(header);

        lv_obj_t* btn1 = lv_btn_create(header);
        lv_obj_set_size(btn1, 44, 44);
        lv_obj_align(btn1, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn1, LV_OPA_0, 0);
        lv_obj_set_style_shadow_opa(btn1, 0, 0);
        lv_obj_add_event_cb(btn1, go_home, LV_EVENT_CLICKED, NULL);
        lv_obj_t* l1 = lv_label_create(btn1);
        lv_label_set_text(l1, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l1, lv_color_hex(COL_PRIM), 0);
        lv_obj_center(l1);

        lv_obj_t* t = lv_label_create(header);
        lv_label_set_text(t, "Wi-Fi");
        lv_obj_set_style_text_font(t, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* btn_qr = lv_btn_create(header);
        lv_obj_set_size(btn_qr, 52, 34);
        lv_obj_align(btn_qr, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_set_style_bg_color(btn_qr, lv_color_hex(0x0A84FF), 0);
        lv_obj_set_style_border_width(btn_qr, 0, 0);
        lv_obj_set_style_radius(btn_qr, 8, 0);
        lv_obj_add_event_cb(btn_qr, open_share_qr_event, LV_EVENT_CLICKED, this);
        lv_obj_t* qr_lbl = lv_label_create(btn_qr);
        lv_label_set_text(qr_lbl, "QR");
        lv_obj_set_style_text_color(qr_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(qr_lbl);

        // --- SWITCH ---
        lv_obj_t* wifi_card = lv_obj_create(parent);
        lv_obj_set_size(wifi_card, 290, 50);
        lv_obj_align(wifi_card, LV_ALIGN_TOP_MID, 0, 60);
        lv_obj_set_style_bg_color(wifi_card, lv_color_hex(0x232326), 0);
        lv_obj_set_style_border_width(wifi_card, 0, 0);
        lv_obj_set_style_radius(wifi_card, 12, 0);
        disable_scroll_everywhere(wifi_card);

        lv_obj_t* wifi_title = lv_label_create(wifi_card);
        lv_label_set_text(wifi_title, "Wi-Fi");
        lv_obj_set_style_text_color(wifi_title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(wifi_title, LV_ALIGN_LEFT_MID, 5, 0);

        sw_wifi = lv_switch_create(wifi_card);
        lv_obj_align(sw_wifi, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(sw_wifi, lv_color_hex(0x303033), 0);
        lv_obj_set_style_bg_color(sw_wifi, lv_color_hex(0x34C759), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw_wifi, sw_wifi_event, LV_EVENT_VALUE_CHANGED, this);

        // Status Label
        status_lbl = lv_label_create(parent);
        lv_label_set_text(status_lbl, "Idle...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_align(status_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(status_lbl, LV_ALIGN_TOP_MID, 0, 115);

        // Message recherche
        search_msg_label = lv_label_create(parent);
        lv_label_set_text(search_msg_label, "Recherche des reseaux WiFi en cours...");
        lv_obj_set_style_text_color(search_msg_label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_align(search_msg_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(search_msg_label, LV_ALIGN_TOP_MID, 0, 150);
        lv_obj_add_flag(search_msg_label, LV_OBJ_FLAG_HIDDEN);

        // Message vide
        empty_msg_label = lv_label_create(parent);
        lv_label_set_text(empty_msg_label, "Activez le WiFi pour voir\nles reseaux disponibles");
        lv_obj_set_style_text_color(empty_msg_label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_align(empty_msg_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(empty_msg_label, LV_ALIGN_TOP_MID, 0, 150);
        lv_obj_add_flag(empty_msg_label, LV_OBJ_FLAG_HIDDEN);

        // Message erreur
        error_msg_label = lv_label_create(parent);
        lv_label_set_text(error_msg_label, "Erreur lors de la recherche WiFi");
        lv_obj_set_style_text_color(error_msg_label, lv_color_hex(COL_ERR), 0);
        lv_obj_set_style_text_align(error_msg_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(error_msg_label, LV_ALIGN_TOP_MID, 0, 150);
        lv_obj_add_flag(error_msg_label, LV_OBJ_FLAG_HIDDEN);

        // Liste
        list = lv_list_create(parent);
        lv_obj_set_size(list, 320, 345);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);
        lv_obj_set_style_bg_color(list, lv_color_hex(COL_BG), 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

        // --- OVERLAY ---
        overlay = lv_obj_create(parent);
        lv_obj_set_size(overlay, 320, 480);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(COL_OVERLAY), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
        lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        disable_scroll_everywhere(overlay);

        // Label d'instruction (Optionnel, pour le style)
        lv_obj_t* lbl_pass = lv_label_create(overlay);
        lv_label_set_text(lbl_pass, "Entrez le mot de passe");
        lv_obj_set_style_text_color(lbl_pass, lv_color_hex(COL_TEXT_INV), 0);
        lv_obj_align(lbl_pass, LV_ALIGN_TOP_MID, 0, 10);

        // TextArea (Arrondie, sur fond gris)
        ta_pass = lv_textarea_create(overlay);
        lv_textarea_set_one_line(ta_pass, true);
        lv_textarea_set_password_mode(ta_pass, true);
        lv_obj_set_width(ta_pass, 220);
        lv_obj_set_style_bg_color(ta_pass, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_text_color(ta_pass, lv_color_hex(COL_TEXT), 0);
        lv_obj_align(ta_pass, LV_ALIGN_TOP_MID, 0, 40);
        lv_obj_add_event_cb(ta_pass, ta_click_event, LV_EVENT_CLICKED, this);

        // Clavier
        lv_kb = lv_keyboard_create(overlay);
        lv_keyboard_set_textarea(lv_kb, ta_pass);
        lv_obj_set_width(lv_kb, 320);
        lv_obj_align(lv_kb, LV_ALIGN_BOTTOM_MID, 0, 10);
        lv_obj_add_event_cb(lv_kb, kb_event, LV_EVENT_ALL, this);
        disable_scroll_everywhere(lv_kb);

        wifi_store::load();

        const bool wifi_enabled = settings::isWifiEnabled();
        last_wifi_enabled = wifi_enabled;
        if (wifi_enabled) {
            wifi_store::autoconnect_init();
        }
        set_wifi_enabled(wifi_enabled, wifi_enabled);   
    }
    
    void update() override {
        // --- GESTION DU SCAN ---
        if (is_scanning) {
            if (millis() - last_scan_poll > 500) {
                last_scan_poll = millis();
                
                int n = WiFi.scanComplete();
                
                if (n >= 0) {
                    is_scanning = false;
                    buildListFromScan(n);
                    WiFi.scanDelete();
                } else if (n == -1) {
                    // Erreur temporaire, on continue
                }
                
                // Timeout
                if ((millis() - scan_start_time) > 10000) {
                    WiFi.scanDelete(); // STOP DRIVER
                    is_scanning = false;
                    lv_obj_clean(list);
                    lv_list_add_text(list, "Erreur Timeout");
                    lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_REFRESH, "Scan");
                    lv_obj_add_event_cb(btn, scan_event, LV_EVENT_CLICKED, this);
                }
            }
        }

        // --- GESTION DU STATUT WIFI (OPTIMISe 1Hz) ---
        // C'est ICI que ça figeait tout le système : on check 1 fois par seconde, pas plus.
        if (millis() - last_status_check > 1000) {
            last_status_check = millis();

            const bool wifi_enabled = settings::isWifiEnabled();
            if (wifi_enabled != last_wifi_enabled) {
                set_wifi_enabled(wifi_enabled, wifi_enabled);
            }

            cached_status = WiFi.status(); // On met en cache
            
            // Mise à jour UI Texte
            if (status_lbl) {
                if (!wifi_enabled) {
                    lv_label_set_text(status_lbl, "WiFi desactive");
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x8E8E93), 0);
                } else if (cached_status == WL_CONNECTED) {
                    String s = "Connecte: "; s += WiFi.SSID();
                    lv_label_set_text(status_lbl, s.c_str());
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(COL_SUCCESS), 0);
                } else if (cached_status == WL_CONNECT_FAILED) {
                    lv_label_set_text(status_lbl, "Echec connexion");
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(COL_ERR), 0);
                } else {
                    lv_label_set_text(status_lbl, "Non connecte");
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF9500), 0);
                }
            }
        }

        // --- SAUVEGARDE ---
        if (pending_save) {
            // On utilise le cache ou on verifie si necessaire
            if (cached_status == WL_CONNECTED) {
                if (String(WiFi.SSID()).equals(pending_ssid)) {
                    wifi_store::upsert_success(pending_ssid, pending_pass);
                    pending_save = false;
                    startScan(); 
                }
            }
            if ((millis() - pending_since) > 15000) {
                pending_save = false;
                startScan();
            }
        }
    }

    void stop() override {
        close_wifi_share_qr();
    }
};

#endif