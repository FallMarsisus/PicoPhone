#ifndef BAMBU_APP_H
#define BAMBU_APP_H

#include "App.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> 
#include <pico/mutex.h>
#include <hardware/watchdog.h>
#include "../system/NetworkErrorHandler.h"

namespace bambu_cfg {
    static constexpr const char* DEVICE_ID = "0309DA542100818";
    static constexpr const char* USER = "u_3437172096";
    static constexpr const char* PASS = "AAD1T9SE-tA3m_0y9O_Nm794UWHhlN2Nv9wUGReyZvS_pXgK-un5qBSX-uFo4Qob9ksAKPi81R4t3jXoCkYoxSdTj4NJjtCeWE0oEjmnve-aTlB3Ip_kJzlysTyc4vZPf_MOLXdSvCpcvO9I";
    static constexpr const char* HOST = "us.mqtt.bambulab.com";
    static constexpr uint16_t PORT = 8883;
}

// --- STRUCTURE DES DONNÉES (Boîte aux lettres Core 1 -> Core 0) ---
struct BambuData {
    String state = "Déconnecté";
    int progress = 0;
    int remaining_time = 0;
    int nozzle_temp = 0;
    int nozzle_target = 0;
    int bed_temp = 0;
    int bed_target = 0;
    int layer = 0;
    int total_layers = 0;
    bool is_connected = false;
};

// Mutex global pour protéger la mémoire entre les deux coeurs
auto_init_mutex(bambuMutex);
static BambuData sharedBambuData;
static bool bambu_has_new_data = false;

// --- DÉCLARATION GLOBALE MQTT (Pour le callback) ---
static WiFiClientSecure* secureBambuClient = nullptr;
static PubSubClient* mqttBambuClient = nullptr;
static String bambuTopicReport;
static String bambuTopicStatus;
static String bambuTopicPrinterState;
static String bambuTopicRequest;
static String bambuTopicPrinterCommand;

// --- FONCTION DE RÉCEPTION MQTT (Exécutée sur le Core 1) ---
static void bambuMqttCallback(char* topic, byte* payload, unsigned int length) {
    // 1. On configure le filtre pour ne lire que l'essentiel et économiser la RAM
    JsonDocument filter;
    filter["print"]["gcode_state"] = true;
    filter["print"]["mc_percent"] = true;
    filter["print"]["mc_remaining_time"] = true;
    filter["print"]["nozzle_temper"] = true;
    filter["print"]["nozzle_target_temper"] = true;
    filter["print"]["bed_temper"] = true;
    filter["print"]["bed_target_temper"] = true;
    filter["print"]["layer_num"] = true;
    filter["print"]["total_layer_num"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, length, DeserializationOption::Filter(filter));

    if (error) {
        Serial.print("[BAMBU] JSON parse error on topic ");
        Serial.print(topic ? topic : "(null)");
        Serial.print(": ");
        Serial.println(error.c_str());
        return;
    }

    if (doc.containsKey("print")) {
        JsonObject p = doc["print"];
        
        // 2. On récupère le Mutex pour mettre à jour la boîte aux lettres
        if (mutex_try_enter(&bambuMutex, nullptr)) {
            if (p.containsKey("gcode_state")) sharedBambuData.state = p["gcode_state"].as<String>();
            if (p.containsKey("mc_percent")) sharedBambuData.progress = p["mc_percent"].as<int>();
            if (p.containsKey("mc_remaining_time")) sharedBambuData.remaining_time = p["mc_remaining_time"].as<int>();
            
            if (p.containsKey("nozzle_temper")) sharedBambuData.nozzle_temp = (int)p["nozzle_temper"].as<float>();
            if (p.containsKey("nozzle_target_temper")) sharedBambuData.nozzle_target = (int)p["nozzle_target_temper"].as<float>();
            
            if (p.containsKey("bed_temper")) sharedBambuData.bed_temp = (int)p["bed_temper"].as<float>();
            if (p.containsKey("bed_target_temper")) sharedBambuData.bed_target = (int)p["bed_target_temper"].as<float>();
            
            if (p.containsKey("layer_num")) sharedBambuData.layer = p["layer_num"].as<int>();
            if (p.containsKey("total_layer_num")) sharedBambuData.total_layers = p["total_layer_num"].as<int>();
            
            sharedBambuData.is_connected = true;
            bambu_has_new_data = true;
            mutex_exit(&bambuMutex);
        }
    }
}

// On déclare les polices externes
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_18);
LV_FONT_DECLARE(lv_font_montserrat_28);

class BambuApp : public App {
private:
    lv_obj_t* main_bg;
    
