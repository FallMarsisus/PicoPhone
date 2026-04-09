#ifndef TOUCH_RAW_APP_H
#define TOUCH_CALIB

#include "App.h"
#include "Hardware.h" // Nécessaire pour accéder aux fonctions SPI

class TouchCalibApp : public App {
private:
    lv_obj_t* label_raw;
    lv_obj_t* label_instruction;

public:
    void start(lv_obj_t* parent) override {
        lv_obj_set_style_bg_color(parent, lv_color_black(), 0);

        // Instructions
        label_instruction = lv_label_create(parent);
        lv_label_set_text(label_instruction, "TOUCHEZ LES COINS\nNotez les valeurs X et Y");
        lv_obj_set_style_text_color(label_instruction, lv_color_white(), 0);
        lv_obj_set_style_text_align(label_instruction, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label_instruction, LV_ALIGN_TOP_MID, 0, 20);

        // Affichage des valeurs
        label_raw = lv_label_create(parent);
        lv_label_set_text(label_raw, "X: ???  Y: ???");
        lv_obj_set_style_text_color(label_raw, lv_color_hex(0x00FF00), 0); // Vert Matrix
        lv_obj_set_style_text_font(label_raw, &lv_font_montserrat_14, 0); // Police lisible
        lv_obj_align(label_raw, LV_ALIGN_CENTER, 0, 0);
    }

    void update() override {
        uint16_t x, y, z;
        
        // On appelle la fonction brute définie dans Hardware.h
        // (Assure-toi que touch_read_raw_spi est bien accessible ou copiée ici)
        // touch_read_spi(x, y, z);

        if (z > 200) { // Si on appuie
            lv_label_set_text_fmt(label_raw, "RAW X: %d\nRAW Y: %d\nZ: %d", x, y, z);
        }
        
        sleep_ms(20);
    }
};

#endif