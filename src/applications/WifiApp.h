#ifndef WIFI_APP_H
#define WIFI_APP_H

#include "App.h"
#include "AppManager.h"
#include "WifiStore.h"
#include <WiFi.h>
#include <cstring>
#include "../plugins/lv_t9_keyboard.h"

// --- COULEURS (THÈME SOMBRE) ---
#define COL_BG       0x000000 // Noir (Fond App)
#define COL_OVERLAY  0x1C1C1E // Gris très foncé (Fond Popup / Header)
#define COL_CARD     0x2C2C2E // Gris foncé (Cartes / boutons)
#define COL_TEXT     0xFFFFFF // Blanc (Texte principal)
#define COL_TEXT_INV 0x000000 // Noir (Texte sur fond clair - si utilisé)
#define COL_PRIM     0x0A84FF // Bleu iOS (Accent)
#define COL_ERR      0xFF453A // Rouge iOS (Erreur)

class WifiApp : public App {
private:
    lv_obj_t* list;
    lv_obj_t* lv_kb;
    lv_obj_t* ta_pass;
    lv_obj_t* overlay;
    char target_ssid[32];

    // Sauvegarde différée
    bool pending_save = false;
    uint32_t pending_since = 0;
    char pending_ssid[33] = {0};
    char pending_pass[65] = {0};

    // Scan Asynchrone & Timers
    bool is_scanning = false;
    uint32_t scan_start_time = 0;
    uint32_t last_scan_poll = 0;
    
    // --- OPTIMISATION LAG ---
    // On ne vérifie le statut WiFi que toutes les 1000ms
    // pour ne pas ralentir le tactile.
    uint32_t last_status_check = 0; 
    int cached_status = WL_DISCONNECTED; // On garde le statut en mémoire

    lv_obj_t* status_lbl = nullptr;

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
            
            // On force une mise à jour immédiate du statut
            app->last_status_check = 0; 
        } 
        else if (code == LV_EVENT_CANCEL) {
            app->close_password_prompt();
        }
    }

    static void ta_click_event(lv_event_t* e) {
        WifiApp* app = (WifiApp*)lv_event_get_user_data(e);
        lv_t9_kb_set_textarea(app->lv_kb, app->ta_pass);
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
        lv_t9_kb_set_textarea(lv_kb, ta_pass);
        disable_scroll_everywhere(overlay);
        disable_scroll_everywhere(ta_pass);
        disable_scroll_everywhere(lv_kb);
    }

    void close_password_prompt() {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_t9_kb_set_textarea(lv_kb, NULL);
        lv_obj_t* sug = lv_obj_get_child(lv_kb, 0);
        if(sug) lv_obj_add_flag(sug, LV_OBJ_FLAG_HIDDEN);
    }

    void show_connecting_state(const char* ssid) {
        lv_obj_clean(list);
        String s = "Connexion a:\n"; s += ssid; s += "\n...";
        lv_list_add_text(list, s.c_str());
    }

    void startScan() {
        if (is_scanning) return;

        WiFi.scanDelete();
        if (WiFi.getMode() != WIFI_STA) {
            WiFi.mode(WIFI_STA);
            delay(20); 
        }

        lv_obj_clean(list);
        lv_obj_set_style_text_color(list, lv_color_hex(COL_TEXT), 0);

        if (WiFi.status() == WL_CONNECTED) {
            String s = "Actuel: "; s += WiFi.SSID();
            lv_list_add_text(list, s.c_str());
        }

        lv_list_add_text(list, "Recherche...");
        
        WiFi.scanNetworks(true); 
        is_scanning = true;
        scan_start_time = millis();
        last_scan_poll = millis();
    }

    void buildListFromScan(int n) {
        lv_obj_clean(list);

        if (WiFi.status() == WL_CONNECTED) {
            String s = "Actuel: "; s += WiFi.SSID();
            lv_list_add_text(list, s.c_str());
        }

        if (wifi_store::count() > 0) {
            lv_list_add_text(list, "Connus");
            for (size_t i = 0; i < wifi_store::count(); i++) {
                auto* e = wifi_store::get(i);
                if (!e || !e->ssid[0]) continue;
                
                lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_OK, e->ssid);
                lv_obj_add_event_cb(btn, list_btn_event, LV_EVENT_CLICKED, this);
                lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
                
                // STYLE ARRONDIS (Par défaut LVGL)
                lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
                lv_obj_set_style_text_color(btn, lv_color_hex(COL_PRIM), 0);
                lv_obj_set_style_pad_all(btn, 14, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xDDDDDD), 0);
                lv_obj_set_style_border_width(btn, 1, 0);
            }
        }

        lv_list_add_text(list, "Detectes");

        if (n <= 0) {
            lv_list_add_text(list, "Aucun reseau");
        } else {
            for (int i = 0; i < n; ++i) {
                if(strlen(WiFi.SSID(i)) == 0) continue;

                lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, WiFi.SSID(i));
                lv_obj_add_event_cb(btn, list_btn_event, LV_EVENT_CLICKED, this);
                lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
                
                // STYLE ARRONDIS
                lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
                lv_obj_set_style_text_color(btn, lv_color_hex(COL_TEXT), 0);
                lv_obj_set_style_pad_all(btn, 15, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0xDDDDDD), 0);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
            }
        }
    }

