#ifdef __cplusplus
extern "C" {
#endif
extern void i2s_play_test_tone(int freq, int duration_ms, float gain);
#ifdef __cplusplus
}
#endif
#ifndef SYSTEM_NOTIFICATION_CENTER_H
#define SYSTEM_NOTIFICATION_CENTER_H

#include <Arduino.h>
#include "Hardware.h"
#include "Settings.h"
#include <lvgl.h>

class NotificationCenter {
private:
    struct Item {
        char app[20];
        char title[36];
        char body[96];
    };

    static constexpr uint8_t QUEUE_SIZE = 12;
    volatile uint8_t head = 0;
    volatile uint8_t tail = 0;
    Item queue[QUEUE_SIZE]{};

    // Historique pour le Control Center
    Item history[2]{};
    uint8_t history_count = 0;

    lv_obj_t* root = nullptr;
    lv_obj_t* lbl_app = nullptr;
    lv_obj_t* lbl_title = nullptr;
    lv_obj_t* lbl_body = nullptr;
    bool ui_ready = false;
    bool showing = false;
    unsigned long show_since = 0;

    static void copy_str(char* dst, size_t size, const char* src) {
        if (!dst || size == 0) return;
        if (!src) {
            dst[0] = '\0';
            return;
        }
        strncpy(dst, src, size - 1);
        dst[size - 1] = '\0';
    }

    bool pop(Item& out) {
        const uint8_t h = __atomic_load_n(&head, __ATOMIC_RELAXED);
        const uint8_t t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
        if (h == t) return false;
        out = queue[h];
        __atomic_store_n(&head, (uint8_t)((h + 1) % QUEUE_SIZE), __ATOMIC_RELEASE);
        return true;
    }

    void ensure_ui() {
        if (ui_ready) return;

        lv_disp_t* disp = lv_disp_get_default();
        lv_coord_t screen_w = disp ? lv_disp_get_hor_res(disp) : 320;
        lv_coord_t panel_w = screen_w - 16;
        if (panel_w < 240) panel_w = 240;

        root = lv_obj_create(lv_layer_top());
        lv_obj_set_size(root, panel_w, 78);
        lv_obj_align(root, LV_ALIGN_TOP_MID, 0, 22);
        lv_obj_set_style_bg_color(root, lv_color_hex(0x101A2C), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_90, 0);
        lv_obj_set_style_radius(root, 16, 0);
        lv_obj_set_style_border_width(root, 1, 0);
        lv_obj_set_style_border_color(root, lv_color_hex(0x2E4368), 0);
        lv_obj_set_style_border_opa(root, LV_OPA_90, 0);
        lv_obj_set_style_pad_all(root, 10, 0);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

        lbl_app = lv_label_create(root);
        lv_obj_set_style_text_color(lbl_app, lv_color_hex(0x0A84FF), 0);
        lv_obj_set_style_text_font(lbl_app, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_app, LV_ALIGN_TOP_LEFT, 0, 0);

        lbl_title = lv_label_create(root);
        lv_obj_set_width(lbl_title, panel_w - 20);
        lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 18);

        lbl_body = lv_label_create(root);
        lv_obj_set_width(lbl_body, panel_w - 20);
        lv_label_set_long_mode(lbl_body, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(lbl_body, lv_color_hex(0xC7C7CC), 0);
        lv_obj_set_style_text_font(lbl_body, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_body, LV_ALIGN_TOP_LEFT, 0, 42);

        ui_ready = true;    
    }

    void show_item(const Item& it) {
        ensure_ui();
        lv_label_set_text(lbl_app, it.app[0] ? it.app : "System");
        lv_label_set_text(lbl_title, it.title[0] ? it.title : "Notification");
        lv_label_set_text(lbl_body, it.body);
        lv_obj_move_foreground(root);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        showing = true;
        show_since = millis();
    }

public:
    void push(const char* app, const char* title, const char* body) {
        const uint8_t t = __atomic_load_n(&tail, __ATOMIC_RELAXED);
        const uint8_t next = (uint8_t)((t + 1) % QUEUE_SIZE);
        const uint8_t h = __atomic_load_n(&head, __ATOMIC_ACQUIRE);

        if (next == h) {
            __atomic_store_n(&head, (uint8_t)((h + 1) % QUEUE_SIZE), __ATOMIC_RELEASE);
        }

        Item it{};
        copy_str(it.app, sizeof(it.app), app);
        copy_str(it.title, sizeof(it.title), title);
        copy_str(it.body, sizeof(it.body), body);
        
        queue[t] = it;
        __atomic_store_n(&tail, next, __ATOMIC_RELEASE);

        // Sauvegarde dans l'historique pour le Centre de Contrôle
        history[1] = history[0];
        history[0] = it;
        if (history_count < 2) history_count++;

        i2s_play_test_tone(1200, 30, settings::getVolume()/100.0f);
        sleep_ms(20);
        i2s_play_test_tone(1200, 30, settings::getVolume()/100.0f);
    }

    void update() {
        ensure_ui();

        if (showing) {
            if ((millis() - show_since) > 2800) {
                lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
                showing = false;
            } else {
                return;
            }
        }

        Item it{};
        if (pop(it)) {
            show_item(it);
        }
    }

    // Nouvelle fonction pour le Control Center
    bool get_latest(uint8_t index, char* out_app, char* out_title, char* out_body) {
        if (index >= history_count) return false;
        copy_str(out_app, 20, history[index].app);
        copy_str(out_title, 36, history[index].title);
        copy_str(out_body, 96, history[index].body);
        return true;
    }
};

namespace notifications {
    inline NotificationCenter& center() {
        static NotificationCenter c;
        return c;
    }

    inline bool latest(uint8_t index, char* out_app, char* out_title, char* out_body) {
        return center().get_latest(index, out_app, out_title, out_body);
    }

    inline void push(const char* app, const char* title, const char* body) {
        center().push(app, title, body);
    }
}

#endif