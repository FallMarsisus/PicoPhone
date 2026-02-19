#ifndef NEW_HOME_APP_H
#define NEW_HOME_APP_H

#include <Arduino.h>
#include <time.h>
#include "App.h"
#include "AppManager.h"
#include <WiFi.h>
#include "../system/Battery.h"
#include "../system/Settings.h"

LV_IMG_DECLARE(fondecran);

// Structure pour définir une app dans la liste
typedef struct {
    const char* name;
    const char* symbol;
    lv_color_t color;
    lv_event_cb_t callback;
} AppEntry;

class NewHomeApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* bg_img;
    
    // Top Bar elements
    lv_obj_t* top_bar_cont; 
    lv_obj_t* wifi_label;
    lv_obj_t* time_label;
    lv_obj_t* batt_icon;
    lv_obj_t* res_icon;
    
    // Bottom Quick Actions
    lv_obj_t* quick_actions_cont;

    // App List (Le menu swipe up)
    lv_obj_t *app_list_cont; 
    bool app_list_open = false;
    
    // Swipe variables
    lv_point_t touch_start_point;

    // Callbacks 
    static void open_telegram(lv_event_t* e) { AppManager::switchTo(APP_TELEGRAM); }
    static void open_bl(lv_event_t* e) { AppManager::switchTo(APP_BOOTLOADER); }
    static void open_weather(lv_event_t* e) { AppManager::switchTo(APP_WEATHER); }
    static void open_velib(lv_event_t* e) { AppManager::switchTo(APP_VELIB); }
    static void open_2048(lv_event_t* e) { AppManager::switchTo(APP_2048); }
    static void open_sketch(lv_event_t* e) { AppManager::switchTo(APP_SKETCH); }
    static void open_calc(lv_event_t* e) { AppManager::switchTo(APP_CALC); }
    static void open_wifi(lv_event_t* e) { AppManager::switchTo(APP_WIFI); }
    static void open_settings(lv_event_t* e) { AppManager::switchTo(APP_SETTINGS); }
    static void open_contacts(lv_event_t* e) { AppManager::switchTo(APP_CONTACTS); }
    static void open_timer(lv_event_t* e) { AppManager::switchTo(APP_TIMER); }
    static void open_explorer(lv_event_t* e) { AppManager::switchTo(APP_EXPLORER); }
    static void open_new_home(lv_event_t* e) { AppManager::switchTo(APP_OLD_HOME); }
    // static void open_wikipedia(lv_event_t* e) { AppManager::switchTo(APP_WIKIPEDIA); }

    // --- GESTION DE L'ANIMATION ---
    static void anim_y_cb(void * var, int32_t v) {
        lv_obj_set_y((lv_obj_t*)var, v);
    }

    void toggleAppList(bool open) {
        app_list_open = open;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, app_list_cont);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_y_cb);
        lv_anim_set_time(&a, 300); // 300ms d'animation
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out); // Effet fluide

        if (open) {
            // Monter de 480 (bas) vers 0 (haut)
            lv_anim_set_values(&a, 480 , 70); // 55px du bas pour laisser les quick actions
            // On cache la barre du haut pour faire propre pendant que ça monte
            // lv_obj_add_flag(top_bar_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(quick_actions_cont, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Descendre de current Y vers 480
            lv_anim_set_values(&a, lv_obj_get_y(app_list_cont), 480 );
            // On réaffiche les barres
            // lv_obj_clear_flag(top_bar_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(quick_actions_cont, LV_OBJ_FLAG_HIDDEN);
        }
        lv_anim_start(&a);
    }

    static void close_app_list(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        if(app) app->toggleAppList(false);
    }

    // --- GESTION DU SWIPE ---
    static void screen_touch_event(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        if(!app) return;

        lv_event_code_t code = lv_event_get_code(e);
        
        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_indev_get_point(indev, &app->touch_start_point);
        }
        else if (code == LV_EVENT_RELEASED) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_point_t end_point;
            lv_indev_get_point(indev, &end_point);
            
            lv_coord_t diff_y = end_point.y - app->touch_start_point.y;

            // Swipe UP (diff négatif) -> Ouvre la liste
            if (diff_y < -50 && !app->app_list_open) {
                app->toggleAppList(true);
            }
            // Swipe DOWN (diff positif) -> Ferme la liste
            else if (diff_y > 50 && app->app_list_open) {
                app->toggleAppList(false);
            }
            // Swipe DOWN depuis l'écran principal -> Control Center
            else if (diff_y > 50 && !app->app_list_open && app->touch_start_point.y < 50) {
                 AppManager::openControlCenter();
            }
        }
    }

    // --- CONSTRUCTION UI LISTE ITEM (Style Image) ---
    void create_list_item(lv_obj_t* parent, const char* name, const char* icon_symbol, lv_event_cb_t cb, lv_color_t color = lv_color_hex(0x007AFF)) {
        // Le conteneur de la ligne (Style Glass)
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 282, 60); // Largeur fixe un peu moins que l'écran
        lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0); // Translucide
        lv_obj_set_style_radius(btn, 15, 0); // Arrondi fort
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        
        if(cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

        // L'icone x à gauche
        lv_obj_t* icon_bg = lv_obj_create(btn);
        lv_obj_set_size(icon_bg, 45, 45);
        lv_obj_set_style_radius(icon_bg, 15, 0);
        lv_obj_set_style_bg_color(icon_bg, color, 0); // Bleu style image
        lv_obj_set_style_border_width(icon_bg, 0, 0);
        lv_obj_align(icon_bg, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE); 

        lv_obj_t* icon_lbl = lv_label_create(icon_bg);
        lv_label_set_text(icon_lbl, icon_symbol);
        lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        lv_obj_center(icon_lbl);

        // Le texte "Clock", "Settings" etc.
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, name);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0); // Police plus grosse
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0); // Gris foncé
        
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 70, 0);
    }

    void createAppListUI() {
        // Conteneur plein écran qui va glisser
        app_list_cont = lv_obj_create(main_bg);
        lv_obj_set_size(app_list_cont, 300, 400);
        // Position initiale : en bas (hors écran)
        lv_obj_set_y(app_list_cont, 480+60); 
        lv_obj_set_x(app_list_cont, 10);
        
        lv_obj_set_scroll_dir(app_list_cont, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(app_list_cont, LV_SCROLLBAR_MODE_OFF);

        // Style du conteneur (Fond flouté/sombre comme sur l'image)
        lv_obj_set_style_bg_color(app_list_cont, lv_color_hex(0xD9D9D9), 0);
        lv_obj_set_style_bg_opa(app_list_cont, LV_OPA_40, 0); // Transparence globale du panneau
        lv_obj_set_style_border_width(app_list_cont, 0, 0);
        lv_obj_set_style_radius(app_list_cont, 20, 0); // Arrondi en haut
        
        // Pour que les éléments s'empilent
        lv_obj_set_flex_flow(app_list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(app_list_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(app_list_cont, 30, 0); // Espace en haut
        lv_obj_set_style_pad_gap(app_list_cont, 10, 0); // Espace entre les items

        // Ajouter les items (Exemples)
        // create_list_item(app_list_cont, "Fermer", LV_SYMBOL_UP, close_app_list, lv_color_hex(0x007AFF)); // Marche pas
        create_list_item(app_list_cont, "2048", "2048", open_2048, lv_color_hex(0xFF9500));
        create_list_item(app_list_cont, "Ardoise", LV_SYMBOL_EDIT, open_sketch, lv_color_hex(0x5AC8FA));
        create_list_item(app_list_cont, "Calculatrice", LV_SYMBOL_PLUS, open_calc, lv_color_hex(0xFF3B30));
        create_list_item(app_list_cont, "Contacts", LV_SYMBOL_LIST, open_contacts, lv_color_hex(0x5856D6));
        create_list_item(app_list_cont, "Explorateur", LV_SYMBOL_DIRECTORY, open_explorer, lv_color_hex(0x34C759));
        create_list_item(app_list_cont, "Horloge", LV_SYMBOL_BELL, open_timer, lv_color_hex(0x007AFF));
        create_list_item(app_list_cont, "Meteo", LV_SYMBOL_CHARGE, open_weather, lv_color_hex(0x4CAF50));
        create_list_item(app_list_cont, "Settings", LV_SYMBOL_SETTINGS, open_settings, lv_color_hex(0xFF9800));
        create_list_item(app_list_cont, "Telegram", LV_SYMBOL_GPS, open_telegram, lv_color_hex(0x2196F3));
        create_list_item(app_list_cont, "Velib", "V", open_velib, lv_color_hex(0x9C27B0));
        create_list_item(app_list_cont, "WiFi", LV_SYMBOL_WIFI, open_wifi, lv_color_hex(0x00BCD4));
        // create_list_item(app_list_cont, "Wikipedia", "W", open_wikipedia, lv_color_hex(0x607D8B));

        
        // IMPORTANT: Le swipe sur la liste doit aussi être détecté pour pouvoir refermer
        lv_obj_add_event_cb(app_list_cont, screen_touch_event, LV_EVENT_ALL, this);
    }

    void createTopBar(lv_obj_t* parent) {
        top_bar_cont = lv_obj_create(parent); // Renommé pour gestion visibilité
        lv_obj_set_size(top_bar_cont, 300, 40);
        lv_obj_set_style_radius(top_bar_cont, 10, 0);
        lv_obj_set_style_bg_color(top_bar_cont, lv_color_hex(0xD9D9D9), 0);
        lv_obj_set_style_bg_opa(top_bar_cont, LV_OPA_40, 0);
        lv_obj_align(top_bar_cont, LV_ALIGN_TOP_MID, 0, 15);
        lv_obj_set_style_border_opa(top_bar_cont, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(top_bar_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(top_bar_cont, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* icon_zone = lv_obj_create(top_bar_cont);
        lv_obj_set_size(icon_zone, 100, 20);
        lv_obj_align(icon_zone, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_border_opa(icon_zone, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(icon_zone, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(icon_zone, LV_OBJ_FLAG_SCROLLABLE);

        wifi_label = lv_label_create(icon_zone);
        lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x323232), 0);
        lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_18, 0);

        time_label = lv_label_create(top_bar_cont);
        lv_label_set_text(time_label, "00:00");
        lv_obj_set_style_text_color(time_label, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_18, 0);
        lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 10, 0);
        
        // Correction ici: Utiliser batt_icon (pas label)
        batt_icon = lv_label_create(icon_zone);
        lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_FULL);
        lv_obj_set_style_text_color(batt_icon, lv_color_hex(0x323232), 0);
        lv_obj_align(batt_icon, LV_ALIGN_LEFT_MID, 60, 0);
        lv_obj_set_style_text_font(batt_icon, &lv_font_montserrat_18, 0);
    }

    void createQuickActionApps(lv_obj_t* parent) {
        // Renommé conteneur pour gestion visibilité
        quick_actions_cont = lv_obj_create(parent);
        lv_obj_set_size(quick_actions_cont, 300, 74);
        lv_obj_set_style_radius(quick_actions_cont, 10, 0);
        lv_obj_set_style_bg_color(quick_actions_cont, lv_color_hex(0xD9D9D9), 0);
        lv_obj_set_style_bg_opa(quick_actions_cont, LV_OPA_40, 0);
        lv_obj_align(quick_actions_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_border_opa(quick_actions_cont, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(quick_actions_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(quick_actions_cont, LV_OBJ_FLAG_CLICKABLE);

        // Données temporaires locales pour créer les boutons
        AppEntry apps[] = {
            {"TG", LV_SYMBOL_GPS, lv_color_hex(0x0088CC), open_telegram},
            {"Météo", LV_SYMBOL_CHARGE, lv_color_hex(0xFF9500), open_weather},
            {"Velib", "V", lv_color_hex(0x34C759), open_velib}
        };

        for(int i = 0; i < 3; i++) {
            lv_obj_t* icon = lv_btn_create(quick_actions_cont);
            lv_obj_set_size(icon, 55, 55);
            lv_obj_set_style_bg_color(icon, apps[i].color, 0);
            lv_obj_set_style_radius(icon, 14, 0);
            lv_obj_set_style_shadow_width(icon, 8, 0);
            lv_obj_set_style_shadow_color(icon, lv_color_hex(0x333333), 0);
            lv_obj_set_style_shadow_ofs_y(icon, 3, 0);
            lv_obj_set_style_shadow_opa(icon, LV_OPA_40, 0);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, i * 85 + 24, 0);

            lv_obj_t* icon_label = lv_label_create(icon);
            lv_label_set_text(icon_label, apps[i].symbol);
            lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
            lv_obj_center(icon_label);

            if (apps[i].callback) {
                lv_obj_add_event_cb(icon, apps[i].callback, LV_EVENT_CLICKED, NULL);
            }
        }
    }

public: 
    void start(lv_obj_t* parent) override {
            main_bg = parent;
            lv_obj_set_style_bg_color(main_bg, lv_color_black(), 0);
            lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE); // Nécessaire pour le swipe
            // --- IMAGE DE FOND ---
            bg_img = lv_img_create(main_bg);
            lv_img_set_src(bg_img, &fondecran);
            lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
            lv_obj_clear_flag(bg_img, LV_OBJ_FLAG_CLICKABLE); 
            lv_obj_clear_flag(bg_img, LV_OBJ_FLAG_SCROLLABLE);
            // Attention: set_style_opa sur une image 320x480 est LENT. 
            // Préférez Recolor si possible, sinon OK sur Pico 2.
            lv_obj_set_style_img_recolor(bg_img, lv_color_black(), 0);
            lv_obj_set_style_img_recolor_opa(bg_img, LV_OPA_30, 0); // Assombrir un peu
            
            // --- IMPORTANT POUR LE SWIPE ---
            // On rend le fond interactif pour capter le swipe
            lv_obj_add_flag(main_bg, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(main_bg, screen_touch_event, LV_EVENT_ALL, this);

            createTopBar(main_bg);
            createQuickActionApps(main_bg);
            
            // --- CRÉATION DE LA LISTE (Cachée en bas au début) ---
            createAppListUI();
    }
    void update() override {
            // WiFi status toutes les 2s
            static unsigned long last_check = 0;
            if (millis() - last_check > 3000) {
                last_check = millis();
                
                if (WiFi.status() == WL_CONNECTED) {
                    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x232323), 0);
                } else {
                    lv_obj_set_style_text_color(wifi_label, lv_color_hex(0xC1C1C1), 0);
                }
            }

            // Heure toutes les minutes
            static int last_update = millis() - 2000;
            struct tm timeinfo;
            time_t now = time(nullptr);
            if (now > 0 && localtime_r(&now, &timeinfo) != nullptr) {
                if (millis() - last_update > 1000) { // Update toutes les minutes
                    
                    last_update = millis();
                    char buf[6];
                    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                    lv_label_set_text(time_label, buf);
                }
            }

            // Batterie toutes les 3s
            static unsigned long last_batt = 0;
            if (millis() - last_batt > 4000) {
                last_batt = millis();
                const uint8_t p = battery::read_percent();
                char bbuf[8];
                snprintf(bbuf, sizeof(bbuf), "%u%%", (unsigned)p);
                if(p > 80) {
                    lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_FULL);
                } else if (p > 60) {
                    lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_3);
                } else if (p > 40) {
                    lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_2);
                } else if (p > 20) {
                    lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_1);
                } else {
                    lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_EMPTY);
                }
            }
        }

         void stop() override {
                // Nettoyage si nécessaire (ex: supprimer les objets LVGL)
                lv_obj_clean(main_bg); // Supprime tous les enfants (barres, liste, etc.)
         }
};

#endif
