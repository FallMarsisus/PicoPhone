#ifndef SKETCH_APP_H
#define SKETCH_APP_H

#include <Arduino.h>
#include <vector>
#include "App.h"
#include "AppManager.h"

class SketchApp : public App {
private:
    struct Segment {
        lv_obj_t* line;
        lv_point_t* pts;
    };

    std::vector<Segment> segments;
    lv_obj_t* canvas = nullptr;
    lv_obj_t* slider_size = nullptr;
    lv_color_t current_color = lv_color_hex(0xFFFFFF);
    uint8_t brush = 4;

    lv_coord_t prev_x = -1;
    lv_coord_t prev_y = -1;

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void clear_event(lv_event_t* e) {
        SketchApp* self = (SketchApp*)lv_event_get_user_data(e);
        self->clear_canvas();
    }

    static void size_event(lv_event_t* e) {
        SketchApp* self = (SketchApp*)lv_event_get_user_data(e);
        self->brush = (uint8_t)lv_slider_get_value(self->slider_size);
        if (self->brush < 1) self->brush = 1;
    }

    static void color_event(lv_event_t* e) {
        SketchApp* self = (SketchApp*)lv_event_get_user_data(e);
        lv_obj_t* btn = lv_event_get_target(e);
        uint32_t c = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
        self->current_color = lv_color_hex(c);
    }

    void add_segment(lv_coord_t x1, lv_coord_t y1, lv_coord_t x2, lv_coord_t y2) {
        Segment s;
        s.pts = new lv_point_t[2];
        s.pts[0] = {x1, y1};
        s.pts[1] = {x2, y2};

        s.line = lv_line_create(canvas);
        lv_line_set_points(s.line, s.pts, 2);
        lv_obj_set_style_line_color(s.line, current_color, 0);
        lv_obj_set_style_line_width(s.line, brush, 0);
        lv_obj_set_style_line_rounded(s.line, true, 0);
        lv_obj_clear_flag(s.line, LV_OBJ_FLAG_CLICKABLE);

        segments.push_back(s);
    }

    void clear_canvas() {
        for (auto& s : segments) {
            if (s.line) lv_obj_del(s.line);
            delete[] s.pts;
            s.pts = nullptr;
        }
        segments.clear();
    }

    static void draw_event(lv_event_t* e) {
        SketchApp* self = (SketchApp*)lv_event_get_user_data(e);
        lv_event_code_t code = lv_event_get_code(e);

        lv_indev_t* indev = lv_indev_get_act();
        if (!indev) return;

        lv_point_t p;
        lv_indev_get_point(indev, &p);

        lv_area_t a;
        lv_obj_get_coords(self->canvas, &a);
        lv_coord_t x = p.x - a.x1;
        lv_coord_t y = p.y - a.y1;

        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > lv_obj_get_width(self->canvas) - 1) x = lv_obj_get_width(self->canvas) - 1;
        if (y > lv_obj_get_height(self->canvas) - 1) y = lv_obj_get_height(self->canvas) - 1;

        if (code == LV_EVENT_PRESSED) {
            self->prev_x = x;
            self->prev_y = y;
            self->add_segment(x, y, x, y);
            return;
        }

        if (code == LV_EVENT_PRESSING) {
            if (self->prev_x < 0 || self->prev_y < 0) {
                self->prev_x = x;
                self->prev_y = y;
                return;
            }

            if (abs(x - self->prev_x) < 2 && abs(y - self->prev_y) < 2) return;
            self->add_segment(self->prev_x, self->prev_y, x, y);
            self->prev_x = x;
            self->prev_y = y;
            return;
        }

        if (code == LV_EVENT_RELEASED) {
            self->prev_x = -1;
            self->prev_y = -1;
        }
    }

public:
    ~SketchApp() override {
        clear_canvas();
    }

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
        lv_label_set_text(title, "Ardoise");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);

        lv_obj_t* side = lv_obj_create(parent);
        lv_obj_set_size(side, 60, 430);
        lv_obj_align(side, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
        lv_obj_set_style_bg_color(side, lv_color_hex(0x1a1a1a), 0);
        lv_obj_set_style_border_width(side, 0, 0);
        lv_obj_set_style_pad_all(side, 6, 0);
        lv_obj_set_style_pad_gap(side, 8, 0);
        lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(side, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(side, LV_OBJ_FLAG_SCROLLABLE);

        const uint32_t colors[] = {0xFFFFFF, 0xFF3B30, 0x34C759, 0x007AFF, 0xFFD60A, 0xBF5AF2};
        for (uint32_t c : colors) {
            lv_obj_t* b = lv_btn_create(side);
            lv_obj_set_size(b, 34, 34);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(c), 0);
            lv_obj_set_style_border_width(b, 2, 0);
            lv_obj_set_style_border_color(b, lv_color_hex(0x2c2c2e), 0);
            lv_obj_set_user_data(b, (void*)(uintptr_t)c);
            lv_obj_add_event_cb(b, color_event, LV_EVENT_CLICKED, this);
        }

        slider_size = lv_slider_create(side);
        lv_obj_set_size(slider_size, 16, 120);
        lv_obj_set_style_radius(slider_size, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(slider_size, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_slider_set_range(slider_size, 1, 12);
        lv_slider_set_value(slider_size, brush, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider_size, size_event, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t* btn_clear = lv_btn_create(side);
        lv_obj_set_size(btn_clear, 46, 30);
        lv_obj_add_event_cb(btn_clear, clear_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lclr = lv_label_create(btn_clear);
        lv_label_set_text(lclr, "C");
        lv_obj_center(lclr);

        canvas = lv_obj_create(parent);
        lv_obj_set_size(canvas, 258, 430);
        lv_obj_align(canvas, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_obj_set_style_bg_color(canvas, lv_color_hex(0x0b0b0b), 0);
        lv_obj_set_style_border_width(canvas, 0, 0);
        lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(canvas, draw_event, LV_EVENT_ALL, this);
    }

    void stop() override {
        clear_canvas();
    }
};

#endif
