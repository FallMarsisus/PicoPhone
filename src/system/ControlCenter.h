#ifdef __cplusplus
extern "C" {
#endif
extern void i2s_play_test_tone(int freq, int duration_ms, float gain);
extern void hardware_set_volume(int vol);  
#ifdef __cplusplus
}
#endif

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
#include "../services/CastService.h" 

class ControlCenter
{
private:
    lv_obj_t *layer = nullptr;
    lv_obj_t *overlay = nullptr;
    
    // Le conteneur maître qui glisse
    lv_obj_t *main_sheet = nullptr; 

    lv_obj_t *panel_top = nullptr;

    // Le panneau de contrôle Musique (dynamique)
    lv_obj_t *panel_media = nullptr;
    lv_obj_t *lbl_media_title = nullptr;
    lv_obj_t *lbl_media_artist = nullptr;
    lv_obj_t *btn_media_prev = nullptr;
    lv_obj_t *btn_media_toggle = nullptr;
    lv_obj_t *btn_media_next = nullptr;

    // Les panneaux de notifications
    lv_obj_t *panel_notif1 = nullptr;
    lv_obj_t *panel_notif2 = nullptr;

    lv_obj_t *lbl_brightness_val = nullptr;
    lv_obj_t *slider_brightness = nullptr;
    lv_obj_t *lbl_volume_val = nullptr;
    lv_obj_t *slider_volume = nullptr;

    lv_obj_t *btn_eco      = nullptr;
    lv_obj_t *btn_son      = nullptr;
    lv_obj_t *btn_4g       = nullptr;
    lv_obj_t *btn_airplane = nullptr;

    // Labels Notif 1
    lv_obj_t *lbl_n1_app = nullptr;
    lv_obj_t *lbl_n1_title = nullptr;
    lv_obj_t *lbl_n1_body = nullptr;

    // Labels Notif 2
    lv_obj_t *lbl_n2_app = nullptr;
    lv_obj_t *lbl_n2_title = nullptr;
    lv_obj_t *lbl_n2_body = nullptr;
    lv_obj_t *handle = nullptr;

    bool is_open = false;
    bool ui_created = false;
    bool animating = false;
    int last_preview_volume = -1;
    int last_preview_brightness = -1;
    lv_coord_t sheet_offset_y = -450; 
    static constexpr lv_coord_t sheet_hidden_y = -450;
    static constexpr lv_opa_t overlay_hidden_opa = LV_OPA_0;
    static constexpr lv_opa_t overlay_visible_opa = LV_OPA_60;

    // --- ANIMATIONS ---
    lv_style_transition_dsc_t btn_trans;

    void apply_btn_style(lv_obj_t* btn) {
        lv_obj_set_style_translate_y(btn, 4, LV_STATE_PRESSED);
        lv_obj_set_style_transition(btn, &btn_trans, 0);
        lv_obj_set_style_transition(btn, &btn_trans, LV_STATE_PRESSED);
    }

