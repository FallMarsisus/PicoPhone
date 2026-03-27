#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <SDFS.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/xosc.h"
#include "hardware/watchdog.h"
#include <AudioOutputI2S.h>
#include <I2S.h> 
#include <hardware/vreg.h>
#include <Wire.h> 
#include "assets/startuplogo.c"
#include "system/Battery.h"
// Ajout pour accès à manager
#include "AppManager.h"
extern AppManager manager;

// --- PINS ECRAN ---
#define LCD_CS_PIN  9
#define LCD_DC_PIN  8    // GP8
#define LCD_RST_PIN 13
#define LCD_BL_PIN  15

// --- PINS TACTILE CAPACITIF (FT6336U) ---
#define TP_SDA 16  // I2C0
#define TP_SCL 17  // I2C0
#define TP_INT 18
#define TP_RST 19
#define FT6336U_ADDR 0x38

#define SD_CS_PIN 22 

// Pour le MAX98357A (Sortie)
#define I2S_OUT_BCLK 6
#define I2S_OUT_WS   7 
#define I2S_OUT_DIN  14

// Pour le INMP441 (Entrée)
#define I2S_IN_BCLK  2
#define I2S_IN_WS    3
#define I2S_IN_DOUT  4

// Pour le SIM800L / A7670E (UART0)
#define SIM800_TX    0 
#define SIM800_RX    1 
#define A7670_PWRKEY 26 

// --- BOUTON VEILLE (GPIO21) ---
#define SLEEP_BTN_PIN 21

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static constexpr uint32_t LV_BUF_PIXELS = 320u * 100u;
static lv_color_t buf1[LV_BUF_PIXELS];
static lv_color_t buf2[LV_BUF_PIXELS];

// Mutex global
auto_init_mutex(spi_mutex);

static inline void audio_pins_quiet() {
    pinMode(I2S_OUT_DIN, INPUT_PULLDOWN);
    pinMode(I2S_OUT_BCLK, INPUT_PULLDOWN);
    pinMode(I2S_OUT_WS, INPUT_PULLDOWN);
    pinMode(I2S_IN_DOUT, INPUT_PULLDOWN);
    pinMode(I2S_IN_BCLK, INPUT_PULLDOWN);
    pinMode(I2S_IN_WS, INPUT_PULLDOWN);
}
// Variable globale pour prévenir le Core 1 de l'extinction
volatile bool system_is_shutting_down = false;

void system_power_off() {
    Serial.println("[POWER] Extinction matérielle en cours...");
    
    // 1. Prévenir le Core 1 de tout arrêter immédiatement
    system_is_shutting_down = true;
    delay(50); 

    // NOUVEAU : 2. COUPURE DU WIFI ET BLUETOOTH DU PICO W
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    LTE::setLowPower(true);
    watchdog_update(); 

    // 3. EXTINCTION DU MODEM A7670E (Correction du bug de rallumage)
    Serial.println("[LTE] Envoi de la commande Power Down...");
    Serial1.println("AT+CPOWD=1"); 
    
    // On attend poliment que le modem nous dise qu'il est bien éteint (Max 5s)
    unsigned long wait_start = millis();
    while (millis() - wait_start < 5000) {
        watchdog_update();
        if (Serial1.available()) {
            String resp = Serial1.readStringUntil('\n');
            if (resp.indexOf("NORMAL POWER DOWN") != -1) break;
        }
    }
    
    // On ne touche PLUS au PWRKEY ici ! On le met juste en haute impédance.

    // 4. ANTI-ALIMENTATION PARASITE (CRUCIAL !)
    Serial1.end();
    pinMode(SIM800_TX, INPUT);
    pinMode(SIM800_RX, INPUT);
    pinMode(A7670_PWRKEY, INPUT);

    // 5. COUPURE DE L'AUDIO
    audio_pins_quiet(); 
    // IMPORTANT : Si vous avez relié SD_MODE du MAX98357 à un pin (ex: GP20)
    // pinMode(20, OUTPUT); digitalWrite(20, LOW); // Force le Shutdown total de l'ampli

    // 6. EXTINCTION DE L'ÉCRAN ET DU TACTILE
    digitalWrite(LCD_BL_PIN, LOW);
    tft.writecommand(0x28); // Display OFF
    tft.writecommand(0x10); // Sleep IN
    
    // NOUVEAU : Coupure matérielle du tactile
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);
    delay(100);

    // 7. ARRÊT DES BUS
    SPI1.end();
    Wire.end();

    Serial.println("[POWER] CPU Zzz...");
    Serial.flush(); 

    // 8. BOUCLE DE VEILLE SÉCURISÉE (Core 0)
    while (true) {
        watchdog_update();
        __wfi(); // Met le CPU en pause. Note: le RP2040 tirera encore ~10mA ici car les horloges (PLL) tournent encore.

        if (digitalRead(SLEEP_BTN_PIN) == LOW) {
            uint32_t press_time = millis();
            bool valid_press = true;

            while (millis() - press_time < 1000) {
                watchdog_update();
                if (digitalRead(SLEEP_BTN_PIN) == HIGH) {
                    valid_press = false; 
                    break; 
                }
                delay(10);
            }

            if (valid_press) {
                Serial.println("[POWER] Rallumage !");
                watchdog_reboot(0, 0, 0);
                while(true);
            }
        }
    }
}

