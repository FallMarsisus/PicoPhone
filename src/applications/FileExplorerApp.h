// FileExplorerApp moderne, style WeatherApp/WifiApp
#ifndef FILE_EXPLORER_APP_H
#define FILE_EXPLORER_APP_H

#include "App.h"
#include "AppManager.h"
#include "PythonApp.h"
#include <lvgl.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <vector>

class FileExplorerApp : public App {
private:
    lv_obj_t* main_bg = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* lbl_path = nullptr;
    lv_obj_t* lbl_storage = nullptr;
    String current_path;

    // Viewer texte plein ecran
    lv_obj_t* viewer_panel = nullptr;

    // Menu contextuel long-press
    lv_obj_t* context_menu = nullptr;
    String selected_path = "";
    bool selected_is_dir = false;
    String suppress_click_path = "";
    
    // Timer pour fermeture différée du menu (évite use-after-free)
    lv_timer_t* close_menu_timer = nullptr;
    
    // Flag pour rebuild différé (évite lv_obj_clean pendant callback)
    bool needs_rebuild = false;

    enum PendingAction {
        ACTION_NONE = 0,
        ACTION_OPEN,
        ACTION_DELETE,
        ACTION_MOVE,
        ACTION_NEW_FOLDER,
    };

    PendingAction pending_action = ACTION_NONE;
    String pending_path = "";
    bool pending_is_dir = false;

    static bool isTextExtension(const String& name) {
        return name.endsWith(".txt") || name.endsWith(".log") ||
               name.endsWith(".json") || name.endsWith(".csv") ||
               name.endsWith(".md") || name.endsWith(".ini");
    }

    static bool isPythonExtension(const String& name) {
        return name.endsWith(".py");
    }

    String fileNameFromPath(const String& path) const {
        int slash = path.lastIndexOf('/');
        if (slash < 0) {
            return path;
        }
        return path.substring(slash + 1);
    }

    String buildFullPath(const String& shortName) const {
        String full = current_path;
        if (!full.endsWith("/")) {
            full += "/";
        }
        full += shortName;
        return full;
    }

    bool consumeSuppressedClick(const String& fullPath) {
        if (suppress_click_path == fullPath) {
            suppress_click_path = "";
            return true;
        }
        return false;
    }

    void showInfoPopup(const String& title, const String& message) {
        lv_obj_t* mbox = lv_msgbox_create(NULL, title.c_str(), message.c_str(), NULL, true);
        lv_obj_center(mbox);
    }

    void updateStorageLabel() {
        if (!lbl_storage) {
            return;
        }
        FSInfo fsinfo;
        if (!LittleFS.info(fsinfo)) {
            lv_label_set_text(lbl_storage, "FS ?");
            return;
        }
        size_t used = fsinfo.usedBytes;
        size_t total = fsinfo.totalBytes;
        char text[48];
        snprintf(text,
                 sizeof(text),
                 "FS %u/%u KB",
                 (unsigned)(used / 1024),
                 (unsigned)(total / 1024));
        lv_label_set_text(lbl_storage, text);
    }

    bool ensureDirExists(const String& path) {
        if (path == "/") {
            return true;
        }
        File d = LittleFS.open(path, "r");
        if (d && d.isDirectory()) {
            d.close();
            return true;
        }
        if (d) {
            d.close();
        }
        return LittleFS.mkdir(path);
    }

    String uniqueNameInDir(const String& dir, const String& baseName) {
        String candidate = dir;
        if (!candidate.endsWith("/")) {
            candidate += "/";
        }
        candidate += baseName;

        File f = LittleFS.open(candidate, "r");
        if (!f) {
            return candidate;
        }
        f.close();

        int idx = 1;
        while (idx < 1000) {
            String alt = dir;
            if (!alt.endsWith("/")) {
                alt += "/";
            }
            alt += baseName;
            alt += "_";
            alt += idx;
            File af = LittleFS.open(alt, "r");
            if (!af) {
                return alt;
            }
            af.close();
            idx++;
        }
        return candidate;
    }

