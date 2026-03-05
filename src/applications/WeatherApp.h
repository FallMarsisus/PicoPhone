#ifndef WEATHER_APP_H
#define WEATHER_APP_H

#include "App.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> 
#include <math.h> 
#include <vector>
#include <pico/mutex.h>
#include <hardware/watchdog.h>
#include "../system/LTE.h"
#include "../system/NetworkErrorHandler.h"

// --- CONFIG ---
#define API_KEY "8fdaebc1c5f040d39d2178f811adfeaa"
#define CITY_NAME "Paris"
#define COUNTRY_CODE "fr"

// --- STRUCTURES DE DONNÉES (Pour passer du Core 1 au Core 0) ---
struct ForecastItem {
    long dt;
    float temp;
    String icon;
};

struct WeatherData {
    // Données actuelles
    String cityName;
    float currentTemp;
    String currentIcon;
    bool success;
    
    // Données prévisions
    std::vector<ForecastItem> forecastList;
};

auto_init_mutex(weatherMutex);

// On déclare les polices
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_28);

class WeatherApp : public App {
private:
    lv_obj_t* main_bg;
    
    // UI Elements
    lv_obj_t* current_card;
    lv_obj_t* lbl_city;
    lv_obj_t* lbl_temp;
    lv_obj_t* lbl_unit; 
    lv_obj_t* lbl_desc;
    lv_obj_t* forecast_card;
    lv_obj_t* loader;

