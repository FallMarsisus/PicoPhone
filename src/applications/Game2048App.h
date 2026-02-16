#ifndef GAME_2048_APP_H
#define GAME_2048_APP_H

#include <Arduino.h>
#include "App.h"
#include "AppManager.h"

class Game2048App : public App {
private:
    int board[4][4] = {{0}};
    lv_obj_t* tile_obj[4][4] = {{nullptr}};
    lv_obj_t* tile_lbl[4][4] = {{nullptr}};
    lv_obj_t* lbl_score = nullptr;
    lv_obj_t* overlay = nullptr;
    lv_obj_t* overlay_text = nullptr;
    int score = 0;

    lv_coord_t touch_start_x = 0;
    lv_coord_t touch_start_y = 0;

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static lv_color_t color_for(int v) {
        switch (v) {
            case 0: return lv_color_hex(0x3a3a3c);
            case 2: return lv_color_hex(0xeee4da);
            case 4: return lv_color_hex(0xede0c8);
            case 8: return lv_color_hex(0xf2b179);
            case 16: return lv_color_hex(0xf59563);
            case 32: return lv_color_hex(0xf67c5f);
            case 64: return lv_color_hex(0xf65e3b);
            case 128: return lv_color_hex(0xedcf72);
            case 256: return lv_color_hex(0xedcc61);
            case 512: return lv_color_hex(0xedc850);
            case 1024: return lv_color_hex(0xedc53f);
            default: return lv_color_hex(0xedc22e);
        }
    }

    void spawn_tile() {
        int empty_count = 0;
        int empties[16][2];
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (board[r][c] == 0) {
                    empties[empty_count][0] = r;
                    empties[empty_count][1] = c;
                    empty_count++;
                }
            }
        }
        if (empty_count == 0) return;
        int idx = random(empty_count);
        int r = empties[idx][0];
        int c = empties[idx][1];
        board[r][c] = (random(10) < 9) ? 2 : 4;
    }

    bool compress_line(int line[4]) {
        bool moved = false;
        int out[4] = {0, 0, 0, 0};
        int j = 0;

        for (int i = 0; i < 4; i++) {
            if (line[i] != 0) {
                if (out[j] == 0) {
                    out[j] = line[i];
                    if (i != j) moved = true;
                } else if (out[j] == line[i]) {
                    out[j] *= 2;
                    score += out[j];
                    j++;
                    moved = true;
                } else {
                    j++;
                    if (j < 4) {
                        out[j] = line[i];
                        if (i != j) moved = true;
                    }
                }
            }
        }

        for (int i = 0; i < 4; i++) line[i] = out[i];
        return moved;
    }

    bool move_left() {
        bool moved = false;
        for (int r = 0; r < 4; r++) {
            int line[4] = {board[r][0], board[r][1], board[r][2], board[r][3]};
            if (compress_line(line)) moved = true;
            for (int c = 0; c < 4; c++) board[r][c] = line[c];
        }
        return moved;
    }

    bool move_right() {
        bool moved = false;
        for (int r = 0; r < 4; r++) {
            int line[4] = {board[r][3], board[r][2], board[r][1], board[r][0]};
            if (compress_line(line)) moved = true;
            board[r][3] = line[0]; board[r][2] = line[1]; board[r][1] = line[2]; board[r][0] = line[3];
        }
        return moved;
    }

    bool move_up() {
        bool moved = false;
        for (int c = 0; c < 4; c++) {
            int line[4] = {board[0][c], board[1][c], board[2][c], board[3][c]};
            if (compress_line(line)) moved = true;
            for (int r = 0; r < 4; r++) board[r][c] = line[r];
        }
        return moved;
    }

    bool move_down() {
        bool moved = false;
        for (int c = 0; c < 4; c++) {
            int line[4] = {board[3][c], board[2][c], board[1][c], board[0][c]};
            if (compress_line(line)) moved = true;
            board[3][c] = line[0]; board[2][c] = line[1]; board[1][c] = line[2]; board[0][c] = line[3];
        }
        return moved;
    }

    bool has_moves() {
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                if (board[r][c] == 0) return true;
                if (r < 3 && board[r][c] == board[r + 1][c]) return true;
                if (c < 3 && board[r][c] == board[r][c + 1]) return true;
            }
        }
        return false;
    }

    void update_ui() {
        char sbuf[24];
        snprintf(sbuf, sizeof(sbuf), "Score: %d", score);
        lv_label_set_text(lbl_score, sbuf);

        bool has_2048 = false;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                int v = board[r][c];
                lv_obj_set_style_bg_color(tile_obj[r][c], color_for(v), 0);
                if (v == 0) {
                    lv_label_set_text(tile_lbl[r][c], "");
                } else {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%d", v);
                    lv_label_set_text(tile_lbl[r][c], buf);
                    if (v >= 8) lv_obj_set_style_text_color(tile_lbl[r][c], lv_color_white(), 0);
                    else lv_obj_set_style_text_color(tile_lbl[r][c], lv_color_hex(0x4a4a4a), 0);
                }
                if (v >= 2048) has_2048 = true;
            }
        }

        if (has_2048) {
            lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(overlay_text, "2048 !");
        } else if (!has_moves()) {
            lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(overlay_text, "Perdu");
        } else {
            lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void reset_game() {
        memset(board, 0, sizeof(board));
        score = 0;
        spawn_tile();
        spawn_tile();
        update_ui();
    }

    static void board_touch_event(lv_event_t* e) {
        Game2048App* self = (Game2048App*)lv_event_get_user_data(e);
        lv_event_code_t code = lv_event_get_code(e);

        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_get_act();
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            self->touch_start_x = p.x;
            self->touch_start_y = p.y;
            return;
        }

        if (code != LV_EVENT_RELEASED) return;

        lv_indev_t* indev = lv_indev_get_act();
        lv_point_t p;
        lv_indev_get_point(indev, &p);

        lv_coord_t dx = p.x - self->touch_start_x;
        lv_coord_t dy = p.y - self->touch_start_y;
        if (abs(dx) < 25 && abs(dy) < 25) return;

        bool moved = false;
        if (abs(dx) > abs(dy)) {
            if (dx > 0) moved = self->move_right();
            else moved = self->move_left();
        } else {
            if (dy > 0) moved = self->move_down();
            else moved = self->move_up();
        }

        if (moved) self->spawn_tile();
        self->update_ui();
    }

    static void restart_event(lv_event_t* e) {
        Game2048App* self = (Game2048App*)lv_event_get_user_data(e);
        self->reset_game();
    }