    bool deleteRecursive(const String& path) {
        Serial.printf("[FileExplorer] deleteRecursive: %s\n", path.c_str());
        watchdog_update();
        
        File f = LittleFS.open(path.c_str(), "r");
        if (!f) {
            Serial.println("[FileExplorer] deleteRecursive: failed to open");
            return false;
        }

        if (!f.isDirectory()) {
            f.close();
            Serial.println("[FileExplorer] deleteRecursive: removing file");
            bool ok = LittleFS.remove(path.c_str());
            Serial.printf("[FileExplorer] deleteRecursive: remove result=%d\n", ok);
            return ok;
        }

        Serial.println("[FileExplorer] deleteRecursive: is directory, iterating children");
        // Collecter tous les noms d'enfants d'abord, puis fermer le répertoire
        // avant de supprimer (évite les conflits de handles ouverts)
        std::vector<String> children;
        File child = f.openNextFile();
        while (child) {
            children.push_back(String(child.name()));
            child.close();
            child = f.openNextFile();
        }
        f.close();  // Fermer le répertoire AVANT de supprimer les enfants
        
        for (const auto& childPath : children) {
            Serial.printf("[FileExplorer] deleteRecursive: child=%s\n", childPath.c_str());
            watchdog_update();
            if (!deleteRecursive(childPath)) {
                Serial.println("[FileExplorer] deleteRecursive: child failed");
                return false;
            }
        }
        
        Serial.println("[FileExplorer] deleteRecursive: removing directory");
        bool ok = LittleFS.rmdir(path.c_str());
        Serial.printf("[FileExplorer] deleteRecursive: rmdir result=%d\n", ok);
        return ok;
    }

    void closeContextMenu() {
        // Annuler tout timer de fermeture en cours
        if (close_menu_timer) {
            lv_timer_del(close_menu_timer);
            close_menu_timer = nullptr;
        }
        
        if (context_menu) {
            lv_obj_del(context_menu);
            context_menu = nullptr;
        }
    }
    
    // Fermeture différée pour éviter use-after-free dans les callbacks
    void closeContextMenuDelayed() {
        if (close_menu_timer) {
            return; // Déjà programmé
        }
        
        close_menu_timer = lv_timer_create([](lv_timer_t* t) {
            FileExplorerApp* self = (FileExplorerApp*)t->user_data;
            if (self) {
                self->closeContextMenu();
            }
        }, 50, this); // 50ms après la fin de la callback
        
        lv_timer_set_repeat_count(close_menu_timer, 1);
    }

