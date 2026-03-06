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
#include "assets/startuplogo.c"
#include "system/Battery.h"

// --- PINS ---
#define TP_CS 16
#define TP_CLK 10
#define TP_MOSI 11
#define TP_MISO 12
#define LCD_RST_PIN 15
#define LCD_CS_PIN 9

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
#define SIM800_TX    0 // TX du Pico
#define SIM800_RX    1 // RX du Pico
#define A7670_PWRKEY 26 // Broche K (KEY) du A7670E

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
// Taille du buffer (ne pas augmenter si on manque de RAM, le DMA compense)
static constexpr uint32_t LV_BUF_PIXELS = 320u * 100u;
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

// --- TEST AUDIO (Micro -> Haut-parleur) avec DSP propre ---
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

// --- LECTURE TACTILE ---
void touch_read_spi_sdk(uint16_t& x, uint16_t& y, uint16_t& z) {
    uint8_t tx_buff[3];
    uint8_t rx_buff[3];
    const uint32_t old_baud = spi_get_baudrate(spi1);
    
    // Vitesse lente obligatoire pour le XPT2046
    spi_set_baudrate(spi1, 1000000); 
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // Lecture Z
    gpio_put(TP_CS, 0);
    tx_buff[0] = 0xB0; tx_buff[1] = 0; tx_buff[2] = 0; 
    spi_write_read_blocking(spi1, tx_buff, rx_buff, 3);
    gpio_put(TP_CS, 1);
    z = ((rx_buff[1] << 8) | rx_buff[2]) >> 3;

    if (z > 200) {
        // Lecture X
        gpio_put(TP_CS, 0); 
        tx_buff[0] = 0xD0; tx_buff[1] = 0; tx_buff[2] = 0; 
        spi_write_read_blocking(spi1, tx_buff, rx_buff, 3);
        spi_write_read_blocking(spi1, tx_buff, rx_buff, 3);
        gpio_put(TP_CS, 1);
        x = ((rx_buff[1] << 8) | rx_buff[2]) >> 3;

        // Lecture Y
        gpio_put(TP_CS, 0);
        tx_buff[0] = 0x90; tx_buff[1] = 0; tx_buff[2] = 0; 
        spi_write_read_blocking(spi1, tx_buff, rx_buff, 3);
        spi_write_read_blocking(spi1, tx_buff, rx_buff, 3);
        gpio_put(TP_CS, 1);
        y = ((rx_buff[1] << 8) | rx_buff[2]) >> 3;
    }

    spi_set_baudrate(spi1, old_baud);
}

void _touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    mutex_enter_blocking(&spi_mutex);

    uint16_t x_raw = 0, y_raw = 0, z_raw = 0;
    
    // Le TFT doit avoir libéré le bus avant de toucher au SPI
    tft.endWrite(); 
    
    touch_read_spi_sdk(x_raw, y_raw, z_raw);
    
    if (z_raw > 200) {
        data->state = LV_INDEV_STATE_PR;
        long map_x = map(x_raw, 3840, 300, 0, 320);
        long map_y = map(y_raw, 3960, 240, 0, 480);
        
        if(map_x < 0) map_x = 0; if(map_x > 319) map_x = 319;
        if(map_y < 0) map_y = 0; if(map_y > 479) map_y = 479;
        
        data->point.x = (int16_t)map_x;
        data->point.y = (int16_t)map_y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }


    mutex_exit(&spi_mutex); 
}

// === AFFICHAGE OPTIMISÉ DMA ===
void _disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    mutex_enter_blocking(&spi_mutex);

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    
    tft.pushPixelsDMA((uint16_t *)&color_p->full, w * h);
    
    // Attendre explicitement que le DMA ait fini avant de libérer le buffer (ou pas)
    // tft.dmaWait(); 
    
    tft.endWrite();
    mutex_exit(&spi_mutex);

    lv_disp_flush_ready(disp);
}

void hardware_init() {
    Serial.begin(115200);
    audio_pins_quiet();
    battery::begin();

    mutex_init(&spi_mutex);

    // vreg_set_voltage(VREG_VOLTAGE_1_20);
    
    pinMode(13, OUTPUT); digitalWrite(13, HIGH);
    gpio_init(TP_CS); gpio_set_dir(TP_CS, GPIO_OUT); gpio_put(TP_CS, 1);
    pinMode(LCD_CS_PIN, OUTPUT); digitalWrite(LCD_CS_PIN, HIGH);

    pinMode(LCD_RST_PIN, OUTPUT);
    digitalWrite(LCD_RST_PIN, HIGH); delay(50);
    digitalWrite(LCD_RST_PIN, LOW);  delay(100);
    digitalWrite(LCD_RST_PIN, HIGH); delay(150);
    
    tft.init();
    tft.setRotation(0);

    tft.initDMA();  
        
    // ==========================================
    // DEBUT DES TESTS HARDWARE AU DEMARRAGE
    // ==========================================
    tft.fillScreen(TFT_BLACK);
    tft.drawBitmap(0, (480 - 140)/2, epd_bitmap_Startup_Logo, 320, 140, TFT_WHITE);
    tft.drawBitmap((320-61)/2, 480-45, epd_bitmap_marsisus_logo, 61, 18, TFT_WHITE);

    // If it's not, convert it to the correct format before calling pushImage.

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2); 
    tft.setCursor(0, 0);

    

    // test_audio_loopback(tft, 4000); 

    // 1. Test du Modem 4G (A7670E)
    test_sim800l(tft);
    // run_sim_diagnostic(tft);
    
    // ==========================================

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