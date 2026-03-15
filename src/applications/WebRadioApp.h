#ifndef WEBRADIO_APP_H
#define WEBRADIO_APP_H

#include "App.h"
#include "AppManager.h"
#include "../Hardware.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <AudioFileSource.h>
#include <algorithm>
#include <vector>
#include <ArduinoJson.h>
#include "../system/Settings.h"

// ======================================================
// I2S avec DMA : 32 petits buffers de 256 words
// ======================================================
class AudioOutputI2SBuffered : public AudioOutputI2S {
public:
    using AudioOutputI2S::AudioOutputI2S;
    bool begin() override {
        if (!i2sOn) {
            i2s.setBuffers(32, 256);
        }
        return AudioOutputI2S::begin();
    }

    bool ConsumeSample(int16_t sample[2]) override {
        if (!i2sOn) return false;
        int16_t ms[2];
        ms[0] = sample[0];
        ms[1] = sample[1];
        MakeSampleStereo16(ms);
        if (this->mono) {
            int32_t ttl = ms[LEFTCHANNEL] + ms[RIGHTCHANNEL];
            ms[LEFTCHANNEL] = ms[RIGHTCHANNEL] = (ttl>>1) & 0xffff;
        }
        uint32_t s32 = ((Amplify(ms[RIGHTCHANNEL])) << 16) | (Amplify(ms[LEFTCHANNEL]) & 0xffff);
        return !!i2s.write((int32_t)s32, true);
    }
};

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(lv_font_montserrat_24); // Idéalement avoir une police un peu plus grande pour le titre

// ======================================================
// SOURCE AUDIO : ring buffer auto-alimenté depuis WiFiClient
// ======================================================
class AudioFileSourceStream : public AudioFileSource {
public:
    AudioFileSourceStream(uint32_t bufSize = 65536) {
        _bufSize = bufSize;
        _buf = (uint8_t*)malloc(bufSize);
        _readPtr = 0;
        _writePtr = 0;
        _length = 0;
        _opened = false;
        _net = nullptr;
        _icyMetaInt = 0;
        _icyDataLeft = 0;
        _icyState = ICY_AUDIO;
        _icyMetaRemain = 0;
        _lastDataMs = 0;
        _totalRead = 0;
    }
    ~AudioFileSourceStream() override { free(_buf); }

    void setStream(WiFiClient* net, int icyMetaInt) {
        _net = net;
        _icyMetaInt = icyMetaInt;
        _icyDataLeft = icyMetaInt ? icyMetaInt : 0;
        _icyState = ICY_AUDIO;
        _icyMetaRemain = 0;
        _readPtr = 0;
        _writePtr = 0;
        _length = 0;
        _opened = (_buf != nullptr && _net != nullptr);
        _lastDataMs = millis();
    }

    bool open(const char *url) override { (void)url; return _opened; }
    bool isOpen() override { return _opened; }
    bool close() override { _opened = false; _net = nullptr; return true; }
    uint32_t getSize() override { return 0xFFFFFFFF; }
    uint32_t getPos() override { return 0; }
    bool seek(int32_t pos, int dir) override { (void)pos; (void)dir; return false; }
    bool loop() override { fill(); return _opened; }

    uint32_t read(void *data, uint32_t len) override {
        if (!_buf) return 0;
        fill();
        if (!_length && _opened && _net && _net->connected()) {
            for (int i = 0; i < 15 && !_length; i++) {
                yield();
                delayMicroseconds(500);
                fill();
            }
        }
        if (!_length) return 0;
        uint32_t curLen = _length;
        uint32_t toRead = std::min(len, curLen);
        uint8_t *dst = (uint8_t*)data;
        uint32_t first = std::min(toRead, _bufSize - _readPtr);
        memcpy(dst, _buf + _readPtr, first);
        if (toRead > first) memcpy(dst + first, _buf, toRead - first);
        _readPtr = (_readPtr + toRead) % _bufSize;
        _length -= toRead;
        _totalRead += toRead;
        return toRead;
    }

