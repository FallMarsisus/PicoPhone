#ifndef LOCKSCREEN_H
#define LOCKSCREEN_H

#include <lvgl.h>
#include <Arduino.h>
#include <time.h>
#include "Battery.h"
#include "Settings.h"

class LockScreen {
private:
    lv_obj_t* layer = nullptr;
    lv_obj_t* bg = nullptr;
    
    // Écran principal (horloge + swipe)
    lv_obj_t* main_view = nullptr;
    lv_obj_t* lbl_time = nullptr;
    lv_obj_t* lbl_date = nullptr;
    lv_obj_t* lbl_power = nullptr;
    lv_obj_t* lbl_swipe_hint = nullptr;
    
    // Écran PIN
    lv_obj_t* pin_view = nullptr;
    lv_obj_t* lbl_pin_title = nullptr;
    lv_obj_t* pin_dots[6] = {nullptr};
    lv_obj_t* pin_btns[12] = {nullptr};
    bool pin_ui_created = false;
    
    bool is_locked = false;
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
        static const char* months[] = {"jan.", "fev.", "mars", "avr.", "mai", "juin",
                                        "juil.", "aout", "sept.", "oct.", "nov.", "dec."};
        if (mon < 0 || mon > 11) return "";
        return months[mon];
    }

    // --- EVENTS SWIPE ---
    static void main_touch_event(lv_event_t* e) {
        LockScreen* self = (LockScreen*)lv_event_get_user_data(e);
        lv_event_code_t code = lv_event_get_code(e);
        
        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            self->touch_start_y = p.y;
            self->touch_active = true;
        }
        else if (code == LV_EVENT_RELEASED && self->touch_active) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            self->touch_active = false;
            
            lv_coord_t dy = self->touch_start_y - p.y;
            
            if (dy > 80) {
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
        lv_obj_set_size(pin_view, 320, 480);
        lv_obj_set_style_pad_all(pin_view, 0, 0);
        lv_obj_set_style_bg_opa(pin_view, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(pin_view, 0, 0);
        lv_obj_clear_flag(pin_view, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(pin_view, LV_OBJ_FLAG_HIDDEN);

        lbl_pin_title = lv_label_create(pin_view);
        lv_label_set_text(lbl_pin_title, "Entrez votre code");
        lv_obj_set_style_text_color(lbl_pin_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_pin_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_pin_title, LV_ALIGN_TOP_MID, 0, 40);

        lv_obj_t* dots_cont = lv_obj_create(pin_view);
        lv_obj_set_size(dots_cont, 200, 20);
        lv_obj_align(dots_cont, LV_ALIGN_TOP_MID, 0, 75);
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
        lv_obj_set_size(keypad, 320, 300);
        lv_obj_align(keypad, LV_ALIGN_BOTTOM_MID, 0, -20);
        lv_obj_set_style_pad_all(keypad, 0, 0);
        lv_obj_set_style_bg_opa(keypad, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(keypad, 0, 0);
        lv_obj_clear_flag(keypad, LV_OBJ_FLAG_SCROLLABLE);

        const char* labels[] = {"1","2","3","4","5","6","7","8","9",LV_SYMBOL_BACKSPACE,"0",LV_SYMBOL_OK};
        int ids[]            = { 1,  2,  3,  4,  5,  6,  7,  8,  9,  10,                 0,  11};

        for (int i = 0; i < 12; i++) {
            int col = i % 3;
            int row = i / 3;
            pin_btns[i] = createPinButton(keypad, labels[i], ids[i], col, row);
        }

        pin_ui_created = true;
    }

    lv_obj_t* createPinButton(lv_obj_t* parent, const char* label, int id, int col, int row) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 70, 55);
        
        int x_start = (320 - 3 * 70 - 2 * 15) / 2;
        int y_start = 8;
        lv_obj_set_pos(btn, x_start + col * (70 + 15), y_start + row * (55 + 12));
        
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
        
        // --- FOND PRINCIPAL ---
        bg = lv_obj_create(layer);
        lv_obj_set_size(bg, 320, 480);
        lv_obj_set_style_pad_all(bg, 0, 0);
        lv_obj_set_style_bg_grad_dir(bg, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_color(bg, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_grad_color(bg, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_border_width(bg, 0, 0);
        lv_obj_set_style_radius(bg, 0, 0);
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
        
        // ================================================================
        // VUE PRINCIPALE (Horloge + Swipe Up)
        // ================================================================
        main_view = lv_obj_create(bg);
        lv_obj_set_size(main_view, 320, 480);
        lv_obj_set_style_pad_all(main_view, 0, 0);
        lv_obj_set_style_bg_opa(main_view, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(main_view, 0, 0);
        lv_obj_clear_flag(main_view, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(main_view, main_touch_event, LV_EVENT_ALL, this);
        
        // Batterie (haut droit)
        lbl_power = lv_label_create(main_view);
        lv_label_set_text(lbl_power, "--%");
        lv_obj_set_style_text_color(lbl_power, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_power, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_power, LV_ALIGN_TOP_RIGHT, -15, 12);
        
        // HEURE (Grande, centrée)
        lbl_time = lv_label_create(main_view);
        lv_label_set_text(lbl_time, "00:00");
        lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);
        lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -40);
        
        // DATE
        lbl_date = lv_label_create(main_view);
        lv_label_set_text(lbl_date, "");
        lv_obj_set_style_text_color(lbl_date, lv_color_hex(0xBBBBBB), 0);
        lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 10);
        
        // Indication swipe
        lbl_swipe_hint = lv_label_create(main_view);
        lv_label_set_text(lbl_swipe_hint, LV_SYMBOL_UP " Glisser pour deverrouiller");
        lv_obj_set_style_text_color(lbl_swipe_hint, lv_color_hex(0x888888), 0);
        lv_obj_set_style_text_font(lbl_swipe_hint, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_swipe_hint, LV_ALIGN_BOTTOM_MID, 0, -30);
        
        // VUE PIN créée à la demande (buildPinUI)
        
        // Par défaut caché
        lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);
    }

    void lock() {
        if (is_locked) return;
        is_locked = true;
        
        pin_len = 0;
        memset(pin_input, 0, sizeof(pin_input));
        pin_error = false;
        
        showMainScreen();
        lv_obj_clear_flag(bg, LV_OBJ_FLAG_HIDDEN);
        updateTime();
    }

    void unlock() {
        is_locked = false;
        pin_len = 0;
        memset(pin_input, 0, sizeof(pin_input));
        
        lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);
        showMainScreen();
        lv_disp_trig_activity(NULL);
    }

    bool isLocked() const { return is_locked; }

    void update() {
        uint32_t timeout = settings::getLockTimeout();
        if (timeout > 0 && !is_locked && lv_disp_get_inactive_time(NULL) > timeout) {
            lock();
        }

        if (is_locked && (millis() - last_update > 1000)) {
            updateTime();
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
        time_t now;
        time(&now);
        struct tm* timeinfo = localtime(&now);
        if (!timeinfo) return;
        
        char buf[8];
        strftime(buf, sizeof(buf), "%H:%M", timeinfo);
        lv_label_set_text(lbl_time, buf);
        
        char datebuf[32];
        snprintf(datebuf, sizeof(datebuf), "%s %d %s",
                 get_day_name(timeinfo->tm_wday),
                 timeinfo->tm_mday,
                 get_month_name(timeinfo->tm_mon));
        lv_label_set_text(lbl_date, datebuf);

        const uint8_t p = battery::read_percent();
        char pbuf[8];
        snprintf(pbuf, sizeof(pbuf), "%u%%", (unsigned)p);
        lv_label_set_text(lbl_power, pbuf);
    }
};

#endif