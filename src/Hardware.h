#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <SDFS.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <hardware/i2c.h>
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

// --- PINS ECRAN (DEV_Config.h Waveshare RP2350-Touch-LCD-3.5) ---
#define SPI_PORT SPI
#define I2C_PORT Wire

#define LCD_RST_PIN  23
#define LCD_DC_PIN   20
#define LCD_BL_PIN   22
#define LCD_CS_PIN   21
#define LCD_CLK_PIN  18
#define LCD_MOSI_PIN 19
#define LCD_MISO_PIN 4

// --- PINS TACTILE CAPACITIF ET PMIC ---
#define TP_SDA 34
#define TP_SCL 35
#define TP_RST 24
#define TP_INT 25
#define FT6336U_ADDR 0x38
#define FT6336U_REG_DEVICE_MODE   0x00
#define FT6336U_REG_TD_STATUS     0x02
#define FT6336U_REG_TOUCH1_X      0x03
#define FT6336U_REG_TOUCH1_Y      0x05
#define FT6336U_REG_CHIP_ID       0xA3
#define FT6336U_REG_G_MODE        0xA4
#define FT6336U_REG_POWER_MODE    0xA5
#define FT6336U_REG_FIRMWARE_ID   0xA6
#define FT6336U_REG_FOCALTECH_ID  0xA8
#define FT6336U_REG_GESTURE_EN    0xD0

// --- PINS IMU / CAPTEURS ---
#define DEV_SDA_PIN 34
#define DEV_SCL_PIN 35
#define DOF_INT1    14
#define I2C_RST     38
#define SYS_OUT_PIN 40
#define BAT_ADC_PIN 28

#define SD_CS_PIN 5

// --- CODEC AUDIO ES8311 ---
#define ES8311_I2C_ADDR 0x18
#define ES8311_RESET_REG00       0x00
#define ES8311_CLK_MANAGER_REG01 0x01
#define ES8311_CLK_MANAGER_REG02 0x02
#define ES8311_CLK_MANAGER_REG03 0x03
#define ES8311_CLK_MANAGER_REG04 0x04
#define ES8311_CLK_MANAGER_REG05 0x05
#define ES8311_CLK_MANAGER_REG06 0x06
#define ES8311_CLK_MANAGER_REG07 0x07
#define ES8311_CLK_MANAGER_REG08 0x08
#define ES8311_SDPIN_REG09       0x09
#define ES8311_SDPOUT_REG0A      0x0A
#define ES8311_SYSTEM_REG0B      0x0B
#define ES8311_SYSTEM_REG0C      0x0C
#define ES8311_SYSTEM_REG0D      0x0D
#define ES8311_SYSTEM_REG0E      0x0E
#define ES8311_SYSTEM_REG0F      0x0F
#define ES8311_SYSTEM_REG10      0x10
#define ES8311_SYSTEM_REG11      0x11
#define ES8311_SYSTEM_REG12      0x12
#define ES8311_SYSTEM_REG13      0x13
#define ES8311_SYSTEM_REG14      0x14
#define ES8311_ADC_REG15         0x15
#define ES8311_ADC_REG16         0x16
#define ES8311_ADC_REG17         0x17
#define ES8311_ADC_REG18         0x18
#define ES8311_ADC_REG19         0x19
#define ES8311_ADC_REG1A         0x1A
#define ES8311_ADC_REG1B         0x1B
#define ES8311_ADC_REG1C         0x1C
#define ES8311_DAC_REG31         0x31
#define ES8311_DAC_REG32         0x32
#define ES8311_DAC_REG33         0x33
#define ES8311_DAC_REG34         0x34
#define ES8311_DAC_REG35         0x35
#define ES8311_DAC_REG37         0x37
#define ES8311_GPIO_REG44        0x44
#define ES8311_GP_REG45          0x45
#define ES8311_CHD1_REGFD        0xFD
#define ES8311_CHD2_REGFE        0xFE
#define ES8311_CHVER_REGFF       0xFF
#define ES8311_MAX_REGISTER      0xFF

// I2S sortie vers ES8311
#define I2S_OUT_BCLK 6
#define I2S_OUT_WS   7 
#define I2S_OUT_DIN  14

// Pour le INMP441 (Entrée)
#define I2S_IN_BCLK  2
#define I2S_IN_WS    3
#define I2S_IN_DOUT  4

// Pour le SIM800L / A7670E (UART0)
#define SIM800_TX    32
#define SIM800_RX    47
#define A7670_PWRKEY 33

