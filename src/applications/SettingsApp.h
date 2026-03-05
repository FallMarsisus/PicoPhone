#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include "App.h"
#include "AppManager.h"
#include "../system/Settings.h"
#include "../system/LTE.h"
#include <WiFi.h>
#include <RP2040Support.h>
#include <time.h>

class SettingsApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* list_cont;
    
    // PIN Panel
    lv_obj_t* pin_panel;
    lv_obj_t* ta_pin;
    lv_obj_t* kb_pin;
    lv_obj_t* lbl_pin_status;
    lv_obj_t* pin_panel_title;
    bool setting_new_pin = false;
    
    // Time Panel
    lv_obj_t* time_panel;
    lv_obj_t* time_panel_title;
    lv_obj_t* roller_hour;
    lv_obj_t* roller_minute;
    lv_obj_t* roller_day;
    lv_obj_t* roller_month;
    lv_obj_t* roller_year;
    lv_obj_t* lbl_time_status;
    
    // --- EVENTS ---
    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }
    
    // --- PIN Toggle ---
    static void pin_toggle_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        lv_obj_t* sw = lv_event_get_target(e);
        bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        
        if (enabled) {
            // Demander un nouveau PIN
            app->setting_new_pin = true;
            app->showPinPanel("Definir un code PIN:");
        } else {
            settings::setPinEnabled(false);
        }
    }
    
    // --- PIN Entry ---
    static void pin_kb_ready(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        const char* pin = lv_textarea_get_text(app->ta_pin);
        
        if (strlen(pin) >= 4 && strlen(pin) <= 6) {
            settings::setPinCode(pin);
            settings::setPinEnabled(true);
            app->hidePinPanel();
            lv_label_set_text(app->lbl_pin_status, "Code active");
            lv_obj_set_style_text_color(app->lbl_pin_status, lv_color_hex(0x4CD964), 0);
        } else {
            lv_label_set_text(app->lbl_pin_status, "4 a 6 chiffres requis");
            lv_obj_set_style_text_color(app->lbl_pin_status, lv_color_hex(0xFF3B30), 0);
        }
    }
    
    static void pin_kb_cancel(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->hidePinPanel();
        // Revert switch si on annule pendant la création
        if (app->setting_new_pin && !settings::isPinEnabled()) {
            // Le switch doit revenir à off
            app->refreshList();
        }
    }
    
    // --- Lock Timeout ---
    static void timeout_event(lv_event_t* e) {
        lv_obj_t* dd = lv_event_get_target(e);
        uint16_t sel = lv_dropdown_get_selected(dd);
        
        uint32_t timeouts[] = {0, 15000, 30000, 60000, 120000, 300000};
        if (sel < 6) {
            settings::setLockTimeout(timeouts[sel]);
        }
    }
    
    // --- Brightness ---
    static void brightness_event(lv_event_t* e) {
        lv_obj_t* slider = lv_event_get_target(e);
        int val = lv_slider_get_value(slider);
        uint8_t hw_val = (uint8_t)(5 + (val * 250 / 100));
        settings::setBrightness(hw_val);
    }
    
    // --- Change PIN ---
    static void change_pin_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->setting_new_pin = true;
        app->showPinPanel("Nouveau code PIN:");
    }

    static void bootloader_event(lv_event_t* e) {
        // Redémarrage en mode bootloader
        delay(100); // Délai pour éviter les rebonds
        rp2040.rebootToBootloader();
    }
    
    // --- Reboot System ---
    static void reboot_event(lv_event_t* e) {
        delay(100);
        rp2040.reboot();
    }
    
    // --- Sync Time Auto ---
    static void sync_time_auto_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        
        if (!LTE::isEnabled() || LTE::isAirplaneMode()) {
            lv_label_set_text(app->lbl_time_status, "Reseau 4G desactive");
            lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0xFF3B30), 0);
            return;
        }
        
        if (!LTE::isReadyForData()) {
            lv_label_set_text(app->lbl_time_status, "Pas de signal 4G");
            lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0xFF9500), 0);
            return;
        }
        
        // Synchronisation en cours
        lv_label_set_text(app->lbl_time_status, "Synchronisation...");
        lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0x007AFF), 0);
        
        // La synchronisation se fera automatiquement via LTE::update() dans le core1
        // On affiche juste un message de succès
        lv_label_set_text(app->lbl_time_status, "Sync demandee");
        lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0x4CD964), 0);
    }
    
    // --- Manual Time Setting ---
    static void show_time_panel_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->showTimePanel();
    }
    
    static void time_panel_save(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        
        uint16_t hour = lv_roller_get_selected(app->roller_hour);
        uint16_t minute = lv_roller_get_selected(app->roller_minute);
        uint16_t day = lv_roller_get_selected(app->roller_day) + 1;
        uint16_t month = lv_roller_get_selected(app->roller_month) + 1;
        uint16_t year = lv_roller_get_selected(app->roller_year) + 2024;
        
        struct tm t = {};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        
        time_t epoch = mktime(&t);
        if (epoch != (time_t)-1) {
            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            
            lv_label_set_text(app->lbl_time_status, "Heure mise a jour");
            lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0x4CD964), 0);
        } else {
            lv_label_set_text(app->lbl_time_status, "Erreur: date invalide");
            lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0xFF3B30), 0);
        }
        
        app->hideTimePanel();
    }
    
    static void time_panel_cancel(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->hideTimePanel();
    }
    
    // --- UI Helpers ---
    
    lv_obj_t* createSection(lv_obj_t* parent, const char* title) {
        lv_obj_t* lbl = lv_label_create(parent);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_pad_left(lbl, 15, 0);
        return lbl;
    }
    
    lv_obj_t* createSettingRow(lv_obj_t* parent, const char* label_text) {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, lv_pct(100), 50);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_left(row, 15, 0);
        lv_obj_set_style_pad_right(row, 15, 0);
        
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, label_text);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        
        return row;
    }
    
    void showPinPanel(const char* title) {
        lv_label_set_text(pin_panel_title, title);
        lv_obj_set_style_text_color(pin_panel_title, lv_color_white(), 0);
        lv_textarea_set_text(ta_pin, "");
        lv_obj_clear_flag(pin_panel, LV_OBJ_FLAG_HIDDEN);
    }
    
    void hidePinPanel() {
        lv_obj_add_flag(pin_panel, LV_OBJ_FLAG_HIDDEN);
        setting_new_pin = false;
    }
    
    void showTimePanel() {
        // Get current time
        time_t now;
        time(&now);
        struct tm* t = localtime(&now);
        
        // Set rollers to current time
        lv_roller_set_selected(roller_hour, t->tm_hour, LV_ANIM_OFF);
        lv_roller_set_selected(roller_minute, t->tm_min, LV_ANIM_OFF);
        lv_roller_set_selected(roller_day, t->tm_mday - 1, LV_ANIM_OFF);
        lv_roller_set_selected(roller_month, t->tm_mon, LV_ANIM_OFF);
        lv_roller_set_selected(roller_year, (t->tm_year + 1900) - 2024, LV_ANIM_OFF);
        
        lv_obj_clear_flag(time_panel, LV_OBJ_FLAG_HIDDEN);
    }
    
    void hideTimePanel() {
        lv_obj_add_flag(time_panel, LV_OBJ_FLAG_HIDDEN);
    }
    
    void refreshList() {
        lv_obj_clean(list_cont);
        buildSettingsList();
    }
    
    void buildSettingsList() {
        // ===== SECTION: VERROUILLAGE =====
        createSection(list_cont, "VERROUILLAGE");
        
        // Code PIN on/off
        lv_obj_t* row_pin = createSettingRow(list_cont, "Code PIN");
        lv_obj_t* sw_pin = lv_switch_create(row_pin);
        lv_obj_align(sw_pin, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(sw_pin, lv_color_hex(0x4CD964), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (settings::isPinEnabled()) {
            lv_obj_add_state(sw_pin, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(sw_pin, pin_toggle_event, LV_EVENT_VALUE_CHANGED, this);
        
        // Modifier PIN (visible seulement si PIN actif)
        if (settings::isPinEnabled()) {
            lv_obj_t* row_change = createSettingRow(list_cont, "Modifier le code");
            lv_obj_t* chevron = lv_label_create(row_change);
            lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(chevron, lv_color_hex(0x8E8E93), 0);
            lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_add_flag(row_change, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row_change, change_pin_event, LV_EVENT_CLICKED, this);
        }
        
        // PIN status label
        lbl_pin_status = lv_label_create(list_cont);
        if (settings::isPinEnabled()) {
            lv_label_set_text(lbl_pin_status, "Code active");
            lv_obj_set_style_text_color(lbl_pin_status, lv_color_hex(0x4CD964), 0);
        } else {
            lv_label_set_text(lbl_pin_status, "Aucun code defini");
            lv_obj_set_style_text_color(lbl_pin_status, lv_color_hex(0x8E8E93), 0);
        }
        lv_obj_set_style_text_font(lbl_pin_status, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_left(lbl_pin_status, 15, 0);
        
        // Délai verrouillage
        lv_obj_t* row_timeout = createSettingRow(list_cont, "Verrouillage auto");
        lv_obj_t* dd = lv_dropdown_create(row_timeout);
        lv_dropdown_set_options(dd, "Jamais\n15 sec\n30 sec\n1 min\n2 min\n5 min");
        lv_obj_set_size(dd, 100, 35);
        lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_font(dd, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(dd, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(dd, lv_color_white(), 0);
        lv_obj_set_style_border_width(dd, 0, 0);
        
        // Set current selection
        uint32_t timeout = settings::getLockTimeout();
        uint16_t sel = 2; // 30s par défaut
        if (timeout == 0) sel = 0;
        else if (timeout <= 15000) sel = 1;
        else if (timeout <= 30000) sel = 2;
        else if (timeout <= 60000) sel = 3;
        else if (timeout <= 120000) sel = 4;
        else sel = 5;
        lv_dropdown_set_selected(dd, sel);
        lv_obj_add_event_cb(dd, timeout_event, LV_EVENT_VALUE_CHANGED, NULL);
        
        // ===== SECTION: ECRAN =====
        createSection(list_cont, "ECRAN");
        
        lv_obj_t* row_br = createSettingRow(list_cont, "Luminosite");
        lv_obj_t* slider = lv_slider_create(row_br);
        lv_obj_set_size(slider, 120, 14);
        lv_obj_align(slider, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_slider_set_range(slider, 0, 100);
        
        uint8_t br = settings::getBrightness();
        int br_pct = ((int)br - 5) * 100 / 250;
        if (br_pct < 0) br_pct = 0;
        if (br_pct > 100) br_pct = 100;
        lv_slider_set_value(slider, br_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x3a3a3c), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x007AFF), LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_outline_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
        lv_obj_add_event_cb(slider, brightness_event, LV_EVENT_VALUE_CHANGED, NULL);
        
        // ===== SECTION: SYSTEME =====
        createSection(list_cont, "SYSTEME");
        
        // Sync time auto
        lv_obj_t* row_sync_time = createSettingRow(list_cont, "Synchro heure auto");
        lv_obj_t* chevron_sync = lv_label_create(row_sync_time);
        lv_label_set_text(chevron_sync, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(chevron_sync, lv_color_hex(0x007AFF), 0);
        lv_obj_align(chevron_sync, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_sync_time, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_sync_time, sync_time_auto_event, LV_EVENT_CLICKED, this);
        
        // Manual time setting
        lv_obj_t* row_manual_time = createSettingRow(list_cont, "Regler l'heure");
        lv_obj_t* chevron_manual = lv_label_create(row_manual_time);
        lv_label_set_text(chevron_manual, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron_manual, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(chevron_manual, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_manual_time, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_manual_time, show_time_panel_event, LV_EVENT_CLICKED, this);
        
        // Time status label
        lbl_time_status = lv_label_create(list_cont);
        time_t now;
        time(&now);
        struct tm* t = localtime(&now);
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d - %02d/%02d/%04d", 
                 t->tm_hour, t->tm_min, t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
        lv_label_set_text(lbl_time_status, timeBuf);
        lv_obj_set_style_text_color(lbl_time_status, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(lbl_time_status, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_left(lbl_time_status, 15, 0);
        
        
        
        // ===== SECTION: INFO =====
        createSection(list_cont, "INFORMATIONS");


        lv_obj_t* row_bl = createSettingRow(list_cont, "Bootloader");

        
        lv_obj_t* chevron_bl = lv_label_create(row_bl);
        lv_label_set_text(chevron_bl, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron_bl, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(chevron_bl, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_bl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_bl, bootloader_event, LV_EVENT_CLICKED, this);

        // Reboot button
        lv_obj_t* row_reboot = createSettingRow(list_cont, "Redemarrer");
        lv_obj_t* chevron_reboot = lv_label_create(row_reboot);
        lv_label_set_text(chevron_reboot, LV_SYMBOL_POWER);
        lv_obj_set_style_text_color(chevron_reboot, lv_color_hex(0xFF3B30), 0);
        lv_obj_align(chevron_reboot, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_reboot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_reboot, reboot_event, LV_EVENT_CLICKED, this);
        
        lv_obj_t* row_ver = createSettingRow(list_cont, "Version");
        lv_obj_t* lbl_ver = lv_label_create(row_ver);
        lv_label_set_text(lbl_ver, COMMIT_HASH);
        lv_obj_set_style_text_color(lbl_ver, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(lbl_ver, LV_ALIGN_RIGHT_MID, 0, 0);
        
        lv_obj_t* row_mem = createSettingRow(list_cont, "RAM libre");
        lv_obj_t* lbl_mem = lv_label_create(row_mem);
        char memBuf[16];
        snprintf(memBuf, sizeof(memBuf), "%u Ko", (unsigned)(rp2040.getFreeHeap() / 1024));
        lv_label_set_text(lbl_mem, memBuf);
        lv_obj_set_style_text_color(lbl_mem, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(lbl_mem, LV_ALIGN_RIGHT_MID, 0, 0);
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        
        // --- HEADER ---
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_home, LV_EVENT_CLICKED, NULL);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_hex(0x007AFF), 0);
        lv_obj_center(l_back);
        
        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Parametres");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_center(title);
        
        // --- LISTE SCROLLABLE ---
        list_cont = lv_obj_create(main_bg);
        lv_obj_set_size(list_cont, 320, 430);
        lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(list_cont, 8, 0);
        lv_obj_set_style_pad_all(list_cont, 5, 0);
        
        buildSettingsList();
        
        // --- PIN PANEL (overlay plein écran) ---
        pin_panel = lv_obj_create(main_bg);
        lv_obj_set_size(pin_panel, 320, 480);
        lv_obj_center(pin_panel);
        lv_obj_set_style_bg_color(pin_panel, lv_color_hex(0x1c1c1e), 0);
        lv_obj_add_flag(pin_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pin_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        pin_panel_title = lv_label_create(pin_panel);
        lv_label_set_text(pin_panel_title, "Definir le code PIN");
        lv_obj_set_style_text_color(pin_panel_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(pin_panel_title, &lv_font_montserrat_14, 0);
        lv_obj_align(pin_panel_title, LV_ALIGN_TOP_MID, 0, 10);
        
        // Reuse lbl_pin_status if needed (already created in list but may be hidden)
        // Create a new one for the pin panel
        lv_obj_t* pin_hint = lv_label_create(pin_panel);
        lv_label_set_text(pin_hint, "4 a 6 chiffres");
        lv_obj_set_style_text_color(pin_hint, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(pin_hint, &lv_font_montserrat_12, 0);
        lv_obj_align(pin_hint, LV_ALIGN_TOP_MID, 0, 35);
        
        ta_pin = lv_textarea_create(pin_panel);
        lv_obj_set_size(ta_pin, 180, 45);
        lv_obj_align(ta_pin, LV_ALIGN_TOP_MID, 0, 60);
        lv_textarea_set_max_length(ta_pin, 6);
        lv_textarea_set_one_line(ta_pin, true);
        lv_textarea_set_password_mode(ta_pin, true);
        lv_obj_set_style_text_font(ta_pin, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_align(ta_pin, LV_TEXT_ALIGN_CENTER, 0);
        
        kb_pin = lv_keyboard_create(pin_panel);
        lv_keyboard_set_mode(kb_pin, LV_KEYBOARD_MODE_NUMBER);
        lv_keyboard_set_textarea(kb_pin, ta_pin);
        lv_obj_align(kb_pin, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(kb_pin, pin_kb_ready, LV_EVENT_READY, this);
        lv_obj_add_event_cb(kb_pin, pin_kb_cancel, LV_EVENT_CANCEL, this);
        
        // --- TIME PANEL (overlay plein écran) ---
        time_panel = lv_obj_create(main_bg);
        lv_obj_set_size(time_panel, 320, 480);
        lv_obj_center(time_panel);
        lv_obj_set_style_bg_color(time_panel, lv_color_hex(0x1c1c1e), 0);
        lv_obj_add_flag(time_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(time_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        time_panel_title = lv_label_create(time_panel);
        lv_label_set_text(time_panel_title, "Regler l'heure");
        lv_obj_set_style_text_color(time_panel_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(time_panel_title, &lv_font_montserrat_14, 0);
        lv_obj_align(time_panel_title, LV_ALIGN_TOP_MID, 0, 10);
        
        // Time section
        lv_obj_t* time_label = lv_label_create(time_panel);
        lv_label_set_text(time_label, "Heure");
        lv_obj_set_style_text_color(time_label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_12, 0);
        lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 20, 50);
        
        lv_obj_t* time_cont = lv_obj_create(time_panel);
        lv_obj_set_size(time_cont, 280, 80);
        lv_obj_align(time_cont, LV_ALIGN_TOP_MID, 0, 70);
        lv_obj_set_style_bg_color(time_cont, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_border_width(time_cont, 0, 0);
        lv_obj_set_flex_flow(time_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(time_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(time_cont, LV_OBJ_FLAG_SCROLLABLE);
        
        // Hour roller
        roller_hour = lv_roller_create(time_cont);
        lv_roller_set_options(roller_hour, 
            "00\\n01\\n02\\n03\\n04\\n05\\n06\\n07\\n08\\n09\\n10\\n11\\n"
            "12\\n13\\n14\\n15\\n16\\n17\\n18\\n19\\n20\\n21\\n22\\n23",
            LV_ROLLER_MODE_INFINITE);
        lv_obj_set_size(roller_hour, 60, 70);
        lv_obj_set_style_text_font(roller_hour, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(roller_hour, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_hour, lv_color_white(), LV_PART_SELECTED);
        
        lv_obj_t* colon = lv_label_create(time_cont);
        lv_label_set_text(colon, ":");
        lv_obj_set_style_text_color(colon, lv_color_white(), 0);
        lv_obj_set_style_text_font(colon, &lv_font_montserrat_28, 0);
        
        // Minute roller
        roller_minute = lv_roller_create(time_cont);
        char minute_opts[400];
        strcpy(minute_opts, "00");
        for (int i = 1; i < 60; i++) {
            char buf[6];
            snprintf(buf, sizeof(buf), "\\n%02d", i);
            strcat(minute_opts, buf);
        }
        lv_roller_set_options(roller_minute, minute_opts, LV_ROLLER_MODE_INFINITE);
        lv_obj_set_size(roller_minute, 60, 70);
        lv_obj_set_style_text_font(roller_minute, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(roller_minute, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_minute, lv_color_white(), LV_PART_SELECTED);
        
        // Date section
        lv_obj_t* date_label = lv_label_create(time_panel);
        lv_label_set_text(date_label, "Date");
        lv_obj_set_style_text_color(date_label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_12, 0);
        lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 20, 170);
        
        lv_obj_t* date_cont = lv_obj_create(time_panel);
        lv_obj_set_size(date_cont, 280, 80);
        lv_obj_align(date_cont, LV_ALIGN_TOP_MID, 0, 190);
        lv_obj_set_style_bg_color(date_cont, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_border_width(date_cont, 0, 0);
        lv_obj_set_flex_flow(date_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(date_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(date_cont, LV_OBJ_FLAG_SCROLLABLE);
        
        // Day roller
        roller_day = lv_roller_create(date_cont);
        char day_opts[200];
        strcpy(day_opts, "01");
        for (int i = 2; i <= 31; i++) {
            char buf[6];
            snprintf(buf, sizeof(buf), "\\n%02d", i);
            strcat(day_opts, buf);
        }
        lv_roller_set_options(roller_day, day_opts, LV_ROLLER_MODE_NORMAL);
        lv_obj_set_size(roller_day, 50, 70);
        lv_obj_set_style_text_font(roller_day, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(roller_day, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_day, lv_color_white(), LV_PART_SELECTED);
        
        lv_obj_t* slash1 = lv_label_create(date_cont);
        lv_label_set_text(slash1, "/");
        lv_obj_set_style_text_color(slash1, lv_color_white(), 0);
        
        // Month roller
        roller_month = lv_roller_create(date_cont);
        lv_roller_set_options(roller_month, 
            "01\\n02\\n03\\n04\\n05\\n06\\n07\\n08\\n09\\n10\\n11\\n12",
            LV_ROLLER_MODE_NORMAL);
        lv_obj_set_size(roller_month, 50, 70);
        lv_obj_set_style_text_font(roller_month, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(roller_month, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_month, lv_color_white(), LV_PART_SELECTED);
        
        lv_obj_t* slash2 = lv_label_create(date_cont);
        lv_label_set_text(slash2, "/");
        lv_obj_set_style_text_color(slash2, lv_color_white(), 0);
        
        // Year roller
        roller_year = lv_roller_create(date_cont);
        lv_roller_set_options(roller_year, 
            "2024\\n2025\\n2026\\n2027\\n2028\\n2029\\n2030\\n2031\\n2032\\n2033\\n2034\\n2035",
            LV_ROLLER_MODE_NORMAL);
        lv_obj_set_size(roller_year, 70, 70);
        lv_obj_set_style_text_font(roller_year, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(roller_year, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_year, lv_color_white(), LV_PART_SELECTED);
        
        // Buttons
        lv_obj_t* btn_save = lv_btn_create(time_panel);
        lv_obj_set_size(btn_save, 130, 45);
        lv_obj_align(btn_save, LV_ALIGN_BOTTOM_LEFT, 20, -20);
        lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x007AFF), 0);
        lv_obj_add_event_cb(btn_save, time_panel_save, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_save = lv_label_create(btn_save);
        lv_label_set_text(lbl_save, "Enregistrer");
        lv_obj_center(lbl_save);
        
        lv_obj_t* btn_cancel = lv_btn_create(time_panel);
        lv_obj_set_size(btn_cancel, 130, 45);
        lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
        lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x3a3a3c), 0);
        lv_obj_add_event_cb(btn_cancel, time_panel_cancel, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
        lv_label_set_text(lbl_cancel, "Annuler");
        lv_obj_center(lbl_cancel);
    }
};

#endif
