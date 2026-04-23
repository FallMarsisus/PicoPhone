#pragma once

#include "../App.h"
#include "../AppManager.h"
#include <lvgl.h>
#include <Arduino.h>
#include <I2S.h>
#include <AudioOutputI2S.h>
#include <hardware/watchdog.h>
#include "../Hardware.h"
#include "../system/UnifiedContacts.h"

LV_FONT_DECLARE(lv_font_montserrat_14);

class PhoneApp : public App {
private:
    lv_obj_t * root = nullptr;
    lv_obj_t * ta_number = nullptr;
    lv_obj_t * btnm_dial = nullptr;
    lv_obj_t * lbl_status = nullptr;

    float lp_spk = 0.0f;

    String serial_buffer;

    bool call_pending = false;
    bool hangup_pending = false;
    bool answer_pending = false;
    String pending_digits;

    // --- AUDIO ---
    I2S * i2sIn = nullptr;
    AudioOutputI2S * out = nullptr;

    volatile bool bridge_requested = false;
    volatile bool bridge_active = false;

    float dc_offset = 2048.0f;

    // UI callback
    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void btnm_event_cb(lv_event_t * e) {
        PhoneApp * app = (PhoneApp *)lv_event_get_user_data(e);
        lv_obj_t * obj = lv_event_get_target(e);
        uint32_t id = lv_btnmatrix_get_selected_btn(obj);
        const char * txt = lv_btnmatrix_get_btn_text(obj, id);
        if (!txt) return;

        if (strcmp(txt, LV_SYMBOL_CALL " Appel") == 0) {
            app->pending_digits = lv_textarea_get_text(app->ta_number);
            app->call_pending = true;
        } else if (strcmp(txt, "Fin") == 0) {
            app->hangup_pending = true;
        } else if (strcmp(txt, "Rep.") == 0) {
            app->answer_pending = true;
        } else if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
            lv_textarea_del_char(app->ta_number);
        } else {
            lv_textarea_add_text(app->ta_number, txt);
        }
    }

    void _doCallNumber(const char * num) {
        if (!num || !*num) {
            lv_label_set_text(lbl_status, "Entrez un numero");
            return;
        }
        const String display = unified_contacts::display_name_for_phone(String(num), String(num));
        lv_label_set_text_fmt(lbl_status, "Appel: %s", display.c_str());
        while (Serial1.available()) Serial1.read();
        Serial1.print("ATD");
        Serial1.print(num);
        Serial1.println(";");
        bridge_requested = true;
    }

    void _doHangUp() {
        lv_label_set_text(lbl_status, "Raccroche...");
        Serial1.println("ATH");
        bridge_requested = false;
    }

    void _doAnswerCall() {
        lv_label_set_text(lbl_status, "Decrochage...");
        Serial1.println("ATA");
        bridge_requested = true;
    }

