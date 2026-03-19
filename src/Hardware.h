#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <SDFS.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <AudioOutputI2S.h>
#include <I2S.h> 
#include <hardware/vreg.h>
#include <Wire.h> 
#include "assets/startuplogo.c"
#include "system/Battery.h"

// --- PINS ECRAN (SANS CONFLIT AUDIO) ---
#define LCD_CS_PIN  9
#define LCD_DC_PIN  8    // Changé en GP8 !
#define LCD_RST_PIN 13
#define LCD_BL_PIN  15

// --- PINS TACTILE CAPACITIF (FT6336U) ---
#define TP_SDA 16  // Changé en GP16 (I2C0)
#define TP_SCL 17  // Changé en GP17 (I2C0)
#define TP_INT 18
#define TP_RST 19
#define FT6336U_ADDR 0x38

#define SD_CS_PIN 22 

// Pour le MAX98357A (Sortie) -> RESTE INCHANGÉ
#define I2S_OUT_BCLK 6
#define I2S_OUT_WS   7 
#define I2S_OUT_DIN  14

// Pour le INMP441 (Entrée) -> RESTE INCHANGÉ
#define I2S_IN_BCLK  2
#define I2S_IN_WS    3
#define I2S_IN_DOUT  4

// Pour le SIM800L / A7670E (UART0) -> RESTE INCHANGÉ
#define SIM800_TX    0 
#define SIM800_RX    1 
#define A7670_PWRKEY 26 

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static constexpr uint32_t LV_BUF_PIXELS = 320u * 150u;
static lv_color_t buf1[LV_BUF_PIXELS];
static lv_color_t buf2[LV_BUF_PIXELS];

mutex_t spi_mutex;

static inline void audio_pins_quiet() {
    pinMode(I2S_OUT_DIN, INPUT_PULLDOWN);
    pinMode(I2S_OUT_BCLK, INPUT_PULLDOWN);
    pinMode(I2S_OUT_WS, INPUT_PULLDOWN);
    pinMode(I2S_IN_DOUT, INPUT_PULLDOWN);
    pinMode(I2S_IN_BCLK, INPUT_PULLDOWN);
    pinMode(I2S_IN_WS, INPUT_PULLDOWN);
}

// --- TEST AUDIO (Micro -> Haut-parleur) ---
void test_audio_loopback(TFT_eSPI &disp, int dummy_duration = 0) {
    const int SAMPLE_RATE = 16000;
    const int SECONDS = 2; // On enregistre 2 secondes
    const int NUM_SAMPLES = SAMPLE_RATE * SECONDS;
    
    // On réserve 64 Ko de RAM pour stocker l'audio
    int16_t *audio_buffer = (int16_t*)malloc(NUM_SAMPLES * sizeof(int16_t));
    if (!audio_buffer) {
        disp.println("Erreur: Pas assez de RAM !");
        Serial.println("Erreur RAM pour l'audio");
        return;
    }

    // ==========================================
    // PHASE 1 : ENREGISTREMENT (Silence total)
    // ==========================================
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
        // On force la lecture
        while (!i2sIn.read32(&l32, &r32)) { yield(); }
        
        int16_t raw = (int16_t)(l32 >> 16); 

        // Gain x6 avec limiteur
        int32_t boosted = (int32_t)raw * 6; 
        if (boosted > 32760) boosted = 32760;
        if (boosted < -32760) boosted = -32760;
        raw = (int16_t)boosted;

        // Filtre passe-bas doux
        filtered_sample = (raw * 3 + filtered_sample) / 4;
        
        // Stockage en RAM
        audio_buffer[i] = filtered_sample;
    }
    i2sIn.end(); // On coupe le micro !

    // ==========================================
    // PHASE 2 : LECTURE
    // ==========================================
    disp.setTextColor(TFT_GREEN, TFT_BLACK);
    disp.println("\nLECTURE...");
    Serial.println("LECTURE...");

    static AudioOutputI2S out;
    out.SetRate(SAMPLE_RATE);
    out.SetBitsPerSample(16);
    out.SetChannels(2);
    out.SetOutputModeMono(true);
    out.SetGain(0.4f); // Volume de sortie à 40%
    out.SetPinout(I2S_OUT_BCLK, I2S_OUT_WS, I2S_OUT_DIN);
    out.begin();

    int16_t sample[2];
    for (int i = 0; i < NUM_SAMPLES; i++) {
        sample[0] = audio_buffer[i];
        sample[1] = audio_buffer[i];
        
        while (!out.ConsumeSample(sample)) { yield(); }
    }

    out.stop();
    free(audio_buffer); // On libère la RAM
    audio_pins_quiet();
    
    disp.println("\nTest termine !");
    Serial.println("Test termine !");
}

