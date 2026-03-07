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
extern "C" {
    #include "../plugins/pika/pikaScript.h"
    #include "../plugins/pika/PikaObj.h"
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
    String scriptSource;

    static bool readScriptFromFs(const String& path, String& outScript) {
        File f = LittleFS.open(path, "r");
        if (!f || f.isDirectory()) return false;
        constexpr size_t MAX_SCRIPT_SIZE = 24 * 1024;
        outScript = "";
        outScript.reserve(1024);
        while (f.available() && outScript.length() < MAX_SCRIPT_SIZE) {
            outScript += (char)f.read();
        }
        f.close();
        return outScript.length() > 0;
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
    PythonApp(String script) : scriptSource(script) {}

    static void queueScriptFromFile(const String& path) {
        queuedScriptPath = path;
    }

    static bool takeQueuedScript(String& outScript) {
        if (queuedScriptPath.length() == 0) return false;
        String path = queuedScriptPath;
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
        // Initialise l'environnement PikaPython une seule fois
        if (globalPikaEnv == nullptr) {
            watchdog_update();
            globalPikaEnv = pikaPythonInit();
            watchdog_update();
        }

        if (scriptSource.length() == 0) return;

        // Activer la capture de sortie
        _pika_output_buf = "";
        _pika_capture_active = true;

        watchdog_update();
        VMParameters* result = obj_run(globalPikaEnv, (char*)scriptSource.c_str());
        watchdog_update();

        _pika_capture_active = false;

        // Vérifier les erreurs
        int errCode = obj_getErrorCode(globalPikaEnv);
        if (errCode != 0) {
            Serial.printf("[PythonApp] Erreur PikaPython code=%d\n", errCode);
            // Nettoyer l'écran (les widgets créés partiellement)
            lv_obj_clean(lv_scr_act());
            showErrorScreen(parent, errCode, _pika_output_buf);
        }
        _pika_output_buf = "";
    }

    void update() override {}
    void stop() override {}
};

PikaObj* PythonApp::globalPikaEnv = nullptr;
String PythonApp::queuedScriptPath = "";

#endif