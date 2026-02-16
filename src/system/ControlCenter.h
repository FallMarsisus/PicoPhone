#ifndef CONTROL_CENTER_H
#define CONTROL_CENTER_H

#include <lvgl.h>
#include <Arduino.h>
#include <WiFi.h>
#include "Battery.h"
#include "Settings.h"
#include "../Hardware.h"

class ControlCenter {
private:
    lv_obj_t* layer = nullptr;
    lv_obj_t* overlay = nullptr;
    lv_obj_t* panel = nullptr;

    lv_obj_t* lbl_brightness_val = nullptr;
    lv_obj_t* slider_brightness = nullptr;
    lv_obj_t* lbl_volume_val = nullptr;
    lv_obj_t* slider_volume = nullptr;
    lv_obj_t* btn_wifi = nullptr;
    lv_obj_t* lbl_wifi_status = nullptr;
    lv_obj_t* btn_son = nullptr;
    lv_obj_t* lbl_battery = nullptr;
    lv_obj_t* lbl_ip = nullptr;

    bool is_open = false;
    bool ui_created = false;
    int last_preview_volume = -1;
    int last_preview_brightness = -1;

    static void brightness_event(lv_event_t* e) {
        ControlCenter* self = (ControlCenter*)lv_event_get_user_data(e);
        lv_obj_t* slider = lv_event_get_target(e);
        int val = lv_slider_get_value(slider);

        uint8_t hw_val = (uint8_t)(5 + (val * 250 / 100));
        settings::setBrightness(hw_val);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(self->lbl_brightness_val, buf);

        if (self->last_preview_brightness != val) {
            self->last_preview_brightness = val;
            float gain = (float)settings::getVolume() / 100.0f;
            if (gain < 0.05f) gain = 0.05f;
            i2s_play_test_tone(400 + val * 6, 10, gain);
        }
    }

    static void volume_event(lv_event_t* e) {
        ControlCenter* self = (ControlCenter*)lv_event_get_user_data(e);
        lv_obj_t* slider = lv_event_get_target(e);
        int val = lv_slider_get_value(slider);

        settings::setVolume((uint8_t)val);

        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(self->lbl_volume_val, buf);

        if (self->last_preview_volume != val) {
            self->last_preview_volume = val;
            float gain = (float)val / 100.0f;
            if (gain < 0.03f) gain = 0.03f;
            i2s_play_test_tone(350 + val * 7, 12, gain);
        }
    }

    static void wifi_toggle_event(lv_event_t* e) {
        ControlCenter* self = (ControlCenter*)lv_event_get_user_data(e);
        bool enabled = settings::isWifiEnabled();

        if (enabled) {
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
            settings::setWifiEnabled(false);
            lv_obj_set_style_bg_color(self->btn_wifi, lv_color_hex(0x555555), 0);
            lv_label_set_text(self->lbl_wifi_status, "Desactive");
        } else {
            WiFi.mode(WIFI_STA);
            settings::setWifiEnabled(true);
            lv_obj_set_style_bg_color(self->btn_wifi, lv_color_hex(0x007AFF), 0);
            lv_label_set_text(self->lbl_wifi_status, "Active");
        }
    }

    static void son_toggle_event(lv_event_t* e) {
        ControlCenter* self = (ControlCenter*)lv_event_get_user_data(e);
        uint8_t vol = settings::getVolume();
        if (vol > 0) {
            settings::setVolume(0);
            lv_obj_set_style_bg_color(self->btn_son, lv_color_hex(0x555555), 0);
        } else {
            settings::setVolume(50);
            lv_obj_set_style_bg_color(self->btn_son, lv_color_hex(0xFF9500), 0);
        }
    }

    static void close_event(lv_event_t* e) {
        ControlCenter* self = (ControlCenter*)lv_event_get_user_data(e);
        self->close();
    }

