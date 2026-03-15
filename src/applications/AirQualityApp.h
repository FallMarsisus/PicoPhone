#pragma once

#include "App.h"
#include "AppManager.h"
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <pico/mutex.h>
#include <hardware/watchdog.h>
#include "../system/LTE.h"
#include "../system/NetworkErrorHandler.h"

// --- OpenWeatherMap Air Pollution API (same key as WeatherApp) ---
// Paris coordinates
#define AQ_LAT      "48.8566"
#define AQ_LON      "2.3522"
#define AQ_API_KEY  "8fdaebc1c5f040d39d2178f811adfeaa"
#define AQ_API_URL  "http://api.openweathermap.org/data/2.5/air_pollution?lat=" AQ_LAT "&lon=" AQ_LON "&appid=" AQ_API_KEY

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(lv_font_montserrat_28);

struct AirData {
    int   aqi;       // 1-5
    float pm25;
    float pm10;
    float o3;
    float no2;
    bool  success;
};

auto_init_mutex(airMutex);

class AirQualityApp : public App {
private:
    lv_obj_t* main_bg    = nullptr;
    lv_obj_t* card_aqi   = nullptr;
    lv_obj_t* lbl_aqi_num= nullptr;
    lv_obj_t* lbl_aqi_txt= nullptr;
    lv_obj_t* lbl_pm25   = nullptr;
    lv_obj_t* lbl_pm10   = nullptr;
    lv_obj_t* lbl_o3     = nullptr;
    lv_obj_t* lbl_no2    = nullptr;
    lv_obj_t* lbl_status = nullptr;
    lv_obj_t* loader     = nullptr;

