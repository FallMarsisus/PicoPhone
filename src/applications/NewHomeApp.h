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
    
    lv_obj_t* top_bar_cont = nullptr; 
    lv_obj_t* time_label = nullptr;
    lv_obj_t* batt_icon = nullptr;
    lv_obj_t* signal_icon = nullptr;
    lv_obj_t* operator_label = nullptr;
    
    lv_obj_t* quick_actions_cont = nullptr;
    lv_obj_t* color_selector_overlay = nullptr;
    lv_obj_t* color_selector_sheet = nullptr;

    lv_obj_t *app_page_cont = nullptr;       
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
        lv_obj_set_size(tile, 64, 70);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 0, 0);
        lv_obj_set_style_pad_all(tile, 0, 0);
        lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn = lv_btn_create(tile);
        lv_obj_set_size(btn, 54, 54);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(entry->color), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, 16, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        apply_btn_style(btn);

        if (entry->isFolder()) {
            int folder_idx = -1;
            for (int i = 0; i < (int)loaded_apps.size(); i++) {
                if (&loaded_apps[i] == entry) { folder_idx = i; break; }
            }
            lv_obj_set_user_data(btn, (void*)(intptr_t)folder_idx);
            lv_obj_add_event_cb(btn, folder_click_cb, LV_EVENT_CLICKED, (void*)this);
        } else {
            lv_obj_add_event_cb(btn, app_click_cb, LV_EVENT_CLICKED, (void*)entry);
        }

        lv_obj_t* icon_lbl = lv_label_create(btn);
        String iconText = (entry->isFolder() || entry->pythonPath.length() == 0)
            ? entry->symbol
            : buildPythonInitials(entry->name, entry->id);
        lv_label_set_text(icon_lbl, iconText.c_str());
        lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(icon_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(icon_lbl);

        lv_obj_t* lbl = lv_label_create(tile);
        if (entry->isFolder()) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s (%d)", entry->name.c_str(), homeConfig::getFolderChildCount(entry->id));
            lv_label_set_text(lbl, buf);
        } else {
            lv_label_set_text(lbl, entry->name.c_str());
        }
        lv_obj_set_width(lbl, 64);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_height(lbl, 14);
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
        lv_obj_set_size(btn, 54, 54);
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

    void createAppListUI() {
        app_page_cont = lv_obj_create(main_bg);
        lv_obj_set_size(app_page_cont, 290, 270);
        lv_obj_align(app_page_cont, LV_ALIGN_TOP_MID, 0, 50); // Espace pour top bar
        lv_obj_set_style_bg_opa(app_page_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(app_page_cont, 0, 0);
        lv_obj_set_style_pad_all(app_page_cont, 5, 0);
        lv_obj_clear_flag(app_page_cont, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_flex_flow(app_page_cont, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(app_page_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_gap(app_page_cont, 10, 0);

        nav_dots_label = lv_label_create(main_bg);
        lv_obj_set_style_text_color(nav_dots_label, lv_color_hex(0xDDDDDD));
        lv_obj_set_style_text_font(nav_dots_label, &lv_font_montserrat_18, 0);
        lv_obj_align(nav_dots_label, LV_ALIGN_BOTTOM_MID, 0, -95); // Juste au dessus du dock

        current_page = 0;
        renderCurrentPage(0); 
    }

    static void screen_touch_event(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        if(!app || app->color_selector_overlay) return;

        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_indev_get_point(indev, &app->touch_start_point);
                app->press_started_top = (app->touch_start_point.y < 50);
                app->long_press_consumed = false;
                app->press_active = true;
                app->long_press_armed = true;
                app->press_start_ms = millis();
            }
        } else if (code == LV_EVENT_PRESSING) {
            if (app->long_press_consumed) return;

            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_point_t now_point;
                lv_indev_get_point(indev, &now_point);
                lv_coord_t diff_x = now_point.x - app->touch_start_point.x;
                lv_coord_t diff_y = now_point.y - app->touch_start_point.y;

                if (abs(diff_x) > 15 || abs(diff_y) > 15) {
                    app->long_press_armed = false;
                }
            }
        } else if (code == LV_EVENT_RELEASED) {
            app->press_active = false;
            app->long_press_armed = false;
            if (app->long_press_consumed) return;
            
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_point_t end_point;
                lv_indev_get_point(indev, &end_point);
                lv_coord_t diff_x = end_point.x - app->touch_start_point.x;
                lv_coord_t diff_y = end_point.y - app->touch_start_point.y;

                if (abs(diff_x) > 40 && abs(diff_x) > abs(diff_y)) {
                    if (diff_x < 0) app->next_page();
                    else app->prev_page();
                } 
                else if (diff_y > 40 && app->press_started_top) {
                    AppManager::openControlCenter();
                }
            }
        }
    }

    void createTopBar(lv_obj_t* parent) {
        top_bar_cont = lv_obj_create(parent);
        lv_obj_set_size(top_bar_cont, 320, 26);
        lv_obj_set_style_bg_opa(top_bar_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(top_bar_cont, 0, 0);
        lv_obj_set_style_pad_all(top_bar_cont, 5, 0);
        lv_obj_align(top_bar_cont, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(top_bar_cont, LV_OBJ_FLAG_SCROLLABLE);

        operator_label = lv_label_create(top_bar_cont);
        lv_label_set_text(operator_label, "Recherche...");
        lv_obj_set_style_text_color(operator_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(operator_label, &lv_font_montserrat_12, 0);
        lv_obj_align(operator_label, LV_ALIGN_LEFT_MID, 5, 0);

        time_label = lv_label_create(top_bar_cont);
        lv_label_set_text(time_label, "00:00");
        lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_12, 0);
        lv_obj_align(time_label, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* icon_zone = lv_obj_create(top_bar_cont);
        lv_obj_set_size(icon_zone, 80, 20); 
        lv_obj_align(icon_zone, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_border_width(icon_zone, 0, 0);
        lv_obj_set_style_bg_opa(icon_zone, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(icon_zone, LV_OBJ_FLAG_SCROLLABLE);

        signal_icon = lv_label_create(icon_zone);
        lv_label_set_text(signal_icon, "||||");
        lv_obj_set_style_text_color(signal_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(signal_icon, &lv_font_montserrat_12, 0);
        lv_obj_align(signal_icon, LV_ALIGN_LEFT_MID, 0, 0);
        
        batt_icon = lv_label_create(icon_zone);
        lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_FULL);
        lv_obj_set_style_text_color(batt_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(batt_icon, &lv_font_montserrat_12, 0);
        lv_obj_align(batt_icon, LV_ALIGN_RIGHT_MID, -5, 0);
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
            {LV_SYMBOL_SETTINGS, 0x555555, [](lv_event_t*e){ AppManager::switchTo(APP_SYSTEM_SETTINGS); }}
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
        lv_obj_remove_event_cb(main_bg, screen_touch_event);
        lv_obj_add_event_cb(main_bg, screen_touch_event, LV_EVENT_ALL, this);

        createTopBar(main_bg);
        createQuickActionApps(main_bg);
        createAppListUI();
    }

    void update() override {
        pollLongPress();

        const unsigned long now_ms = millis();

        static unsigned long last_time_update = 0;
        if (now_ms - last_time_update > 1000) {
            struct tm timeinfo;
            time_t now = time(nullptr);
            if (now > 0 && localtime_r(&now, &timeinfo) != nullptr) {
                last_time_update = now_ms;
                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                lv_label_set_text(time_label, buf);
            }
        }

        static unsigned long last_batt = 0;
        if (now_ms - last_batt > 4000) {
            last_batt = now_ms;
            const uint8_t p = battery::read_percent();
            const bool charging = battery::is_charging() || battery::is_external_power();
            const bool eco_manual = battery::is_manual_saver_enabled() && !charging;

            const char* icon = LV_SYMBOL_BATTERY_EMPTY;
            if (p > 80) icon = LV_SYMBOL_BATTERY_FULL;
            else if (p > 60) icon = LV_SYMBOL_BATTERY_3;
            else if (p > 40) icon = LV_SYMBOL_BATTERY_2;
            else if (p > 20) icon = LV_SYMBOL_BATTERY_1;

            char batt_buf[16];
            snprintf(batt_buf, sizeof(batt_buf), "%s%s", charging ? LV_SYMBOL_CHARGE : "", icon);
            lv_label_set_text(batt_icon, batt_buf);

            if (charging) {
                lv_obj_set_style_text_color(batt_icon, lv_color_hex(0x77E4A1), 0);
            } else if (p <= 20 || eco_manual) {
                lv_obj_set_style_text_color(batt_icon, lv_color_hex(0xFFB15A), 0);
            } else {
                lv_obj_set_style_text_color(batt_icon, lv_color_white(), 0);
            }
        }

        static unsigned long last_lte_check = 0;
        if (now_ms - last_lte_check > 2000) {
            last_lte_check = now_ms;
            
            if (!LTE::isEnabled()) {
                lv_label_set_text(operator_label, "Mode Avion");
                lv_label_set_text(signal_icon, "X");
                lv_obj_set_style_text_color(signal_icon, lv_color_hex(0xFF3B30), 0);
            } else {
                lv_label_set_text(operator_label, LTE::getOperator().c_str());
                lv_obj_set_style_text_color(signal_icon, lv_color_white(), 0);
                
                int sig = LTE::getSignal();
                if (sig == 0) lv_label_set_text(signal_icon, "!");
                else if (sig == 1) lv_label_set_text(signal_icon, "|");
                else if (sig == 2) lv_label_set_text(signal_icon, "||");
                else if (sig == 3) lv_label_set_text(signal_icon, "|||");
                else lv_label_set_text(signal_icon, "||||");
            }
        }
    }

    void stop() override {
        if (quick_actions_cont) {
            lv_anim_del(quick_actions_cont, nullptr);
        }
        if (app_page_cont) {
            lv_anim_del(app_page_cont, nullptr);
        }
        if (main_bg) {
            lv_obj_remove_event_cb(main_bg, screen_touch_event);
            lv_obj_clean(main_bg);
        }

        color_selector_overlay = nullptr;
        color_selector_sheet = nullptr;

        app_page_cont = nullptr;
        nav_dots_label = nullptr;
        quick_actions_cont = nullptr;
        top_bar_cont = nullptr;
        time_label = nullptr;
        batt_icon = nullptr;
        bg_img = nullptr;
    }
};

#endif