    static void set_btn_state(lv_obj_t *btn, bool active, uint32_t active_color)
    {
        if (active)
        {
            lv_obj_set_style_bg_color(btn, lv_color_hex(active_color), 0);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl) lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        }
        else
        {
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
            lv_obj_t *lbl = lv_obj_get_child(btn, 0);
            if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(0x323232), 0);
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
            if (gain < 0.05f) gain = 0.05f;
            i2s_play_test_tone(400 + val * 6, 10, gain);
        }
    }

    static void volume_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        lv_obj_t *slider = lv_event_get_target(e);
        int val = lv_slider_get_value(slider);

        hardware_set_volume(val);
        settings::setVolume((uint8_t)val);

        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", val);
        lv_label_set_text(self->lbl_volume_val, buf);

        if (self->last_preview_volume != val)
        {
            self->last_preview_volume = val;
            float gain = (float)val / 100.0f;
            if (gain < 0.03f) gain = 0.03f;
            delayMicroseconds(20000);  
            i2s_play_test_tone(350 + val * 7, 140, gain);
        }
    }

    static void eco_toggle_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        const bool enabled = battery::toggle_manual_saver();
        set_btn_state(self->btn_eco, enabled, 0xF2C94C);
    }

    static void lte_toggle_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        bool enabled = LTE::isEnabled();
        LTE::enable(!enabled);
        set_btn_state(self->btn_4g, !enabled, 0x34C759);
    }

    static void airplane_toggle_event(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        bool airplane = LTE::isAirplaneMode();
        LTE::setAirplaneMode(!airplane);
        set_btn_state(self->btn_airplane, !airplane, 0xFF3B30);
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

    // Gère le clic sur l'arrière-plan ET le swipe vers le haut
    static void background_event_cb(lv_event_t *e)
    {
        ControlCenter *self = (ControlCenter *)lv_event_get_user_data(e);
        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_CLICKED) {
            self->close();
        } else if (code == LV_EVENT_GESTURE) {
            lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
            if (dir == LV_DIR_TOP) {
                self->close();
            }
        }
    }

    void applySheetOffset(lv_coord_t offset)
    {
        sheet_offset_y = offset;
        if (main_sheet) lv_obj_set_style_translate_y(main_sheet, offset, 0);
    }

    static void sheet_anim_exec(void *var, int32_t v)
    {
        ControlCenter *self = (ControlCenter *)var;
        if (self) self->applySheetOffset((lv_coord_t)v);
    }

    static void overlay_anim_exec(void *var, int32_t v)
    {
        ControlCenter *self = (ControlCenter *)var;
        if (!self || !self->overlay) return;
        lv_obj_set_style_bg_opa(self->overlay, (lv_opa_t)v, 0);
    }

    void animateOverlayTo(lv_opa_t target_opa, uint16_t duration)
    {
        if (!overlay) return;

        lv_anim_del(overlay, (lv_anim_exec_xcb_t)overlay_anim_exec);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, this);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)overlay_anim_exec);
        lv_anim_set_values(&a, lv_obj_get_style_bg_opa(overlay, 0), target_opa);
        lv_anim_set_time(&a, duration);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }

    static void sheet_anim_ready(lv_anim_t *a)
    {
        ControlCenter *self = (ControlCenter *)lv_anim_get_user_data(a);
        if (!self) return;

        self->animating = false;
        if (self->is_open) {
            self->animateOverlayTo(overlay_visible_opa, 150); 
        } else if (self->main_sheet) {
            lv_obj_add_flag(self->main_sheet, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(self->overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void animateSheetTo(lv_coord_t target_y)
    {
        if (!ui_created) return;

        animating = true;
        lv_anim_del(main_sheet, (lv_anim_exec_xcb_t)sheet_anim_exec);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, this);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)sheet_anim_exec);
        lv_anim_set_values(&a, sheet_offset_y, target_y);
        lv_anim_set_time(&a, 300); 
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_user_data(&a, this);
        lv_anim_set_ready_cb(&a, sheet_anim_ready);
        lv_anim_start(&a);
    }

    // --- CALLBACKS LECTEUR MULTIMEDIA ---
    static void media_prev_event(lv_event_t *e) { cast_service::instance().prev(); }
    static void media_next_event(lv_event_t *e) { cast_service::instance().next(); }
    static void media_toggle_event(lv_event_t *e) { 
        cast_service::instance().toggle(); 
        lv_obj_t *btn = lv_event_get_target(e);
        lv_obj_t *lbl = lv_obj_get_child(btn, 0);
        if (lbl) {
            if (strcmp(lv_label_get_text(lbl), LV_SYMBOL_PLAY) == 0) {
                lv_label_set_text(lbl, LV_SYMBOL_PAUSE);
            } else {
                lv_label_set_text(lbl, LV_SYMBOL_PLAY);
            }
        }
    }

    lv_obj_t *createRoundBtn(lv_obj_t *parent, const char *icon, lv_event_cb_t cb, int x, int y, bool is_active, uint32_t active_color)
    {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 60, 60);
        lv_obj_set_pos(btn, x, y);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0); 
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);

        apply_btn_style(btn);

        lv_obj_t *l_icon = lv_label_create(btn);
        lv_label_set_text(l_icon, icon);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_18, 0);
        lv_obj_align(l_icon, LV_ALIGN_CENTER, 0, 0);

        set_btn_state(btn, is_active, active_color);

        return btn;
    }

    lv_obj_t *createHorizontalSlider(lv_obj_t *parent, const char *icon, int min_val, int max_val, int init_val, lv_event_cb_t cb, lv_obj_t **val_label, int x, int y)
    {
        lv_obj_t *slider = lv_slider_create(parent);
        lv_obj_set_size(slider, 260, 60);
        lv_obj_set_pos(slider, x, y);
        lv_slider_set_range(slider, min_val, max_val);
        lv_slider_set_value(slider, init_val, LV_ANIM_OFF);

        lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_all(slider, 0, LV_PART_MAIN);

        lv_obj_set_style_bg_color(slider, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, 10, LV_PART_INDICATOR);

        lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);

        lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t *l_icon = lv_label_create(slider);
        lv_label_set_text(l_icon, icon);
        lv_obj_set_style_text_color(l_icon, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_18, 0);
        lv_obj_align(l_icon, LV_ALIGN_LEFT_MID, 15, 0);

        *val_label = lv_label_create(slider);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", init_val);
        lv_label_set_text(*val_label, buf);
        lv_obj_set_style_text_color(*val_label, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(*val_label, &lv_font_montserrat_14, 0);
        lv_obj_align(*val_label, LV_ALIGN_RIGHT_MID, -15, 0);

        return slider;
    }

    lv_obj_t *createMediaBtn(lv_obj_t *parent, const char *icon, lv_event_cb_t cb, int x, int y) 
    {
        lv_obj_t *btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 40, 40);
        lv_obj_align(btn, LV_ALIGN_LEFT_MID, x, y);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x007AFF), 0); 
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);

        apply_btn_style(btn); 

        lv_obj_t *l_icon = lv_label_create(btn);
        lv_label_set_text(l_icon, icon);
        lv_obj_set_style_text_color(l_icon, lv_color_white(), 0);
        lv_obj_align(l_icon, LV_ALIGN_CENTER, 0, 0);
        return btn;
    }

    void createUI()
    {
        if (ui_created) return;
        ui_created = true;

        layer = lv_layer_top();

        static const lv_style_prop_t props[] = {LV_STYLE_TRANSLATE_Y, (lv_style_prop_t)0};
        lv_style_transition_dsc_init(&btn_trans, props, lv_anim_path_ease_out, 100, 0, NULL);

        // 1. Overlay Noir Transparent
        overlay = lv_obj_create(layer);
        lv_obj_set_size(overlay, 320, 480);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, overlay_hidden_opa, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_set_style_radius(overlay, 0, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
        
        // On permet au fond noir d'accepter les clics et le balayage
        lv_obj_add_event_cb(overlay, background_event_cb, LV_EVENT_ALL, this);

        // 2. MASTER SHEET (Conteneur principal qui porte tout)
        main_sheet = lv_obj_create(layer);
        lv_obj_set_size(main_sheet, 320, 480);
        lv_obj_align(main_sheet, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(main_sheet, LV_OPA_TRANSP, 0); 
        lv_obj_set_style_border_width(main_sheet, 0, 0);
        lv_obj_set_style_pad_all(main_sheet, 0, 0);
        lv_obj_clear_flag(main_sheet, LV_OBJ_FLAG_SCROLLABLE);
        
        // --- LA CORRECTION MAGIQUE ---
        // Empêche le conteneur global d'avaler les clics ! 
        // Les clics dans le "vide" passent au travers et atteignent l'overlay.
        lv_obj_clear_flag(main_sheet, LV_OBJ_FLAG_CLICKABLE); 
        
        lv_obj_add_flag(main_sheet, LV_OBJ_FLAG_HIDDEN); 
        
        // 3. Panneau Principal (Haut)
        panel_top = lv_obj_create(main_sheet);
        lv_obj_set_size(panel_top, 300, 255);
        lv_obj_align(panel_top, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(panel_top, lv_color_hex(0xE0E0E0), 0); 
        lv_obj_set_style_bg_opa(panel_top, LV_OPA_COVER, 0); 
        lv_obj_set_style_radius(panel_top, 10, 0);
        lv_obj_set_style_border_width(panel_top, 0, 0);
        lv_obj_clear_flag(panel_top, LV_OBJ_FLAG_SCROLLABLE);
        
        // Ajout du Swipe-to-close sur le panneau du haut
        lv_obj_add_event_cb(panel_top, background_event_cb, LV_EVENT_GESTURE, this);

        bool eco_on = battery::is_manual_saver_enabled();
        uint8_t cur_vol = settings::getVolume();

        btn_eco      = createRoundBtn(panel_top, "ECO",          eco_toggle_event,      3, 8, eco_on,               0xF2C94C);
        btn_son      = createRoundBtn(panel_top, LV_SYMBOL_BELL,  son_toggle_event,      73, 8, cur_vol > 0,         0xFF9500);
        btn_4g       = createRoundBtn(panel_top, LV_SYMBOL_CALL,  lte_toggle_event,      143, 8, LTE::isEnabled(),   0x34C759);
        btn_airplane = createRoundBtn(panel_top, "\xE2\x9C\x88", airplane_toggle_event, 213, 8, LTE::isAirplaneMode(), 0xFF3B30);

        lv_obj_t *sep = lv_obj_create(panel_top);
        lv_obj_set_size(sep, 240, 2);
        lv_obj_set_pos(sep, 30, 84);
        lv_obj_set_style_bg_color(sep, lv_color_hex(0x888888), 0);
        lv_obj_set_style_border_width(sep, 0, 0);

        uint8_t br = settings::getBrightness();
        int br_pct = ((int)br - 5) * 100 / 250;
        if (br_pct < 0) br_pct = 0;
        if (br_pct > 100) br_pct = 100;

        slider_brightness = createHorizontalSlider(panel_top, LV_SYMBOL_CHARGE, 0, 100, br_pct, brightness_event, &lbl_brightness_val, 7, 98);
        slider_volume = createHorizontalSlider(panel_top, LV_SYMBOL_AUDIO, 0, 100, cur_vol, volume_event, &lbl_volume_val, 7, 168 );

        // --- PANNEAU MEDIA ---
        panel_media = lv_obj_create(main_sheet);
        lv_obj_set_size(panel_media, 300, 80);
        lv_obj_set_style_bg_color(panel_media, lv_color_hex(0x2C2C2E), 0); 
        lv_obj_set_style_bg_opa(panel_media, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(panel_media, 10, 0);
        lv_obj_set_style_border_width(panel_media, 0, 0);
        lv_obj_clear_flag(panel_media, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(panel_media, background_event_cb, LV_EVENT_GESTURE, this);

        lbl_media_title = lv_label_create(panel_media);
        lv_obj_set_width(lbl_media_title, 135); 
        lv_label_set_long_mode(lbl_media_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_set_style_text_color(lbl_media_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_media_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_media_title, LV_ALIGN_TOP_LEFT, 5, 10);

        lbl_media_artist = lv_label_create(panel_media);
        lv_obj_set_width(lbl_media_artist, 135);
        lv_label_set_long_mode(lbl_media_artist, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(lbl_media_artist, lv_color_hex(0x999999), 0);
        lv_obj_set_style_text_font(lbl_media_artist, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_media_artist, LV_ALIGN_TOP_LEFT, 5, 35);

        btn_media_prev = createMediaBtn(panel_media, LV_SYMBOL_PREV, media_prev_event, 135, 0);
        btn_media_toggle = createMediaBtn(panel_media, LV_SYMBOL_PLAY, media_toggle_event, 185, 0);
        btn_media_next = createMediaBtn(panel_media, LV_SYMBOL_NEXT, media_next_event, 235, 0);

        // 4. Panneau Notif 1
        panel_notif1 = lv_obj_create(main_sheet);
        lv_obj_set_size(panel_notif1, 300, 80);
        lv_obj_set_style_bg_color(panel_notif1, lv_color_hex(0xF2F2F2), 0); 
        lv_obj_set_style_bg_opa(panel_notif1, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(panel_notif1, 10, 0);
        lv_obj_set_style_border_width(panel_notif1, 0, 0);
        lv_obj_clear_flag(panel_notif1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(panel_notif1, background_event_cb, LV_EVENT_GESTURE, this);

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

        // 5. Panneau Notif 2
        panel_notif2 = lv_obj_create(main_sheet);
        lv_obj_set_size(panel_notif2, 300, 80);
        lv_obj_set_style_bg_color(panel_notif2, lv_color_hex(0xF2F2F2), 0);
        lv_obj_set_style_bg_opa(panel_notif2, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(panel_notif2, 10, 0);
        lv_obj_set_style_border_width(panel_notif2, 0, 0);
        lv_obj_clear_flag(panel_notif2, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(panel_notif2, background_event_cb, LV_EVENT_GESTURE, this);

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

        // 6. Poignée en bas (Handle)
        handle = lv_obj_create(main_sheet); 
        lv_obj_set_size(handle, 36, 4);
        lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -10);
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
        if (!overlay)
            return;

        is_open = true;
        animating = false;

        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_opa(overlay, overlay_hidden_opa, 0);
        
        lv_obj_clear_flag(main_sheet, LV_OBJ_FLAG_HIDDEN);

        applySheetOffset(sheet_hidden_y);

        uint8_t br = settings::getBrightness();
        int br_pct = ((int)br - 5) * 100 / 250;
        if (br_pct < 0) br_pct = 0;
        if (br_pct > 100) br_pct = 100;
        lv_slider_set_value(slider_brightness, br_pct, LV_ANIM_OFF);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", br_pct);
        lv_label_set_text(lbl_brightness_val, buf);

        int vol = settings::getVolume();
        lv_slider_set_value(slider_volume, vol, LV_ANIM_OFF);
        snprintf(buf, sizeof(buf), "%d%%", vol);
        lv_label_set_text(lbl_volume_val, buf);

        set_btn_state(btn_eco, battery::is_manual_saver_enabled(), 0xF2C94C);
        set_btn_state(btn_4g, LTE::isEnabled(), 0x34C759);
        set_btn_state(btn_airplane, LTE::isAirplaneMode(), 0xFF3B30);
        set_btn_state(btn_son, settings::getVolume() > 0, 0xFF9500);

        char n_app[20], n_title[36], n_body[96];
        bool media_active = cast_service::instance().isMediaActive();

        if (media_active) {
            lv_obj_align(panel_media, LV_ALIGN_TOP_MID, 0, 274);
            lv_obj_clear_flag(panel_media, LV_OBJ_FLAG_HIDDEN);
            
            lv_label_set_text(lbl_media_title, cast_service::instance().getTitle().c_str());
            lv_label_set_text(lbl_media_artist, cast_service::instance().getArtist().c_str());
            
            const char* icon = (cast_service::instance().getState() == "PLAYING") ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
            lv_obj_t* toggle_lbl = lv_obj_get_child(btn_media_toggle, 0);
            if (toggle_lbl) lv_label_set_text(toggle_lbl, icon);

            if (notifications::center().get_latest(0, n_app, n_title, n_body)) {
                lv_label_set_text(lbl_n1_app, n_app[0] ? n_app : "Systeme");
                lv_label_set_text(lbl_n1_title, n_title);
                lv_label_set_text(lbl_n1_body, n_body);
                lv_obj_align(panel_notif1, LV_ALIGN_TOP_MID, 0, 364);
                lv_obj_clear_flag(panel_notif1, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(panel_notif1, LV_OBJ_FLAG_HIDDEN);
            }

            lv_obj_add_flag(panel_notif2, LV_OBJ_FLAG_HIDDEN);

        } else {
            lv_obj_add_flag(panel_media, LV_OBJ_FLAG_HIDDEN);

            lv_obj_align(panel_notif1, LV_ALIGN_TOP_MID, 0, 274);
            lv_obj_clear_flag(panel_notif1, LV_OBJ_FLAG_HIDDEN);
            if (notifications::center().get_latest(0, n_app, n_title, n_body)) {
                lv_label_set_text(lbl_n1_app, n_app[0] ? n_app : "Systeme");
                lv_label_set_text(lbl_n1_title, n_title);
                lv_label_set_text(lbl_n1_body, n_body);
            } else {
                lv_label_set_text(lbl_n1_app, "Systeme");
                lv_label_set_text(lbl_n1_title, "Aucune notification");
                lv_label_set_text(lbl_n1_body, "Vous etes a jour.");
            }

            lv_obj_align(panel_notif2, LV_ALIGN_TOP_MID, 0, 364);
            if (notifications::center().get_latest(1, n_app, n_title, n_body)) {
                lv_label_set_text(lbl_n2_app, n_app[0] ? n_app : "Systeme");
                lv_label_set_text(lbl_n2_title, n_title);
                lv_label_set_text(lbl_n2_body, n_body);
                lv_obj_clear_flag(panel_notif2, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(panel_notif2, LV_OBJ_FLAG_HIDDEN);
            }
        }

        animateSheetTo(0);
    }

    void close()
    {
        if (!is_open)
            return;
        is_open = false;

        if (!ui_created) return;
        animateOverlayTo(overlay_hidden_opa, 90);
        animateSheetTo(sheet_hidden_y);
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