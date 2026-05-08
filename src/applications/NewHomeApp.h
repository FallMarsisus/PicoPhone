#ifndef NEW_HOME_APP_H
#define NEW_HOME_APP_H

#include <Arduino.h>
#include <time.h>
#include <vector>
#include "App.h"
#include "AppManager.h"
#include "../system/Battery.h"
#include "../system/Settings.h"
#include "../system/LTE.h"
#include "../system/HomeConfig.h"
#include "./PythonApp.h"

LV_IMG_DECLARE(fondecran);

class NewHomeApp : public App {
private:
    lv_obj_t* main_bg = nullptr;
    lv_obj_t* bg_img = nullptr;
    
    
    lv_obj_t* quick_actions_cont = nullptr;
    lv_obj_t* color_selector_overlay = nullptr;
    lv_obj_t* color_selector_sheet = nullptr;

    lv_obj_t *app_page_cont = nullptr;
    lv_obj_t *page_indicator_cont = nullptr;
    int total_pages = 0;
    int active_page = 0;

    static void on_scroll_end(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        lv_obj_t* cont = lv_event_get_target(e);
        lv_coord_t scroll_x = lv_obj_get_scroll_x(cont);
        lv_coord_t w = lv_obj_get_width(cont);
        if (w > 0) app->active_page = (scroll_x + w / 2) / w;
        app->updatePageIndicators();
    }

    void updatePageIndicators() {
        if (!page_indicator_cont) return;
        for(uint32_t j=0; j<lv_obj_get_child_cnt(page_indicator_cont); j++) {
            lv_obj_t* dot = lv_obj_get_child(page_indicator_cont, j);
            if (j == active_page) {
                lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_bg_color(dot, lv_color_hex(0x888888), 0);
                lv_obj_set_style_bg_opa(dot, LV_OPA_50, 0);
            }
        }
    }
       
    lv_obj_t *nav_dots_label = nullptr;      
    
    bool page_animating = false;
    int page_slide_dir = 0;
    
    lv_point_t touch_start_point;
    bool press_started_top = false;
    bool long_press_consumed = false;
    bool press_active = false;
    bool long_press_armed = false;
    unsigned long press_start_ms = 0;
    static constexpr unsigned long LONG_PRESS_DELAY_MS = 850;
    static constexpr lv_coord_t LONG_PRESS_MOVE_LIMIT = 12;

    uint32_t current_bg_color = 0x408A71;
    lv_color_t bg_anim_from;
    lv_color_t bg_anim_to;

    int current_page = 0;
    static const int GRID_COLS = 4;
    static const int GRID_ROWS = 3;
    static const int ITEMS_PER_PAGE = GRID_COLS * GRID_ROWS;

    std::vector<HomeAppEntry> loaded_apps;
    int current_folder_index = -1; 

    lv_style_transition_dsc_t btn_trans;

    static String buildPythonInitials(const String& name, const String& fallbackId) {
        String source = name.length() > 0 ? name : fallbackId;
        source.trim();
        if (source.length() == 0) return "?";

        auto isAsciiAlphaNum = [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        };
        auto toUpperAscii = [](char c) {
            if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
            return c;
        };

        String initials;
        bool word_start = true;
        for (int i = 0; i < source.length() && initials.length() < 2; ++i) {
            char c = source.charAt(i);
            if (isAsciiAlphaNum(c)) {
                if (word_start) {
                    initials += toUpperAscii(c);
                    word_start = false;
                }
            } else {
                word_start = true;
            }
        }
        return initials.length() == 0 ? "?" : initials;
    }

    static void app_click_cb(lv_event_t* e) {
        HomeAppEntry* entry = (HomeAppEntry*)lv_event_get_user_data(e);
        if (!entry) return;
        if (entry->appId >= 0) {
            AppManager::switchTo((AppID)entry->appId);
        } else if (entry->pythonPath.length() > 0) {
            PythonApp::queueScriptFromFile(entry->pythonPath);
            AppManager::switchTo(APP_PYTHON_TEST);
        }
    }

