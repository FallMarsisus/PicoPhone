#ifndef VELIB_APP_H
#define VELIB_APP_H

#include "App.h"
#include <vector>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <pico/mutex.h>
#include "../system/LTE.h"
#include "../system/NetworkErrorHandler.h"

// --- CONFIGURATION ---
// On prend plusieurs résultats puis on filtre côté code station exact
#define API_BASE "http://opendata.paris.fr/api/records/1.0/search/?dataset=velib-disponibilite-en-temps-reel&rows=20&q="
#define REFRESH_RATE 5000 

struct Station {
    String name;      
    String code;      
    int mech = 0;     
    int elec = 0;     
    int park = 0;     
    bool is_updated = false; 
    
    // UI Pointers
    lv_obj_t* lbl_mech = nullptr;
    lv_obj_t* lbl_elec = nullptr;
    lv_obj_t* lbl_park = nullptr;
};

auto_init_mutex(velibMutex);

class VelibApp : public App {
private:
    lv_obj_t* ui_root;
    lv_obj_t* list_cont;
    
    // UI Ajout
    lv_obj_t* add_panel;
    lv_obj_t* ta_code;
    lv_obj_t* kb;
    lv_obj_t* lbl_status; 

    std::vector<Station> stations;
    unsigned long last_update = 0;
    
    // Communication Ajout
    String pending_code_search = ""; 
    String searching_code = "";
    bool search_finished = false;
    String search_payload = "";
    bool search_success = false;

    static int json_to_int(const JsonVariantConst& v) {
        if (v.is<int>()) return v.as<int>();
        if (v.is<long>()) return (int)v.as<long>();
        if (v.is<const char*>()) return atoi(v.as<const char*>());
        return 0;
    }

    static void extract_counts(const JsonObjectConst& fields, int& mech, int& elec, int& park) {
        mech = 0;
        elec = 0;
        park = 0;

        if (fields["numdocksavailable"].is<int>() || fields["numdocksavailable"].is<const char*>()) {
            park = json_to_int(fields["numdocksavailable"]);
        }

        if (fields["numbikesavailable"].is<int>() || fields["numbikesavailable"].is<const char*>()) {
            int total = json_to_int(fields["numbikesavailable"]);
            elec = json_to_int(fields["ebike"]);
            mech = total - elec;
            if (mech < 0) mech = 0;
        }

        JsonVariantConst by_type = fields["num_bikes_available_types"];
        if (by_type.is<JsonArrayConst>()) {
            int local_mech = 0;
            int local_elec = 0;
            for (JsonObjectConst t : by_type.as<JsonArrayConst>()) {
                if (t["mechanical"].is<int>() || t["mechanical"].is<const char*>()) {
                    local_mech = json_to_int(t["mechanical"]);
                }
                if (t["ebike"].is<int>() || t["ebike"].is<const char*>()) {
                    local_elec = json_to_int(t["ebike"]);
                }
            }
            if (local_mech > 0 || local_elec > 0) {
                mech = local_mech;
                elec = local_elec;
            }
        }
    }

    bool already_exists_code(const String& code) {
        for (const auto& s : stations) {
            if (s.code == code) return true;
        }
        return false;
    }

    // --- PERSISTANCE ---

    void save_stations() {
        File f = LittleFS.open("/velib.json", "w");
        if (!f) return;
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        
        mutex_enter_blocking(&velibMutex);
        for(const auto& s : stations) {
            JsonObject obj = arr.add<JsonObject>();
            obj["n"] = s.name;
            obj["c"] = s.code;
        }
        mutex_exit(&velibMutex);
        
        serializeJson(arr, f);
        f.close();
    }

    void load_stations() {
        if (!LittleFS.exists("/velib.json")) return;

        File f = LittleFS.open("/velib.json", "r");
        if (!f) return;
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, f);
        f.close();