static void btn_poweroff_event_cb(lv_event_t * e) {
    system_power_off(); 
}

static void btn_reboot_event_cb(lv_event_t * e) {
    Serial.println("Redémarrage en cours...");
    watchdog_reboot(0, 0, 0); 
    while (true);
}

static void btn_cancel_event_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    lv_obj_t * modal = lv_obj_get_parent(lv_obj_get_parent(btn)); 
    lv_obj_del(modal); 
}



// --- CRÉATION DE L'INTERFACE ---

void showPowerMenu() {
    // 1. Fond noir semi-transparent qui couvre tout l'écran
    lv_obj_t * modal_bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal_bg, 320, 480);
    lv_obj_set_style_bg_color(modal_bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(modal_bg, LV_OPA_70, 0);
    lv_obj_set_style_border_width(modal_bg, 0, 0);
    lv_obj_set_style_radius(modal_bg, 0, 0);
    lv_obj_clear_flag(modal_bg, LV_OBJ_FLAG_SCROLLABLE);

    // 2. Le conteneur du menu au centre
    lv_obj_t * menu_box = lv_obj_create(modal_bg);
    lv_obj_set_size(menu_box, 260, LV_SIZE_CONTENT);
    lv_obj_center(menu_box);
    lv_obj_set_style_bg_color(menu_box, lv_color_hex(0x222222), 0);
    lv_obj_set_style_border_color(menu_box, lv_color_hex(0x444444), 0);
    lv_obj_set_style_border_width(menu_box, 1, 0);
    lv_obj_set_style_radius(menu_box, 15, 0);
    lv_obj_set_flex_flow(menu_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(menu_box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(menu_box, 20, 0);

    // Titre
    lv_obj_t * title = lv_label_create(menu_box);
    lv_label_set_text(title, "Options d'alimentation");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_pad_bottom(title, 20, 0);

    // --- Bouton Éteindre ---
    lv_obj_t * btn_off = lv_btn_create(menu_box);
    lv_obj_set_size(btn_off, 200, 50);
    lv_obj_set_style_bg_color(btn_off, lv_color_hex(0xFF3B30), 0); // Rouge iOS
    lv_obj_add_event_cb(btn_off, btn_poweroff_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_off = lv_label_create(btn_off);
    lv_label_set_text(lbl_off, LV_SYMBOL_POWER " Eteindre");
    lv_obj_set_style_text_font(lbl_off, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_off);

    // --- Bouton Redémarrer ---
    lv_obj_t * btn_reboot = lv_btn_create(menu_box);
    lv_obj_set_size(btn_reboot, 200, 50);
    lv_obj_set_style_bg_color(btn_reboot, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(btn_reboot, btn_reboot_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_reboot = lv_label_create(btn_reboot);
    lv_label_set_text(lbl_reboot, LV_SYMBOL_LOOP " Redemarrer");
    lv_obj_set_style_text_font(lbl_reboot, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_reboot);

    // --- Bouton Annuler ---
    lv_obj_t * btn_cancel = lv_btn_create(menu_box);
    lv_obj_set_size(btn_cancel, 200, 50);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x333333), 0);
    lv_obj_add_event_cb(btn_cancel, btn_cancel_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "Annuler");
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_montserrat_18, 0);
    lv_obj_center(lbl_cancel);

    // Animation d'apparition
    lv_obj_set_style_opa(menu_box, 0, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, menu_box);
    lv_anim_set_values(&a, 0, 255);
    lv_anim_set_time(&a, 200);
    lv_anim_set_exec_cb(&a, [](void * var, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t *)var, v, 0);
    });
    lv_anim_start(&a);
}

