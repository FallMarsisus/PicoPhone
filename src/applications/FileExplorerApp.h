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

    // ── Viewer texte plein écran ─────────────────────────────────────────
    lv_obj_t* viewer_panel = nullptr;

    void showTextFile(const String& path) {
        // Panneau plein écran par-dessus l'explorer
        viewer_panel = lv_obj_create(main_bg);
        lv_obj_set_size(viewer_panel, 320, 480);
        lv_obj_align(viewer_panel, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(viewer_panel, lv_color_hex(0x18181A), 0);
        lv_obj_set_style_border_width(viewer_panel, 0, 0);
        lv_obj_set_style_pad_all(viewer_panel, 0, 0);
        lv_obj_set_style_radius(viewer_panel, 0, 0);
        lv_obj_clear_flag(viewer_panel, LV_OBJ_FLAG_SCROLLABLE);

        // Header viewer
        lv_obj_t* vh = lv_obj_create(viewer_panel);
        lv_obj_set_size(vh, 320, 48);
        lv_obj_align(vh, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(vh, lv_color_hex(0x232326), 0);
        lv_obj_set_style_border_width(vh, 0, 0);
        lv_obj_set_style_radius(vh, 0, 0);
        lv_obj_clear_flag(vh, LV_OBJ_FLAG_SCROLLABLE);

        // Bouton fermer
        lv_obj_t* btn_close = lv_btn_create(vh);
        lv_obj_set_size(btn_close, 44, 40);
        lv_obj_align(btn_close, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn_close, LV_OPA_0, 0);
        lv_obj_set_style_shadow_opa(btn_close, 0, 0);
        lv_obj_add_event_cb(btn_close, [](lv_event_t* e) {
            FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
            if (app && app->viewer_panel) {
                lv_obj_del(app->viewer_panel);
                app->viewer_panel = nullptr;
            }
        }, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_x = lv_label_create(btn_close);
        lv_label_set_text(lbl_x, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(lbl_x, lv_color_hex(0x0A84FF), 0);
        lv_obj_center(lbl_x);

        // Nom du fichier
        lv_obj_t* lbl_name = lv_label_create(vh);
        String shortname = path;
        int sl = shortname.lastIndexOf('/');
        if (sl >= 0) shortname = shortname.substring(sl + 1);
        lv_label_set_text(lbl_name, shortname.c_str());
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_name, LV_ALIGN_CENTER, 0, 0);

        // Zone scrollable pour le contenu
        lv_obj_t* txt_area = lv_textarea_create(viewer_panel);
        lv_obj_set_size(txt_area, 320, 432);
        lv_obj_align(txt_area, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(txt_area, lv_color_hex(0x0E0E0F), 0);
        lv_obj_set_style_text_color(txt_area, lv_color_hex(0xC7C7CC), 0);
        lv_obj_set_style_text_font(txt_area, &lv_font_montserrat_12, 0);
        lv_obj_set_style_border_width(txt_area, 0, 0);
        lv_obj_set_style_pad_all(txt_area, 6, 0);
        lv_textarea_set_cursor_click_pos(txt_area, false);
        lv_textarea_set_one_line(txt_area, false);

        // Lire le fichier (max 8 KB affichés pour ne pas saturer LVGL)
        File f = LittleFS.open(path, "r");
        if (f) {
            constexpr size_t MAX_DISPLAY = 8192;
            String content = "";
            size_t remaining = f.size();
            if (remaining > MAX_DISPLAY) {
                // Sauter les premiers octets pour afficher la FIN (plus récente)
                f.seek(remaining - MAX_DISPLAY);
                content = "...[début tronqué]\n";
            }
            while (f.available() && content.length() < MAX_DISPLAY + 64) {
                content += (char)f.read();
            }
            f.close();
            lv_textarea_set_text(txt_area, content.c_str());
            // Scroll en bas pour voir les logs les plus récents
            lv_obj_scroll_to_y(txt_area, LV_COORD_MAX, LV_ANIM_OFF);
        } else {
            lv_textarea_set_text(txt_area, "(impossible d'ouvrir le fichier)");
        }
    }

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
                bool is_txt = shortName.endsWith(".txt") || shortName.endsWith(".log")
                           || shortName.endsWith(".json") || shortName.endsWith(".csv");
                if (is_txt) {
                    // Viewer texte scrollable
                    lv_obj_add_event_cb(item, [](lv_event_t* e) {
                        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
                        if (!app) return;
                        lv_obj_t* btn = lv_event_get_target(e);
                        const char* fname = lv_label_get_text(lv_obj_get_child(btn, 1));
                        String full = app->current_path;
                        if (!full.endsWith("/")) full += "/";
                        full += fname;
                        app->showTextFile(full);
                    }, LV_EVENT_CLICKED, this);
                } else {
                    // Autres fichiers : popup info taille
                    lv_obj_add_event_cb(item, [](lv_event_t* e) {
                        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
                        lv_obj_t* btn = lv_event_get_target(e);
                        const char* fname = lv_label_get_text(lv_obj_get_child(btn, 1));
                        String full = app ? (app->current_path + (app->current_path.endsWith("/") ? "" : "/") + fname) : fname;
                        File fi = LittleFS.open(full, "r");
                        String msg = "Fichier : ";
                        msg += fname;
                        if (fi) { msg += "\nTaille : "; msg += fi.size(); msg += " octets"; fi.close(); }
                        lv_obj_t* mbox = lv_msgbox_create(NULL, "Info", msg.c_str(), NULL, true);
                        lv_obj_center(mbox);
                    }, LV_EVENT_CLICKED, this);
                }
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