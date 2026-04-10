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
    static constexpr lv_coord_t status_bar_height = 18;

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

        statusBar = lv_obj_create(lv_layer_top());
        lv_obj_set_size(statusBar, 320, status_bar_height);
        lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(statusBar, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(statusBar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(statusBar, 0, 0);
        lv_obj_set_style_radius(statusBar, 0, 0);
        lv_obj_set_style_pad_left(statusBar, 4, 0);
        lv_obj_set_style_pad_right(statusBar, 4, 0);
        lv_obj_set_style_pad_top(statusBar, 0, 0);
        lv_obj_set_style_pad_bottom(statusBar, 0, 0);
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_GESTURE_BUBBLE);

        statusTime = lv_label_create(statusBar);
        lv_obj_set_style_text_color(statusTime, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusTime, &lv_font_montserrat_12, 0);
        lv_obj_align(statusTime, LV_ALIGN_LEFT_MID, 0, 0);

        statusNotif = lv_label_create(statusBar);
        lv_label_set_text(statusNotif, "");
        lv_obj_set_style_text_color(statusNotif, lv_color_hex(0xFF453A), 0);
        lv_obj_set_style_text_font(statusNotif, &lv_font_montserrat_12, 0);
        lv_obj_align(statusNotif, LV_ALIGN_CENTER, 0, 0);

        statusNetwork = lv_label_create(statusBar);
        lv_obj_set_style_text_color(statusNetwork, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusNetwork, &lv_font_montserrat_12, 0);
        lv_obj_set_width(statusNetwork, 32);
        lv_label_set_long_mode(statusNetwork, LV_LABEL_LONG_CLIP);
        lv_obj_align(statusNetwork, LV_ALIGN_RIGHT_MID, -64, 0);

        statusBattery = lv_label_create(statusBar);
        lv_obj_set_style_text_color(statusBattery, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusBattery, &lv_font_montserrat_12, 0);
        lv_obj_set_width(statusBattery, 52);
        lv_label_set_long_mode(statusBattery, LV_LABEL_LONG_CLIP);
        lv_obj_align(statusBattery, LV_ALIGN_RIGHT_MID, 0, 0);

        lv_obj_move_foreground(statusBar);
    }

    void updateStatusBar() {
        if (!statusBar) return;

        lv_obj_move_foreground(statusBar);

        const bool show_on_screen = (currentAppID != APP_HOME && currentAppID != APP_OLD_HOME);
        if (!show_on_screen) {
            lv_obj_add_flag(statusBar, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_HIDDEN);

        char time_buf[6] = {0};
        time_t now = time(nullptr);
        struct tm timeinfo;
        if (now > 0 && localtime_r(&now, &timeinfo) != nullptr) {
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
        } else {
            snprintf(time_buf, sizeof(time_buf), "--:--");
        }
        lv_label_set_text(statusTime, time_buf);

        lv_label_set_text(statusNetwork, network_status().c_str());

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
            lv_label_set_text(statusNotif, "!");
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