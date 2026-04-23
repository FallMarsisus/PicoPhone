#ifndef APPMANAGER_H
#define APPMANAGER_H

#include <time.h>
#include "system/LockScreen.h"
#include "system/ControlCenter.h"
#include "system/Battery.h"
#include "system/LTE.h"
#include "system/NotificationCenter.h"

// La liste de toutes les applications
enum AppID {
    APP_HOME,
    APP_BOOTLOADER,
    APP_WIFI,
    APP_WALLET,
    APP_TOUCH_CALIB,
    APP_WEATHER,
    APP_TELEGRAM,
    APP_VELIB,
    APP_2048,
    APP_SKETCH,
    APP_CALC,
    APP_SETTINGS,
    APP_CONTACTS,
    APP_TIMER, 
    APP_EXPLORER, 
    APP_OLD_HOME,
    APP_PHONE, 
    APP_SMS,
    APP_WEBRADIO, 
    APP_PYTHON_TEST,
    APP_STORE,
    APP_CRYPTO,
    APP_NEWS,
    APP_AIR_QUALITY,
    APP_BAMBU,
    APP_CHATBOT,
    APP_VECTOR_MAP,
};

class AppManager {
public:
    LockScreen lockScreen;
    ControlCenter controlCenter;

    static constexpr lv_coord_t statusBarHeight() {
        return status_bar_height;
    }

private:
    lv_obj_t* statusBar = nullptr;
    lv_obj_t* statusTime = nullptr;
    lv_obj_t* statusNetwork = nullptr;
    lv_obj_t* statusNotif = nullptr;
    lv_obj_t* statusBattery = nullptr;
    uint32_t status_last_update_ms = 0;
    bool status_was_visible = false;
    static constexpr lv_coord_t status_bar_height = 20;

    static const char* battery_icon(uint8_t percent) {
        if (percent >= 80) return LV_SYMBOL_BATTERY_FULL;
        if (percent >= 60) return LV_SYMBOL_BATTERY_3;
        if (percent >= 40) return LV_SYMBOL_BATTERY_2;
        if (percent >= 20) return LV_SYMBOL_BATTERY_1;
        return LV_SYMBOL_BATTERY_EMPTY;
    }

    static String network_status() {
        if (LTE::isAirplaneMode()) {
            return "X";
        }
        if (LTE::isEnabled()) {
            int signal = LTE::getSignal();
            if (signal > 0) {
                switch (signal) {
                    case 1: return "I";
                    case 2: return "II";
                    case 3: return "III";
                    default: return "IIII";
                }
            }
            return "-";
        }
        return "-";
    }

