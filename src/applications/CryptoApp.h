#pragma once

#include "App.h"
#include "AppManager.h"
#include <lvgl.h>
#include <ArduinoJson.h>
#include <pico/mutex.h>
#include <hardware/watchdog.h>
#include "../system/LTE.h"
#include "../system/NetworkErrorHandler.h"

// --- Binance public API (no key required, IoT friendly) ---
// On demande spécifiquement les paires en Euro (BTCEUR, ETHEUR, SOLEUR)
#define CRYPTO_API_URL "https://api.binance.com/api/v3/ticker/24hr?symbols=%5B%22BTCEUR%22,%22ETHEUR%22,%22SOLEUR%22%5D"

LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(lv_font_montserrat_28);

struct CryptoEntry {
    const char* id;
    const char* name;
    const char* symbol;
    uint32_t    color;
    float       price;
    float       change24h;
    bool        valid;
};

struct CryptoData {
    CryptoEntry coins[3];
    bool success;
    unsigned long fetched_at;
};

auto_init_mutex(cryptoMutex);

class CryptoApp : public App {
private:
    lv_obj_t* main_bg   = nullptr;
    lv_obj_t* lbl_status= nullptr;
    lv_obj_t* loader    = nullptr;

    // One card per coin
    struct CoinCard {
        lv_obj_t* card    = nullptr;
        lv_obj_t* lbl_price  = nullptr;
        lv_obj_t* lbl_change = nullptr;
    } cards[3];