        if (!error) {
            mutex_enter_blocking(&velibMutex);
            stations.clear();
            JsonArray arr = doc.as<JsonArray>();
            for(JsonObject obj : arr) {
                Station s;
                s.name = obj["n"].as<String>();
                s.code = obj["c"].as<String>();
                stations.push_back(s);
            }
            mutex_exit(&velibMutex);
        }
    }

    // --- UI HELPERS ---

    lv_obj_t* create_badge(lv_obj_t* parent, const char* icon, uint32_t color, int x_offset) {
        lv_obj_t* cont = lv_obj_create(parent);
        lv_obj_set_size(cont, 48, 25);
        lv_obj_align(cont, LV_ALIGN_BOTTOM_LEFT, x_offset, 0);
        lv_obj_set_style_bg_color(cont, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(cont, 0, 0);
        lv_obj_set_style_radius(cont, 5, 0);
        lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* l_icon = lv_label_create(cont);
        lv_label_set_text(l_icon, icon);
        lv_obj_set_style_text_color(l_icon, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_icon, &lv_font_montserrat_12, 0);
        lv_obj_align(l_icon, LV_ALIGN_LEFT_MID, -3, 0);

        lv_obj_t* l_val = lv_label_create(cont);
        lv_label_set_text(l_val, "-");
        lv_obj_set_style_text_color(l_val, lv_color_white(), 0);
        lv_obj_set_style_text_font(l_val, &lv_font_montserrat_12, 0);
        lv_obj_align(l_val, LV_ALIGN_RIGHT_MID, 3, 0);
        
        return l_val;
    }

    void refresh_list_ui() {
        lv_obj_clean(list_cont);

        mutex_enter_blocking(&velibMutex);
        for(int i=0; i<stations.size(); i++) {
            Station &s = stations[i];

            lv_obj_t* card = lv_obj_create(list_cont);
            lv_obj_set_size(card, lv_pct(100), 70);
            lv_obj_set_style_bg_color(card, lv_color_hex(0xECF0F1), 0);
            lv_obj_set_style_radius(card, 10, 0);
            lv_obj_set_style_border_width(card, 0, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t* lbl_name = lv_label_create(card);
            lv_label_set_text(lbl_name, s.name.c_str());
            lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(lbl_name, lv_color_hex(0x2C3E50), 0);
            lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 0, -5);
            lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_width(lbl_name, 200);

            s.lbl_mech = create_badge(card, LV_SYMBOL_SETTINGS, 0x27AE60, 0);
            s.lbl_elec = create_badge(card, LV_SYMBOL_CHARGE, 0x2980B9, 53);
            s.lbl_park = create_badge(card, "P", 0x7F8C8D, 106);

            lv_obj_t* btn_del = lv_btn_create(card);
            lv_obj_set_size(btn_del, 25, 25);
            lv_obj_align(btn_del, LV_ALIGN_RIGHT_MID, 5, 0);
            lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xE74C3C), 0);
            lv_obj_set_user_data(btn_del, (void*)(intptr_t)i);
            lv_obj_add_event_cb(btn_del, delete_event, LV_EVENT_CLICKED, this);
            
            lv_obj_t* l_del = lv_label_create(btn_del);
            lv_label_set_text(l_del, LV_SYMBOL_TRASH);
            lv_obj_center(l_del);
        }
        mutex_exit(&velibMutex);
    }

    // --- EVENTS ---

    static void delete_event(lv_event_t* e) {
        VelibApp* app = (VelibApp*)lv_event_get_user_data(e);
        lv_obj_t* btn = lv_event_get_target(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(btn);

        mutex_enter_blocking(&velibMutex);
        if(idx >= 0 && idx < app->stations.size()) {
            app->stations.erase(app->stations.begin() + idx);
        }
        mutex_exit(&velibMutex);

        app->save_stations();
        app->refresh_list_ui();
    }

    static void show_add_panel(lv_event_t* e) {
        VelibApp* app = (VelibApp*)lv_event_get_user_data(e);
        lv_obj_clear_flag(app->add_panel, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(app->ta_code, "");
        lv_label_set_text(app->lbl_status, "Entrez code station:");
    }

    static void hide_add_panel(lv_event_t* e) {
        VelibApp* app = (VelibApp*)lv_event_get_user_data(e);
        lv_obj_add_flag(app->add_panel, LV_OBJ_FLAG_HIDDEN);
        app->pending_code_search = "";
    }

    static void request_search_event(lv_event_t* e) {
        VelibApp* app = (VelibApp*)lv_event_get_user_data(e);
        const char* code = lv_textarea_get_text(app->ta_code);
        if(strlen(code) > 0) {
            lv_label_set_text(app->lbl_status, "Recherche...");
            app->search_finished = false;
            app->pending_code_search = String(code);
        }
    }
    
    // Helper : GET via LTE
    static String netGet(const String& url) {
        if (LTE::isReadyForData()) {
            return LTE::httpGetBlocking(url);
        }
        return "";
    }

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

public:
    void start(lv_obj_t* parent) override {
        if (!LittleFS.begin()) { LittleFS.format(); LittleFS.begin(); }
        
        ui_root = parent;
        lv_obj_set_style_bg_color(ui_root, lv_color_hex(0xBDC3C7), 0);

        // Header
        lv_obj_t* header = lv_obj_create(ui_root);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x8E44AD), 0); 
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_home, LV_EVENT_CLICKED, NULL);
        lv_label_set_text(lv_label_create(btn_back), LV_SYMBOL_LEFT);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Velib'");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);

        lv_obj_t* btn_add = lv_btn_create(header);
        lv_obj_set_size(btn_add, 40, 40);
        lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, 10, 0);
        lv_obj_set_style_bg_opa(btn_add, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_add, show_add_panel, LV_EVENT_CLICKED, this);
        lv_label_set_text(lv_label_create(btn_add), LV_SYMBOL_PLUS);

        // Liste
        list_cont = lv_obj_create(ui_root);
        lv_obj_set_size(list_cont, 320, 430 - 18);  
        lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(list_cont, 10, 0);

        // Panel Ajout
        add_panel = lv_obj_create(ui_root);
        lv_obj_set_size(add_panel, 320, 480);
        lv_obj_center(add_panel);
        lv_obj_set_style_bg_color(add_panel, lv_color_hex(0x333333), 0);
        lv_obj_add_flag(add_panel, LV_OBJ_FLAG_HIDDEN);

        lbl_status = lv_label_create(add_panel);
        lv_label_set_text(lbl_status, "Code Station:");
        lv_obj_set_style_text_color(lbl_status, lv_color_white(), 0);
        lv_obj_align(lbl_status, LV_ALIGN_TOP_MID, 0, 20);

        ta_code = lv_textarea_create(add_panel);
        lv_obj_set_size(ta_code, 150, 40);
        lv_obj_align(ta_code, LV_ALIGN_TOP_MID, 0, 50);
        lv_textarea_set_max_length(ta_code, 5);
        lv_textarea_set_one_line(ta_code, true);

        kb = lv_keyboard_create(add_panel);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
        lv_keyboard_set_textarea(kb, ta_code);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(kb, request_search_event, LV_EVENT_READY, this);
        lv_obj_add_event_cb(kb, hide_add_panel, LV_EVENT_CANCEL, this);

        load_stations();
        refresh_list_ui();
    }

    // --- CORE 0 : UI (AVEC MUTEX MAINTENANT !) ---
    void update() override {
        // C'EST ICI LE FIX : On lock pour ne pas lire un vecteur en cours de modification
        if(mutex_try_enter(&velibMutex, nullptr)) {
            for(auto &s : stations) {
                if(s.is_updated) {
                    if(s.lbl_mech) lv_label_set_text_fmt(s.lbl_mech, "%d", s.mech);
                    if(s.lbl_elec) lv_label_set_text_fmt(s.lbl_elec, "%d", s.elec);
                    if(s.lbl_park) lv_label_set_text_fmt(s.lbl_park, "%d", s.park);
                    s.is_updated = false;
                }
            }
            mutex_exit(&velibMutex);
        }

        // Fin de recherche (Géré par Core 0 pour éviter les crashs)
        if (search_finished) {
            bool found = false;

            if (search_payload.length() > 0) {
                JsonDocument doc;
                DeserializationError error = deserializeJson(doc, search_payload);
                
                if (!error && doc["records"].is<JsonArray>()) {
                    JsonArray records = doc["records"].as<JsonArray>();
                    for (JsonObject rec : records) {
                        JsonObject fields = rec["fields"];
                        String codeFromApi = fields["stationcode"].as<String>();
                        if (codeFromApi.length() == 0) {
                            codeFromApi = searching_code;
                        }
                        if (codeFromApi != searching_code) continue;

                        String rawName = fields["name"].as<String>();
                        int dash = rawName.indexOf('-');
                        if (dash > 0) rawName = rawName.substring(dash + 2);

                        int mech = 0, elec = 0, park = 0;
                        extract_counts(fields, mech, elec, park);

                        mutex_enter_blocking(&velibMutex);
                        if (!already_exists_code(searching_code)) {
                            Station s;
                            s.code = searching_code;
                            s.name = rawName;
                            s.mech = mech;
                            s.elec = elec;
                            s.park = park;
                            stations.push_back(s);
                            found = true;
                        }
                        mutex_exit(&velibMutex);
                        break;
                    }
                }
            }

            if (found) {
                save_stations();
                refresh_list_ui();
                lv_obj_add_flag(add_panel, LV_OBJ_FLAG_HIDDEN);
                lv_textarea_set_text(ta_code, "");
            } else {
                lv_label_set_text(lbl_status, "Introuvable ou Erreur !");
            }

            // Nettoyage final
            search_payload = "";
            search_finished = false; 
            searching_code = "";
        }
    }

    // --- CORE 1 : RESEAU ---
    void update1() override {
        bool net_ok = LTE::isReadyForData();
        if (!net_ok) return;

       // A. RECHERCHE
        if (pending_code_search != "") {
            searching_code = pending_code_search;
            pending_code_search = "";

            String payload = netGet(String(API_BASE) + searching_code);
            search_payload = payload;
            search_finished = true;
            return;
        }

        // B. BACKGROUND REFRESH
        if(millis() - last_update > REFRESH_RATE) { 
            String batchQuery = "";

            mutex_enter_blocking(&velibMutex);
            for (const auto& s : stations) {
                if (s.code.length() == 0) {
                    continue;
                }
                if (batchQuery.length() > 0) {
                    batchQuery += "%20OR%20";
                }
                batchQuery += s.code;
            }
            mutex_exit(&velibMutex);

            if(batchQuery.length() > 0) {
                String payload = netGet(String(API_BASE) + batchQuery);
                if (payload.length() > 0) {
                    JsonDocument doc;
                    DeserializationError err = deserializeJson(doc, payload);
                    if (err) {
                        Serial.printf("[VELIB] JSON invalide (%s), len=%u\n", err.c_str(), (unsigned)payload.length());
                    } else if (doc["records"].is<JsonArray>() && doc["records"].size() > 0) {
                        JsonArray records = doc["records"].as<JsonArray>();
                        int updated_count = 0;

                        mutex_enter_blocking(&velibMutex);
                        for (auto &station : stations) {
                            bool matched = false;
                            int mech = 0, elec = 0, park = 0;

                            for (JsonObject rec : records) {
                                JsonObject fields = rec["fields"];
                                String codeFromApi = fields["stationcode"].as<String>();
                                if (codeFromApi.length() == 0) {
                                    continue;
                                }
                                if (codeFromApi != station.code) {
                                    continue;
                                }

                                extract_counts(fields, mech, elec, park);
                                matched = true;
                                break;
                            }

                            if (matched) {
                                station.mech = mech;
                                station.elec = elec;
                                station.park = park;
                                station.is_updated = true;
                                updated_count++;
                            }
                        }
                        mutex_exit(&velibMutex);

                        if (updated_count == 0) {
                            Serial.printf("[VELIB] Aucun match stationcode (records=%u)\n", (unsigned)records.size());
                        }
                    }
                } else {
                    Serial.println("[VELIB] payload vide pour requete groupee");
                }
            }
            last_update = millis();
        }
    }
};

#endif