// --- Gestion du PWRKEY pour A7670 ---
// Durée recommandée pour extinction matérielle : 1-2 secondes
inline void a7670_power_key_press(unsigned long ms = 1500) {
    pinMode(A7670_PWRKEY, OUTPUT);
    digitalWrite(A7670_PWRKEY, LOW); // Active PWRKEY (niveau bas)
    delay(ms);
    pinMode(A7670_PWRKEY, INPUT); // Haute impédance après l'impulsion
}

// Mise en mode fonctionnalité minimale (AT+CFUN=0)
inline void a7670_set_minimal_functionality() {
    Serial1.println("AT+CFUN=0");
    // Attendre la réponse OK ou délai de sécurité
    unsigned long wait_start = millis();
    while (millis() - wait_start < 2000) {
        if (Serial1.available()) {
            String resp = Serial1.readStringUntil('\n');
            if (resp.indexOf("OK") != -1) break;
        }
    }
}

// --- BOUTON VEILLE ---
// Evite le conflit avec LCD_CS_PIN (GP21).
#define SLEEP_BTN_PIN 46
#define TEMP_DISABLE_POWER_BUTTON 1
#define TEMP_DISABLE_TOUCH_INIT 0

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static constexpr uint32_t LV_BUF_PIXELS = 320u * 100u;
static lv_color_t buf1[LV_BUF_PIXELS];
static lv_color_t buf2[LV_BUF_PIXELS];
static bool g_tft_dma_ready = false;
static bool g_touch_present = false;
static volatile bool g_hw_deferred_init_pending = true;
static uint16_t g_boot_stage_y = 24;
static bool g_boot_stage_onscreen_enabled = true;

// Mutex global
auto_init_mutex(spi_mutex);

static inline void boot_stage(const char* msg, uint16_t color = TFT_WHITE) {
    if (Serial) {
        Serial.print("[BOOT] ");
        Serial.println(msg);
    }

    if (g_boot_stage_onscreen_enabled) {
        mutex_enter_blocking(&spi_mutex);
        tft.setTextSize(1);
        tft.setTextColor(color, TFT_BLACK);
        tft.fillRect(8, g_boot_stage_y, 304, 10, TFT_BLACK);
        tft.setCursor(8, g_boot_stage_y);
        tft.print(msg);
        mutex_exit(&spi_mutex);

        g_boot_stage_y += 11;
        if (g_boot_stage_y > 460) {
            g_boot_stage_y = 24;
        }
    }
}

static inline void tft_bringup_test_pattern() {
    tft.fillScreen(TFT_RED);
    delay(250);
    tft.fillScreen(TFT_GREEN);
    delay(250);
    tft.fillScreen(TFT_BLUE);
    delay(250);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(8, 8);
    tft.print("ST7796 SPI OK");
    delay(300);
}

static inline bool es8311_is_present() {
    I2C_PORT.beginTransmission(ES8311_I2C_ADDR);
    return I2C_PORT.endTransmission() == 0;
}

static inline bool ft6336_read_bytes(uint8_t reg, uint8_t* out, size_t len);
static inline bool ft6336_write_byte(uint8_t reg, uint8_t value);

static inline bool ft6336_is_present() {
    uint8_t chip_id = 0;
    return ft6336_read_bytes(FT6336U_REG_CHIP_ID, &chip_id, 1) && chip_id == 0x64;
}

static inline void ft6336_i2c_hw_init() {
    i2c_init(i2c0, 100000);
    gpio_set_function(TP_SDA, GPIO_FUNC_I2C);
    gpio_set_function(TP_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(TP_SDA);
    gpio_pull_up(TP_SCL);
}

static inline void ft6336_reset_hw() {
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, HIGH);
    delay(10);
    digitalWrite(TP_RST, LOW);
    delay(10);
    digitalWrite(TP_RST, HIGH);
    delay(300);
}

static inline bool ft6336_write_byte(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_write_blocking(i2c0, FT6336U_ADDR, data, 2, false) == 2;
}

