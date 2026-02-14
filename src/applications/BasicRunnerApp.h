#ifndef BASIC_RUNNER_APP_H
#define BASIC_RUNNER_APP_H

#include "App.h"
#include "AppManager.h"
#include "../system/BasicRuntime.h"

class BasicRunnerApp : public App {
private:
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* stage = nullptr;
    BasicRuntime runtime;

    static String pendingScriptPath;

    static void go_home(lv_event_t* e) {
        (void)e;
        AppManager::switchTo(APP_HOME);
    }

    static void go_explorer(lv_event_t* e) {
        (void)e;
        AppManager::switchTo(APP_FILE_EXPLORER);
    }

    static void rerun(lv_event_t* e) {
        BasicRunnerApp* app = (BasicRunnerApp*)lv_event_get_user_data(e);
        if (app) app->runCurrentScript();
    }

    void runCurrentScript() {
        if (!statusLabel || !stage) return;

        lv_obj_clean(stage);
        runtime.end();

        if (!runtime.begin(stage)) {
            lv_label_set_text_fmt(statusLabel, "Erreur runtime: %s", runtime.error().c_str());
            return;
        }

        String target = pendingScriptPath;
        if (target.isEmpty()) target = "/apps/test_ui.bas";

        if (runtime.runFile(target)) {
            lv_label_set_text_fmt(statusLabel, "Script: %s", target.c_str());
        } else {
            lv_label_set_text_fmt(statusLabel, "Erreur script: %s", runtime.error().c_str());
        }
    }

public:
    static void setScriptPath(const String& path) {
        pendingScriptPath = path;
    }

    void start(lv_obj_t* parent) override {
        lv_obj_set_style_bg_color(parent, lv_color_hex(0x0b0b0b), 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* header = lv_obj_create(parent);
        lv_obj_set_size(header, 320, 50);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1f1f1f), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* backBtn = lv_btn_create(header);
        lv_obj_set_size(backBtn, 40, 40);
        lv_obj_align(backBtn, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_add_event_cb(backBtn, go_home, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(lv_label_create(backBtn), LV_SYMBOL_HOME);

        lv_obj_t* expBtn = lv_btn_create(header);
        lv_obj_set_size(expBtn, 40, 40);
        lv_obj_align(expBtn, LV_ALIGN_LEFT_MID, 36, 0);
        lv_obj_add_event_cb(expBtn, go_explorer, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(lv_label_create(expBtn), LV_SYMBOL_DIRECTORY);

        lv_obj_t* runBtn = lv_btn_create(header);
        lv_obj_set_size(runBtn, 40, 40);
        lv_obj_align(runBtn, LV_ALIGN_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(runBtn, rerun, LV_EVENT_CLICKED, this);
        lv_label_set_text(lv_label_create(runBtn), LV_SYMBOL_REFRESH);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "BASIC Runner");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);

        statusLabel = lv_label_create(parent);
        lv_label_set_text(statusLabel, "Initialisation...");
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xb0b0b0), 0);
        lv_obj_align(statusLabel, LV_ALIGN_TOP_LEFT, 10, 55);

        stage = lv_obj_create(parent);
        lv_obj_set_size(stage, 316, 410);
        lv_obj_align(stage, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_color(stage, lv_color_hex(0x151515), 0);
        lv_obj_set_style_border_color(stage, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(stage, 1, 0);
        lv_obj_clear_flag(stage, LV_OBJ_FLAG_SCROLLABLE);

        runCurrentScript();
    }

    void stop() override {
        runtime.end();
    }
};

String BasicRunnerApp::pendingScriptPath = "/apps/test_ui.bas";

#endif
