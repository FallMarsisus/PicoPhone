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
#include "../system/Settings.h"

// ======================================================
// I2S avec DMA : 32 petits buffers de 256 words (au lieu de 6x64).
// Chaque buffer = 256/44100 = 5.8ms.  Le DMA reçoit un nouveau
// buffer toutes les ~5.8ms au lieu de ~46ms.  Plus de micro-gaps.
// ConsumeSample bloquant : le décodeur tourne au rythme du DAC.
// ======================================================
class AudioOutputI2SBuffered : public AudioOutputI2S {
public:
    using AudioOutputI2S::AudioOutputI2S;
    bool begin() override {
        if (!i2sOn) {
            i2s.setBuffers(32, 256); // 32x256 = 8192 words = ~186ms @44.1kHz
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
        // Ecriture BLOQUANTE → le write() attend qu'un buffer DMA
        // soit libre.  Le décodeur tourne exactement au rythme du DAC.
        // Pas de spin-wait, pas de micro-gaps.
        return !!i2s.write((int32_t)s32, true);
    }
};

LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_18);

// ======================================================
// SOURCE AUDIO : ring buffer auto-alimenté depuis WiFiClient
// Le décodeur MP3 appelle read() qui tire automatiquement du réseau.
// ICY metadata géré par machine à état non-bloquante.
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
        // Si buffer vide, qq tentatives rapides sans bloquer longtemps
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
        uint8_t tmp[1460];  // MTU-sized chunks pour max throughput
        int rounds = 0;

        while (rounds++ < 96) {
            int avail = _net->available();
            if (avail <= 0) break;

            // --- Machine à état ICY (non-bloquante) ---
            if (_icyMetaInt && _icyState == ICY_META_LEN) {
                int b = _net->read();
                if (b < 0) break;
                _icyMetaRemain = b * 16;
                _icyState = (_icyMetaRemain > 0) ? ICY_META_SKIP : ICY_AUDIO;
                _icyDataLeft = _icyMetaInt;
                continue;
            }

            if (_icyMetaInt && _icyState == ICY_META_SKIP) {
                // Jeter les octets de metadata sans bloquer
                int toSkip = std::min(_icyMetaRemain, (int)_net->available());
                if (toSkip <= 0) break;
                // Lire et jeter par blocs
                while (toSkip > 0) {
                    int chunk = std::min(toSkip, (int)sizeof(tmp));
                    int rd = _net->read(tmp, chunk);
                    if (rd <= 0) break;
                    toSkip -= rd;
                    _icyMetaRemain -= rd;
                }
                if (_icyMetaRemain <= 0) {
                    _icyState = ICY_AUDIO;
                }
                continue;
            }

            // --- Lecture audio normale ---
            uint32_t space = _bufSize - _length;
            if (!space) break;

            size_t toRead = std::min((size_t)avail, sizeof(tmp));
            toRead = std::min(toRead, (size_t)space);
            if (_icyMetaInt) toRead = std::min(toRead, (size_t)_icyDataLeft);
            if (!toRead) break;

            int rd = _net->read(tmp, toRead);
            if (rd <= 0) break;
            _lastDataMs = millis();

            // Écrire dans le ring buffer
            uint32_t toWrite = std::min((uint32_t)rd, space);
            uint32_t first = std::min(toWrite, _bufSize - _writePtr);
            memcpy(_buf + _writePtr, tmp, first);
            if (toWrite > first) memcpy(_buf, tmp + first, toWrite - first);
            _writePtr = (_writePtr + toWrite) % _bufSize;
            _length += toWrite;

            if (_icyMetaInt) {
                _icyDataLeft -= rd;
                if (_icyDataLeft <= 0) {
                    _icyState = ICY_META_LEN;
                }
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
// APP
// ======================================================
class WebRadioApp : public App {
private:
    enum RadioUiState : uint8_t {
        UI_IDLE = 0,
        UI_CONNECTING,
        UI_BUFFERING,
        UI_PLAYING,
        UI_RETRY_WAIT,
        UI_ERROR
    };

    lv_obj_t* main_bg;
    lv_obj_t* btn_play;
    lv_obj_t* lbl_play_icon;
    lv_obj_t* lbl_status;
    lv_obj_t* lbl_station;

    volatile bool play_requested = false;
    volatile bool is_playing     = false;
    volatile bool force_stop     = false;

    static constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
    static constexpr uint32_t READ_TIMEOUT_MS = 5000;
    static constexpr uint32_t STREAM_STALL_TIMEOUT_MS = 12000;
    static constexpr uint32_t RETRY_BASE_DELAY_MS = 1200;
    static constexpr uint32_t RETRY_MAX_DELAY_MS = 15000;
    int icyMetaInt = 0;
    String url = "http://icecast.radiofrance.fr/fip-midfi.mp3";

    volatile uint8_t ui_state = UI_IDLE;
    volatile int ui_last_error = 0;
    uint32_t retry_after_ms = 0;
    uint8_t retry_count = 0;

    // Audio ESP8266Audio (même lib que i2s_play_test_tone)
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
        AppManager::switchTo(APP_HOME);
    }

    static void btn_play_event(lv_event_t* e) {
        WebRadioApp* app = (WebRadioApp*)lv_event_get_user_data(e);
        if (app->play_requested) {
            app->play_requested = false;
            lv_label_set_text(app->lbl_play_icon, LV_SYMBOL_PLAY);
            lv_label_set_text(app->lbl_status, "Arret...");
        } else {
            if (WiFi.status() == WL_CONNECTED) {
                app->play_requested = true;
                lv_label_set_text(app->lbl_play_icon, LV_SYMBOL_STOP);
                lv_label_set_text(app->lbl_status, "Connexion...");
            } else {
                lv_label_set_text(app->lbl_status, "Pas de Wi-Fi");
            }
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
        Serial.printf("[WebRadio] Audio init OK (BCLK=%d WS=%d DIN=%d swap=%d)\n",
                      I2S_OUT_BCLK, I2S_OUT_WS, I2S_OUT_DIN, wantSwap);
        return true;
    }

public:
    WebRadioApp() {}
    ~WebRadioApp() {}

    void start(lv_obj_t* parent) override {
        main_bg = parent;
        play_requested = false;
        is_playing = false;
        force_stop = false;
        retry_after_ms = 0;
        retry_count = 0;
        frames_count = 0;
        set_ui_state(UI_IDLE, 0);

        if (!init_audio()) {
            set_ui_state(UI_ERROR, -201);
            return;
        }

        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x000000), 0);

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
        lv_obj_add_event_cb(btn_back, go_back_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_hex(0x007AFF), 0);
        lv_obj_center(l_back);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Radio");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_center(title);

        lbl_station = lv_label_create(main_bg);
        lv_label_set_text(lbl_station, "France Inter");
        lv_obj_set_style_text_color(lbl_station, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl_station, &lv_font_montserrat_18, 0);
        lv_obj_align(lbl_station, LV_ALIGN_TOP_MID, 0, 70);

        lbl_status = lv_label_create(main_bg);
        lv_label_set_text(lbl_status, "Pret");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 105);

        btn_play = lv_btn_create(main_bg);
        lv_obj_set_size(btn_play, 80, 80);
        lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn_play, lv_color_hex(0x0A84FF), 0);
        lv_obj_align(btn_play, LV_ALIGN_CENTER, 0, 20);
        lv_obj_add_event_cb(btn_play, btn_play_event, LV_EVENT_CLICKED, this);

        lbl_play_icon = lv_label_create(btn_play);
        lv_label_set_text(lbl_play_icon, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(lbl_play_icon, lv_color_white(), 0);
        lv_obj_center(lbl_play_icon);
    }

    // --------------------------------------------------
    // UI UPDATE (Core 0)
    // --------------------------------------------------
    void update() override {
        static uint32_t last = 0;
        if (millis() - last < 500) return;
        last = millis();

        if (!play_requested) {
            set_ui_state(UI_IDLE, 0);
        } else if (is_playing) {
            set_ui_state((frames_count > 0) ? UI_PLAYING : UI_BUFFERING, 0);
            // Suivre le volume global
            if (audio_out) audio_out->SetGain(settings::getVolume() / 100.0f);
        }

        switch (ui_state) {
            case UI_CONNECTING:
                lv_label_set_text(lbl_status, "Connexion...");
                break;
            case UI_BUFFERING:
                lv_label_set_text(lbl_status, "Mise en cache...");
                break;
            case UI_PLAYING:
                lv_label_set_text(lbl_status, "En lecture " LV_SYMBOL_AUDIO);
                break;
            case UI_RETRY_WAIT: {
                uint32_t now = millis();
                uint32_t remaining = (retry_after_ms > now) ? (retry_after_ms - now) : 0;
                char msg[64];
                snprintf(msg, sizeof(msg), "Reconnexion %lus", (unsigned long)((remaining + 999) / 1000));
                lv_label_set_text(lbl_status, msg);
                break;
            }
            case UI_ERROR: {
                char msg[64];
                snprintf(msg, sizeof(msg), "%s (%d)", map_http_error(ui_last_error), ui_last_error);
                lv_label_set_text(lbl_status, msg);
                break;
            }
            case UI_IDLE:
            default:
                lv_label_set_text(lbl_status, "Pret");
                break;
        }
        lv_label_set_text(lbl_play_icon, play_requested ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
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
            return;
        }

        // ========= START STREAM =========
        if (play_requested && !is_playing) {

            if (millis() < retry_after_ms) return;

            if (WiFi.status() != WL_CONNECTED) {
                schedule_retry("WiFi indisponible", -100);
                return;
            }

            set_ui_state(UI_CONNECTING, 0);

            bool is_https = url.startsWith("https://");
            Serial.printf("[WebRadio] Connexion %s...\n", is_https ? "HTTPS" : "HTTP");
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
                Serial.printf("[WebRadio] HTTP erreur %d\n", code);
                schedule_retry("Echec connexion HTTP", code);
                return;
            }

            if (http_client->hasHeader("icy-metaint")) {
                icyMetaInt = http_client->header("icy-metaint").toInt();
            } else {
                icyMetaInt = 0;
            }

            Serial.println("[WebRadio] Flux OK, pre-buffering...");

            // Connecter la source au flux réseau (gère ICY en interne)
            stream_src->setStream(http_client->getStreamPtr(), icyMetaInt);

            // Pré-remplir 16 Ko avant de démarrer le décodeur
            set_ui_state(UI_BUFFERING, 0);
            uint32_t prebuf_start = millis();
            while (stream_src->available() < 16384 && (millis() - prebuf_start < 10000) && !force_stop) {
                stream_src->pumpNetwork();
                yield();
                delay(2);
            }
            Serial.printf("[WebRadio] Pre-buffer: %u octets\n", (unsigned)stream_src->available());

            if (stream_src->available() < 4096) {
                schedule_retry("Pre-buffer insuffisant", -210);
                return;
            }

            if (!mp3->begin(stream_src, audio_out)) {
                Serial.println("[WebRadio] Echec mp3->begin()");
                schedule_retry("Decodeur MP3 echoue", -200);
                return;
            }

            is_playing = true;
            frames_count = 0;
            retry_after_ms = 0;
            retry_count = 0;
            set_ui_state(UI_BUFFERING, 0);
            Serial.println("[WebRadio] Decodeur demarre");
        }

        // ========= STOP =========
        else if (!play_requested && is_playing) {
            cleanup_http();
            retry_after_ms = 0;
            retry_count = 0;
            set_ui_state(UI_IDLE, 0);
            Serial.println("[WebRadio] Stop");
        }

        // ========= BOUCLE PRINCIPALE =========
        // Décoder par batch avec pompage réseau intercalé.
        // Puis RETOURNER pour laisser FreeRTOS/lwIP tourner.
        if (is_playing && mp3) {

            // Toujours pomper le réseau en premier
            stream_src->pumpNetwork();

            if (mp3->isRunning()) {
                // ConsumeSample est BLOQUANT : le décodeur tourne
                // exactement au rythme du DAC (44.1kHz).
                // mp3->loop() décode et pousse des samples en continu
                // jusqu'à ce que le ring buffer soit vide.
                // On appelle en boucle avec un budget temps, puis on
                // retourne pour laisser loop1() tourner.
                bool decoder_stopped = false;
                uint32_t t_end = millis() + 100; // 100ms budget

                while (mp3->isRunning() && !force_stop && millis() < t_end) {
                    if (!mp3->loop()) {
                        decoder_stopped = true;
                        break;
                    }
                    frames_count++;
                }

                stream_src->pumpNetwork();

                // Si le décodeur s'est arrêté (read() a renvoyé 0)
                if (decoder_stopped) {
                    Serial.printf("[WebRadio] Decodeur stop, buf=%u\n",
                                  (unsigned)stream_src->available());
                    // Pomper et relancer si assez de données
                    stream_src->pumpNetwork();
                    if (stream_src->available() >= 4096) {
                        mp3->stop();
                        mp3->begin(stream_src, audio_out);
                        Serial.printf("[WebRadio] Relance, buf=%u\n",
                                      (unsigned)stream_src->available());
                    }
                    // Sinon : on retourne, le prochain cycle pompera plus
                }

                // Stats toutes les 2s
                static uint32_t last_stats_ms = 0;
                uint32_t now_ms = millis();
                if (now_ms - last_stats_ms > 2000) {
                    last_stats_ms = now_ms;
                    Serial.printf("[WebRadio] buf=%u/%u frames=%lu\n",
                                  (unsigned)stream_src->available(),
                                  65536u,
                                  (unsigned long)frames_count);
                }
            } else {
                // Décodeur arrêté — vérifier le flux
                WiFiClient* ns = http_client->getStreamPtr();
                if (!ns || !ns->connected()) {
                    Serial.println("[WebRadio] Flux perdu");
                    cleanup_http();
                    schedule_retry("Flux perdu", -5);
                    return;
                }
                // Pomper et relancer si buffer suffisant
                stream_src->pumpNetwork();
                if (stream_src->available() >= 4096) {
                    mp3->begin(stream_src, audio_out);
                    Serial.printf("[WebRadio] Relance apres arret, buf=%u\n",
                                  (unsigned)stream_src->available());
                }
                // Pas de delay() — on retourne et on repompe au cycle suivant
            }

            // Stall check
            uint32_t last_net = stream_src->lastDataMs();
            if (last_net && (millis() - last_net) > STREAM_STALL_TIMEOUT_MS) {
                cleanup_http();
                schedule_retry("Flux bloque", -3);
                return;
            }
        }
    }

    // --------------------------------------------------
    // STOP APP
    // --------------------------------------------------
    void stop() override {
        play_requested = false;
        force_stop = true;

        uint32_t t = millis();
        while (force_stop && millis() - t < 500) {
            delay(5);
        }

        if (mp3 && mp3->isRunning()) mp3->stop();
        if (audio_out) { audio_out->flush(); audio_out->stop(); }
        audio_pins_quiet();

        main_bg = nullptr;
    }
};

#endif