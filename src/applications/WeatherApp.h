#ifndef WEATHER_APP_H
#define WEATHER_APP_H

#include "App.h"
#include "AppManager.h"
#include <ArduinoJson.h>
#include <math.h>
#include <vector>
#include <pico/mutex.h>
#include <hardware/watchdog.h>
#include "../system/LTE.h"
#include "../system/Logger.h"
#include "../system/NetworkErrorHandler.h"

#define API_KEY "8fdaebc1c5f040d39d2178f811adfeaa"
#define CITY_NAME "Paris"
#define COUNTRY_CODE "fr"

#define AQ_LAT "48.8566"
#define AQ_LON "2.3522"

struct ForecastItem {
    String dayLabel;
    float minTemp = 0.0f;
    float maxTemp = 0.0f;
    String icon;
};

struct HourlyForecastItem {
    String hourLabel;
    float temp = 0.0f;
    String icon;
};

struct WeatherData {
    String cityName;
    float currentTemp = 0.0f;
    String currentIcon;
    bool weatherSuccess = false;
    bool airSuccess = false;
    int aqi = 0;
    float pm25 = 0.0f;
    float pm10 = 0.0f;
    float o3 = 0.0f;
    float no2 = 0.0f;
    std::vector<HourlyForecastItem> hourlyList;
    std::vector<ForecastItem> forecastList;
};

auto_init_mutex(weatherMutex);

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(lv_font_montserrat_28);

class WeatherApp : public App {
private:
    enum ViewMode {
        VIEW_WEATHER,
        VIEW_AIR
    };

    bool start_on_air = false;
    ViewMode active_view = VIEW_WEATHER;

    lv_obj_t* main_bg = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* weather_view = nullptr;
    lv_obj_t* air_view = nullptr;
    lv_obj_t* footer = nullptr;
    lv_obj_t* loader = nullptr;

    lv_obj_t* current_card = nullptr;
    lv_obj_t* lbl_city = nullptr;
    lv_obj_t* lbl_temp = nullptr;
    lv_obj_t* lbl_unit = nullptr;
    lv_obj_t* lbl_desc = nullptr;
    lv_obj_t* hourly_card = nullptr;
    lv_obj_t* hourly_list = nullptr;
    lv_obj_t* forecast_card = nullptr;
    lv_obj_t* daily_list = nullptr;

    lv_obj_t* card_aqi = nullptr;
    lv_obj_t* lbl_aqi_num = nullptr;
    lv_obj_t* lbl_aqi_txt = nullptr;
    lv_obj_t* lbl_pm25 = nullptr;
    lv_obj_t* lbl_pm10 = nullptr;
    lv_obj_t* lbl_o3 = nullptr;
    lv_obj_t* lbl_no2 = nullptr;
    lv_obj_t* lbl_status = nullptr;

    lv_obj_t* btn_weather = nullptr;
    lv_obj_t* btn_air = nullptr;
    lv_obj_t* weather_label = nullptr;
    lv_obj_t* air_label = nullptr;

