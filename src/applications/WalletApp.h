#ifndef WALLET_APP_H
#define WALLET_APP_H

#include "App.h"
#include "AppManager.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <vector>

class WalletApp : public App {
private:
    struct WalletCard {
        String name;
        String number;
        bool useQr;
    };

    static constexpr const char* WALLET_FILE = "/config/wallet.json";

    std::vector<WalletCard> cards;
    lv_obj_t* root = nullptr;
    lv_obj_t* add_overlay = nullptr;
    lv_obj_t* detail_code_host = nullptr;
    lv_obj_t* detail_mode_lbl = nullptr;
    lv_obj_t* kb = nullptr;
    lv_obj_t* ta_name = nullptr;
    lv_obj_t* ta_number = nullptr;
    lv_obj_t* active_ta = nullptr;
    int current_index = -1;

    static void go_home_cb(lv_event_t* e) {
        (void)e;
        AppManager::switchTo(APP_HOME);
    }

    static void open_add_cb(lv_event_t* e) {
        WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
        if (self) self->openAddOverlay();
    }

    static void card_open_cb(lv_event_t* e) {
        WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
        if (!self) return;
        int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
        if (idx < 0 || idx >= (int)self->cards.size()) return;
        self->openDetail(idx);
    }

    static void delete_card_cb(lv_event_t* e) {
        WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
        if (!self || self->current_index < 0 || self->current_index >= (int)self->cards.size()) return;
        self->cards.erase(self->cards.begin() + self->current_index);
        self->saveCards();
        self->renderList();
    }

    static void back_to_list_cb(lv_event_t* e) {
        WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
        if (self) self->renderList();
    }

    static void toggle_mode_cb(lv_event_t* e) {
        WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
        if (!self || self->current_index < 0 || self->current_index >= (int)self->cards.size()) return;
        self->cards[self->current_index].useQr = !self->cards[self->current_index].useQr;
        self->saveCards();
        self->renderDetailCode();
    }

    static void ta_focus_cb(lv_event_t* e) {
        WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
        if (!self || !self->kb) return;
        self->active_ta = lv_event_get_target(e);
        lv_keyboard_set_textarea(self->kb, self->active_ta);
    }