    // UI Elements
    lv_obj_t* lbl_status;
    lv_obj_t* progress_bar;
    lv_obj_t* lbl_progress;
    lv_obj_t* lbl_time;
    lv_obj_t* lbl_nozzle;
    lv_obj_t* lbl_bed;
    lv_obj_t* lbl_layer;
    lv_obj_t* loader;

    unsigned long last_reconnect_attempt = 0;

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent; 
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x2C3E50), 0); // Gris foncé élégant
        
        // --- HEADER ---
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
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
        lv_obj_set_style_text_color(l_back, lv_color_white(), 0);
        lv_obj_center(l_back);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Imprimante 3D");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_center(title);

        // --- CARTE PRINCIPALE ---
        lv_obj_t* card = lv_obj_create(main_bg);
        lv_obj_set_size(card, 290, 360);
        lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 70);
        lv_obj_set_style_bg_color(card, lv_color_white(), 0);
        lv_obj_set_style_radius(card, 15, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        // Statut
        lbl_status = lv_label_create(card);
        lv_label_set_text(lbl_status, "Connexion...");
        lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_18, 0);
        lv_obj_set_style_text_color(lbl_status, lv_color_hex(0x27ae60), 0); // Vert
        lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 10);

        // Barre de progression
        progress_bar = lv_bar_create(card);
        lv_obj_set_size(progress_bar, 250, 20);
        lv_obj_align(progress_bar, LV_ALIGN_TOP_MID, 0, 60);
        lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x2ecc71), LV_PART_INDICATOR); // Joli Vert

        lbl_progress = lv_label_create(card);
        lv_label_set_text(lbl_progress, "0 %");
        lv_obj_set_style_text_font(lbl_progress, &lv_font_montserrat_28, 0);
        lv_obj_align(lbl_progress, LV_ALIGN_TOP_MID, 0, 90);

        // Temps restant
        lbl_time = lv_label_create(card);
        lv_label_set_text(lbl_time, "Temps restant : -- min");
        lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_time, lv_color_hex(0x7f8c8d), 0);
        lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 130);

        // Infos Températures et Couche
        lv_obj_t* info_cont = lv_obj_create(card);
        lv_obj_set_size(info_cont, 250, 150);
        lv_obj_align(info_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(info_cont, lv_color_hex(0xf1f2f6), 0);
        lv_obj_set_style_border_width(info_cont, 0, 0);
        lv_obj_clear_flag(info_cont, LV_OBJ_FLAG_SCROLLABLE);

        lbl_nozzle = lv_label_create(info_cont);
        lv_label_set_text(lbl_nozzle, "Buse : -- / -- °C");
        lv_obj_align(lbl_nozzle, LV_ALIGN_TOP_LEFT, 10, 20);

        lbl_bed = lv_label_create(info_cont);
        lv_label_set_text(lbl_bed, "Plateau : -- / -- °C");
        lv_obj_align(lbl_bed, LV_ALIGN_LEFT_MID, 10, 0);

        lbl_layer = lv_label_create(info_cont);
        lv_label_set_text(lbl_layer, "Couche : -- / --");
        lv_obj_align(lbl_layer, LV_ALIGN_BOTTOM_LEFT, 10, -20);

        // Loader réseau
        loader = lv_spinner_create(main_bg, 1000, 60);
        lv_obj_set_size(loader, 30, 30);
        lv_obj_align(loader, LV_ALIGN_TOP_RIGHT, -15, 10);

        // Configuration MQTT Sécurisée (1 seule fois)
        bambuTopicReport = String("device/") + bambu_cfg::DEVICE_ID + "/report";
        bambuTopicStatus = String("device/") + bambu_cfg::DEVICE_ID + "/status";
        bambuTopicPrinterState = String("printer/") + bambu_cfg::DEVICE_ID + "/state";
        bambuTopicRequest = String("device/") + bambu_cfg::DEVICE_ID + "/request";
        bambuTopicPrinterCommand = String("printer/") + bambu_cfg::DEVICE_ID + "/command";

        if (!secureBambuClient) {
            secureBambuClient = new WiFiClientSecure();
        }
        if (!mqttBambuClient && secureBambuClient) {
            mqttBambuClient = new PubSubClient(*secureBambuClient);
        }

        if (secureBambuClient) {
            secureBambuClient->setInsecure(); // Obligatoire pour BambuLab sans certificat
        }
        if (mqttBambuClient) {
            mqttBambuClient->setServer(bambu_cfg::HOST, bambu_cfg::PORT);
            mqttBambuClient->setCallback(bambuMqttCallback);
            // Les payloads "pushall" peuvent depasser 8K avec AMS/HMS -> augmenter le buffer.
            mqttBambuClient->setBufferSize(16384);
        }
    }
    
    // --- CORE 0 : MISE À JOUR DE L'INTERFACE ---
    void update() override {
        if (bambu_has_new_data) {
            // On récupère le colis
            if (!mutex_try_enter(&bambuMutex, nullptr)) return;
            BambuData data = sharedBambuData;
            bambu_has_new_data = false;
            mutex_exit(&bambuMutex);

            // Mise à jour UI
            lv_label_set_text(lbl_status, data.state.c_str());
            lv_bar_set_value(progress_bar, data.progress, LV_ANIM_ON);
            lv_label_set_text_fmt(lbl_progress, "%d %%", data.progress);
            lv_label_set_text_fmt(lbl_time, "Temps restant : %d min", data.remaining_time);
            
            lv_label_set_text_fmt(lbl_nozzle, "Buse : %d / %d °C", data.nozzle_temp, data.nozzle_target);
            lv_label_set_text_fmt(lbl_bed, "Plateau : %d / %d °C", data.bed_temp, data.bed_target);
            lv_label_set_text_fmt(lbl_layer, "Couche : %d / %d", data.layer, data.total_layers);
        }

        bool is_connected = false;
        if (mutex_try_enter(&bambuMutex, nullptr)) {
            is_connected = sharedBambuData.is_connected;
            mutex_exit(&bambuMutex);
        }

        // On masque le loader si on est connecté
        if (is_connected && !lv_obj_has_flag(loader, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);
        } else if (!is_connected && lv_obj_has_flag(loader, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_clear_flag(loader, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // --- CORE 1 : GESTION DE LA CONNEXION RÉSEAU (MQTT) ---
    void update1() override {
        watchdog_update();

        // 1. Vérification du WiFi
        if (WiFi.status() != WL_CONNECTED) {
            NetworkErrorHandler::showIfError("Bambu", "WiFi non connecté");
            return;
        }

        // 2. Gestion de la connexion MQTT
        if (!mqttBambuClient) {
            return;
        }

        if (!mqttBambuClient->connected()) {
            if (mutex_try_enter(&bambuMutex, nullptr)) {
                sharedBambuData.is_connected = false;
                sharedBambuData.state = "Connexion perdue...";
                bambu_has_new_data = true;
                mutex_exit(&bambuMutex);
            }

            // On tente de se reconnecter toutes les 5 secondes
            if (millis() - last_reconnect_attempt > 5000) {
                last_reconnect_attempt = millis();
                Serial.println("[BAMBU] Tentative de connexion MQTT...");
                
                String clientId = "PicoPhone-" + String(random(0xffff), HEX);
                
                if (mqttBambuClient->connect(clientId.c_str(), bambu_cfg::USER, bambu_cfg::PASS)) {
                    Serial.println("[BAMBU] Connecté au Cloud !");

                    bool sub_report = mqttBambuClient->subscribe(bambuTopicReport.c_str());
                    bool sub_status = mqttBambuClient->subscribe(bambuTopicStatus.c_str());
                    bool sub_state = mqttBambuClient->subscribe(bambuTopicPrinterState.c_str());
                    Serial.printf("[BAMBU] Subs report=%d status=%d state=%d\n", (int)sub_report, (int)sub_status, (int)sub_state);

                    // Format conforme doc: pushing.command=pushall (sans sequence_id obligatoire)
                    const char* pushall = "{\"pushing\":{\"command\":\"pushall\"}}";
                    bool pub_device = mqttBambuClient->publish(bambuTopicRequest.c_str(), pushall);
                    bool pub_printer = mqttBambuClient->publish(bambuTopicPrinterCommand.c_str(), pushall);
                    Serial.printf("[BAMBU] Pushall published device=%d printer=%d\n", (int)pub_device, (int)pub_printer);
                } else {
                    Serial.print("[BAMBU] Erreur MQTT : ");
                    Serial.println(mqttBambuClient->state());
                }
            }
        } else {
            // 3. Maintien de la boucle d'écoute MQTT
            mqttBambuClient->loop();
        }
    }

    void stop() override {
        // On se déconnecte proprement quand on quitte l'application
        if (mqttBambuClient && mqttBambuClient->connected()) {
            mqttBambuClient->disconnect();
        }

        delete mqttBambuClient;
        mqttBambuClient = nullptr;
        delete secureBambuClient;
        secureBambuClient = nullptr;

        if (mutex_try_enter(&bambuMutex, nullptr)) {
            sharedBambuData.is_connected = false;
            mutex_exit(&bambuMutex);
        }
    }
};

#endif