    void createStatusBar() {
        if (statusBar) return;

        lv_disp_t* disp = lv_disp_get_default();
        const lv_coord_t screen_w = disp ? lv_disp_get_hor_res(disp) : 320;

        statusBar = lv_obj_create(lv_layer_top());
        lv_obj_set_size(statusBar, screen_w, status_bar_height);
        lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_grad_dir(statusBar, LV_GRAD_DIR_VER, 0);
        lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x0B1220), 0);
        lv_obj_set_style_bg_grad_color(statusBar, lv_color_hex(0x162338), 0);
        lv_obj_set_style_bg_opa(statusBar, LV_OPA_80, 0);
        lv_obj_set_style_border_width(statusBar, 1, 0);
        lv_obj_set_style_border_color(statusBar, lv_color_hex(0x26344F), 0);
        lv_obj_set_style_border_side(statusBar, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_opa(statusBar, LV_OPA_80, 0);
        lv_obj_set_style_radius(statusBar, 0, 0);
        lv_obj_set_style_pad_left(statusBar, 6, 0);
        lv_obj_set_style_pad_right(statusBar, 6, 0);
        lv_obj_set_style_pad_top(statusBar, 0, 0);
        lv_obj_set_style_pad_bottom(statusBar, 0, 0);
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_GESTURE_BUBBLE);

        statusTime = lv_label_create(statusBar);
        lv_obj_set_style_text_color(statusTime, lv_color_hex(0xDCE7FF), 0);
        lv_obj_set_style_text_font(statusTime, &lv_font_montserrat_12, 0);
        lv_obj_align(statusTime, LV_ALIGN_LEFT_MID, 0, 0);

        statusNotif = lv_label_create(statusBar);
        lv_label_set_text(statusNotif, "");
        lv_obj_set_style_text_color(statusNotif, lv_color_hex(0x67B6FF), 0);
        lv_obj_set_style_text_font(statusNotif, &lv_font_montserrat_12, 0);
        lv_obj_align(statusNotif, LV_ALIGN_CENTER, 0, 0);

        statusNetwork = lv_label_create(statusBar);
        lv_obj_set_style_text_color(statusNetwork, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusNetwork, &lv_font_montserrat_12, 0);
        lv_obj_set_width(statusNetwork, 42);
        lv_label_set_long_mode(statusNetwork, LV_LABEL_LONG_CLIP);
        lv_obj_align(statusNetwork, LV_ALIGN_RIGHT_MID, -70, 0);

        statusBattery = lv_label_create(statusBar);
        lv_obj_set_style_text_color(statusBattery, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusBattery, &lv_font_montserrat_12, 0);
        lv_obj_set_width(statusBattery, 64);
        lv_label_set_long_mode(statusBattery, LV_LABEL_LONG_CLIP);
        lv_obj_align(statusBattery, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_move_foreground(statusBar);
    }

    void updateStatusBar() {
        if (!statusBar) return;

        lv_obj_move_foreground(statusBar);

        const bool show_on_screen = (currentAppID != APP_HOME && currentAppID != APP_OLD_HOME);
        const bool hide_for_lock = lockScreen.isLocked();
        if (!show_on_screen || hide_for_lock) {
            lv_obj_add_flag(statusBar, LV_OBJ_FLAG_HIDDEN);
            status_was_visible = false;
            return;
        }

        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_HIDDEN);

        const uint32_t now_ms = millis();
        const uint32_t refresh_interval_ms = lockScreen.isLocked() ? 2500 : 1000;
        if (status_was_visible && (uint32_t)(now_ms - status_last_update_ms) < refresh_interval_ms) {
            return;
        }
        status_was_visible = true;
        status_last_update_ms = now_ms;

        char time_buf[6] = {0};
        time_t now = time(nullptr);
        struct tm timeinfo;
        if (now > 0 && localtime_r(&now, &timeinfo) != nullptr) {
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        } else {
            snprintf(time_buf, sizeof(time_buf), "--:--");
        }
        lv_label_set_text(statusTime, time_buf);

        const String net = network_status();
        lv_label_set_text(statusNetwork, net.c_str());
        if (net == "X") {
            lv_obj_set_style_text_color(statusNetwork, lv_color_hex(0xFFB15A), 0);
        } else if (net == "-") {
            lv_obj_set_style_text_color(statusNetwork, lv_color_hex(0xA0AEC8), 0);
        } else {
            lv_obj_set_style_text_color(statusNetwork, lv_color_hex(0xCFE4FF), 0);
        }

        const uint8_t batt = battery::read_percent();
        const bool charging = battery::is_charging() || battery::is_external_power();
        const bool eco_manual = battery::is_manual_saver_enabled() && !charging;

        lv_obj_set_style_text_color(statusBattery,
                        eco_manual ? lv_color_hex(0xF2C94C) : lv_color_white(),
                        0);

        char batt_buf[20];
        snprintf(batt_buf, sizeof(batt_buf), "%s%s %u%%",
                 charging ? LV_SYMBOL_CHARGE : "",
                 battery_icon(batt),
                 batt);
        lv_label_set_text(statusBattery, batt_buf);

        char app[20] = {0};
        char title[36] = {0};
        char body[96] = {0};
        if (notifications::center().get_latest(0, app, title, body)) {
            lv_label_set_text(statusNotif, LV_SYMBOL_BELL);
        } else {
            lv_label_set_text(statusNotif, "");
        }
    }

public:
    // --- GESTION DU CHANGEMENT D'APP (Statique) ---
    static volatile AppID nextAppID;
    static volatile bool switchRequested;
    static volatile bool ccOpenRequested;
    static volatile AppID currentAppID;

    static void switchTo(AppID id) {
        __atomic_store_n(&nextAppID, id, __ATOMIC_RELEASE);
        __atomic_store_n(&switchRequested, true, __ATOMIC_RELEASE);
    }

    static bool consumeSwitchRequest(AppID& outId) {
        if (!__atomic_load_n(&switchRequested, __ATOMIC_ACQUIRE)) {
            return false;
        }
        outId = __atomic_load_n(&nextAppID, __ATOMIC_ACQUIRE);
        __atomic_store_n(&switchRequested, false, __ATOMIC_RELEASE);
        return true;
    }
    
    static void openControlCenter() {
        __atomic_store_n(&ccOpenRequested, true, __ATOMIC_RELEASE);
    }

    static void setCurrentApp(AppID id) {
        __atomic_store_n(&currentAppID, id, __ATOMIC_RELEASE);
    }

    // --- GESTION DU SYSTEME ---
    
    void init() {
        lockScreen.init();
        controlCenter.init();
        createStatusBar();
    }

    void update() {
        lockScreen.update();
        
        // Ouvrir le Control Center si demandé (et pas verrouillé)
        if (__atomic_exchange_n(&ccOpenRequested, false, __ATOMIC_ACQ_REL)) {
            if (!lockScreen.isLocked()) {
                controlCenter.toggle();
            }
        }

        updateStatusBar();
    }
    
    bool isLocked() const { return lockScreen.isLocked(); }
    bool isControlCenterOpen() const { return controlCenter.isOpen(); }
};

// Initialisation des variables statiques
volatile AppID AppManager::nextAppID = APP_HOME;
volatile bool AppManager::switchRequested = false;
volatile bool AppManager::ccOpenRequested = false;
volatile AppID AppManager::currentAppID = APP_HOME;

#endif