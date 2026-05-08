#ifndef VECTOR_MAP_APP_H
#define VECTOR_MAP_APP_H

#include "App.h"
#include "AppManager.h"
#include <lvgl.h>
#include <pico/mutex.h>
#include "../system/LTE.h"

// 1. ON INCLUT TA VRAIE CARTE !
// Ce fichier contient le tableau map_database[], map_chunks[], NUM_ROADS et NUM_CHUNKS
#include "../assets/map_data.h" 

// --- DONNÉES PARTAGÉES ---
struct GpsData {
    float player_lat = 48.8575f; 
    float player_lon = 2.3525f;
    float camera_lat = 48.8575f; 
    float camera_lon = 2.3525f;
    bool is_fixed = false;
};

auto_init_mutex(gpsMutex);

class VectorMapApp : public App {
private:
    lv_obj_t* main_bg = nullptr;
    lv_obj_t* map_canvas = nullptr; 
    lv_obj_t* lbl_info = nullptr;
    
    // Boutons d'interface
    lv_obj_t* btn_zoom_in = nullptr;
    lv_obj_t* btn_zoom_out = nullptr;
    unsigned long last_drag_draw = 0;
    lv_obj_t* btn_recenter = nullptr;

    GpsData sharedGpsData;
    float current_zoom = 20000.0f; 
    bool is_following_player = true;
    unsigned long last_gps_poll = 0;

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    // --- LE MOTEUR VECTORIEL ULTRA-RAPIDE (AVEC TUILES) ---
    static void map_draw_event_cb(lv_event_t* e) {
        lv_obj_t* obj = lv_event_get_target(e);
        lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(e);
        VectorMapApp* app = (VectorMapApp*)lv_event_get_user_data(e);
        if (!app) return;

        float cam_lat = app->sharedGpsData.camera_lat;
        float cam_lon = app->sharedGpsData.camera_lon;
        float p_lat = app->sharedGpsData.player_lat;
        float p_lon = app->sharedGpsData.player_lon;
        float zoom = app->current_zoom;

        // 1. L.O.D AGRESSIF
        bool hide_nature        = (zoom < 14000.0f); 
        bool hide_small_streets = (zoom < 10000.0f);
        bool hide_water         = (zoom < 4000.0f);

        // 2. PRÉ-CALCULS MATHÉMATIQUES
        float zoom_x = zoom;
        float zoom_y = zoom * 1.5f;
        float offset_x = 160.0f - (cam_lon * zoom_x);
        float offset_y = 215.0f + (cam_lat * zoom_y);

        // Boîte englobante de l'écran 
        float view_width_deg = 320.0f / zoom_x;
        float view_height_deg = 430.0f / zoom_y;
        float min_lat = cam_lat - (view_height_deg / 2.0f);
        float max_lat = cam_lat + (view_height_deg / 2.0f);
        float min_lon = cam_lon - (view_width_deg / 2.0f);
        float max_lon = cam_lon + (view_width_deg / 2.0f);

        // 3. PINCEAUX 
        lv_draw_line_dsc_t line_dsc[5];
        for(int i=0; i<5; i++) {
            lv_draw_line_dsc_init(&line_dsc[i]);
        }

        line_dsc[1].color = lv_color_hex(0xFFA500); line_dsc[1].width = 4; // Gros axes
        line_dsc[2].color = lv_color_hex(0xFFFFFF); line_dsc[2].width = 2; // Petites rues
        line_dsc[3].color = lv_color_hex(0x74B9FF); line_dsc[3].width = 6; // Eau
        line_dsc[4].color = lv_color_hex(0x55EFC4); line_dsc[4].width = 5; // Parcs

        lv_coord_t base_x = obj->coords.x1;
        lv_coord_t base_y = obj->coords.y1;

        // 4. BOUCLE D'AFFICHAGE PAR TUILES SPATIALES
        for (int chunk_idx = 0; chunk_idx < NUM_CHUNKS; chunk_idx++) {
            const MapChunk& chunk = map_chunks[chunk_idx];

            // 🚀 REJET DE TUILE : Si le carré géographique de la tuile n'est pas à l'écran, on saute TOUTES ses routes !
            // On ajoute une marge de 0.005° au cas où une route de la tuile déborderait légèrement sur l'écran.
            if (chunk.max_lat < min_lat - 0.005f || chunk.min_lat > max_lat + 0.005f || 
                chunk.max_lon < min_lon - 0.005f || chunk.min_lon > max_lon + 0.005f) {
                continue; 
            }

            // Si on est là, c'est que la tuile est visible ! On dessine ses routes.
            int end_index = chunk.start_index + chunk.num_roads;
            for (int i = chunk.start_index; i < end_index; i++) {
                const VectorRoad& road = map_database[i];
                uint8_t type = road.type;

                // Rejets rapides
                if (type == 4 && hide_nature) continue;
                if (type == 2 && hide_small_streets) continue;
                if (type == 3 && hide_water) continue;

                if (road.lat1 < min_lat && road.lat2 < min_lat) continue;
                if (road.lat1 > max_lat && road.lat2 > max_lat) continue;
                if (road.lon1 < min_lon && road.lon2 < min_lon) continue;
                if (road.lon1 > max_lon && road.lon2 > max_lon) continue;

                // Projection Mathématique
                lv_coord_t x1 = (lv_coord_t)(offset_x + road.lon1 * zoom_x);
                lv_coord_t y1 = (lv_coord_t)(offset_y - road.lat1 * zoom_y);
                lv_coord_t x2 = (lv_coord_t)(offset_x + road.lon2 * zoom_x);
                lv_coord_t y2 = (lv_coord_t)(offset_y - road.lat2 * zoom_y);

                // Anti-Lag
                lv_coord_t dx = x2 - x1;
                lv_coord_t dy = y2 - y1;
                if (dx > -3 && dx < 3 && dy > -3 && dy < 3) continue; 

                // Dessin
                lv_point_t p1 = { (lv_coord_t)(base_x + x1), (lv_coord_t)(base_y + y1) };
                lv_point_t p2 = { (lv_coord_t)(base_x + x2), (lv_coord_t)(base_y + y2) };

                lv_draw_line(draw_ctx, &line_dsc[type], &p1, &p2);
            }
        }

        // --- DESSIN DU JOUEUR ---
        if (p_lat >= min_lat && p_lat <= max_lat && p_lon >= min_lon && p_lon <= max_lon) {
            lv_coord_t px = (lv_coord_t)(offset_x + p_lon * zoom_x);
            lv_coord_t py = (lv_coord_t)(offset_y - p_lat * zoom_y);

            lv_draw_rect_dsc_t player_dsc;
            lv_draw_rect_dsc_init(&player_dsc);
            player_dsc.bg_color = lv_color_hex(0x007AFF);
            player_dsc.radius = LV_RADIUS_CIRCLE;
            player_dsc.border_width = 2;
            player_dsc.border_color = lv_color_white();

            lv_area_t player_area;
            player_area.x1 = base_x + px - 6;
            player_area.y1 = base_y + py - 6;
            player_area.x2 = player_area.x1 + 12;
            player_area.y2 = player_area.y1 + 12;
            lv_draw_rect(draw_ctx, &player_dsc, &player_area);
        }
    }
    