    static void folder_click_cb(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
        if (idx < 0 || idx >= (int)app->loaded_apps.size()) return;
        if (!app->loaded_apps[idx].isFolder()) return;
        app->current_folder_index = idx;
        app->current_page = 0;
        app->renderCurrentPage(1); 
    }

    static void folder_back_cb(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        app->current_folder_index = -1;
        app->current_page = 0;
        app->renderCurrentPage(-1); 
    }

    static void open_telegram(lv_event_t* e) { AppManager::switchTo(APP_TELEGRAM); }
    static void open_weather(lv_event_t* e) { AppManager::switchTo(APP_WEATHER); }
    static void open_velib(lv_event_t* e) { AppManager::switchTo(APP_VELIB); }

    static void anim_y_cb(void * var, int32_t v) {
        lv_obj_set_style_translate_y((lv_obj_t*)var, v, 0);
    }

    static void anim_x_cb(void * var, int32_t v) {
        lv_obj_set_style_translate_x((lv_obj_t*)var, v, 0);
    }

    void pollLongPress() {
        if (!press_active || !long_press_armed || long_press_consumed) {
            return;
        }

        if (millis() - press_start_ms < LONG_PRESS_DELAY_MS) return;

        lv_coord_t diff_x = 0;
        lv_coord_t diff_y = 0;
        if (touch_start_point.y >= 0) {
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_point_t now_point;
                lv_indev_get_point(indev, &now_point);
                diff_x = now_point.x - touch_start_point.x;
                diff_y = now_point.y - touch_start_point.y;
            }
        }

