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
#include <XPowersLib.h>
#include "drivers/FT6336U.h"
#include "assets/startuplogo.c"
#include "system/Battery.h"
#include "system/Logger.h"
#include "AppManager.h"

extern AppManager manager;
void DEV_I2C_Write_Byte(uint8_t addr, uint8_t reg, uint8_t Value);
void DEV_KEY_Config(uint16_t Pin);
uint8_t DEV_Module_Init(void);

// Création de l'objet pour le PMIC AXP2101
XPowersAXP2101 PMIC;

#define I2C_PORT i2c0


// --- PINS ECRAN (Waveshare RP2350-Touch-LCD-3.5) ---
#define LCD_RST_PIN  23
#define LCD_DC_PIN   20
#define LCD_BL_PIN   22
#define LCD_CS_PIN   21
#define LCD_CLK_PIN  18
#define LCD_MOSI_PIN 19
#define LCD_MISO_PIN 4

// --- PINS TACTILE CAPACITIF ET PMIC ---

// --- PINS IMU / CAPTEURS ---

#define DEV_SDA_PIN   34
#define DEV_SCL_PIN   35
#define DOF_INT1      14

#define Touch_RST_PIN 24
#define Touch_INT_PIN 25
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

// I2S / ES8311
#define I2S_DSDIN    12  // MCU -> ES8311 DIN
#define I2S_ASDOUT   13  // ES8311 DOUT -> MCU
#define I2S_MCLK     14
#define I2S_LRCK     15
#define I2S_SCLK     16
#define PA_CTRL_PIN  17

#define I2S_OUT_BCLK I2S_SCLK
#define I2S_OUT_WS   I2S_LRCK
#define I2S_OUT_DIN  I2S_DSDIN

#define I2S_IN_BCLK  I2S_SCLK
#define I2S_IN_WS    I2S_LRCK
#define I2S_IN_DOUT  I2S_ASDOUT

// Pour le SIM800L / A7670E (UART0)
#define SIM800_TX    1
#define SIM800_RX    0
#define A7670_PWRKEY 2

// --- BOUTON VEILLE ---
// Bouton physique sur GP47.
#define SLEEP_BTN_PIN 47

TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static constexpr uint32_t LV_BUF_PIXELS = 320u * 140u;
static lv_color_t buf1[LV_BUF_PIXELS];
static lv_color_t buf2[LV_BUF_PIXELS];
static bool g_tft_dma_ready = false;
static bool g_touch_present = false;
static volatile bool g_touch_irq_pending = false;
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

static inline void audio_pins_quiet() {
    pinMode(PA_CTRL_PIN, OUTPUT);
    digitalWrite(PA_CTRL_PIN, LOW);

    pinMode(I2S_OUT_DIN, INPUT_PULLDOWN);
    pinMode(I2S_OUT_BCLK, INPUT_PULLDOWN);
    pinMode(I2S_OUT_WS, INPUT_PULLDOWN);
    pinMode(I2S_IN_DOUT, INPUT_PULLDOWN);
    pinMode(I2S_IN_BCLK, INPUT_PULLDOWN);
    pinMode(I2S_IN_WS, INPUT_PULLDOWN);
}

static inline void audio_amp_enable(bool enable) {
    pinMode(PA_CTRL_PIN, OUTPUT);
    digitalWrite(PA_CTRL_PIN, enable ? HIGH : LOW);
}