static inline bool ft6336_read_bytes(uint8_t reg, uint8_t* out, size_t len) {
    if (i2c_write_blocking(i2c0, FT6336U_ADDR, &reg, 1, true) != 1) {
        return false;
    }
    return i2c_read_blocking(i2c0, FT6336U_ADDR, out, len, false) == (int)len;
}

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

    // 2. COUPURE DU WIFI ET BLUETOOTH DU PICO W
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    LTE::setLowPower(true);
    watchdog_update();

    // 3. EXTINCTION DU MODEM A7670E via PWRKEY
    Serial.println("[LTE] Extinction matérielle via PWRKEY...");
    a7670_power_key_press(); // Impulsion PWRKEY pour extinction
    // Optionnel : attendre la réponse du module si nécessaire

    // On ne touche PLUS au PWRKEY ici !

    // 4. ANTI-ALIMENTATION PARASITE (CRUCIAL !)
    Serial1.end();
    pinMode(SIM800_TX, INPUT);
    pinMode(SIM800_RX, INPUT);
    pinMode(A7670_PWRKEY, INPUT);

    // 5. COUPURE DE L'AUDIO
    audio_pins_quiet(); 
    // IMPORTANT : Si vous avez relié SD_MODE du MAX98357 à un pin (ex: GP20)
    // pinMode(20, OUTPUT); digitalWrite(20, LOW); // Force le Shutdown total de l'ampli
