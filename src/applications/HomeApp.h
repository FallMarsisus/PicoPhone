#ifndef HOME_APP_H
#define HOME_APP_H

#include <Arduino.h>
#include <time.h>
#include "App.h"
#include "AppManager.h"
#include <WiFi.h>
#include "../system/Battery.h"
#include "../system/Settings.h"

class HomeApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* wifi_label;
    lv_obj_t* timel;
    lv_obj_t* batt_label;
    lv_obj_t* batt_icon;
    
    // Swipe detection pour Control Center
    lv_coord_t touch_start_y = 0;
    bool touch_active = false;

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

    // Swipe down = Control Center
    static void screen_touch_event(lv_event_t* e) {
        HomeApp* app = (HomeApp*)lv_event_get_user_data(e);
        lv_event_code_t code = lv_event_get_code(e);
        
        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            app->touch_start_y = p.y;
            app->touch_active = (p.y < 50); // seulement si on part du haut
        }
        else if (code == LV_EVENT_RELEASED && app->touch_active) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            app->touch_active = false;
            
            if ((p.y - app->touch_start_y) > 60) {
                AppManager::openControlCenter();
            }
        }
    }

    void createIcon(lv_obj_t* parent, const char* name, lv_color_t color, const char* symbol, lv_event_cb_t cb) {
        lv_obj_t* cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 70, 85);
        lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_gap(cont, 5, 0);

        lv_obj_t* btn = lv_btn_create(cont);
        lv_obj_set_size(btn, 55, 55);
        lv_obj_set_style_bg_color(btn, color, 0);
        lv_obj_set_style_radius(btn, 14, 0);
        lv_obj_set_style_shadow_width(btn, 8, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_shadow_ofs_y(btn, 3, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
        if (cb) lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, symbol);
        lv_obj_set_style_text_color(icon, lv_color_white(), 0);
        lv_obj_center(icon);

        lv_obj_t* label = lv_label_create(cont);
        lv_label_set_text(label, name);
        lv_obj_set_style_text_color(label, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        
        // Fond dégradé sombre
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x0a0a0a), 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        
        // Event swipe pour Control Center
        lv_obj_add_event_cb(parent, screen_touch_event, LV_EVENT_ALL, this);

        // === STATUS BAR (style iOS) ===
        lv_obj_t* bar = lv_obj_create(parent);
        lv_obj_set_size(bar, 320, 35);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_scrollbar_mode(bar, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(bar, LV_OBJ_FLAG_EVENT_BUBBLE); // Laisse passer le swipe
        
        timel = lv_label_create(bar);
        lv_label_set_text(timel, "00:00");
        lv_obj_set_style_text_color(timel, lv_color_white(), 0);
        lv_obj_set_style_text_font(timel, &lv_font_montserrat_14, 0);
        lv_obj_align(timel, LV_ALIGN_LEFT_MID, 8, 0);

        wifi_label = lv_label_create(bar);
        lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x555555), 0);
        lv_obj_align(wifi_label, LV_ALIGN_RIGHT_MID, -50, 0);

        batt_label = lv_label_create(bar);
        lv_label_set_text(batt_label, "--%");
        lv_obj_set_style_text_color(batt_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(batt_label, &lv_font_montserrat_12, 0);
        lv_obj_align(batt_label, LV_ALIGN_RIGHT_MID, -5, 0);

        // === GRILLE D'APPS (style launcher) ===
        lv_obj_t* grid = lv_obj_create(parent);
        lv_obj_set_size(grid, 310, 420);
        lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(grid, 15, 0);
        lv_obj_set_style_pad_column(grid, 5, 0);
        lv_obj_set_style_pad_top(grid, 15, 0);
        lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(grid, LV_OBJ_FLAG_EVENT_BUBBLE);

        // --- ICONES ---
        createIcon(grid, "Meteo",    lv_color_hex(0xFF9500), LV_SYMBOL_CHARGE,   open_weather);
        createIcon(grid, "Telegram", lv_color_hex(0x0088CC), LV_SYMBOL_GPS, open_telegram);
        createIcon(grid, "Velib'",   lv_color_hex(0x34C759), LV_SYMBOL_IMAGE,    open_velib);
        createIcon(grid, "2048",     lv_color_hex(0xFF375F), LV_SYMBOL_SHUFFLE,  open_2048);
        createIcon(grid, "Ardoise",  lv_color_hex(0x5AC8FA), LV_SYMBOL_EDIT,     open_sketch);
        createIcon(grid, "WiFi",     lv_color_hex(0x007AFF), LV_SYMBOL_WIFI,     open_wifi);
        createIcon(grid, "Calcul",   lv_color_hex(0xFF3B30), LV_SYMBOL_PLUS,     open_calc);
        createIcon(grid, "Systeme",  lv_color_hex(0x8E8E93), LV_SYMBOL_REFRESH,  open_bl);
        createIcon(grid, "Reglages", lv_color_hex(0x636366), LV_SYMBOL_SETTINGS, open_settings);
        createIcon(grid, "Contacts", lv_color_hex(0x5856D6), LV_SYMBOL_LIST, open_contacts);
        createIcon(grid, "Timer",    lv_color_hex(0xFF9F0A), LV_SYMBOL_BELL, open_timer);
    }

    void update() override {
        // WiFi status toutes les 2s
        static unsigned long last_check = 0;
        if (millis() - last_check > 2000) {
            last_check = millis();
            
            if (WiFi.status() == WL_CONNECTED) {
                lv_obj_set_style_text_color(wifi_label, lv_color_white(), 0);
            } else {
                lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x555555), 0);
            }
        }

        // Heure toutes les minutes
        static int last_min = -1;
        struct tm timeinfo;
        time_t now = time(nullptr);
        if (now > 0 && localtime_r(&now, &timeinfo) != nullptr) {
            if (timeinfo.tm_min != last_min) {
                last_min = timeinfo.tm_min;
                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                lv_label_set_text(timel, buf);
            }
        }

        // Batterie toutes les 3s
        static unsigned long last_batt = 0;
        if (millis() - last_batt > 3000) {
            last_batt = millis();
            const uint8_t p = battery::read_percent();
            char bbuf[8];
            snprintf(bbuf, sizeof(bbuf), "%u%%", (unsigned)p);
            lv_label_set_text(batt_label, bbuf);
        }
    }
};

#endif