    void showTextFile(const String& path) {
        viewer_panel = lv_obj_create(main_bg);
        lv_obj_set_size(viewer_panel, 320, 480);
        lv_obj_align(viewer_panel, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_color(viewer_panel, lv_color_hex(0x18181A), 0);
        lv_obj_set_style_border_width(viewer_panel, 0, 0);
        lv_obj_set_style_pad_all(viewer_panel, 0, 0);
        lv_obj_set_style_radius(viewer_panel, 0, 0);
        lv_obj_clear_flag(viewer_panel, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* vh = lv_obj_create(viewer_panel);
        lv_obj_set_size(vh, 320, 48);
        lv_obj_align(vh, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(vh, lv_color_hex(0x232326), 0);
        lv_obj_set_style_border_width(vh, 0, 0);
        lv_obj_set_style_radius(vh, 0, 0);
        lv_obj_clear_flag(vh, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn_close = lv_btn_create(vh);
        lv_obj_set_size(btn_close, 44, 40);
        lv_obj_align(btn_close, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn_close, LV_OPA_0, 0);
        lv_obj_set_style_shadow_opa(btn_close, 0, 0);
        lv_obj_add_event_cb(btn_close,
                            [](lv_event_t* e) {
                                FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
                                if (app && app->viewer_panel) {
                                    lv_obj_del(app->viewer_panel);
                                    app->viewer_panel = nullptr;
                                }
                            },
                            LV_EVENT_CLICKED,
                            this);
        lv_obj_t* lbl_x = lv_label_create(btn_close);
        lv_label_set_text(lbl_x, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(lbl_x, lv_color_hex(0x0A84FF), 0);
        lv_obj_center(lbl_x);

        lv_obj_t* lbl_name = lv_label_create(vh);
        lv_label_set_text(lbl_name, fileNameFromPath(path).c_str());
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_name, LV_ALIGN_CENTER, 0, 0);

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

        File f = LittleFS.open(path, "r");
        if (f) {
            constexpr size_t MAX_DISPLAY = 8192;
            String content = "";
            size_t remaining = f.size();
            if (remaining > MAX_DISPLAY) {
                f.seek(remaining - MAX_DISPLAY);
                content = "...[debut tronque]\n";
            }
            while (f.available() && content.length() < MAX_DISPLAY + 64) {
                content += (char)f.read();
            }
            f.close();
            lv_textarea_set_text(txt_area, content.c_str());
            lv_obj_scroll_to_y(txt_area, LV_COORD_MAX, LV_ANIM_OFF);
        } else {
            lv_textarea_set_text(txt_area, "(impossible d'ouvrir le fichier)");
        }
    }

    void openPath(const String& fullPath, bool isDir) {
        if (isDir) {
            buildFileList(fullPath);
            return;
        }

        String shortName = fileNameFromPath(fullPath);
        if (isPythonExtension(shortName)) {
            PythonApp::queueScriptFromFile(fullPath);
            AppManager::switchTo(APP_PYTHON_TEST);
            return;
        }

        if (isTextExtension(shortName)) {
            showTextFile(fullPath);
            return;
        }

        File fi = LittleFS.open(fullPath, "r");
        String msg = "Fichier : ";
        msg += shortName;
        if (fi) {
            msg += "\nTaille : ";
            msg += fi.size();
            msg += " octets";
            fi.close();
        }
        showInfoPopup("Info", msg);
    }

    bool createFolderInCurrentPath() {
        String base = "NouveauDossier";
        String dst = uniqueNameInDir(current_path, base);
        return LittleFS.mkdir(dst);
    }

    bool moveToMovedFolder(const String& sourcePath) {
        if (!ensureDirExists("/moved")) {
            return false;
        }
        String name = fileNameFromPath(sourcePath);
        String target = uniqueNameInDir("/moved", name);
        return LittleFS.rename(sourcePath, target);
    }

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void up_event(lv_event_t* e) {
        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
        if (!app) {
            return;
        }
        String parent = app->current_path;
        if (parent == "/") {
            return;
        }
        int idx = parent.lastIndexOf('/');
        if (idx <= 0) {
            parent = "/";
        } else {
            parent = parent.substring(0, idx);
        }
        app->buildFileList(parent);
    }

    static void file_item_click_event(lv_event_t* e) {
        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
        if (!app) {
            return;
        }
        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* lbl = lv_obj_get_child(btn, 1);
        if (!lbl) {
            return;
        }
        String shortName = lv_label_get_text(lbl);
        String fullPath = app->buildFullPath(shortName);
        if (app->consumeSuppressedClick(fullPath)) {
            return;
        }

        File f = LittleFS.open(fullPath, "r");
        bool isDir = (f && f.isDirectory());
        if (f) {
            f.close();
        }
        app->openPath(fullPath, isDir);
    }

    static void file_item_long_press_event(lv_event_t* e) {
        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
        if (!app) {
            return;
        }
        lv_obj_t* btn = lv_event_get_target(e);
        lv_obj_t* lbl = lv_obj_get_child(btn, 1);
        if (!lbl) {
            return;
        }
        String shortName = lv_label_get_text(lbl);
        String fullPath = app->buildFullPath(shortName);

        File f = LittleFS.open(fullPath, "r");
        bool isDir = (f && f.isDirectory());
        if (f) {
            f.close();
        }

        app->selected_path = fullPath;
        app->selected_is_dir = isDir;
        app->suppress_click_path = fullPath;

        Serial.printf("[FileExplorer] Long press: %s (isDir=%d)\n", fullPath.c_str(), isDir);

        static const char* btns[] = {"Open", "Delete", "Move", "New Folder", "Cancel", ""};
        // Stocker title pour éviter destruction avant utilisation par LVGL
        static String s_title;
        static String s_name;
        s_title = isDir ? "Folder" : "File";
        s_name = shortName;
        
        app->closeContextMenu();
        app->context_menu = lv_msgbox_create(NULL, s_title.c_str(), s_name.c_str(), btns, true);
        lv_obj_center(app->context_menu);
        lv_obj_add_event_cb(app->context_menu,
                            [](lv_event_t* ev) {
                                FileExplorerApp* self = (FileExplorerApp*)lv_event_get_user_data(ev);
                                if (!self) {
                                    return;
                                }
                                lv_obj_t* mbox = lv_event_get_target(ev);
                                const char* action = lv_msgbox_get_active_btn_text(mbox);
                                if (!action) {
                                    return;
                                }

                                Serial.printf("[FileExplorer] Action: %s\n", action);

                                // Ne pas faire d'opération lourde LVGL/FS dans cette callback.
                                self->pending_action = ACTION_NONE;
                                self->pending_path = self->selected_path;
                                self->pending_is_dir = self->selected_is_dir;

                                if (strcmp(action, "Open") == 0) {
                                    self->pending_action = ACTION_OPEN;
                                } else if (strcmp(action, "Delete") == 0) {
                                    self->pending_action = ACTION_DELETE;
                                } else if (strcmp(action, "Move") == 0) {
                                    self->pending_action = ACTION_MOVE;
                                } else if (strcmp(action, "New Folder") == 0) {
                                    self->pending_action = ACTION_NEW_FOLDER;
                                } else if (strcmp(action, "Cancel") == 0) {
                                    Serial.println("[FileExplorer] Cancelled");
                                }

                                // Fermeture différée pour éviter use-after-free (on est dans la callback du menu)
                                Serial.println("[FileExplorer] Closing menu (delayed)...");
                                self->closeContextMenuDelayed();
                                Serial.println("[FileExplorer] Callback done");
                            },
                            LV_EVENT_VALUE_CHANGED,
                            app);
    }

    void buildFileList(const String& path) {
        current_path = path;
        lv_label_set_text(lbl_path, path.c_str());
        lv_obj_clean(list);

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
            if (shortName.startsWith(path)) {
                shortName = shortName.substring(path.length());
            }
            if (shortName.startsWith("/")) {
                shortName = shortName.substring(1);
            }
            if (shortName.length() == 0) {
                file = root.openNextFile();
                continue;
            }

            lv_obj_t* item = lv_list_add_btn(list,
                                             isDir ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
                                             shortName.c_str());
            lv_obj_add_event_cb(item, file_item_click_event, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(item, file_item_long_press_event, LV_EVENT_LONG_PRESSED, this);

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
        needs_rebuild = false;
        pending_action = ACTION_NONE;
        pending_path = "";
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x18181A), 0);
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);

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
        lv_obj_align(lbl_path, LV_ALIGN_LEFT_MID, 56, 0);

        lbl_storage = lv_label_create(header);
        lv_label_set_text(lbl_storage, "FS ...");
        lv_obj_set_style_text_color(lbl_storage, lv_color_hex(0x9BC1FF), 0);
        lv_obj_set_style_text_font(lbl_storage, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_storage, LV_ALIGN_RIGHT_MID, -8, 0);

        list = lv_list_create(main_bg);
        lv_obj_set_size(list, 320, 370);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x18181A), 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

        updateStorageLabel();
        buildFileList("/");
    }

    void update() override {
        if (pending_action != ACTION_NONE) {
            PendingAction action = pending_action;
            pending_action = ACTION_NONE;
            
            // CRITIQUE: Fermer le context menu IMMÉDIATEMENT avant toute opération
            // Le closeContextMenuDelayed() avait créé un timer, mais il faut libérer
            // le menu MAINTENANT pour éviter tout conflit avec les opérations FS/UI
            closeContextMenu();

            if (action == ACTION_OPEN) {
                Serial.println("[FileExplorer] UPDATE: Opening...");
                openPath(pending_path, pending_is_dir);
            } else if (action == ACTION_DELETE) {
                Serial.printf("[FileExplorer] UPDATE: Delete %s (isDir=%d)\n", pending_path.c_str(), pending_is_dir);
                watchdog_update();
                bool ok;
                if (pending_is_dir) {
                    ok = deleteRecursive(pending_path);
                } else {
                    ok = LittleFS.remove(pending_path.c_str());
                }
                watchdog_update();
                Serial.printf("[FileExplorer] UPDATE: Delete result=%d\n", ok);
                needs_rebuild = true;
            } else if (action == ACTION_MOVE) {
                Serial.printf("[FileExplorer] UPDATE: Move %s\n", pending_path.c_str());
                bool ok = moveToMovedFolder(pending_path);
                needs_rebuild = true;
            } else if (action == ACTION_NEW_FOLDER) {
                Serial.println("[FileExplorer] UPDATE: Creating folder...");
                bool ok = createFolderInCurrentPath();
                needs_rebuild = true;
            }
        }

        // Rebuild différé de la liste après actions (Delete, Move, New Folder)
        if (needs_rebuild) {
            Serial.println("[FileExplorer] UPDATE: Rebuilding file list...");
            needs_rebuild = false;
            buildFileList(current_path);
            updateStorageLabel();
            Serial.println("[FileExplorer] UPDATE: File list rebuilt");
        }
    }

    void stop() override {
        closeContextMenu();
        viewer_panel = nullptr;
        suppress_click_path = "";
        selected_path = "";
    }
};

#endif