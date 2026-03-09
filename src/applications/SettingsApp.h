#ifndef SETTINGS_APP_H
#define SETTINGS_APP_H

#include "App.h"
#include "AppManager.h"
#include "../system/Settings.h"
#include "../system/LTE.h"
#include "../system/HomeConfig.h"
#include "../plugins/lv_t9_keyboard.h"
#include <WiFi.h>
#include <RP2040Support.h>
#include <time.h>
#include <vector>

class SettingsApp : public App {
private:
    lv_obj_t* main_bg;
    lv_obj_t* list_cont;
    
    // PIN Panel
    lv_obj_t* pin_panel;
    lv_obj_t* ta_pin;
    lv_obj_t* kb_pin;
    lv_obj_t* lbl_pin_status;
    lv_obj_t* pin_panel_title;
    bool setting_new_pin = false;
    
    // Time Panel
    lv_obj_t* time_panel;
    lv_obj_t* time_panel_title;
    lv_obj_t* roller_hour;
    lv_obj_t* roller_minute;
    lv_obj_t* roller_day;
    lv_obj_t* roller_month;
    lv_obj_t* roller_year;
    lv_obj_t* lbl_time_status;

    // Home customization panels
    lv_obj_t* home_reorder_panel = nullptr;
    lv_obj_t* home_reorder_list = nullptr;
    lv_obj_t* home_reorder_hint = nullptr;
    lv_obj_t* home_python_panel = nullptr;
    lv_obj_t* home_python_list = nullptr;
    // T9 folder naming panel
    lv_obj_t* home_t9_panel = nullptr;
    lv_obj_t* home_t9_ta = nullptr;
    lv_obj_t* home_t9_kb = nullptr;
    std::vector<HomeAppEntry> home_apps_cfg;    // config actuelle (apps affichees a l'accueil)
    std::vector<HomeAppEntry> python_apps_found; // apps Python trouvees sur le FS
    int home_selected_index = -1;
    int editing_folder_index = -1; // -1 = vue principale, >=0 = edition contenu dossier
    bool folder_move_mode = false; // mode "choisir un dossier destination"
    int app_to_move_index = -1;    // index de l'app a deplacer dans un dossier
    