    static void kb_event_cb(lv_event_t* e) {
        WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
        if (!self) return;
        lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_CANCEL) {
            self->closeAddOverlay();
        }
    }

    static void save_new_card_cb(lv_event_t* e) {
        WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
        if (!self || !self->ta_name || !self->ta_number) return;

        const char* name = lv_textarea_get_text(self->ta_name);
        const char* number = lv_textarea_get_text(self->ta_number);
        if (!name || !number || strlen(name) == 0 || strlen(number) == 0) {
            return;
        }

        WalletCard c;
        c.name = name;
        c.number = number;
        c.useQr = true;
        self->cards.push_back(c);
        self->saveCards();
        self->closeAddOverlay();
        self->renderList();
    }

    void ensureFs() {
        static bool fs_ok = false;
        if (!fs_ok) {
            fs_ok = LittleFS.begin();
        }
        LittleFS.mkdir("/config");
    }

    void loadCards() {
        ensureFs();
        cards.clear();
        if (!LittleFS.exists(WALLET_FILE)) return;

        File f = LittleFS.open(WALLET_FILE, "r");
        if (!f) return;

        JsonDocument doc;
        if (deserializeJson(doc, f)) {
            f.close();
            return;
        }
        f.close();

        JsonArray arr = doc["cards"].as<JsonArray>();
        for (const auto& it : arr) {
            WalletCard c;
            c.name = it["name"] | "Carte";
            c.number = it["number"] | "";
            c.useQr = (it["mode"] | "qr") == String("qr");
            if (c.number.length() > 0) cards.push_back(c);
        }
    }

    void saveCards() {
        ensureFs();
        JsonDocument doc;
        JsonArray arr = doc["cards"].to<JsonArray>();
        for (const auto& c : cards) {
            JsonObject o = arr.add<JsonObject>();
            o["name"] = c.name;
            o["number"] = c.number;
            o["mode"] = c.useQr ? "qr" : "barcode";
        }

        File f = LittleFS.open(WALLET_FILE, "w");
        if (!f) return;
        serializeJson(doc, f);
        f.close();
    }

    void renderPseudoBarcode(lv_obj_t* parent, const String& data) {
        lv_obj_set_style_bg_color(parent, lv_color_white(), 0);
        lv_obj_set_style_border_width(parent, 0, 0);
        lv_obj_set_style_radius(parent, 6, 0);

        const int w = lv_obj_get_width(parent);
        const int h = lv_obj_get_height(parent);
        const int left = 10;
        const int right = w - 10;
        const int top = 8;
        const int bottom = h - 28;

        uint32_t seed = 2166136261u;
        for (size_t i = 0; i < data.length(); i++) {
            seed ^= (uint8_t)data[i];
            seed *= 16777619u;
        }

        int x = left;
        while (x < right) {
            seed = seed * 1664525u + 1013904223u;
            int bar_w = ((seed >> 8) & 0x03) + 1;
            if (x + bar_w > right) break;

            if ((seed & 0x01u) == 0) {
                lv_obj_t* bar = lv_obj_create(parent);
                lv_obj_set_size(bar, bar_w, bottom - top);
                lv_obj_set_pos(bar, x, top);
                lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
                lv_obj_set_style_border_width(bar, 0, 0);
                lv_obj_set_style_radius(bar, 0, 0);
                lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
            }
            x += bar_w;
        }

        lv_obj_t* lbl = lv_label_create(parent);
        lv_label_set_text(lbl, data.c_str());
        lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -5);
    }

    void renderDetailCode() {
        if (!detail_code_host || current_index < 0 || current_index >= (int)cards.size()) return;
        lv_obj_clean(detail_code_host);

        const WalletCard& c = cards[current_index];
        if (detail_mode_lbl) {
            lv_label_set_text(detail_mode_lbl, c.useQr ? "Mode: QR" : "Mode: Code-barres");
        }

#if LV_USE_QRCODE
        if (c.useQr) {
            lv_obj_t* qr = lv_qrcode_create(detail_code_host, 220, lv_color_black(), lv_color_white());
            lv_obj_center(qr);
            lv_qrcode_update(qr, c.number.c_str(), c.number.length());
            lv_obj_set_style_border_color(qr, lv_color_white(), 0);
            lv_obj_set_style_border_width(qr, 4, 0);
            return;
        }
#endif

        renderPseudoBarcode(detail_code_host, c.number);
    }

    void renderDetailShell(const WalletCard& c) {
        lv_obj_clean(root);
        lv_obj_set_style_bg_color(root, lv_color_hex(0x0A0E1A), 0);

        lv_obj_t* header = lv_obj_create(root);
        lv_obj_set_size(header, 320, 56);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x121A2E), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);

        lv_obj_t* back = lv_btn_create(header);
        lv_obj_set_size(back, 44, 44);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_0, 0);
        lv_obj_add_event_cb(back, back_to_list_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* back_lbl = lv_label_create(back);
        lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
        lv_obj_center(back_lbl);

        lv_obj_t* del = lv_btn_create(header);
        lv_obj_set_size(del, 44, 44);
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(del, LV_OPA_0, 0);
        lv_obj_add_event_cb(del, delete_card_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* del_lbl = lv_label_create(del);
        lv_label_set_text(del_lbl, LV_SYMBOL_TRASH);
        lv_obj_set_style_text_color(del_lbl, lv_color_hex(0xFF6B6B), 0);
        lv_obj_center(del_lbl);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, c.name.c_str());
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);

        lv_obj_t* number = lv_label_create(root);
        lv_label_set_text(number, c.number.c_str());
        lv_obj_set_style_text_color(number, lv_color_hex(0xAEB7CC), 0);
        lv_obj_align(number, LV_ALIGN_TOP_MID, 0, 72);

        detail_mode_lbl = lv_label_create(root);
        lv_label_set_text(detail_mode_lbl, c.useQr ? "Mode: QR" : "Mode: Code-barres");
        lv_obj_set_style_text_color(detail_mode_lbl, lv_color_hex(0x6EC6FF), 0);
        lv_obj_align(detail_mode_lbl, LV_ALIGN_TOP_MID, 0, 94);

        lv_obj_t* toggle = lv_btn_create(root);
        lv_obj_set_size(toggle, 220, 38);
        lv_obj_align(toggle, LV_ALIGN_TOP_MID, 0, 118);
        lv_obj_set_style_bg_color(toggle, lv_color_hex(0x1E3A66), 0);
        lv_obj_add_event_cb(toggle, toggle_mode_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* t_lbl = lv_label_create(toggle);
        lv_label_set_text(t_lbl, "Basculer QR / Code-barres");
        lv_obj_center(t_lbl);

        detail_code_host = lv_obj_create(root);
        lv_obj_set_size(detail_code_host, 280, 250);
        lv_obj_align(detail_code_host, LV_ALIGN_BOTTOM_MID, 0, -18);
        lv_obj_set_style_bg_color(detail_code_host, lv_color_hex(0xEAEFFB), 0);
        lv_obj_set_style_border_width(detail_code_host, 0, 0);
        lv_obj_set_style_radius(detail_code_host, 10, 0);
        lv_obj_clear_flag(detail_code_host, LV_OBJ_FLAG_SCROLLABLE);

        renderDetailCode();
    }

    void openDetail(int index) {
        current_index = index;
        renderDetailShell(cards[index]);
    }

    void closeAddOverlay() {
        if (add_overlay) {
            lv_obj_del(add_overlay);
            add_overlay = nullptr;
            kb = nullptr;
            ta_name = nullptr;
            ta_number = nullptr;
            active_ta = nullptr;
        }
    }

    void openAddOverlay() {
        if (add_overlay) return;

        add_overlay = lv_obj_create(root);
        lv_obj_set_size(add_overlay, 320, 480);
        lv_obj_align(add_overlay, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(add_overlay, lv_color_hex(0x111827), 0);
        lv_obj_set_style_bg_opa(add_overlay, LV_OPA_90, 0);
        lv_obj_set_style_border_width(add_overlay, 0, 0);
        lv_obj_set_style_radius(add_overlay, 0, 0);

        lv_obj_t* title = lv_label_create(add_overlay);
        lv_label_set_text(title, "Nouvelle carte");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

        ta_name = lv_textarea_create(add_overlay);
        lv_obj_set_size(ta_name, 280, 38);
        lv_obj_align(ta_name, LV_ALIGN_TOP_MID, 0, 40);
        lv_textarea_set_one_line(ta_name, true);
        lv_textarea_set_placeholder_text(ta_name, "Nom (ex: Carte Fid)");
        lv_obj_add_event_cb(ta_name, ta_focus_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ta_name, ta_focus_cb, LV_EVENT_CLICKED, this);

        ta_number = lv_textarea_create(add_overlay);
        lv_obj_set_size(ta_number, 280, 38);
        lv_obj_align(ta_number, LV_ALIGN_TOP_MID, 0, 84);
        lv_textarea_set_one_line(ta_number, true);
        lv_textarea_set_placeholder_text(ta_number, "Numero carte");
        lv_obj_add_event_cb(ta_number, ta_focus_cb, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ta_number, ta_focus_cb, LV_EVENT_CLICKED, this);

        lv_obj_t* save = lv_btn_create(add_overlay);
        lv_obj_set_size(save, 130, 36);
        lv_obj_align(save, LV_ALIGN_TOP_LEFT, 20, 130);
        lv_obj_set_style_bg_color(save, lv_color_hex(0x1E88E5), 0);
        lv_obj_add_event_cb(save, save_new_card_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* save_lbl = lv_label_create(save);
        lv_label_set_text(save_lbl, "Ajouter");
        lv_obj_center(save_lbl);

        lv_obj_t* cancel = lv_btn_create(add_overlay);
        lv_obj_set_size(cancel, 130, 36);
        lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -20, 130);
        lv_obj_set_style_bg_color(cancel, lv_color_hex(0x6B7280), 0);
        lv_obj_add_event_cb(cancel, [](lv_event_t* e) {
            WalletApp* self = (WalletApp*)lv_event_get_user_data(e);
            if (self) self->closeAddOverlay();
        }, LV_EVENT_CLICKED, this);
        lv_obj_t* cancel_lbl = lv_label_create(cancel);
        lv_label_set_text(cancel_lbl, "Annuler");
        lv_obj_center(cancel_lbl);

        kb = lv_keyboard_create(add_overlay);
        lv_obj_set_size(kb, 320, 250);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, this);

        active_ta = ta_name;
        lv_keyboard_set_textarea(kb, ta_name);
    }

    void renderList() {
        current_index = -1;
        detail_code_host = nullptr;
        detail_mode_lbl = nullptr;
        closeAddOverlay();

        lv_obj_clean(root);
        lv_obj_set_style_bg_color(root, lv_color_hex(0x0A0E1A), 0);

        lv_obj_t* header = lv_obj_create(root);
        lv_obj_set_size(header, 320, 56);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x121A2E), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);

        lv_obj_t* back = lv_btn_create(header);
        lv_obj_set_size(back, 44, 44);
        lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(back, LV_OPA_0, 0);
        lv_obj_add_event_cb(back, go_home_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* back_lbl = lv_label_create(back);
        lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
        lv_obj_center(back_lbl);

        lv_obj_t* add = lv_btn_create(header);
        lv_obj_set_size(add, 44, 44);
        lv_obj_align(add, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(add, LV_OPA_0, 0);
        lv_obj_add_event_cb(add, open_add_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* add_lbl = lv_label_create(add);
        lv_label_set_text(add_lbl, LV_SYMBOL_PLUS);
        lv_obj_set_style_text_color(add_lbl, lv_color_hex(0x7ED957), 0);
        lv_obj_center(add_lbl);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Wallet");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);

        lv_obj_t* list = lv_list_create(root);
        lv_obj_set_size(list, 308, 410);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x0A0E1A), 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_style_pad_row(list, 8, 0);

        if (cards.empty()) {
            lv_obj_t* empty = lv_label_create(list);
            lv_label_set_text(empty, "Aucune carte\nAjoute une carte avec +");
            lv_obj_set_style_text_color(empty, lv_color_hex(0x8A94A8), 0);
            lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
            return;
        }

        for (int i = 0; i < (int)cards.size(); i++) {
            lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_SAVE, cards[i].name.c_str());
            lv_obj_set_user_data(btn, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn, card_open_cb, LV_EVENT_CLICKED, this);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x18233A), 0);
            lv_obj_set_style_text_color(btn, lv_color_white(), 0);
            lv_obj_set_style_radius(btn, 10, 0);
        }
    }

public:
    void start(lv_obj_t* parent) override {
        root = parent;
        loadCards();
        renderList();
    }

    void update() override {}

    void stop() override {
        closeAddOverlay();
        detail_code_host = nullptr;
        detail_mode_lbl = nullptr;
        root = nullptr;
    }
};

#endif