    void pumpNetwork() { fill(); }
    uint32_t available() { return _length; }
    uint32_t totalRead() { return _totalRead; }
    uint32_t lastDataMs() { return _lastDataMs; }

private:
    enum IcyState : uint8_t { ICY_AUDIO, ICY_META_LEN, ICY_META_SKIP };

    void fill() {
        if (!_net || !_net->connected() || !_buf) return;
        uint8_t tmp[1460];
        int rounds = 0;

        while (rounds++ < 96) {
            int avail = _net->available();
            if (avail <= 0) break;

            if (_icyMetaInt && _icyState == ICY_META_LEN) {
                int b = _net->read();
                if (b < 0) break;
                _icyMetaRemain = b * 16;
                _icyState = (_icyMetaRemain > 0) ? ICY_META_SKIP : ICY_AUDIO;
                _icyDataLeft = _icyMetaInt;
                continue;
            }

            if (_icyMetaInt && _icyState == ICY_META_SKIP) {
                int toSkip = std::min(_icyMetaRemain, (int)_net->available());
                if (toSkip <= 0) break;
                while (toSkip > 0) {
                    int chunk = std::min(toSkip, (int)sizeof(tmp));
                    int rd = _net->read(tmp, chunk);
                    if (rd <= 0) break;
                    toSkip -= rd;
                    _icyMetaRemain -= rd;
                }
                if (_icyMetaRemain <= 0) _icyState = ICY_AUDIO;
                continue;
            }

            uint32_t space = _bufSize - _length;
            if (!space) break;

            size_t toRead = std::min((size_t)avail, sizeof(tmp));
            toRead = std::min(toRead, (size_t)space);
            if (_icyMetaInt) toRead = std::min(toRead, (size_t)_icyDataLeft);
            if (!toRead) break;

            int rd = _net->read(tmp, toRead);
            if (rd <= 0) break;
            _lastDataMs = millis();

            uint32_t toWrite = std::min((uint32_t)rd, space);
            uint32_t first = std::min(toWrite, _bufSize - _writePtr);
            memcpy(_buf + _writePtr, tmp, first);
            if (toWrite > first) memcpy(_buf, tmp + first, toWrite - first);
            _writePtr = (_writePtr + toWrite) % _bufSize;
            _length += toWrite;

            if (_icyMetaInt) {
                _icyDataLeft -= rd;
                if (_icyDataLeft <= 0) _icyState = ICY_META_LEN;
            }
        }
    }

    uint8_t *_buf;
    uint32_t _bufSize;
    volatile uint32_t _readPtr;
    volatile uint32_t _writePtr;
    volatile uint32_t _length;
    bool _opened;
    WiFiClient* _net;
    int _icyMetaInt;
    int _icyDataLeft;
    IcyState _icyState;
    int _icyMetaRemain;
    uint32_t _lastDataMs;
    volatile uint32_t _totalRead;
};

// ======================================================
// CALLBACKS D'ANIMATION LVGL
// ======================================================
static void anim_y_cb(void * var, int32_t v) {
    lv_obj_set_y((lv_obj_t *)var, v);
}
static void anim_height_cb(void * var, int32_t v) {
    lv_obj_set_height((lv_obj_t *)var, v);
}
static void anim_opa_cb(void * var, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
}

// ======================================================
// APP
// ======================================================
class WebRadioApp : public App {
private:
    struct RadioEntry {
        String name;
        String url;
        uint32_t color;
        lv_obj_t* btn;
        lv_obj_t* lbl_icon;
    };

    enum RadioUiState : uint8_t {
        UI_IDLE = 0,
        UI_CONNECTING,
        UI_BUFFERING,
        UI_PLAYING,
        UI_RETRY_WAIT,
        UI_ERROR
    };

    lv_obj_t* main_bg;
    lv_obj_t* lbl_status;
    lv_obj_t* list_cont;
    