// --- TEST AUDIO (Micro -> Haut-parleur) ---
void test_audio_loopback(TFT_eSPI &disp, int dummy_duration = 0) {
    const int SAMPLE_RATE = 16000;
    const int SECONDS = 2; 
    const int NUM_SAMPLES = SAMPLE_RATE * SECONDS;
    
    int16_t *audio_buffer = (int16_t*)malloc(NUM_SAMPLES * sizeof(int16_t));
    if (!audio_buffer) {
        disp.println("Erreur: Pas assez de RAM !");
        Serial.println("Erreur RAM pour l'audio");
        return;
    }

    disp.fillScreen(TFT_BLACK);
    disp.setCursor(0, 0);
    disp.setTextColor(TFT_RED, TFT_BLACK);
    disp.println("ENREGISTREMENT !");
    disp.println("Parlez maintenant...");
    Serial.println("ENREGISTREMENT (2 sec)...");

    I2S i2sIn(INPUT);
    i2sIn.setBCLK(I2S_IN_BCLK); 
    i2sIn.setDATA(I2S_IN_DOUT);
    i2sIn.setBitsPerSample(32); 
    i2sIn.begin(SAMPLE_RATE);

    int32_t l32 = 0, r32 = 0;
    int16_t filtered_sample = 0;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        while (!i2sIn.read32(&l32, &r32)) { yield(); }
        int16_t raw = (int16_t)(l32 >> 16); 
        int32_t boosted = (int32_t)raw * 6; 
        if (boosted > 32760) boosted = 32760;
        if (boosted < -32760) boosted = -32760;
        raw = (int16_t)boosted;
        filtered_sample = (raw * 3 + filtered_sample) / 4;
        audio_buffer[i] = filtered_sample;
    }
    i2sIn.end(); 

    disp.setTextColor(TFT_GREEN, TFT_BLACK);
    disp.println("\nLECTURE...");
    Serial.println("LECTURE...");

    static AudioOutputI2S out;
    out.SetRate(SAMPLE_RATE);
    out.SetBitsPerSample(16);
    out.SetChannels(2);
    out.SetOutputModeMono(true);
    out.SetGain(0.4f); 
    out.SetPinout(I2S_OUT_BCLK, I2S_OUT_WS, I2S_OUT_DIN);
    out.begin();

    int16_t sample[2];
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sample[0] = audio_buffer[i];
        sample[1] = audio_buffer[i];
        while (!out.ConsumeSample(sample)) { yield(); }
    }

    out.stop();
    free(audio_buffer); 
    audio_pins_quiet();
    
    disp.println("\nTest termine !");
    Serial.println("Test termine !");
}

// --- TEST MODEM A7670E ---
void test_sim800l(TFT_eSPI &disp) {
    Serial.println("--- DEMARRAGE A7670E ---");

    Serial1.setTX(SIM800_TX);
    Serial1.setRX(SIM800_RX);
    pinMode(A7670_PWRKEY, OUTPUT);
    digitalWrite(A7670_PWRKEY, HIGH); 

    Serial1.begin(115200);
    while (Serial1.available()) Serial1.read();
    Serial1.println("AT");
    delay(600);
    String probe = "";
    while (Serial1.available()) probe += (char)Serial1.read();
    bool already_on  = (probe.indexOf("OK") != -1);
    bool need_baud   = already_on; 

    if (!already_on) {
        Serial1.begin(9600);
        while (Serial1.available()) Serial1.read();
        Serial1.println("AT");
        delay(600);
        probe = "";
        while (Serial1.available()) probe += (char)Serial1.read();
        if (probe.indexOf("OK") != -1) {
            already_on = true;
            need_baud  = false; 
            Serial.println("> Modem déjà actif à 9600 baud");
        } else {
            Serial1.begin(115200); 
        }
    } else {
        Serial.println("> Modem déjà actif à 115200 baud");
    }

    if (!already_on) {
        Serial.println("> Allumage via PWRKEY...");
        digitalWrite(A7670_PWRKEY, LOW);
        delay(1500);
        digitalWrite(A7670_PWRKEY, HIGH);
        Serial.println("> Attente boot modem (5s)...");
        delay(5000); 
        need_baud = true; 
    }

    if (need_baud) {
        Serial1.begin(115200);
        Serial1.println("AT+IPR=9600");
        delay(400);
        Serial1.println("AT&W");
        delay(400);
        Serial1.begin(9600);
        delay(300);
    }
    Serial.println("> Envoi: AT");
}

void run_sim_diagnostic(TFT_eSPI &disp) {
    Serial.println("--- DIAGNOSTIC A7670E ---");
    String cmds[] = {"AT+CPIN?", "AT+CSQ", "AT+CREG?", "AT+COPS?", "AT+CPSI?"};
    for(int i=0; i<5; i++) {
        Serial.print("\n> "); Serial.println(cmds[i]);
        Serial1.println(cmds[i]);
        uint32_t t = millis();
        while(millis() - t < 2000) {
            while(Serial1.available()) {
                char c = Serial1.read();
                if(c != '\r') Serial.print(c); 
            }
        }
    }
}