    lv_obj_t* createToggleBtn(lv_obj_t* parent, const char* icon, const char* label_text, uint32_t color, lv_event_cb_t cb) {
        lv_obj_t* cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 85, 85);
        lv_obj_set_style_bg_color(cont, lv_color_hex(color), 0);
        lv_obj_set_style_radius(cont, 18, 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(cont, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(cont, cb, LV_EVENT_CLICKED, this);

        lv_obj_t* l_icon = lv_label_create(cont);
        lv_label_set_text(l_icon, icon);
        lv_obj_set_style_text_color(l_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_28, 0);
        lv_obj_align(l_icon, LV_ALIGN_CENTER, 0, -8);

        lv_obj_t* l_name = lv_label_create(cont);
        lv_label_set_text(l_name, label_text);
        lv_obj_set_style_text_color(l_name, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_name, &lv_font_montserrat_12, 0);
        lv_obj_align(l_name, LV_ALIGN_BOTTOM_MID, 0, -5);

        return cont;
    }

    // Dans src/system/ControlCenter.h

lv_obj_t* createSliderRow(lv_obj_t* parent, const char* icon, int min_val, int max_val, int init_val, lv_event_cb_t cb, lv_obj_t** val_label) {
    lv_obj_t* row = lv_obj_create(parent);
    // On augmente un peu la hauteur du conteneur pour accommoder le slider plus gros
    lv_obj_set_size(row, 280, 60); 
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);


    lv_obj_t* slider = lv_slider_create(row);
    // CHANGEMENT MAJEUR ICI : Hauteur passée de 16 à 28 (plus épais style iOS)
    lv_obj_set_size(slider, 220, 45); 
    lv_obj_align(slider, LV_ALIGN_CENTER, -30, -5);
    lv_slider_set_range(slider, min_val, max_val);
    lv_slider_set_value(slider, init_val, LV_ANIM_OFF);
    
    // Style de la barre de fond (grise)
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x3a3a3c), LV_PART_MAIN);
    // Assure que le rayon est suffisant pour faire une "pilule" parfaite
    lv_obj_set_style_radius(slider, 15, LV_PART_MAIN); 
    
    // Style de la partie active (bleue)
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x007AFF), LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 15, LV_PART_INDICATOR);
    
    // Suppression complète du "bouton" (knob) visible pour faire comme iOS
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB); // Pas de marge pour le bouton
    
    // IMPORTANT : Enlever le padding interne du slider pour que la couleur remplisse tout
    lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);

    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, this);

    lv_obj_t* l_icon = lv_label_create(row);
    lv_label_set_text(l_icon, icon);
    lv_obj_set_style_text_color(l_icon, lv_color_white(), 0);
    lv_obj_align(l_icon, LV_ALIGN_CENTER, -109, -5);

    *val_label = lv_label_create(row);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", init_val);
    lv_label_set_text(*val_label, buf);
    lv_obj_set_style_text_color(*val_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(*val_label, &lv_font_montserrat_12, 0);
    lv_obj_align(*val_label, LV_ALIGN_RIGHT_MID, 0, -5);

    return slider;
}

    void createUI() {
        if (ui_created) return;
        ui_created = true;

        layer = lv_layer_top();

        overlay = lv_obj_create(layer);
        lv_obj_set_size(overlay, 320, 480);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_set_style_radius(overlay, 0, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overlay, close_event, LV_EVENT_CLICKED, this);

        panel = lv_obj_create(layer);
        lv_obj_set_size(panel, 300, 380);
        lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(panel, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
        lv_obj_set_style_radius(panel, 25, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_shadow_width(panel, 20, 0);
        lv_obj_set_style_shadow_color(panel, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(panel, LV_OPA_60, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* title = lv_label_create(panel);
        lv_label_set_text(title, "Centre de controle");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

        lv_obj_t* handle = lv_obj_create(panel);
        lv_obj_set_size(handle, 40, 4);
        lv_obj_set_style_bg_color(handle, lv_color_hex(0x666666), 0);
        lv_obj_set_style_radius(handle, 2, 0);
        lv_obj_set_style_border_width(handle, 0, 0);
        lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, -2);

        lv_obj_t* toggles_row = lv_obj_create(panel);
        lv_obj_set_size(toggles_row, 280, 95);
        lv_obj_align(toggles_row, LV_ALIGN_TOP_MID, 0, 30);
        lv_obj_set_style_bg_opa(toggles_row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(toggles_row, 0, 0);
        lv_obj_clear_flag(toggles_row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(toggles_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(toggles_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        bool wifi_on = settings::isWifiEnabled();
        btn_wifi = createToggleBtn(toggles_row, LV_SYMBOL_WIFI, "WiFi", wifi_on ? 0x007AFF : 0x555555, wifi_toggle_event);

        uint8_t cur_vol = settings::getVolume();
        btn_son = createToggleBtn(toggles_row, LV_SYMBOL_BELL, "Son", cur_vol > 0 ? 0xFF9500 : 0x555555, son_toggle_event);

        createToggleBtn(toggles_row, LV_SYMBOL_POWER, "Veille", 0x555555, close_event);

        lbl_wifi_status = lv_label_create(panel);
        lv_label_set_text(lbl_wifi_status, "WiFi: --");
        lv_obj_set_style_text_color(lbl_wifi_status, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl_wifi_status, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_wifi_status, LV_ALIGN_TOP_MID, 0, 135);

        lv_obj_t* lbl_br = lv_label_create(panel);
        lv_label_set_text(lbl_br, "Luminosite");
        lv_obj_set_style_text_color(lbl_br, lv_color_hex(0xBBBBBB), 0);
        lv_obj_set_style_text_font(lbl_br, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_br, LV_ALIGN_TOP_LEFT, 15, 160);

        uint8_t br = settings::getBrightness();
        int br_pct = ((int)br - 5) * 100 / 250;
        if (br_pct < 0) br_pct = 0;
        if (br_pct > 100) br_pct = 100;

        slider_brightness = createSliderRow(panel, LV_SYMBOL_CHARGE, 0, 100, br_pct, brightness_event, &lbl_brightness_val);
        lv_obj_align(lv_obj_get_parent(slider_brightness), LV_ALIGN_TOP_LEFT, 10, 180);

        lv_obj_t* lbl_vol = lv_label_create(panel);
        lv_label_set_text(lbl_vol, "Volume");
        lv_obj_set_style_text_color(lbl_vol, lv_color_hex(0xBBBBBB), 0);
        lv_obj_set_style_text_font(lbl_vol, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_vol, LV_ALIGN_TOP_LEFT, 15, 235);

        slider_volume = createSliderRow(panel, LV_SYMBOL_AUDIO, 0, 100, settings::getVolume(), volume_event, &lbl_volume_val);
        lv_obj_align(lv_obj_get_parent(slider_volume), LV_ALIGN_TOP_LEFT, 10, 255);

        lbl_battery = lv_label_create(panel);
        lv_label_set_text(lbl_battery, "Batterie: --");
        lv_obj_set_style_text_color(lbl_battery, lv_color_hex(0xBBBBBB), 0);
        lv_obj_set_style_text_font(lbl_battery, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_battery, LV_ALIGN_TOP_LEFT, 15, 310);

        lbl_ip = lv_label_create(panel);
        lv_label_set_text(lbl_ip, "IP: --");
        lv_obj_set_style_text_color(lbl_ip, lv_color_hex(0xBBBBBB), 0);
        lv_obj_set_style_text_font(lbl_ip, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_ip, LV_ALIGN_TOP_LEFT, 15, 330);
    }

    void updateWifiStatusLabel() {
        if (!lbl_wifi_status) return;
        if (WiFi.status() == WL_CONNECTED) {
            String ssid = WiFi.SSID();
            String txt = "WiFi: " + ssid;
            lv_label_set_text(lbl_wifi_status, txt.c_str());
        } else if (settings::isWifiEnabled()) {
            lv_label_set_text(lbl_wifi_status, "WiFi: Non connecte");
        } else {
            lv_label_set_text(lbl_wifi_status, "WiFi: Desactive");
        }
    }

public:
    void init() {
        // Init différée pour éviter le pic mémoire au boot.
    }

    void open() {
        if (is_open) return;

        createUI();
        if (!overlay || !panel || !slider_brightness || !slider_volume || !lbl_battery || !lbl_ip) return;

        is_open = true;

        updateWifiStatusLabel();

        uint8_t br = settings::getBrightness();
        int br_pct = ((int)br - 5) * 100 / 250;
        if (br_pct < 0) br_pct = 0;
        if (br_pct > 100) br_pct = 100;
        lv_slider_set_value(slider_brightness, br_pct, LV_ANIM_OFF);
        lv_slider_set_value(slider_volume, settings::getVolume(), LV_ANIM_OFF);

        const uint8_t p = battery::read_percent();
        char bbuf[24];
        snprintf(bbuf, sizeof(bbuf), "Batterie: %u%%", (unsigned)p);
        lv_label_set_text(lbl_battery, bbuf);

        if (WiFi.status() == WL_CONNECTED) {
            String ipStr = "IP: " + WiFi.localIP().toString();
            lv_label_set_text(lbl_ip, ipStr.c_str());
        } else {
            lv_label_set_text(lbl_ip, "IP: --");
        }

        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }

    void close() {
        if (!is_open) return;
        is_open = false;
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }

    void toggle() {
        if (is_open) close();
        else open();
    }

    bool isOpen() const { return is_open; }

    bool checkSwipeDown(lv_coord_t start_y, lv_coord_t end_y) {
        if (start_y < 40 && (end_y - start_y) > 60) {
            open();
            return true;
        }
        return false;
    }
};

#endif