    bool refresh_requested = false;
    bool has_new_data      = false;
    CryptoData shared_data{};

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void refresh_event(lv_event_t* e) {
        CryptoApp* app = (CryptoApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->refresh_requested = true;
        if (app->loader) lv_obj_clear_flag(app->loader, LV_OBJ_FLAG_HIDDEN);
        if (app->lbl_status) lv_label_set_text(app->lbl_status, "Actualisation...");
    }

    void build_coin_card(int idx, const char* name, const char* symbol, uint32_t color,
                          lv_coord_t y_offset) {
        lv_obj_t* card = lv_obj_create(main_bg);
        lv_obj_set_size(card, 290, 110);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y_offset);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_radius(card, 18, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        cards[idx].card = card;

        // Colored accent band on the left
        lv_obj_t* band = lv_obj_create(card);
        lv_obj_set_size(band, 6, 70);
        lv_obj_align(band, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(band, lv_color_hex(color), 0);
        lv_obj_set_style_radius(band, 3, 0);
        lv_obj_set_style_border_width(band, 0, 0);

        // Name label
        lv_obj_t* lbl_name = lv_label_create(card);
        lv_label_set_text(lbl_name, name);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(0xAEAEB2), 0);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 20, 8);

        // Symbol label
        lv_obj_t* lbl_sym = lv_label_create(card);
        lv_label_set_text(lbl_sym, symbol);
        lv_obj_set_style_text_color(lbl_sym, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(lbl_sym, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_sym, LV_ALIGN_TOP_LEFT, 20, 28);

        // Price label (big)
        cards[idx].lbl_price = lv_label_create(card);
        lv_label_set_text(cards[idx].lbl_price, "---");
        lv_obj_set_style_text_color(cards[idx].lbl_price, lv_color_white(), 0);
        lv_obj_set_style_text_font(cards[idx].lbl_price, &lv_font_montserrat_28, 0);
        lv_obj_align(cards[idx].lbl_price, LV_ALIGN_LEFT_MID, 20, 10);

        // 24h change label
        cards[idx].lbl_change = lv_label_create(card);
        lv_label_set_text(cards[idx].lbl_change, "---");
        lv_obj_set_style_text_color(cards[idx].lbl_change, lv_color_hex(0xAEAEB2), 0);
        lv_obj_set_style_text_font(cards[idx].lbl_change, &lv_font_montserrat_14, 0);
        lv_obj_align(cards[idx].lbl_change, LV_ALIGN_BOTTOM_LEFT, 20, -8);
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
        lv_label_set_text(title, "Crypto");
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

        // --- Status label ---
        lbl_status = lv_label_create(main_bg);
        lv_label_set_text(lbl_status, "En attente...");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x636366), 0);
        lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -12);

        // --- Coin cards ---
        build_coin_card(0, "Bitcoin",  "BTC", 0xF7931A, 65);
        build_coin_card(1, "Ethereum", "ETH", 0x627EEA, 185);
        build_coin_card(2, "Solana",   "SOL", 0x9945FF, 305);

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

        if (!mutex_try_enter(&cryptoMutex, nullptr)) return;
        CryptoData data = shared_data;
        has_new_data = false;
        mutex_exit(&cryptoMutex);

        if (data.success) {
            for (int i = 0; i < 3; i++) {
                if (!data.coins[i].valid) continue;

                char price_buf[32];
                float p = data.coins[i].price;
                if (p >= 1000.0f) {
                    snprintf(price_buf, sizeof(price_buf), "%.0f EUR", p);
                } else {
                    snprintf(price_buf, sizeof(price_buf), "%.2f EUR", p);
                }
                lv_label_set_text(cards[i].lbl_price, price_buf);

                float ch = data.coins[i].change24h;
                char chg_buf[24];
                snprintf(chg_buf, sizeof(chg_buf), "%+.2f%%", ch);
                lv_label_set_text(cards[i].lbl_change, chg_buf);
                lv_obj_set_style_text_color(cards[i].lbl_change,
                    ch >= 0 ? lv_color_hex(0x34C759) : lv_color_hex(0xFF3B30), 0);
            }
            lv_label_set_text(lbl_status, "Mis a jour");
        } else {
            NetworkErrorHandler::showIfError("Crypto", "Impossible de charger les cours");
            lv_label_set_text(lbl_status, "Erreur reseau");
        }

        lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);
    }

    // --- CORE 1: network fetch ---
    // --- CORE 1: network fetch ---
    void update1() override {
        if (!refresh_requested) return;
        watchdog_update();

        if (!LTE::isReadyForData()) {
            CryptoData fail{};
            fail.success = false;
            NetworkErrorHandler::showIfError("Crypto", "Aucune connexion reseau");
            unsigned long t0 = millis();
            while (millis() - t0 < 30) {
                watchdog_update();
                if (mutex_try_enter(&cryptoMutex, nullptr)) {
                    shared_data = fail;
                    has_new_data = true;
                    mutex_exit(&cryptoMutex);
                    break;
                }
                sleep_ms(1);
            }
            refresh_requested = false;
            return;
        }

        CryptoData newData{};
        newData.success = false;
        newData.coins[0] = {"bitcoin",  "Bitcoin",  "BTC", 0xF7931A, 0, 0, false};
        newData.coins[1] = {"ethereum", "Ethereum", "ETH", 0x627EEA, 0, 0, false};
        newData.coins[2] = {"solana",   "Solana",   "SOL", 0x9945FF, 0, 0, false};

        String url = CRYPTO_API_URL;
        String payload = LTE::httpGetBlocking(url);
        watchdog_update();

        if (payload.length() > 0) {
            JsonDocument doc; // Dynamique par défaut sur ArduinoJson v7
            DeserializationError err = deserializeJson(doc, payload);
            
            if (err) {
                // Si la mémoire plante ou que le JSON est malformé, on le saura !
                Logger::printf("[Crypto] Erreur JSON: %s\n", err.c_str());
            } 
            else if (doc.is<JsonArray>()) {
                JsonArray arr = doc.as<JsonArray>();
                for (JsonObject obj : arr) {
                    // On récupère les valeurs sous forme de texte brut (const char*)
                    const char* symbol = obj["symbol"];
                    const char* price_str = obj["lastPrice"];
                    const char* change_str = obj["priceChangePercent"];

                    if (symbol && price_str && change_str) {
                        // On force la conversion du texte vers le format Float
                        float price = atof(price_str);
                        float change = atof(change_str);
                        
                        Logger::printf("[Crypto] Lu avec succes -> %s : %.2f EUR (%.2f%%)\n", symbol, price, change);

                        // On assigne les données selon le symbole reçu
                        if (strcmp(symbol, "BTCEUR") == 0) {
                            newData.coins[0].price = price;
                            newData.coins[0].change24h = change;
                            newData.coins[0].valid = true;
                        } else if (strcmp(symbol, "ETHEUR") == 0) {
                            newData.coins[1].price = price;
                            newData.coins[1].change24h = change;
                            newData.coins[1].valid = true;
                        } else if (strcmp(symbol, "SOLEUR") == 0) {
                            newData.coins[2].price = price;
                            newData.coins[2].change24h = change;
                            newData.coins[2].valid = true;
                        }
                    }
                }
                newData.success = true;
            } else {
                Logger::println("[Crypto] Erreur: Le JSON recu n'est pas un tableau valide !");
            }
        } else {
            Logger::println("[Crypto] Payload vide recu du modem.");
        }

        unsigned long t0 = millis();
        while (millis() - t0 < 50) {
            watchdog_update();
            if (mutex_try_enter(&cryptoMutex, nullptr)) {
                shared_data = newData;
                has_new_data = true;
                mutex_exit(&cryptoMutex);
                break;
            }
            sleep_ms(1);
        }
        refresh_requested = false;
    }
};