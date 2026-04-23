#ifdef __cplusplus
extern "C" {
#endif
extern void hardware_sleep();
extern void hardware_wake();
#ifdef __cplusplus
}
#endif
#ifndef LOCKSCREEN_H
#define LOCKSCREEN_H

#include <lvgl.h>
#include <Arduino.h>
#include <time.h>
#include <string.h>
#include "Battery.h"
#include "Settings.h"

namespace notifications {
    bool latest(uint8_t index, char* out_app, char* out_title, char* out_body);
}

class LockScreen {
private:
    static constexpr uint8_t LOCK_DIM_PWM = 56;
    static constexpr uint32_t CLOCK_UPDATE_AWAKE_MS = 1000;

    lv_coord_t screen_w = 320;
    lv_coord_t screen_h = 480;

    lv_obj_t* layer = nullptr;
    lv_obj_t* bg = nullptr;

    // Ecran principal (heure + notifications + swipe)
    lv_obj_t* main_view = nullptr;
    lv_obj_t* lbl_time = nullptr;
    lv_obj_t* lbl_date = nullptr;
    lv_obj_t* lbl_power = nullptr;
    lv_obj_t* lbl_swipe_hint = nullptr;
    lv_obj_t* notif_panel = nullptr;
    lv_obj_t* lbl_notif_title = nullptr;
    lv_obj_t* lbl_notif_count = nullptr;
    lv_obj_t* lbl_notif_empty = nullptr;
    lv_obj_t* notif_cards[2] = {nullptr, nullptr};
    lv_obj_t* notif_meta[2] = {nullptr, nullptr};
    lv_obj_t* notif_body[2] = {nullptr, nullptr};

    // Écran PIN
    lv_obj_t* pin_view = nullptr;
    lv_obj_t* lbl_pin_title = nullptr;
    lv_obj_t* pin_dots[6] = {nullptr};
    lv_obj_t* pin_btns[12] = {nullptr};
    bool pin_ui_created = false;

    bool is_locked = false;
    bool display_sleeping = false;
    unsigned long last_update = 0;

    // PIN input state
    char pin_input[7] = {0};
    uint8_t pin_len = 0;
    bool pin_error = false;
    unsigned long pin_error_time = 0;

    // Swipe detection
    lv_coord_t touch_start_y = 0;
    bool touch_active = false;

