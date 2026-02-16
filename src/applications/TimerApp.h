#ifndef TIMER_APP_H
#define TIMER_APP_H

#include "App.h"
#include "AppManager.h"
#include "../services/TimerService.h"

class TimerApp : public App {
private:
    lv_obj_t* lbl_time = nullptr;
    lv_obj_t* slider = nullptr;
    lv_obj_t* lbl_slider = nullptr;
    lv_obj_t* btn_start = nullptr;
    lv_obj_t* lbl_start = nullptr;
    unsigned long last_ui = 0;

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void slider_event(lv_event_t* e) {
        TimerApp* app = (TimerApp*)lv_event_get_user_data(e);
        if (!app || !app->slider || !app->lbl_slider) return;
        int min_val = lv_slider_get_value(app->slider);
        lv_label_set_text_fmt(app->lbl_slider, "%d min", min_val);
    }

    static void start_stop_event(lv_event_t* e) {
        TimerApp* app = (TimerApp*)lv_event_get_user_data(e);
        if (!app || !app->slider) return;

        auto& svc = timer_service::instance();
        if (svc.isRunning()) {
            svc.stopTimer();
            return;
        }

        int minutes = lv_slider_get_value(app->slider);
        if (minutes < 1) minutes = 1;
        svc.startTimer((uint32_t)minutes * 60U);
    }

    void update_labels() {
        auto& svc = timer_service::instance();
        if (svc.isRunning()) {
            uint32_t rem = svc.remainingSeconds();
            uint32_t mm = rem / 60U;
            uint32_t ss = rem % 60U;
            lv_label_set_text_fmt(lbl_time, "%02u:%02u", (unsigned)mm, (unsigned)ss);
            lv_label_set_text(lbl_start, "Stop");
            lv_obj_set_style_bg_color(btn_start, lv_color_hex(0xFF453A), 0);
        } else {
            int minutes = lv_slider_get_value(slider);
            lv_label_set_text_fmt(lbl_time, "%02d:00", minutes);
            lv_label_set_text(lbl_start, "Start");
            lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x0A84FF), 0);
        }
    }

public:
    void start(lv_obj_t* parent) override {
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* header = lv_obj_create(parent);
        lv_obj_set_size(header, 320, 56);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn_back, 0, 0);
        lv_obj_add_event_cb(btn_back, go_home, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x0A84FF), 0);
        lv_obj_center(lbl_back);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Timer");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_center(title);

        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_size(card, 300, 210);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 84);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lbl_time = lv_label_create(card);
        lv_label_set_text(lbl_time, "05:00");
        lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_28, 0);
        lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 20);

        slider = lv_slider_create(card);
        lv_obj_set_size(slider, 250, 24);
        lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 78);
        lv_slider_set_range(slider, 1, 60);
        lv_slider_set_value(slider, 5, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, slider_event, LV_EVENT_VALUE_CHANGED, this);

        lbl_slider = lv_label_create(card);
        lv_label_set_text(lbl_slider, "5 min");
        lv_obj_set_style_text_color(lbl_slider, lv_color_hex(0xC7C7CC), 0);
        lv_obj_align(lbl_slider, LV_ALIGN_TOP_MID, 0, 108);

        btn_start = lv_btn_create(card);
        lv_obj_set_size(btn_start, 180, 44);
        lv_obj_align(btn_start, LV_ALIGN_BOTTOM_MID, 0, -16);
        lv_obj_set_style_bg_color(btn_start, lv_color_hex(0x0A84FF), 0);
        lv_obj_set_style_border_width(btn_start, 0, 0);
        lv_obj_set_style_radius(btn_start, 12, 0);
        lv_obj_add_event_cb(btn_start, start_stop_event, LV_EVENT_CLICKED, this);

        lbl_start = lv_label_create(btn_start);
        lv_label_set_text(lbl_start, "Start");
        lv_obj_set_style_text_color(lbl_start, lv_color_white(), 0);
        lv_obj_center(lbl_start);

        update_labels();
    }

    void update() override {
        if ((millis() - last_ui) < 200) return;
        last_ui = millis();
        update_labels();
    }
};

#endif
