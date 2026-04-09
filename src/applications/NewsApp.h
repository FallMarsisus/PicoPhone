#pragma once

#include "App.h"
#include "AppManager.h"
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <pico/mutex.h>
#include <hardware/watchdog.h>
#include "../system/LTE.h"
#include "../system/NetworkErrorHandler.h"

// --- NewsAPI.org (free tier, requires a free API key at newsapi.org) ---
// Replace with your own key from https://newsapi.org/register
#define NEWS_API_KEY   "3028730fa95248a79da671f75618581f"
#define NEWS_API_URL   "http://newsapi.org/v2/top-headlines?sources=le-monde&apiKey=" NEWS_API_KEY
#define NEWS_MAX_ITEMS 5

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_18);

struct NewsItem {
    String title;
    String source;
};

struct NewsData {
    std::vector<NewsItem> items;
    bool success;
    String errorMsg;
};

auto_init_mutex(newsMutex);

class NewsApp : public App {
private:
    lv_obj_t* main_bg    = nullptr;
    lv_obj_t* list_cont  = nullptr;
    lv_obj_t* lbl_status = nullptr;
    lv_obj_t* loader     = nullptr;

    bool refresh_requested = false;
    bool has_new_data      = false;
    NewsData shared_data{};

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void refresh_event(lv_event_t* e) {
        NewsApp* app = (NewsApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->refresh_requested = true;
        if (app->loader) lv_obj_clear_flag(app->loader, LV_OBJ_FLAG_HIDDEN);
        if (app->lbl_status) lv_label_set_text(app->lbl_status, "Chargement...");
        if (app->list_cont) lv_obj_clean(app->list_cont);
    }

    void populate_list(const NewsData& data) {
        lv_obj_clean(list_cont);

        if (!data.success || data.items.empty()) {
            lv_obj_t* lbl = lv_label_create(list_cont);
            const char* msg = data.errorMsg.length() > 0
                              ? data.errorMsg.c_str()
                              : "Aucune actualite disponible";
            lv_label_set_text(lbl, msg);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x636366), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
            lv_obj_center(lbl);
            return;
        }

        for (int i = 0; i < (int)data.items.size(); i++) {
            lv_obj_t* card = lv_obj_create(list_cont);
            lv_obj_set_size(card, 280, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
            lv_obj_set_style_radius(card, 12, 0);
            lv_obj_set_style_border_width(card, 0, 0);
            lv_obj_set_style_pad_all(card, 10, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

            // Source badge
            lv_obj_t* lbl_src = lv_label_create(card);
            lv_label_set_text(lbl_src, data.items[i].source.c_str());
            lv_obj_set_style_text_color(lbl_src, lv_color_hex(0x007AFF), 0);
            lv_obj_set_style_text_font(lbl_src, &lv_font_montserrat_12, 0);
            lv_obj_align(lbl_src, LV_ALIGN_TOP_LEFT, 0, 0);

            // Title
            lv_obj_t* lbl_title = lv_label_create(card);
            lv_label_set_text(lbl_title, data.items[i].title.c_str());
            lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl_title, 260);
            lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
            lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
            lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 18);
        }
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
        lv_label_set_text(title, "Actualites");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
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

        // --- Scrollable news list ---
        list_cont = lv_obj_create(main_bg);
        lv_obj_set_size(list_cont, 300, 415);
        lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 55);
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_style_pad_row(list_cont, 8, 0);
        lv_obj_set_style_pad_all(list_cont, 5, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list_cont, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);

        // --- Status ---
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

        if (!mutex_try_enter(&newsMutex, nullptr)) return;
        NewsData data = shared_data;
        has_new_data = false;
        mutex_exit(&newsMutex);

        lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);

        if (data.success) {
            populate_list(data);
            char buf[32];
            snprintf(buf, sizeof(buf), "%d articles", (int)data.items.size());
            lv_label_set_text(lbl_status, buf);
        } else {
            const char* msg = data.errorMsg.length() > 0
                              ? data.errorMsg.c_str()
                              : "Erreur reseau";
            // Only show network error dialog for real network failures
            if (data.errorMsg.length() == 0) {
                NetworkErrorHandler::showIfError("Actualites", msg);
            }
            lv_label_set_text(lbl_status, msg);
        }
    }

    // --- CORE 1: network fetch ---
    void update1() override {
        if (!refresh_requested) return;
        watchdog_update();

        // Guard against unconfigured API key
        if (strcmp(NEWS_API_KEY, "YOUR_NEWS_API_KEY") == 0) {
            NewsData fail{};
            fail.success = false;
            fail.errorMsg = "Cle API manquante";
            unsigned long t0 = millis();
            while (millis() - t0 < 30) {
                watchdog_update();
                if (mutex_try_enter(&newsMutex, nullptr)) {
                    shared_data = std::move(fail);
                    has_new_data = true;
                    mutex_exit(&newsMutex);
                    break;
                }
                sleep_ms(1);
            }
            refresh_requested = false;
            return;
        }

        bool use_wifi = (WiFi.status() == WL_CONNECTED);
        bool use_lte  = (!use_wifi && LTE::isReadyForData());

        if (!use_wifi && !use_lte) {
            NewsData fail{};
            fail.success = false;
            NetworkErrorHandler::showIfError("Actualites", "Aucune connexion reseau");
            unsigned long t0 = millis();
            while (millis() - t0 < 30) {
                watchdog_update();
                if (mutex_try_enter(&newsMutex, nullptr)) {
                    shared_data = std::move(fail);
                    has_new_data = true;
                    mutex_exit(&newsMutex);
                    break;
                }
                sleep_ms(1);
            }
            refresh_requested = false;
            return;
        }

        NewsData newData{};
        newData.success = false;
        String url = NEWS_API_URL;
        String payload;

        if (use_wifi) {
            HTTPClient http;
            http.setTimeout(10000);
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
            filter["articles"][0]["title"] = true;
            filter["articles"][0]["source"]["name"] = true;

            JsonDocument doc;
            auto err = deserializeJson(doc, payload,
                                       DeserializationOption::Filter(filter));
            if (!err) {
                JsonArray arts = doc["articles"].as<JsonArray>();
                for (JsonObject art : arts) {
                    watchdog_update();
                    NewsItem item;
                    item.title  = art["title"].as<String>();
                    item.source = art["source"]["name"].as<String>();
                    if (item.title.length() > 0) {
                        newData.items.push_back(item);
                    }
                    if ((int)newData.items.size() >= NEWS_MAX_ITEMS) break;
                }
                newData.success = !newData.items.empty();
            }
        }

        unsigned long t0 = millis();
        while (millis() - t0 < 50) {
            watchdog_update();
            if (mutex_try_enter(&newsMutex, nullptr)) {
                shared_data = std::move(newData);
                has_new_data = true;
                mutex_exit(&newsMutex);
                break;
            }
            sleep_ms(1);
        }
        refresh_requested = false;
    }
};
