#ifndef FILE_EXPLORER_APP_H
#define FILE_EXPLORER_APP_H

#include "App.h"
#include "AppManager.h"
#include "BasicRunnerApp.h"
#include "../system/BasicRuntime.h"
#include <LittleFS.h>
#include <vector>

class FileExplorerApp : public App {
private:
    lv_obj_t* list = nullptr;
    lv_obj_t* statusLabel = nullptr;
    std::vector<String> entries;

    static bool isBasicFile(const String& path) {
        return path.endsWith(".bas") || path.endsWith(".BAS");
    }

    static void go_home(lv_event_t* e) {
        (void)e;
        AppManager::switchTo(APP_HOME);
    }

    static void open_transfer(lv_event_t* e) {
        (void)e;
        AppManager::switchTo(APP_FILE_TRANSFER);
    }

    static void refresh_event(lv_event_t* e) {
        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
        if (app) app->refreshList();
    }

    static void open_file(lv_event_t* e) {
        FileExplorerApp* app = (FileExplorerApp*)lv_event_get_user_data(e);
        if (!app) return;

        lv_obj_t* btn = lv_event_get_target(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
        if (idx < 0 || idx >= (int)app->entries.size()) return;

        const String& path = app->entries[(size_t)idx];
        if (!isBasicFile(path)) {
            if (app->statusLabel) lv_label_set_text(app->statusLabel, "Fichier non .bas (non executable)");
            return;
        }

        BasicRunnerApp::setScriptPath(path);
        AppManager::switchTo(APP_BASIC_RUNNER);
    }

    void addFileButton(const String& filePath) {
        String title = filePath;
        int slash = title.lastIndexOf('/');
        if (slash >= 0) title = title.substring(slash + 1);

        const char* icon = isBasicFile(filePath) ? LV_SYMBOL_PLAY : LV_SYMBOL_FILE;
        lv_obj_t* btn = lv_list_add_btn(list, icon, title.c_str());
        lv_obj_set_user_data(btn, (void*)(intptr_t)(entries.size() - 1));
        lv_obj_add_event_cb(btn, open_file, LV_EVENT_CLICKED, this);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    }

    void scanDir(const String& path) {
        File dir = LittleFS.open(path, "r");
        if (!dir || !dir.isDirectory()) return;

        File f = dir.openNextFile();
        while (f) {
            String name = String(f.name());
            if (!f.isDirectory()) {
                entries.push_back(name);
            }
            f = dir.openNextFile();
        }
    }

    void refreshList() {
        if (!list) return;
        lv_obj_clean(list);
        entries.clear();

        if (!basicfs::ensureMounted()) {
            lv_list_add_text(list, "LittleFS indisponible");
            if (statusLabel) lv_label_set_text(statusLabel, "Erreur montage LittleFS");
            return;
        }

        scanDir("/");
        if (LittleFS.exists("/apps")) scanDir("/apps");

        if (entries.empty()) {
            lv_list_add_text(list, "Aucun fichier");
            if (statusLabel) lv_label_set_text(statusLabel, "Deposez des .bas via FileTransfer");
            return;
        }

        for (const String& p : entries) {
            addFileButton(p);
        }

        if (statusLabel) lv_label_set_text(statusLabel, "Touchez un .bas pour l'executer");
    }

public:
    void start(lv_obj_t* parent) override {
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x0d1117), 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* header = lv_obj_create(parent);
        lv_obj_set_size(header, 320, 50);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x161b22), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* backBtn = lv_btn_create(header);
        lv_obj_set_size(backBtn, 40, 40);
        lv_obj_align(backBtn, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_add_event_cb(backBtn, go_home, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(lv_label_create(backBtn), LV_SYMBOL_LEFT);

        lv_obj_t* transferBtn = lv_btn_create(header);
        lv_obj_set_size(transferBtn, 40, 40);
        lv_obj_align(transferBtn, LV_ALIGN_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(transferBtn, open_transfer, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(lv_label_create(transferBtn), LV_SYMBOL_UPLOAD);

        lv_obj_t* refreshBtn = lv_btn_create(header);
        lv_obj_set_size(refreshBtn, 40, 40);
        lv_obj_align(refreshBtn, LV_ALIGN_RIGHT_MID, -36, 0);
        lv_obj_add_event_cb(refreshBtn, refresh_event, LV_EVENT_CLICKED, this);
        lv_label_set_text(lv_label_create(refreshBtn), LV_SYMBOL_REFRESH);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Fichiers");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);

        statusLabel = lv_label_create(parent);
        lv_label_set_text(statusLabel, "Chargement...");
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xa5a5a5), 0);
        lv_obj_align(statusLabel, LV_ALIGN_TOP_LEFT, 10, 55);

        list = lv_list_create(parent);
        lv_obj_set_size(list, 320, 418);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x0d1117), 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

        refreshList();
    }
};

#endif
