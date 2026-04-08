#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <pico/mutex.h>
#include <hardware/watchdog.h>
#include "Hardware.h"
#include "AppManager.h"
#include "WifiStore.h"
#include "system/Settings.h"
#include "system/NotificationCenter.h"
#include "system/LockScreen.h"
#include "system/BackgroundServices.h"
#include "services/CastService.h"
#include "applications/HomeApp.h"
#include "applications/BootloaderApp.h"
#include "applications/WifiApp.h"
#include "applications/WalletApp.h"
#include "applications/TouchCalibApp.h"
#include "applications/WeatherApp.h"
#include "applications/TelegramApp.h"
#include "applications/ContactsApp.h"
#include "applications/TimerApp.h"
#include "applications/VelibApp.h"
#include "applications/PhoneApp.h"
#include "applications/SmsApp.h"
#include "applications/WebRadioApp.h"
#include "applications/PythonApp.h" 
#include "applications/Game2048App.h"
#include "applications/SketchApp.h"
#include "applications/CalculatorApp.h"
#include "applications/SettingsApp.h"
#include "applications/FileExplorerApp.h"
#include "applications/AppStoreApp.h"
#include "applications/CryptoApp.h"
#include "applications/NewsApp.h"
#include "applications/NewHomeApp.h"
#include "applications/AirQualityApp.h"
#include "applications/BambuApp.h"
#include "applications/ChatbotApp.h"
#include "services/TelegramNotifyService.h"
#include "services/TimerService.h"
#include "services/SmsNotifyService.h"
#include "system/LTE.h"

// ═══════════════════════════════════════════════════════════════════════════
//  Notification des erreurs réseau LTE
// ═══════════════════════════════════════════════════════════════════════════
void lte_notify_error(const char* title, const char* body) {
    notifications::push(title, "Erreur réseau", body);
}


App* currentApp = nullptr;
auto_init_mutex(myMutex); 
auto_init_mutex(app_switch_mutex);
AppManager manager;
static volatile bool g_system_ready = false;
static volatile bool g_lte_init_pending = true;
static volatile bool g_services_begin_pending = true;
static constexpr bool kDisableLteTemporarily = true;

// --- ANTI-FREEZE ---
static volatile uint32_t core0_heartbeat = 0;
static volatile uint32_t core1_heartbeat = 0;
static uint32_t last_mem_check = 0;

// Watchdog : nourrir régulièrement sinon reboot auto
static inline void feed_watchdog() {
    watchdog_update();
}

// Callback appelé automatiquement par LVGL quand l'animation est finie
static void on_screen_unloaded_cb(lv_event_t * e) {
    App* old_app = (App*)lv_event_get_user_data(e);
    if (old_app) {
        old_app->stop();
        delete old_app;
    }
}