public:
    void start(lv_obj_t* parent) override {
        randomSeed(millis());
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x111111), 0);
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
        lv_label_set_text(title, "2048");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);

        lv_obj_t* btn_restart = lv_btn_create(header);
        lv_obj_set_size(btn_restart, 68, 34);
        lv_obj_align(btn_restart, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(btn_restart, restart_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_res = lv_label_create(btn_restart);
        lv_label_set_text(l_res, "Reset");
        lv_obj_center(l_res);

        lbl_score = lv_label_create(parent);
        lv_label_set_text(lbl_score, "Score: 0");
        lv_obj_set_style_text_color(lbl_score, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_score, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_score, LV_ALIGN_TOP_MID, 0, 58);

        lv_obj_t* board_wrap = lv_obj_create(parent);
        lv_obj_set_size(board_wrap, 296, 296);
        lv_obj_align(board_wrap, LV_ALIGN_CENTER, 0, 24);
        lv_obj_set_style_bg_color(board_wrap, lv_color_hex(0x2b2b2d), 0);
        lv_obj_set_style_radius(board_wrap, 12, 0);
        lv_obj_set_style_border_width(board_wrap, 0, 0);
        lv_obj_set_style_pad_all(board_wrap, 8, 0);
        lv_obj_clear_flag(board_wrap, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(board_wrap, board_touch_event, LV_EVENT_ALL, this);

        const int tile = 64;
        const int gap = 8;
        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                tile_obj[r][c] = lv_obj_create(board_wrap);
                lv_obj_set_size(tile_obj[r][c], tile, tile);
                lv_obj_set_pos(tile_obj[r][c], c * (tile + gap), r * (tile + gap));
                lv_obj_set_style_radius(tile_obj[r][c], 8, 0);
                lv_obj_set_style_border_width(tile_obj[r][c], 0, 0);
                lv_obj_clear_flag(tile_obj[r][c], LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_clear_flag(tile_obj[r][c], LV_OBJ_FLAG_CLICKABLE);

                tile_lbl[r][c] = lv_label_create(tile_obj[r][c]);
                lv_obj_set_style_text_font(tile_lbl[r][c], &lv_font_montserrat_14, 0);
                lv_obj_center(tile_lbl[r][c]);
            }
        }

        overlay = lv_obj_create(board_wrap);
        lv_obj_set_size(overlay, 280, 280);
        lv_obj_align(overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_40, 0);
        lv_obj_set_style_border_width(overlay, 0, 0);
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

        overlay_text = lv_label_create(overlay);
        lv_obj_set_style_text_font(overlay_text, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(overlay_text, lv_color_white(), 0);
        lv_obj_center(overlay_text);

        reset_game();
    }
};

#endif
