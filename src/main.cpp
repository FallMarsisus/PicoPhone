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
#include "applications/HomeApp.h"
#include "applications/BootloaderApp.h"
#include "applications/WifiApp.h"
#include "applications/TouchCalibApp.h"
#include "applications/WeatherApp.h"
#include "applications/TelegramApp.h"
#include "applications/ContactsApp.h"
#include "applications/TimerApp.h"
#include "applications/VelibApp.h"
#include "applications/PhoneApp.h"
#include "applications/SmsApp.h"
#include "applications/NewHomeApp.h"
#include "applications/WebRadioApp.h"
#include "applications/Game2048App.h"
#include "applications/SketchApp.h"
#include "applications/CalculatorApp.h"
#include "applications/SettingsApp.h"
#include "applications/FileExplorerApp.h"
#include "services/TelegramNotifyService.h"
#include "services/TimerService.h"
#include "services/SmsNotifyService.h"


App* currentApp = nullptr;
auto_init_mutex(myMutex); 
auto_init_mutex(app_switch_mutex);
AppManager manager;
static volatile bool g_system_ready = false;

// --- ANTI-FREEZE ---
static volatile uint32_t core0_heartbeat = 0;
static volatile uint32_t core1_heartbeat = 0;
static uint32_t last_mem_check = 0;

// Watchdog : nourrir régulièrement sinon reboot auto
static inline void feed_watchdog() {
    watchdog_update();
}




void loadApp(AppID id) {
    // 1. On essaie de prendre le mutex sans bloquer le Watchdog
    // Si Core 1 bloque, on nourrit le chien en attendant
    while (!mutex_try_enter(&app_switch_mutex, nullptr)) {
        feed_watchdog();
        delay(10);
    }

    // --- ZONE CRITIQUE ---

    lv_obj_t* scr = lv_scr_act();

    // 2. CORRECTION MAJEURE : On nettoie LVGL *AVANT* de tuer l'App
    // Ainsi, si un widget envoie un événement lors de sa destruction, l'App est encore là.
    lv_obj_clean(scr); 

    // 3. Maintenant que l'écran est vide, on peut tuer l'App C++ en sécurité
    if (currentApp != nullptr) {
        currentApp->stop();
        delete currentApp;
        currentApp = nullptr;
    }

    // 4. On force un petit nettoyage mémoire LVGL (optionnel mais sain)
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    // 2. Création (Factory)
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
        default:
            currentApp = new NewHomeApp();
            break;
    }

    // 3. Démarrage
    if (currentApp) {
        currentApp->start(lv_scr_act());
    }


    // Reset du flag
    mutex_exit(&app_switch_mutex);
}

void setup() {
    Serial.begin(115200);
    Serial.println("[BOOT] setup start");
    mutex_enter_blocking(&myMutex);
    hardware_init();
    Serial.println("[BOOT] hardware_init ok");


    
    // WiFi (auto-connect en arrière-plan)
    wifi_store::autoconnect_init();
    Serial.println("[BOOT] wifi init ok");
    
    // Charger les paramètres et appliquer la luminosité
    settings::applyBrightness();
    Serial.println("[BOOT] settings ok");
    
    manager.init();
    Serial.println("[BOOT] manager init ok");

    background_services::manager().registerService(&telegram_service::instance());
    background_services::manager().registerService(&timer_service::instance());
    background_services::manager().registerService(&sms_service::instance());
    background_services::manager().begin();

    loadApp(APP_HOME);
    Serial.println("[BOOT] home loaded");
    

    // Watchdog matériel RP2040 : reboot si pas nourri pendant 8.3s
    watchdog_enable(8300, true);
    Serial.println("[BOOT] watchdog enabled (8.3s)");

    __atomic_store_n(&g_system_ready, true, __ATOMIC_RELEASE);
    mutex_exit(&myMutex);
    Serial.println("[BOOT] setup done");
}

void loop() {
    if (!__atomic_load_n(&g_system_ready, __ATOMIC_ACQUIRE)) {
        delay(1);
        return;
    }

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
    delay(1);
}

void setup1() {
    while (!__atomic_load_n(&g_system_ready, __ATOMIC_ACQUIRE)) {
        delay(1);
    }
}

void loop1() {
    if (!__atomic_load_n(&g_system_ready, __ATOMIC_ACQUIRE)) return;

    core1_heartbeat = millis();

    if (settings::isWifiEnabled()) {
        wifi_store::autoconnect_tick();
    }

    background_services::manager().update1();

    if (mutex_try_enter(&app_switch_mutex, nullptr) == true) {
        if (currentApp) currentApp->update1();
        mutex_exit(&app_switch_mutex);
    }

    yield();
    delay(5);
}