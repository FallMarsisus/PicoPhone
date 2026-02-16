#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <AudioOutputI2S.h>

#include "system/Battery.h"

// --- PINS ---
#define TP_CS 16
#define TP_CLK 10
#define TP_MOSI 11
#define TP_MISO 12
#define LCD_RST_PIN 15
#define LCD_CS_PIN 9

// Pour le MAX98357A (Sortie)
#define I2S_OUT_BCLK 26
#define I2S_OUT_WS   27 
#define I2S_OUT_DIN  28

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
// Taille du buffer (ne pas augmenter si on manque de RAM, le DMA compense)
static constexpr uint32_t LV_BUF_PIXELS = 320u * 60u;
static lv_color_t buf1[LV_BUF_PIXELS];
static lv_color_t buf2[LV_BUF_PIXELS];

static inline void audio_pins_quiet() {
    pinMode(I2S_OUT_DIN, INPUT_PULLDOWN);
    pinMode(I2S_OUT_BCLK, INPUT_PULLDOWN);
    pinMode(I2S_OUT_WS, INPUT_PULLDOWN);
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
    uint16_t x_raw = 0, y_raw = 0, z_raw = 0;
    
    // Le TFT doit avoir libéré le bus avant de toucher au SPI
    tft.endWrite(); 
    
    touch_read_spi_sdk(x_raw, y_raw, z_raw);
    
    if (z_raw > 200) {
        data->state = LV_INDEV_STATE_PR;
        long map_x = map(x_raw, 3840, 280, 0, 320);
        long map_y = map(y_raw, 3960, 240, 0, 480);
        
        if(map_x < 0) map_x = 0; if(map_x > 319) map_x = 319;
        if(map_y < 0) map_y = 0; if(map_y > 479) map_y = 479;
        
        data->point.x = (int16_t)map_x;
        data->point.y = (int16_t)map_y;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// === AFFICHAGE OPTIMISÉ DMA ===
void _disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);

    // Envoi en DMA : Le CPU est libre de calculer la prochaine frame LVGL pendant le transfert
    tft.pushPixelsDMA((uint16_t *)&color_p->full, w * h);
    
    tft.endWrite();
    lv_disp_flush_ready(disp);
}

void hardware_init() {
    Serial.begin(115200);
    audio_pins_quiet();
    battery::begin();
    
    pinMode(13, OUTPUT); digitalWrite(13, HIGH);
    gpio_init(TP_CS); gpio_set_dir(TP_CS, GPIO_OUT); gpio_put(TP_CS, 1);
    pinMode(LCD_CS_PIN, OUTPUT); digitalWrite(LCD_CS_PIN, HIGH);

    pinMode(LCD_RST_PIN, OUTPUT);
    digitalWrite(LCD_RST_PIN, HIGH); delay(50);
    digitalWrite(LCD_RST_PIN, LOW);  delay(100);
    digitalWrite(LCD_RST_PIN, HIGH); delay(150);
    
    tft.init();
    tft.initDMA(); // Activation du DMA pour l'écran
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    
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