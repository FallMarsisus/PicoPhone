#ifndef CHATBOT_APP_H
#define CHATBOT_APP_H

#include "App.h"
#include <ArduinoJson.h> 
#include <lvgl.h>
#include <vector>
#include <pico/mutex.h>
#include <hardware/watchdog.h>
#include "../system/NetworkErrorHandler.h"
#include "../system/LTE.h"
#include "../AppManager.h"

// --- CONFIGURATION GEMINI ---
#define GEMINI_API_KEY "AIzaSyCuk6e8r-RmuoeJb77K8_Ja7lfqfXXV1XQ" // Remplace par ta vraie clé en prod
#define GEMINI_MODEL "gemini-3-flash-preview"

// Structure d'un message pour l'historique
struct ChatMessage {
    String role; // "user" ou "model"
    String text;
};

// Données partagées entre le Core 0 (UI) et le Core 1 (Réseau)
struct ChatState {
    String pendingPrompt = "";
    bool requestPending = false;
    
    String lastResponse = "";
    bool responseReady = false;
    bool success = false;
};

auto_init_mutex(chatMutex);

class ChatbotApp : public App {
private:
    static constexpr size_t MAX_HISTORY_MESSAGES = 12;
    static constexpr uint32_t REQUEST_TIMEOUT_MS = 15000;
    static constexpr uint32_t CHAT_BG = 0xFFFFFF;
    static constexpr uint32_t CHAT_HEADER_BG = 0xE9E9E9;
    static constexpr uint32_t CHAT_IN_BG = 0x3A3A3C;
    static constexpr uint32_t CHAT_OUT_BG = 0x34C759;
    static constexpr uint32_t CHAT_TEXT = 0xFFFFFF;
    static constexpr uint32_t CHAT_MUTED = 0xAEAEB2;

    lv_obj_t* main_bg;
    lv_obj_t* header;
    lv_obj_t* chat_view;
    lv_obj_t* chat_list;
    lv_obj_t* ta_input;
    lv_obj_t* kb;
    lv_obj_t* loader;
    lv_obj_t* footer;

    std::vector<ChatMessage> history;
    ChatState sharedData;

    struct RichSegment {
        String text;
        bool bold = false;
        bool italic = false;
    };

    void trimHistoryLocked() {
        if (history.size() <= MAX_HISTORY_MESSAGES) {
            return;
        }
        history.erase(history.begin(), history.begin() + (history.size() - MAX_HISTORY_MESSAGES));
    }

    static String normalize_ai_text(const String& input) {
        String out;
        out.reserve(input.length());
        for (size_t i = 0; i < input.length(); i++) {
            char ch = input[i];
            if (ch == '\r') continue;
            out += ch;
        }
        return out;
    }

    std::vector<RichSegment> parse_rich_segments(const String& text) {
        std::vector<RichSegment> segments;
        String buffer;
        bool bold = false;
        bool italic = false;

        auto flush = [&]() {
            if (buffer.length() == 0) return;
            segments.push_back({buffer, bold, italic});
            buffer = "";
        };

        for (size_t i = 0; i < text.length(); i++) {
            char ch = text[i];
            if (ch == '*' && (i + 1) < text.length() && text[i + 1] == '*') {
                flush();
                bold = !bold;
                i++;
                continue;
            }
            if (ch == '*') {
                flush();
                italic = !italic;
                continue;
            }
            buffer += ch;
        }

        flush();
        return segments;
    }

    void style_span(lv_span_t* span, bool bold, bool italic, bool user_message) {
        if (!span) return;

        lv_style_set_text_color(&span->style, user_message ? lv_color_white() : lv_color_white());
        lv_style_set_text_opa(&span->style, LV_OPA_COVER);

        if (bold) {
            lv_style_set_outline_width(&span->style, 1);
            lv_style_set_outline_color(&span->style, user_message ? lv_color_white() : lv_color_white());
            lv_style_set_outline_opa(&span->style, LV_OPA_COVER);
            lv_style_set_shadow_width(&span->style, 1);
            lv_style_set_shadow_ofs_x(&span->style, 0);
            lv_style_set_shadow_ofs_y(&span->style, 0);
            lv_style_set_shadow_color(&span->style, user_message ? lv_color_white() : lv_color_white());
            lv_style_set_shadow_opa(&span->style, LV_OPA_COVER);
            lv_style_set_text_letter_space(&span->style, -1);
        }

        if (italic) {
            lv_style_set_text_decor(&span->style, LV_TEXT_DECOR_UNDERLINE);
        }
    }

