#ifndef CONTROL_CENTER_H
#define CONTROL_CENTER_H

#include <lvgl.h>
#include <Arduino.h>
#include "LTE.h"
#include <WiFi.h>
#include "Battery.h"
#include "Settings.h"
#include "../Hardware.h"
#include "NotificationCenter.h"

class ControlCenter
{
private:
    lv_obj_t *layer = nullptr;
    lv_obj_t *overlay = nullptr;

    // Nouveaux panneaux basés sur ton design Figma
    lv_obj_t *panel_top = nullptr;
    lv_obj_t *panel_notif1 = nullptr;
    lv_obj_t *panel_notif2 = nullptr;

    lv_obj_t *lbl_brightness_val = nullptr;
    lv_obj_t *slider_brightness = nullptr;
    lv_obj_t *lbl_volume_val = nullptr;
    lv_obj_t *slider_volume = nullptr;

    lv_obj_t *btn_wifi     = nullptr;
    lv_obj_t *btn_son      = nullptr;
    lv_obj_t *btn_4g       = nullptr;
    lv_obj_t *btn_airplane = nullptr; // Mode avion

    // Labels Notif 1
    lv_obj_t *lbl_n1_app = nullptr;
    lv_obj_t *lbl_n1_title = nullptr;
    lv_obj_t *lbl_n1_body = nullptr;

    // Labels Notif 2
    lv_obj_t *lbl_n2_app = nullptr;
    lv_obj_t *lbl_n2_title = nullptr;
    lv_obj_t *lbl_n2_body = nullptr;

    bool is_open = false;
    bool ui_created = false;
    int last_preview_volume = -1;
    int last_preview_brightness = -1;