// --- FONCTION SONORE ---
void i2s_play_test_tone(int freq, int duration_ms, float gain = 0.6f) {
    static AudioOutputI2S out;
    const int base = (I2S_OUT_BCLK < I2S_OUT_WS) ? I2S_OUT_BCLK : I2S_OUT_WS;
    const bool wantSwap = (I2S_OUT_WS < I2S_OUT_BCLK);

    if (I2S_OUT_BCLK == I2S_OUT_WS || (abs(I2S_OUT_BCLK - I2S_OUT_WS) != 1)) return;

    out.SetRate(44100);
    out.SetBitsPerSample(16);
    out.SetChannels(2);
    out.SetOutputModeMono(true);
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    out.SetGain(gain);
    out.SwapClocks(wantSwap);
    out.SetPinout(base, base + 1, I2S_OUT_DIN);
    if (!out.begin()) {
        audio_pins_quiet();
        return;
    }

    const int sampleRate = 44100;
    const int half_period = (freq > 0) ? (sampleRate / freq / 2) : 0;
    const int samples = (sampleRate * duration_ms) / 1000;
    if (half_period <= 0 || samples <= 0) return;

    int16_t amp = 12000;
    int count = 0;
    int16_t stereo[2] = {amp, amp};

    for (int i = 0; i < samples; i++) {
        if (count >= half_period) {
            amp = (int16_t)-amp;
            count = 0;
            stereo[0] = amp;
            stereo[1] = amp;
        }
        while (!out.ConsumeSample(stereo)) { delayMicroseconds(50); }
        count++;
    }

    int16_t zero[2] = {0, 0};
    for (int i = 0; i < 400; i++) {
        while (!out.ConsumeSample(zero)) { delayMicroseconds(50); }
    }

    out.flush();
    out.stop();
    audio_pins_quiet();
}

// --- LECTURE TACTILE CAPACITIF I2C ---
void _touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    Wire.beginTransmission(FT6336U_ADDR);
    Wire.write(0x02); // Registre TD_STATUS
    if (Wire.endTransmission(false) != 0) {
        data->state = LV_INDEV_STATE_REL;
        return; 
    }

    Wire.requestFrom(FT6336U_ADDR, 1);
    if (!Wire.available()) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }
    
    uint8_t touches = Wire.read() & 0x0F;

    if (touches > 0) {
        Wire.beginTransmission(FT6336U_ADDR);
        Wire.write(0x03); 
        Wire.endTransmission(false);
        Wire.requestFrom(FT6336U_ADDR, 4);

        if (Wire.available() >= 4) {
            uint8_t p1_xh = Wire.read();
            uint8_t p1_xl = Wire.read();
            uint8_t p1_yh = Wire.read();
            uint8_t p1_yl = Wire.read();

            uint16_t x = ((p1_xh & 0x0F) << 8) | p1_xl;
            uint16_t y = ((p1_yh & 0x0F) << 8) | p1_yl;
            
            data->point.x = (int16_t)x;
            data->point.y = (int16_t)y;
            data->state = LV_INDEV_STATE_PR;
            return;
        }
    }
    
    data->state = LV_INDEV_STATE_REL;
}

// === AFFICHAGE HAUTES PERFORMANCES (DMA + MUTEX) ===
void _disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    uint32_t len = w * h;

    // 1. VERROUILLAGE SÉCURISÉ DU SPI (Pour protéger la carte SD)
    mutex_enter_blocking(&spi_mutex);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    
    // 2. ENVOI DMA DIRECT ET IMMÉDIAT (SANS BOUCLE FOR !)
    tft.pushPixelsDMA((uint16_t *)&color_p->full, len);
    
    // 3. ATTENTE DE FIN DE TRANSFERT (Essentiel pour éviter les glitchs visuels !)
    tft.dmaWait();

    tft.endWrite();
    
    // 4. LIBÉRATION SÉCURISÉE DU SPI
    mutex_exit(&spi_mutex);

    lv_disp_flush_ready(disp);
}