    void add_ai_message_ui(const String& raw_text) {
        String text = normalize_ai_text(raw_text);

        lv_obj_t* bubble = lv_obj_create(chat_list);
        lv_obj_set_width(bubble, lv_pct(85));
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(bubble, 10, 0);
        lv_obj_set_style_radius(bubble, 15, 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        lv_obj_set_style_bg_color(bubble, lv_color_hex(CHAT_IN_BG), 0);
        lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t* spans = lv_spangroup_create(bubble);
        lv_obj_set_width(spans, lv_pct(100));
        lv_spangroup_set_mode(spans, LV_SPAN_MODE_BREAK);
        lv_spangroup_set_align(spans, LV_TEXT_ALIGN_LEFT);
        lv_spangroup_set_overflow(spans, LV_SPAN_OVERFLOW_CLIP);
        lv_obj_set_style_bg_opa(spans, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(spans, 0, 0);
        lv_obj_set_style_pad_all(spans, 0, 0);

        std::vector<RichSegment> segments = parse_rich_segments(text);
        if (segments.empty()) {
            segments.push_back({text, false, false});
        }

        for (const auto& seg : segments) {
            lv_span_t* span = lv_spangroup_new_span(spans);
            lv_span_set_text(span, seg.text.c_str());
            style_span(span, seg.bold, seg.italic, false);
        }

        lv_spangroup_refr_mode(spans);
        lv_obj_scroll_to_view(bubble, LV_ANIM_ON);
    }

    static void go_home_event(lv_event_t* e) {
        AppManager::switchTo(APP_HOME);
    }

    static void kb_send_event(lv_event_t* e) {
        ChatbotApp* app = (ChatbotApp*)lv_event_get_user_data(e);
        if (!app || !app->ta_input) return;
        String text = lv_textarea_get_text(app->ta_input);
        app->send_prompt(text);
    }

    static void kb_cancel_event(lv_event_t* e) {
        ChatbotApp* app = (ChatbotApp*)lv_event_get_user_data(e);
        if (!app || !app->ta_input) return;
        lv_textarea_set_text(app->ta_input, "");
    }

    bool send_prompt(const String& text) {
        if (text.length() == 0) return false;

        bool queued = false;
        mutex_enter_blocking(&chatMutex);
        if (!sharedData.requestPending) {
            sharedData.pendingPrompt = text;
            sharedData.requestPending = true;
            history.push_back({"user", text});
            trimHistoryLocked();
            queued = true;
        }
        mutex_exit(&chatMutex);

        if (!queued) return false;

        add_message_ui("user", text);
        if (loader) lv_obj_clear_flag(loader, LV_OBJ_FLAG_HIDDEN);
        if (ta_input) lv_textarea_set_text(ta_input, "");
        return true;
    }

    // Helper pour ajouter une bulle de texte dans l'UI
    void add_message_ui(String role, String text) {
        lv_obj_t* bubble = lv_obj_create(chat_list);
        lv_obj_set_width(bubble, lv_pct(85));
        lv_obj_set_height(bubble, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(bubble, 10, 0);
        lv_obj_set_style_radius(bubble, 15, 0);
        lv_obj_set_style_border_width(bubble, 0, 0);
        
        if (role == "user") {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(CHAT_OUT_BG), 0);
            lv_obj_set_style_text_color(bubble, lv_color_white(), 0);
            lv_obj_align(bubble, LV_ALIGN_TOP_RIGHT, 0, 0);
        } else {
            lv_obj_set_style_bg_color(bubble, lv_color_hex(CHAT_IN_BG), 0);
            lv_obj_set_style_text_color(bubble, lv_color_white(), 0);
            lv_obj_align(bubble, LV_ALIGN_TOP_LEFT, 0, 0);
        }

        lv_obj_t* label = lv_label_create(bubble);
        lv_label_set_text(label, text.c_str());
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(label, 240);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        
        // Scroll automatique tout en bas
        lv_obj_scroll_to_view(bubble, LV_ANIM_ON);
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        header = nullptr;
        chat_view = nullptr;
        footer = nullptr;
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(CHAT_BG), 0);
        
        // --- HEADER ---
        header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(CHAT_HEADER_BG), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_home_event, LV_EVENT_CLICKED, NULL);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_black(), 0);
        lv_obj_center(l_back);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Gemini Chat");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(title, lv_color_black(), 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

