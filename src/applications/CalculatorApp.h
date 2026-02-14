#ifndef CALCULATOR_APP_H
#define CALCULATOR_APP_H

#include <Arduino.h>
#include <stdlib.h>
#include "App.h"
#include "AppManager.h"

class CalculatorApp : public App {
private:
    lv_obj_t* display = nullptr;
    char expr[64] = {0};

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void key_event(lv_event_t* e) {
        CalculatorApp* self = (CalculatorApp*)lv_event_get_user_data(e);
        const char* txt = lv_btnmatrix_get_btn_text((lv_obj_t*)lv_event_get_target(e), lv_btnmatrix_get_selected_btn((lv_obj_t*)lv_event_get_target(e)));
        if (!txt) return;

        if (strcmp(txt, "C") == 0) {
            self->expr[0] = '\0';
            lv_label_set_text(self->display, "0");
            return;
        }

        if (strcmp(txt, "=") == 0) {
            self->evalExpression();
            return;
        }

        if (strlen(self->expr) >= sizeof(self->expr) - 2) return;

        if (strcmp(self->expr, "0") == 0 && txt[0] >= '0' && txt[0] <= '9') {
            self->expr[0] = '\0';
        }

        strncat(self->expr, txt, 1);
        lv_label_set_text(self->display, self->expr[0] ? self->expr : "0");
    }

    void evalExpression() {
        const char* op = nullptr;
        for (size_t i = 1; i < strlen(expr); i++) {
            if (expr[i] == '+' || expr[i] == '-' || expr[i] == 'x' || expr[i] == '/') {
                op = &expr[i];
                break;
            }
        }

        if (!op) return;

        char left[32] = {0};
        char right[32] = {0};
        size_t left_len = (size_t)(op - expr);
        if (left_len >= sizeof(left)) left_len = sizeof(left) - 1;
        strncpy(left, expr, left_len);
        strncpy(right, op + 1, sizeof(right) - 1);

        double a = atof(left);
        double b = atof(right);
        double r = 0.0;

        if (*op == '+') r = a + b;
        else if (*op == '-') r = a - b;
        else if (*op == 'x') r = a * b;
        else if (*op == '/') {
            if (b == 0.0) {
                lv_label_set_text(display, "Erreur");
                expr[0] = '\0';
                return;
            }
            r = a / b;
        }

        char out[32];
        if ((long)r == r) snprintf(out, sizeof(out), "%ld", (long)r);
        else snprintf(out, sizeof(out), "%.3f", r);

        strncpy(expr, out, sizeof(expr) - 1);
        expr[sizeof(expr) - 1] = '\0';
        lv_label_set_text(display, out);
    }

public:
    void start(lv_obj_t* parent) override {
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* header = lv_obj_create(parent);
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
        lv_label_set_text(title, "Calculatrice");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_center(title);

        lv_obj_t* display_wrap = lv_obj_create(parent);
        lv_obj_set_size(display_wrap, 300, 90);
        lv_obj_align(display_wrap, LV_ALIGN_TOP_MID, 0, 62);
        lv_obj_set_style_bg_color(display_wrap, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_radius(display_wrap, 14, 0);
        lv_obj_set_style_border_width(display_wrap, 0, 0);
        lv_obj_clear_flag(display_wrap, LV_OBJ_FLAG_SCROLLABLE);

        display = lv_label_create(display_wrap);
        lv_label_set_text(display, "0");
        lv_obj_set_width(display, 280);
        lv_obj_set_style_text_align(display, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(display, lv_color_white(), 0);
        lv_obj_set_style_text_font(display, &lv_font_montserrat_28, 0);
        lv_obj_align(display, LV_ALIGN_RIGHT_MID, -8, 0);

        static const char* btn_map[] = {
            "7", "8", "9", "/", "\n",
            "4", "5", "6", "x", "\n",
            "1", "2", "3", "-", "\n",
            "C", "0", "=", "+", ""
        };

        lv_obj_t* pad = lv_btnmatrix_create(parent);
        lv_obj_set_size(pad, 300, 300);
        lv_obj_align(pad, LV_ALIGN_BOTTOM_MID, 0, -14);
        lv_btnmatrix_set_map(pad, btn_map);
        lv_obj_set_style_bg_color(pad, lv_color_hex(0x111111), 0);
        lv_obj_set_style_border_width(pad, 0, 0);
        lv_obj_set_style_pad_all(pad, 6, 0);
        lv_obj_set_style_pad_gap(pad, 6, 0);
        lv_obj_set_style_radius(pad, 14, 0);

        lv_obj_set_style_bg_color(pad, lv_color_hex(0x2c2c2e), LV_PART_ITEMS);
        lv_obj_set_style_text_color(pad, lv_color_white(), LV_PART_ITEMS);
        lv_obj_set_style_radius(pad, 10, LV_PART_ITEMS);

        lv_obj_add_event_cb(pad, key_event, LV_EVENT_VALUE_CHANGED, this);
    }
};

#endif
