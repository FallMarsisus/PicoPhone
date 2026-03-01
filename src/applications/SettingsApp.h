#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include "App.h"
#include "AppManager.h"
#include "../system/Settings.h"
#include <WiFi.h>

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
        rp2040.rebootToBootloader();
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
        
        // ===== SECTION: INFO =====
        createSection(list_cont, "INFORMATIONS");


        lv_obj_t* row_bl = createSettingRow(list_cont, "Bootloader");
        lv_obj_t* chevron_bl = lv_label_create(row_bl);
        lv_label_set_text(chevron_bl, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron_bl, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(chevron_bl, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_bl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_bl, bootloader_event, LV_EVENT_CLICKED, this);
        
        lv_obj_t* row_ver = createSettingRow(list_cont, "Version");
        lv_obj_t* lbl_ver = lv_label_create(row_ver);
        lv_label_set_text(lbl_ver, "1.0.1");
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
    }
};

#endif