    // Nouveaux éléments pour la zone de lecture dynamique
    lv_obj_t* player_cont;
    lv_obj_t* lbl_player_title;
    lv_obj_t* btn_main_play;
    lv_obj_t* lbl_main_play_icon;

    std::vector<RadioEntry> radios;

    volatile bool play_requested = false;
    volatile bool is_playing     = false;
    volatile bool force_stop     = false;
    
    int active_radio_index = -1;
    volatile int next_radio_index = -1;
    bool player_expanded = false;

    static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
    static constexpr uint32_t READ_TIMEOUT_MS = 5000;
    static constexpr uint32_t STREAM_STALL_TIMEOUT_MS = 12000;
    static constexpr uint32_t RETRY_BASE_DELAY_MS = 1200;
    static constexpr uint32_t RETRY_MAX_DELAY_MS = 15000;
    
    int icyMetaInt = 0;
    String url = "";

    volatile uint8_t ui_state = UI_IDLE;
    volatile int ui_last_error = 0;
    uint32_t retry_after_ms = 0;
    uint8_t retry_count = 0;

    AudioOutputI2SBuffered* audio_out = nullptr;
    AudioGeneratorMP3* mp3 = nullptr;
    AudioFileSourceStream* stream_src = nullptr;
    WiFiClient* plain_client = nullptr;
    WiFiClientSecure* secure_client = nullptr;
    HTTPClient* http_client = nullptr;
    bool audio_inited = false;
    uint32_t frames_count = 0;

    const char* map_http_error(int code) {
        switch (code) {
            case -1: return "Connexion serveur";
            case -2: return "Envoi echoue";
            case -3: return "Lecture timeout";
            case -4: return "Memoire faible";
            case -5: return "Connexion perdue";
            case -11: return "TLS handshake";
            default: return "Erreur HTTP";
        }
    }

    void set_ui_state(uint8_t state, int err = 0) {
        ui_state = state;
        ui_last_error = err;
    }

    void schedule_retry(const char* reason, int code) {
        cleanup_http();
        if (!play_requested) {
            retry_after_ms = 0;
            retry_count = 0;
            set_ui_state(UI_IDLE, 0);
            return;
        }
        retry_count = (uint8_t)std::min((int)retry_count + 1, 6);
        uint32_t delay_ms = RETRY_BASE_DELAY_MS * (1u << (retry_count - 1));
        delay_ms = std::min(delay_ms, RETRY_MAX_DELAY_MS);
        retry_after_ms = millis() + delay_ms;
        set_ui_state(UI_RETRY_WAIT, code);
        Serial.printf("[WebRadio] %s (code=%d), retry dans %lu ms\n", reason, code, (unsigned long)delay_ms);
    }

    static void go_back_event(lv_event_t* e) {
        WebRadioApp* app = (WebRadioApp*)lv_event_get_user_data(e);
        app->play_requested = false;
        app->force_stop = true;
        AppManager::switchTo(APP_HOME);
    }

    static void radio_clicked_event(lv_event_t* e) {
        WebRadioApp* app = (WebRadioApp*)lv_event_get_user_data(e);
        lv_obj_t* target = lv_event_get_target(e);
        int index = (int)(uintptr_t)lv_obj_get_user_data(target);

        if (app->active_radio_index == index && app->play_requested) {
            app->play_requested = false;
            app->force_stop = true;
            app->next_radio_index = -1;
        } else {
            app->play_requested = false;
            app->force_stop = true;
            app->next_radio_index = index;
            app->transition_to_player(index);
        }
    }

    static void btn_main_play_event(lv_event_t* e) {
        WebRadioApp* app = (WebRadioApp*)lv_event_get_user_data(e);
        if (app->play_requested) {
            app->play_requested = false;
            app->force_stop = true;
            app->next_radio_index = -1;
        } else if (app->active_radio_index != -1) {
            app->play_requested = true;
            app->next_radio_index = app->active_radio_index;
        }
    }