// --- TEST MODEM A7670E (Ex-SIM800L) ---
void test_sim800l(TFT_eSPI &disp) {
    Serial.println("--- DEMARRAGE A7670E ---");

    // Initialiser Serial1 pour sonder si le modem est déjà actif
    Serial1.setTX(SIM800_TX);
    Serial1.setRX(SIM800_RX);
    pinMode(A7670_PWRKEY, OUTPUT);
    digitalWrite(A7670_PWRKEY, HIGH); // assure état neutre

    // --- Probe 1 : tenter à 115200 ---
    Serial1.begin(115200);
    while (Serial1.available()) Serial1.read();
    Serial1.println("AT");
    delay(600);
    String probe = "";
    while (Serial1.available()) probe += (char)Serial1.read();
    bool already_on  = (probe.indexOf("OK") != -1);
    bool need_baud   = already_on; // à 115200 → il faudra basculer à 9600

    if (!already_on) {
        // --- Probe 2 : tenter à 9600 (modem déjà configuré) ---
        Serial1.begin(9600);
        while (Serial1.available()) Serial1.read();
        Serial1.println("AT");
        delay(600);
        probe = "";
        while (Serial1.available()) probe += (char)Serial1.read();
        if (probe.indexOf("OK") != -1) {
            already_on = true;
            need_baud  = false; // déjà à 9600
            Serial.println("> Modem déjà actif à 9600 baud");
        } else {
            Serial1.begin(115200); // Repartir en 115200 pour allumage
        }
    } else {
        Serial.println("> Modem déjà actif à 115200 baud");
    }

    if (!already_on) {
        // Modem éteint : pulse PWRKEY LOW 1.5 s pour l'allumer
        // (< 3 s = allumage uniquement, pas d'extinction)
        Serial.println("> Allumage via PWRKEY...");
        digitalWrite(A7670_PWRKEY, LOW);
        delay(1500);
        digitalWrite(A7670_PWRKEY, HIGH);

        Serial.println("> Attente boot modem (5s)...");
        delay(5000); // Laisser le temps au modem de démarrer complètement
        need_baud = true; // modem démarre à 115200 par défaut
    }

    if (need_baud) {
        // Basculer le modem à 9600 et sauvegarder
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

    String cmds[] = {
        "AT+CPIN?",  // Test 1: La SIM est-elle lue et débloquée ?
        "AT+CSQ",    // Test 2: Qualité du signal de l'antenne (0 à 31)
        "AT+CREG?",  // Test 3: Statut d'enregistrement sur le réseau
        "AT+COPS?",  // Test 4: Nom de l'opérateur trouvé
        "AT+CPSI?"   // Test 5: Info réseau spécifique 4G (LTE)
    };

    for(int i=0; i<5; i++) {
        Serial.print("\n> "); Serial.println(cmds[i]);
        
        Serial1.println(cmds[i]);
        uint32_t t = millis();
        
        // Attendre la réponse 2 secondes
        while(millis() - t < 2000) {
            while(Serial1.available()) {
                char c = Serial1.read();
                if(c != '\r') Serial.print(c); // Afficher la réponse
            }
        }
    }
}

// --- FONCTION SONORE ---
void i2s_play_test_tone(int freq, int duration_ms, float gain = 0.6f) {
    static AudioOutputI2S out;
    const int base = (I2S_OUT_BCLK < I2S_OUT_WS) ? I2S_OUT_BCLK : I2S_OUT_WS;
    const bool wantSwap = (I2S_OUT_WS < I2S_OUT_BCLK);

    if (I2S_OUT_BCLK == I2S_OUT_WS || (abs(I2S_OUT_BCLK - I2S_OUT_WS) != 1)) {
        return;
    }

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
        while (!out.ConsumeSample(stereo)) {
            delayMicroseconds(50);
        }
        count++;
    }

    int16_t zero[2] = {0, 0};
    for (int i = 0; i < 400; i++) {
        while (!out.ConsumeSample(zero)) {
            delayMicroseconds(50);
        }
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

    Wire.requestFrom(FT6336U_ADDR, 5);
    if (Wire.available() >= 5) {
        uint8_t touches = Wire.read() & 0x0F;
        uint8_t p1_xh = Wire.read();
        uint8_t p1_xl = Wire.read();
        uint8_t p1_yh = Wire.read();
        uint8_t p1_yl = Wire.read();

        if (touches > 0) {
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

// === AFFICHAGE SANS DMA (Compatible Pico 2) MAIS AVEC MUTEX (Pour sauver la SD) ===
void _disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    // LE MUTEX EST VITAL ICI ! Il empêche la carte SD de parler en même temps.
    mutex_enter_blocking(&spi_mutex);

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    
    // On envoie via le processeur classique (très rapide grâce à tes 240MHz)
    tft.pushPixels((uint16_t *)&color_p->full, w * h);
    tft.endWrite();
    
    mutex_exit(&spi_mutex);

    lv_disp_flush_ready(disp);
}

void hardware_init() {
    Serial.begin(115200);
    
    // 1. ALLUMAGE IMMÉDIAT DU RÉTROÉCLAIRAGE
    pinMode(LCD_BL_PIN, OUTPUT); 
    digitalWrite(LCD_BL_PIN, HIGH);
    
    audio_pins_quiet();
    mutex_init(&spi_mutex);

    // 2. Initialisation du bus I2C (Tactile)
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, LOW);
    delay(10);
    digitalWrite(TP_RST, HIGH);
    delay(50);
    
    Wire.setSDA(TP_SDA);
    Wire.setSCL(TP_SCL);
    Wire.begin();
    
    // 3. FORCER LE ROUTAGE SPI1 DU PICO 2
    SPI1.setRX(12);
    SPI1.setTX(11);
    SPI1.setSCK(10);
    
    // 4. Initialisation de l'écran TFT
    tft.init();
    tft.setRotation(0); 
    // tft.initDMA(); <--- ON LAISSE DÉSACTIVÉ ! Le DMA du Pico 2 fait crasher la lib.
        
    tft.fillScreen(TFT_BLACK); // Fond noir propre

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
}

#endif