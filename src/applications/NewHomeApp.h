#ifndef NEW_HOME_APP_H
#define NEW_HOME_APP_H

#include <Arduino.h>
#include <time.h>
#include <vector>
#include "App.h"
#include "AppManager.h"
#include <WiFi.h>
#include "../system/Battery.h"
#include "../system/Settings.h"
#include "../system/LTE.h"
#include "../system/HomeConfig.h"
#include "./PythonApp.h"

LV_IMG_DECLARE(fondecran);

class NewHomeApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* bg_img;
    lv_obj_t* top_bar_cont; 
    lv_obj_t* wifi_label;
    lv_obj_t* time_label;
    lv_obj_t* batt_icon;
    lv_obj_t* signal_icon = nullptr;
    lv_obj_t* operator_label = nullptr;
    lv_obj_t* quick_actions_cont;
    lv_obj_t* color_selector_overlay = nullptr;
    lv_obj_t* color_selector_sheet = nullptr;

    lv_obj_t *app_list_cont = nullptr;       
    lv_obj_t *app_page_cont = nullptr;       
    lv_obj_t *nav_dots_label = nullptr;      
    lv_obj_t *btn_prev = nullptr;
    lv_obj_t *btn_next = nullptr;
    
    bool app_list_open = false;
    lv_point_t touch_start_point;
    bool press_started_top = false;
    bool long_press_consumed = false;

    uint32_t current_bg_color = 0x408A71;
    lv_color_t bg_anim_from;
    lv_color_t bg_anim_to;

    int current_page = 0;
    static const int ITEMS_PER_PAGE = 4;

    std::vector<HomeAppEntry> loaded_apps;
    int current_folder_index = -1; 

    lv_style_transition_dsc_t btn_trans;

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

    static void anim_opa_cb(void * var, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)var, v, 0);
    }

    static void anim_bg_color_cb(void* var, int32_t v) {
        NewHomeApp* app = (NewHomeApp*)var;
        if (!app || !app->main_bg) return;
        lv_color_t mix = lv_color_mix(app->bg_anim_to, app->bg_anim_from, (uint8_t)v);
        lv_obj_set_style_bg_color(app->main_bg, mix, 0);
    }

    lv_coord_t drawer_hidden_ty = 435;

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
        lv_anim_set_time(&a, 240);
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
        lv_obj_set_style_bg_color(color_selector_sheet, lv_color_hex(0x202225), 0);
        lv_obj_set_style_bg_opa(color_selector_sheet, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(color_selector_sheet, 0, 0);
        lv_obj_set_style_radius(color_selector_sheet, 22, 0);
        lv_obj_set_style_pad_all(color_selector_sheet, 14, 0);
        lv_obj_set_style_pad_row(color_selector_sheet, 10, 0);
        lv_obj_set_style_pad_column(color_selector_sheet, 10, 0);
        lv_obj_set_layout(color_selector_sheet, LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(color_selector_sheet, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_flex_align(color_selector_sheet, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(color_selector_sheet, nullptr, LV_EVENT_CLICKED, nullptr);

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
        lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x3A3A3D), 0);
        lv_obj_set_style_bg_opa(close_btn, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(close_btn, 0, 0);
        lv_obj_add_event_cb(close_btn, color_selector_close_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* close_lbl = lv_label_create(close_btn);
        lv_label_set_text(close_lbl, "Fermer");
        lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
        lv_obj_center(close_lbl);

        lv_obj_move_foreground(color_selector_sheet);
    }

    void toggleAppList(bool open) {
        if (!app_list_cont) return;
        app_list_open = open;

        lv_anim_del(app_list_cont, (lv_anim_exec_xcb_t)anim_y_cb);
        if (quick_actions_cont) lv_anim_del(quick_actions_cont, (lv_anim_exec_xcb_t)anim_opa_cb);

        int32_t cur_ty = lv_obj_get_style_translate_y(app_list_cont, 0);

        lv_anim_t a_list;
        lv_anim_init(&a_list);
        lv_anim_set_var(&a_list, app_list_cont);
        lv_anim_set_exec_cb(&a_list, (lv_anim_exec_xcb_t)anim_y_cb);

        lv_anim_t a_qa;
        lv_anim_init(&a_qa);
        if (quick_actions_cont) {
            lv_obj_clear_flag(quick_actions_cont, LV_OBJ_FLAG_HIDDEN);
            lv_anim_set_var(&a_qa, quick_actions_cont);
            lv_anim_set_exec_cb(&a_qa, (lv_anim_exec_xcb_t)anim_opa_cb);
            lv_anim_set_path_cb(&a_qa, lv_anim_path_ease_out);
        }

        if (open) {
            lv_anim_set_time(&a_list, 320);
            lv_anim_set_path_cb(&a_list, lv_anim_path_ease_out);
            lv_anim_set_values(&a_list, cur_ty, 0);
            if (quick_actions_cont) {
                lv_anim_set_time(&a_qa, 220);
                lv_anim_set_values(&a_qa, 255, 0); 
            }
        } else {
            lv_anim_set_time(&a_list, 260);
            lv_anim_set_path_cb(&a_list, lv_anim_path_ease_in_out);
            lv_anim_set_values(&a_list, cur_ty, drawer_hidden_ty);
            if (quick_actions_cont) {
                lv_anim_set_time(&a_qa, 200);
                lv_anim_set_values(&a_qa, 0, 255); 
            }
        }

        lv_anim_start(&a_list);
        if (quick_actions_cont) lv_anim_start(&a_qa);
    }

    static void close_app_list_cb(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        if(app) app->toggleAppList(false);
    }

    static void prev_page_cb(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        if(app && app->current_page > 0) {
            app->current_page--;
            app->renderCurrentPage(-1); 
        }
    }

    static void next_page_cb(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        int total_pages = ((int)app->loaded_apps.size() + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        if(app && app->current_page < total_pages - 1) {
            app->current_page++;
            app->renderCurrentPage(1); 
        }
    }

    void apply_btn_style(lv_obj_t* btn) {
        lv_obj_set_style_translate_y(btn, 4, LV_STATE_PRESSED);
        lv_obj_set_style_transition(btn, &btn_trans, 0);
        lv_obj_set_style_transition(btn, &btn_trans, LV_STATE_PRESSED);
    }

    void create_list_item(lv_obj_t* parent, HomeAppEntry* entry) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 280, 60); 
        lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_30, 0); 
        lv_obj_set_style_radius(btn, 15, 0); 
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

        lv_obj_t* icon_bg = lv_obj_create(btn);
        lv_obj_set_size(icon_bg, 45, 45);
        lv_obj_set_style_radius(icon_bg, 15, 0);
        lv_obj_set_style_bg_color(icon_bg, lv_color_hex(entry->color), 0);
        lv_obj_set_style_border_width(icon_bg, 0, 0);
        lv_obj_align(icon_bg, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE); 

        lv_obj_t* icon_lbl = lv_label_create(icon_bg);
        lv_label_set_text(icon_lbl, entry->symbol.c_str());
        lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        lv_obj_center(icon_lbl);

        lv_obj_t* lbl = lv_label_create(btn);
        if (entry->isFolder()) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s (%d)", entry->name.c_str(), homeConfig::getFolderChildCount(entry->id));
            lv_label_set_text(lbl, buf);
        } else {
            lv_label_set_text(lbl, entry->name.c_str());
        }
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0); 
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0); 
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 70, 0);
    }

    void create_back_item(lv_obj_t* parent) {
        lv_obj_t* btn = lv_btn_create(parent);
        lv_obj_set_size(btn, 280, 60);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x3a3a3c), 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
        lv_obj_set_style_radius(btn, 15, 0);
        lv_obj_set_style_border_width(btn, 0, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, folder_back_cb, LV_EVENT_CLICKED, (void*)this);

        apply_btn_style(btn);

        lv_obj_t* icon_bg = lv_obj_create(btn);
        lv_obj_set_size(icon_bg, 45, 45);
        lv_obj_set_style_radius(icon_bg, 15, 0);
        lv_obj_set_style_bg_color(icon_bg, lv_color_hex(0x007AFF), 0);
        lv_obj_set_style_border_width(icon_bg, 0, 0);
        lv_obj_align(icon_bg, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* icon_lbl = lv_label_create(icon_bg);
        lv_label_set_text(icon_lbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(icon_lbl, lv_color_white(), 0);
        lv_obj_center(icon_lbl);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "Retour");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 70, 0);
    }

    void renderCurrentPage(int direction = 0) {
        if (!app_page_cont) return;
        
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

        int total_pages;
        if (current_folder_index >= 0 && total > 0) {
            total_pages = 1 + ((total - (ITEMS_PER_PAGE - 1) + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE);
            if (total <= ITEMS_PER_PAGE - 1) total_pages = 1;
        } else {
            total_pages = (total + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        }
        if (total_pages < 1) total_pages = 1;

        String dots = "";
        for(int i=0; i<total_pages; i++) {
            if (i == current_page) dots += "- "; 
            else dots += ". ";
        }
        lv_label_set_text(nav_dots_label, dots.c_str());

        if (current_page == 0) lv_obj_add_state(btn_prev, LV_STATE_DISABLED);
        else lv_obj_clear_state(btn_prev, LV_STATE_DISABLED);

        if (current_page >= total_pages - 1) lv_obj_add_state(btn_next, LV_STATE_DISABLED);
        else lv_obj_clear_state(btn_next, LV_STATE_DISABLED);

        // Slide horizontal plus "phone-like": un peu plus long et plus souple.
        if (direction != 0) {
            lv_anim_t a_slide;
            lv_anim_init(&a_slide);
            lv_anim_set_var(&a_slide, app_page_cont);
            lv_anim_set_exec_cb(&a_slide, (lv_anim_exec_xcb_t)anim_x_cb);
            lv_anim_set_time(&a_slide, 300);
            lv_anim_set_path_cb(&a_slide, lv_anim_path_ease_in_out);

            if (direction > 0) {
                lv_anim_set_values(&a_slide, 320, 0);
            } else {
                lv_anim_set_values(&a_slide, -320, 0);
            }
            lv_anim_start(&a_slide);
        }
    }

    void createAppListUI() {
        app_list_cont = lv_obj_create(main_bg);
        lv_obj_set_size(app_list_cont, 300, 410);
        lv_obj_set_pos(app_list_cont, 10, 65); 
        lv_obj_set_style_translate_y(app_list_cont, drawer_hidden_ty, 0); 
        lv_obj_set_style_bg_color(app_list_cont, lv_color_hex(0x222222), 0);
        lv_obj_set_style_bg_opa(app_list_cont, LV_OPA_50, 0); 
        lv_obj_set_style_border_width(app_list_cont, 0, 0);
        lv_obj_set_style_radius(app_list_cont, 20, 0); 
        lv_obj_set_scrollbar_mode(app_list_cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(app_list_cont, LV_DIR_NONE);

        lv_obj_t* header_btn = lv_btn_create(app_list_cont);
        lv_obj_set_size(header_btn, 280, 40);
        lv_obj_align(header_btn, LV_ALIGN_TOP_MID, 0, 5);
        lv_obj_set_style_bg_color(header_btn, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(header_btn, LV_OPA_30, 0);
        lv_obj_set_style_border_width(header_btn, 1, 0);
        lv_obj_set_style_border_color(header_btn, lv_color_white(), 0);
        lv_obj_set_style_border_opa(header_btn, LV_OPA_30, 0);
        lv_obj_set_style_radius(header_btn, 10, 0);
        lv_obj_add_event_cb(header_btn, close_app_list_cb, LV_EVENT_CLICKED, this);
        apply_btn_style(header_btn);

        lv_obj_t* header_lbl = lv_label_create(header_btn);
        lv_label_set_text(header_lbl, LV_SYMBOL_DOWN "     Back     " LV_SYMBOL_DOWN);
        lv_obj_set_style_text_color(header_lbl, lv_color_white(), 0);
        lv_obj_center(header_lbl);

        app_page_cont = lv_obj_create(app_list_cont);
        lv_obj_set_size(app_page_cont, 280, 280);
        lv_obj_align(app_page_cont, LV_ALIGN_TOP_MID, 0, 45);
        lv_obj_set_style_bg_opa(app_page_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(app_page_cont, 0, 0);
        lv_obj_clear_flag(app_page_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(app_page_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(app_page_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(app_page_cont, 10, 0);

        lv_obj_t* footer_cont = lv_obj_create(app_list_cont);
        lv_obj_set_size(footer_cont, 280, 60);
        lv_obj_align(footer_cont, LV_ALIGN_BOTTOM_MID, 0, 8);
        lv_obj_set_style_bg_opa(footer_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(footer_cont, 0, 0);
        lv_obj_clear_flag(footer_cont, LV_OBJ_FLAG_SCROLLABLE);

        btn_prev = lv_btn_create(footer_cont);
        lv_obj_set_size(btn_prev, 60, 50);
        lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn_prev, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn_prev, LV_OPA_40, 0);
        lv_obj_set_style_radius(btn_prev, 15, 0);
        lv_obj_t* lbl_prev = lv_label_create(btn_prev);
        lv_label_set_text(lbl_prev, LV_SYMBOL_LEFT);
        lv_obj_center(lbl_prev);
        lv_obj_add_event_cb(btn_prev, prev_page_cb, LV_EVENT_CLICKED, this);
        apply_btn_style(btn_prev);

        nav_dots_label = lv_label_create(footer_cont);
        lv_obj_set_style_text_color(nav_dots_label, lv_color_white(), 0);
        lv_obj_set_style_text_font(nav_dots_label, &lv_font_montserrat_18, 0);
        lv_obj_align(nav_dots_label, LV_ALIGN_CENTER, 0, 0);

        btn_next = lv_btn_create(footer_cont);
        lv_obj_set_size(btn_next, 60, 50);
        lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn_next, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(btn_next, LV_OPA_40, 0);
        lv_obj_set_style_radius(btn_next, 15, 0);
        lv_obj_t* lbl_next = lv_label_create(btn_next);
        lv_label_set_text(lbl_next, LV_SYMBOL_RIGHT);
        lv_obj_center(lbl_next);
        lv_obj_add_event_cb(btn_next, next_page_cb, LV_EVENT_CLICKED, this);
        apply_btn_style(btn_next);

        current_page = 0;
        renderCurrentPage(0); 

        lv_obj_add_event_cb(app_list_cont, screen_touch_event, LV_EVENT_ALL, this);
    }

    static void screen_touch_event(lv_event_t* e) {
        NewHomeApp* app = (NewHomeApp*)lv_event_get_user_data(e);
        if(!app) return;

        if (app->color_selector_overlay) {
            return;
        }

        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_indev_get_point(indev, &app->touch_start_point);
                app->press_started_top = (app->touch_start_point.y < 200);
                app->long_press_consumed = false;
            }
        } else if (code == LV_EVENT_LONG_PRESSED) {
            app->long_press_consumed = true;
            app->showColorSelector();
        } else if (code == LV_EVENT_RELEASED) {
            if (app->long_press_consumed) return;
            lv_indev_t* indev = lv_indev_get_act();
            if (indev) {
                lv_point_t end_point;
                lv_indev_get_point(indev, &end_point);
                lv_coord_t diff_y = end_point.y - app->touch_start_point.y;

                if (diff_y < -50 && !app->app_list_open) {
                    app->toggleAppList(true);
                } else if (diff_y > 50) {
                    if (app->app_list_open) {
                        app->toggleAppList(false);
                    } else if (app->press_started_top) {
                        AppManager::openControlCenter();
                    }
                }
            }
        }
    }

    void createTopBar(lv_obj_t* parent) {
        top_bar_cont = lv_obj_create(parent);
        lv_obj_set_size(top_bar_cont, 300, 40);
        lv_obj_set_style_radius(top_bar_cont, 10, 0);
        lv_obj_set_style_bg_color(top_bar_cont, lv_color_hex(0xD9D9D9), 0);
        lv_obj_set_style_bg_opa(top_bar_cont, LV_OPA_40, 0);
        lv_obj_align(top_bar_cont, LV_ALIGN_TOP_MID, 0, 15);
        lv_obj_set_style_border_opa(top_bar_cont, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(top_bar_cont, LV_OBJ_FLAG_SCROLLABLE);

        operator_label = lv_label_create(top_bar_cont);
        lv_label_set_text(operator_label, "Recherche...");
        lv_obj_set_style_text_color(operator_label, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(operator_label, &lv_font_montserrat_14, 0);
        lv_obj_align(operator_label, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* icon_zone = lv_obj_create(top_bar_cont);
        lv_obj_set_size(icon_zone, 120, 20); 
        lv_obj_align(icon_zone, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_border_opa(icon_zone, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(icon_zone, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(icon_zone, LV_OBJ_FLAG_SCROLLABLE);

        signal_icon = lv_label_create(icon_zone);
        lv_label_set_text(signal_icon, "||||");
        lv_obj_set_style_text_color(signal_icon, lv_color_hex(0x323232), 0);
        lv_obj_set_style_text_font(signal_icon, &lv_font_montserrat_12, 0);
        lv_obj_align(signal_icon, LV_ALIGN_LEFT_MID, 0, 0);

        wifi_label = lv_label_create(icon_zone);
        lv_label_set_text(wifi_label, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x323232), 0);
        lv_obj_align(wifi_label, LV_ALIGN_LEFT_MID, 30, 0);
        
        time_label = lv_label_create(top_bar_cont);
        lv_label_set_text(time_label, "00:00");
        lv_obj_set_style_text_color(time_label, lv_color_hex(0x323232), 0);
        lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 10, 0);
        
        batt_icon = lv_label_create(icon_zone);
        lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_FULL);
        lv_obj_set_style_text_color(batt_icon, lv_color_hex(0x323232), 0);
        lv_obj_align(batt_icon, LV_ALIGN_LEFT_MID, 75, 0);
    }

    void createQuickActionApps(lv_obj_t* parent) {
        quick_actions_cont = lv_obj_create(parent);
        lv_obj_set_size(quick_actions_cont, 300, 74);
        lv_obj_set_style_radius(quick_actions_cont, 10, 0);
        lv_obj_set_style_bg_color(quick_actions_cont, lv_color_hex(0xD9D9D9), 0);
        lv_obj_set_style_bg_opa(quick_actions_cont, LV_OPA_40, 0);
        lv_obj_align(quick_actions_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_border_opa(quick_actions_cont, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(quick_actions_cont, LV_OBJ_FLAG_SCROLLABLE);

        struct QuickApp { const char* sym; uint32_t col; lv_event_cb_t cb; };
        QuickApp qa[] = {
            {LV_SYMBOL_GPS, 0x0088CC, open_telegram},
            {LV_SYMBOL_CHARGE, 0xFF9500, open_weather},
            {"V", 0x34C759, open_velib}
        };

        for(int i = 0; i < 3; i++) {
            lv_obj_t* icon = lv_btn_create(quick_actions_cont);
            lv_obj_set_size(icon, 55, 55);
            lv_obj_set_style_bg_color(icon, lv_color_hex(qa[i].col), 0);
            lv_obj_set_style_radius(icon, 14, 0);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, i * 85 + 24, 0);
            apply_btn_style(icon);

            lv_obj_t* icon_label = lv_label_create(icon);
            lv_label_set_text(icon_label, qa[i].sym);
            lv_obj_set_style_text_color(icon_label, lv_color_white(), 0);
            lv_obj_center(icon_label);
            lv_obj_add_event_cb(icon, qa[i].cb, LV_EVENT_CLICKED, NULL);
        }
    }

public: 
    void start(lv_obj_t* parent) override {
        homeConfig::loadConfig(loaded_apps);
        current_folder_index = -1;

        main_bg = parent;
        app_list_open = false; 
        press_started_top = false;

        static const lv_style_prop_t props[] = {LV_STYLE_TRANSLATE_Y, (lv_style_prop_t)0};
        lv_style_transition_dsc_init(&btn_trans, props, lv_anim_path_ease_out, 80, 0, NULL);

        drawer_hidden_ty = 415;

        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE); 
        current_bg_color = homeConfig::getBackgroundColor();
        applyBackgroundColor(current_bg_color, false);

        // bg_img = lv_img_create(main_bg);
        //lv_img_set_src(bg_img, &fondecran);
        //lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
        // lv_obj_clear_flag(bg_img, LV_OBJ_FLAG_CLICKABLE); 
        // lv_obj_set_style_img_recolor(bg_img, lv_color_black(), 0);
        // lv_obj_set_style_img_recolor_opa(bg_img, LV_OPA_30, 0); 
        
        lv_obj_add_flag(main_bg, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_event_cb(main_bg, screen_touch_event);
        lv_obj_add_event_cb(main_bg, screen_touch_event, LV_EVENT_ALL, this);

        createTopBar(main_bg);
        createQuickActionApps(main_bg);
        createAppListUI();
    }

    void update() override {
        const unsigned long now_ms = millis();

        static unsigned long last_check = 0;
        if (now_ms - last_check > 3000) {
            last_check = now_ms;
            if (WiFi.status() == WL_CONNECTED) lv_obj_set_style_text_color(wifi_label, lv_color_hex(0x232323), 0);
            else lv_obj_set_style_text_color(wifi_label, lv_color_hex(0xC1C1C1), 0);
        }

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
            if(p > 80) lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_FULL);
            else if (p > 60) lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_3);
            else if (p > 40) lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_2);
            else if (p > 20) lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_1);
            else lv_label_set_text(batt_icon, LV_SYMBOL_BATTERY_EMPTY);
        }

        static unsigned long last_lte_check = 0;
        if (now_ms - last_lte_check > 2000) {
            last_lte_check = now_ms;
            
            if (!LTE::isEnabled()) {
                lv_label_set_text(operator_label, "Mode Avion");
                lv_label_set_text(signal_icon, "X");
            } else {
                lv_label_set_text(operator_label, LTE::getOperator().c_str());
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
        if (app_list_cont) {
            lv_anim_del(app_list_cont, nullptr);
            lv_obj_remove_event_cb(app_list_cont, screen_touch_event);
        }
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

        app_list_cont = nullptr;
        app_page_cont = nullptr;
        nav_dots_label = nullptr;
        btn_prev = nullptr;
        btn_next = nullptr;
        quick_actions_cont = nullptr;
        top_bar_cont = nullptr;
        wifi_label = nullptr;
        time_label = nullptr;
        batt_icon = nullptr;
        bg_img = nullptr;
    }
};

#endif