    // Logique de synchro
    bool refresh_requested = false;
    bool has_new_data = false;
    WeatherData sharedData; // La boite aux lettres

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }
    
    static void refresh_event(lv_event_t* e) {
        WeatherApp* app = (WeatherApp*)lv_event_get_user_data(e);
        app->refresh_requested = true;
        lv_obj_clear_flag(app->loader, LV_OBJ_FLAG_HIDDEN); // On affiche le loader tout de suite (Core 0)
    }

    // --- HELPER TEXTE ---
    const char* get_short_desc(String iconCode) {
        if (iconCode == "01d") return "SOLEIL";       
        if (iconCode == "01n") return "NUIT CLR";      
        if (iconCode.startsWith("02")) return "ECLAIRCIES"; 
        if (iconCode.startsWith("03")) return "NUAG."; 
        if (iconCode.startsWith("04")) return "COUV."; 
        if (iconCode.startsWith("09")) return "AVERSES"; 
        if (iconCode.startsWith("10")) return "PLUIE"; 
        if (iconCode.startsWith("11")) return "ORAGE"; 
        if (iconCode.startsWith("13")) return "NEIGE";   
        if (iconCode.startsWith("50")) return "BRUME"; 
        return "";
    }

    const char* get_icon_symbol(String iconCode) {
        if (iconCode == "01d") return "SOL";       
        if (iconCode == "01n") return "LUNE";      
        if (iconCode.startsWith("02")) return "NUAG"; 
        if (iconCode.startsWith("03")) return "NUAG"; 
        if (iconCode.startsWith("04")) return "COUV"; 
        if (iconCode.startsWith("09")) return "PLUIE"; 
        if (iconCode.startsWith("10")) return "PLUIE"; 
        if (iconCode.startsWith("11")) return "ORAGE"; 
        if (iconCode.startsWith("13")) return "NEIGE";   
        return "-";
    }

    void update_background_style(String icon) {
        lv_color_t top, bot;
        top = lv_color_hex(0xbdc3c7); bot = lv_color_hex(0x2c3e50); 

        if (icon == "01d") { top = lv_color_hex(0xFF8008); bot = lv_color_hex(0xFFC837); }
        else if (icon.endsWith("n")) { top = lv_color_hex(0x0f2027); bot = lv_color_hex(0x000000); }
        else if (icon.startsWith("02") || icon.startsWith("03")) { top = lv_color_hex(0x2193b0); bot = lv_color_hex(0x6dd5ed); }
        else if (icon.startsWith("04")) { top = lv_color_hex(0x3E5151); bot = lv_color_hex(0xDECBA4); }
        else if (icon.startsWith("09") || icon.startsWith("10")) { top = lv_color_hex(0x20002c); bot = lv_color_hex(0xcbb4d4); }
        
        lv_obj_set_style_bg_color(main_bg, top, 0);
        lv_obj_set_style_bg_grad_color(main_bg, bot, 0);
        lv_obj_set_style_bg_grad_dir(main_bg, LV_GRAD_DIR_VER, 0);
    }

    // Fonction Helper UI (exécutée par Core 0)
    void add_forecast_ui_item(long dt, float temp, String icon) {
        int hour = (dt / 3600 + 1) % 24; // Approximation simple UTC+1
        
        lv_obj_t* item = lv_obj_create(forecast_card);
        lv_obj_set_size(item, 50, 75); 
        lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(item, 0, 0);
        lv_obj_set_style_pad_row(item, 2, 0);

        lv_obj_t* l_time = lv_label_create(item);
        lv_label_set_text_fmt(l_time, "%dh", hour);
        lv_obj_set_style_text_color(l_time, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_time, &lv_font_montserrat_12, 0);

        lv_obj_t* l_icon = lv_label_create(item);
        lv_label_set_text(l_icon, get_icon_symbol(icon));
        lv_obj_set_style_text_color(l_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_12, 0);

        lv_obj_t* l_temp = lv_label_create(item);
        int t = (int)round(temp);
        lv_label_set_text_fmt(l_temp, "%d°", t); 
        lv_obj_set_style_text_color(l_temp, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_temp, &lv_font_montserrat_14, 0);
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent; 
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0xbdc3c7), 0);
        
        // --- HEADER ---
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
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
        lv_obj_set_style_text_color(l_back, lv_color_white(), 0);
        lv_obj_center(l_back);

        lv_obj_t* btn_ref = lv_btn_create(header);
        lv_obj_set_size(btn_ref, 40, 40);
        lv_obj_align(btn_ref, LV_ALIGN_RIGHT_MID, 10, 0);
        lv_obj_set_style_bg_opa(btn_ref, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_ref, refresh_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_ref = lv_label_create(btn_ref);
        lv_label_set_text(l_ref, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(l_ref, lv_color_white(), 0);
        lv_obj_center(l_ref);

        // --- CURRENT CARD ---
        current_card = lv_obj_create(main_bg);
        lv_obj_set_size(current_card, 290, 180); 
        lv_obj_align(current_card, LV_ALIGN_TOP_MID, 0, 70);
        lv_obj_set_style_bg_color(current_card, lv_color_hex(0xFFFFFF), 0); // Blanc par defaut
        lv_obj_set_style_bg_opa(current_card, LV_OPA_80, 0);
        lv_obj_set_style_radius(current_card, 20, 0);
        lv_obj_set_style_border_width(current_card, 0, 0);
        lv_obj_clear_flag(current_card, LV_OBJ_FLAG_SCROLLABLE);

        lbl_city = lv_label_create(current_card);
        lv_label_set_text(lbl_city, CITY_NAME);
        lv_obj_set_style_text_font(lbl_city, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_city, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t* cont_temp = lv_obj_create(current_card);
        lv_obj_set_size(cont_temp, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_opa(cont_temp, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cont_temp, 0, 0);
        lv_obj_set_flex_flow(cont_temp, LV_FLEX_FLOW_ROW);
        lv_obj_align(cont_temp, LV_ALIGN_CENTER, 0, 0);

        lbl_temp = lv_label_create(cont_temp);
        lv_label_set_text(lbl_temp, "--");
        lv_obj_set_style_text_font(lbl_temp, &lv_font_montserrat_28, 0);

        lbl_unit = lv_label_create(cont_temp);
        lv_label_set_text(lbl_unit, "o");
        lv_obj_set_style_text_font(lbl_unit, &lv_font_montserrat_14, 0);
        lv_obj_set_style_translate_y(lbl_unit, -15, 0); 

        lbl_desc = lv_label_create(current_card);
        lv_label_set_text(lbl_desc, "En attente...");
        lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_desc, LV_ALIGN_BOTTOM_MID, 0, -5);

        // --- FORECAST CARD ---
        forecast_card = lv_obj_create(main_bg);
        lv_obj_set_size(forecast_card, 290, 100);
        lv_obj_align(forecast_card, LV_ALIGN_BOTTOM_MID, 0, -40);
        lv_obj_set_style_bg_color(forecast_card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(forecast_card, LV_OPA_60, 0);
        lv_obj_set_style_radius(forecast_card, 15, 0);
        lv_obj_set_style_border_width(forecast_card, 0, 0);
        lv_obj_set_flex_flow(forecast_card, LV_FLEX_FLOW_ROW); 
        lv_obj_set_flex_align(forecast_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scroll_dir(forecast_card, LV_DIR_HOR); 

        loader = lv_spinner_create(main_bg, 1000, 60);
        lv_obj_set_size(loader, 40, 40);
        lv_obj_center(loader);
        lv_obj_clear_flag(loader, LV_OBJ_FLAG_HIDDEN);

        refresh_requested = true;
    }
    
    // --- CORE 0 : MISE A JOUR UI (Rapide) ---
    void update() override {
        // Est-ce que le Core 1 a livré le colis ?
        if (has_new_data) {
            // On récupère le colis sans bloquer le Core 0 (anti-watchdog)
            if (!mutex_try_enter(&weatherMutex, nullptr)) {
                return;
            }
            WeatherData data = sharedData; // Copie
            has_new_data = false;
            mutex_exit(&weatherMutex);

            // Mise à jour UI
            if (data.success) {
                // Current
                lv_label_set_text(lbl_city, data.cityName.c_str());
                char tempBuf[16];
                sprintf(tempBuf, "%d", (int)round(data.currentTemp)); 
                lv_label_set_text(lbl_temp, tempBuf);
                lv_label_set_text(lbl_desc, get_short_desc(data.currentIcon));
                update_background_style(data.currentIcon);

                // Forecast
                lv_obj_clean(forecast_card); // On vide la liste
                for (auto& item : data.forecastList) {
                    add_forecast_ui_item(item.dt, item.temp, item.icon);
                }
            } else {
                // Afficher des détails sur l'erreur réseau
                NetworkErrorHandler::showIfError("Météo", "Impossible de récupérer les données");
                String error_msg = "Erreur: ";
                error_msg += NetworkErrorHandler::getNetworkStatus();
                lv_label_set_text(lbl_desc, error_msg.c_str());
            }
            
            lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // --- CORE 1 : TRAVAIL RÉSEAU (Lent) ---
    void update1() override {
        if (!refresh_requested) return;
        watchdog_update();

        bool use_wifi = (WiFi.status() == WL_CONNECTED);
        bool use_lte  = (!use_wifi && LTE::isReadyForData());
        if (!use_wifi && !use_lte) {
            WeatherData fail;
            fail.success = false;
            // Afficher l'erreur réseau
            NetworkErrorHandler::showIfError("Météo", "Aucune connexion réseau disponible");
            unsigned long t0 = millis();
            while (millis() - t0 < 30) {
                watchdog_update();
                if (mutex_try_enter(&weatherMutex, nullptr)) {
                    sharedData = std::move(fail);
                    has_new_data = true;
                    mutex_exit(&weatherMutex);
                    break;
                }
                delay(1);
            }
            refresh_requested = false;
            return;
        }

        WeatherData newData;
        newData.success = false;
        newData.forecastList.reserve(8);

        String url1 = "http://api.openweathermap.org/data/2.5/weather?q=" CITY_NAME "," COUNTRY_CODE "&appid=" API_KEY "&units=metric&lang=fr";
        String url2 = "http://api.openweathermap.org/data/2.5/forecast?q=" CITY_NAME "," COUNTRY_CODE "&appid=" API_KEY "&units=metric&cnt=8";

        if (use_wifi) {
            // --- Chemin WiFi (HTTPClient) ---
            HTTPClient http;
            http.setTimeout(8000);
            http.begin(url1);
            int httpCode = http.GET();
            watchdog_update();
            if (httpCode == 200) {
                String payload = http.getString();
                JsonDocument doc;
                auto err = deserializeJson(doc, payload);
                if (!err) {
                    newData.cityName     = doc["name"].as<String>();
                    newData.currentTemp  = doc["main"]["temp"];
                    newData.currentIcon  = doc["weather"][0]["icon"].as<String>();
                    newData.success      = true;
                }
            }
            http.end();

            if (newData.success) {
                HTTPClient http2;
                http2.setTimeout(8000);
                http2.begin(url2);
                int httpCode2 = http2.GET();
                watchdog_update();
                if (httpCode2 == 200) {
                    WiFiClient* stream = http2.getStreamPtr();
                    JsonDocument filter;
                    filter["list"][0]["dt"] = true;
                    filter["list"][0]["main"]["temp"] = true;
                    filter["list"][0]["weather"][0]["icon"] = true;
                    JsonDocument doc;
                    auto err = deserializeJson(doc, *stream, DeserializationOption::Filter(filter));
                    if (!err) {
                        for (JsonObject item : doc["list"].as<JsonArray>()) {
                            watchdog_update();
                            ForecastItem fItem;
                            fItem.dt   = item["dt"];
                            fItem.temp = item["main"]["temp"];
                            fItem.icon = item["weather"][0]["icon"].as<String>();
                            newData.forecastList.push_back(fItem);
                        }
                    }
                }
                http2.end();
            }
        } else {
            // --- Chemin 4G (AT+HTTP) ---
            String payload = LTE::httpGetBlocking(url1);
            if (payload.length() > 0) {
                JsonDocument filter;
                filter["name"] = true;
                filter["main"]["temp"] = true;
                filter["weather"][0]["icon"] = true;
                JsonDocument doc;
                auto err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
                if (!err) {
                    newData.cityName    = doc["name"].as<String>();
                    newData.currentTemp = doc["main"]["temp"];
                    newData.currentIcon = doc["weather"][0]["icon"].as<String>();
                    newData.success     = true;
                }
            }
            if (newData.success) {
                String payload2 = LTE::httpGetBlocking(url2);
                if (payload2.length() > 0) {
                    JsonDocument filter;
                    filter["list"][0]["dt"] = true;
                    filter["list"][0]["main"]["temp"] = true;
                    filter["list"][0]["weather"][0]["icon"] = true;
                    JsonDocument doc;
                    auto err = deserializeJson(doc, payload2, DeserializationOption::Filter(filter));
                    if (!err) {
                        for (JsonObject item : doc["list"].as<JsonArray>()) {
                            watchdog_update();
                            ForecastItem fItem;
                            fItem.dt   = item["dt"];
                            fItem.temp = item["main"]["temp"];
                            fItem.icon = item["weather"][0]["icon"].as<String>();
                            newData.forecastList.push_back(fItem);
                        }
                    }
                }
            }
        }

        bool posted = false;
        unsigned long t0 = millis();
        while (millis() - t0 < 50) {
            watchdog_update();
            if (mutex_try_enter(&weatherMutex, nullptr)) {
                sharedData = std::move(newData);
                has_new_data = true;
                mutex_exit(&weatherMutex);
                posted = true;
                break;
            }
            delay(1);
        }
        if (!posted) {
            has_new_data = false;
        }
        refresh_requested = false;
    }
};

#endif