    // --- EVENTS ---
    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }
    static void open_wifi_event(lv_event_t* e) { AppManager::switchTo(APP_WIFI); }

    // === REORDER PANEL ===
    // Selectionner une app dans la liste de reordonnancement
    static void home_reorder_select_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));

        // Mode "deplacer dans un dossier" : clic sur un dossier = destination
        if (app->folder_move_mode) {
            auto& list = (app->editing_folder_index >= 0)
                ? homeConfig::getFolderChildren(app->home_apps_cfg[app->editing_folder_index].id)
                : app->home_apps_cfg;
            if (idx >= 0 && idx < (int)list.size() && list[idx].isFolder() && app->app_to_move_index >= 0) {
                HomeAppEntry moved = list[app->app_to_move_index];
                homeConfig::getFolderChildren(list[idx].id).push_back(moved);
                list.erase(list.begin() + app->app_to_move_index);
            }
            app->folder_move_mode = false;
            app->app_to_move_index = -1;
            app->home_selected_index = -1;
            app->refreshHomeReorderList();
            return;
        }

        auto& list = (app->editing_folder_index >= 0)
            ? homeConfig::getFolderChildren(app->home_apps_cfg[app->editing_folder_index].id)
            : app->home_apps_cfg;
        if (idx < 0 || idx >= (int)list.size()) return;
        app->home_selected_index = idx;
        app->refreshHomeReorderList();
    }

    static void home_move_up_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        int i = app->home_selected_index;
        auto& list = (app->editing_folder_index >= 0)
            ? homeConfig::getFolderChildren(app->home_apps_cfg[app->editing_folder_index].id)
            : app->home_apps_cfg;
        if (i > 0 && i < (int)list.size()) {
            std::swap(list[i - 1], list[i]);
            app->home_selected_index = i - 1;
            app->refreshHomeReorderList();
        }
    }

    static void home_move_down_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        int i = app->home_selected_index;
        auto& list = (app->editing_folder_index >= 0)
            ? homeConfig::getFolderChildren(app->home_apps_cfg[app->editing_folder_index].id)
            : app->home_apps_cfg;
        int n = (int)list.size();
        if (i >= 0 && i < n - 1) {
            std::swap(list[i], list[i + 1]);
            app->home_selected_index = i + 1;
            app->refreshHomeReorderList();
        }
    }

    // Supprimer l'app selectionnee de la liste
    static void home_remove_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        int i = app->home_selected_index;
        auto& list = (app->editing_folder_index >= 0)
            ? homeConfig::getFolderChildren(app->home_apps_cfg[app->editing_folder_index].id)
            : app->home_apps_cfg;
        if (i >= 0 && i < (int)list.size()) {
            // Si c'est un dossier, remonter ses enfants dans la liste principale
            if (list[i].isFolder() && app->editing_folder_index < 0) {
                auto ch = homeConfig::getFolderChildren(list[i].id);
                homeConfig::getFolderChildren(list[i].id).clear();
                list.erase(list.begin() + i);
                for (auto& c : ch) {
                    list.insert(list.begin() + i, c);
                    i++;
                }
            } else if (app->editing_folder_index >= 0) {
                // Retirer de dossier → remettre dans la liste principale
                HomeAppEntry moved = list[i];
                list.erase(list.begin() + i);
                app->home_apps_cfg.insert(app->home_apps_cfg.begin() + app->editing_folder_index + 1, moved);
            } else {
                list.erase(list.begin() + i);
            }
            app->home_selected_index = -1;
            app->refreshHomeReorderList();
        }
    }

    static void home_reorder_save_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->editing_folder_index = -1;
        homeConfig::saveConfig(app->home_apps_cfg);
        app->hideHomeReorderPanel();
    }

    static void home_reorder_cancel_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->editing_folder_index = -1;
        app->folder_move_mode = false;
        app->hideHomeReorderPanel();
    }

    static void home_customize_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        homeConfig::loadConfig(app->home_apps_cfg);
        app->home_selected_index = -1;
        app->editing_folder_index = -1;
        app->folder_move_mode = false;
        app->refreshHomeReorderList();
        app->showHomeReorderPanel();
    }

    // === RESET PAR DEFAUT ===
    static void home_reset_default_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        homeConfig::resetToDefault(app->home_apps_cfg);
        app->home_selected_index = -1;
        app->editing_folder_index = -1;
        app->refreshHomeReorderList();
    }

    // === DOSSIER : CREER ===
    static void home_create_folder_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->showT9Panel();
    }

    // === DOSSIER : DEPLACER APP DANS DOSSIER ===
    static void home_move_to_folder_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        if (app->editing_folder_index >= 0) return; // pas en mode edition dossier
        int i = app->home_selected_index;
        if (i < 0 || i >= (int)app->home_apps_cfg.size()) return;
        if (app->home_apps_cfg[i].isFolder()) return; // on ne met pas un dossier dans un dossier
        // Verifier qu'il y a au moins un dossier
        bool has_folder = false;
        for (const auto& a : app->home_apps_cfg) { if (a.isFolder()) { has_folder = true; break; } }
        if (!has_folder) return;
        app->folder_move_mode = true;
        app->app_to_move_index = i;
        app->refreshHomeReorderList();
    }

    // === DOSSIER : OUVRIR/FERMER ===
    static void home_open_folder_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        if (app->editing_folder_index >= 0) {
            // Fermer le dossier
            app->editing_folder_index = -1;
            app->home_selected_index = -1;
            app->refreshHomeReorderList();
            return;
        }
        int i = app->home_selected_index;
        if (i < 0 || i >= (int)app->home_apps_cfg.size()) return;
        if (!app->home_apps_cfg[i].isFolder()) return;
        app->editing_folder_index = i;
        app->home_selected_index = -1;
        app->refreshHomeReorderList();
    }

    // === T9 KEYBOARD ===
    static void t9_confirm_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        const char* text = lv_textarea_get_text(app->home_t9_ta);
        String name(text);
        name.trim();
        if (name.length() > 0) {
            HomeAppEntry folder;
            folder.id = "folder:" + name;
            folder.name = name;
            folder.symbol = LV_SYMBOL_DIRECTORY;
            folder.color = 0x8E8E93;
            folder.appId = -2;
            app->home_apps_cfg.push_back(folder);
            app->refreshHomeReorderList();
        }
        lv_textarea_set_text(app->home_t9_ta, "");
        app->hideT9Panel();
    }

    static void t9_cancel_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        lv_textarea_set_text(app->home_t9_ta, "");
        app->hideT9Panel();
    }

    // === PYTHON PANEL ===
    static void home_python_toggle_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
        if (idx < 0 || idx >= (int)app->python_apps_found.size()) return;

        const HomeAppEntry& py = app->python_apps_found[idx];
        if (homeConfig::hasAppId(app->home_apps_cfg, py.id)) {
            homeConfig::removeAppById(app->home_apps_cfg, py.id);
        } else {
            app->home_apps_cfg.push_back(py);
        }
        // Sauver immediatement pour que ca soit persistant
        homeConfig::saveConfig(app->home_apps_cfg);
        app->refreshHomePythonList();
    }

    static void home_python_close_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->hideHomePythonPanel();
    }

    static void home_add_python_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        // Charger la config actuelle
        homeConfig::loadConfig(app->home_apps_cfg);
        // Scanner les apps Python sur le FS
        homeConfig::listPythonApps(app->python_apps_found);
        app->refreshHomePythonList();
        app->showHomePythonPanel();
    }
    
    // --- PIN Toggle ---
    static void pin_toggle_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        lv_obj_t* sw = lv_event_get_target(e);
        bool enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
        
        if (enabled) {
            // Demander un nouveau PIN
            app->setting_new_pin = true;
            app->showPinPanel("Definir un code PIN:");
        } else {
            settings::setPinEnabled(false);
        }
    }
    
    // --- PIN Entry ---
    static void pin_kb_ready(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        const char* pin = lv_textarea_get_text(app->ta_pin);
        
        if (strlen(pin) >= 4 && strlen(pin) <= 6) {
            settings::setPinCode(pin);
            settings::setPinEnabled(true);
            app->hidePinPanel();
            lv_label_set_text(app->lbl_pin_status, "Code active");
            lv_obj_set_style_text_color(app->lbl_pin_status, lv_color_hex(0x4CD964), 0);
        } else {
            lv_label_set_text(app->lbl_pin_status, "4 a 6 chiffres requis");
            lv_obj_set_style_text_color(app->lbl_pin_status, lv_color_hex(0xFF3B30), 0);
        }
    }
    
    static void pin_kb_cancel(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->hidePinPanel();
        // Revert switch si on annule pendant la création
        if (app->setting_new_pin && !settings::isPinEnabled()) {
            // Le switch doit revenir à off
            app->refreshList();
        }
    }
    
    // --- Lock Timeout ---
    static void timeout_event(lv_event_t* e) {
        lv_obj_t* dd = lv_event_get_target(e);
        uint16_t sel = lv_dropdown_get_selected(dd);
        
        uint32_t timeouts[] = {0, 15000, 30000, 60000, 120000, 300000};
        if (sel < 6) {
            settings::setLockTimeout(timeouts[sel]);
        }
    }
    
    // --- Brightness ---
    static void brightness_event(lv_event_t* e) {
        lv_obj_t* slider = lv_event_get_target(e);
        int val = lv_slider_get_value(slider);
        uint8_t hw_val = (uint8_t)(5 + (val * 250 / 100));
        settings::setBrightness(hw_val);
    }
    
    // --- Change PIN ---
    static void change_pin_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->setting_new_pin = true;
        app->showPinPanel("Nouveau code PIN:");
    }

    static void bootloader_event(lv_event_t* e) {
        // Redémarrage en mode bootloader
        delay(100); // Délai pour éviter les rebonds
        rp2040.rebootToBootloader();
    }
    
    // --- Reboot System ---
    static void reboot_event(lv_event_t* e) {
        delay(100);
        rp2040.reboot();
    }
    
    // --- Sync Time Auto ---
    static void sync_time_auto_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        
        if (!LTE::isEnabled() || LTE::isAirplaneMode()) {
            lv_label_set_text(app->lbl_time_status, "Reseau 4G desactive");
            lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0xFF3B30), 0);
            return;
        }
        
        if (!LTE::isReadyForData()) {
            lv_label_set_text(app->lbl_time_status, "Pas de signal 4G");
            lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0xFF9500), 0);
            return;
        }
        
        // Synchronisation en cours
        lv_label_set_text(app->lbl_time_status, "Synchronisation...");
        lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0x007AFF), 0);
        
        // La synchronisation se fera automatiquement via LTE::update() dans le core1
        // On affiche juste un message de succès
        lv_label_set_text(app->lbl_time_status, "Sync demandee");
        lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0x4CD964), 0);
    }
    
    // --- Manual Time Setting ---
    static void show_time_panel_event(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->showTimePanel();
    }
    
    static void time_panel_save(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        
        uint16_t hour = lv_roller_get_selected(app->roller_hour);
        uint16_t minute = lv_roller_get_selected(app->roller_minute);
        uint16_t day = lv_roller_get_selected(app->roller_day) + 1;
        uint16_t month = lv_roller_get_selected(app->roller_month) + 1;
        uint16_t year = lv_roller_get_selected(app->roller_year) + 2024;
        
        struct tm t = {};
        t.tm_year = year - 1900;
        t.tm_mon = month - 1;
        t.tm_mday = day;
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        
        time_t epoch = mktime(&t);
        if (epoch != (time_t)-1) {
            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            
            lv_label_set_text(app->lbl_time_status, "Heure mise a jour");
            lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0x4CD964), 0);
        } else {
            lv_label_set_text(app->lbl_time_status, "Erreur: date invalide");
            lv_obj_set_style_text_color(app->lbl_time_status, lv_color_hex(0xFF3B30), 0);
        }
        
        app->hideTimePanel();
    }
    
    static void time_panel_cancel(lv_event_t* e) {
        SettingsApp* app = (SettingsApp*)lv_event_get_user_data(e);
        app->hideTimePanel();
    }
    
    // --- UI Helpers ---
    
    lv_obj_t* createSection(lv_obj_t* parent, const char* title) {
        lv_obj_t* lbl = lv_label_create(parent);
        lv_label_set_text(lbl, title);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_pad_left(lbl, 15, 0);
        return lbl;
    }
    
    lv_obj_t* createSettingRow(lv_obj_t* parent, const char* label_text) {
        lv_obj_t* row = lv_obj_create(parent);
        lv_obj_set_size(row, lv_pct(100), 50);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_radius(row, 12, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_left(row, 15, 0);
        lv_obj_set_style_pad_right(row, 15, 0);
        
        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, label_text);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);
        
        return row;
    }
    
    void showPinPanel(const char* title) {
        lv_label_set_text(pin_panel_title, title);
        lv_obj_set_style_text_color(pin_panel_title, lv_color_white(), 0);
        lv_textarea_set_text(ta_pin, "");
        lv_obj_clear_flag(pin_panel, LV_OBJ_FLAG_HIDDEN);
    }
    
    void hidePinPanel() {
        lv_obj_add_flag(pin_panel, LV_OBJ_FLAG_HIDDEN);
        setting_new_pin = false;
    }
    
    void showTimePanel() {
        // Get current time
        time_t now;
        time(&now);
        struct tm* t = localtime(&now);
        
        // Set rollers to current time
        lv_roller_set_selected(roller_hour, t->tm_hour, LV_ANIM_OFF);
        lv_roller_set_selected(roller_minute, t->tm_min, LV_ANIM_OFF);
        lv_roller_set_selected(roller_day, t->tm_mday - 1, LV_ANIM_OFF);
        lv_roller_set_selected(roller_month, t->tm_mon, LV_ANIM_OFF);
        lv_roller_set_selected(roller_year, (t->tm_year + 1900) - 2024, LV_ANIM_OFF);
        
        lv_obj_clear_flag(time_panel, LV_OBJ_FLAG_HIDDEN);
    }
    
    void hideTimePanel() {
        lv_obj_add_flag(time_panel, LV_OBJ_FLAG_HIDDEN);
    }

    void showHomeReorderPanel() {
        if (!home_reorder_panel) return;
        lv_obj_clear_flag(home_reorder_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(home_reorder_panel);
    }

    void hideHomeReorderPanel() {
        if (!home_reorder_panel) return;
        lv_obj_add_flag(home_reorder_panel, LV_OBJ_FLAG_HIDDEN);
    }

    void showHomePythonPanel() {
        if (!home_python_panel) return;
        lv_obj_clear_flag(home_python_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(home_python_panel);
    }

    void hideHomePythonPanel() {
        if (!home_python_panel) return;
        lv_obj_add_flag(home_python_panel, LV_OBJ_FLAG_HIDDEN);
    }

    void showT9Panel() {
        if (!home_t9_panel) return;
        lv_textarea_set_text(home_t9_ta, "");
        lv_obj_clear_flag(home_t9_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(home_t9_panel);
    }

    void hideT9Panel() {
        if (!home_t9_panel) return;
        lv_obj_add_flag(home_t9_panel, LV_OBJ_FLAG_HIDDEN);
    }

    void refreshHomeReorderList() {
        if (!home_reorder_list) return;
        lv_obj_clean(home_reorder_list);

        // Determiner la liste a afficher
        auto& list = (editing_folder_index >= 0)
            ? homeConfig::getFolderChildren(home_apps_cfg[editing_folder_index].id)
            : home_apps_cfg;

        if (list.empty()) {
            lv_obj_t* lbl = lv_label_create(home_reorder_list);
            lv_label_set_text(lbl, editing_folder_index >= 0 ? "Dossier vide" : "Aucune app");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x8E8E93), 0);
            if (home_reorder_hint) {
                if (editing_folder_index >= 0) {
                    char hint[96];
                    snprintf(hint, sizeof(hint), LV_SYMBOL_DIRECTORY " %s (vide)",
                        home_apps_cfg[editing_folder_index].name.c_str());
                    lv_label_set_text(home_reorder_hint, hint);
                } else {
                    lv_label_set_text(home_reorder_hint, "Aucune app configuree");
                }
            }
            return;
        }

        for (int i = 0; i < (int)list.size(); ++i) {
            lv_obj_t* row = lv_btn_create(home_reorder_list);
            lv_obj_set_size(row, lv_pct(100), 46);

            uint32_t bg = 0x2c2c2e;
            if (folder_move_mode && list[i].isFolder()) bg = 0x5856D6; // violet pour dossiers cibles
            else if (i == home_selected_index) bg = 0x2f6ff0;
            lv_obj_set_style_bg_color(row, lv_color_hex(bg), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 10, 0);
            lv_obj_set_user_data(row, (void*)(intptr_t)i);
            lv_obj_add_event_cb(row, home_reorder_select_event, LV_EVENT_CLICKED, this);

            lv_obj_t* lbl = lv_label_create(row);
            char line[96];
            if (list[i].isFolder()) {
                snprintf(line, sizeof(line), "%02d " LV_SYMBOL_DIRECTORY " %s (%d)",
                    i + 1, list[i].name.c_str(), homeConfig::getFolderChildCount(list[i].id));
            } else {
                snprintf(line, sizeof(line), "%02d  %s", i + 1, list[i].name.c_str());
            }
            lv_label_set_text(lbl, line);
            lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);
        }

        if (home_reorder_hint) {
            if (folder_move_mode) {
                lv_label_set_text(home_reorder_hint, "Choisissez un dossier destination");
            } else if (editing_folder_index >= 0) {
                char hint[96];
                snprintf(hint, sizeof(hint), LV_SYMBOL_DIRECTORY " %s - Suppr = retirer du dossier",
                    home_apps_cfg[editing_folder_index].name.c_str());
                lv_label_set_text(home_reorder_hint, hint);
            } else if (home_selected_index >= 0 && home_selected_index < (int)list.size()) {
                char hint[96];
                snprintf(hint, sizeof(hint), "Selection: %s", list[home_selected_index].name.c_str());
                lv_label_set_text(home_reorder_hint, hint);
            } else {
                lv_label_set_text(home_reorder_hint, "Selectionnez puis Monter/Descendre/Supprimer");
            }
        }
    }

    void refreshHomePythonList() {
        if (!home_python_list) return;
        lv_obj_clean(home_python_list);

        if (python_apps_found.empty()) {
            lv_obj_t* lbl = lv_label_create(home_python_list);
            lv_label_set_text(lbl, "Aucune app Python detectee dans /apps");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x8E8E93), 0);
            return;
        }

        for (int i = 0; i < (int)python_apps_found.size(); ++i) {
            const HomeAppEntry& py = python_apps_found[i];
            bool enabled = homeConfig::hasAppId(home_apps_cfg, py.id);

            lv_obj_t* row = lv_btn_create(home_python_list);
            lv_obj_set_size(row, lv_pct(100), 50);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2c2c2e), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_radius(row, 10, 0);
            lv_obj_set_user_data(row, (void*)(intptr_t)i);
            lv_obj_add_event_cb(row, home_python_toggle_event, LV_EVENT_CLICKED, this);

            lv_obj_t* lbl = lv_label_create(row);
            lv_label_set_text(lbl, py.name.c_str());
            lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
            lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 8, 0);

            lv_obj_t* state = lv_label_create(row);
            lv_label_set_text(state, enabled ? "Retirer" : "Ajouter");
            lv_obj_set_style_text_color(state, enabled ? lv_color_hex(0xFF9F0A) : lv_color_hex(0x4CD964), 0);
            lv_obj_align(state, LV_ALIGN_RIGHT_MID, -6, 0);
        }
    }
    
    void refreshList() {
        lv_obj_clean(list_cont);
        buildSettingsList();
    }
    
    void buildSettingsList() {
        // ===== SECTION: VERROUILLAGE =====
        createSection(list_cont, "VERROUILLAGE");
        
        // Code PIN on/off
        lv_obj_t* row_pin = createSettingRow(list_cont, "Code PIN");
        lv_obj_t* sw_pin = lv_switch_create(row_pin);
        lv_obj_align(sw_pin, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(sw_pin, lv_color_hex(0x4CD964), LV_PART_INDICATOR | LV_STATE_CHECKED);
        if (settings::isPinEnabled()) {
            lv_obj_add_state(sw_pin, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(sw_pin, pin_toggle_event, LV_EVENT_VALUE_CHANGED, this);
        
        // Modifier PIN (visible seulement si PIN actif)
        if (settings::isPinEnabled()) {
            lv_obj_t* row_change = createSettingRow(list_cont, "Modifier le code");
            lv_obj_t* chevron = lv_label_create(row_change);
            lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
            lv_obj_set_style_text_color(chevron, lv_color_hex(0x8E8E93), 0);
            lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_add_flag(row_change, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(row_change, change_pin_event, LV_EVENT_CLICKED, this);
        }
        
        // PIN status label
        lbl_pin_status = lv_label_create(list_cont);
        if (settings::isPinEnabled()) {
            lv_label_set_text(lbl_pin_status, "Code active");
            lv_obj_set_style_text_color(lbl_pin_status, lv_color_hex(0x4CD964), 0);
        } else {
            lv_label_set_text(lbl_pin_status, "Aucun code defini");
            lv_obj_set_style_text_color(lbl_pin_status, lv_color_hex(0x8E8E93), 0);
        }
        lv_obj_set_style_text_font(lbl_pin_status, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_left(lbl_pin_status, 15, 0);
        
        // Délai verrouillage
        lv_obj_t* row_timeout = createSettingRow(list_cont, "Verrouillage auto");
        lv_obj_t* dd = lv_dropdown_create(row_timeout);
        lv_dropdown_set_options(dd, "Jamais\n15 sec\n30 sec\n1 min\n2 min\n5 min");
        lv_obj_set_size(dd, 100, 35);
        lv_obj_align(dd, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_text_font(dd, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(dd, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(dd, lv_color_white(), 0);
        lv_obj_set_style_border_width(dd, 0, 0);
        
        // Set current selection
        uint32_t timeout = settings::getLockTimeout();
        uint16_t sel = 2; // 30s par défaut
        if (timeout == 0) sel = 0;
        else if (timeout <= 15000) sel = 1;
        else if (timeout <= 30000) sel = 2;
        else if (timeout <= 60000) sel = 3;
        else if (timeout <= 120000) sel = 4;
        else sel = 5;
        lv_dropdown_set_selected(dd, sel);
        lv_obj_add_event_cb(dd, timeout_event, LV_EVENT_VALUE_CHANGED, NULL);
        
        // ===== SECTION: ECRAN =====
        createSection(list_cont, "ECRAN");
        
        lv_obj_t* row_br = createSettingRow(list_cont, "Luminosite");
        lv_obj_t* slider = lv_slider_create(row_br);
        lv_obj_set_size(slider, 120, 14);
        lv_obj_align(slider, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_slider_set_range(slider, 0, 100);
        
        uint8_t br = settings::getBrightness();
        int br_pct = ((int)br - 5) * 100 / 250;
        if (br_pct < 0) br_pct = 0;
        if (br_pct > 100) br_pct = 100;
        lv_slider_set_value(slider, br_pct, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x3a3a3c), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x007AFF), LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_border_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_outline_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
        lv_obj_add_event_cb(slider, brightness_event, LV_EVENT_VALUE_CHANGED, NULL);
        
        // ===== SECTION: SYSTEME =====
        createSection(list_cont, "SYSTEME");

        // WiFi (deplace depuis l'ecran d'accueil)
        lv_obj_t* row_wifi = createSettingRow(list_cont, "WiFi");
        lv_obj_t* chevron_wifi = lv_label_create(row_wifi);
        lv_label_set_text(chevron_wifi, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron_wifi, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(chevron_wifi, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_wifi, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_wifi, open_wifi_event, LV_EVENT_CLICKED, this);
        
        // Sync time auto
        lv_obj_t* row_sync_time = createSettingRow(list_cont, "Synchro heure auto");
        lv_obj_t* chevron_sync = lv_label_create(row_sync_time);
        lv_label_set_text(chevron_sync, LV_SYMBOL_REFRESH);
        lv_obj_set_style_text_color(chevron_sync, lv_color_hex(0x007AFF), 0);
        lv_obj_align(chevron_sync, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_sync_time, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_sync_time, sync_time_auto_event, LV_EVENT_CLICKED, this);

        // Manual time setting (remonte plus haut dans la liste)
        lv_obj_t* row_manual_time = createSettingRow(list_cont, "Regler l'heure");
        lv_obj_t* chevron_manual = lv_label_create(row_manual_time);
        lv_label_set_text(chevron_manual, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron_manual, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(chevron_manual, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_manual_time, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_manual_time, show_time_panel_event, LV_EVENT_CLICKED, this);

        // Time status label (remonte avec Regler l'heure)
        lbl_time_status = lv_label_create(list_cont);
        time_t now;
        time(&now);
        struct tm* t = localtime(&now);
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d - %02d/%02d/%04d",
                 t->tm_hour, t->tm_min, t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
        lv_label_set_text(lbl_time_status, timeBuf);
        lv_obj_set_style_text_color(lbl_time_status, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(lbl_time_status, &lv_font_montserrat_12, 0);
        lv_obj_set_style_pad_left(lbl_time_status, 15, 0);

        // ===== SECTION: ECRAN D'ACCUEIL =====
        createSection(list_cont, "ECRAN D'ACCUEIL");

        // Reorganiser apps
        lv_obj_t* row_reorder = createSettingRow(list_cont, "Reorganiser les applis");
        lv_obj_t* chevron_reorder = lv_label_create(row_reorder);
        lv_label_set_text(chevron_reorder, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron_reorder, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(chevron_reorder, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_reorder, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_reorder, home_customize_event, LV_EVENT_CLICKED, this);

        // Ajouter apps Python
        lv_obj_t* row_add_python = createSettingRow(list_cont, "Ajouter applis Python");
        lv_obj_t* chevron_python = lv_label_create(row_add_python);
        lv_label_set_text(chevron_python, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron_python, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(chevron_python, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_add_python, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_add_python, home_add_python_event, LV_EVENT_CLICKED, this);
        
        
        
        // ===== SECTION: INFO =====
        createSection(list_cont, "INFORMATIONS");


        lv_obj_t* row_bl = createSettingRow(list_cont, "Bootloader");

        
        lv_obj_t* chevron_bl = lv_label_create(row_bl);
        lv_label_set_text(chevron_bl, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chevron_bl, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(chevron_bl, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_bl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_bl, bootloader_event, LV_EVENT_CLICKED, this);

        // Reboot button
        lv_obj_t* row_reboot = createSettingRow(list_cont, "Redemarrer");
        lv_obj_t* chevron_reboot = lv_label_create(row_reboot);
        lv_label_set_text(chevron_reboot, LV_SYMBOL_POWER);
        lv_obj_set_style_text_color(chevron_reboot, lv_color_hex(0xFF3B30), 0);
        lv_obj_align(chevron_reboot, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(row_reboot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row_reboot, reboot_event, LV_EVENT_CLICKED, this);
        
        lv_obj_t* row_ver = createSettingRow(list_cont, "Version");
        lv_obj_t* lbl_ver = lv_label_create(row_ver);
        lv_label_set_text(lbl_ver, COMMIT_HASH);
        lv_obj_set_style_text_color(lbl_ver, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(lbl_ver, LV_ALIGN_RIGHT_MID, 0, 0);
        
        lv_obj_t* row_mem = createSettingRow(list_cont, "RAM libre");
        lv_obj_t* lbl_mem = lv_label_create(row_mem);
        char memBuf[16];
        snprintf(memBuf, sizeof(memBuf), "%u Ko", (unsigned)(rp2040.getFreeHeap() / 1024));
        lv_label_set_text(lbl_mem, memBuf);
        lv_obj_set_style_text_color(lbl_mem, lv_color_hex(0x8E8E93), 0);
        lv_obj_align(lbl_mem, LV_ALIGN_RIGHT_MID, 0, 0);
    }

public:
    void start(lv_obj_t* parent) override {
        main_bg = parent;
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x000000), 0);
        lv_obj_clear_flag(main_bg, LV_OBJ_FLAG_SCROLLABLE);
        
        // --- HEADER ---
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1c1c1e), 0);
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
        lv_obj_set_style_text_color(l_back, lv_color_hex(0x007AFF), 0);
        lv_obj_center(l_back);
        
        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Parametres");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_center(title);
        
        // --- LISTE SCROLLABLE ---
        list_cont = lv_obj_create(main_bg);
        lv_obj_set_size(list_cont, 320, 430);
        lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_gap(list_cont, 8, 0);
        lv_obj_set_style_pad_all(list_cont, 5, 0);
        
        buildSettingsList();
        
        // --- PIN PANEL (overlay plein écran) ---
        pin_panel = lv_obj_create(main_bg);
        lv_obj_set_size(pin_panel, 320, 480);
        lv_obj_center(pin_panel);
        lv_obj_set_style_bg_color(pin_panel, lv_color_hex(0x1c1c1e), 0);
        lv_obj_add_flag(pin_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pin_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        pin_panel_title = lv_label_create(pin_panel);
        lv_label_set_text(pin_panel_title, "Definir le code PIN");
        lv_obj_set_style_text_color(pin_panel_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(pin_panel_title, &lv_font_montserrat_14, 0);
        lv_obj_align(pin_panel_title, LV_ALIGN_TOP_MID, 0, 10);
        
        // Reuse lbl_pin_status if needed (already created in list but may be hidden)
        // Create a new one for the pin panel
        lv_obj_t* pin_hint = lv_label_create(pin_panel);
        lv_label_set_text(pin_hint, "4 a 6 chiffres");
        lv_obj_set_style_text_color(pin_hint, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(pin_hint, &lv_font_montserrat_12, 0);
        lv_obj_align(pin_hint, LV_ALIGN_TOP_MID, 0, 35);
        
        ta_pin = lv_textarea_create(pin_panel);
        lv_obj_set_size(ta_pin, 180, 45);
        lv_obj_align(ta_pin, LV_ALIGN_TOP_MID, 0, 60);
        lv_textarea_set_max_length(ta_pin, 6);
        lv_textarea_set_one_line(ta_pin, true);
        lv_textarea_set_password_mode(ta_pin, true);
        lv_obj_set_style_text_font(ta_pin, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_align(ta_pin, LV_TEXT_ALIGN_CENTER, 0);
        
        kb_pin = lv_keyboard_create(pin_panel);
        lv_keyboard_set_mode(kb_pin, LV_KEYBOARD_MODE_NUMBER);
        lv_keyboard_set_textarea(kb_pin, ta_pin);
        lv_obj_align(kb_pin, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(kb_pin, pin_kb_ready, LV_EVENT_READY, this);
        lv_obj_add_event_cb(kb_pin, pin_kb_cancel, LV_EVENT_CANCEL, this);
        
        // --- TIME PANEL (overlay plein écran) ---
        time_panel = lv_obj_create(main_bg);
        lv_obj_set_size(time_panel, 320, 480);
        lv_obj_center(time_panel);
        lv_obj_set_style_bg_color(time_panel, lv_color_hex(0x1c1c1e), 0);
        lv_obj_add_flag(time_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(time_panel, LV_OBJ_FLAG_SCROLLABLE);
        
        time_panel_title = lv_label_create(time_panel);
        lv_label_set_text(time_panel_title, "Regler l'heure");
        lv_obj_set_style_text_color(time_panel_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(time_panel_title, &lv_font_montserrat_14, 0);
        lv_obj_align(time_panel_title, LV_ALIGN_TOP_MID, 0, 10);
        
        // Time section
        lv_obj_t* time_label = lv_label_create(time_panel);
        lv_label_set_text(time_label, "Heure");
        lv_obj_set_style_text_color(time_label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(time_label, &lv_font_montserrat_12, 0);
        lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, 20, 50);
        
        lv_obj_t* time_cont = lv_obj_create(time_panel);
        lv_obj_set_size(time_cont, 280, 80);
        lv_obj_align(time_cont, LV_ALIGN_TOP_MID, 0, 70);
        lv_obj_set_style_bg_color(time_cont, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_border_width(time_cont, 0, 0);
        lv_obj_set_flex_flow(time_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(time_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(time_cont, LV_OBJ_FLAG_SCROLLABLE);
        
        // Hour roller
        roller_hour = lv_roller_create(time_cont);
        lv_roller_set_options(roller_hour, 
            "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n"
            "12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
            LV_ROLLER_MODE_INFINITE);
        lv_obj_set_size(roller_hour, 60, 70);
        lv_obj_set_style_text_font(roller_hour, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(roller_hour, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_hour, lv_color_white(), LV_PART_SELECTED);
        
        lv_obj_t* colon = lv_label_create(time_cont);
        lv_label_set_text(colon, ":");
        lv_obj_set_style_text_color(colon, lv_color_white(), 0);
        lv_obj_set_style_text_font(colon, &lv_font_montserrat_28, 0);
        
        // Minute roller
        roller_minute = lv_roller_create(time_cont);
        char minute_opts[400];
        strcpy(minute_opts, "00");
        for (int i = 1; i < 60; i++) {
            char buf[6];
            snprintf(buf, sizeof(buf), "\n%02d", i);
            strcat(minute_opts, buf);
        }
        lv_roller_set_options(roller_minute, minute_opts, LV_ROLLER_MODE_INFINITE);
        lv_obj_set_size(roller_minute, 60, 70);
        lv_obj_set_style_text_font(roller_minute, &lv_font_montserrat_14, 0);
        lv_obj_set_style_bg_color(roller_minute, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_minute, lv_color_white(), LV_PART_SELECTED);
        
        // Date section
        lv_obj_t* date_label = lv_label_create(time_panel);
        lv_label_set_text(date_label, "Date");
        lv_obj_set_style_text_color(date_label, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(date_label, &lv_font_montserrat_12, 0);
        lv_obj_align(date_label, LV_ALIGN_TOP_LEFT, 20, 170);
        
        lv_obj_t* date_cont = lv_obj_create(time_panel);
        lv_obj_set_size(date_cont, 280, 80);
        lv_obj_align(date_cont, LV_ALIGN_TOP_MID, 0, 190);
        lv_obj_set_style_bg_color(date_cont, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_border_width(date_cont, 0, 0);
        lv_obj_set_flex_flow(date_cont, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(date_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(date_cont, LV_OBJ_FLAG_SCROLLABLE);
        
        // Day roller
        roller_day = lv_roller_create(date_cont);
        char day_opts[200];
        strcpy(day_opts, "01");
        for (int i = 2; i <= 31; i++) {
            char buf[6];
            snprintf(buf, sizeof(buf), "\n%02d", i);
            strcat(day_opts, buf);
        }
        lv_roller_set_options(roller_day, day_opts, LV_ROLLER_MODE_NORMAL);
        lv_obj_set_size(roller_day, 50, 70);
        lv_obj_set_style_text_font(roller_day, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(roller_day, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_day, lv_color_white(), LV_PART_SELECTED);
        
        lv_obj_t* slash1 = lv_label_create(date_cont);
        lv_label_set_text(slash1, "/");
        lv_obj_set_style_text_color(slash1, lv_color_white(), 0);
        
        // Month roller
        roller_month = lv_roller_create(date_cont);
        lv_roller_set_options(roller_month, 
            "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12",
            LV_ROLLER_MODE_NORMAL);
        lv_obj_set_size(roller_month, 50, 70);
        lv_obj_set_style_text_font(roller_month, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(roller_month, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_month, lv_color_white(), LV_PART_SELECTED);
        
        lv_obj_t* slash2 = lv_label_create(date_cont);
        lv_label_set_text(slash2, "/");
        lv_obj_set_style_text_color(slash2, lv_color_white(), 0);
        
        // Year roller
        roller_year = lv_roller_create(date_cont);
        lv_roller_set_options(roller_year, 
            "2024\n2025\n2026\n2027\n2028\n2029\n2030\n2031\n2032\n2033\n2034\n2035",
            LV_ROLLER_MODE_NORMAL);
        lv_obj_set_size(roller_year, 70, 70);
        lv_obj_set_style_text_font(roller_year, &lv_font_montserrat_12, 0);
        lv_obj_set_style_bg_color(roller_year, lv_color_hex(0x2c2c2e), 0);
        lv_obj_set_style_text_color(roller_year, lv_color_white(), LV_PART_SELECTED);
        
        // Buttons
        lv_obj_t* btn_save = lv_btn_create(time_panel);
        lv_obj_set_size(btn_save, 130, 45);
        lv_obj_align(btn_save, LV_ALIGN_BOTTOM_LEFT, 20, -20);
        lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x007AFF), 0);
        lv_obj_add_event_cb(btn_save, time_panel_save, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_save = lv_label_create(btn_save);
        lv_label_set_text(lbl_save, "Enregistrer");
        lv_obj_center(lbl_save);
        
        lv_obj_t* btn_cancel = lv_btn_create(time_panel);
        lv_obj_set_size(btn_cancel, 130, 45);
        lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -20, -20);
        lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x3a3a3c), 0);
        lv_obj_add_event_cb(btn_cancel, time_panel_cancel, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_cancel = lv_label_create(btn_cancel);
        lv_label_set_text(lbl_cancel, "Annuler");
        lv_obj_center(lbl_cancel);

        // --- HOME REORDER PANEL ---
        home_reorder_panel = lv_obj_create(main_bg);
        lv_obj_set_size(home_reorder_panel, 320, 480);
        lv_obj_center(home_reorder_panel);
        lv_obj_set_style_bg_color(home_reorder_panel, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_border_width(home_reorder_panel, 0, 0);
        lv_obj_clear_flag(home_reorder_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(home_reorder_panel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* hr_title = lv_label_create(home_reorder_panel);
        lv_label_set_text(hr_title, "Reorganiser les applis");
        lv_obj_set_style_text_color(hr_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(hr_title, &lv_font_montserrat_14, 0);
        lv_obj_align(hr_title, LV_ALIGN_TOP_MID, 0, 10);

        home_reorder_hint = lv_label_create(home_reorder_panel);
        lv_label_set_text(home_reorder_hint, "Selectionnez une app, puis Monter/Descendre");
        lv_obj_set_style_text_color(home_reorder_hint, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(home_reorder_hint, &lv_font_montserrat_12, 0);
        lv_obj_align(home_reorder_hint, LV_ALIGN_TOP_LEFT, 12, 36);

        home_reorder_list = lv_obj_create(home_reorder_panel);
        lv_obj_set_size(home_reorder_list, 300, 270);
        lv_obj_align(home_reorder_list, LV_ALIGN_TOP_MID, 0, 58);
        lv_obj_set_style_bg_color(home_reorder_list, lv_color_hex(0x161618), 0);
        lv_obj_set_style_border_width(home_reorder_list, 0, 0);
        lv_obj_set_style_pad_all(home_reorder_list, 6, 0);
        lv_obj_set_style_pad_gap(home_reorder_list, 6, 0);
        lv_obj_set_flex_flow(home_reorder_list, LV_FLEX_FLOW_COLUMN);

        // Row 1: UP, DOWN, DELETE, SAVE
        lv_obj_t* btn_up = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_up, 68, 38);
        lv_obj_align(btn_up, LV_ALIGN_BOTTOM_LEFT, 6, -100);
        lv_obj_set_style_bg_color(btn_up, lv_color_hex(0x2f6ff0), 0);
        lv_obj_add_event_cb(btn_up, home_move_up_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_up = lv_label_create(btn_up);
        lv_label_set_text(lbl_up, LV_SYMBOL_UP);
        lv_obj_center(lbl_up);

        lv_obj_t* btn_down = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_down, 68, 38);
        lv_obj_align(btn_down, LV_ALIGN_BOTTOM_LEFT, 80, -100);
        lv_obj_set_style_bg_color(btn_down, lv_color_hex(0x2f6ff0), 0);
        lv_obj_add_event_cb(btn_down, home_move_down_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_down = lv_label_create(btn_down);
        lv_label_set_text(lbl_down, LV_SYMBOL_DOWN);
        lv_obj_center(lbl_down);

        lv_obj_t* btn_del = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_del, 68, 38);
        lv_obj_align(btn_del, LV_ALIGN_BOTTOM_RIGHT, -86, -100);
        lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xFF3B30), 0);
        lv_obj_add_event_cb(btn_del, home_remove_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, LV_SYMBOL_TRASH);
        lv_obj_center(lbl_del);

        lv_obj_t* btn_hr_save = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_hr_save, 68, 38);
        lv_obj_align(btn_hr_save, LV_ALIGN_BOTTOM_RIGHT, -12, -100);
        lv_obj_set_style_bg_color(btn_hr_save, lv_color_hex(0x34C759), 0);
        lv_obj_add_event_cb(btn_hr_save, home_reorder_save_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_hr_save = lv_label_create(btn_hr_save);
        lv_label_set_text(lbl_hr_save, "Sauver");
        lv_obj_center(lbl_hr_save);

        // Row 2: RESET, DOSSIER+, OUVRIR/MOVE, FERMER DOSSIER
        lv_obj_t* btn_reset = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_reset, 68, 38);
        lv_obj_align(btn_reset, LV_ALIGN_BOTTOM_LEFT, 6, -56);
        lv_obj_set_style_bg_color(btn_reset, lv_color_hex(0xFF9500), 0);
        lv_obj_add_event_cb(btn_reset, home_reset_default_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_reset = lv_label_create(btn_reset);
        lv_label_set_text(lbl_reset, LV_SYMBOL_REFRESH);
        lv_obj_center(lbl_reset);

        lv_obj_t* btn_folder = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_folder, 68, 38);
        lv_obj_align(btn_folder, LV_ALIGN_BOTTOM_LEFT, 80, -56);
        lv_obj_set_style_bg_color(btn_folder, lv_color_hex(0x5856D6), 0);
        lv_obj_add_event_cb(btn_folder, home_create_folder_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_folder = lv_label_create(btn_folder);
        lv_label_set_text(lbl_folder, LV_SYMBOL_DIRECTORY "+");
        lv_obj_center(lbl_folder);

        lv_obj_t* btn_move_to = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_move_to, 68, 38);
        lv_obj_align(btn_move_to, LV_ALIGN_BOTTOM_RIGHT, -86, -56);
        lv_obj_set_style_bg_color(btn_move_to, lv_color_hex(0x5856D6), 0);
        lv_obj_add_event_cb(btn_move_to, home_move_to_folder_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_move = lv_label_create(btn_move_to);
        lv_label_set_text(lbl_move, LV_SYMBOL_RIGHT LV_SYMBOL_DIRECTORY);
        lv_obj_center(lbl_move);

        lv_obj_t* btn_open_folder = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_open_folder, 68, 38);
        lv_obj_align(btn_open_folder, LV_ALIGN_BOTTOM_RIGHT, -12, -56);
        lv_obj_set_style_bg_color(btn_open_folder, lv_color_hex(0x007AFF), 0);
        lv_obj_add_event_cb(btn_open_folder, home_open_folder_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_open = lv_label_create(btn_open_folder);
        lv_label_set_text(lbl_open, LV_SYMBOL_EYE_OPEN);
        lv_obj_center(lbl_open);

        // Row 3: CLOSE
        lv_obj_t* btn_hr_cancel = lv_btn_create(home_reorder_panel);
        lv_obj_set_size(btn_hr_cancel, 296, 38);
        lv_obj_align(btn_hr_cancel, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(btn_hr_cancel, lv_color_hex(0x3a3a3c), 0);
        lv_obj_add_event_cb(btn_hr_cancel, home_reorder_cancel_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_hr_cancel = lv_label_create(btn_hr_cancel);
        lv_label_set_text(lbl_hr_cancel, "Fermer");
        lv_obj_center(lbl_hr_cancel);

        // --- HOME PYTHON PANEL ---
        home_python_panel = lv_obj_create(main_bg);
        lv_obj_set_size(home_python_panel, 320, 480);
        lv_obj_center(home_python_panel);
        lv_obj_set_style_bg_color(home_python_panel, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_border_width(home_python_panel, 0, 0);
        lv_obj_clear_flag(home_python_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(home_python_panel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* hp_title = lv_label_create(home_python_panel);
        lv_label_set_text(hp_title, "Ajouter applis Python");
        lv_obj_set_style_text_color(hp_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(hp_title, &lv_font_montserrat_14, 0);
        lv_obj_align(hp_title, LV_ALIGN_TOP_MID, 0, 10);

        lv_obj_t* hp_hint = lv_label_create(home_python_panel);
        lv_label_set_text(hp_hint, "Cliquez pour ajouter/retirer (sauvegarde auto)");
        lv_obj_set_style_text_color(hp_hint, lv_color_hex(0x8E8E93), 0);
        lv_obj_set_style_text_font(hp_hint, &lv_font_montserrat_12, 0);
        lv_obj_align(hp_hint, LV_ALIGN_TOP_LEFT, 12, 36);

        home_python_list = lv_obj_create(home_python_panel);
        lv_obj_set_size(home_python_list, 300, 370);
        lv_obj_align(home_python_list, LV_ALIGN_TOP_MID, 0, 58);
        lv_obj_set_style_bg_color(home_python_list, lv_color_hex(0x161618), 0);
        lv_obj_set_style_border_width(home_python_list, 0, 0);
        lv_obj_set_style_pad_all(home_python_list, 6, 0);
        lv_obj_set_style_pad_gap(home_python_list, 6, 0);
        lv_obj_set_flex_flow(home_python_list, LV_FLEX_FLOW_COLUMN);

        lv_obj_t* btn_hp_close = lv_btn_create(home_python_panel);
        lv_obj_set_size(btn_hp_close, 296, 40);
        lv_obj_align(btn_hp_close, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_bg_color(btn_hp_close, lv_color_hex(0x3a3a3c), 0);
        lv_obj_add_event_cb(btn_hp_close, home_python_close_event, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_hp_close = lv_label_create(btn_hp_close);
        lv_label_set_text(lbl_hp_close, "Fermer");
        lv_obj_center(lbl_hp_close);

        // --- T9 KEYBOARD PANEL (for folder naming) ---
        home_t9_panel = lv_obj_create(main_bg);
        lv_obj_set_size(home_t9_panel, 320, 480);
        lv_obj_center(home_t9_panel);
        lv_obj_set_style_bg_color(home_t9_panel, lv_color_hex(0x1c1c1e), 0);
        lv_obj_set_style_border_width(home_t9_panel, 0, 0);
        lv_obj_clear_flag(home_t9_panel, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(home_t9_panel, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t* t9_title = lv_label_create(home_t9_panel);
        lv_label_set_text(t9_title, "Nom du dossier");
        lv_obj_set_style_text_color(t9_title, lv_color_white(), 0);
        lv_obj_set_style_text_font(t9_title, &lv_font_montserrat_14, 0);
        lv_obj_align(t9_title, LV_ALIGN_TOP_MID, 0, 10);

        home_t9_ta = lv_textarea_create(home_t9_panel);
        lv_obj_set_size(home_t9_ta, 280, 45);
        lv_obj_align(home_t9_ta, LV_ALIGN_TOP_MID, 0, 40);
        lv_textarea_set_one_line(home_t9_ta, true);
        lv_textarea_set_max_length(home_t9_ta, 20);
        lv_obj_set_style_text_font(home_t9_ta, &lv_font_montserrat_18, 0);

        home_t9_kb = lv_t9_kb_create(home_t9_panel);
        lv_obj_set_size(home_t9_kb, 320, 380);
        lv_obj_align(home_t9_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_t9_kb_set_textarea(home_t9_kb, home_t9_ta);
        lv_obj_add_event_cb(home_t9_kb, t9_confirm_event, LV_EVENT_READY, this);
        lv_obj_add_event_cb(home_t9_kb, t9_cancel_event, LV_EVENT_CANCEL, this);
    }
};

#endif