public:
    PhoneApp() {}
    ~PhoneApp() {}

    void start(lv_obj_t* parent) override {
        serial_buffer = "";
        bridge_requested = false;
        bridge_active = false;
        call_pending = hangup_pending = answer_pending = false;
        pending_digits = "";

        lv_obj_set_style_bg_color(parent, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

        root = lv_obj_create(parent);
        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(root, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(root, 0, 0);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

        // Header (style Settings/Timer)
        lv_obj_t* header = lv_obj_create(root);
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
        lv_label_set_text(title, "Telephone");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_center(title);

        lbl_status = lv_label_create(root);
        lv_label_set_text(lbl_status, "A7670E Pret");
        lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 60);
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x8E8E93), 0);

        ta_number = lv_textarea_create(root);
        lv_textarea_set_one_line(ta_number, true);
        lv_textarea_set_align(ta_number, LV_TEXT_ALIGN_CENTER);
        lv_obj_set_size(ta_number, 280, 50);
        lv_obj_align(ta_number, LV_ALIGN_TOP_MID, 0, 90);
        lv_obj_set_style_bg_color(ta_number, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_text_color(ta_number, lv_color_white(), 0);
        lv_obj_set_style_border_color(ta_number, lv_color_hex(0x3A3A3C), 0);

        btnm_dial = lv_btnmatrix_create(root);
        lv_obj_set_size(btnm_dial, 300, 295);
        lv_obj_align(btnm_dial, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(btnm_dial, lv_color_hex(0x111111), 0);
        lv_obj_set_style_border_width(btnm_dial, 0, 0);
        lv_obj_set_style_bg_color(btnm_dial, lv_color_hex(0x2C2C2E), LV_PART_ITEMS);
        lv_obj_set_style_text_color(btnm_dial, lv_color_white(), LV_PART_ITEMS);

        static const char * map[] = {
            "1","2","3","\n",
            "4","5","6","\n",
            "7","8","9","\n",
            "+","0","#","\n",
            LV_SYMBOL_CALL " Appel","Rep.","Fin","\n",
            LV_SYMBOL_BACKSPACE,""
        };

        lv_btnmatrix_set_map(btnm_dial, map);
        lv_obj_add_event_cb(btnm_dial, btnm_event_cb, LV_EVENT_VALUE_CHANGED, this);
    }

    // --- CORE 0 ---
    void update() override {
        if (call_pending) {
            _doCallNumber(pending_digits.c_str());
            call_pending = false;
            pending_digits = "";
        }
        if (hangup_pending) {
            _doHangUp();
            hangup_pending = false;
        }
        if (answer_pending) {
            _doAnswerCall();
            answer_pending = false;
        }

        // Start audio bridge
        if (bridge_requested && !bridge_active) {
            analogReadResolution(12);
            analogWriteResolution(8);
            analogWriteRange(255);
            analogWriteFreq(62500);

            pinMode(26, INPUT);
            pinMode(27, OUTPUT);

            if (!i2sIn) i2sIn = new I2S(INPUT);
            if (!out)   out   = new AudioOutputI2SRP();

            if (!i2sIn || !out) {
                bridge_requested = false;
                return;
            }

            i2sIn->swapClocks();
            i2sIn->setBCLK(I2S_IN_WS);
            i2sIn->setDATA(I2S_IN_DOUT);
            i2sIn->setBitsPerSample(32);
            i2sIn->begin(8000);

            out->SetRate(8000);
            out->SetBitsPerSample(16);
            out->SetChannels(2);
            out->SetOutputModeMono(true);
            out->SetGain(1.0f);
            audio_output_set_pinout(*out);
            audio_amp_enable(true);
            out->begin();

            dc_offset = 2048.0f;
            bridge_active = true;
        }

        // Stop audio bridge
        if (!bridge_requested && bridge_active) {
            if (i2sIn) i2sIn->end();
            if (out)   out->stop();
            analogWrite(27, 0);
            pinMode(27, INPUT);
            audio_pins_quiet();
            bridge_active = false;
        }

        // SIM800 serial
        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                serial_buffer.trim();
                if (serial_buffer.length()) {
                    lv_label_set_text_fmt(lbl_status, "> %s", serial_buffer.c_str());
                    if (serial_buffer.indexOf("NO CARRIER") >= 0 ||
                        serial_buffer.indexOf("BUSY") >= 0) {
                        bridge_requested = false;
                    }
                }
                serial_buffer = "";
            } else if (c != '\r') {
                serial_buffer += c;
            }
        }
    }

    // --- CORE 1 : AUDIO ---
   // --- PONT AUDIO HAUTE VITESSE (Cœur 1) ---
    // --- PONT AUDIO HAUTE VITESSE (Cœur 1) ---
    void update1() override {
        if (bridge_requested && !bridge_active) {
            analogReadResolution(12);
            analogWriteResolution(8);
            analogWriteFreq(32000); 
            pinMode(26, INPUT);     
            pinMode(27, OUTPUT);    

            if (!i2sIn) i2sIn = new I2S(INPUT);
            if (!out)   out   = new AudioOutputI2SRP();

            if (!i2sIn || !out) {
                bridge_requested = false;
                return;
            }

            i2sIn->swapClocks();
            i2sIn->setBCLK(I2S_IN_WS);
            i2sIn->setDATA(I2S_IN_DOUT);
            i2sIn->setBitsPerSample(32);
            i2sIn->begin(8000); 

            out->SetRate(8000);
            out->SetBitsPerSample(16);
            out->SetChannels(2);
            out->SetOutputModeMono(true);
            out->SetGain(1.0f); 
            audio_output_set_pinout(*out);
            audio_amp_enable(true);
            out->begin();

            bridge_active = true;
        } 
        else if (!bridge_requested && bridge_active) {
            if (i2sIn) { i2sIn->end(); delete i2sIn; i2sIn = nullptr; }
            if (out)   { out->stop(); delete out; out = nullptr; }
            analogWrite(27, 0); 
            pinMode(27, INPUT);
            audio_pins_quiet();
            bridge_active = false;
        }

        if (!bridge_active || !i2sIn || !out) return;

        while (i2sIn->available()) {
            int32_t l32 = 0, r32 = 0;
            i2sIn->read32(&l32, &r32); 

            // --- A. MICRO -> SIM800L (Votre voix vers le réseau) ---
            int16_t mic_sample = (int16_t)(l32 >> 16);
            int pwm_val = 128 + (mic_sample / 128); 
            if (pwm_val < 0) pwm_val = 0; 
            if (pwm_val > 255) pwm_val = 255;
            analogWrite(27, pwm_val);

            // --- B. SIM800L -> HAUT-PARLEUR (La voix de votre correspondant) ---
            int adc = analogRead(26);

            // 1. Filtre Passe-Haut (Enlève la tension continue de 1.4V du SIM800L)
            static float last_adc = 0;
            static float high_pass = 0;
            high_pass = 0.98f * high_pass + (float)adc - last_adc;
            last_adc = (float)adc;

            // 2. Filtre Passe-Bas (Enlève le souffle et les grésillements aigus)
            static float low_pass = 0;
            low_pass = low_pass + 0.3f * (high_pass - low_pass); 

            // Gain (Boost du volume, ajustez entre 10 et 20 si c'est trop fort/faible)
            int32_t spk = (int32_t)(low_pass * 15.0f); 

            // Limiteur doux pour ne pas saturer l'I2S
            if (spk > 32000) spk = 32000;
            if (spk < -32000) spk = -32000;

            int16_t s = (int16_t)spk;
            int16_t stereo[2] = { s, s };

            out->ConsumeSample(stereo);
        }
    }
};