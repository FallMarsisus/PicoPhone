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
    APP_SETTINGS,
    APP_CONTACTS,
    APP_TIMER, 
    APP_EXPLORER, 
    APP_OLD_HOME,
    APP_PHONE, 
    APP_SMS,
    APP_WEBRADIO, 
    APP_PYTHON_TEST,
    APP_STORE
};

class AppManager {
private:
    LockScreen lockScreen;
    ControlCenter controlCenter;

public:
    // --- GESTION DU CHANGEMENT D'APP (Statique) ---
    static volatile AppID nextAppID;
    static volatile bool switchRequested;
    static volatile bool ccOpenRequested;

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

    // --- GESTION DU SYSTEME ---
    
    void init() {
        lockScreen.init();
        controlCenter.init();
    }

    void update() {
        lockScreen.update();
        
        // Ouvrir le Control Center si demandé (et pas verrouillé)
        if (__atomic_exchange_n(&ccOpenRequested, false, __ATOMIC_ACQ_REL)) {
            if (!lockScreen.isLocked()) {
                controlCenter.toggle();
            }
        }
    }
    
    bool isLocked() const { return lockScreen.isLocked(); }
    bool isControlCenterOpen() const { return controlCenter.isOpen(); }
};

// Initialisation des variables statiques
volatile AppID AppManager::nextAppID = APP_HOME;
volatile bool AppManager::switchRequested = false;
volatile bool AppManager::ccOpenRequested = false;

#endif