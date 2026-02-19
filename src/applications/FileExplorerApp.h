// FileExplorerApp moderne, style WeatherApp/WifiApp
#ifndef FILE_EXPLORER_APP_H
#define FILE_EXPLORER_APP_H

#include "App.h"
#include "AppManager.h"
#include <lvgl.h>
#include <Arduino.h>
#include <LittleFS.h>

class FileExplorerApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* list;
    lv_obj_t* header;
    lv_obj_t* lbl_path;
    String current_path;

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void up_event(lv_event_t* e) {
        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
        if (!app) return;
        String parent = app->current_path;
        if (parent == "/") return;
        int idx = parent.lastIndexOf('/');
        if (idx <= 0) parent = "/";
        else parent = parent.substring(0, idx);
        app->buildFileList(parent);
    }

    static void open_dir_event(lv_event_t* e) {
        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
        if (!app) return;
        lv_obj_t* btn = lv_event_get_target(e);
        const char* name = lv_label_get_text(lv_obj_get_child(btn, 1));
        String new_path = app->current_path;
        if (!new_path.endsWith("/")) new_path += "/";
        new_path += name;
        app->buildFileList(new_path);
    }

    void buildFileList(const String& path) {
        current_path = path;
        lv_label_set_text(lbl_path, path.c_str());
        lv_obj_clean(list);

        // Bouton pour remonter
        if (path != "/") {
            lv_obj_t* up_btn = lv_list_add_btn(list, LV_SYMBOL_UP, ".. (Parent)");
            lv_obj_add_event_cb(up_btn, up_event, LV_EVENT_CLICKED, this);
        }

        File root = LittleFS.open(path, "r");
        if (!root || !root.isDirectory()) {
            lv_list_add_text(list, "Dossier invalide ou inaccessible");
            return;
        }

        File file = root.openNextFile();
        bool has_content = false;
        while (file) {
            String name = file.name();
            bool isDir = file.isDirectory();
            String shortName = name;
            if (shortName.startsWith(path)) shortName = shortName.substring(path.length());
            if (shortName.startsWith("/")) shortName = shortName.substring(1);
            if (shortName.length() == 0) { file = root.openNextFile(); continue; }

            lv_obj_t* item = lv_list_add_btn(list, isDir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE, shortName.c_str());
            if (isDir) {
                lv_obj_add_event_cb(item, open_dir_event, LV_EVENT_CLICKED, this);
            } else {
                // Pour l'instant, simple popup info
                lv_obj_add_event_cb(item, [](lv_event_t* e) {
                    FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
                    lv_obj_t* btn = lv_event_get_target(e);
                    const char* fname = lv_label_get_text(lv_obj_get_child(btn, 1));
                    String msg = "Fichier : ";
                    msg += fname;
                    lv_obj_t* mbox = lv_msgbox_create(NULL, "Info", msg.c_str(), NULL, true);
                    lv_obj_center(mbox);
                }, LV_EVENT_CLICKED, this);
            }
            has_content = true;
            file = root.openNextFile();
        }
        if (!has_content) {
            lv_list_add_text(list, "Dossier vide");
        }
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x18181A), 0);
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);

        // --- HEADER ---
        header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 56);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x232326), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn_home = lv_btn_create(header);
        lv_obj_set_size(btn_home, 44, 44);
        lv_obj_align(btn_home, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn_home, LV_OPA_0, 0);
        lv_obj_set_style_shadow_opa(btn_home, 0, 0);
        lv_obj_add_event_cb(btn_home, go_home, LV_EVENT_CLICKED, NULL);
        lv_obj_t* l_home = lv_label_create(btn_home);
        lv_label_set_text(l_home, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_home, lv_color_hex(0x0A84FF), 0);
        lv_obj_center(l_home);

        lbl_path = lv_label_create(header);
        lv_label_set_text(lbl_path, "/");
        lv_obj_set_style_text_color(lbl_path, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_path, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_path, LV_ALIGN_CENTER, 0, 0);

        // --- LISTE ---
        list = lv_list_create(main_bg);
        lv_obj_set_size(list, 320, 370);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x18181A), 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

        buildFileList("/");
    }
};

#endif