    // Fonction utilitaire pour gérer l'état (actif/inactif) des boutons ronds
    static void set_btn_state(lv_obj_t *btn, bool active, uint32_t active_color)
    {
        if (active)
        {
            lv_obj_set_style_bg_color(btn, lv_color_hex(active_color), 0);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl)
                lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        }
        else
        {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl)
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x323232), 0);
        }
    }

    static void brightness_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        lv_obj_t *slider = lv_event_get_target(e);
        int val = lv_slider_get_value(slider);

        uint8_t hw_val = (uint8_t)(5 + (val * 250 / 100));
        settings::setBrightness(hw_val);

        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(self->lbl_brightness_val, buf);

        if (self->last_preview_brightness != val)
        {
            self->last_preview_brightness = val;
            float gain = (float)settings::getVolume() / 100.0f;
            if (gain < 0.05f)
                gain = 0.05f;
            i2s_play_test_tone(400 + val * 6, 10, gain);
        }
    }

    static void volume_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        lv_obj_t *slider = lv_event_get_target(e);
        int val = lv_slider_get_value(slider);

        settings::setVolume((uint8_t)val);

        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(self->lbl_volume_val, buf);

        if (self->last_preview_volume != val)
        {
            self->last_preview_volume = val;
            float gain = (float)val / 100.0f;
            if (gain < 0.03f)
                gain = 0.03f;
            i2s_play_test_tone(350 + val * 7, 12, gain);
        }
    }

    static void wifi_toggle_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        bool enabled = settings::isWifiEnabled();

        if (enabled)
        {
            WiFi.disconnect();
            WiFi.mode(WIFI_OFF);
            settings::setWifiEnabled(false);
            set_btn_state(self->btn_wifi, false, 0x007AFF);
        }
        else
        {
            WiFi.mode(WIFI_STA);
            settings::setWifiEnabled(true);
            set_btn_state(self->btn_wifi, true, 0x007AFF);
        }
    }

    static void lte_toggle_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        bool enabled = LTE::isEnabled();
        LTE::enable(!enabled); // bascule UNIQUEMENT les données, pas la radio
        set_btn_state(self->btn_4g, !enabled, 0x34C759);
    }

    static void airplane_toggle_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        bool airplane = LTE::isAirplaneMode();
        LTE::setAirplaneMode(!airplane);
        set_btn_state(self->btn_airplane, !airplane, 0xFF3B30); // Rouge mode avion
        // Met aussi à jour l'état du bouton 4G
        set_btn_state(self->btn_4g, LTE::isEnabled(), 0x34C759);
    }

    static void son_toggle_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        uint8_t vol = settings::getVolume();
        if (vol > 0)
        {
            settings::setVolume(0);
            set_btn_state(self->btn_son, false, 0xFF9500);
            lv_slider_set_value(self->slider_volume, 0, LV_ANIM_OFF);
            lv_label_set_text(self->lbl_volume_val, "0%");
        }
        else
        {
            settings::setVolume(50);
            set_btn_state(self->btn_son, true, 0xFF9500);
            lv_slider_set_value(self->slider_volume, 50, LV_ANIM_OFF);
            lv_label_set_text(self->lbl_volume_val, "50%");
        }
    }

    static void close_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        self->close();
    }

    // Boutons ronds basés sur le design Figma
    lv_obj_t *createRoundBtn(lv_obj_t *parent, const char *icon, lv_event_cb_t cb, int x, int y, bool is_active, uint32_t active_color)
    {
        lv_obj_t *btn = lv_obj_create(parent);
        lv_obj_set_size(btn, 60, 60);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);

        lv_obj_t *l_icon = lv_label_create(btn);
        lv_label_set_text(l_icon, icon);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_18, 0);
        lv_obj_align(l_icon, LV_ALIGN_CENTER, 0, 0);

        set_btn_state(btn, is_active, active_color);

        return btn;
    }

    // Sliders horizontaux basés sur ton Figma (260x60)
    lv_obj_t *createHorizontalSlider(lv_obj_t *parent, const char *icon, int min_val, int max_val, int init_val, lv_event_cb_t cb, lv_obj_t **val_label, int x, int y)
    {
        lv_obj_t *slider = lv_slider_create(parent);
        lv_obj_set_size(slider, 260, 60);
        lv_obj_set_pos(slider, x, y);
        lv_slider_set_range(slider, min_val, max_val);
        lv_slider_set_value(slider, init_val, LV_ANIM_OFF);

        // Fond (blanc pur d'après le SVG)
        lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);

        // Indicateur actif (#888888 gris d'après le SVG)
        lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, 10, LV_PART_INDICATOR);

        // Bouton invisible
        lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);

        lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, this);

        // Icône intégrée à gauche
        lv_obj_t *l_icon = lv_label_create(slider);
        lv_label_set_text(l_icon, icon);
        lv_obj_set_style_text_color(l_icon, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_18, 0);
        lv_obj_align(l_icon, LV_ALIGN_LEFT_MID, 15, 0);

        // Valeur intégrée à droite
        *val_label = lv_label_create(slider);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", init_val);
        lv_label_set_text(*val_label, buf);
        lv_obj_set_style_text_color(*val_label, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(*val_label, &lv_font_montserrat_14, 0);
        lv_obj_align(*val_label, LV_ALIGN_RIGHT_MID, -15, 0);

        return slider;
    }

    void createUI()
    {
        if (ui_created)
            return;
        ui_created = true;

        layer = lv_layer_top();

        // 1. Overlay (assombrit l'écran)
        overlay = lv_obj_create(layer);
        lv_obj_set_size(overlay, 320, 480);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_set_style_radius(overlay, 0, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(overlay, close_event, LV_EVENT_CLICKED, this);

        // 2. Panneau Principal (Haut) -> 300x255, fond #D9D9D9 80%
        panel_top = lv_obj_create(layer);
        lv_obj_set_size(panel_top, 300, 255);
        lv_obj_align(panel_top, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(panel_top, lv_color_hex(0xD9D9D9), 0);
        lv_obj_set_style_bg_opa(panel_top, LV_OPA_90, 0);
        lv_obj_set_style_radius(panel_top, 10, 0);
        lv_obj_set_style_border_width(panel_top, 0, 0);
        lv_obj_clear_flag(panel_top, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(panel_top, LV_OBJ_FLAG_HIDDEN);

        // 4 boutons ronds
        bool wifi_on = settings::isWifiEnabled();
        uint8_t cur_vol = settings::getVolume();

        btn_wifi     = createRoundBtn(panel_top, LV_SYMBOL_WIFI,  wifi_toggle_event,     3, 8, wifi_on,              0x007AFF);
        btn_son      = createRoundBtn(panel_top, LV_SYMBOL_BELL,  son_toggle_event,      73, 8, cur_vol > 0,         0xFF9500);
        btn_4g       = createRoundBtn(panel_top, LV_SYMBOL_CALL,  lte_toggle_event,      143, 8, LTE::isEnabled(),   0x34C759);
        btn_airplane = createRoundBtn(panel_top, "\xE2\x9C\x88", airplane_toggle_event, 213, 8, LTE::isAirplaneMode(), 0xFF3B30);

        // Ligne de séparation
        lv_obj_t *sep = lv_obj_create(panel_top);
        lv_obj_set_size(sep, 240, 2);
        lv_obj_set_pos(sep, 30, 84);
        lv_obj_set_style_bg_color(sep, lv_color_hex(0x888888), 0);
        lv_obj_set_style_border_width(sep, 0, 0);

        // Sliders horizontaux
        uint8_t br = settings::getBrightness();
        int br_pct = ((int)br - 5) * 100 / 250;
        if (br_pct < 0)
            br_pct = 0;
        if (br_pct > 100)
            br_pct = 100;

        slider_brightness = createHorizontalSlider(panel_top, LV_SYMBOL_CHARGE, 0, 100, br_pct, brightness_event, &lbl_brightness_val, 7, 98);
        slider_volume = createHorizontalSlider(panel_top, LV_SYMBOL_AUDIO, 0, 100, cur_vol, volume_event, &lbl_volume_val, 7, 168 );

        // 3. Panneau Notif 1 (Milieu) -> Y = 274
        panel_notif1 = lv_obj_create(layer);
        lv_obj_set_size(panel_notif1, 300, 80);
        lv_obj_align(panel_notif1, LV_ALIGN_TOP_MID, 0, 274);
        lv_obj_set_style_bg_color(panel_notif1, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(panel_notif1, LV_OPA_80, 0);
        lv_obj_set_style_radius(panel_notif1, 10, 0);
        lv_obj_set_style_border_width(panel_notif1, 0, 0);
        lv_obj_clear_flag(panel_notif1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(panel_notif1, LV_OBJ_FLAG_HIDDEN);

        lbl_n1_app = lv_label_create(panel_notif1);
        lv_obj_set_style_text_color(lbl_n1_app, lv_color_hex(0x007AFF), 0);
        lv_obj_set_style_text_font(lbl_n1_app, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_n1_app, LV_ALIGN_TOP_LEFT, 5, 2);

        lbl_n1_title = lv_label_create(panel_notif1);
        lv_obj_set_style_text_color(lbl_n1_title, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(lbl_n1_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_n1_title, LV_ALIGN_TOP_LEFT, 5, 22);

        lbl_n1_body = lv_label_create(panel_notif1);
        lv_obj_set_width(lbl_n1_body, 280);
        lv_label_set_long_mode(lbl_n1_body, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(lbl_n1_body, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_font(lbl_n1_body, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_n1_body, LV_ALIGN_TOP_LEFT, 4, 42);

        // 4. Panneau Notif 2 (Bas) -> Y = 364
        panel_notif2 = lv_obj_create(layer);
        lv_obj_set_size(panel_notif2, 300, 80);
        lv_obj_align(panel_notif2, LV_ALIGN_TOP_MID, 0, 364);
        lv_obj_set_style_bg_color(panel_notif2, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(panel_notif2, LV_OPA_80, 0);
        lv_obj_set_style_radius(panel_notif2, 10, 0);
        lv_obj_set_style_border_width(panel_notif2, 0, 0);
        lv_obj_clear_flag(panel_notif2, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(panel_notif2, LV_OBJ_FLAG_HIDDEN);

        lbl_n2_app = lv_label_create(panel_notif2);
        lv_obj_set_style_text_color(lbl_n2_app, lv_color_hex(0x007AFF), 0);
        lv_obj_set_style_text_font(lbl_n2_app, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_n2_app, LV_ALIGN_TOP_LEFT, 4, 2);

        lbl_n2_title = lv_label_create(panel_notif2);
        lv_obj_set_style_text_color(lbl_n2_title, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(lbl_n2_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_n2_title, LV_ALIGN_TOP_LEFT, 5, 22);

        lbl_n2_body = lv_label_create(panel_notif2);
        lv_obj_set_width(lbl_n2_body, 280);
        lv_label_set_long_mode(lbl_n2_body, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(lbl_n2_body, lv_color_hex(0x555555), 0);
        lv_obj_set_style_text_font(lbl_n2_body, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_n2_body, LV_ALIGN_TOP_LEFT, 5, 42);

        // 5. Handle / Poignée de fermeture en bas
        lv_obj_t *handle = lv_obj_create(overlay);
        lv_obj_set_size(handle, 36, 4);
        lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -10); // Y=457 in SVG
        lv_obj_set_style_bg_color(handle, lv_color_hex(0xA0A0A0), 0);
        lv_obj_set_style_radius(handle, 2, 0);
        lv_obj_set_style_border_width(handle, 0, 0);
    }

public:
    void init()
    {
    }

    void open()
    {
        if (is_open)
            return;

        createUI();
        if (!overlay || !panel_top || !panel_notif1 || !panel_notif2)
            return;

        is_open = true;

        // Rafraichir les sliders
        uint8_t br = settings::getBrightness();
        int br_pct = ((int)br - 5) * 100 / 250;
        if (br_pct < 0)
            br_pct = 0;
        if (br_pct > 100)
            br_pct = 100;

        lv_slider_set_value(slider_brightness, br_pct, LV_ANIM_OFF);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", br_pct);
        lv_label_set_text(lbl_brightness_val, buf);

        int vol = settings::getVolume();
        lv_slider_set_value(slider_volume, vol, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", vol);
        lv_label_set_text(lbl_volume_val, buf);

        // Lecture des Notifications depuis l'historique
        char n_app[20], n_title[36], n_body[96];

        // Notif 1 (La plus récente, index 0)
        if (notifications::center().get_latest(0, n_app, n_title, n_body))
        {
            lv_label_set_text(lbl_n1_app, n_app[0] ? n_app : "Systeme");
            lv_label_set_text(lbl_n1_title, n_title);
            lv_label_set_text(lbl_n1_body, n_body);
        }
        else
        {
            // État vide par défaut
            lv_label_set_text(lbl_n1_app, "Systeme");
            lv_label_set_text(lbl_n1_title, "Aucune notification");
            lv_label_set_text(lbl_n1_body, "Vous etes a jour.");
        }

        // Notif 2 (L'avant-dernière, index 1)
        if (notifications::center().get_latest(1, n_app, n_title, n_body))
        {
            lv_label_set_text(lbl_n2_app, n_app[0] ? n_app : "Systeme");
            lv_label_set_text(lbl_n2_title, n_title);
            lv_label_set_text(lbl_n2_body, n_body);
            lv_obj_clear_flag(panel_notif2, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            // S'il n'y en a pas 2, on masque purement et simplement le 2ème panneau
            lv_obj_add_flag(panel_notif2, LV_OBJ_FLAG_HIDDEN);
        }

        // Afficher
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(panel_top, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(panel_notif1, LV_OBJ_FLAG_HIDDEN);
    }

    void close()
    {
        if (!is_open)
            return;
        is_open = false;
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(panel_top, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(panel_notif1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(panel_notif2, LV_OBJ_FLAG_HIDDEN);
    }

    void toggle()
    {
        if (is_open)
            close();
        else
            open();
    }

    bool isOpen() const { return is_open; }

    bool checkSwipeDown(lv_coord_t start_y, lv_coord_t end_y)
    {
        if (start_y < 40 && (end_y - start_y) > 60)
        {
            open();
            return true;
        }
        return false;
    }
};

#endif