public:
    void start(lv_obj_t* parent) override {
        // Fond App
        lv_obj_set_style_bg_color(parent, lv_color_hex(COL_BG), 0);
        disable_scroll_everywhere(parent);
        
        // Header
        lv_obj_t* header = lv_obj_create(parent);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(COL_OVERLAY), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_shadow_width(header, 10, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_set_style_shadow_opa(header, LV_OPA_90, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        disable_scroll_everywhere(header);

        // Bouton Retour
        lv_obj_t* btn1 = lv_btn_create(header);
        lv_obj_set_size(btn1, 40, 40);
        lv_obj_align(btn1, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_color(btn1, lv_color_hex(COL_BG), 0);
        lv_obj_set_style_text_color(btn1, lv_color_hex(COL_TEXT), 0);
        lv_obj_add_event_cb(btn1, go_home, LV_EVENT_CLICKED, NULL);
        lv_obj_t* l1 = lv_label_create(btn1);
        lv_label_set_text(l1, LV_SYMBOL_LEFT);
        lv_obj_center(l1);

        // Titre
        lv_obj_t* t = lv_label_create(header);
        lv_label_set_text(t, "WiFi");
        lv_obj_set_style_text_color(t, lv_color_hex(COL_TEXT), 0);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, 0);

        // Bouton Settings
        lv_obj_t* btn2 = lv_btn_create(header);
        lv_obj_set_size(btn2, 40, 40);
        lv_obj_align(btn2, LV_ALIGN_RIGHT_MID, 10, 0);
        lv_obj_set_style_bg_color(btn2, lv_color_hex(COL_BG), 0);
        lv_obj_set_style_text_color(btn2, lv_color_hex(COL_TEXT), 0);
        lv_obj_t* l2 = lv_label_create(btn2);
        lv_label_set_text(l2, LV_SYMBOL_SETTINGS);
        lv_obj_center(l2);

        // Status Label
        status_lbl = lv_label_create(parent);
        lv_label_set_text(status_lbl, "Idle...");
        lv_obj_set_style_text_color(status_lbl, lv_color_hex(COL_OVERLAY), 0);
        lv_obj_align(status_lbl, LV_ALIGN_TOP_MID, 0, 54);

        // Liste
        list = lv_list_create(parent);
        lv_obj_set_size(list, 320, 375);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -10);
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
        lv_kb = lv_t9_kb_create(overlay);
        lv_t9_kb_set_textarea(lv_kb, ta_pass);
        lv_obj_set_width(lv_kb, 320);
        lv_obj_align(lv_kb, LV_ALIGN_BOTTOM_MID, 0, 10);
        lv_obj_add_event_cb(lv_kb, kb_event, LV_EVENT_ALL, this);
        disable_scroll_everywhere(lv_kb);

        wifi_store::load();
        wifi_store::autoconnect_init();

        startScan();
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

        // --- GESTION DU STATUT WIFI (OPTIMISÉ 1Hz) ---
        // C'est ICI que ça figeait tout le système : on check 1 fois par seconde, pas plus.
        if (millis() - last_status_check > 1000) {
            last_status_check = millis();
            cached_status = WiFi.status(); // On met en cache
            
            // Mise à jour UI Texte
            if (status_lbl) {
                if (cached_status == WL_CONNECTED) {
                    String s = "Connecte: "; s += WiFi.SSID();
                    lv_label_set_text(status_lbl, s.c_str());
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(0x34C759), 0);
                } else if (cached_status == WL_CONNECT_FAILED) {
                    lv_label_set_text(status_lbl, "Echec connexion");
                    lv_obj_set_style_text_color(status_lbl, lv_color_hex(COL_ERR), 0);
                }
            }
        }

        // --- SAUVEGARDE ---
        if (pending_save) {
            // On utilise le cache ou on vérifie si nécessaire
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
};

#endif