        // --- ZONE DE DISCUSSION ---
        chat_view = lv_obj_create(main_bg);
        lv_obj_set_size(chat_view, 320, 430);
        lv_obj_align(chat_view, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_color(chat_view, lv_color_hex(CHAT_BG), 0);
        lv_obj_set_style_border_width(chat_view, 0, 0);
        lv_obj_clear_flag(chat_view, LV_OBJ_FLAG_SCROLLABLE);

        chat_list = lv_obj_create(chat_view);
        lv_obj_set_size(chat_list, 320, 420 - 200 - 40);
        lv_obj_align(chat_list, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_opa(chat_list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(chat_list, 0, 0);
        lv_obj_set_style_pad_all(chat_list, 10, 0);
        lv_obj_set_style_pad_row(chat_list, 10, 0);
        lv_obj_set_flex_flow(chat_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(chat_list, LV_DIR_VER);

        footer = lv_obj_create(chat_view);
        lv_obj_set_size(footer, 320, 210);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 10);
        lv_obj_set_style_bg_color(footer, lv_color_hex(CHAT_HEADER_BG), 0);
        lv_obj_set_style_border_width(footer, 0, 0);
        lv_obj_clear_flag(footer, LV_OBJ_FLAG_SCROLLABLE);

        // --- ZONE DE SAISIE ---
        ta_input = lv_textarea_create(main_bg);
        lv_textarea_set_one_line(ta_input, true);
        lv_textarea_set_placeholder_text(ta_input, "Pose ta question");
        lv_obj_set_size(ta_input, 320, 40);
        lv_obj_align(ta_input, LV_ALIGN_BOTTOM_MID, 0, -200);
        lv_obj_set_style_border_width(ta_input, 2, 0);
        lv_obj_set_style_border_color(ta_input, lv_color_hex(0xD1D1D6), 0);

        // --- CLAVIER ---
        kb = lv_keyboard_create(main_bg);
        lv_keyboard_set_textarea(kb, ta_input);
        lv_obj_set_size(kb, 320, 200);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(kb, kb_send_event, LV_EVENT_READY, this);
        lv_obj_add_event_cb(kb, kb_cancel_event, LV_EVENT_CANCEL, this);

        // --- CHARGEMENT ---
        loader = lv_spinner_create(main_bg, 1000, 60);
        lv_obj_set_size(loader, 40, 40);
        lv_obj_align(loader, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN); // Caché par défaut
        
        // Message d'accueil
        add_message_ui("model", "Salut ! Je suis Gemini. Pose-moi une question.");
    }

    // --- CORE 0 : MISE À JOUR UI ---
    void update() override {
        // On vérifie si le Core 1 a reçu une réponse
        if (sharedData.responseReady) {
            mutex_enter_blocking(&chatMutex);
            if (sharedData.responseReady) {
                if (sharedData.success) {
                    add_ai_message_ui(sharedData.lastResponse);
                    history.push_back({"model", sharedData.lastResponse});
                    trimHistoryLocked();
                } else {
                    String err = sharedData.lastResponse.length() ? sharedData.lastResponse : String("Erreur reseau.");
                    NetworkErrorHandler::showIfError("Gemini", err.c_str());
                    add_message_ui("model", err);
                }
                
                sharedData.responseReady = false; // On acquitte
                lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN); // On cache le chargement
            }
            mutex_exit(&chatMutex);
        }
    }

    // --- CORE 1 : TRAVAIL RÉSEAU (Bloquant, mais sans impacter le Core 0) ---
    void update1() override {
        if (!sharedData.requestPending) return;
        watchdog_update(); // Garde le système en vie

        if (!LTE::isReadyForData()) {
            mutex_enter_blocking(&chatMutex);
            sharedData.success = false;
            sharedData.lastResponse = "LTE indisponible";
            sharedData.responseReady = true;
            sharedData.requestPending = false;
            mutex_exit(&chatMutex);
            return;
        }

        std::vector<ChatMessage> historySnapshot;
        mutex_enter_blocking(&chatMutex);
        historySnapshot = history;
        mutex_exit(&chatMutex);

        // 1. Préparation du JSON avec ArduinoJson
        JsonDocument doc;
        JsonArray contents = doc["contents"].to<JsonArray>();

        // On injecte tout l'historique de la conversation
        for (const auto& msg : historySnapshot) {
            JsonObject content = contents.add<JsonObject>();
            content["role"] = msg.role;
            content["parts"].add<JsonObject>()["text"] = msg.text;
        }

        String requestBody;
        serializeJson(doc, requestBody);

        String url = String("https://generativelanguage.googleapis.com/v1beta/models/") + GEMINI_MODEL + ":generateContent?key=" + GEMINI_API_KEY;
        // 2. Requête HTTP POST via modem LTE (HTTP AT) avec retry court.
        static constexpr int kMaxAttempts = 2;
        String apiResponse = "";
        bool success = false;
        String errorResponse = "";

        for (int attempt = 0; attempt < kMaxAttempts && !success; attempt++) {
            String payload = LTE::httpPostBlocking(url, requestBody, "application/json", "Connection: close");
            watchdog_update();

            if (payload.length() > 0) {
                JsonDocument responseDoc;
                DeserializationError err = deserializeJson(responseDoc, payload);

                if (!err) {
                    apiResponse = responseDoc["candidates"][0]["content"]["parts"][0]["text"].as<String>();
                    success = true;
                } else {
                    errorResponse = String("Reponse JSON invalide: ") + err.c_str();
                }
            } else {
                errorResponse = "HTTP LTE: reponse vide";
                if (attempt + 1 < kMaxAttempts) {
                    sleep_ms(120);
                    yield();
                    continue;
                }
            }
        }

        // 3. Renvoi des données au Core 0
        mutex_enter_blocking(&chatMutex);
        sharedData.lastResponse = apiResponse;
        if (!success) sharedData.lastResponse = errorResponse.length() ? errorResponse : String("Erreur API Gemini");
        sharedData.success = success;
        sharedData.responseReady = true;
        sharedData.requestPending = false;
        sharedData.pendingPrompt = "";
        mutex_exit(&chatMutex);
    }
};

#endif