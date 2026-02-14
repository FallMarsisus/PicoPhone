#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <pico/mutex.h>
#include "Hardware.h"
#include "AppManager.h"
#include "WifiStore.h"
#include "system/Settings.h"
#include "applications/HomeApp.h"
#include "applications/BootloaderApp.h"
#include "applications/WifiApp.h"
#include "applications/TouchCalibApp.h"
#include "applications/WeatherApp.h"
#include "applications/TelegramApp.h"
#include "applications/VelibApp.h"
#include "applications/Game2048App.h"
#include "applications/SketchApp.h"
#include "applications/CalculatorApp.h"
#include "applications/SettingsApp.h"

App* currentApp = nullptr;
auto_init_mutex(myMutex); 
auto_init_mutex(app_switch_mutex);
AppManager manager;

void loadApp(AppID id) {
    mutex_enter_blocking(&app_switch_mutex);
    // 1. Nettoyage
    if (currentApp != nullptr) {
        currentApp->stop();
        delete currentApp;
        currentApp = nullptr;
    }
    lv_obj_clean(lv_scr_act()); // Vide l'écran LVGL

    // 2. Création (Factory)
    switch (id) {
        case APP_HOME:
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
        default:
            currentApp = new HomeApp();
            break;
    }

    // 3. Démarrage
    if (currentApp) {
        currentApp->start(lv_scr_act());
    }
    
    // Reset du flag
    AppManager::switchRequested = false;

    mutex_exit(&app_switch_mutex);
}

void setup() {
    Serial.begin(115200);
    Serial.println("[BOOT] setup start");
    mutex_enter_blocking(&myMutex);
    hardware_init();
    Serial.println("[BOOT] hardware_init ok");

    // i2s_play_test_tone(440, 200); 
    
    // Ecran de chargement
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("Booting OS...", 160, 240, 2);

    // WiFi (auto-connect en arrière-plan)
    wifi_store::autoconnect_init();
    Serial.println("[BOOT] wifi init ok");
    
    // Charger les paramètres et appliquer la luminosité
    settings::applyBrightness();
    Serial.println("[BOOT] settings ok");
    
    manager.init();
    Serial.println("[BOOT] manager init ok");
    loadApp(APP_HOME);
    Serial.println("[BOOT] home loaded");
    mutex_exit(&myMutex);
    Serial.println("[BOOT] setup done");

}

void loop() {
    lv_timer_handler();

    // Auto-connexion WiFi (non bloquant, respecte les paramètres)
    if (settings::isWifiEnabled()) {
        wifi_store::autoconnect_tick();
    }

    manager.update();
    
    if (AppManager::switchRequested) {
        loadApp(AppManager::nextAppID);
    }

    // Update de l'app courante
    if (currentApp) {
        currentApp->update();
    }

    // 5ms limite rapidement le FPS perçu; 1-2ms garde une UI plus fluide.
    delay(1);
}

void setup1() {
  mutex_enter_blocking(&myMutex);
  mutex_exit(&myMutex);
}

void loop1() {
    if (AppManager::switchRequested) return;
  if(mutex_try_enter(&app_switch_mutex, nullptr) == true) {
    if (currentApp) currentApp->update1();
    mutex_exit(&app_switch_mutex);
  }
}