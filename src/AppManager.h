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

    static void status_swipe_cb(lv_event_t* e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_BOTTOM) {
            AppManager::openControlCenter();
        }
    }

    void createStatusBar() {
        if (statusBar) return;

        lv_disp_t* disp = lv_disp_get_default();
        const lv_coord_t screen_w = disp ? lv_disp_get_hor_res(disp) : 320;

        statusBar = lv_obj_create(lv_layer_top());
        lv_obj_set_size(statusBar, screen_w, status_bar_height);
        lv_obj_align(statusBar, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(statusBar, LV_OPA_TRANSP, 0); // Transparent au départ
        lv_obj_set_style_border_width(statusBar, 0, 0);
        lv_obj_set_style_radius(statusBar, 0, 0); // Pas de coins arrondis
        lv_obj_set_style_pad_left(statusBar, 10, 0);
        lv_obj_set_style_pad_right(statusBar, 10, 0);
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_GESTURE_BUBBLE); // Capturer swipe si besoin
        lv_obj_add_event_cb(statusBar, status_swipe_cb, LV_EVENT_GESTURE, nullptr);
        lv_obj_add_flag(statusBar, LV_OBJ_FLAG_CLICKABLE);
        // Un gestionnaire pour Swipe Center ? AppManager a t il top_swipe_cb ? Non, on fera appel a controlCenter

        statusNotif = lv_label_create(statusBar);
        lv_label_set_text(statusNotif, "Recherche...");
        lv_obj_set_style_text_color(statusNotif, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusNotif, &lv_font_montserrat_12, 0);
        lv_obj_align(statusNotif, LV_ALIGN_LEFT_MID, 0, 0);

        statusTime = lv_label_create(statusBar);
        lv_obj_set_style_text_color(statusTime, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusTime, &lv_font_montserrat_12, 0);
        lv_obj_align(statusTime, LV_ALIGN_CENTER, 0, 0);

        // Container right
        lv_obj_t* icon_zone = lv_obj_create(statusBar);
        lv_obj_set_size(icon_zone, 80, 20); 
        lv_obj_align(icon_zone, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_border_width(icon_zone, 0, 0);
        lv_obj_set_style_bg_opa(icon_zone, LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(icon_zone, LV_OBJ_FLAG_SCROLLABLE);

        statusNetwork = lv_label_create(icon_zone);
        lv_label_set_text(statusNetwork, "||||");
        lv_obj_set_style_text_color(statusNetwork, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusNetwork, &lv_font_montserrat_12, 0);
        lv_obj_align(statusNetwork, LV_ALIGN_LEFT_MID, 0, 0);

        statusBattery = lv_label_create(icon_zone);
        lv_obj_set_style_text_color(statusBattery, lv_color_white(), 0);
        lv_obj_set_style_text_font(statusBattery, &lv_font_montserrat_12, 0);
        lv_obj_align(statusBattery, LV_ALIGN_RIGHT_MID, -5, 0);

        lv_obj_move_foreground(statusBar);
    }

    void updateStatusBar() {
        if (!statusBar) return;
        lv_obj_move_foreground(statusBar);

        const bool show_on_screen = (currentAppID != APP_OLD_HOME); // Toujours visible, sauf vieux home
        const bool hide_for_lock = lockScreen.isLocked();
        if (!show_on_screen || hide_for_lock) {
            lv_obj_add_flag(statusBar, LV_OBJ_FLAG_HIDDEN);
            status_was_visible = false;
            return;
        }
        lv_obj_clear_flag(statusBar, LV_OBJ_FLAG_HIDDEN);
        
        // Style de la topbar (Transparent sur HOME, semi-opaque ailleurs)
        // Style de la topbar
        if (currentAppID == APP_HOME) {
            lv_obj_set_style_bg_opa(statusBar, LV_OPA_TRANSP, 0);
        } else {
            // Prendre la couleur de fond de l'écran actif
            lv_color_t bg_color = lv_obj_get_style_bg_color(lv_scr_act(), 0);
            // Vérifier s'il y a un composant principal prenant tout l'écran pour piocher sa couleur
            if (lv_obj_get_child_cnt(lv_scr_act()) > 0) {
                lv_obj_t* first_child = lv_obj_get_child(lv_scr_act(), 0);
                if (first_child) {
                    bg_color = lv_obj_get_style_bg_color(first_child, 0);
                }
            }
            lv_obj_set_style_bg_color(statusBar, bg_color, 0);
            lv_obj_set_style_bg_opa(statusBar, LV_OPA_COVER, 0); // Opaque
        }

        // Ajuster la couleur du texte en fonction de la couleur de fond
        lv_color_t current_bg = lv_obj_get_style_bg_color(statusBar, 0);
        uint8_t brightness = lv_color_brightness(current_bg);
        
        lv_color_t text_color = (brightness > 180 || currentAppID == APP_HOME) ? lv_color_white() : lv_color_white(); // wait, APP_HOME text is white. So if not home and brightness > 128 -> black.
        if (currentAppID != APP_HOME && brightness > 150) {
            text_color = lv_color_black();
        }
        
        lv_obj_set_style_text_color(statusTime, text_color, 0);
        lv_obj_set_style_text_color(statusNetwork, text_color, 0);
        lv_obj_set_style_text_color(statusBattery, text_color, 0);
        lv_obj_set_style_text_color(statusNotif, text_color, 0);
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
        if (net == "X") {
            lv_label_set_text(statusNetwork, "No S.");
            lv_obj_set_style_text_color(statusNetwork, lv_color_hex(0xFFB15A), 0);
        } else if (net == "-") {
            lv_label_set_text(statusNetwork, "...");
            lv_obj_set_style_text_color(statusNetwork, lv_color_hex(0xA0AEC8), 0);
        } else {
            lv_label_set_text(statusNetwork, net.c_str());
            lv_obj_set_style_text_color(statusNetwork, lv_color_white(), 0);
        }

        const uint8_t batt = battery::read_percent();
        const bool charging = battery::is_charging() || battery::is_external_power();
        const bool eco_manual = battery::is_manual_saver_enabled() && !charging;
        lv_obj_set_style_text_color(statusBattery, eco_manual ? lv_color_hex(0xF2C94C) : lv_color_white(), 0);
        char batt_buf[20];
        snprintf(batt_buf, sizeof(batt_buf), "%s%s", charging ? LV_SYMBOL_CHARGE : "", battery_icon(batt));
        lv_label_set_text(statusBattery, batt_buf);

        // statusNotif left as operator placeholder, e.g. Free
        char app[20] = {0}; char title[36] = {0}; char body[96] = {0};
        if (notifications::center().get_latest(0, app, title, body)) {
            lv_label_set_text(statusNotif, "New" LV_SYMBOL_BELL);
        } else {
            lv_label_set_text(statusNotif, LTE::isAirplaneMode() ? "Mode Avion" : "Free");
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