    void transition_to_player(int index) {
        // Appliquer le dégradé de couleur
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(radios[index].color), 0);
        
        // Mettre à jour le titre du lecteur
        lv_label_set_text(lbl_player_title, radios[index].name.c_str());

        if (!player_expanded) {
            player_expanded = true;

            // Animer la liste vers le bas (Y de 50 à 220, Height de 430 à 260)
            lv_anim_t a1;
            lv_anim_init(&a1);
            lv_anim_set_var(&a1, list_cont);
            lv_anim_set_time(&a1, 400);
            lv_anim_set_path_cb(&a1, lv_anim_path_ease_out);
            
            lv_anim_set_values(&a1, lv_obj_get_y(list_cont), 220);
            lv_anim_set_exec_cb(&a1, anim_y_cb);
            lv_anim_start(&a1);

            lv_anim_t a2;
            lv_anim_init(&a2);
            lv_anim_set_var(&a2, list_cont);
            lv_anim_set_time(&a2, 400);
            lv_anim_set_path_cb(&a2, lv_anim_path_ease_out);
            lv_anim_set_values(&a2, lv_obj_get_height(list_cont), 260);
            lv_anim_set_exec_cb(&a2, anim_height_cb);
            lv_anim_start(&a2);

            // Rendre le lecteur visible avec un fondu
            lv_anim_t a3;
            lv_anim_init(&a3);
            lv_anim_set_var(&a3, player_cont);
            lv_anim_set_time(&a3, 400);
            lv_anim_set_values(&a3, 0, 255);
            lv_anim_set_exec_cb(&a3, anim_opa_cb);
            lv_anim_start(&a3);
        }
    }

    void cleanup_http() {
        if (mp3 && mp3->isRunning()) mp3->stop();
        if (stream_src) stream_src->close();
        if (http_client) http_client->end();
        if (secure_client) secure_client->stop();
        if (plain_client) plain_client->stop();
        is_playing = false;
        icyMetaInt = 0;
    }

    bool init_audio() {
        if (audio_inited) return true;

        const int base = (I2S_OUT_BCLK < I2S_OUT_WS) ? I2S_OUT_BCLK : I2S_OUT_WS;
        const bool wantSwap = (I2S_OUT_WS < I2S_OUT_BCLK);

        if (!audio_out) audio_out = new AudioOutputI2SBuffered();
        audio_out->SetRate(44100);
        audio_out->SetBitsPerSample(16);
        audio_out->SetChannels(2);
        audio_out->SetOutputModeMono(true);
        audio_out->SetGain(settings::getVolume() / 100.0f);
        audio_out->SwapClocks(wantSwap);
        audio_out->SetPinout(base, base + 1, I2S_OUT_DIN);

        if (!stream_src) stream_src = new AudioFileSourceStream(65536);
        if (!mp3) mp3 = new AudioGeneratorMP3();
        if (!secure_client) secure_client = new WiFiClientSecure();
        if (!plain_client) plain_client = new WiFiClient();
        if (!http_client) http_client = new HTTPClient();

        audio_inited = true;
        return true;
    }

    void fetch_radio_list() {
        radios.clear();
        if (WiFi.status() != WL_CONNECTED) return;

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(10000);

        HTTPClient http;
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        if (!http.begin(client, "https://raw.githubusercontent.com/FallMarsisus/picophone-app-repo/refs/heads/main/radio-list.json")) {
            return;
        }
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            
            DynamicJsonDocument doc(4096);
            DeserializationError error = deserializeJson(doc, payload);
            
            if (!error) {
                JsonArray items = doc["items"].as<JsonArray>();
                for (JsonObject item : items) {
                    RadioEntry r;
                    r.name = item["name"].as<String>();
                    r.url = item["stream_url"].as<String>();
                    
                    String colorStr = item["color"].as<String>();
                    colorStr.replace("#", "");
                    r.color = strtol(colorStr.c_str(), NULL, 16);
                    
                    radios.push_back(r);
                }
            }
        }
        http.end();
    }