// ─ Initialisation du codec audio ES8311 via I2C ─
static inline void es8311_init() {
    Serial.println("[AUDIO] Initialisation du codec ES8311...");
    
    // Reset du codec (REG0x00 = 0x1F puis 0x00)
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_RESET_REG00, 0x1F);
    sleep_ms(10);
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_RESET_REG00, 0x00);
    sleep_ms(50);
    
    // Configuration du mode d'horloge (REG01)
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_CLK_MANAGER_REG01, 0x00);  
    sleep_ms(10);
    
    // Configuration MCLK (REG03, REG04, REG05, REG08)
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_CLK_MANAGER_REG03, 0x10);  
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_CLK_MANAGER_REG04, 0x00);  
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_CLK_MANAGER_REG05, 0x00);  
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_CLK_MANAGER_REG08, 0x00); 
    
    // Configuration I2S en MODE SLAVE (REG09, REG0A)
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_SDPIN_REG09, 0x00);   // I2S Slave
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_SDPOUT_REG0A, 0x00);  // I2S Slave
    
    // Configuration audio mode (REG0B, REG0C, REG0D, REG0E, REG0F)
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_SYSTEM_REG0B, 0x00);
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_SYSTEM_REG0C, 0x00);
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_SYSTEM_REG0D, 0x0C);  // Mode slave
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_SYSTEM_REG0E, 0x02);  // Format 16-bit
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_SYSTEM_REG0F, 0x00);
    
    // ADC config (son qui rentre n'est pas critique ici)
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_ADC_REG15, 0x00);
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_ADC_REG16, 0x24);  
    
    // DAC PATH ENABLE (REG31, REG32, REG33, REG34 sont les clés)
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_DAC_REG31, 0x00);  // DAC L select
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_DAC_REG32, 0x00);  // DAC R select
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_DAC_REG33, 0xB8);  // DAC source from I2S
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_DAC_REG34, 0x20);  // DAC unmute & left/right enable
    
    // DAC GAIN & UNMUTE (REG35 = 0xB0 unmute, REG37, REG38, REG39)
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_DAC_REG35, 0xB0);  // UNMUTE DAC, gain 0dB
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_DAC_REG37, 0x88);  // HPF enable
    // DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_DAC_REG38, 0x00);  // DAC DVC config
    // DEV_I2C_Write_Byte(ES8311_I2C_ADDR, ES8311_DAC_REG39, 0x00);  
    
    sleep_ms(20);
    Serial.println("[AUDIO] ES8311 initialisé avec DAC actif");
}

static inline void modem_uart_begin() {
    Serial1.setTX(SIM800_RX);
    Serial1.setRX(SIM800_TX);
    Serial1.begin(115200);
}

static bool modem_boot_probe(uint8_t retries = 3) {
    modem_uart_begin();
    while (Serial1.available()) {
        Serial1.read();
    }

    for (uint8_t i = 0; i < retries; ++i) {
        Logger::printf("[LTE] Boot probe AT %u/%u\n", (unsigned)(i + 1), (unsigned)retries);
        Serial1.print('\r');
        sleep_ms(40);
        Serial1.println("AT");

        uint32_t start = millis();
        String resp;
        String line;
        while (millis() - start < 500) {
            while (Serial1.available()) {
                char c = (char)Serial1.read();
                resp += c;
                if (c == '\n') {
                    line.trim();
                    if (line.length() > 0) {
                        Logger::printf("[GSM->LTE] %s\n", line.c_str());
                        if (line.indexOf("OK") != -1) {
                            Logger::println("[LTE] Modem repond AT au boot");
                            return true;
                        }
                    }
                    line = "";
                } else if (c != '\r') {
                    line += c;
                }
            }
            sleep_ms(10);
        }

        if (resp.length() > 0) {
            String log = resp;
            log.replace("\r\n", " | ");
            log.trim();
            Logger::printf("[GSM->LTE] %s\n", log.c_str());
        }
        sleep_ms(120);
    }

    Logger::println("[LTE] Aucun OK sur AT au boot");
    return false;
}

static void touch_int_callback() {
    g_touch_irq_pending = true;
}
// Variable globale pour prévenir le Core 1 de l'extinction
volatile bool system_is_shutting_down = false;


