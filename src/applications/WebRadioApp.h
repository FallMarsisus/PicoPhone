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
            // Slightly larger DMA queue to absorb network/decode jitter.
            i2s.setBuffers(40, 320);
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
            // Keep signed 16-bit value (no masking) to avoid distortion artifacts.
            int16_t mixed = (int16_t)(ttl >> 1);
            ms[LEFTCHANNEL] = mixed;
            ms[RIGHTCHANNEL] = mixed;
        }
        uint16_t left = (uint16_t)Amplify(ms[LEFTCHANNEL]);
        uint16_t right = (uint16_t)Amplify(ms[RIGHTCHANNEL]);
        uint32_t s32 = ((uint32_t)right << 16) | left;
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

    lv_obj_t* main_bg = nullptr;
    lv_obj_t* lbl_status = nullptr;
    lv_obj_t* list_cont = nullptr;
    
    // Nouveaux éléments pour la zone de lecture dynamique
    lv_obj_t* player_cont = nullptr;
    lv_obj_t* lbl_player_title = nullptr;
    lv_obj_t* btn_main_play = nullptr;
    lv_obj_t* lbl_main_play_icon = nullptr;
    lv_obj_t* slider_volume = nullptr;
    lv_obj_t* lbl_volume_val = nullptr;

    std::vector<RadioEntry> radios;

    volatile bool play_requested = false;
    volatile bool is_playing     = false;
    volatile bool force_stop     = false;
    volatile bool exit_requested = false;
    
    int active_radio_index = -1;
    volatile int next_radio_index = -1;
    bool player_expanded = false;

    static constexpr uint32_t CONNECT_TIMEOUT_MS = 2500;
    static constexpr uint32_t READ_TIMEOUT_MS = 2500;
    static constexpr uint32_t STREAM_STALL_TIMEOUT_MS = 12000;
    static constexpr uint32_t RETRY_BASE_DELAY_MS = 1200;
    static constexpr uint32_t RETRY_MAX_DELAY_MS = 15000;
    static constexpr uint32_t PREBUFFER_TARGET_BYTES = 16384;
    static constexpr uint32_t PREBUFFER_MIN_BYTES = 4096;
    static constexpr uint32_t PREBUFFER_TIMEOUT_MS = 3000;
    static constexpr uint32_t RADIO_FETCH_RETRY_MS = 30000;
    
    int icyMetaInt = 0;
    String url = "";

    volatile uint8_t ui_state = UI_IDLE;
    volatile int ui_last_error = 0;
    uint32_t retry_after_ms = 0;
    uint8_t retry_count = 0;
    volatile bool radio_list_dirty = false;
    volatile bool radio_fetch_requested = false;
    bool radio_fetch_done = false;
    uint32_t radio_fetch_next_try_ms = 0;

    AudioOutputI2SBuffered* audio_out = nullptr;
    AudioGeneratorMP3* mp3 = nullptr;
    AudioFileSourceStream* stream_src = nullptr;
    WiFiClient* plain_client = nullptr;
    WiFiClientSecure* secure_client = nullptr;
    HTTPClient* http_client = nullptr;
    bool audio_inited = false;
    uint32_t frames_count = 0;
    uint8_t decoder_fail_streak = 0;
    uint32_t last_decoder_restart_ms = 0;

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
        cleanup_http(true);
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
        app->exit_requested = true;
    }

    static void volume_slider_event(lv_event_t* e) {
        WebRadioApp* app = (WebRadioApp*)lv_event_get_user_data(e);
        lv_obj_t* slider = lv_event_get_target(e);
        int val = lv_slider_get_value(slider);
        settings::setVolume((uint8_t)val);

        if (app->lbl_volume_val) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d%%", val);
            lv_label_set_text(app->lbl_volume_val, buf);
        }

        if (app->audio_out) {
            app->audio_out->SetGain(val / 100.0f);
        }
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
        if (!main_bg || !lbl_player_title || !list_cont || !player_cont) return;
        if (index < 0 || index >= (int)radios.size()) return;

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

    void cleanup_http(bool hard_stop_output = false) {
        if (mp3 && mp3->isRunning()) mp3->stop();
        if (stream_src) stream_src->close();
        if (http_client) http_client->end();
        if (secure_client) secure_client->stop();
        if (plain_client) plain_client->stop();
        if (hard_stop_output && audio_out) {
            audio_out->stop();
            audio_pins_quiet();
        }
        is_playing = false;
        icyMetaInt = 0;
        decoder_fail_streak = 0;
    }

    void cleanup_audio_objects() {
        if (mp3) { delete mp3; mp3 = nullptr; }
        if (audio_out) { delete audio_out; audio_out = nullptr; }
        if (stream_src) { delete stream_src; stream_src = nullptr; }
        if (plain_client) { delete plain_client; plain_client = nullptr; }
        if (secure_client) { delete secure_client; secure_client = nullptr; }
        if (http_client) { delete http_client; http_client = nullptr; }
        audio_inited = false;
    }

    bool init_audio() {
        if (audio_inited) return true;

        if (!audio_out) audio_out = new AudioOutputI2SBuffered();
        if (!audio_out) return false;
        audio_amp_enable(true);
        audio_out->SetRate(44100);
        audio_out->SetBitsPerSample(16);
        audio_out->SetChannels(2);
        audio_out->SetOutputModeMono(true);
        audio_out->SetGain(settings::getVolume() / 100.0f);
        audio_out->SetPinout(I2S_OUT_BCLK, I2S_OUT_WS, I2S_OUT_DIN);
        if (!audio_out->begin()) return false;

        if (!stream_src) stream_src = new AudioFileSourceStream(65536);
        if (!stream_src) return false;
        if (!mp3) mp3 = new AudioGeneratorMP3();
        if (!mp3) return false;
        if (!secure_client) secure_client = new WiFiClientSecure();
        if (!secure_client) return false;
        if (!plain_client) plain_client = new WiFiClient();
        if (!plain_client) return false;
        if (!http_client) http_client = new HTTPClient();
        if (!http_client) return false;

        audio_inited = true;
        return true;
    }

    void load_default_radios() {
        radios.clear();
        radios.reserve(4);

        RadioEntry r1;
        r1.name = "FIP";
        r1.url = "https://icecast.radiofrance.fr/fip-hifi.aac";
        r1.color = 0x4FC3F7;
        r1.btn = nullptr;
        r1.lbl_icon = nullptr;
        radios.push_back(r1);

        RadioEntry r2;
        r2.name = "France Info";
        r2.url = "https://icecast.radiofrance.fr/franceinfo-hifi.aac";
        r2.color = 0xFF7043;
        r2.btn = nullptr;
        r2.lbl_icon = nullptr;
        radios.push_back(r2);

        RadioEntry r3;
        r3.name = "NOVA";
        r3.url = "https://nova.fr/stream";
        r3.color = 0x81C784;
        r3.btn = nullptr;
        r3.lbl_icon = nullptr;
        radios.push_back(r3);
    }

    bool fetch_radio_list() {
        if (WiFi.status() != WL_CONNECTED) return false;

        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(CONNECT_TIMEOUT_MS);

        HTTPClient http;
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        http.setTimeout(READ_TIMEOUT_MS);
        if (!http.begin(client, "https://raw.githubusercontent.com/FallMarsisus/picophone-app-repo/refs/heads/main/radio-list.json")) {
            return false;
        }

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            http.end();
            return false;
        }

        DynamicJsonDocument doc(6144);
        DeserializationError error = deserializeJson(doc, http.getStream());
        http.end();
        if (error) return false;

        JsonArray items = doc["items"].as<JsonArray>();
        if (items.isNull()) return false;

        std::vector<RadioEntry> fetched;
        fetched.reserve(items.size());
        for (JsonObject item : items) {
            RadioEntry r;
            r.name = item["name"].as<String>();
            r.url = item["stream_url"].as<String>();
            if (!r.name.length() || !r.url.length()) continue;

            String colorStr = item["color"].as<String>();
            colorStr.replace("#", "");
            r.color = strtol(colorStr.c_str(), NULL, 16);
            r.btn = nullptr;
            r.lbl_icon = nullptr;
            fetched.push_back(r);
        }

        if (fetched.empty()) return false;
        radios = std::move(fetched);
        return true;
    }

    void rebuild_radio_list_ui() {
        if (!list_cont) return;
        lv_obj_clean(list_cont);

        if (radios.empty()) {
            lv_obj_t* lbl_err = lv_label_create(list_cont);
            lv_label_set_text(lbl_err, "Aucune radio disponible");
            lv_obj_set_style_text_color(lbl_err, lv_color_white(), 0);
            return;
        }

        for (int i = 0; i < (int)radios.size(); i++) {
            radios[i].btn = nullptr;
            radios[i].lbl_icon = nullptr;

            lv_obj_t* item = lv_obj_create(list_cont);
            lv_obj_set_size(item, 292, 54);
            lv_obj_set_style_bg_color(item, lv_color_hex(0x1F1F21), 0);
            lv_obj_set_style_bg_opa(item, LV_OPA_90, 0);
            lv_obj_set_style_radius(item, 12, 0);
            lv_obj_set_style_border_width(item, 0, 0);
            lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);

            lv_obj_set_user_data(item, (void*)(uintptr_t)i);
            lv_obj_add_event_cb(item, radio_clicked_event, LV_EVENT_CLICKED, this);

            lv_obj_t* color_sq = lv_obj_create(item);
            lv_obj_set_size(color_sq, 22, 22);
            lv_obj_set_style_radius(color_sq, 6, 0);
            lv_obj_set_style_bg_color(color_sq, lv_color_hex(radios[i].color), 0);
            lv_obj_set_style_border_width(color_sq, 0, 0);
            lv_obj_align(color_sq, LV_ALIGN_LEFT_MID, 8, 0);
            lv_obj_clear_flag(color_sq, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* lbl_name = lv_label_create(item);
            lv_label_set_text(lbl_name, radios[i].name.c_str());
            lv_obj_set_style_text_color(lbl_name, lv_color_white(), 0);
            lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
            lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 42, 0);

            lv_obj_t* lbl_icon = lv_label_create(item);
            lv_label_set_text(lbl_icon, LV_SYMBOL_PLAY);
            lv_obj_set_style_text_color(lbl_icon, lv_color_hex(0xDDDDDD), 0);
            lv_obj_align(lbl_icon, LV_ALIGN_RIGHT_MID, -8, 0);

            radios[i].btn = item;
            radios[i].lbl_icon = lbl_icon;
        }
    }

