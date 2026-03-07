#ifndef PYTHONAPP_H
#define PYTHONAPP_H

#include "../App.h"
#include "../AppManager.h"
#include <lvgl.h>
#include <LittleFS.h>
extern "C" {
    #include "../plugins/pika/pikaScript.h"
    #include "../plugins/pika/PikaObj.h"
}

// Fonction C implémentée côté C++ (main.cpp), appelée depuis pika_lvgl.c
extern "C" void pika_app_go_home(void);

class PythonApp : public App {
private:
    // Environnement Python global singleton (partagé entre toutes les instances)
    static PikaObj* globalPikaEnv;
    static String queuedScriptPath;
    String scriptSource;

    static bool readScriptFromFs(const String& path, String& outScript) {
        File f = LittleFS.open(path, "r");
        if (!f || f.isDirectory()) {
            return false;
        }
        constexpr size_t MAX_SCRIPT_SIZE = 24 * 1024;
        outScript = "";
        outScript.reserve(1024);
        while (f.available() && outScript.length() < MAX_SCRIPT_SIZE) {
            outScript += (char)f.read();
        }
        f.close();
        return outScript.length() > 0;
    }

public:
    PythonApp(String script) : scriptSource(script) {}

    static void queueScriptFromFile(const String& path) {
        queuedScriptPath = path;
    }

    static bool takeQueuedScript(String& outScript) {
        if (queuedScriptPath.length() == 0) {
            return false;
        }
        String path = queuedScriptPath;
        queuedScriptPath = "";
        return readScriptFromFs(path, outScript);
    }

    void start(lv_obj_t* parent) override {
        // Initialise l'environnement PikaPython une seule fois
        if (globalPikaEnv == nullptr) {
            globalPikaEnv = pikaPythonInit();
        }
        
        // Exécute le script personnalisé
        if (scriptSource.length() > 0) {
            obj_run(globalPikaEnv, (char*)scriptSource.c_str());
        }
    }

    void update() override {
        // Si besoin de logique à chaque frame
    }

    void stop() override {
        // Ne pas détruire l'environnement Python ici: des callbacks LVGL
        // peuvent toujours référencer des objets Python tant que le moteur tourne.
        // Le nettoyage d'écran est géré par loadApp().
    }
};

// Initialisation du singleton
PikaObj* PythonApp::globalPikaEnv = nullptr;
String PythonApp::queuedScriptPath = "";

#endif