void loadApp(AppID id) {
    AppManager::setCurrentApp(id);

    while (!mutex_try_enter(&app_switch_mutex, nullptr)) {
        feed_watchdog();
        delay(10);
    }

    // --- 1. Récupération de l'écran et de l'app actuels ---
    lv_obj_t* old_scr = lv_scr_act();
    App* old_app = currentApp;

    // Si une app est déjà ouverte, on prépare sa suppression après l'animation
    if (old_scr != nullptr && old_app != nullptr) {
        old_app->preClean();
        // On attache le callback pour supprimer l'objet C++ (delete) une fois l'écran débarrassé
        lv_obj_add_event_cb(old_scr, on_screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, old_app);
    }

    // --- 2. Création du nouvel écran indépendant ---
    lv_obj_t* new_scr = lv_obj_create(NULL);
    switch (id) {
        case APP_OLD_HOME:
            currentApp = new HomeApp();
            break;
        case APP_BOOTLOADER:
            currentApp = new BootloaderApp();
            break;
        case APP_WIFI:
            currentApp = new WifiApp();
            break;
        case APP_WALLET:
            currentApp = new WalletApp();
            break;
        case APP_TOUCH_CALIB:
            currentApp = new TouchCalibApp();
            break;
        case APP_WEATHER:
            currentApp = new WeatherApp();
            break;
        case APP_TELEGRAM:
            currentApp = new TelegramApp();
            break;
        case APP_VELIB:
            currentApp = new VelibApp();
            break;
        case APP_2048:
            currentApp = new Game2048App();
            break;
        case APP_SKETCH:
            currentApp = new SketchApp();
            break;
        case APP_CALC:
            currentApp = new CalculatorApp();
            break;
        case APP_SETTINGS:
            currentApp = new SettingsApp();
            break;
        case APP_CONTACTS:
            currentApp = new ContactsApp();
            break;
        case APP_TIMER:
            currentApp = new TimerApp();
            break;
        case APP_EXPLORER:
            currentApp = new FileExplorerApp();
            break;
        case APP_HOME:
            currentApp = new NewHomeApp();
            break;
        case APP_PHONE:
            currentApp = new PhoneApp();
            break;
        case APP_SMS:
            currentApp = new SmsApp();
            break;
        case APP_WEBRADIO: 
            currentApp = new WebRadioApp();
            break;
        case APP_PYTHON_TEST: {
            String pyCode;
            if (!PythonApp::takeQueuedScript(pyCode)) {
                pyCode = PythonApp::getDefaultScript();
            }
            currentApp = new PythonApp(pyCode);
            break;
        }
        case APP_STORE:
            currentApp = new AppStoreApp();
            break;
        case APP_CRYPTO:
            currentApp = new CryptoApp();
            break;
        case APP_NEWS:
            currentApp = new NewsApp();
            break;
        case APP_AIR_QUALITY:
            currentApp = new AirQualityApp();
            break;
        case APP_BAMBU:
            currentApp = new BambuApp();
            break;
        case APP_CHATBOT:
            currentApp = new ChatbotApp();
            break;
        default:
            currentApp = new NewHomeApp();
            break;
    }

    // --- 4. Démarrage de la nouvelle App sur le NOUVEL écran ---
    if (currentApp) {
        currentApp->start(new_scr);
    }

    if (id != APP_HOME && id != APP_OLD_HOME) {
        lv_obj_set_style_translate_y(new_scr, 18, 0);
    } else {
        lv_obj_set_style_translate_y(new_scr, 0, 0);
    }

    static auto enable_gesture_bubble_recursive = [](lv_obj_t* obj, const auto& self_ref) -> void {
        if (!obj) return;
        lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
        const uint32_t child_count = lv_obj_get_child_cnt(obj);
        for (uint32_t i = 0; i < child_count; ++i) {
            self_ref(lv_obj_get_child(obj, i), self_ref);
        }
    };

    static auto global_gesture_cb = [](lv_event_t* e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_BOTTOM) {
            AppManager::openControlCenter();
        } else if (dir == LV_DIR_TOP && manager.isControlCenterOpen()) {
            manager.controlCenter.close();
        }
    };

    enable_gesture_bubble_recursive(new_scr, enable_gesture_bubble_recursive);
    lv_obj_add_event_cb(new_scr, global_gesture_cb, LV_EVENT_GESTURE, nullptr);

    // --- 5. Lancement de l'animation ---
    if (old_app == nullptr) {
        // Premier écran réel: charger sans animation pour éviter de rester
        // bloqué sur l'écran noir par défaut si le tick LVGL n'est pas prêt.
        lv_scr_load(new_scr);
    } else {
        // Transition animée (ex: Slide depuis la droite, 300ms, délai 0, true = effacer old_scr)
        lv_scr_load_anim(new_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 350, 0, true);
    }

    mutex_exit(&app_switch_mutex);
}