    bool refresh_requested = false;
    bool has_new_data = false;
    WeatherData sharedData;

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void refresh_event(lv_event_t* e) {
        WeatherApp* app = (WeatherApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->refresh_requested = true;
        if (app->loader) lv_obj_clear_flag(app->loader, LV_OBJ_FLAG_HIDDEN);
    }

    static void tab_weather_event(lv_event_t* e) {
        WeatherApp* app = (WeatherApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->set_view(VIEW_WEATHER);
    }

    static void tab_air_event(lv_event_t* e) {
        WeatherApp* app = (WeatherApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->set_view(VIEW_AIR);
    }

    static const char* get_short_desc(const String& iconCode) {
        if (iconCode == "01d") return "SOLEIL";
        if (iconCode == "01n") return "NUIT CLR";
        if (iconCode.startsWith("02")) return "ECLAIRCIES";
        if (iconCode.startsWith("03")) return "NUAGEUX";
        if (iconCode.startsWith("04")) return "COUVERT";
        if (iconCode.startsWith("09")) return "AVERSES";
        if (iconCode.startsWith("10")) return "PLUIE";
        if (iconCode.startsWith("11")) return "ORAGE";
        if (iconCode.startsWith("13")) return "NEIGE";
        if (iconCode.startsWith("50")) return "BRUME";
        return "INCONNU";
    }

    static const char* get_icon_symbol(const String& iconCode) {
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

    static uint32_t aqi_color(int aqi) {
        switch (aqi) {
            case 1: return 0x27AE60;
            case 2: return 0xF1C40F;
            case 3: return 0xE67E22;
            case 4: return 0xC0392B;
            case 5: return 0x8E44AD;
            default: return 0x636366;
        }
    }

    static String day_label_from_iso(const String& isoDate) {
        if (isoDate.length() < 10) return isoDate;
        return isoDate.substring(8, 10) + "/" + isoDate.substring(5, 7);
    }

    static String hour_label_from_dt_txt(const String& dtTxt) {
        if (dtTxt.length() < 16) return dtTxt;
        return dtTxt.substring(11, 13) + "h";
    }

    void update_background_style(const String& icon) {
        lv_color_t top = lv_color_hex(0xBDC3C7);
        lv_color_t bot = lv_color_hex(0x2C3E50);

        if (icon == "01d") {
            top = lv_color_hex(0xFF8008);
            bot = lv_color_hex(0xFFC837);
        } else if (icon.endsWith("n")) {
            top = lv_color_hex(0x0F2027);
            bot = lv_color_hex(0x000000);
        } else if (icon.startsWith("02") || icon.startsWith("03")) {
            top = lv_color_hex(0x2193B0);
            bot = lv_color_hex(0x6DD5ED);
        } else if (icon.startsWith("04")) {
            top = lv_color_hex(0x3E5151);
            bot = lv_color_hex(0xDECBA4);
        } else if (icon.startsWith("09") || icon.startsWith("10")) {
            top = lv_color_hex(0x20002C);
            bot = lv_color_hex(0xCBB4D4);
        }

        lv_obj_set_style_bg_color(main_bg, top, 0);
        lv_obj_set_style_bg_grad_color(main_bg, bot, 0);
        lv_obj_set_style_bg_grad_dir(main_bg, LV_GRAD_DIR_VER, 0);
    }

    static void set_view_flag(lv_obj_t* obj, bool visible) {
        if (!obj) return;
        if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    void set_tab_visuals() {
        if (!btn_weather || !btn_air) return;

        const bool weather_selected = (active_view == VIEW_WEATHER);
        const uint32_t selected_bg = 0x2C2C2E;
        const uint32_t selected_fg = 0xFFFFFF;
        const uint32_t unselected_fg = 0x8E8E93;

        lv_obj_set_style_bg_color(btn_weather, lv_color_hex(weather_selected ? selected_bg : 0x101114), 0);
        lv_obj_set_style_bg_opa(btn_weather, weather_selected ? LV_OPA_COVER : LV_OPA_0, 0);
        lv_obj_set_style_bg_color(btn_air, lv_color_hex(weather_selected ? 0x101114 : selected_bg), 0);
        lv_obj_set_style_bg_opa(btn_air, weather_selected ? LV_OPA_0 : LV_OPA_COVER, 0);

        if (weather_label) lv_obj_set_style_text_color(weather_label, lv_color_hex(weather_selected ? selected_fg : unselected_fg), 0);
        if (air_label) lv_obj_set_style_text_color(air_label, lv_color_hex(weather_selected ? unselected_fg : selected_fg), 0);
    }

    void set_view(ViewMode view) {
        active_view = view;
        set_view_flag(weather_view, view == VIEW_WEATHER);
        set_view_flag(air_view, view == VIEW_AIR);
        set_tab_visuals();
    }

    void add_forecast_ui_item(const ForecastItem& item) {
        lv_obj_t* card = lv_obj_create(daily_list);
        lv_obj_set_size(card, 270, 34);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_pad_left(card, 10, 0);
        lv_obj_set_style_pad_right(card, 10, 0);

        lv_obj_t* l_day = lv_label_create(card);
        lv_label_set_text(l_day, item.dayLabel.c_str());
        lv_obj_set_style_text_color(l_day, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_day, &lv_font_montserrat_12, 0);

        lv_obj_t* l_icon = lv_label_create(card);
        lv_label_set_text(l_icon, get_icon_symbol(item.icon));
        lv_obj_set_style_text_color(l_icon, lv_color_hex(0xD1D1D6), 0);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_12, 0);

        lv_obj_t* l_temp = lv_label_create(card);
        lv_label_set_text_fmt(l_temp, "%d/%d", (int)round(item.minTemp), (int)round(item.maxTemp));
        lv_obj_set_style_text_color(l_temp, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_temp, &lv_font_montserrat_12, 0);
    }

    void add_hourly_ui_item(const HourlyForecastItem& item) {
        lv_obj_t* card = lv_obj_create(hourly_list);
        lv_obj_set_size(card, 56, 76);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_style_pad_row(card, 2, 0);

        lv_obj_t* l_hour = lv_label_create(card);
        lv_label_set_text(l_hour, item.hourLabel.c_str());
        lv_obj_set_style_text_color(l_hour, lv_color_hex(0xD1D1D6), 0);
        lv_obj_set_style_text_font(l_hour, &lv_font_montserrat_12, 0);

        lv_obj_t* l_icon = lv_label_create(card);
        lv_label_set_text(l_icon, get_icon_symbol(item.icon));
        lv_obj_set_style_text_color(l_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_12, 0);

        lv_obj_t* l_temp = lv_label_create(card);
        lv_label_set_text_fmt(l_temp, "%d°", (int)round(item.temp));
        lv_obj_set_style_text_color(l_temp, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_temp, &lv_font_montserrat_12, 0);
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
        return lbl_val;
    }

    void render_weather_ui(const WeatherData& data) {
        if (!current_card || !hourly_list || !daily_list) return;

        if (data.weatherSuccess) {
            lv_label_set_text(lbl_city, data.cityName.c_str());
            char tempBuf[16];
            snprintf(tempBuf, sizeof(tempBuf), "%d", (int)round(data.currentTemp));
            lv_label_set_text(lbl_temp, tempBuf);
            lv_label_set_text(lbl_desc, get_short_desc(data.currentIcon));
            update_background_style(data.currentIcon);

            lv_obj_clean(hourly_list);
            lv_obj_clean(daily_list);
            for (const auto& item : data.hourlyList) {
                add_hourly_ui_item(item);
            }
            for (const auto& item : data.forecastList) {
                add_forecast_ui_item(item);
            }
        } else {
            NetworkErrorHandler::showIfError("Meteo", "Impossible de recuperer les donnees");
            lv_label_set_text(lbl_desc, "Erreur parsing JSON");
        }
    }

    void render_air_ui(const WeatherData& data) {
        if (!card_aqi) return;

        if (data.airSuccess) {
            lv_obj_set_style_bg_color(card_aqi, lv_color_hex(aqi_color(data.aqi)), 0);
            char buf[24];
            snprintf(buf, sizeof(buf), "IQA %d/5", data.aqi);
            lv_label_set_text(lbl_aqi_num, buf);
            lv_label_set_text(lbl_aqi_txt, aqi_label(data.aqi));
            lv_label_set_text_fmt(lbl_pm25, "%.1f", data.pm25);
            lv_label_set_text_fmt(lbl_pm10, "%.1f", data.pm10);
            lv_label_set_text_fmt(lbl_o3, "%.1f", data.o3);
            lv_label_set_text_fmt(lbl_no2, "%.1f", data.no2);
            lv_label_set_text(lbl_status, "Mis a jour");
        } else {
            NetworkErrorHandler::showIfError("Qualite Air", "Erreur de chargement");
            lv_label_set_text(lbl_status, "Erreur reseau");
        }
    }

    bool http_get_payload(const String& url, String& payload) {
        payload = "";
        payload = LTE::httpGetBlocking(url);
        return payload.length() > 0;
    }

    // --- PARSING SANS FILTRE & AVEC FALLBACK ---
    bool parse_weather_payload(const String& payload, WeatherData& out) {
        JsonDocument doc; 
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Logger::printf("[Weather] Current JSON Error: %s\n", err.c_str());
            return false;
        }

        out.cityName = doc["name"] | "Inconnu";
        out.currentTemp = doc["main"]["temp"] | 0.0f;
        
        if (doc["weather"].is<JsonArray>() && doc["weather"].size() > 0) {
            out.currentIcon = doc["weather"][0]["icon"] | "01d";
        } else {
            out.currentIcon = "01d";
        }
        
        out.weatherSuccess = true;
        Logger::println("[Weather] Current Parse OK!");
        return true;
    }

    void aggregate_forecast(const JsonArray& items, WeatherData& out) {
        out.forecastList.clear();
        out.hourlyList.clear();
        for (JsonObject item : items) {
            String dt_txt = item["dt_txt"] | "";
            if (dt_txt.length() < 10) continue;
            
            String dayKey = dt_txt.substring(0, 10);
            String hourLabel = hour_label_from_dt_txt(dt_txt);
            
            float temp = 0.0f;
            if (item["main"].is<JsonObject>()) {
                temp = item["main"]["temp"] | 0.0f;
            }
            
            String icon = "01d";
            if (item["weather"].is<JsonArray>() && item["weather"].size() > 0) {
                icon = item["weather"][0]["icon"] | "01d";
            }

            if (out.hourlyList.size() < 8) {
                HourlyForecastItem hourItem;
                hourItem.hourLabel = hourLabel;
                hourItem.temp = temp;
                hourItem.icon = icon;
                out.hourlyList.push_back(hourItem);
            }

            int index = -1;
            for (size_t i = 0; i < out.forecastList.size(); ++i) {
                if (out.forecastList[i].dayLabel == dayKey) {
                    index = (int)i;
                    break;
                }
            }

            if (index < 0) {
                ForecastItem forecast;
                forecast.dayLabel = dayKey;
                forecast.minTemp = temp;
                forecast.maxTemp = temp;
                forecast.icon = icon;
                out.forecastList.push_back(forecast);
            } else {
                ForecastItem& forecast = out.forecastList[(size_t)index];
                if (temp < forecast.minTemp) forecast.minTemp = temp;
                if (temp > forecast.maxTemp) forecast.maxTemp = temp;
            }
        }

        for (auto& forecast : out.forecastList) {
            forecast.dayLabel = day_label_from_iso(forecast.dayLabel);
        }

        if (out.forecastList.size() > 5) {
            out.forecastList.resize(5);
        }
    }

    bool parse_forecast_payload(const String& payload, WeatherData& out) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Logger::printf("[Weather] Forecast JSON Error: %s\n", err.c_str());
            return false;
        }
        if (!doc["list"].is<JsonArray>()) {
            Logger::println("[Weather] Forecast: Missing 'list' array");
            return false;
        }

        aggregate_forecast(doc["list"].as<JsonArray>(), out);
        Logger::println("[Weather] Forecast Parse OK!");
        return true;
    }

    bool parse_air_payload(const String& payload, WeatherData& out) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err) {
            Logger::printf("[Weather] Air JSON Error: %s\n", err.c_str());
            return false;
        }
        if (!doc["list"].is<JsonArray>() || doc["list"].size() == 0) {
            return false;
        }