public:
    WebRadioApp() {}
    ~WebRadioApp() {
        cleanup_audio_objects();
    }

    void start(lv_obj_t* parent) override {
        main_bg = parent;
        lbl_status = nullptr;
        list_cont = nullptr;
        player_cont = nullptr;
        lbl_player_title = nullptr;
        btn_main_play = nullptr;
        lbl_main_play_icon = nullptr;
        slider_volume = nullptr;
        lbl_volume_val = nullptr;
        play_requested = false;
        is_playing = false;
        force_stop = false;
        exit_requested = false;
        active_radio_index = -1;
        next_radio_index = -1;
        player_expanded = false;
        retry_after_ms = 0;
        retry_count = 0;
        frames_count = 0;
        decoder_fail_streak = 0;
        last_decoder_restart_ms = 0;
        radio_list_dirty = false;
        radio_fetch_requested = true;
        radio_fetch_done = false;
        radio_fetch_next_try_ms = 0;
        set_ui_state(UI_IDLE, 0);

        load_default_radios();

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

        slider_volume = lv_slider_create(player_cont);
        lv_obj_set_size(slider_volume, 220, 26);
        lv_obj_align(slider_volume, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_slider_set_range(slider_volume, 0, 100);
        lv_slider_set_value(slider_volume, settings::getVolume(), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider_volume, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(slider_volume, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_radius(slider_volume, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider_volume, lv_color_white(), LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider_volume, 12, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider_volume, lv_color_white(), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider_volume, LV_OPA_100, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider_volume, 4, LV_PART_KNOB);
        lv_obj_add_event_cb(slider_volume, volume_slider_event, LV_EVENT_VALUE_CHANGED, this);

        lv_obj_t* lbl_vol_icon = lv_label_create(player_cont);
        lv_label_set_text(lbl_vol_icon, LV_SYMBOL_AUDIO);
        lv_obj_set_style_text_color(lbl_vol_icon, lv_color_white(), 0);
        lv_obj_align_to(lbl_vol_icon, slider_volume, LV_ALIGN_OUT_LEFT_MID, -8, 0);

        lbl_volume_val = lv_label_create(player_cont);
        char vol_buf[16];
        snprintf(vol_buf, sizeof(vol_buf), "%u%%", (unsigned)settings::getVolume());
        lv_label_set_text(lbl_volume_val, vol_buf);
        lv_obj_set_style_text_color(lbl_volume_val, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_volume_val, &lv_font_montserrat_14, 0);
        lv_obj_align_to(lbl_volume_val, slider_volume, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

        // --- LISTE DES RADIOS ---
        list_cont = lv_obj_create(main_bg);
        lv_obj_set_size(list_cont, 320, 430); // Plein écran au début
        lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(list_cont, 10, 0);
        lv_obj_set_style_pad_row(list_cont, 12, 0);
        rebuild_radio_list_ui();
    }

    // --------------------------------------------------
    // UI UPDATE (Core 0)
    // --------------------------------------------------
    void update() override {
        if (!main_bg || !lbl_status || !lbl_main_play_icon) return;

        static uint32_t last = 0;
        if (millis() - last < 300) return;
        last = millis();

        if (!play_requested && next_radio_index == -1) {
            set_ui_state(UI_IDLE, 0);
        } else if (is_playing) {
            set_ui_state((frames_count > 0) ? UI_PLAYING : UI_BUFFERING, 0);
            if (audio_out) audio_out->SetGain(settings::getVolume() / 100.0f);
        }

        if (exit_requested && !is_playing && !force_stop) {
            exit_requested = false;
            AppManager::switchTo(APP_HOME);
            return;
        }

        if (radio_list_dirty) {
            radio_list_dirty = false;
            rebuild_radio_list_ui();
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
            if (!radios[i].lbl_icon) continue;
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
            cleanup_http(true);
            force_stop = false;
            retry_after_ms = 0;
            retry_count = 0;
            set_ui_state(UI_IDLE, 0);

            if (next_radio_index != -1) {
                if (next_radio_index < 0 || next_radio_index >= (int)radios.size()) {
                    next_radio_index = -1;
                    active_radio_index = -1;
                    return;
                }
                active_radio_index = next_radio_index;
                url = radios[active_radio_index].url;
                play_requested = true;
                next_radio_index = -1;
            } else {
                active_radio_index = -1;
            }
            return;
        }

        if (radio_fetch_requested && !radio_fetch_done && millis() >= radio_fetch_next_try_ms && !play_requested) {
            bool ok = fetch_radio_list();
            if (ok) {
                radio_fetch_done = true;
                radio_list_dirty = true;
            } else {
                radio_fetch_next_try_ms = millis() + RADIO_FETCH_RETRY_MS;
            }
        }

        if (play_requested && !is_playing) {
            if (millis() < retry_after_ms) return;

            if (!audio_inited && !init_audio()) {
                schedule_retry("Init audio echoue", -201);
                return;
            }

            if (!http_client || !stream_src || !mp3 || !audio_out || !secure_client || !plain_client) {
                schedule_retry("Audio ressources indisponibles", -202);
                return;
            }

            if (!audio_out->begin()) {
                schedule_retry("I2S begin echoue", -203);
                return;
            }

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
            
            while (stream_src->available() < PREBUFFER_TARGET_BYTES && (millis() - prebuf_start < PREBUFFER_TIMEOUT_MS) && !force_stop) {
                stream_src->pumpNetwork();
                yield();
                delay(2);
            }

            if (stream_src->available() < PREBUFFER_MIN_BYTES) {
                schedule_retry("Pre-buffer insuffisant", -210);
                return;
            }

            if (!mp3->begin(stream_src, audio_out)) {
                schedule_retry("Decodeur MP3 echoue", -200);
                return;
            }

            is_playing = true;
            frames_count = 0;
            decoder_fail_streak = 0;
            retry_after_ms = 0;
            retry_count = 0;
            set_ui_state(UI_BUFFERING, 0);
        }

        if (is_playing && mp3) {
            stream_src->pumpNetwork();

            if (mp3->isRunning()) {
                bool decoder_stopped = false;
                if (!force_stop) {
                    if (!mp3->loop()) {
                        decoder_stopped = true;
                        decoder_fail_streak = (uint8_t)std::min((int)decoder_fail_streak + 1, 10);
                    } else {
                        frames_count++;
                        decoder_fail_streak = 0;
                    }
                }

                stream_src->pumpNetwork();

                if (decoder_stopped) {
                    stream_src->pumpNetwork();
                    uint32_t now = millis();
                    // Avoid frequent decoder restarts that can create periodic artifacts.
                    if (decoder_fail_streak >= 2 && stream_src->available() >= 8192 && (now - last_decoder_restart_ms) > 1200) {
                        mp3->stop();
                        mp3->begin(stream_src, audio_out);
                        last_decoder_restart_ms = now;
                        decoder_fail_streak = 0;
                    }
                }
            } else {
                WiFiClient* ns = http_client->getStreamPtr();
                if (!ns || !ns->connected()) {
                    cleanup_http(true);
                    schedule_retry("Flux perdu", -5);
                    return;
                }
                stream_src->pumpNetwork();
                if (stream_src->available() >= 4096) {
                    mp3->begin(stream_src, audio_out);
                    decoder_fail_streak = 0;
                }
            }

            uint32_t last_net = stream_src->lastDataMs();
            if (last_net && (millis() - last_net) > STREAM_STALL_TIMEOUT_MS) {
                cleanup_http(true);
                schedule_retry("Flux bloque", -3);
                return;
            }
        }
    }

    void stop() override {
        play_requested = false;
        force_stop = true;
        next_radio_index = -1;

        cleanup_http(true);

        if (mp3 && mp3->isRunning()) mp3->stop();
        if (audio_out) { audio_out->stop(); }
        audio_pins_quiet();
        
        radios.clear();
        main_bg = nullptr;
        lbl_status = nullptr;
        list_cont = nullptr;
        player_cont = nullptr;
        lbl_player_title = nullptr;
        btn_main_play = nullptr;
        lbl_main_play_icon = nullptr;
        slider_volume = nullptr;
        lbl_volume_val = nullptr;
        exit_requested = false;
        force_stop = false;
    }
};

#endif