    static void map_drag_event_cb(lv_event_t* e) {
        VectorMapApp* app = (VectorMapApp*)lv_event_get_user_data(e);
        lv_indev_t * indev = lv_indev_get_act();
        if(!indev || !app) return;

        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);

        if(vect.x != 0 || vect.y != 0) {
            app->is_following_player = false; 
            lv_obj_clear_flag(app->btn_recenter, LV_OBJ_FLAG_HIDDEN);

            float delta_lon = (float)(-vect.x) / app->current_zoom;
            float delta_lat = (float)(vect.y) / (app->current_zoom * 1.5f);

            if (mutex_try_enter(&gpsMutex, nullptr)) {
                app->sharedGpsData.camera_lon += delta_lon;
                app->sharedGpsData.camera_lat += delta_lat;
                mutex_exit(&gpsMutex);
            }

            if (millis() - app->last_drag_draw > 50) {
                app->last_drag_draw = millis();
                lv_obj_invalidate(app->map_canvas);
            }
        }
    }

    static void zoom_in_cb(lv_event_t* e) {
        VectorMapApp* app = (VectorMapApp*)lv_event_get_user_data(e);
        app->current_zoom *= 1.5f; 
        lv_obj_invalidate(app->map_canvas);
    }

    static void zoom_out_cb(lv_event_t* e) {
        VectorMapApp* app = (VectorMapApp*)lv_event_get_user_data(e);
        app->current_zoom /= 1.5f; 
        lv_obj_invalidate(app->map_canvas);
    }

    static void recenter_cb(lv_event_t* e) {
        VectorMapApp* app = (VectorMapApp*)lv_event_get_user_data(e);
        
        if (mutex_try_enter(&gpsMutex, nullptr)) {
            app->sharedGpsData.camera_lat = app->sharedGpsData.player_lat;
            app->sharedGpsData.camera_lon = app->sharedGpsData.player_lon;
            mutex_exit(&gpsMutex);
        }
        
        app->is_following_player = true;
        lv_obj_add_flag(app->btn_recenter, LV_OBJ_FLAG_HIDDEN); 
        lv_obj_invalidate(app->map_canvas);
    }