public:
    WebRadioApp() {}
    ~WebRadioApp() {}

    void start(lv_obj_t* parent) override {
        main_bg = parent;
        play_requested = false;
        is_playing = false;
        force_stop = false;
        active_radio_index = -1;
        next_radio_index = -1;
        player_expanded = false;
        retry_after_ms = 0;
        retry_count = 0;
        frames_count = 0;
        set_ui_state(UI_IDLE, 0);

        if (!init_audio()) {
            set_ui_state(UI_ERROR, -201);
            return;
        }

        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        // Configuration initiale du dégradé de fond (Noir)
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_grad_color(main_bg, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_grad_dir(main_bg, LV_GRAD_DIR_VER, 0);

        // Header
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0); // Transparent pour voir le dégradé
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_back_event, LV_EVENT_CLICKED, this);
        
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(l_back);

        lbl_status = lv_label_create(header);
        lv_label_set_text(lbl_status, "Pret");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_status, LV_ALIGN_RIGHT_MID, -10, 0);

        // --- ZONE DU LECTEUR (Cachée au début) ---
        player_cont = lv_obj_create(main_bg);
        lv_obj_set_size(player_cont, 320, 170);
        lv_obj_align(player_cont, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_opa(player_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(player_cont, 0, 0);
        lv_obj_set_style_opa(player_cont, 0, 0); // Invisible au début
        lv_obj_clear_flag(player_cont, LV_OBJ_FLAG_SCROLLABLE);

        lbl_player_title = lv_label_create(player_cont);
        lv_label_set_text(lbl_player_title, "");
        lv_obj_set_style_text_color(lbl_player_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_player_title, &lv_font_montserrat_18, 0); // Utiliser une plus grande police si dispo
        lv_obj_align(lbl_player_title, LV_ALIGN_TOP_MID, 0, 10);

        btn_main_play = lv_btn_create(player_cont);
        lv_obj_set_size(btn_main_play, 80, 80);
        lv_obj_set_style_radius(btn_main_play, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn_main_play, lv_color_white(), 0);
        lv_obj_set_style_shadow_width(btn_main_play, 20, 0);
        lv_obj_set_style_shadow_color(btn_main_play, lv_color_black(), 0);
        lv_obj_set_style_shadow_opa(btn_main_play, 80, 0);
        lv_obj_align(btn_main_play, LV_ALIGN_CENTER, 0, 20);
        lv_obj_add_event_cb(btn_main_play, btn_main_play_event, LV_EVENT_CLICKED, this);

        lbl_main_play_icon = lv_label_create(btn_main_play);
        lv_label_set_text(lbl_main_play_icon, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(lbl_main_play_icon, lv_color_black(), 0);
        lv_obj_center(lbl_main_play_icon);

        // --- LISTE DES RADIOS ---
        fetch_radio_list();

        list_cont = lv_obj_create(main_bg);
        lv_obj_set_size(list_cont, 320, 430); // Plein écran au début
        lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(list_cont, 10, 0);
        lv_obj_set_style_pad_row(list_cont, 15, 0); 

        if (radios.empty()) {
            lv_obj_t* lbl_err = lv_label_create(list_cont);
            lv_label_set_text(lbl_err, "Impossible de charger les radios");
            lv_obj_set_style_text_color(lbl_err, lv_color_white(), 0);
        } else {
            for (int i = 0; i < radios.size(); i++) {
                lv_obj_t* item = lv_obj_create(list_cont);
                lv_obj_set_size(item, 290, 50);
                lv_obj_set_style_bg_color(item, lv_color_hex(0x2C2C2E), 0); // Gris foncé translucide
                lv_obj_set_style_bg_opa(item, 200, 0);
                lv_obj_set_style_radius(item, 10, 0);
                lv_obj_set_style_border_width(item, 0, 0);
                lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
                
                lv_obj_set_user_data(item, (void*)(uintptr_t)i);
                lv_obj_add_event_cb(item, radio_clicked_event, LV_EVENT_CLICKED, this);

                lv_obj_t* color_sq = lv_obj_create(item);
                lv_obj_set_size(color_sq, 20, 20);
                lv_obj_set_style_radius(color_sq, 5, 0);
                lv_obj_set_style_bg_color(color_sq, lv_color_hex(radios[i].color), 0);
                lv_obj_set_style_border_width(color_sq, 0, 0);
                lv_obj_align(color_sq, LV_ALIGN_LEFT_MID, 5, 0);
                lv_obj_clear_flag(color_sq, LV_OBJ_FLAG_SCROLLABLE);

                lv_obj_t* lbl_name = lv_label_create(item);
                lv_label_set_text(lbl_name, radios[i].name.c_str());
                lv_obj_set_style_text_color(lbl_name, lv_color_white(), 0);
                lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
                lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 40, 0);

                lv_obj_t* lbl_icon = lv_label_create(item);
                lv_label_set_text(lbl_icon, LV_SYMBOL_PLAY);
                lv_obj_set_style_text_color(lbl_icon, lv_color_white(), 0);
                lv_obj_align(lbl_icon, LV_ALIGN_RIGHT_MID, -5, 0);

                radios[i].btn = item;
                radios[i].lbl_icon = lbl_icon;
            }
        }
    }

    // --------------------------------------------------
    // UI UPDATE (Core 0)
    // --------------------------------------------------
    void update() override {
        static uint32_t last = 0;
        if (millis() - last < 300) return;
        last = millis();

        if (!play_requested && next_radio_index == -1) {
            set_ui_state(UI_IDLE, 0);
        } else if (is_playing) {
            set_ui_state((frames_count > 0) ? UI_PLAYING : UI_BUFFERING, 0);
            if (audio_out) audio_out->SetGain(settings::getVolume() / 100.0f);
        }

        switch (ui_state) {
            case UI_CONNECTING: lv_label_set_text(lbl_status, "Connexion"); break;
            case UI_BUFFERING: lv_label_set_text(lbl_status, "Cache..."); break;
            case UI_PLAYING: lv_label_set_text(lbl_status, "Lecture " LV_SYMBOL_AUDIO); break;
            case UI_RETRY_WAIT: {
                uint32_t now = millis();
                uint32_t remaining = (retry_after_ms > now) ? (retry_after_ms - now) : 0;
                char msg[32];
                snprintf(msg, sizeof(msg), "Retry %lus", (unsigned long)((remaining + 999) / 1000));
                lv_label_set_text(lbl_status, msg);
                break;
            }
            case UI_ERROR: lv_label_set_text(lbl_status, "Erreur"); break;
            case UI_IDLE: default: lv_label_set_text(lbl_status, "Pret"); break;
        }

        // MAJ de l'icône du gros bouton principal
        lv_label_set_text(lbl_main_play_icon, play_requested ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);

        // MAJ des petites icônes de la liste
        for (int i = 0; i < radios.size(); i++) {
            if (i == active_radio_index && play_requested) {
                lv_label_set_text(radios[i].lbl_icon, (ui_state == UI_PLAYING) ? LV_SYMBOL_STOP : LV_SYMBOL_REFRESH);
            } else {
                lv_label_set_text(radios[i].lbl_icon, LV_SYMBOL_PLAY);
            }
        }
    }

    // --------------------------------------------------
    // AUDIO LOOP (Core 1)
    // --------------------------------------------------
    void update1() override {
        if (force_stop) {
            cleanup_http();
            force_stop = false;
            retry_after_ms = 0;
            retry_count = 0;
            set_ui_state(UI_IDLE, 0);

            if (next_radio_index != -1) {
                active_radio_index = next_radio_index;
                url = radios[active_radio_index].url;
                play_requested = true;
                next_radio_index = -1;
            } else {
                active_radio_index = -1;
            }
            return;
        }

        if (play_requested && !is_playing) {
            if (millis() < retry_after_ms) return;

            if (WiFi.status() != WL_CONNECTED) {
                schedule_retry("WiFi indisponible", -100);
                return;
            }

            set_ui_state(UI_CONNECTING, 0);

            bool is_https = url.startsWith("https://");
            if (is_https) {
                secure_client->setInsecure();
                secure_client->setTimeout(CONNECT_TIMEOUT_MS);
            } else {
                plain_client->setTimeout(CONNECT_TIMEOUT_MS);
            }

            http_client->end();
            bool begin_ok = is_https
                ? http_client->begin(*secure_client, url)
                : http_client->begin(*plain_client, url);
            if (!begin_ok) {
                schedule_retry("HTTP begin() echoue", -120);
                return;
            }
            http_client->setReuse(false);
            http_client->setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            http_client->setTimeout(READ_TIMEOUT_MS);

            const char *icyHdrs[] = { "icy-metaint" };
            http_client->collectHeaders(icyHdrs, 1);
            http_client->addHeader("Icy-MetaData", "1");
            http_client->addHeader("User-Agent", "TestDisplay-WebRadio/1.0");

            int code = http_client->GET();
            if (code != HTTP_CODE_OK) {
                schedule_retry("Echec connexion HTTP", code);
                return;
            }

            if (http_client->hasHeader("icy-metaint")) {
                icyMetaInt = http_client->header("icy-metaint").toInt();
            } else {
                icyMetaInt = 0;
            }

            stream_src->setStream(http_client->getStreamPtr(), icyMetaInt);
            set_ui_state(UI_BUFFERING, 0);
            uint32_t prebuf_start = millis();
            
            while (stream_src->available() < 16384 && (millis() - prebuf_start < 10000) && !force_stop) {
                stream_src->pumpNetwork();
                yield();
                delay(2);
            }

            if (stream_src->available() < 4096) {
                schedule_retry("Pre-buffer insuffisant", -210);
                return;
            }

            if (!mp3->begin(stream_src, audio_out)) {
                schedule_retry("Decodeur MP3 echoue", -200);
                return;
            }

            is_playing = true;
            frames_count = 0;
            retry_after_ms = 0;
            retry_count = 0;
            set_ui_state(UI_BUFFERING, 0);
        }

        if (is_playing && mp3) {
            stream_src->pumpNetwork();

            if (mp3->isRunning()) {
                bool decoder_stopped = false;
                uint32_t t_end = millis() + 100;

                while (mp3->isRunning() && !force_stop && millis() < t_end) {
                    if (!mp3->loop()) {
                        decoder_stopped = true;
                        break;
                    }
                    frames_count++;
                }

                stream_src->pumpNetwork();

                if (decoder_stopped) {
                    stream_src->pumpNetwork();
                    if (stream_src->available() >= 4096) {
                        mp3->stop();
                        mp3->begin(stream_src, audio_out);
                    }
                }
            } else {
                WiFiClient* ns = http_client->getStreamPtr();
                if (!ns || !ns->connected()) {
                    cleanup_http();
                    schedule_retry("Flux perdu", -5);
                    return;
                }
                stream_src->pumpNetwork();
                if (stream_src->available() >= 4096) {
                    mp3->begin(stream_src, audio_out);
                }
            }

            uint32_t last_net = stream_src->lastDataMs();
            if (last_net && (millis() - last_net) > STREAM_STALL_TIMEOUT_MS) {
                cleanup_http();
                schedule_retry("Flux bloque", -3);
                return;
            }
        }
    }

    void stop() override {
        play_requested = false;
        force_stop = true;
        next_radio_index = -1;

        uint32_t t = millis();
        while (force_stop && millis() - t < 500) {
            delay(5);
        }

        if (mp3 && mp3->isRunning()) mp3->stop();
        if (audio_out) { audio_out->flush(); audio_out->stop(); }
        audio_pins_quiet();
        
        radios.clear();
        main_bg = nullptr;
    }
};

#endif