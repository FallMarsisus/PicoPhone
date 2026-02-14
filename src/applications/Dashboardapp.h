#ifndef DASHBOARD_APP_H
#define DASHBOARD_APP_H

#include "App.h"
#include <WiFi.h>

class DashboardApp : public App {
private:
    lv_obj_t* label_info;
    lv_obj_t* btn_action;
    int counter = 0;

    // Callback statique pour le bouton (obligatoire en C++)
    static void btn_handler(lv_event_t* e) {
        DashboardApp* app = (DashboardApp*)lv_event_get_user_data(e);
        app->onBtnClick();
    }

    void onBtnClick() {
        counter++;
        lv_label_set_text_fmt(label_info, "Clics: %d", counter);
    }

public:
    void start(lv_obj_t* parent) override {
        // 1. Création d'un titre
        lv_obj_t* title = lv_label_create(parent);
        lv_label_set_text(title, "MON DASHBOARD");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

        // 2. Affichage IP
        lv_obj_t* ip_label = lv_label_create(parent);
        String ip = "WiFi: " + WiFi.localIP().toString();
        lv_label_set_text(ip_label, ip.c_str());
        lv_obj_align(ip_label, LV_ALIGN_BOTTOM_LEFT, 5, -5);

        // 3. Création du Bouton
        btn_action = lv_btn_create(parent);
        lv_obj_set_size(btn_action, 140, 60);
        lv_obj_center(btn_action);
        lv_obj_add_event_cb(btn_action, btn_handler, LV_EVENT_CLICKED, this);

        // Label du bouton
        lv_obj_t* l = lv_label_create(btn_action);
        lv_label_set_text(l, "ACTION !");
        lv_obj_center(l);

        // 4. Label d'info
        label_info = lv_label_create(parent);
        lv_label_set_text(label_info, "En attente...");
        lv_obj_align(label_info, LV_ALIGN_BOTTOM_MID, 0, -30);
    }

    void update() override {
        // Ici tu peux mettre du code qui s'exécute en boucle
        // Par exemple lire un capteur
    }
};

#endif