void system_power_off() {
    Serial.println("[POWER] Extinction matérielle en cours...");
    // 1. Prévenir le Core 1 de tout arrêter immédiatement
    system_is_shutting_down = true;
    sleep_ms(50);

    // 2. COUPURE DU WIFI ET BLUETOOTH DU PICO W
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    sleep_ms(100);

    // LTE::setLowPower(true);
    watchdog_update();

    // 3. EXTINCTION DU MODEM A7670E via PWRKEY
    Serial.println("[LTE] Extinction matérielle via PWRKEY...");
    pinMode(A7670_PWRKEY, OUTPUT);
    digitalWrite(A7670_PWRKEY, HIGH);  // Pin inversé: HIGH pour éteindre
    sleep_ms(1500);

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
    sleep_ms(50);

    // 7. VERROUILLAGE DES PINS FLOTTANTS (CRUCIAL CONTRE LES FUITES)
    SPI.end();
    Wire.end();

    // On force les pins de l'écran et du bus SPI à GND
    pinMode(LCD_RST_PIN, OUTPUT); digitalWrite(LCD_RST_PIN, LOW);
    pinMode(LCD_CS_PIN, OUTPUT);  digitalWrite(LCD_CS_PIN, LOW);
    pinMode(LCD_DC_PIN, OUTPUT);  digitalWrite(LCD_DC_PIN, LOW);
    
    pinMode(LCD_CLK_PIN, OUTPUT);  digitalWrite(LCD_CLK_PIN, LOW);
    pinMode(LCD_MOSI_PIN, OUTPUT); digitalWrite(LCD_MOSI_PIN, LOW);
    pinMode(LCD_MISO_PIN, INPUT_PULLUP);

    // Extinction totale du tactile
    pinMode(Touch_RST_PIN, OUTPUT); digitalWrite(Touch_RST_PIN, LOW);
    pinMode(DEV_SDA_PIN, INPUT_PULLDOWN);
    pinMode(DEV_SCL_PIN, INPUT_PULLDOWN);

    Serial.println("[POWER] CPU Zzz...");
    Serial.flush(); 

    // 8. BAISSE DE L'HORLOGE ET DODO PROFOND
    // On passe le RP2040 de 133 MHz à 2 MHz (fait chuter la conso du processeur à ~1mA)
    set_sys_clock_khz(20000, true);

    while (true) {
        watchdog_update();
        // L'utilisation de sleep_ms(100) est gérée par le cœur Arduino pour mettre
        // le processeur en vraie veille, contrairement à un simple __wfi().
        sleep_ms(10); 

        if (digitalRead(SLEEP_BTN_PIN) == LOW) {
            uint32_t press_time = millis();
            bool valid_press = true;

            while (millis() - press_time < 100) {
                watchdog_update();
                if (digitalRead(SLEEP_BTN_PIN) == HIGH) {
                    valid_press = false; 
                    break; 
                }
                sleep_ms(10);
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

// --- FONCTION SONORE ---
void i2s_play_test_tone(int freq, int duration_ms, float gain = 0.6f) {
    // Non-static pour assurer une réinitialisation propre à chaque appel
    AudioOutputI2S out;

    audio_amp_enable(true);

    out.SetRate(44100);
    out.SetBitsPerSample(16);
    out.SetChannels(2);
    out.SetOutputModeMono(true);
    out.SetPinout(I2S_OUT_BCLK, I2S_OUT_WS, I2S_OUT_DIN);
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    out.SetGain(gain);
    
    if (!out.begin()) {
        Serial.println("[AUDIO] Test tone: out.begin() failed!");
        audio_pins_quiet();
        return;
    }

    const int sampleRate = 44100;
    const int half_period = (freq > 0) ? (sampleRate / freq / 2) : 0;
    const int samples = (sampleRate * duration_ms) / 1000;
    if (half_period <= 0 || samples <= 0) {
        out.stop();
        return;
    }

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
    // On garde l'ampli aligné avec le volume utilisateur au lieu de forcer un shutdown complet.
    audio_amp_enable(settings::getVolume() > 0);
}

// --- LECTURE TACTILE CAPACITIF I2C ---
void _touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;
    if (!g_touch_present) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    // Fallback polling: certaines cartes RP2350B ne remontent pas toujours
    // l'IRQ tactile via attachInterrupt, donc on sonde periodiquement.
    static uint32_t last_poll_ms = 0;
    const uint32_t now = millis();
    if (!g_touch_irq_pending) {
        if (now - last_poll_ms < 12) {
            data->state = LV_INDEV_STATE_REL;
            return;
        }
    }
    g_touch_irq_pending = false;
    last_poll_ms = now;

    if (FT6336U_ReadState(FT6336U_FINGER_NUMBER) == 0) {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    FT6336U_Get_Point();

    uint16_t x = FT6336U.touch1_x;
    uint16_t y = FT6336U.touch1_y;
    if (x > 319) x = 319;
    if (y > 479) y = 479;

    data->point.x = (int16_t)x;
    data->point.y = (int16_t)y;
    data->state = LV_INDEV_STATE_PR;
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
    if (src_x_off == 0 && src_w == w) {
        uint32_t src_index = (uint32_t)src_y_off * src_w;
        lv_color_t* src = color_p + src_index;
        tft.pushPixels((uint16_t *)&src->full, len);
    } else {
        for (uint32_t row = 0; row < h; ++row) {
            uint32_t src_index = (uint32_t)(src_y_off + (int32_t)row) * src_w + (uint32_t)src_x_off;
            lv_color_t* src_line = color_p + src_index;

            tft.pushPixels((uint16_t *)&src_line->full, w);
        }
    }

    tft.endWrite();
    
    // 4. LIBÉRATION SÉCURISÉE DU SPI
    mutex_exit(&spi_mutex);

    lv_disp_flush_ready(disp);
}

void hardware_init() {

    
    pinMode(LCD_BL_PIN, OUTPUT); 
    digitalWrite(LCD_BL_PIN, HIGH);

    Logger::begin();

    audio_amp_enable(true);

    // 1) Bring-up ecran en tout premier pour eviter tout blocage annexe.
    SPI.setTX(LCD_MOSI_PIN);
    SPI.setSCK(LCD_CLK_PIN);
    SPI.begin();

    Wire1.setSDA(DEV_SDA_PIN);
    Wire1.setSCL(DEV_SCL_PIN);
    Wire1.setClock(400 * 1000);
    Wire1.begin();

    if (!PMIC.init(Wire1, DEV_SDA_PIN, DEV_SCL_PIN)) {
        Serial.println("Erreur: Impossible de trouver le AXP2101 !");
        while (1);
    }

    PMIC.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ);
    PMIC.clearIrqStatus();
    pinMode(SYS_OUT_PIN, INPUT_PULLUP);

    pinMode(LCD_RST_PIN, OUTPUT);
    digitalWrite(LCD_RST_PIN, LOW);
    sleep_ms(20);
    digitalWrite(LCD_RST_PIN, HIGH);
    sleep_ms(120);

    tft.init();
    tft.setRotation(0);
    tft.writecommand(0x11); // Sleep OUT
    sleep_ms(120);
    tft.writecommand(0x29); // Display ON
    sleep_ms(20);

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
    
    es8311_init();
    boot_stage("audio codec init ok");
    
    audio_amp_enable(settings::getVolume() > 0);
    boot_stage("audio amp enabled");

    boot_stage("sim probe start", TFT_CYAN);
    const bool sim_ok = modem_boot_probe(4);
    boot_stage(sim_ok ? "sim at ok" : "sim no response", sim_ok ? TFT_GREEN : TFT_YELLOW);

#if TEMP_DISABLE_TOUCH_INIT
    g_touch_present = false;
    boot_stage("touch init temp disabled", TFT_YELLOW);
#else
    boot_stage("touch init start", TFT_CYAN);

    // FT6336U driver Waveshare.
    pinMode(Touch_RST_PIN, OUTPUT);
    digitalWrite(Touch_RST_PIN, HIGH);
    sleep_ms(10);
    digitalWrite(Touch_RST_PIN, LOW);
    sleep_ms(10);
    digitalWrite(Touch_RST_PIN, HIGH);
    sleep_ms(50);
    boot_stage("touch reset ok", TFT_CYAN);

    boot_stage("touch module init start", TFT_CYAN);
    
    SPI.endTransaction();
    boot_stage("touch module init ok", TFT_CYAN);

    boot_stage("touch probe start", TFT_CYAN);
    FT6336U_Init(FT6336U_Gesture_Mode);
    DEV_KEY_Config(Touch_INT_PIN);
    attachInterrupt(Touch_INT_PIN, touch_int_callback, RISING);
    const uint16_t chip_id = FT6336U_ReadID();
    g_touch_present = (chip_id == 0x64 || chip_id == 0x98);
    if (Serial) {
        if (g_touch_present) {
            Serial.print("[TOUCH] Chip ID lu : 0x");
            Serial.println(chip_id, HEX);
        } else {
            Serial.print("[TOUCH] Chip ID invalide : 0x");
            Serial.println(chip_id, HEX);
        }
    }

    if (g_touch_present) {
        boot_stage("touch driver ready", TFT_GREEN);
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

    // LTE::setLowPower(false);
    boot_stage("lte config on", TFT_GREEN);

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
    if (g_touch_present) {
        DEV_I2C_Write_Byte(FT6336U_I2C_ADDR, FT6336U_ADDR_POWER_MODE, 0x03);
    }
}

// Appelle hardware_sleep() pour mettre en veille, hardware_wake() pour réveiller.
// Pour sortir de veille : détecter touche ou tactile (TP_INT ou autre GPIO)

void check_sleep_button() {
#if TEMP_DISABLE_POWER_BUTTON
    return;
#endif

    static uint32_t pmic_poll_ms = 0;
    const uint32_t now = millis();
    const bool pmic_irq_active = (digitalRead(SYS_OUT_PIN) == LOW);
    if (pmic_irq_active && (now - pmic_poll_ms >= 20)) {
        pmic_poll_ms = now;
        PMIC.getIrqStatus();
    }

    // Priorite au bouton PMIC (K3 / PWRON) si les IRQ PEK sont remontees.
    if (PMIC.isPekeyLongPressIrq()) {
        Serial.println("Bouton K3 (PWRON) maintenu !");
        PMIC.clearIrqStatus();
        showPowerMenu();
        return;
    }

    if (PMIC.isPekeyShortPressIrq()) {
        Serial.println("Bouton K3 (PWRON) pressé brièvement !");
        PMIC.clearIrqStatus();
        if (!manager.lockScreen.isLocked()) {
            manager.lockScreen.lock();
        } else {
            manager.lockScreen.unlock();
        }
        return;
    }

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
    SPI.begin(); 
    
    // RÉVEILLER LE TACTILE AVEC UN RESET MATÉRIEL
    digitalWrite(Touch_RST_PIN, LOW);
    sleep_ms(10);
    digitalWrite(Touch_RST_PIN, HIGH);
    sleep_ms(50); // Le FT6336U a besoin de temps pour redémarrer
    
    tft.startWrite();
    tft.writecommand(0x11); // Sleep Out
    tft.endWrite();
    
    sleep_ms(120); 

    tft.startWrite();
    tft.writecommand(0x29); // Display ON
    tft.endWrite();

    digitalWrite(LCD_BL_PIN, HIGH);
    Serial1.println("AT+CSCLK=0"); // Réveiller le modem LTE
    
    tft.fillScreen(TFT_BLACK); 
}

// À appeler dans loop() : check_sleep_button();
// Initialisation dans hardware_init()


// Fonction physique de gestion du volume
inline void hardware_set_volume(int vol) {
    // 1. Gestion de l'amplificateur physique
    if (vol == 0) {
        digitalWrite(PA_CTRL_PIN, LOW); // Coupe l'ampli (Mute)
    } else {
        digitalWrite(PA_CTRL_PIN, HIGH); // Allume l'ampli
    }

    // 2. Gestion du volume numérique du DAC de l'ES8311 (Registre 0x32)ƒ
    // 0x00 = Volume Max (+24dB) | 0x50 = Volume modéré | 0xFF = Mute
    uint8_t reg_val;
    if (vol == 0) {
        reg_val = 0xFF;
    } else {
        // On map le pourcentage (1-100) vers la plage du registre ES8311
        // Attention : Plus la valeur I2C est PETITE, plus le son est FORT
        reg_val = map(vol, 1, 100, 0x50, 0x00); 
    }
    
    // Envoi de l'ordre à la puce
    DEV_I2C_Write_Byte(ES8311_I2C_ADDR, 0x32, reg_val);
}

#endif