void setup() {
    Serial.begin(115200);
    uint32_t serial_wait_start = millis();
    while (!Serial && (millis() - serial_wait_start) < 2000) {
        delay(10);
    }
    Serial.println("[BOOT] setup start");
    mutex_enter_blocking(&myMutex);
    hardware_init();
    boot_stage("hardware_init done", TFT_GREEN);
    Serial.println("[BOOT] hardware_init ok");


    
    // WiFi (auto-connect en arrière-plan)
    wifi_store::autoconnect_init();
    boot_stage("wifi init done");
    Serial.println("[BOOT] wifi init ok");
    
    // Charger les paramètres et appliquer la luminosité
    settings::applyBrightness();
    boot_stage("settings brightness done");
    Serial.println("[BOOT] settings ok");
    
    manager.init();
    boot_stage("manager init done");
    Serial.println("[BOOT] manager init ok");

    // LTE init est deplace sur le core1 pour eviter de bloquer l'UI au boot.
    Serial.println("[BOOT] LTE init deferred to core1");
    if (kDisableLteTemporarily) {
        __atomic_store_n(&g_lte_init_pending, false, __ATOMIC_RELEASE);
        boot_stage("lte init temp disabled", TFT_YELLOW);
    }

    background_services::manager().registerService(&telegram_service::instance());
    background_services::manager().registerService(&timer_service::instance());
    background_services::manager().registerService(&sms_service::instance());
    background_services::manager().registerService(&cast_service::instance());

    loadApp(APP_HOME);
    boot_stage("home loaded", TFT_GREEN);
    Serial.println("[BOOT] home loaded");
    

    // Watchdog matériel RP2040 : reboot si pas nourri pendant 8.3s
    watchdog_enable(8300, true);
    boot_stage("watchdog enabled");
    Serial.println("[BOOT] watchdog enabled (8.3s)");

    __atomic_store_n(&g_system_ready, true, __ATOMIC_RELEASE);
    mutex_exit(&myMutex);
    boot_stage("setup done", TFT_GREEN);
    Serial.println("[BOOT] setup done");
}

void loop() {
    check_sleep_button();
    
    if (!__atomic_load_n(&g_system_ready, __ATOMIC_ACQUIRE)) {
        delay(1);
        return;
    }

    // LVGL et ses drivers doivent etre initialises sur le meme coeur que lv_timer_handler.
    hardware_deferred_init();

    // --- ANTI-FREEZE : nourrir le watchdog à chaque tour ---
    feed_watchdog();
    core0_heartbeat = millis();

    lv_timer_handler();
    yield();

    // Auto-connexion WiFi (non bloquant, respecte les paramètres)
    

    feed_watchdog(); // Nourrir aussi après WiFi (peut être lent)

    manager.update();
    background_services::manager().update();
    notifications::center().update();
    
    AppID requestedApp;
    if (AppManager::consumeSwitchRequest(requestedApp)) {
        loadApp(requestedApp);
    }

    // Update de l'app courante
    if (currentApp) {
        currentApp->update();
    }

    yield();
}

void setup1() {
    while (!__atomic_load_n(&g_system_ready, __ATOMIC_ACQUIRE)) {
        delay(1);
    }
}

// Ajoute cette ligne juste avant loop1() pour qu'il connaisse la variable
extern volatile bool system_is_shutting_down;

void loop1() {
    if (!__atomic_load_n(&g_system_ready, __ATOMIC_ACQUIRE)) return;

    if (!kDisableLteTemporarily && __atomic_load_n(&g_lte_init_pending, __ATOMIC_ACQUIRE)) {
        LTE::init();
        __atomic_store_n(&g_lte_init_pending, false, __ATOMIC_RELEASE);
    }

    if (__atomic_load_n(&g_services_begin_pending, __ATOMIC_ACQUIRE)) {
        background_services::manager().begin();
        __atomic_store_n(&g_services_begin_pending, false, __ATOMIC_RELEASE);
    }

    // --- LE CORE 1 SE FIGE ICI EN CAS D'EXTINCTION ---
    if (system_is_shutting_down) {
        while (true) {
            watchdog_update(); // Garde le système en vie
            __wfi();           // Endort le Core 1 indéfiniment
        }
    }
    // -------------------------------------------------

    watchdog_update();

    // LTE est le SEUL gestionnaire de Serial1...
    if (!kDisableLteTemporarily) {
        LTE::update();
    }

    core1_heartbeat = millis();

    if (settings::isWifiEnabled()) {
        wifi_store::autoconnect_tick();
    }

    background_services::manager().update1();

    if (mutex_try_enter(&app_switch_mutex, nullptr) == true) {
        if (currentApp) currentApp->update1();
        mutex_exit(&app_switch_mutex);
    }

    watchdog_update();

    yield();
}