    // --- FORMATAGE DATE ---
    static const char* get_day_name(int wday) {
        static const char* days[] = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};
        if (wday < 0 || wday > 6) return "";
        return days[wday];
    }
    
    static const char* get_month_name(int mon) {
        static const char* months[] = {
            "jan.", "fev.", "mars", "avr.", "mai", "juin",
            "juil.", "aout", "sept.", "oct.", "nov.", "dec."
        };
        if (mon < 0 || mon > 11) return "";
        return months[mon];
    }

    static const char* battery_icon(uint8_t percent) {
        if (percent >= 80) return LV_SYMBOL_BATTERY_FULL;
        if (percent >= 60) return LV_SYMBOL_BATTERY_3;
        if (percent >= 40) return LV_SYMBOL_BATTERY_2;
        if (percent >= 20) return LV_SYMBOL_BATTERY_1;
        return LV_SYMBOL_BATTERY_EMPTY;
    }

    void refresh_screen_geometry() {
        lv_disp_t* disp = lv_disp_get_default();
        if (disp) {
            screen_w = lv_disp_get_hor_res(disp);
            screen_h = lv_disp_get_ver_res(disp);
        }
        if (screen_w <= 0) screen_w = 320;
        if (screen_h <= 0) screen_h = 480;
    }

    void sleepDisplay() {
        if (display_sleeping) return;
        hardware_sleep();
        display_sleeping = true;
    }

    void wakeDisplay() {
        if (!display_sleeping) return;
        hardware_wake();
        display_sleeping = false;
    }

    void layoutNotifCard(uint8_t index, lv_coord_t y, lv_coord_t card_w, lv_coord_t card_h) {
        if (index >= 2 || !notif_cards[index]) return;

        if (card_h < 50) card_h = 50;

        lv_obj_set_size(notif_cards[index], card_w, card_h);
        lv_obj_align(notif_cards[index], LV_ALIGN_TOP_MID, 0, y);

        if (notif_meta[index]) {
            lv_obj_set_width(notif_meta[index], card_w - 16);
            lv_obj_align(notif_meta[index], LV_ALIGN_TOP_LEFT, 0, 0);
        }

        if (notif_body[index]) {
            lv_obj_set_width(notif_body[index], card_w - 16);
            lv_obj_set_height(notif_body[index], card_h - 22);
            lv_obj_align(notif_body[index], LV_ALIGN_TOP_LEFT, 0, 16);
        }
    }

    void updateNotificationPanelLayout(uint8_t count) {
        if (!notif_panel) return;

        const lv_coord_t panel_w = screen_w - ((screen_w >= 360) ? 22 : 18);
        const lv_coord_t panel_h_none = (screen_h >= 520) ? 80 : 70;
        const lv_coord_t panel_h_one = (screen_h >= 520) ? 136 : 122;
        const lv_coord_t panel_h_two = (screen_h >= 520) ? 196 : 172;

        lv_coord_t panel_h = panel_h_two;
        if (count == 0) panel_h = panel_h_none;
        else if (count == 1) panel_h = panel_h_one;

        lv_obj_set_size(notif_panel, panel_w, panel_h);
        lv_obj_align(notif_panel, LV_ALIGN_BOTTOM_MID, 0, -16);
        lv_obj_set_style_bg_opa(notif_panel, (count == 0) ? LV_OPA_40 : LV_OPA_60, 0);

        if (lbl_notif_title) lv_obj_align(lbl_notif_title, LV_ALIGN_TOP_LEFT, 0, 0);
        if (lbl_notif_count) lv_obj_align(lbl_notif_count, LV_ALIGN_TOP_RIGHT, 0, 0);

        const lv_coord_t card_w = panel_w - 20;
        const lv_coord_t header_y = 26;

        if (count == 0) {
            if (notif_cards[0]) lv_obj_add_flag(notif_cards[0], LV_OBJ_FLAG_HIDDEN);
            if (notif_cards[1]) lv_obj_add_flag(notif_cards[1], LV_OBJ_FLAG_HIDDEN);
            if (lbl_notif_empty) {
                lv_obj_clear_flag(lbl_notif_empty, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_width(lbl_notif_empty, card_w);
                lv_label_set_long_mode(lbl_notif_empty, LV_LABEL_LONG_DOT);
                lv_obj_align(lbl_notif_empty, LV_ALIGN_CENTER, 0, 10);
            }
        } else if (count == 1) {
            if (lbl_notif_empty) lv_obj_add_flag(lbl_notif_empty, LV_OBJ_FLAG_HIDDEN);
            if (notif_cards[0]) lv_obj_clear_flag(notif_cards[0], LV_OBJ_FLAG_HIDDEN);
            if (notif_cards[1]) lv_obj_add_flag(notif_cards[1], LV_OBJ_FLAG_HIDDEN);

            lv_coord_t h = panel_h - header_y - 8;
            if (h < 54) h = 54;
            layoutNotifCard(0, header_y, card_w, h);
        } else {
            if (lbl_notif_empty) lv_obj_add_flag(lbl_notif_empty, LV_OBJ_FLAG_HIDDEN);
            if (notif_cards[0]) lv_obj_clear_flag(notif_cards[0], LV_OBJ_FLAG_HIDDEN);
            if (notif_cards[1]) lv_obj_clear_flag(notif_cards[1], LV_OBJ_FLAG_HIDDEN);

            lv_coord_t h = (panel_h - header_y - 8) / 2;
            if (h < 50) h = 50;
            layoutNotifCard(0, header_y, card_w, h);
            layoutNotifCard(1, header_y + h + 8, card_w, h);
        }

        if (lbl_swipe_hint) {
            lv_obj_align_to(lbl_swipe_hint, notif_panel, LV_ALIGN_OUT_TOP_MID, 0, -10);
        }
    }

    void setNotifCardText(uint8_t index, const char* app, const char* title, const char* body) {
        if (index >= 2 || !notif_cards[index]) return;

        lv_obj_clear_flag(notif_cards[index], LV_OBJ_FLAG_HIDDEN);
        if (notif_meta[index]) lv_obj_clear_flag(notif_meta[index], LV_OBJ_FLAG_HIDDEN);
        if (notif_body[index]) lv_obj_clear_flag(notif_body[index], LV_OBJ_FLAG_HIDDEN);

        char meta_buf[96];
        if (app && app[0] && title && title[0]) {
            snprintf(meta_buf, sizeof(meta_buf), "%s - %s", app, title);
        } else if (title && title[0]) {
            snprintf(meta_buf, sizeof(meta_buf), "%s", title);
        } else if (app && app[0]) {
            snprintf(meta_buf, sizeof(meta_buf), "%s", app);
        } else {
            snprintf(meta_buf, sizeof(meta_buf), "Notification");
        }

        lv_label_set_text(notif_meta[index], meta_buf);
        lv_label_set_text(notif_body[index], (body && body[0]) ? body : "-");
    }

    void updateNotifications() {
        char app0[20] = {0};
        char title0[36] = {0};
        char body0[96] = {0};
        char app1[20] = {0};
        char title1[36] = {0};
        char body1[96] = {0};

        const bool has0 = notifications::latest(0, app0, title0, body0);
        const bool has1 = notifications::latest(1, app1, title1, body1);
        const uint8_t count = (has0 ? 1 : 0) + (has1 ? 1 : 0);

        updateNotificationPanelLayout(count);

        if (has0) {
            setNotifCardText(0, app0, title0, body0);
        } else if (notif_cards[0]) {
            lv_obj_add_flag(notif_cards[0], LV_OBJ_FLAG_HIDDEN);
        }

        if (has1) {
            setNotifCardText(1, app1, title1, body1);
        } else if (notif_cards[1]) {
            lv_obj_add_flag(notif_cards[1], LV_OBJ_FLAG_HIDDEN);
        }

        char count_buf[24];
        if (count == 0) {
            snprintf(count_buf, sizeof(count_buf), "Aucune");
        } else if (count == 1) {
            snprintf(count_buf, sizeof(count_buf), "1 recente");
        } else {
            snprintf(count_buf, sizeof(count_buf), "%u recentes", (unsigned)count);
        }
        lv_label_set_text(lbl_notif_count, count_buf);
    }

    // --- EVENTS SWIPE ---
    static void main_touch_event(lv_event_t* e) {
        LockScreen* self = (LockScreen*)lv_event_get_user_data(e);
        if (!self || !self->is_locked || self->display_sleeping) return;

        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_get_act();
            if (!indev) return;
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            self->touch_start_y = p.y;
            self->touch_active = true;
        }
        else if (code == LV_EVENT_RELEASED && self->touch_active) {
            lv_indev_t* indev = lv_indev_get_act();
            if (!indev) return;
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            self->touch_active = false;

            lv_coord_t dy = self->touch_start_y - p.y;

            lv_coord_t min_swipe = self->screen_h / 7;
            if (min_swipe < 70) min_swipe = 70;

            if (dy > min_swipe) {
                if (settings::isPinEnabled()) {
                    self->showPinScreen();
                } else {
                    self->unlock();
                }
            }
        }
    }

    // --- EVENTS PIN ---
    static void pin_btn_event(lv_event_t* e) {
        LockScreen* self = (LockScreen*)lv_event_get_user_data(e);
        if (!self) return;
        lv_obj_t* btn = lv_event_get_target(e);
        int digit = (int)(intptr_t)lv_obj_get_user_data(btn);

        if (digit == 10) {
            // Effacer
            if (self->pin_len > 0) {
                self->pin_len--;
                self->pin_input[self->pin_len] = '\0';
                self->updatePinDots();
            }
        } else if (digit == 11) {
            self->validatePin();
        } else {
            if (self->pin_len < 6) {
                self->pin_input[self->pin_len] = '0' + digit;
                self->pin_len++;
                self->pin_input[self->pin_len] = '\0';
                self->updatePinDots();

                size_t expected_len = strlen(settings::getPinCode());
                if (self->pin_len == expected_len) {
                    self->validatePin();
                }
            }
        }
    }
    
    void validatePin() {
        if (strcmp(pin_input, settings::getPinCode()) == 0) {
            unlock();
        } else {
            pin_error = true;
            pin_error_time = millis();
            lv_label_set_text(lbl_pin_title, "Code incorrect");
            lv_obj_set_style_text_color(lbl_pin_title, lv_color_hex(0xFF3B30), 0);

            pin_len = 0;
            memset(pin_input, 0, sizeof(pin_input));
            updatePinDots();
        }
    }

    void updatePinDots() {
        size_t expected_len = strlen(settings::getPinCode());
        if (expected_len > 6) expected_len = 6;

        for (int i = 0; i < 6; i++) {
            if (!pin_dots[i]) continue;
            if (i >= (int)expected_len) {
                lv_obj_add_flag(pin_dots[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(pin_dots[i], LV_OBJ_FLAG_HIDDEN);
                if (i < pin_len) {
                    lv_obj_set_style_bg_color(pin_dots[i], lv_color_white(), 0);
                    lv_obj_set_style_bg_opa(pin_dots[i], LV_OPA_COVER, 0);
                } else {
                    lv_obj_set_style_bg_color(pin_dots[i], lv_color_white(), 0);
                    lv_obj_set_style_bg_opa(pin_dots[i], LV_OPA_30, 0);
                }
            }
        }
    }
    
    void showPinScreen() {
        if (!pin_ui_created) {
            buildPinUI();
        }
        wakeDisplay();

        pin_len = 0;
        memset(pin_input, 0, sizeof(pin_input));
        pin_error = false;

        lv_label_set_text(lbl_pin_title, "Entrez votre code");
        lv_obj_set_style_text_color(lbl_pin_title, lv_color_white(), 0);
        updatePinDots();

        lv_obj_add_flag(main_view, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pin_view, LV_OBJ_FLAG_HIDDEN);
    }

    void showMainScreen() {
        lv_obj_clear_flag(main_view, LV_OBJ_FLAG_HIDDEN);
        if (pin_view) lv_obj_add_flag(pin_view, LV_OBJ_FLAG_HIDDEN);
    }

    void buildPinUI() {
        if (pin_ui_created) return;

        pin_view = lv_obj_create(bg);
        lv_obj_set_size(pin_view, screen_w, screen_h);
        lv_obj_set_style_pad_all(pin_view, 0, 0);
        lv_obj_set_style_bg_opa(pin_view, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pin_view, 0, 0);
        lv_obj_clear_flag(pin_view, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(pin_view, LV_OBJ_FLAG_HIDDEN);

        lbl_pin_title = lv_label_create(pin_view);
        lv_label_set_text(lbl_pin_title, "Entrez votre code");
        lv_obj_set_style_text_color(lbl_pin_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_pin_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_pin_title, LV_ALIGN_TOP_MID, 0, screen_h / 11);

        lv_obj_t* dots_cont = lv_obj_create(pin_view);
        lv_obj_set_size(dots_cont, (screen_w > 260) ? 220 : (screen_w - 32), 20);
        lv_obj_align(dots_cont, LV_ALIGN_TOP_MID, 0, (screen_h / 11) + 34);
        lv_obj_set_style_pad_all(dots_cont, 0, 0);
        lv_obj_set_style_bg_opa(dots_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(dots_cont, 0, 0);
        lv_obj_clear_flag(dots_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(dots_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(dots_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(dots_cont, 12, 0);

        for (int i = 0; i < 6; i++) {
            pin_dots[i] = lv_obj_create(dots_cont);
            lv_obj_set_size(pin_dots[i], 14, 14);
            lv_obj_set_style_radius(pin_dots[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(pin_dots[i], lv_color_white(), 0);
            lv_obj_set_style_bg_opa(pin_dots[i], LV_OPA_30, 0);
            lv_obj_set_style_border_color(pin_dots[i], lv_color_white(), 0);
            lv_obj_set_style_border_width(pin_dots[i], 1, 0);
            lv_obj_set_style_border_opa(pin_dots[i], LV_OPA_50, 0);
        }

        lv_obj_t* keypad = lv_obj_create(pin_view);
        lv_obj_set_size(keypad, screen_w, (screen_h * 58) / 100);
        lv_obj_align(keypad, LV_ALIGN_BOTTOM_MID, 0, -14);
        lv_obj_set_style_pad_all(keypad, 0, 0);
        lv_obj_set_style_bg_opa(keypad, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(keypad, 0, 0);
        lv_obj_clear_flag(keypad, LV_OBJ_FLAG_SCROLLABLE);

        const char* labels[] = {"1","2","3","4","5","6","7","8","9",LV_SYMBOL_BACKSPACE,"0",LV_SYMBOL_OK};
        int ids[]            = { 1,  2,  3,  4,  5,  6,  7,  8,  9,  10,                 0,  11};

        const lv_coord_t btn_w = (screen_w >= 360) ? 78 : 70;
        const lv_coord_t btn_h = (screen_h >= 520) ? 62 : 56;
        const lv_coord_t gap_x = (screen_w >= 360) ? 16 : 12;
        const lv_coord_t gap_y = 10;

        const lv_coord_t grid_w = btn_w * 3 + gap_x * 2;
        const lv_coord_t grid_h = btn_h * 4 + gap_y * 3;
        lv_coord_t x_start = (screen_w - grid_w) / 2;
        if (x_start < 8) x_start = 8;
        lv_coord_t y_start = (lv_obj_get_height(keypad) - grid_h) / 2;
        if (y_start < 4) y_start = 4;

        for (int i = 0; i < 12; i++) {
            int col = i % 3;
            int row = i / 3;
            pin_btns[i] = createPinButton(
                keypad,
                labels[i],
                ids[i],
                x_start + col * (btn_w + gap_x),
                y_start + row * (btn_h + gap_y),
                btn_w,
                btn_h
            );
        }

        pin_ui_created = true;
    }

    lv_obj_t* createPinButton(lv_obj_t* parent,
                              const char* label,
                              int id,
                              lv_coord_t x,
                              lv_coord_t y,
                              lv_coord_t w,
                              lv_coord_t h) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, w, h);
        lv_obj_set_pos(btn, x, y);

        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_20, 0);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);

        lv_obj_set_user_data(btn, (void*)(intptr_t)id);
        lv_obj_add_event_cb(btn, pin_btn_event, LV_EVENT_CLICKED, this);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, label);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_center(lbl);

        return btn;
    }

public:
    void init() {
        layer = lv_layer_top();
        lv_disp_trig_activity(NULL);

        refresh_screen_geometry();

        // --- FOND PRINCIPAL ---
        bg = lv_obj_create(layer);
        lv_obj_set_size(bg, screen_w, screen_h);
        lv_obj_set_style_pad_all(bg, 0, 0);
        lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_color(bg, lv_color_hex(0x0B1220), 0);
        lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x111B2E), 0);
        lv_obj_set_style_border_width(bg, 0, 0);
        lv_obj_set_style_radius(bg, 0, 0);
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* glow_top = lv_obj_create(bg);
        lv_obj_set_size(glow_top, screen_w + 120, screen_w + 120);
        lv_obj_align(glow_top, LV_ALIGN_TOP_MID, 0, -(screen_w / 2));
        lv_obj_set_style_radius(glow_top, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(glow_top, lv_color_hex(0x2F80ED), 0);
        lv_obj_set_style_bg_opa(glow_top, LV_OPA_20, 0);
        lv_obj_set_style_border_width(glow_top, 0, 0);
        lv_obj_clear_flag(glow_top, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(glow_top, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* glow_bottom = lv_obj_create(bg);
        lv_obj_set_size(glow_bottom, screen_w + 90, screen_w + 90);
        lv_obj_align(glow_bottom, LV_ALIGN_BOTTOM_MID, 0, screen_w / 3);
        lv_obj_set_style_radius(glow_bottom, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(glow_bottom, lv_color_hex(0x4D6FB0), 0);
        lv_obj_set_style_bg_opa(glow_bottom, LV_OPA_10, 0);
        lv_obj_set_style_border_width(glow_bottom, 0, 0);
        lv_obj_clear_flag(glow_bottom, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(glow_bottom, LV_OBJ_FLAG_CLICKABLE);

        main_view = lv_obj_create(bg);
        lv_obj_set_size(main_view, screen_w, screen_h);
        lv_obj_set_style_pad_all(main_view, 0, 0);
        lv_obj_set_style_bg_opa(main_view, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(main_view, 0, 0);
        lv_obj_clear_flag(main_view, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(main_view, main_touch_event, LV_EVENT_ALL, this);

        // Badge batterie
        lv_obj_t* power_chip = lv_obj_create(main_view);
        lv_obj_set_size(power_chip, (screen_w >= 360) ? 128 : 116, 30);
        lv_obj_align(power_chip, LV_ALIGN_TOP_RIGHT, -10, 10);
        lv_obj_set_style_bg_color(power_chip, lv_color_hex(0x1C2639), 0);
        lv_obj_set_style_bg_opa(power_chip, LV_OPA_50, 0);
        lv_obj_set_style_radius(power_chip, 15, 0);
        lv_obj_set_style_border_width(power_chip, 1, 0);
        lv_obj_set_style_border_color(power_chip, lv_color_hex(0x3B4D70), 0);
        lv_obj_set_style_border_opa(power_chip, LV_OPA_70, 0);
        lv_obj_set_style_pad_all(power_chip, 0, 0);
        lv_obj_clear_flag(power_chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(power_chip, LV_OBJ_FLAG_CLICKABLE);

        // CORRECTION N°1 : Assigner 'power_chip' comme objet parent au lieu de 'main_view' 
        lbl_power = lv_label_create(power_chip);
        lv_label_set_text(lbl_power, "--%");
        lv_obj_set_style_text_color(lbl_power, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_power, &lv_font_montserrat_14, 0);
        // Ainsi le label restera magnétiquement centré dans la puce !
        lv_obj_center(lbl_power); 

        // Heure
        lbl_time = lv_label_create(main_view);
        // CORRECTION N°2 : Forcer la largeur et centrer le texte pour un rendu dynamique parfait
        lv_obj_set_width(lbl_time, screen_w); 
        lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(lbl_time, "00:00");
        lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);
        lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, screen_h / 7);

        // Date
        lbl_date = lv_label_create(main_view);
        // CORRECTION N°3 : Même chose pour la date
        lv_obj_set_width(lbl_date, screen_w); 
        lv_obj_set_style_text_align(lbl_date, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(lbl_date, "");
        lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xA9B9D5), 0);
        lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
        // On aligne par rapport au label de l'heure en ajoutant LV_ALIGN_OUT_BOTTOM_MID
        lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

        // Panneau notifications
        notif_panel = lv_obj_create(main_view);
        lv_obj_set_size(notif_panel, screen_w - 18, (screen_h >= 520) ? 196 : 172);
        lv_obj_align(notif_panel, LV_ALIGN_BOTTOM_MID, 0, -18);
        lv_obj_set_style_bg_color(notif_panel, lv_color_hex(0x121A29), 0);
        lv_obj_set_style_bg_opa(notif_panel, LV_OPA_60, 0);
        lv_obj_set_style_radius(notif_panel, 16, 0);
        lv_obj_set_style_border_width(notif_panel, 1, 0);
        lv_obj_set_style_border_color(notif_panel, lv_color_hex(0x2F4369), 0);
        lv_obj_set_style_border_opa(notif_panel, LV_OPA_70, 0);
        lv_obj_set_style_shadow_width(notif_panel, 16, 0);
        lv_obj_set_style_shadow_color(notif_panel, lv_color_hex(0x050A14), 0);
        lv_obj_set_style_shadow_opa(notif_panel, LV_OPA_40, 0);
        lv_obj_set_style_shadow_ofs_y(notif_panel, 3, 0);
        lv_obj_set_style_pad_left(notif_panel, 10, 0);
        lv_obj_set_style_pad_right(notif_panel, 10, 0);
        lv_obj_set_style_pad_top(notif_panel, 8, 0);
        lv_obj_set_style_pad_bottom(notif_panel, 8, 0);
        lv_obj_clear_flag(notif_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(notif_panel, LV_OBJ_FLAG_CLICKABLE);

        lbl_notif_title = lv_label_create(notif_panel);
        lv_label_set_text(lbl_notif_title, LV_SYMBOL_BELL " Notifications");
        lv_obj_set_style_text_color(lbl_notif_title, lv_color_hex(0xDCE7FF), 0);
        lv_obj_set_style_text_font(lbl_notif_title, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_notif_title, LV_ALIGN_TOP_LEFT, 0, 0);

        lbl_notif_count = lv_label_create(notif_panel);
        lv_label_set_text(lbl_notif_count, "Aucune");
        lv_obj_set_style_text_color(lbl_notif_count, lv_color_hex(0x9FB2D4), 0);
        lv_obj_set_style_text_font(lbl_notif_count, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_notif_count, LV_ALIGN_TOP_RIGHT, 0, 0);

        const lv_coord_t card_w = lv_obj_get_width(notif_panel) - 20;
        lv_coord_t card_h = (lv_obj_get_height(notif_panel) - 40 - 10) / 2;
        if (card_h < 48) card_h = 48;

        for (uint8_t i = 0; i < 2; ++i) {
            notif_cards[i] = lv_obj_create(notif_panel);
            lv_obj_set_size(notif_cards[i], card_w, card_h);
            lv_obj_align(notif_cards[i], LV_ALIGN_TOP_MID, 0, 26 + i * (card_h + 8));
            lv_obj_set_style_bg_color(notif_cards[i], lv_color_hex(0x1E2A42), 0);
            lv_obj_set_style_bg_opa(notif_cards[i], LV_OPA_70, 0);
            lv_obj_set_style_radius(notif_cards[i], 12, 0);
            lv_obj_set_style_border_width(notif_cards[i], 1, 0);
            lv_obj_set_style_border_color(notif_cards[i], lv_color_hex(0x39517D), 0);
            lv_obj_set_style_border_opa(notif_cards[i], LV_OPA_70, 0);
            lv_obj_set_style_pad_left(notif_cards[i], 8, 0);
            lv_obj_set_style_pad_right(notif_cards[i], 8, 0);
            lv_obj_set_style_pad_top(notif_cards[i], 5, 0);
            lv_obj_set_style_pad_bottom(notif_cards[i], 5, 0);
            lv_obj_clear_flag(notif_cards[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(notif_cards[i], LV_OBJ_FLAG_CLICKABLE);

            notif_meta[i] = lv_label_create(notif_cards[i]);
            lv_obj_set_width(notif_meta[i], card_w - 16);
            lv_label_set_long_mode(notif_meta[i], LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(notif_meta[i], lv_color_hex(0xA8BEDF), 0);
            lv_obj_set_style_text_font(notif_meta[i], &lv_font_montserrat_12, 0);
            lv_obj_align(notif_meta[i], LV_ALIGN_TOP_LEFT, 0, 0);

            notif_body[i] = lv_label_create(notif_cards[i]);
            lv_obj_set_width(notif_body[i], card_w - 16);
            lv_label_set_long_mode(notif_body[i], LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_color(notif_body[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(notif_body[i], &lv_font_montserrat_12, 0);
            lv_obj_align(notif_body[i], LV_ALIGN_TOP_LEFT, 0, 16);
        }

        lbl_notif_empty = lv_label_create(notif_panel);
        lv_label_set_text(lbl_notif_empty, "Aucune notification recente");
        lv_obj_set_style_text_color(lbl_notif_empty, lv_color_hex(0xC8D5EC), 0);
        lv_obj_set_style_text_font(lbl_notif_empty, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_notif_empty, LV_ALIGN_CENTER, 0, 14);

        // Indication swipe
        lbl_swipe_hint = lv_label_create(main_view);
        lv_label_set_text(lbl_swipe_hint, LV_SYMBOL_UP " Glisser pour deverrouiller");
        lv_obj_set_style_text_color(lbl_swipe_hint, lv_color_hex(0x97A8C9), 0);
        lv_obj_set_style_text_font(lbl_swipe_hint, &lv_font_montserrat_12, 0);
        lv_obj_align_to(lbl_swipe_hint, notif_panel, LV_ALIGN_OUT_TOP_MID, 0, -10);

        // Par défaut caché
        lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);

        updateTime();
        updateNotifications();
    }

    void lock() {
        if (is_locked) return;

        is_locked = true;
        showMainScreen();
        pin_len = 0;
        memset(pin_input, 0, sizeof(pin_input));

        lv_obj_clear_flag(bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(bg);
        updateTime();
        updateNotifications();
        sleepDisplay();
    }

    void wakeToLockScreen() {
        if (!is_locked) {
            lock();
            return;
        }

        lv_obj_clear_flag(bg, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(bg);
        showMainScreen();
        wakeDisplay();
        updateTime();
        updateNotifications();
        lv_disp_trig_activity(NULL);
    }

    void unlock() {
        if (!is_locked) return;

        if (display_sleeping) {
            is_locked = false;
            hardware_wake();
            display_sleeping = false;
        } else {
            is_locked = false;
        }

        lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);
        showMainScreen();
        lv_disp_trig_activity(NULL);
    }

    bool isLocked() const { return is_locked; }
    bool isDisplaySleeping() const { return display_sleeping; }
    uint8_t lockAwakePwm() const { return LOCK_DIM_PWM; }

    void update() {
        uint32_t timeout = settings::getLockTimeout();
        if (timeout > 0 && !is_locked && lv_disp_get_inactive_time(NULL) > timeout) {
            lock();
        }

        if (is_locked && !display_sleeping && (millis() - last_update > CLOCK_UPDATE_AWAKE_MS)) {
            updateTime();
            updateNotifications();
            last_update = millis();
        }

        // Reset erreur PIN après 1.5s
        if (pin_error && (millis() - pin_error_time > 1500)) {
            pin_error = false;
            lv_label_set_text(lbl_pin_title, "Entrez votre code");
            lv_obj_set_style_text_color(lbl_pin_title, lv_color_white(), 0);
        }
    }

    void updateTime() {
        time_t now = time(nullptr);
        struct tm timeinfo;
        if (now <= 0 || localtime_r(&now, &timeinfo) == nullptr) {
            lv_label_set_text(lbl_time, "--:--");
            lv_label_set_text(lbl_date, "-");
            lv_label_set_text(lbl_power, "--%");
            return;
        }

        char buf[8] = {0};
        snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        lv_label_set_text(lbl_time, buf);

        char datebuf[32];
        snprintf(datebuf, sizeof(datebuf), "%s %d %s",
                 get_day_name(timeinfo.tm_wday),
                 timeinfo.tm_mday,
                 get_month_name(timeinfo.tm_mon));
        lv_label_set_text(lbl_date, datebuf);

        const uint8_t p = battery::read_percent();
        const bool charging = battery::is_charging() || battery::is_external_power();

        char pbuf[24];
        snprintf(pbuf, sizeof(pbuf), "%s%s %u%%",
                 charging ? LV_SYMBOL_CHARGE : "",
                 battery_icon(p),
                 (unsigned)p);

        if (charging) {
            lv_obj_set_style_text_color(lbl_power, lv_color_hex(0x77E4A1), 0);
        } else if (p <= 20) {
            lv_obj_set_style_text_color(lbl_power, lv_color_hex(0xFFB15A), 0);
        } else {
            lv_obj_set_style_text_color(lbl_power, lv_color_white(), 0);
        }

        lv_label_set_text(lbl_power, pbuf);
    }
};

#endif