        if (abs(diff_x) <= LONG_PRESS_MOVE_LIMIT && abs(diff_y) <= LONG_PRESS_MOVE_LIMIT) {
            long_press_consumed = true;
            long_press_armed = false;
            showColorSelector();
        }
    }

    static void page_slide_in_done_cb(lv_anim_t* a) {
        NewHomeApp* app = (NewHomeApp*)lv_anim_get_user_data(a);
        if (app) app->page_animating = false;
    }

    static void page_slide_out_done_cb(lv_anim_t* a) {
        NewHomeApp* app = (NewHomeApp*)lv_anim_get_user_data(a);
        if (!app || !app->app_page_cont) return;

        int dir = (app->page_slide_dir >= 0) ? 1 : -1;
        app->renderCurrentPage(0);
        lv_obj_set_style_translate_x(app->app_page_cont, (dir > 0) ? 320 : -320, 0);

        lv_anim_t in;
        lv_anim_init(&in);
        lv_anim_set_var(&in, app->app_page_cont);
        lv_anim_set_exec_cb(&in, (lv_anim_exec_xcb_t)anim_x_cb);
        lv_anim_set_time(&in, 350);  
        lv_anim_set_path_cb(&in, lv_anim_path_ease_out);
        lv_anim_set_values(&in, (dir > 0) ? 320 : -320, 0);
        lv_anim_set_user_data(&in, app);
        lv_anim_set_ready_cb(&in, page_slide_in_done_cb);
        lv_anim_start(&in);
    }

    static void anim_bg_color_cb(void* var, int32_t v) {
        NewHomeApp* app = (NewHomeApp*)var;
        if (!app || !app->main_bg) return;
        lv_color_t mix = lv_color_mix(app->bg_anim_to, app->bg_anim_from, (uint8_t)v);
        lv_obj_set_style_bg_color(app->main_bg, mix, 0);
    }

    void closeColorSelector() {
        if (!color_selector_overlay) return;
        lv_obj_t* to_delete = color_selector_overlay;
        color_selector_overlay = nullptr;
        color_selector_sheet = nullptr;
        lv_obj_del_async(to_delete);
    }

    void applyBackgroundColor(uint32_t new_color, bool animated) {
        if (!main_bg) {
            current_bg_color = new_color;
            return;
        }

        if (current_bg_color == new_color) {
            lv_obj_set_style_bg_color(main_bg, lv_color_hex(new_color), 0);
            return;
        }

        if (!animated) {
            current_bg_color = new_color;
            lv_obj_set_style_bg_color(main_bg, lv_color_hex(new_color), 0);
            return;
        }

        bg_anim_from = lv_color_hex(current_bg_color);
        bg_anim_to = lv_color_hex(new_color);
        current_bg_color = new_color;

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, this);
        lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)anim_bg_color_cb);
        lv_anim_set_values(&a, 0, 255);
        lv_anim_set_time(&a, 300);  
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }

    static void color_selector_close_cb(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        if (app) app->closeColorSelector();
    }

    static void color_pick_cb(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        if (!app) return;

        lv_obj_t* target = lv_event_get_target(e);
        uint32_t color = (uint32_t)(uintptr_t)lv_obj_get_user_data(target);
        app->applyBackgroundColor(color, true);
        homeConfig::setBackgroundColor(color);
        homeConfig::saveConfig(app->loaded_apps);
        app->closeColorSelector();
    }

    void showColorSelector() {
        if (!main_bg) return;
        if (color_selector_overlay) {
            lv_obj_move_foreground(color_selector_overlay);
            return;
        }

        color_selector_overlay = lv_obj_create(main_bg);
        lv_obj_set_size(color_selector_overlay, 320, 480);
        lv_obj_set_pos(color_selector_overlay, 0, 0);
        lv_obj_set_style_bg_color(color_selector_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(color_selector_overlay, LV_OPA_50, 0);
        lv_obj_set_style_border_width(color_selector_overlay, 0, 0);
        lv_obj_set_style_radius(color_selector_overlay, 0, 0);
        lv_obj_clear_flag(color_selector_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(color_selector_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(color_selector_overlay, color_selector_close_cb, LV_EVENT_CLICKED, this);

        color_selector_sheet = lv_obj_create(color_selector_overlay);
        lv_obj_set_size(color_selector_sheet, 296, 240);
        lv_obj_align(color_selector_sheet, LV_ALIGN_BOTTOM_MID, 0, -14);
        
        lv_obj_set_style_bg_color(color_selector_sheet, lv_color_hex(0x121A29), 0);
        lv_obj_set_style_bg_opa(color_selector_sheet, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(color_selector_sheet, 1, 0);
        lv_obj_set_style_border_color(color_selector_sheet, lv_color_hex(0x2F4369), 0);
        lv_obj_set_style_radius(color_selector_sheet, 22, 0);
        lv_obj_set_style_shadow_width(color_selector_sheet, 0, 0);

        lv_obj_set_style_pad_all(color_selector_sheet, 14, 0);
        lv_obj_set_style_pad_row(color_selector_sheet, 10, 0);
        lv_obj_set_style_pad_column(color_selector_sheet, 10, 0);
        lv_obj_set_layout(color_selector_sheet, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(color_selector_sheet, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(color_selector_sheet, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

        lv_obj_t* title = lv_label_create(color_selector_sheet);
        lv_label_set_text(title, "Couleur fond d'ecran");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_set_width(title, 260);

        static const uint32_t kPalette[] = {
            0x408A71, 0x2E3A87, 0x6942A8, 0xB04A5A,
            0xC7782D, 0x2B8F9E, 0x3C7A3F, 0x4C4F59
        };

        for (uint32_t col : kPalette) {
            lv_obj_t* chip = lv_btn_create(color_selector_sheet);
            lv_obj_set_size(chip, 58, 58);
            lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(chip, lv_color_hex(col), 0);
            lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(chip, (col == current_bg_color) ? 3 : 1, 0);
            lv_obj_set_style_border_color(chip, lv_color_white(), 0);
            lv_obj_set_style_border_opa(chip, (col == current_bg_color) ? LV_OPA_90 : LV_OPA_30, 0);
            lv_obj_set_user_data(chip, (void*)(uintptr_t)col);
            lv_obj_add_event_cb(chip, color_pick_cb, LV_EVENT_CLICKED, this);
        }

        lv_obj_t* close_btn = lv_btn_create(color_selector_sheet);
        lv_obj_set_size(close_btn, 120, 40);
        lv_obj_set_style_radius(close_btn, 12, 0);
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x1C2639), 0);
        lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(close_btn, 1, 0);
        lv_obj_set_style_border_color(close_btn, lv_color_hex(0x3B4D70), 0);
        lv_obj_add_event_cb(close_btn, color_selector_close_cb, LV_EVENT_CLICKED, this);
        
        lv_obj_t* close_lbl = lv_label_create(close_btn);
        lv_label_set_text(close_lbl, "Fermer");
        lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
        lv_obj_center(close_lbl);

        lv_obj_move_foreground(color_selector_sheet);
    }

    void prev_page() {
        if(current_page > 0) {
            current_page--;
            renderCurrentPage(-1); 
        }
    }

    void next_page() {
        int total_pages = computeTotalPages(getCurrentSourceTotal());
        if(current_page < total_pages - 1) {
            current_page++;
            renderCurrentPage(1); 
        }
    }

    void apply_btn_style(lv_obj_t* btn) {
        lv_obj_set_style_translate_y(btn, 4, LV_STATE_PRESSED);
        lv_obj_set_style_transition(btn, &btn_trans, 0);
        lv_obj_set_style_transition(btn, &btn_trans, LV_STATE_PRESSED);
    }

    int getCurrentSourceTotal() {
        if (current_folder_index >= 0 && current_folder_index < (int)loaded_apps.size()
            && loaded_apps[current_folder_index].isFolder()) {
            return (int)homeConfig::getFolderChildren(loaded_apps[current_folder_index].id).size();
        }
        return (int)loaded_apps.size();
    }

    int computeTotalPages(int total) {
        if (current_folder_index >= 0 && total > 0) {
            const int first_page_capacity = ITEMS_PER_PAGE - 1;
            if (total <= first_page_capacity) return 1;
            const int remaining = total - first_page_capacity;
            return 1 + ((remaining + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
        }
        int pages = (total + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        return (pages < 1) ? 1 : pages;
    }

    void create_list_item(lv_obj_t* parent, HomeAppEntry* entry) {
        lv_obj_t* tile = lv_obj_create(parent);
        int w = (entry->width > 1) ? (80 * entry->width + 15 * (entry->width - 1)) : 80;
        lv_obj_set_size(tile, w, 88);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* btn = lv_btn_create(tile);
        if (entry->width > 1) {
            lv_obj_set_size(btn, w - 20, 60);
        } else {
            lv_obj_set_size(btn, 60, 60);
        }
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(entry->color), 0);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        apply_btn_style(btn);
        if (entry->isFolder()) {
            int folder_idx = -1;
            for (int k = 0; k < (int)loaded_apps.size(); k++) if (&loaded_apps[k] == entry) folder_idx = k;
            lv_obj_set_user_data(btn, (void*)(intptr_t)folder_idx);
            lv_obj_add_event_cb(btn, folder_click_cb, LV_EVENT_CLICKED, (void*)this);
        } else {
            lv_obj_add_event_cb(btn, app_click_cb, LV_EVENT_CLICKED, (void*)entry);
        }
        if (entry->width > 1) {
            lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
            if (entry->id == "weather") {
                lv_obj_t* icon = lv_label_create(btn);
                lv_label_set_text(icon, LV_SYMBOL_IMAGE " 21°C");
                lv_obj_set_style_text_font(icon, &lv_font_montserrat_14, 0);
                lv_obj_set_style_text_color(icon, lv_color_white(), 0);
                lv_obj_align(icon, LV_ALIGN_LEFT_MID, 10, 0);
                lv_obj_t* city = lv_label_create(btn);
                lv_label_set_text(city, "Paris");
                lv_obj_set_style_text_font(city, &lv_font_montserrat_12, 0);
                lv_obj_set_style_text_color(city, lv_color_white(), 0);
                lv_obj_align(city, LV_ALIGN_RIGHT_MID, -10, 0);
            } else if (entry->id == "clock") {
                lv_obj_t* time_lbl = lv_label_create(btn);
                lv_label_set_text(time_lbl, "12:00");
                lv_obj_set_style_text_font(time_lbl, &lv_font_montserrat_18, 0);
                lv_obj_set_style_text_color(time_lbl, lv_color_white(), 0);
                lv_obj_align(time_lbl, LV_ALIGN_CENTER, 0, 0);
            } else {
                lv_obj_t* text = lv_label_create(btn);
                lv_label_set_text(text, entry->name.c_str());
                lv_obj_set_style_text_color(text, lv_color_white(), 0);
                lv_obj_center(text);
            }
            return;
        }
        lv_obj_t* icon_lbl = lv_label_create(btn);
        String iconText = (entry->isFolder() || entry->pythonPath.length() == 0) ? entry->symbol : buildPythonInitials(entry->name, entry->id);
        lv_label_set_text(icon_lbl, iconText.c_str());
        lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        lv_obj_center(icon_lbl);
        lv_obj_t* lbl = lv_label_create(tile);
        if (entry->isFolder()) { char buf[64]; snprintf(buf, sizeof(buf), "%s (%d)", entry->name.c_str(), homeConfig::getFolderChildCount(entry->id)); lv_label_set_text(lbl, buf); }
        else lv_label_set_text(lbl, entry->name.c_str());
        lv_obj_set_width(lbl, 76); // Max 2 lines
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
    }
    void create_back_item(lv_obj_t* parent) {
        lv_obj_t* tile = lv_obj_create(parent);
        lv_obj_set_size(tile, 64, 70);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn = lv_btn_create(tile);
        lv_obj_set_size(btn, 60, 60);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x007AFF), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, folder_back_cb, LV_EVENT_CLICKED, (void*)this);
        apply_btn_style(btn);

        lv_obj_t* icon_lbl = lv_label_create(btn);
        lv_label_set_text(icon_lbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        lv_obj_center(icon_lbl);

        lv_obj_t* lbl = lv_label_create(tile);
        lv_label_set_text(lbl, "Retour");
        lv_obj_set_width(lbl, 64);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_height(lbl, 14);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -2);
    }

    void renderCurrentPage(int direction = 0) {
        if (!app_page_cont) return;

        if (page_animating && direction != 0) return;

        if (direction != 0) {
            page_animating = true;
            page_slide_dir = direction;
            lv_anim_del(app_page_cont, (lv_anim_exec_xcb_t)anim_x_cb);
            lv_obj_set_style_translate_x(app_page_cont, 0, 0);

            lv_anim_t out;
            lv_anim_init(&out);
            lv_anim_set_var(&out, app_page_cont);
            lv_anim_set_exec_cb(&out, (lv_anim_exec_xcb_t)anim_x_cb);
            lv_anim_set_time(&out, 300);  
            lv_anim_set_path_cb(&out, lv_anim_path_ease_in);
            lv_anim_set_values(&out, 0, (direction > 0) ? -320 : 320);
            lv_anim_set_user_data(&out, this);
            lv_anim_set_ready_cb(&out, page_slide_out_done_cb);
            lv_anim_start(&out);
            return;
        }
        
        lv_anim_del(app_page_cont, (lv_anim_exec_xcb_t)anim_x_cb);
        lv_obj_set_style_translate_x(app_page_cont, 0, 0);

        lv_obj_clean(app_page_cont); 

        std::vector<HomeAppEntry>* source;
        if (current_folder_index >= 0 && current_folder_index < (int)loaded_apps.size()
            && loaded_apps[current_folder_index].isFolder()) {
            source = &homeConfig::getFolderChildren(loaded_apps[current_folder_index].id);
        } else {
            current_folder_index = -1;
            source = &loaded_apps;
        }

        int total = (int)source->size();
        int effective_items = ITEMS_PER_PAGE;

        bool show_back = (current_folder_index >= 0 && current_page == 0);
        if (show_back) {
            create_back_item(app_page_cont);
            effective_items = ITEMS_PER_PAGE - 1;
        }

        int start_idx = current_page * ITEMS_PER_PAGE - (current_folder_index >= 0 && current_page > 0 ? 1 : 0);
        if (current_page == 0) start_idx = 0;
        int items_shown = 0;

        for (int i = start_idx; i < total && items_shown < effective_items; i++) {
            create_list_item(app_page_cont, &(*source)[i]);
            items_shown++;
        }

        int total_pages = computeTotalPages(total);
        String dots = "";
        for(int i=0; i<total_pages; i++) {
            if (i == current_page) dots += "• "; 
            else dots += "· ";
        }
        lv_label_set_text(nav_dots_label, dots.c_str());
    }

    
    void renderAppList() {
        lv_obj_clean(app_page_cont);
        lv_obj_set_flex_flow(app_page_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_scroll_snap_x(app_page_cont, LV_SCROLL_SNAP_CENTER);

        int current_row = 1;
        int current_col = 1;
        int page_index = 0;
        int max_cols = 3;
        int max_rows = 3;
        
        lv_obj_t* current_page = lv_obj_create(app_page_cont);
        lv_obj_set_size(current_page, 300, 240);
        lv_obj_set_style_bg_opa(current_page, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(current_page, 0, 0);
        lv_obj_set_style_pad_all(current_page, 0, 0);
        lv_obj_set_flex_flow(current_page, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(current_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(current_page, 10, 0);
        lv_obj_set_style_pad_column(current_page, 15, 0);
        lv_obj_add_flag(current_page, LV_OBJ_FLAG_SNAPPABLE);

        for (int i=0; i<loaded_apps.size(); i++) {
            HomeAppEntry* entry = &loaded_apps[i];

            if (entry->width > 1) {
                if (current_col + entry->width - 1 > max_cols) {
                    current_col = 1;
                    current_row++;
                }
            }

            if (current_row > max_rows) {
                current_page = lv_obj_create(app_page_cont);
                lv_obj_set_size(current_page, 300, 240);
                lv_obj_set_style_bg_opa(current_page, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(current_page, 0, 0);
                lv_obj_set_style_pad_all(current_page, 0, 0);
                lv_obj_set_flex_flow(current_page, LV_FLEX_FLOW_ROW_WRAP);
                lv_obj_set_flex_align(current_page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
                lv_obj_set_style_pad_row(current_page, 10, 0);
                lv_obj_set_style_pad_column(current_page, 15, 0);
                lv_obj_add_flag(current_page, LV_OBJ_FLAG_SNAPPABLE);

                current_row = 1;
                current_col = 1;
                page_index++;
            }

            create_list_item(current_page, entry);

            current_col += entry->width;
            if (current_col > max_cols) {
                current_col = 1;
                current_row++;
            }
        }
        total_pages = page_index + 1;
    }

    void createAppListUI() {
        app_page_cont = lv_obj_create(main_bg);
        lv_obj_set_size(app_page_cont, 300, 320);
        lv_obj_align(app_page_cont, LV_ALIGN_TOP_MID, 0, 36); 
        lv_obj_set_style_bg_opa(app_page_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(app_page_cont, 0, 0);
        lv_obj_set_style_pad_all(app_page_cont, 0, 0);
        lv_obj_add_flag(app_page_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(app_page_cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_style_outline_width(app_page_cont, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_outline_width(app_page_cont, 0, LV_STATE_FOCUS_KEY);
        lv_obj_set_style_outline_width(app_page_cont, 0, LV_STATE_FOCUSED);
        lv_obj_set_flex_flow(app_page_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_scroll_dir(app_page_cont, LV_DIR_HOR);
        lv_obj_set_scroll_snap_x(app_page_cont, LV_SCROLL_SNAP_START);
        lv_obj_add_flag(app_page_cont, LV_OBJ_FLAG_SCROLL_ONE);
        lv_obj_set_style_pad_column(app_page_cont, 0, 0);
        lv_obj_set_style_anim_time(app_page_cont, 400, 0);
        renderAppList(); 
        lv_obj_add_event_cb(app_page_cont, on_scroll_end, LV_EVENT_SCROLL_END, this);
        page_indicator_cont = lv_obj_create(main_bg);
        lv_obj_set_size(page_indicator_cont, 200, 10);
        lv_obj_align(page_indicator_cont, LV_ALIGN_BOTTOM_MID, 0, -85);
        lv_obj_set_style_bg_opa(page_indicator_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(page_indicator_cont, 0, 0);
        lv_obj_set_flex_flow(page_indicator_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(page_indicator_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(page_indicator_cont, 8, 0);
        for (int p=0; p<total_pages; p++) {
            lv_obj_t* dot = lv_obj_create(page_indicator_cont);
            lv_obj_set_size(dot, 6, 6);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(dot, 0, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        }
        active_page = 0;
        updatePageIndicators();
    }


    void createQuickActionApps(lv_obj_t* parent) {
        quick_actions_cont = lv_obj_create(parent);
        lv_obj_set_size(quick_actions_cont, 290, 72);
        
        lv_obj_set_style_bg_color(quick_actions_cont, lv_color_hex(0x151E2E), 0);
        lv_obj_set_style_bg_opa(quick_actions_cont, LV_OPA_COVER, 0); 
        lv_obj_set_style_radius(quick_actions_cont, 22, 0);
        lv_obj_set_style_border_width(quick_actions_cont, 1, 0);
        lv_obj_set_style_border_color(quick_actions_cont, lv_color_hex(0x27364F), 0);
        lv_obj_clear_flag(quick_actions_cont, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_align(quick_actions_cont, LV_ALIGN_BOTTOM_MID, 0, -10);

        struct QuickApp { const char* sym; uint32_t col; lv_event_cb_t cb; };
        QuickApp qa[] = {
            {LV_SYMBOL_GPS, 0x0088CC, open_telegram},
            {LV_SYMBOL_CHARGE, 0xFF9500, open_weather},
            {"V", 0x34C759, open_velib},
            {LV_SYMBOL_SETTINGS, 0x555555, [](lv_event_t*e){ AppManager::switchTo(APP_SETTINGS); }}
        };

        for(int i = 0; i < 4; i++) {
            lv_obj_t* icon = lv_btn_create(quick_actions_cont);
            lv_obj_set_size(icon, 52, 52);
            lv_obj_set_style_bg_color(icon, lv_color_hex(qa[i].col), 0);
            lv_obj_set_style_radius(icon, 16, 0);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, i * 65 + 16, 0);
            apply_btn_style(icon);

            lv_obj_t* icon_label = lv_label_create(icon);
            lv_label_set_text(icon_label, qa[i].sym);
            lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
            lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_18, 0);
            lv_obj_center(icon_label);
            if (qa[i].cb) lv_obj_add_event_cb(icon, qa[i].cb, LV_EVENT_CLICKED, NULL);
        }
    }

public: 
    void start(lv_obj_t* parent) override {
        homeConfig::loadConfig(loaded_apps);
        current_folder_index = -1;

        main_bg = parent;
        press_started_top = false;

        static const lv_style_prop_t props[] = {LV_STYLE_TRANSLATE_Y, (lv_style_prop_t)0};
        lv_style_transition_dsc_init(&btn_trans, props, lv_anim_path_ease_out, 80, 0, NULL);

        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE); 
        current_bg_color = homeConfig::getBackgroundColor();
        applyBackgroundColor(current_bg_color, false);

        lv_obj_add_flag(main_bg, LV_OBJ_FLAG_CLICKABLE);

        createQuickActionApps(main_bg);
        createAppListUI();
    }

    void update() override {

    } void stop() override {
        if (quick_actions_cont) {
            lv_anim_del(quick_actions_cont, nullptr);
        }
        if (app_page_cont) {
            lv_anim_del(app_page_cont, nullptr);
        }
        if (main_bg) {
            lv_obj_clean(main_bg);
        }

        color_selector_overlay = nullptr;
        color_selector_sheet = nullptr;

        app_page_cont = nullptr;
        nav_dots_label = nullptr;
        quick_actions_cont = nullptr;

        bg_img = nullptr;
    }
};

#endif