        JsonObject comp = doc["list"][0]["components"];
        out.aqi = doc["list"][0]["main"]["aqi"] | 0;
        out.pm25 = comp["pm2_5"] | 0.0f;
        out.pm10 = comp["pm10"] | 0.0f;
        out.o3 = comp["o3"] | 0.0f;
        out.no2 = comp["no2"] | 0.0f;
        out.airSuccess = (out.aqi >= 1 && out.aqi <= 5);
        
        Logger::println("[Weather] Air Parse OK!");
        return out.airSuccess;
    }

    void build_ui() {
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0xBDC3C7), 0);

        header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_home, LV_EVENT_CLICKED, nullptr);
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

        weather_view = lv_obj_create(main_bg);
        lv_obj_set_size(weather_view, 320, 370);
        lv_obj_align(weather_view, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_opa(weather_view, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(weather_view, 0, 0);
        lv_obj_clear_flag(weather_view, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(weather_view, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(weather_view, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(weather_view, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_pad_bottom(weather_view, 240, 0);

        current_card = lv_obj_create(weather_view);
        lv_obj_set_size(current_card, 290, 140);
        lv_obj_align(current_card, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(current_card, lv_color_hex(0xFFFFFF), 0);
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
        lv_obj_clear_flag(cont_temp, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(cont_temp, LV_FLEX_FLOW_ROW);
        lv_obj_align(cont_temp, LV_ALIGN_CENTER, 0, -5);

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

        hourly_card = lv_obj_create(weather_view);
        lv_obj_set_size(hourly_card, 290, 110);
        lv_obj_align(hourly_card, LV_ALIGN_TOP_MID, 0, 160);
        lv_obj_set_style_bg_color(hourly_card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(hourly_card, LV_OPA_20, 0);
        lv_obj_set_style_radius(hourly_card, 18, 0);
        lv_obj_set_style_border_width(hourly_card, 0, 0);
        lv_obj_clear_flag(hourly_card, LV_OBJ_FLAG_SCROLLABLE);

        hourly_list = lv_obj_create(hourly_card);
        lv_obj_set_size(hourly_list, 270, 82);
        lv_obj_align(hourly_list, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_set_style_bg_opa(hourly_list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(hourly_list, 0, 0);
        lv_obj_add_flag(hourly_list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(hourly_list, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(hourly_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(hourly_list, 0, 0);
        lv_obj_set_style_pad_column(hourly_list, 8, 0);
        lv_obj_set_scroll_dir(hourly_list, LV_DIR_HOR);
        lv_obj_set_scrollbar_mode(hourly_list, LV_SCROLLBAR_MODE_AUTO);

        forecast_card = lv_obj_create(weather_view);
        lv_obj_set_size(forecast_card, 290, 220);
        lv_obj_align(forecast_card, LV_ALIGN_TOP_MID, 0, 280);
        lv_obj_set_style_bg_color(forecast_card, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(forecast_card, LV_OPA_20, 0);
        lv_obj_set_style_radius(forecast_card, 18, 0);
        lv_obj_set_style_border_width(forecast_card, 0, 0);
        lv_obj_clear_flag(forecast_card, LV_OBJ_FLAG_SCROLLABLE);

        daily_list = lv_obj_create(forecast_card);
        lv_obj_set_size(daily_list, 270, 192);
        lv_obj_align(daily_list, LV_ALIGN_BOTTOM_MID, 0, -4);
        lv_obj_set_style_bg_opa(daily_list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(daily_list, 0, 0);
        lv_obj_add_flag(daily_list, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(daily_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(daily_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_all(daily_list, 0, 0);
        lv_obj_set_style_pad_row(daily_list, 6, 0);
        lv_obj_set_scroll_dir(daily_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(daily_list, LV_SCROLLBAR_MODE_AUTO);

        air_view = lv_obj_create(main_bg);
        lv_obj_set_size(air_view, 320, 480);
        lv_obj_align(air_view, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_opa(air_view, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(air_view, 0, 0);
        lv_obj_add_flag(air_view, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(air_view, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(air_view, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_pad_bottom(air_view, 120, 0);

        card_aqi = lv_obj_create(air_view);
        lv_obj_set_size(card_aqi, 290, 140);
        lv_obj_align(card_aqi, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(card_aqi, lv_color_hex(0x27AE60), 0);
        lv_obj_set_style_radius(card_aqi, 20, 0);
        lv_obj_set_style_border_width(card_aqi, 0, 0);
        lv_obj_clear_flag(card_aqi, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl_city_air = lv_label_create(card_aqi);
        lv_label_set_text(lbl_city_air, "Qualite de l'air");
        lv_obj_set_style_text_color(lbl_city_air, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_city_air, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_city_air, LV_ALIGN_TOP_LEFT, 12, 10);

        lv_obj_t* lbl_city_air_name = lv_label_create(card_aqi);
        lv_label_set_text(lbl_city_air_name, CITY_NAME);
        lv_obj_set_style_text_color(lbl_city_air_name, lv_color_hex(0xE8F8EE), 0);
        lv_obj_set_style_text_font(lbl_city_air_name, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_city_air_name, LV_ALIGN_TOP_LEFT, 12, 28);

        lbl_aqi_num = lv_label_create(card_aqi);
        lv_label_set_text(lbl_aqi_num, "--");
        lv_obj_set_style_text_color(lbl_aqi_num, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_aqi_num, &lv_font_montserrat_28, 0);
        lv_obj_align(lbl_aqi_num, LV_ALIGN_CENTER, 0, 0);

        lbl_aqi_txt = lv_label_create(card_aqi);
        lv_label_set_text(lbl_aqi_txt, "En attente...");
        lv_obj_set_style_text_color(lbl_aqi_txt, lv_color_hex(0xEEEEEE), 0);
        lv_obj_set_style_text_font(lbl_aqi_txt, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl_aqi_txt, LV_ALIGN_BOTTOM_MID, 0, -10);

        lbl_pm25 = make_pollutant_row(air_view, "PM2.5 (ug/m3)", 198-30);
        lbl_pm10 = make_pollutant_row(air_view, "PM10 (ug/m3)", 252-30);
        lbl_o3 = make_pollutant_row(air_view, "Ozone (ug/m3)", 306-30);
        lbl_no2 = make_pollutant_row(air_view, "NO2 (ug/m3)", 360-30);

        lbl_status = lv_label_create(air_view);
        lv_label_set_text(lbl_status, "Chargement...");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -14);
        lv_obj_set_style_opa(lbl_status, LV_OPA_0, 0);

        footer = lv_obj_create(main_bg);
        lv_obj_set_size(footer, 304, 68);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_set_style_bg_color(footer, lv_color_hex(0x101114), 0);
        lv_obj_set_style_bg_opa(footer, LV_OPA_90, 0);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_set_style_radius(footer, 22, 0);
        lv_obj_set_style_pad_all(footer, 6, 0);
        lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

        btn_weather = lv_btn_create(footer);
        lv_obj_set_size(btn_weather, 130, 56);
        lv_obj_align(btn_weather, LV_ALIGN_LEFT_MID, 6, 0);
        lv_obj_add_event_cb(btn_weather, tab_weather_event, LV_EVENT_CLICKED, this);
        lv_obj_set_style_bg_opa(btn_weather, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(btn_weather, 18, 0);
        lv_obj_set_style_border_width(btn_weather, 0, 0);
        lv_obj_set_style_pad_all(btn_weather, 0, 0);
        lv_obj_set_flex_flow(btn_weather, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn_weather, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* icon_weather = lv_label_create(btn_weather);
        lv_label_set_text(icon_weather, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_font(icon_weather, &lv_font_montserrat_14, 0);
        weather_label = lv_label_create(btn_weather);
        lv_label_set_text(weather_label, "Meteo");
        lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_12, 0);

        btn_air = lv_btn_create(footer);
        lv_obj_set_size(btn_air, 130, 56);
        lv_obj_align(btn_air, LV_ALIGN_RIGHT_MID, -6, 0);
        lv_obj_add_event_cb(btn_air, tab_air_event, LV_EVENT_CLICKED, this);
        lv_obj_set_style_bg_opa(btn_air, LV_OPA_TRANSP, 0);
        lv_obj_set_style_radius(btn_air, 18, 0);
        lv_obj_set_style_border_width(btn_air, 0, 0);
        lv_obj_set_style_pad_all(btn_air, 0, 0);
        lv_obj_set_flex_flow(btn_air, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn_air, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t* icon_air = lv_label_create(btn_air);
        lv_label_set_text(icon_air, LV_SYMBOL_EYE_OPEN);
        lv_obj_set_style_text_font(icon_air, &lv_font_montserrat_14, 0);
        air_label = lv_label_create(btn_air);
        lv_label_set_text(air_label, "Air");
        lv_obj_set_style_text_font(air_label, &lv_font_montserrat_12, 0);

        loader = lv_spinner_create(main_bg, 1000, 60);
        lv_obj_set_size(loader, 40, 40);
        lv_obj_center(loader);
        lv_obj_clear_flag(loader, LV_OBJ_FLAG_HIDDEN);

        set_view(start_on_air ? VIEW_AIR : VIEW_WEATHER);
        refresh_requested = true;
    }

public:
    explicit WeatherApp(bool startOnAir = false) : start_on_air(startOnAir), active_view(startOnAir ? VIEW_AIR : VIEW_WEATHER) {}

    void start(lv_obj_t* parent) override {
        main_bg = parent;
        refresh_requested = false;
        has_new_data = false;
        sharedData = WeatherData{};
        build_ui();
    }

    void update() override {
        if (!has_new_data) return;

        if (!mutex_try_enter(&weatherMutex, nullptr)) return;
        WeatherData data = sharedData;
        has_new_data = false;
        mutex_exit(&weatherMutex);

        if (data.weatherSuccess || data.airSuccess) {
            if (data.weatherSuccess) {
                render_weather_ui(data);
            }
            if (data.airSuccess) {
                render_air_ui(data);
            }
        } else {
            NetworkErrorHandler::showIfError("Meteo", "Aucune donnee disponible");
            lv_label_set_text(lbl_desc, "Erreur reseau ou JSON");
            lv_label_set_text(lbl_status, "Erreur reseau");
        }

        lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);
    }

    void update1() override {
        if (!refresh_requested) return;
        watchdog_update();


        if (!LTE::isReadyForData()) {
            WeatherData fail;
            fail.weatherSuccess = false;
            fail.airSuccess = false;
            NetworkErrorHandler::showIfError("Meteo", "Aucune connexion reseau disponible");
            unsigned long t0 = millis();
            while (millis() - t0 < 30) {
                watchdog_update();
                if (mutex_try_enter(&weatherMutex, nullptr)) {
                    sharedData = std::move(fail);
                    has_new_data = true;
                    mutex_exit(&weatherMutex);
                    break;
                }
                sleep_ms(1);
            }
            refresh_requested = false;
            return;
        }

        WeatherData newData;
        newData.hourlyList.reserve(8);
        newData.forecastList.reserve(5);
        String currentUrl = String("http://api.openweathermap.org/data/2.5/weather?q=") + CITY_NAME + "," + COUNTRY_CODE + "&appid=" + API_KEY + "&units=metric&lang=fr";
        // Correction ici: je repasse en cnt=9 pour être sûr de respecter la taille mémoire.
        String forecastUrl = String("http://api.openweathermap.org/data/2.5/forecast?cnt=9&q=") + CITY_NAME + "," + COUNTRY_CODE + "&appid=" + API_KEY + "&units=metric&lang=fr";
        String airUrl = String("http://api.openweathermap.org/data/2.5/air_pollution?lat=") + AQ_LAT + "&lon=" + AQ_LON + "&appid=" + API_KEY;

        String payload;
        if (http_get_payload(currentUrl, payload)) {
            parse_weather_payload(payload, newData);
        }

        payload = "";
        if (http_get_payload(forecastUrl, payload)) {
            parse_forecast_payload(payload, newData);
        }

        payload = "";
        if (http_get_payload(airUrl, payload)) {
            parse_air_payload(payload, newData);
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
            sleep_ms(1);
        }
        if (!posted) {
            has_new_data = false;
        }
        refresh_requested = false;
    }
};

#endif