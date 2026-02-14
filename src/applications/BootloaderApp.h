#ifndef BOOTLOADER_APP_H
#define BOOTLOADER_APP_H

#include "App.h"
#include "Arduino.h"
#include <RP2040Support.h>


class BootloaderApp : public App {
private:
    lv_obj_t* label_info;
    lv_obj_t* btn_back;
    lv_obj_t* btn_action;
    int counter = 0;

    static void back_btn_handler(lv_event_t* e) {
        AppManager::switchTo(APP_HOME);
    }
    // Callback statique pour le bouton (obligatoire en C++)
    static void btn_handler(lv_event_t* e) {
        BootloaderApp* app = (BootloaderApp*)lv_event_get_user_data(e);
        app->onBtnClick();
    }

    void onBtnClick() {
        rp2040.rebootToBootloader();
    }

public:
    void start(lv_obj_t* parent) override {
        // 1. Création d'un titre
        lv_obj_t* title = lv_label_create(parent);
        lv_label_set_text(title, "Informations systeme :");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 55);

        // 2. Création du label d'information système
        label_info = lv_label_create(parent);
        char sysinfo[128];
        snprintf(sysinfo, sizeof(sysinfo), "CPU Freq. : %d MHz\nFree Heap : %d/%d bytes", rp2040.f_cpu(), rp2040.getFreeHeap(), rp2040.getTotalHeap());
        lv_label_set_text(label_info, sysinfo);
        lv_obj_set_style_text_font(label_info, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label_info, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(label_info, LV_ALIGN_TOP_MID, 0, 72);

        // 3. Création du Bouton
        btn_action = lv_btn_create(parent);
        lv_obj_set_size(btn_action, 140, 40);
        lv_obj_center(btn_action);
        lv_obj_align(btn_action, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_add_event_cb(btn_action, btn_handler, LV_EVENT_CLICKED, this);

        // Label du bouton
        lv_obj_t* l = lv_label_create(btn_action);
        lv_label_set_text(l, LV_SYMBOL_REFRESH " Bootloader");
        lv_obj_center(l);

        // 4. Bouton retour
        btn_back = lv_btn_create(parent);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 5, 5);
        lv_obj_add_event_cb(btn_back, back_btn_handler, LV_EVENT_CLICKED, this);

        // Label du bouton
        lv_obj_t* l2 = lv_label_create(btn_back);
        lv_label_set_text(l2, LV_SYMBOL_LEFT);
        lv_obj_center(l2);


    }

    void update() override {
        // Ici tu peux mettre du code qui s'exécute en boucle
        // Par exemple lire un capteur
    }
};

#endif