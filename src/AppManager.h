#ifndef APPMANAGER_H
#define APPMANAGER_H

#include "system/LockScreen.h"
#include "system/ControlCenter.h"

// La liste de toutes les applications
enum AppID {
    APP_HOME,
    APP_BOOTLOADER,
    APP_WIFI,
    APP_TOUCH_CALIB,
    APP_WEATHER,
    APP_TELEGRAM,
    APP_VELIB,
    APP_2048,
    APP_SKETCH,
    APP_CALC,
    APP_SETTINGS
};

class AppManager {
private:
    LockScreen lockScreen;
    ControlCenter controlCenter;

public:
    // --- GESTION DU CHANGEMENT D'APP (Statique) ---
    static AppID nextAppID;
    static bool switchRequested;
    static bool ccOpenRequested;

    static void switchTo(AppID id) {
        nextAppID = id;
        switchRequested = true;
    }
    
    static void openControlCenter() {
        ccOpenRequested = true;
    }

    // --- GESTION DU SYSTEME ---
    
    void init() {
        lockScreen.init();
        controlCenter.init();
    }

    void update() {
        lockScreen.update();
        
        // Ouvrir le Control Center si demandé (et pas verrouillé)
        if (ccOpenRequested) {
            ccOpenRequested = false;
            if (!lockScreen.isLocked()) {
                controlCenter.toggle();
            }
        }
    }
    
    bool isLocked() const { return lockScreen.isLocked(); }
    bool isControlCenterOpen() const { return controlCenter.isOpen(); }
};

// Initialisation des variables statiques
AppID AppManager::nextAppID = APP_HOME;
bool AppManager::switchRequested = false;
bool AppManager::ccOpenRequested = false;

#endif