    bool refresh_requested = false;
    bool has_new_data      = false;
    AirData shared_data{};

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void refresh_event(lv_event_t* e) {
        AirQualityApp* app = (AirQualityApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->refresh_requested = true;
        if (app->loader) lv_obj_clear_flag(app->loader, LV_OBJ_FLAG_HIDDEN);
        if (app->lbl_status) lv_label_set_text(app->lbl_status, "Actualisation...");
    }

    // AQI 1-5 → label text
    static const char* aqi_label(int aqi) {
        switch (aqi) {
            case 1: return "Bonne";
            case 2: return "Correcte";
            case 3: return "Moderee";
            case 4: return "Mauvaise";
            case 5: return "Tres Mauvaise";
            default: return "Inconnue";
        }
    }

    // AQI 1-5 → background gradient top color
    static uint32_t aqi_color(int aqi) {
        switch (aqi) {
            case 1: return 0x27AE60;   // green
            case 2: return 0xF1C40F;   // yellow
            case 3: return 0xE67E22;   // orange
            case 4: return 0xC0392B;   // red
            case 5: return 0x8E44AD;   // purple
            default: return 0x636366;
        }
    }

    lv_obj_t* make_pollutant_row(lv_obj_t* parent, const char* name, lv_coord_t y) {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, 270, 48);
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl_name = lv_label_create(row);
        lv_label_set_text(lbl_name, name);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xAEAEB2), 0);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 10, 0);

        lv_obj_t* lbl_val = lv_label_create(row);
        lv_label_set_text(lbl_val, "---");
        lv_obj_set_style_text_color(lbl_val, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_val, LV_ALIGN_RIGHT_MID, -10, 0);
        return lbl_val;  // return value label for later updates
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        refresh_requested = false;
        has_new_data = false;

        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);

        // --- Header ---
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_home, LV_EVENT_CLICKED, NULL);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_hex(0x007AFF), 0);
        lv_obj_center(l_back);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Qualite de l'Air");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_center(title);

        lv_obj_t* btn_ref = lv_btn_create(header);
        lv_obj_set_size(btn_ref, 40, 40);
        lv_obj_align(btn_ref, LV_ALIGN_RIGHT_MID, 10, 0);
        lv_obj_set_style_bg_opa(btn_ref, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_ref, refresh_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_ref = lv_label_create(btn_ref);
        lv_label_set_text(l_ref, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(l_ref, lv_color_hex(0x007AFF), 0);
        lv_obj_center(l_ref);

        // --- AQI main card ---
        card_aqi = lv_obj_create(main_bg);
        lv_obj_set_size(card_aqi, 290, 160);
        lv_obj_align(card_aqi, LV_ALIGN_TOP_MID, 0, 60);
        lv_obj_set_style_bg_color(card_aqi, lv_color_hex(0x27AE60), 0);
        lv_obj_set_style_radius(card_aqi, 20, 0);
        lv_obj_set_style_border_width(card_aqi, 0, 0);
        lv_obj_clear_flag(card_aqi, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl_city = lv_label_create(card_aqi);
        lv_label_set_text(lbl_city, "Paris");
        lv_obj_set_style_text_color(lbl_city, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_city, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_city, LV_ALIGN_TOP_MID, 0, 8);

        lbl_aqi_num = lv_label_create(card_aqi);
        lv_label_set_text(lbl_aqi_num, "--");
        lv_obj_set_style_text_color(lbl_aqi_num, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_aqi_num, &lv_font_montserrat_28, 0);
        lv_obj_align(lbl_aqi_num, LV_ALIGN_CENTER, 0, -5);

        lbl_aqi_txt = lv_label_create(card_aqi);
        lv_label_set_text(lbl_aqi_txt, "En attente...");
        lv_obj_set_style_text_color(lbl_aqi_txt, lv_color_hex(0xEEEEEE), 0);
        lv_obj_set_style_text_font(lbl_aqi_txt, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl_aqi_txt, LV_ALIGN_BOTTOM_MID, 0, -8);

        // --- Pollutant rows ---
        lbl_pm25 = make_pollutant_row(main_bg, "PM2.5  (ug/m3)",  235);
        lbl_pm10 = make_pollutant_row(main_bg, "PM10   (ug/m3)",  291);
        lbl_o3   = make_pollutant_row(main_bg, "Ozone  (ug/m3)",  347);
        lbl_no2  = make_pollutant_row(main_bg, "NO2    (ug/m3)",  403);

        // --- Status label ---
        lbl_status = lv_label_create(main_bg);
        lv_label_set_text(lbl_status, "Chargement...");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -8);

        // --- Spinner ---
        loader = lv_spinner_create(main_bg, 1000, 60);
        lv_obj_set_size(loader, 40, 40);
        lv_obj_center(loader);
        lv_obj_clear_flag(loader, LV_OBJ_FLAG_HIDDEN);

        refresh_requested = true;
    }

    // --- CORE 0: UI update ---
    void update() override {
        if (!has_new_data) return;

        if (!mutex_try_enter(&airMutex, nullptr)) return;
        AirData data = shared_data;
        has_new_data = false;
        mutex_exit(&airMutex);

        lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);

        if (data.success) {
            // Update AQI card background color
            uint32_t col = aqi_color(data.aqi);
            lv_obj_set_style_bg_color(card_aqi, lv_color_hex(col), 0);

            char buf[16];
            snprintf(buf, sizeof(buf), "IQA %d/5", data.aqi);
            lv_label_set_text(lbl_aqi_num, buf);
            lv_label_set_text(lbl_aqi_txt, aqi_label(data.aqi));

            snprintf(buf, sizeof(buf), "%.1f", data.pm25);
            lv_label_set_text(lbl_pm25, buf);

            snprintf(buf, sizeof(buf), "%.1f", data.pm10);
            lv_label_set_text(lbl_pm10, buf);

            snprintf(buf, sizeof(buf), "%.1f", data.o3);
            lv_label_set_text(lbl_o3, buf);

            snprintf(buf, sizeof(buf), "%.1f", data.no2);
            lv_label_set_text(lbl_no2, buf);

            lv_label_set_text(lbl_status, "Mis a jour");
        } else {
            NetworkErrorHandler::showIfError("Qualite Air", "Erreur de chargement");
            lv_label_set_text(lbl_status, "Erreur reseau");
        }
    }

    // --- CORE 1: network fetch ---
    void update1() override {
        if (!refresh_requested) return;
        watchdog_update();

        bool use_wifi = (WiFi.status() == WL_CONNECTED);
        bool use_lte  = (!use_wifi && LTE::isReadyForData());

        if (!use_wifi && !use_lte) {
            AirData fail{};
            fail.success = false;
            NetworkErrorHandler::showIfError("Qualite Air", "Aucune connexion reseau");
            unsigned long t0 = millis();
            while (millis() - t0 < 30) {
                watchdog_update();
                if (mutex_try_enter(&airMutex, nullptr)) {
                    shared_data = fail;
                    has_new_data = true;
                    mutex_exit(&airMutex);
                    break;
                }
                delay(1);
            }
            refresh_requested = false;
            return;
        }

        AirData newData{};
        newData.success = false;
        String url = AQ_API_URL;
        String payload;

        if (use_wifi) {
            HTTPClient http;
            http.setTimeout(8000);
            http.begin(url);
            int code = http.GET();
            watchdog_update();
            if (code == 200) {
                payload = http.getString();
            }
            http.end();
        } else {
            payload = LTE::httpGetBlocking(url);
        }
        watchdog_update();

        if (payload.length() > 0) {
            JsonDocument filter;
            filter["list"][0]["main"]["aqi"] = true;
            filter["list"][0]["components"]["pm2_5"] = true;
            filter["list"][0]["components"]["pm10"]  = true;
            filter["list"][0]["components"]["o3"]    = true;
            filter["list"][0]["components"]["no2"]   = true;

            JsonDocument doc;
            auto err = deserializeJson(doc, payload,
                                       DeserializationOption::Filter(filter));
            if (!err && doc["list"].is<JsonArray>()) {
                JsonObject comp = doc["list"][0]["components"];
                newData.aqi  = doc["list"][0]["main"]["aqi"].as<int>();
                newData.pm25 = comp["pm2_5"].as<float>();
                newData.pm10 = comp["pm10"].as<float>();
                newData.o3   = comp["o3"].as<float>();
                newData.no2  = comp["no2"].as<float>();
                newData.success = (newData.aqi >= 1 && newData.aqi <= 5);
            }
        }

        unsigned long t0 = millis();
        while (millis() - t0 < 50) {
            watchdog_update();
            if (mutex_try_enter(&airMutex, nullptr)) {
                shared_data = newData;
                has_new_data = true;
                mutex_exit(&airMutex);
                break;
            }
            delay(1);
        }
        refresh_requested = false;
    }
};