void hardware_init() {
    Serial.begin(115200);
    
    pinMode(LCD_BL_PIN, OUTPUT); 
    battery::begin();
    digitalWrite(LCD_BL_PIN, HIGH);
    
    audio_pins_quiet();

    // Initialisation du bus I2C (Tactile)
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);
    delay(10);
    digitalWrite(TP_RST, HIGH);
    delay(50);
    
    Wire.setSDA(TP_SDA);
    Wire.setSCL(TP_SCL);
    Wire.begin();
    
    // FORCER LE ROUTAGE SPI1 DU PICO 2
    SPI1.setRX(12);
    SPI1.setTX(11);
    SPI1.setSCK(10);

    pinMode(LCD_RST_PIN, OUTPUT);
    digitalWrite(LCD_RST_PIN, LOW);
    delay(20);
    digitalWrite(LCD_RST_PIN, HIGH);
    delay(20);
        
    // Initialisation de l'écran TFT
    tft.init();
    tft.setRotation(0);
    
    // ON RÉACTIVE LE DMA ! 🚀
    tft.initDMA(); 
    
    tft.fillScreen(TFT_BLACK);
    tft.drawBitmap(0, (480 - 140)/2, epd_bitmap_Startup_Logo, 320, 140, TFT_WHITE);
    tft.drawBitmap((320-61)/2, 480-45, epd_bitmap_marsisus_logo, 61, 18, TFT_WHITE);

    test_sim800l(tft);


    LTE::setLowPower(false);


    // Initialisation LVGL
    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LV_BUF_PIXELS);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 320;
    disp_drv.ver_res = 480;
    disp_drv.flush_cb = _disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = _touch_read;
    lv_indev_drv_register(&indev_drv);

    pinMode(SLEEP_BTN_PIN, INPUT_PULLUP);
}

// Dans Hardware.h

void hardware_sleep() {
    mutex_enter_blocking(&spi_mutex);
    digitalWrite(LCD_BL_PIN, LOW);
    tft.writecommand(0x10); // Sleep
    // On ne coupe pas forcément le SPI.end() ici pour éviter de perdre la config des pins
    mutex_exit(&spi_mutex);

    Serial1.println("AT+CSCLK=2");
}


// Appelle hardware_sleep() pour mettre en veille, hardware_wake() pour réveiller.
// Pour sortir de veille : détecter touche ou tactile (TP_INT ou autre GPIO)

void check_sleep_button() {
    static uint32_t press_start_time = 0;
    static bool is_pressing = false;
    static bool long_press_handled = false; // Pour savoir si le menu a déjà pop
    
    // On lit l'état (LOW = pressé car on a un INPUT_PULLUP)
    bool state = digitalRead(SLEEP_BTN_PIN); 

    if (state == LOW) {
        if (!is_pressing) {
            // 1. Le doigt vient TOUT JUSTE de se poser
            is_pressing = true;
            press_start_time = millis();
            long_press_handled = false;
        } 
        else {
            // 2. Le doigt est MAINTENU enfoncé
            if (!long_press_handled && (millis() - press_start_time > 1500)) {
                // Les 1.5s sont passées ! On affiche le menu IMMÉDIATEMENT
                Serial.println("[POWER] Appui long détecté !");
                showPowerMenu(); // Appel direct de la fonction LVGL
                long_press_handled = true; // On bloque pour ne pas ouvrir le menu en boucle
            }
        }
    } 
    else { // state == HIGH (bouton relâché)
        if (is_pressing) {
            is_pressing = false;
            
            // 3. On a relâché AVANT les 1.5s (Appui court)
            // L'anti-rebond de 50ms évite les faux positifs
            if (!long_press_handled && (millis() - press_start_time > 50)) {
                Serial.println("[POWER] Appui court détecté !");
                if (!manager.lockScreen.isLocked()) {
                    manager.lockScreen.lock();
                } else {
                    manager.lockScreen.unlock();
                }
            }
        }
    }
}

void hardware_wake() {
    // 1. Relancer le bus SPI pour pouvoir parler à l'écran
    SPI1.begin(); 
    
    // 2. Sortir l'écran du mode Sleep (Commande ST7796S)
    tft.startWrite();
    tft.writecommand(0x11); // Sleep Out
    tft.endWrite();
    
    delay(120); // INDISPENSABLE : l'écran a besoin de temps pour relancer ses tensions internes

    // 3. Réactiver l'affichage
    tft.startWrite();
    tft.writecommand(0x29); // Display ON
    tft.endWrite();

    // 4. Rallumer la lumière
    digitalWrite(LCD_BL_PIN, HIGH);
    Serial1.println("AT+CSCLK=0");
    
    // 5. Optionnel : Rafraîchir l'écran pour éviter un "flash" de vieux pixels
    tft.fillScreen(TFT_BLACK); 
}

// À appeler dans loop() : check_sleep_button();
// Initialisation dans hardware_init()

#endif