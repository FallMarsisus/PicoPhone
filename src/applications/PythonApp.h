#ifndef PYTHONAPP_H
#define PYTHONAPP_H

#include "../App.h"
#include "../AppManager.h"
#include <lvgl.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <hardware/watchdog.h>
#include <ArduinoJson.h>
extern "C" {
    #include "../plugins/pika/pikaScript.h"
    #include "../plugins/pika/PikaObj.h"
    #include "../plugins/pika/PikaCompiler.h"
    #include "../plugins/pika/PikaVM.h"
    #include "../plugins/pika/pika_lvgl.h"
}

// ═══════════════════════════════════════════════════════════════════════════
//  Capture de la sortie PikaPython (override de la fonction PIKA_WEAK)
// ═══════════════════════════════════════════════════════════════════════════

static String _pika_output_buf;
static bool   _pika_capture_active = false;
static constexpr size_t PIKA_OUTPUT_MAX = 4096;

extern "C" void pika_platform_printf(char* fmt, ...) {
    char tmp[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    // Toujours vers Serial
    Serial.print(tmp);
    // Capturer si actif
    if (_pika_capture_active && _pika_output_buf.length() < PIKA_OUTPUT_MAX) {
        _pika_output_buf += tmp;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Implémentation des fonctions de fichier pour PikaPython avec LittleFS
// ═══════════════════════════════════════════════════════════════════════════

extern "C" FILE* pika_platform_fopen(const char* filename, const char* modes) {
    // Convertir les modes stdio vers les modes LittleFS
    const char* fsMode = "r"; // Par défaut lecture
    
    if (strchr(modes, 'w')) {
        fsMode = "w"; // Écriture (crée ou tronque)
    } else if (strchr(modes, 'a')) {
        fsMode = "a"; // Ajout
    } else if (strchr(modes, 'r') && strchr(modes, '+')) {
        fsMode = "r+"; // Lecture/écriture
    }
    
    Serial.printf("[pika_fopen] Opening '%s' with mode '%s' (from '%s')\n", filename, fsMode, modes);
    
    File* f = new File(LittleFS.open(filename, fsMode));
    if (!f || !(*f)) {
        Serial.printf("[pika_fopen] FAILED to open '%s'\n", filename);
        delete f;
        return nullptr;
    }
    
    Serial.printf("[pika_fopen] SUCCESS - file handle: %p\n", (void*)f);
    return (FILE*)f;
}

extern "C" int pika_platform_fclose(FILE* stream) {
    if (!stream) return -1;
    File* f = (File*)stream;
    Serial.printf("[pika_fclose] Closing file handle: %p\n", (void*)f);
    f->close();
    delete f;
    return 0;
}

extern "C" size_t pika_platform_fwrite(const void* ptr, size_t size, size_t n, FILE* stream) {
    if (!stream) return 0;
    File* f = (File*)stream;
    size_t total = size * n;
    size_t written = f->write((const uint8_t*)ptr, total);
    // Serial.printf("[pika_fwrite] Wrote %d/%d bytes\n", written, total); // Trop verbeux
    return written;
}

extern "C" size_t pika_platform_fread(void* ptr, size_t size, size_t n, FILE* stream) {
    if (!stream) return 0;
    File* f = (File*)stream;
    size_t total = size * n;
    size_t bytes_read = f->read((uint8_t*)ptr, total);
    // Serial.printf("[pika_fread] Read %d/%d bytes\n", bytes_read, total); // Trop verbeux
    return bytes_read;
}

extern "C" int pika_platform_fseek(FILE* stream, long offset, int whence) {
    if (!stream) return -1;
    File* f = (File*)stream;
    SeekMode mode = SeekSet;
    if (whence == SEEK_CUR) mode = SeekCur;
    else if (whence == SEEK_END) mode = SeekEnd;
    return f->seek(offset, mode) ? 0 : -1;
}

extern "C" long pika_platform_ftell(FILE* stream) {
    if (!stream) return -1;
    File* f = (File*)stream;
    return f->position();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Fonctions bridge C appelées depuis pika_lvgl.c
// ═══════════════════════════════════════════════════════════════════════════

static String _pika_http_response_buf;

extern "C" void pika_app_go_home(void) {
    AppManager::switchTo(APP_HOME);
}

extern "C" const char* pika_app_http_get(const char* url) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] Pas de WiFi");
        return NULL;
    }
    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (strncmp(url, "https", 5) == 0) {
        secureClient.setInsecure();
        http.begin(secureClient, url);
    } else {
        http.begin(plainClient, url);
    }
    http.setTimeout(10000);
    watchdog_update();
    int code = http.GET();
    watchdog_update();
    if (code <= 0) {
        Serial.printf("[HTTP GET] Echec code=%d\n", code);
        http.end();
        return NULL;
    }
    _pika_http_response_buf = http.getString();
    http.end();
    if (_pika_http_response_buf.length() > 16384) {
        _pika_http_response_buf = _pika_http_response_buf.substring(0, 16384);
    }
    Serial.printf("[HTTP GET] OK %d, %d bytes\n", code, _pika_http_response_buf.length());
    return _pika_http_response_buf.c_str();
}

extern "C" const char* pika_app_http_post(const char* url, const char* body, const char* content_type) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] Pas de WiFi");
        return NULL;
    }
    HTTPClient http;
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    if (strncmp(url, "https", 5) == 0) {
        secureClient.setInsecure();
        http.begin(secureClient, url);
    } else {
        http.begin(plainClient, url);
    }
    http.setTimeout(10000);
    http.addHeader("Content-Type", content_type);
    watchdog_update();
    int code = http.POST((uint8_t*)body, strlen(body));
    watchdog_update();
    if (code <= 0) {
        Serial.printf("[HTTP POST] Echec code=%d\n", code);
        http.end();
        return NULL;
    }
    _pika_http_response_buf = http.getString();
    http.end();
    if (_pika_http_response_buf.length() > 16384) {
        _pika_http_response_buf = _pika_http_response_buf.substring(0, 16384);
    }
    Serial.printf("[HTTP POST] OK %d, %d bytes\n", code, _pika_http_response_buf.length());
    return _pika_http_response_buf.c_str();
}

