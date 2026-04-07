#ifndef TIMER_APP_H
#define TIMER_APP_H

#include "App.h"
#include "AppManager.h"
#include "../services/TimerService.h"
#include <time.h>

class TimerApp : public App {
private:
    enum ViewMode {
        VIEW_CLOCK,
        VIEW_TIMER
    };

    ViewMode active_view = VIEW_CLOCK;

    lv_obj_t* main_bg = nullptr;
    lv_obj_t* clock_view = nullptr;
    lv_obj_t* timer_view = nullptr;
    lv_obj_t* footer = nullptr;
    lv_obj_t* btn_clock = nullptr;
    lv_obj_t* btn_timer = nullptr;
    lv_obj_t* clock_label = nullptr;
    lv_obj_t* date_label = nullptr;
    lv_obj_t* clock_subtitle = nullptr;

    lv_obj_t* lbl_time = nullptr;
    lv_obj_t* slider = nullptr;
    lv_obj_t* lbl_slider = nullptr;
    lv_obj_t* btn_start = nullptr;
    lv_obj_t* lbl_start = nullptr;
    unsigned long last_ui = 0;
    unsigned long last_clock_update = 0;

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void tab_clock_event(lv_event_t* e) {
        TimerApp* app = (TimerApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->set_view(VIEW_CLOCK);
    }

    static void tab_timer_event(lv_event_t* e) {
        TimerApp* app = (TimerApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->set_view(VIEW_TIMER);
    }

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

    static String format_time_now() {
        time_t now = time(nullptr);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
        return String(buf);
    }

    static String format_date_now() {
        time_t now = time(nullptr);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        static const char* days[] = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};
        static const char* months[] = {"Jan", "Fev", "Mar", "Avr", "Mai", "Jun", "Jul", "Aou", "Sep", "Oct", "Nov", "Dec"};
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %02d %s", days[tm_now.tm_wday], tm_now.tm_mday, months[tm_now.tm_mon]);
        return String(buf);
    }

    void set_view(ViewMode view) {
        active_view = view;
        if (clock_view) {
            if (view == VIEW_CLOCK) lv_obj_clear_flag(clock_view, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(clock_view, LV_OBJ_FLAG_HIDDEN);
        }
        if (timer_view) {
            if (view == VIEW_TIMER) lv_obj_clear_flag(timer_view, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(timer_view, LV_OBJ_FLAG_HIDDEN);
        }

        const uint32_t selected = 0x2C2C2E;
        const uint32_t unselected = 0x101114;
        const uint32_t selected_fg = 0xFFFFFF;
        const uint32_t unselected_fg = 0x8E8E93;

        if (btn_clock) {
            lv_obj_set_style_bg_color(btn_clock, lv_color_hex(view == VIEW_CLOCK ? selected : unselected), 0);
            lv_obj_set_style_bg_opa(btn_clock, LV_OPA_COVER, 0);
        }
        if (btn_timer) {
            lv_obj_set_style_bg_color(btn_timer, lv_color_hex(view == VIEW_TIMER ? selected : unselected), 0);
            lv_obj_set_style_bg_opa(btn_timer, LV_OPA_COVER, 0);
        }

        if (clock_label) lv_obj_set_style_text_color(clock_label, lv_color_hex(view == VIEW_CLOCK ? selected_fg : unselected_fg), 0);
        if (date_label) lv_obj_set_style_text_color(date_label, lv_color_hex(view == VIEW_CLOCK ? selected_fg : unselected_fg), 0);
        if (clock_subtitle) lv_obj_set_style_text_color(clock_subtitle, lv_color_hex(view == VIEW_CLOCK ? selected_fg : unselected_fg), 0);
        if (lbl_start) lv_obj_set_style_text_color(lbl_start, lv_color_white(), 0);
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* header = lv_obj_create(main_bg);
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
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_center(title);

        clock_view = lv_obj_create(main_bg);
        lv_obj_set_size(clock_view, 320, 360);
        lv_obj_align(clock_view, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_set_style_bg_opa(clock_view, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(clock_view, 0, 0);
        lv_obj_clear_flag(clock_view, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* clock_card = lv_obj_create(clock_view);
        lv_obj_set_size(clock_card, 300, 210);
        lv_obj_align(clock_card, LV_ALIGN_TOP_MID, 0, 20);
        lv_obj_set_style_bg_color(clock_card, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_border_width(clock_card, 0, 0);
        lv_obj_set_style_radius(clock_card, 20, 0);
        lv_obj_clear_flag(clock_card, LV_OBJ_FLAG_SCROLLABLE);

        clock_label = lv_label_create(clock_card);
        lv_label_set_text(clock_label, "00:00");
        lv_obj_set_style_text_color(clock_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(clock_label, &lv_font_montserrat_28, 0);
        lv_obj_align(clock_label, LV_ALIGN_CENTER, 0, -25);

        date_label = lv_label_create(clock_card);
        lv_label_set_text(date_label, "--");
        lv_obj_set_style_text_color(date_label, lv_color_hex(0xC7C7CC), 0);
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_14, 0);
        lv_obj_align(date_label, LV_ALIGN_CENTER, 0, 15);

        clock_subtitle = lv_label_create(clock_card);
        lv_label_set_text(clock_subtitle, "Horloge");
        lv_obj_set_style_text_color(clock_subtitle, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(clock_subtitle, &lv_font_montserrat_12, 0);
        lv_obj_align(clock_subtitle, LV_ALIGN_BOTTOM_MID, 0, -16);

        timer_view = lv_obj_create(main_bg);
        lv_obj_set_size(timer_view, 320, 360);
        lv_obj_align(timer_view, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_set_style_bg_opa(timer_view, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(timer_view, 0, 0);
        lv_obj_clear_flag(timer_view, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* card = lv_obj_create(timer_view);
        lv_obj_set_size(card, 300, 210);
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

        footer = lv_obj_create(main_bg);
        lv_obj_set_size(footer, 304, 68);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_bg_color(footer, lv_color_hex(0x101114), 0);
        lv_obj_set_style_bg_opa(footer, LV_OPA_90, 0);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_set_style_radius(footer, 22, 0);
        lv_obj_set_style_pad_all(footer, 6, 0);
        lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

        btn_clock = lv_btn_create(footer);
        lv_obj_set_size(btn_clock, 130, 56);
        lv_obj_align(btn_clock, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_add_event_cb(btn_clock, tab_clock_event, LV_EVENT_CLICKED, this);
        lv_obj_set_style_bg_opa(btn_clock, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_clock, 0, 0);
        lv_obj_set_style_radius(btn_clock, 18, 0);
        lv_obj_set_style_pad_all(btn_clock, 0, 0);
        lv_obj_set_flex_flow(btn_clock, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn_clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* icon_clock = lv_label_create(btn_clock);
        lv_label_set_text(icon_clock, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(icon_clock, &lv_font_montserrat_14, 0);
        lv_obj_t* lbl_clock = lv_label_create(btn_clock);
        lv_label_set_text(lbl_clock, "Horloge");
        lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_12, 0);

        btn_timer = lv_btn_create(footer);
        lv_obj_set_size(btn_timer, 130, 56);
        lv_obj_align(btn_timer, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_add_event_cb(btn_timer, tab_timer_event, LV_EVENT_CLICKED, this);
        lv_obj_set_style_bg_opa(btn_timer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(btn_timer, 0, 0);
        lv_obj_set_style_radius(btn_timer, 18, 0);
        lv_obj_set_style_pad_all(btn_timer, 0, 0);
        lv_obj_set_flex_flow(btn_timer, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn_timer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* icon_timer = lv_label_create(btn_timer);
        lv_label_set_text(icon_timer, LV_SYMBOL_BELL);
        lv_obj_set_style_text_font(icon_timer, &lv_font_montserrat_14, 0);
        lv_obj_t* lbl_timer = lv_label_create(btn_timer);
        lv_label_set_text(lbl_timer, "Timer");
        lv_obj_set_style_text_font(lbl_timer, &lv_font_montserrat_12, 0);

        set_view(VIEW_CLOCK);
        update_labels();
    }

    void update() override {
        if ((millis() - last_clock_update) >= 1000) {
            last_clock_update = millis();
            if (clock_label) lv_label_set_text(clock_label, format_time_now().c_str());
            if (date_label) lv_label_set_text(date_label, format_date_now().c_str());
        }

        if ((millis() - last_ui) < 200) return;
        last_ui = millis();
        update_labels();
    }
};

#endif
