#ifndef WEBRADIO_APP_H
#define WEBRADIO_APP_H

#include "App.h"
#include "AppManager.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <BackgroundAudio.h>
#include <I2S.h>

// ======================================================
// CONFIG EXACTE DE L'EXEMPLE
// ======================================================
#define STREAMBUFF (16 * 1024) 

// ======================================================
// POINTEURS GLOBAUX (Le secret pour ne pas crasher le Pico)
// On ne les instancie qu'APRES l'allumage complet du système.
// ======================================================
static I2S* i2s_audio = nullptr;  
static BackgroundAudioMP3Class<RawDataBuffer<STREAMBUFF>>* mp3_decoder = nullptr;
static WiFiClientSecure* secure_client = nullptr; 
static HTTPClient* http_client = nullptr;

// ======================================================
// APP
// ======================================================
class WebRadioApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* btn_play;
    lv_obj_t* lbl_play_icon;
    lv_obj_t* lbl_status;
    lv_obj_t* lbl_station;

    volatile bool play_requested = false;
    volatile bool is_playing     = false;
    volatile bool force_stop     = false;

    // --- VARIABLES EXACTES DE L'EXEMPLE EARLE ---
    uint8_t buff[512]; 
    int icyMetaInt = 0;
    int icyDataLeft = 0;
    String url = "https://ice.audionow.com/485BBCWorld.mp3"; // Station de test Earle

    // --------------------------------------------------
    // UI CALLBACKS
    // --------------------------------------------------
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

    // --------------------------------------------------
    // CLEANUP AUDIO
    // --------------------------------------------------
    void cleanup_audio() {
        if (http_client) http_client->end();
        if (secure_client) secure_client->stop();
        // On ne détruit pas le mp3_decoder, on le met juste en pause
        if (mp3_decoder) mp3_decoder->pause(); 
        is_playing = false;
    }