public:
    void start(lv_obj_t* parent) override {
        LTE::enableGPS();

        main_bg = parent;
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0xECE8D9), 0); 

        // --- HEADER ---
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1C1C1E), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_home, LV_EVENT_CLICKED, NULL);
        lv_obj_t* l_back = lv_label_create(btn_back);
        lv_label_set_text(l_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(l_back, lv_color_white(), 0);
        lv_obj_center(l_back);

        lbl_info = lv_label_create(header);
        lv_label_set_text(lbl_info, "Recherche Satellites...");
        lv_obj_set_style_text_color(lbl_info, lv_color_white(), 0);
        lv_obj_center(lbl_info);

        // --- LE CONTENEUR VECTORIEL ---
        map_canvas = lv_obj_create(main_bg);
        lv_obj_set_size(map_canvas, 320, 430); 
        lv_obj_align(map_canvas, LV_ALIGN_TOP_MID, 0, 50);
        lv_obj_set_style_bg_opa(map_canvas, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(map_canvas, 0, 0);
        
        lv_obj_add_flag(map_canvas, LV_OBJ_FLAG_CLICKABLE); 
        lv_obj_add_event_cb(map_canvas, map_draw_event_cb, LV_EVENT_DRAW_MAIN, this);
        lv_obj_add_event_cb(map_canvas, map_drag_event_cb, LV_EVENT_PRESSING, this);

        // --- BOUTONS ---
        btn_zoom_in = lv_btn_create(main_bg);
        lv_obj_set_size(btn_zoom_in, 40, 40);
        lv_obj_align(btn_zoom_in, LV_ALIGN_BOTTOM_RIGHT, -10, -60);
        lv_obj_set_style_radius(btn_zoom_in, 20, 0);
        lv_obj_set_style_bg_color(btn_zoom_in, lv_color_white(), 0);
        lv_obj_set_style_text_color(btn_zoom_in, lv_color_black(), 0);
        lv_obj_add_event_cb(btn_zoom_in, zoom_in_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* l_zi = lv_label_create(btn_zoom_in);
        lv_label_set_text(l_zi, LV_SYMBOL_PLUS);
        lv_obj_center(l_zi);

        btn_zoom_out = lv_btn_create(main_bg);
        lv_obj_set_size(btn_zoom_out, 40, 40);
        lv_obj_align(btn_zoom_out, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
        lv_obj_set_style_radius(btn_zoom_out, 20, 0);
        lv_obj_set_style_bg_color(btn_zoom_out, lv_color_white(), 0);
        lv_obj_set_style_text_color(btn_zoom_out, lv_color_black(), 0);
        lv_obj_add_event_cb(btn_zoom_out, zoom_out_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* l_zo = lv_label_create(btn_zoom_out);
        lv_label_set_text(l_zo, LV_SYMBOL_MINUS);
        lv_obj_center(l_zo);

        btn_recenter = lv_btn_create(main_bg);
        lv_obj_set_size(btn_recenter, 40, 40);
        lv_obj_align(btn_recenter, LV_ALIGN_BOTTOM_LEFT, 10, -10);
        lv_obj_set_style_radius(btn_recenter, 20, 0);
        lv_obj_set_style_bg_color(btn_recenter, lv_color_white(), 0);
        lv_obj_set_style_text_color(btn_recenter, lv_color_hex(0x007AFF), 0);
        lv_obj_add_event_cb(btn_recenter, recenter_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_flag(btn_recenter, LV_OBJ_FLAG_HIDDEN); 
        lv_obj_t* l_rec = lv_label_create(btn_recenter);
        lv_label_set_text(l_rec, LV_SYMBOL_GPS);
        lv_obj_center(l_rec);
    }

    void update() override { }

    void update1() override {
        watchdog_update();

        if (millis() - last_gps_poll > 1000) {
            last_gps_poll = millis();

            float lat = 0.0f, lon = 0.0f;
            bool got_fix = LTE::getGPSLocation(lat, lon); 

            if (mutex_try_enter(&gpsMutex, nullptr)) {
                sharedGpsData.is_fixed = got_fix;
                if (got_fix) {
                    sharedGpsData.player_lat = lat;
                    sharedGpsData.player_lon = lon;

                    if (is_following_player) {
                        sharedGpsData.camera_lat = lat;
                        sharedGpsData.camera_lon = lon;
                        lv_obj_invalidate(map_canvas); 
                    }
                }
                mutex_exit(&gpsMutex);
            }

            if (lbl_info) {
                if (got_fix) {
                    lv_label_set_text_fmt(lbl_info, "Fix %.5f, %.5f", lat, lon);
                } else {
                    lv_label_set_text(lbl_info, "Recherche Satellites...");
                }
            }
        }
    }

    void stop() override {
        LTE::disableGPS(); 
    }
};

#endif