// ═══════════════════════════════════════════════════════════════════════════
//  PythonApp
// ═══════════════════════════════════════════════════════════════════════════

class PythonApp : public App {
private:
    static PikaObj* globalPikaEnv;
    static String queuedScriptPath;
    static String lastScriptPath;  // Garde le chemin du dernier script chargé
    String scriptSource;
    String appName;
    String appPath;
    bool splash_done;
    bool runtime_cleaned;
    lv_timer_t* start_script_timer;

    void cleanupPythonRuntime(const char* reason) {
        if (runtime_cleaned) {
            return;
        }

        Serial.printf("[PythonApp] cleanup runtime (%s)\n", reason ? reason : "?");

        _pika_capture_active = false;
        _pika_output_buf = "";

        if (start_script_timer) {
            lv_timer_del(start_script_timer);
            start_script_timer = nullptr;
        }

        if (globalPikaEnv) {
            // Drop pika_lvgl listeners first so stale LVGL timers self-delete safely.
            pika_lvgl_deinit(globalPikaEnv);
            obj_deinit(globalPikaEnv);
            globalPikaEnv = nullptr;
        }

        runtime_cleaned = true;
    }

    static bool readScriptFromFs(const String& path, String& outScript) {
        File f = LittleFS.open(path, "r");
        if (!f || f.isDirectory()) return false;
        
        outScript = f.readString();
        f.close();
        
        if (outScript.length() > 24576) {
            outScript = outScript.substring(0, 24576);
            Serial.printf("[PythonApp] Script truncated to 24KB: %s\n", path.c_str());
        }
        
        return outScript.length() > 0;
    }

    // Lit le manifest.json pour obtenir le nom de l'appli
    void readManifest(const String& scriptPath) {
        // Extraire le dossier de l'appli depuis le chemin du script
        // Ex: /apps/wordle/main.py -> /apps/wordle
        int lastSlash = scriptPath.lastIndexOf('/');
        if (lastSlash == -1) {
            appName = "Python App";
            appPath = "";
            return;
        }
        appPath = scriptPath.substring(0, lastSlash);
        String manifestPath = appPath + "/manifest.json";
        
        Serial.printf("[PythonApp] Reading manifest: %s\n", manifestPath.c_str());
        
        File f = LittleFS.open(manifestPath, "r");
        if (!f) {
            Serial.println("[PythonApp] No manifest found");
            appName = "Python App";
            return;
        }
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, f);
        f.close();
        