// 6. EXTINCTION DE L'ÉCRAN
    digitalWrite(LCD_BL_PIN, LOW);
    tft.writecommand(0x28); // Display OFF
    tft.writecommand(0x10); // Sleep IN
    delay(50);

    // 7. VERROUILLAGE DES PINS FLOTTANTS (CRUCIAL CONTRE LES FUITES)
    SPI_PORT.end();
    I2C_PORT.end();

    // On force les pins de l'écran et du bus SPI à GND
    pinMode(LCD_RST_PIN, OUTPUT); digitalWrite(LCD_RST_PIN, LOW);
    pinMode(LCD_CS_PIN, OUTPUT);  digitalWrite(LCD_CS_PIN, LOW);
    pinMode(LCD_DC_PIN, OUTPUT);  digitalWrite(LCD_DC_PIN, LOW);
    
    pinMode(LCD_CLK_PIN, OUTPUT);  digitalWrite(LCD_CLK_PIN, LOW);
    pinMode(LCD_MOSI_PIN, OUTPUT); digitalWrite(LCD_MOSI_PIN, LOW);
    pinMode(LCD_MISO_PIN, INPUT_PULLUP);

    // Extinction totale du tactile
    pinMode(TP_RST, OUTPUT); digitalWrite(TP_RST, LOW);
    pinMode(TP_SDA, INPUT_PULLDOWN);
    pinMode(TP_SCL, INPUT_PULLDOWN);

    Serial.println("[POWER] CPU Zzz...");
    Serial.flush(); 

    // 8. BAISSE DE L'HORLOGE ET DODO PROFOND
    // On passe le RP2040 de 133 MHz à 2 MHz (fait chuter la conso du processeur à ~1mA)
    set_sys_clock_khz(20000, true);

    while (true) {
        watchdog_update();
        // L'utilisation de delay(100) est gérée par le cœur Arduino pour mettre
        // le processeur en vraie veille, contrairement à un simple __wfi().
        delay(10); 

        if (digitalRead(SLEEP_BTN_PIN) == LOW) {
            uint32_t press_time = millis();
            bool valid_press = true;

            while (millis() - press_time < 100) {
                watchdog_update();
                if (digitalRead(SLEEP_BTN_PIN) == HIGH) {
                    valid_press = false; 
                    break; 
                }
                delay(10);
            }

            if (valid_press) {
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




// --- Mise en veille : passage du A7670 en mode minimal ---
inline void enter_sleep_mode() {
    Serial.println("[LTE] Passage du A7670 en mode fonctionnalité minimale...");
    a7670_set_minimal_functionality();
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

    out.SetRate(44100);
    out.SetBitsPerSample(16);
    out.SetChannels(2);
    out.SetOutputModeMono(true);
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    out.SetGain(gain);
    out.SetPinout(I2S_OUT_BCLK, I2S_OUT_WS, I2S_OUT_DIN);
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
    (void)drv;
    static uint32_t last_reprobe_ms = 0;
    if (!g_touch_present) {
        const uint32_t now = millis();
        if (now - last_reprobe_ms > 1000) {
            last_reprobe_ms = now;
            uint8_t chip_id = 0;
            g_touch_present = ft6336_read_bytes(FT6336U_REG_CHIP_ID, &chip_id, 1) && chip_id == 0x64;
        }
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    static uint8_t i2c_fail_streak = 0;
    uint8_t regs[5] = {0};

    if (!ft6336_read_bytes(FT6336U_REG_TD_STATUS, regs, sizeof(regs))) {
        data->state = LV_INDEV_STATE_REL;
        if (++i2c_fail_streak > 20) {
            g_touch_present = false;
        }
        return; 
    }

    const uint8_t touches = regs[0] & 0x0F;
    const uint8_t p1_xh = regs[0];
    const uint8_t p1_xl = regs[1];
    const uint8_t p1_yh = regs[2];
    const uint8_t p1_yl = regs[3];

    if (touches > 0) {
        const uint8_t event = (p1_xh >> 6) & 0x03;
        if (event == 0x01) {
            data->state = LV_INDEV_STATE_REL;
            i2c_fail_streak = 0;
            return;
        }

        uint16_t x = ((p1_xh & 0x0F) << 8) | p1_xl;
        uint16_t y = ((p1_yh & 0x0F) << 8) | p1_yl;

        if (x > 319) x = 319;
        if (y > 479) y = 479;

        data->point.x = (int16_t)x;
        data->point.y = (int16_t)y;
        data->state = LV_INDEV_STATE_PR;
        i2c_fail_streak = 0;
        return;
    }
    
    data->state = LV_INDEV_STATE_REL;
}

// === AFFICHAGE HAUTES PERFORMANCES (DMA + MUTEX) ===
void _disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    // LVGL peut fournir des zones partiellement hors ecran: clip defensif obligatoire.
    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;

    if (x2 < 0 || y2 < 0 || x1 > 319 || y1 > 479) {
        lv_disp_flush_ready(disp);
        return;
    }

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > 319) x2 = 319;
    if (y2 > 479) y2 = 479;

    uint32_t w = (uint32_t)(x2 - x1 + 1);
    uint32_t h = (uint32_t)(y2 - y1 + 1);
    uint32_t len = w * h;
    uint32_t src_w = (uint32_t)(area->x2 - area->x1 + 1);

    if (len == 0) {
        lv_disp_flush_ready(disp);
        return;
    }

    // 1. VERROUILLAGE SÉCURISÉ DU SPI (Pour protéger la carte SD)
    mutex_enter_blocking(&spi_mutex);

    tft.startWrite();
    tft.setAddrWindow(x1, y1, w, h);

    // 2. Envoi pixel: DMA si disponible, sinon mode direct (fallback de securite).
    int32_t src_x_off = x1 - area->x1;
    int32_t src_y_off = y1 - area->y1;

    // En cas de clipping horizontal, les lignes ne sont plus contigues en memoire.
    // On envoie donc ligne par ligne pour garantir la coherence des donnees.
    for (uint32_t row = 0; row < h; ++row) {
        uint32_t src_index = (uint32_t)(src_y_off + (int32_t)row) * src_w + (uint32_t)src_x_off;
        lv_color_t* src_line = color_p + src_index;

        tft.pushPixels((uint16_t *)&src_line->full, w);
    }

    tft.endWrite();
    
    // 4. LIBÉRATION SÉCURISÉE DU SPI
    mutex_exit(&spi_mutex);

    lv_disp_flush_ready(disp);
}

void hardware_init() {
    pinMode(LCD_BL_PIN, OUTPUT); 
    digitalWrite(LCD_BL_PIN, HIGH);

    // 1) Bring-up ecran en tout premier pour eviter tout blocage annexe.
    SPI_PORT.setTX(LCD_MOSI_PIN);
    SPI_PORT.setSCK(LCD_CLK_PIN);
    SPI_PORT.begin();

    pinMode(LCD_RST_PIN, OUTPUT);
    digitalWrite(LCD_RST_PIN, LOW);
    delay(20);
    digitalWrite(LCD_RST_PIN, HIGH);
    delay(120);

    tft.init();
    tft.setRotation(0);
    tft.writecommand(0x11); // Sleep OUT
    delay(120);
    tft.writecommand(0x29); // Display ON
    delay(20);

    tft.fillScreen(TFT_BLACK);
    tft.drawBitmap(0, (480 - 140)/2, epd_bitmap_Startup_Logo, 320, 140, TFT_WHITE);
    tft.drawBitmap((320-61)/2, 480-45, epd_bitmap_marsisus_logo, 61, 18, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(8, 8);
    tft.print("Initialisation...");
    g_boot_stage_y = 24;
    boot_stage("display init ok");

    g_tft_dma_ready = tft.initDMA();
    boot_stage(g_tft_dma_ready ? "dma init ok" : "dma init off", TFT_CYAN);

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
    boot_stage("lvgl display driver ok", TFT_GREEN);
    g_boot_stage_onscreen_enabled = false;

    pinMode(SLEEP_BTN_PIN, INPUT_PULLUP);
}

inline void hardware_deferred_init() {
    if (!g_hw_deferred_init_pending) {
        return;
    }

    boot_stage("deferred hw init start", TFT_CYAN);

    // Annexes deplacees hors chemin critique du boot UI.
    battery::begin();
    boot_stage("battery init ok");

    audio_pins_quiet();
    boot_stage("audio pins quiet ok");

#if TEMP_DISABLE_TOUCH_INIT
    g_touch_present = false;
    boot_stage("touch init temp disabled", TFT_YELLOW);
#else
    boot_stage("touch init start", TFT_CYAN);

    // FT6336U driver Waveshare: seul le reset tactile est nécessaire ici.
    pinMode(TP_RST, OUTPUT);
    digitalWrite(TP_RST, HIGH);
    delay(10);
    digitalWrite(TP_RST, LOW);
    delay(10);
    digitalWrite(TP_RST, HIGH);
    delay(50);
    boot_stage("touch reset ok", TFT_CYAN);

    pinMode(TP_INT, INPUT_PULLUP);

    boot_stage("touch i2c init start", TFT_CYAN);
    ft6336_i2c_hw_init();
    boot_stage("touch i2c init ok", TFT_CYAN);

    boot_stage("touch probe start", TFT_CYAN);
    ft6336_reset_hw();
    boot_stage("touch reset sequence ok", TFT_CYAN);

    g_touch_present = ft6336_is_present();
    if (g_touch_present) {
        uint8_t chip_id = 0;
        uint8_t focal_id = 0;
        boot_stage("touch read ids start", TFT_CYAN);
        (void)ft6336_read_bytes(FT6336U_REG_CHIP_ID, &chip_id, 1);
        (void)ft6336_read_bytes(FT6336U_REG_FOCALTECH_ID, &focal_id, 1);
        boot_stage("touch read ids ok", TFT_CYAN);
        boot_stage("touch config start", TFT_CYAN);
        (void)ft6336_write_byte(FT6336U_REG_G_MODE, 0x00); // polling mode
        (void)ft6336_write_byte(FT6336U_REG_GESTURE_EN, 0x00);
        boot_stage("touch config ok", TFT_CYAN);

        char msg[40];
        snprintf(msg, sizeof(msg), "touch ft6336 id:%02X/%02X", chip_id, focal_id);
        boot_stage(msg, TFT_GREEN);
    } else {
        boot_stage("touch not detected", TFT_YELLOW);
    }

    if (g_touch_present) {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = _touch_read;
        lv_indev_drv_register(&indev_drv);
        boot_stage("lvgl touch driver ok", TFT_GREEN);
    }
#endif

    // Temporairement desactive pour isoler les crashes modem/LTE.
    // LTE::setLowPower(false);
    boot_stage("lte lowpower skipped", TFT_YELLOW);

    g_hw_deferred_init_pending = false;
    boot_stage("deferred hw init done", TFT_GREEN);
}

// Dans Hardware.h
void hardware_sleep() {
    mutex_enter_blocking(&spi_mutex);
    digitalWrite(LCD_BL_PIN, LOW);
    tft.writecommand(0x10); // Sleep écran
    mutex_exit(&spi_mutex);

    // ENDORMIR LE TACTILE (FT6336U Mode Sleep)
    I2C_PORT.beginTransmission(FT6336U_ADDR);
    I2C_PORT.write(0xA5); // Registre Power Mode
    I2C_PORT.write(0x03); // Valeur pour "Sleep Mode"
    I2C_PORT.endTransmission();

    // Serial1.println("AT+CSCLK=2"); // Endormir le modem LTE
}


// Appelle hardware_sleep() pour mettre en veille, hardware_wake() pour réveiller.
// Pour sortir de veille : détecter touche ou tactile (TP_INT ou autre GPIO)

void check_sleep_button() {
#if TEMP_DISABLE_POWER_BUTTON
    return;
#endif

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
    SPI_PORT.begin(); 
    
    // RÉVEILLER LE TACTILE AVEC UN RESET MATÉRIEL
    digitalWrite(TP_RST, LOW);
    delay(10);
    digitalWrite(TP_RST, HIGH);
    delay(50); // Le FT6336U a besoin de temps pour redémarrer
    
    tft.startWrite();
    tft.writecommand(0x11); // Sleep Out
    tft.endWrite();
    
    delay(120); 

    tft.startWrite();
    tft.writecommand(0x29); // Display ON
    tft.endWrite();

    digitalWrite(LCD_BL_PIN, HIGH);
    Serial1.println("AT+CSCLK=0"); // Réveiller le modem LTE
    
    tft.fillScreen(TFT_BLACK); 
}

// À appeler dans loop() : check_sleep_button();
// Initialisation dans hardware_init()

#endif