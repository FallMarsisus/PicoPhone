#ifndef OLD_HOME_APP_H
#define OLD_HOME_APP_H

#include <Arduino.h>
#include <time.h>
#include <vector>
#include "App.h"
#include "AppManager.h"
#include <WiFi.h>
#include "../system/Battery.h"
#include "../system/Settings.h"
#include "../system/HomeConfig.h"
#include "./PythonApp.h"

class HomeApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* wifi_label;
    lv_obj_t* timel;
    lv_obj_t* batt_label;
    lv_obj_t* batt_icon;
    
    // Configuration des apps
    std::vector<HomeAppEntry> homeApps;
    
    // Swipe detection pour Control Center
    lv_coord_t touch_start_y = 0;
    bool touch_active = false;

    // Callback de clic sur une icône d'app
    static void app_click_callback(lv_event_t* e) {
        HomeAppEntry* entry = (HomeAppEntry*)lv_event_get_user_data(e);
        if (entry == nullptr) {
            return;
        }
        
        if (entry->appId >= 0) {
            // App système
            AppManager::switchTo((AppID)entry->appId);
        } else {
            // App Python
            PythonApp::queueScriptFromFile(entry->pythonPath);
            AppManager::switchTo(APP_PYTHON_TEST);
        }
    }

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

    void createIcon(lv_obj_t* parent, const HomeAppEntry& entry) {
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
        lv_obj_set_style_bg_color(btn, lv_color_hex(entry.color), 0);
        lv_obj_set_style_radius(btn, 14, 0);
        lv_obj_set_style_shadow_width(btn, 8, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_shadow_ofs_y(btn, 3, 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
        
        // Passe l'entry directement en user_data de l'event callback.
        lv_obj_add_event_cb(btn, app_click_callback, LV_EVENT_CLICKED, (void*)&entry);

        lv_obj_t* icon = lv_label_create(btn);
        lv_label_set_text(icon, entry.symbol.c_str());
        lv_obj_set_style_text_color(icon, lv_color_white(), 0);
        lv_obj_center(icon);

        lv_obj_t* label = lv_label_create(cont);
        lv_label_set_text(label, entry.name.c_str());
        lv_obj_set_style_text_color(label, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        
        // Charger la configuration des apps
        homeConfig::loadConfig(homeApps);
        homeConfig::removeAppById(homeApps, "wifi");
        
        // Fond dégradé sombre
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x0a0a0a), 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
        
        // Event swipe pour Control Center
        lv_obj_add_event_cb(parent, screen_touch_event, LV_EVENT_ALL, this);

        // === STATUS BAR (style iOS) ===
        lv_obj_t* bar = lv_obj_create(parent);
        lv_obj_set_size(bar, 320, 30);
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bar, 0, 0);
        lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, -3);
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

        // --- ICONES (chargées dynamiquement) ---
        for (const auto& app_entry : homeApps) {
            createIcon(grid, app_entry);
        }
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
        static int last_update = millis() - 1000;
        struct tm timeinfo;
        time_t now = time(nullptr);
        if (now > 0 && localtime_r(&now, &timeinfo) != nullptr) {
            if (millis() - last_update > 1000) { // Update toutes les minutes
                
                last_update = millis();
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