        if (error) {
            Serial.printf("[PythonApp] Manifest parse error: %s\n", error.c_str());
            appName = "Python App";
            return;
        }
        
        appName = doc["name"].as<String>();
        if (appName.length() == 0) {
            appName = "Python App";
        }
        Serial.printf("[PythonApp] App name: %s\n", appName.c_str());
    }

    // Compile le script Python vers un fichier bytecode .pyo pour accélérer les exécutions suivantes
    bool compileScriptToBytecode(const String& pyPath, const String& pyoPath) {
        Serial.printf("[PythonApp] Compiling %s -> %s\n", pyPath.c_str(), pyoPath.c_str());
        
        // Vérifier que le dossier parent existe
        int lastSlash = pyoPath.lastIndexOf('/');
        if (lastSlash > 0) {
            String parentDir = pyoPath.substring(0, lastSlash);
            if (!LittleFS.exists(parentDir)) {
                Serial.printf("[PythonApp] Creating directory: %s\n", parentDir.c_str());
                if (!LittleFS.mkdir(parentDir)) {
                    Serial.printf("[PythonApp] Failed to create directory!\n");
                    return false;
                }
            }
        }
        
        unsigned long compile_start = millis();
        
        watchdog_update();
        PIKA_RES res = pikaCompile((char*)pyoPath.c_str(), (char*)scriptSource.c_str());
        watchdog_update();
        
        unsigned long compile_time = millis() - compile_start;
        
        if (res == PIKA_RES_OK) {
            Serial.printf("[PythonApp] Compilation successful in %lu ms\n", compile_time);
            // Vérifier que le fichier a bien été créé
            if (LittleFS.exists(pyoPath)) {
                File check = LittleFS.open(pyoPath, "r");
                if (check) {
                    size_t fileSize = check.size();
                    check.close();
                    Serial.printf("[PythonApp] Bytecode file created: %d bytes\n", fileSize);
                    return true;
                }
            }
            Serial.printf("[PythonApp] Warning: Compilation reported success but file not found!\n");
            return false;
        } else {
            Serial.printf("[PythonApp] Compilation failed with code %d\n", res);
            return false;
        }
    }

    // Tente de charger et exécuter un fichier bytecode .pyo
    bool tryRunBytecode(const String& pyoPath) {
        if (!LittleFS.exists(pyoPath)) {
            Serial.printf("[PythonApp] Bytecode cache not found: %s\n", pyoPath.c_str());
            return false;
        }
        
        Serial.printf("[PythonApp] Running bytecode from %s\n", pyoPath.c_str());
        unsigned long exec_start = millis();
        
        watchdog_update();
        VMParameters* result = pikaVM_runByteCodeFile(globalPikaEnv, (char*)pyoPath.c_str());
        watchdog_update();
        
        unsigned long exec_time = millis() - exec_start;
        Serial.printf("[PythonApp] Bytecode execution took %lu ms\n", exec_time);
        
        return (result != nullptr);
    }

    // Vérifie si le bytecode est à jour par rapport au fichier source
    bool isBytecodeUpToDate(const String& pyPath, const String& pyoPath) {
        if (!LittleFS.exists(pyoPath)) {
            return false;
        }
        
        // Comparer les dates de modification
        File pyFile = LittleFS.open(pyPath, "r");
        File pyoFile = LittleFS.open(pyoPath, "r");
        
        if (!pyFile || !pyoFile) {
            if (pyFile) pyFile.close();
            if (pyoFile) pyoFile.close();
            return false;
        }
        
        time_t pyTime = pyFile.getLastWrite();
        time_t pyoTime = pyoFile.getLastWrite();
        
        pyFile.close();
        pyoFile.close();
        
        // Si le source est plus récent que le bytecode, invalider le cache
        if (pyTime > pyoTime) {
            Serial.printf("[PythonApp] Source file is newer than bytecode, recompiling\n");
            LittleFS.remove(pyoPath); // Supprimer l'ancien bytecode
            return false;
        }
        
        Serial.printf("[PythonApp] Bytecode is up to date\n");
        return true;
    }

    // Affiche un splash screen de loading
    void showSplashScreen() {
        lv_obj_t* scr = lv_scr_act();
        
        // Fond dégradé bleu
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1A3E), LV_PART_MAIN);
        
        // Icône au centre (symbole fichier/script)
        lv_obj_t* iconLbl = lv_label_create(scr);
        lv_label_set_text(iconLbl, LV_SYMBOL_FILE);
        lv_obj_set_style_text_color(iconLbl, lv_color_hex(0x00D9FF), LV_PART_MAIN);
        lv_obj_set_style_text_font(iconLbl, &lv_font_montserrat_28, LV_PART_MAIN);
        lv_obj_align(iconLbl, LV_ALIGN_CENTER, 0, -40);
        
        // Nom de l'appli
        lv_obj_t* nameLbl = lv_label_create(scr);
        lv_label_set_text(nameLbl, appName.c_str());
        lv_obj_set_style_text_color(nameLbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_align(nameLbl, LV_ALIGN_CENTER, 0, 30);
        
        // Texte "Chargement..."
        lv_obj_t* loadingLbl = lv_label_create(scr);
        lv_label_set_text(loadingLbl, "Chargement...");
        lv_obj_set_style_text_color(loadingLbl, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(loadingLbl, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_align(loadingLbl, LV_ALIGN_CENTER, 0, 65);
        
        Serial.println("[PythonApp] Splash screen displayed");
    }

    // Lance l'exécution du script Python (appelé par le timer)
    void startPythonExecution() {
        Serial.println("[PythonApp] Starting Python execution");
        unsigned long start_time = millis();
        
        // Nettoyer le splash screen
        lv_obj_clean(lv_scr_act());
        
        // Initialise l'environnement PikaPython une seule fois
        if (globalPikaEnv == nullptr) {
            Serial.println("[PythonApp] Initializing PikaPython environment...");
            watchdog_update();
            globalPikaEnv = pikaPythonInit();
            watchdog_update();
            Serial.printf("[PythonApp] PikaPython init took %lu ms\n", millis() - start_time);
        }

        if (scriptSource.length() == 0) return;

        // Activer la capture de sortie
        _pika_output_buf = "";
        _pika_capture_active = true;   
        obj_setErrorCode(globalPikaEnv, 0);

        // Optimisation: utiliser un fichier bytecode .pyo compilé si disponible
        bool executedFromBytecode = false;
        
        if (appPath.length() > 0) {
            String pyPath = appPath + "/main.py";
            String pyoPath = appPath + "/main.pyo";
            
            // Vérifier si le bytecode existe et est à jour
            if (isBytecodeUpToDate(pyPath, pyoPath)) {
                // Essayer de charger le bytecode existant
                if (tryRunBytecode(pyoPath)) {
                    executedFromBytecode = true;
                    Serial.println("[PythonApp] Executed from cached bytecode");
                }
            }
            
            // Si pas de bytecode à jour, compiler
            if (!executedFromBytecode) {
                Serial.println("[PythonApp] Compiling to bytecode...");
                if (compileScriptToBytecode(pyPath, pyoPath)) {
                    // Compiler a réussi, exécuter le bytecode fraîchement créé
                    if (tryRunBytecode(pyoPath)) {
                        executedFromBytecode = true;
                        Serial.println("[PythonApp] Executed from newly compiled bytecode");
                    }
                }
            }
        }
        
        // Fallback: si le bytecode n'a pas fonctionné, utiliser l'ancienne méthode
        if (!executedFromBytecode) {
            Serial.printf("[PythonApp] Running script from source (%d bytes)...\n", scriptSource.length());
            unsigned long exec_start = millis();
            watchdog_update();
            VMParameters* result = obj_run(globalPikaEnv, (char*)scriptSource.c_str());
            (void)result;
            watchdog_update();
            unsigned long exec_time = millis() - exec_start;
            Serial.printf("[PythonApp] Script execution took %lu ms\n", exec_time);
        }

        _pika_capture_active = false;

        // Vérifier les erreurs
        int errCode = obj_getErrorCode(globalPikaEnv);
        if (errCode != 0) {
            Serial.printf("[PythonApp] Erreur PikaPython code=%d\n", errCode);
            // Nettoyer l'écran (les widgets créés partiellement)
            lv_obj_clean(lv_scr_act());
            showErrorScreen(lv_scr_act(), errCode, _pika_output_buf);
        }
        _pika_output_buf = "";
        splash_done = true;
        
        Serial.printf("[PythonApp] Total startup time: %lu ms\n", millis() - start_time);
    }

    // Affiche un écran d'erreur LVGL avec les détails
    void showErrorScreen(lv_obj_t* parent, int errCode, const String& output) {
        lv_obj_t* scr = lv_scr_act();

        // Fond rouge foncé
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x330000), LV_PART_MAIN);

        // Titre
        lv_obj_t* titleLbl = lv_label_create(scr);
        lv_label_set_text(titleLbl, LV_SYMBOL_WARNING " Erreur Python");
        lv_obj_set_style_text_color(titleLbl, lv_color_hex(0xFF4444), LV_PART_MAIN);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_align(titleLbl, LV_ALIGN_TOP_MID, 0, 8);

        // Code erreur
        char codeBuf[64];
        snprintf(codeBuf, sizeof(codeBuf), "Code erreur: %d", errCode);
        lv_obj_t* codeLbl = lv_label_create(scr);
        lv_label_set_text(codeLbl, codeBuf);
        lv_obj_set_style_text_color(codeLbl, lv_color_hex(0xFFAAAA), LV_PART_MAIN);
        lv_obj_align(codeLbl, LV_ALIGN_TOP_MID, 0, 34);

        // Sortie PikaPython (erreurs, traces, etc.)
        lv_obj_t* container = lv_obj_create(scr);
        lv_obj_set_size(container, 300, 310);
        lv_obj_align(container, LV_ALIGN_TOP_MID, 0, 58);
        lv_obj_set_style_bg_color(container, lv_color_hex(0x1A0000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(container, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(container, lv_color_hex(0x662222), LV_PART_MAIN);
        lv_obj_set_style_border_width(container, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(container, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_all(container, 8, LV_PART_MAIN);

        lv_obj_t* outLbl = lv_label_create(container);
        lv_label_set_long_mode(outLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(outLbl, 280);
        lv_obj_set_style_text_color(outLbl, lv_color_hex(0xFFCCCC), LV_PART_MAIN);
        lv_obj_set_style_text_font(outLbl, &lv_font_montserrat_12, LV_PART_MAIN);
        if (output.length() > 0) {
            // Tronquer si trop long pour l'affichage
            if (output.length() > 800) {
                String truncated = output.substring(output.length() - 800);
                lv_label_set_text(outLbl, truncated.c_str());
            } else {
                lv_label_set_text(outLbl, output.c_str());
            }
        } else {
            lv_label_set_text(outLbl, "(aucune sortie capturee)");
        }

        // Bouton retour
        lv_obj_t* btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 200, 44);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x662222), LV_PART_MAIN);
        lv_obj_t* btnLbl = lv_label_create(btn);
        lv_label_set_text(btnLbl, "Retour Home");
        lv_obj_center(btnLbl);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            AppManager::switchTo(APP_HOME);
        }, LV_EVENT_CLICKED, nullptr);
    }

public:
    PythonApp(String script)
        : scriptSource(script),
          splash_done(false),
          runtime_cleaned(false),
          start_script_timer(nullptr) {
        appName = "Python App";
        appPath = "";
    }

    ~PythonApp() {
        cleanupPythonRuntime("destructor");
    }

    static void queueScriptFromFile(const String& path) {
        queuedScriptPath = path;
    }

    static bool takeQueuedScript(String& outScript) {
        if (queuedScriptPath.length() == 0) return false;
        String path = queuedScriptPath;
        lastScriptPath = path;  // Garder le chemin pour le manifest
        queuedScriptPath = "";
        return readScriptFromFs(path, outScript);
    }

    static const char* getDefaultScript() {
        return
            "import pika_lvgl as lv\n"
            "scr = lv.scr_act()\n"
            "quit_timer = 0\n"
            "\n"
            "title = lv.label(scr)\n"
            "title.set_text('PikaPython HTTP Demo')\n"
            "title.align(lv.ALIGN.TOP_MID, 0, 10)\n"
            "\n"
            "result_label = lv.label(scr)\n"
            "result_label.set_long_mode(1)\n"
            "result_label.set_width(280)\n"
            "result_label.set_text('Appuie sur Fetch pour tester HTTP')\n"
            "result_label.align(lv.ALIGN.TOP_MID, 0, 40)\n"
            "\n"
            "def on_fetch(evt):\n"
            "    global result_label\n"
            "    result_label.set_text('Chargement...')\n"
            "    r = lv.http_get('http://random-word-api.herokuapp.com/word?length=5')\n"
            "    if r:\n"
            "        result_label.set_text(r)\n"
            "    else:\n"
            "        result_label.set_text('Erreur: pas de WiFi ou echec')\n"
            "\n"
            "fetch_btn = lv.btn(scr)\n"
            "fetch_btn.set_size(200, 50)\n"
            "fetch_btn.align(lv.ALIGN.BOTTOM_MID, 0, -70)\n"
            "fetch_lbl = lv.label(fetch_btn)\n"
            "fetch_lbl.set_text('Fetch HTTP')\n"
            "fetch_lbl.center()\n"
            "fetch_btn.add_event_cb(on_fetch, lv.EVENT.CLICKED, 0)\n"
            "\n"
            "home_btn = lv.btn(scr)\n"
            "home_btn.set_size(200, 50)\n"
            "home_btn.align(lv.ALIGN.BOTTOM_MID, 0, -10)\n"
            "home_lbl = lv.label(home_btn)\n"
            "home_lbl.set_text('Retour Home')\n"
            "home_lbl.center()\n"
            "\n"
            "def do_quit(t):\n"
            "    global quit_timer\n"
            "    t._del()\n"
            "    quit_timer = 0\n"
            "    lv.go_home()\n"
            "\n"
            "def on_home(evt):\n"
            "    global quit_timer\n"
            "    quit_timer = lv.timer_create_basic()\n"
            "    quit_timer.set_period(60)\n"
            "    quit_timer.set_cb(do_quit)\n"
            "\n"
            "home_btn.add_event_cb(on_home, lv.EVENT.CLICKED, 0)\n";
    }

    void start(lv_obj_t* parent) override {
        (void)parent;
        if (scriptSource.length() == 0) return;

        runtime_cleaned = false;
        
        // Lire le manifest si on vient d'un fichier queued
        if (lastScriptPath.length() > 0) {
            readManifest(lastScriptPath);
        }
        
        // Afficher le splash screen
        showSplashScreen();
        
       // Lancer un timer pour démarrer le script Python après un délai
        // pour laisser l'UI se stabiliser
        start_script_timer = lv_timer_create([](lv_timer_t* t) {
            PythonApp* self = (PythonApp*)t->user_data;
            if (self) {
                self->startPythonExecution();
                
                // CRUCIAL : On remet le pointeur à zéro pour que preClean l'ignore !
                self->start_script_timer = nullptr; 
            }
            // ATTENTION : Surtout pas de lv_timer_del(t) ici !
            // LVGL s'en charge tout seul grâce au repeat_count = 1
        }, 500, this);
        
        lv_timer_set_repeat_count(start_script_timer, 1);
    }

    void update() override {}
    
    void preClean() override {
        Serial.println("[PythonApp] preClean");
        cleanupPythonRuntime("preClean");
        Serial.println("[PythonApp] preClean done");
    }
    
    void stop() override {
        Serial.println("[PythonApp] Stopping");
        cleanupPythonRuntime("stop");
    }
};

PikaObj* PythonApp::globalPikaEnv = nullptr;
String PythonApp::queuedScriptPath = "";
String PythonApp::lastScriptPath = "";

#endif