public:
    WebRadioApp() {}
    ~WebRadioApp() {}

    // --------------------------------------------------
    // START UI
    // --------------------------------------------------
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        play_requested = false;
        is_playing = false;
        force_stop = false;

        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x1a1a2e), 0);

        lv_obj_t* btn_back = lv_btn_create(main_bg);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_back_event, LV_EVENT_CLICKED, this);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_white(), 0);
        lv_obj_center(l_back);

        lbl_station = lv_label_create(main_bg);
        lv_label_set_text(lbl_station, "BBC World (Earle)");
        lv_obj_set_style_text_color(lbl_station, lv_color_white(), 0);
        lv_obj_align(lbl_station, LV_ALIGN_TOP_MID, 0, 40);

        lbl_status = lv_label_create(main_bg);
        lv_label_set_text(lbl_status, "Pret");
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x888888), 0);
        lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 80);

        btn_play = lv_btn_create(main_bg);
        lv_obj_set_size(btn_play, 70, 70);
        lv_obj_set_style_radius(btn_play, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(btn_play, lv_color_hex(0xe94560), 0);
        lv_obj_align(btn_play, LV_ALIGN_CENTER, 0, 40);
        lv_obj_add_event_cb(btn_play, btn_play_event, LV_EVENT_CLICKED, this);

        lbl_play_icon = lv_label_create(btn_play);
        lv_label_set_text(lbl_play_icon, LV_SYMBOL_PLAY);
        lv_obj_center(lbl_play_icon);
    }

    // --------------------------------------------------
    // UI UPDATE (Core 0)
    // --------------------------------------------------
    void update() override {
        static uint32_t last = 0;
        if (millis() - last < 500) return;
        last = millis();

        if (play_requested && is_playing && mp3_decoder) {
            if (mp3_decoder->frames() > 0) {
                lv_label_set_text(lbl_status, "En lecture " LV_SYMBOL_AUDIO);
            } else {
                lv_label_set_text(lbl_status, "Mise en cache...");
            }
        } else if (!play_requested) {
            lv_label_set_text(lbl_status, "Pret");
            lv_label_set_text(lbl_play_icon, LV_SYMBOL_PLAY);
        }
    }

    // --------------------------------------------------
    // AUDIO LOOP (Core 1)
    // --------------------------------------------------
    void update1() override {

        if (force_stop) {
            cleanup_audio();
            force_stop = false;
            return;
        }

        // ========= START STREAM =========
        if (play_requested && !is_playing) {

            Serial.println("[WebRadio] Connexion HTTPS...");
            
            // On instancie les objets proprement UNE SEULE FOIS sans crasher le processeur
            if (i2s_audio == nullptr) {
                i2s_audio = new I2S(OUTPUT, 6, 14);
                mp3_decoder = new BackgroundAudioMP3Class<RawDataBuffer<STREAMBUFF>>(*i2s_audio);
                secure_client = new WiFiClientSecure();
                http_client = new HTTPClient();
            }

            secure_client->setInsecure();
            
            http_client->end();
            http_client->begin(*secure_client, url);
            http_client->setReuse(true);
            http_client->setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            
            // Métadonnées
            const char *icyHdrs[] = { "icy-metaint" }; 
            http_client->collectHeaders(icyHdrs, 1);
            http_client->addHeader("Icy-MetaData", "1");

            int code = http_client->GET();
            if (code != HTTP_CODE_OK) {
                Serial.printf("[WebRadio] HTTP erreur %d\n", code);
                cleanup_audio();
                play_requested = false;
                return;
            }

            if (http_client->hasHeader("icy-metaint")) {
                icyMetaInt = http_client->header("icy-metaint").toInt();
                icyDataLeft = icyMetaInt;
            } else {
                icyMetaInt = 0;
            }

            Serial.println("[WebRadio] Flux MP3 valide. Demarrage decodeur...");

            mp3_decoder->begin();
            mp3_decoder->setGain(0.8f);
            is_playing = true;
        }

        // ========= STOP =========
        else if (!play_requested && is_playing) {
            cleanup_audio();
            Serial.println("[WebRadio] Stop");
        }

        // ========= STREAM PUMP (Moteur Earle) =========
        if (is_playing) {

            WiFiClient* stream = http_client->getStreamPtr();
            if (!stream || !stream->connected()) {
                Serial.println("[WebRadio] Flux perdu");
                cleanup_audio();
                play_requested = true;
                return;
            }

            do {
                if (force_stop) break;

                size_t httpavail = stream->available();
                httpavail = std::min(sizeof(buff), httpavail); 
                size_t mp3avail = mp3_decoder->availableForWrite();
                
                if (!httpavail || !mp3avail) {
                    break;
                }
                
                size_t toRead = std::min(mp3avail, httpavail); 
                if (icyMetaInt) {
                    toRead = std::min(toRead, (size_t)icyDataLeft);
                }
                
                int read = stream->read(buff, toRead);
                if (read < 0) {
                    break; 
                }
                
                mp3_decoder->write(buff, read);

                if (mp3_decoder->available() < 1024) {
                    mp3_decoder->pause();
                } else if (mp3_decoder->paused() && mp3_decoder->available() > (STREAMBUFF / 2)) { 
                    mp3_decoder->unpause();
                }

                icyDataLeft -= read;
                
                // LE FILTRE ANTI-FRITURE
                if (icyMetaInt && !icyDataLeft) {
                    while (!stream->available() && stream->connected() && !force_stop) {
                        delay(1);
                    }
                    if (!stream->connected() || force_stop) {
                        break;
                    }
                    
                    int totalCnt = stream->read() * 16;
                    int cnt = totalCnt;

                    int buffCnt = std::min(sizeof(buff), (size_t)cnt); 
                    uint8_t *p = buff;
                    while (buffCnt && stream->connected() && !force_stop) {
                        read = stream->read(p, buffCnt);
                        p += read;
                        buffCnt -= read;
                        cnt -= read;
                    }

                    // On jette le reste
                    while (cnt && stream->connected() && !force_stop) {
                        stream->read(); 
                        cnt--;
                    }
                    
                    icyDataLeft = icyMetaInt;
                }
            } while (true);
            
            delay(2);
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

        main_bg = nullptr;
    }
};

#endif