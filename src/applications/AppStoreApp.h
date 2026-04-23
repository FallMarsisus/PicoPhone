#ifndef APP_STORE_APP_H
#define APP_STORE_APP_H

#include "../App.h"
#include "../AppManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <hardware/watchdog.h>
#include <pico/mutex.h>
#include <vector>
#include "../system/LTE.h"

// ═══════════════════════════════════════════════════════════════════════════
//  URL de base du dépôt d'applications
// ═══════════════════════════════════════════════════════════════════════════
#define APP_REPO_BASE "https://raw.githubusercontent.com/FallMarsisus/picophone-app-repo/main/"
#define APP_LIST_URL  APP_REPO_BASE "app-list.json"

struct AppInfo {
    String id;
    String name;
    String description;
    String author;
    String version;
    String local_version;
    String entry;   // e.g. "main.py"
    String category;
    bool installed = false;
    bool update_available = false;
};

struct AppStoreData {
    std::vector<AppInfo> apps;
    bool success = false;
    String error_msg;
};

auto_init_mutex(appStoreMutex);

class AppStoreApp : public App {
private:
    lv_obj_t* main_bg = nullptr;
    lv_obj_t* list_cont = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_obj_t* loader = nullptr;

    // Synchro Core 0 / Core 1
    bool refresh_requested = false;
    bool has_new_data = false;
    AppStoreData sharedData;
    
    // Installation en arrière-plan
    bool install_requested = false;
    int install_index = -1;
    bool install_done = false;
    bool install_success = false;

    // Désinstallation en arrière-plan
    bool uninstall_requested = false;
    int uninstall_index = -1;
    bool uninstall_done = false;
    bool uninstall_success = false;

    static AppStoreApp* instance;

    // ─── Utilitaires réseau (Core 1 uniquement) ──────────────

    String httpGet(const String& url) {
        if (!LTE::isReadyForData()) return "";
        return LTE::httpGetBlocking(url);
    }

    // ─── Vérifie si une app est déjà installée ──────────────

    bool isInstalled(const String& appId) {
        return LittleFS.exists("/apps/" + appId + "/manifest.json");
    }

    String readInstalledVersion(const String& appId) {
        String manifestPath = "/apps/" + appId + "/manifest.json";
        File f = LittleFS.open(manifestPath, "r");
        if (!f) {
            return "";
        }

        String json = f.readString();
        f.close();
        if (json.length() == 0) {
            return "";
        }

        JsonDocument doc;
        if (deserializeJson(doc, json)) {
            return "";
        }
        return doc["version"].as<String>();
    }

    // ─── Créer les dossiers récursivement ────────────────────

    static void mkdirs(const String& path) {
        String acc = "";
        for (int i = 1; i < (int)path.length(); i++) {
            if (path[i] == '/') {
                acc = path.substring(0, i);
                if (!LittleFS.exists(acc)) LittleFS.mkdir(acc);
            }
        }
        if (!LittleFS.exists(path)) LittleFS.mkdir(path);
    }

    bool deleteRecursiveFs(const String& path) {
        watchdog_update();
        Serial.printf("[AppStore] deleteRecursiveFs: %s\n", path.c_str());
        
        File f = LittleFS.open(path.c_str(), "r");
        if (!f) {
            Serial.printf("[AppStore] deleteRecursiveFs: failed to open %s\n", path.c_str());
            return false;
        }

        if (!f.isDirectory()) {
            f.close();
            bool ok = LittleFS.remove(path.c_str());
            Serial.printf("[AppStore] deleteRecursiveFs: remove file %s -> %d\n", path.c_str(), ok);
            return ok;
        }

        // Collecter tous les enfants d'abord, puis fermer le dir
        std::vector<String> children;
        File child = f.openNextFile();
        while (child) {
            children.push_back(String(child.name()));
            child.close();
            child = f.openNextFile();
        }
        f.close();  // Fermer le répertoire AVANT de supprimer les enfants
        
        for (const auto& childPath : children) {
            watchdog_update();
            if (!deleteRecursiveFs(childPath)) {
                Serial.printf("[AppStore] deleteRecursiveFs: child failed %s\n", childPath.c_str());
                return false;
            }
        }
        
        bool ok = LittleFS.rmdir(path.c_str());
        Serial.printf("[AppStore] deleteRecursiveFs: rmdir %s -> %d\n", path.c_str(), ok);
        return ok;
    }

    // ─── Télécharge un fichier vers LittleFS ─────────────────

    bool downloadToFs(const String& url, const String& fsPath) {
        String content = httpGet(url);
        if (content.length() == 0) return false;
        File f = LittleFS.open(fsPath, "w");
        if (!f) return false;
        f.print(content);
        f.close();
        Serial.printf("[AppStore] Saved %s (%d B)\n", fsPath.c_str(), content.length());
        return true;
    }

    // ─── Récupérer la liste depuis le repo (CORE 1) ──────────

    void fetchAppListBackground(AppStoreData& data) {
        data.success = false;
        data.apps.clear();
        
        if (!LTE::isReadyForData()) {
            data.error_msg = "LTE indisponible";
            return;
        }

        String json = httpGet(APP_LIST_URL);
        watchdog_update();
        
        if (json.length() == 0) {
            data.error_msg = "Erreur de connexion";
            return;
        }

        JsonDocument doc;
        auto err = deserializeJson(doc, json);
        if (err) {
            Serial.printf("[AppStore] JSON parse error: %s\n", err.c_str());
            data.error_msg = "Erreur JSON";
            return;
        }

        JsonArray appArr = doc["apps"].as<JsonArray>();
        for (JsonObject obj : appArr) {
            watchdog_update();
            String appId = obj["id"].as<String>();
            if (appId.length() == 0) continue;

            // Télécharger le manifest de chaque app
            String manifestUrl = String(APP_REPO_BASE) + "apps/" + appId + "/manifest.json";
            String manifestJson = httpGet(manifestUrl);
            watchdog_update();

            AppInfo info;
            info.id = appId;
            info.author = obj["author"] | "Inconnu";
            info.name = obj["name"] | appId;
            info.description = obj["description"] | "";
            info.version = obj["version"] | "?";
            info.entry = obj["entry"] | "main.py";
            info.category = obj["category"] | "";

            if (manifestJson.length() > 0) {
                JsonDocument mdoc;
                if (!deserializeJson(mdoc, manifestJson)) {
                    info.name        = mdoc["name"]        | appId;
                    info.description = mdoc["description"] | "";
                    info.version     = mdoc["version"]     | "?";
                    info.entry       = mdoc["entry"]       | "main.py";
                    info.category    = mdoc["category"]    | "";
                } else {
                    info.name = appId;
                    info.entry = "main.py";
                }
            }

            info.installed = isInstalled(appId);
            if (info.installed) {
                info.local_version = readInstalledVersion(appId);
                info.update_available =
                    (info.local_version.length() > 0) && (info.version != info.local_version);
            }
            data.apps.push_back(info);
        }

        data.success = true;
        Serial.printf("[AppStore] %d apps trouvees\n", data.apps.size());
    }

    // ─── Installe une app (CORE 1) ────────────────────────────

    bool downloadAppFiles(const String& appId, const String& entry) {
        String dir = "/apps/" + appId;
        mkdirs(dir);

        // Télécharger le manifest
        String manifestUrl = String(APP_REPO_BASE) + "apps/" + appId + "/manifest.json";
        if (!downloadToFs(manifestUrl, dir + "/manifest.json")) return false;
        watchdog_update();

        // Télécharger le fichier principal (entry)
        String entryUrl = String(APP_REPO_BASE) + "apps/" + appId + "/" + entry;
        if (!downloadToFs(entryUrl, dir + "/" + entry)) return false;
        watchdog_update();

        Serial.printf("[AppStore] Installe: %s\n", appId.c_str());
        return true;
    }

    bool uninstallAppFiles(const String& appId) {
        String dir = "/apps/" + appId;
        if (!LittleFS.exists(dir)) {
            return true;
        }
        bool ok = deleteRecursiveFs(dir);
        Serial.printf("[AppStore] Desinstalle: %s -> %d\n", appId.c_str(), ok);
        return ok;
    }

    // ─── UI : Affichage de la liste (CORE 0) ─────────────────

    void showAppList() {
        if (!list_cont) return;
        Serial.println("[AppStore] showAppList() START");
        
        // IMPORTANT: Cacher le status AVANT lv_obj_clean() qui détruit tous les enfants
        if (status_label) {
            lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
        }
        
        lv_obj_clean(list_cont);

        // Copier les données rapidement, puis relâcher le mutex
        std::vector<AppInfo> appsCopy;
        mutex_enter_blocking(&appStoreMutex);
        appsCopy = sharedData.apps;
        int totalApps = appsCopy.size();
        mutex_exit(&appStoreMutex);
        
        Serial.printf("[AppStore] showAppList() appsCopy size = %d\n", totalApps);
        
        if (appsCopy.empty()) {
            Serial.println("[AppStore] showAppList() EMPTY - showing message");
            lv_obj_t* lbl = lv_label_create(list_cont);
            lv_label_set_text(lbl, "Aucune application disponible");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAAA), 0);
            lv_obj_center(lbl);
            return;
        }

        Serial.printf("[AppStore] Creating %d app cards...\n", totalApps);
        
        for (int i = 0; i < totalApps; i++) {
            watchdog_update();
            AppInfo& app = appsCopy[i];
            
            Serial.printf("[AppStore] Card %d: %s\n", i, app.name.c_str());

            lv_obj_t* card = lv_obj_create(list_cont);
            lv_obj_set_size(card, 296, 72);
            lv_obj_set_style_bg_color(card, lv_color_hex(0x3D566E), 0);
            lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(card, 0, 0);
            lv_obj_set_style_radius(card, 10, 0);
            lv_obj_set_style_pad_all(card, 10, 0);
            lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

            // Nom
            lv_obj_t* nameLbl = lv_label_create(card);
            lv_label_set_text(nameLbl, app.name.c_str());
            lv_obj_set_style_text_color(nameLbl, lv_color_white(), 0);
            lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_14, 0);
            lv_obj_align(nameLbl, LV_ALIGN_TOP_LEFT, 0, 0);

            // Auteur + version - DUPLIQUER LE TEXTE pour éviter destruction
            char subText[64];
            snprintf(subText, sizeof(subText), "%s  v%s", app.author.c_str(), app.version.c_str());
            lv_obj_t* subLbl = lv_label_create(card);
            lv_label_set_text(subLbl, subText);
            lv_obj_set_style_text_color(subLbl, lv_color_hex(0xAABBCC), 0);
            lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_12, 0);
            lv_obj_align(subLbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

            // Badge installé ou indicateur
            if (app.installed) {
                lv_obj_t* badge = lv_label_create(card);
                if (app.update_available) {
                    lv_label_set_text(badge, LV_SYMBOL_REFRESH " MAJ");
                    lv_obj_set_style_text_color(badge, lv_color_hex(0xFFB74D), 0);
                } else {
                    lv_label_set_text(badge, LV_SYMBOL_OK);
                    lv_obj_set_style_text_color(badge, lv_color_hex(0x4CAF50), 0);
                }
                lv_obj_set_style_text_font(badge, &lv_font_montserrat_14, 0);
                lv_obj_align(badge, LV_ALIGN_RIGHT_MID, 0, 0);
            } else {
                lv_obj_t* arrow = lv_label_create(card);
                lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
                lv_obj_set_style_text_color(arrow, lv_color_hex(0x7799BB), 0);
                lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
            }

            // Clic → page produit
            lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_user_data(card, (void*)(intptr_t)i);
            lv_obj_add_event_cb(card, [](lv_event_t* e) {
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                if (instance) instance->showProductPage(idx);
            }, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        }
        
        Serial.printf("[AppStore] showAppList() DONE - %d cards created\n", totalApps);
    }

    // ─── UI : Page produit (CORE 0) ───────────────────────────

    void showProductPage(int index) {
        // Copier UNE seule app (acceptable)
        mutex_enter_blocking(&appStoreMutex);
        if (index < 0 || index >= (int)sharedData.apps.size()) {
            mutex_exit(&appStoreMutex);
            return;
        }
        AppInfo app = sharedData.apps[index];
        int totalApps = (int)sharedData.apps.size();
        mutex_exit(&appStoreMutex);

        lv_obj_clean(list_cont);
        watchdog_update();

        // Bouton retour à la liste
        lv_obj_t* backBtn = lv_btn_create(list_cont);
        lv_obj_set_size(backBtn, 80, 36);
        lv_obj_align(backBtn, LV_ALIGN_TOP_LEFT, 4, 4);
        lv_obj_set_style_bg_color(backBtn, lv_color_hex(0x2c3e50), 0);
        lv_obj_t* backLbl = lv_label_create(backBtn);
        lv_label_set_text(backLbl, LV_SYMBOL_LEFT " Liste");
        lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_12, 0);
        lv_obj_center(backLbl);
        lv_obj_add_event_cb(backBtn, [](lv_event_t* e) {
            if (instance) instance->showAppList();
        }, LV_EVENT_CLICKED, nullptr);

        // Nom de l'app (gros)
        lv_obj_t* nameLbl = lv_label_create(list_cont);
        lv_label_set_text(nameLbl, app.name.c_str());
        lv_obj_set_style_text_color(nameLbl, lv_color_white(), 0);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_18, 0);
        lv_obj_align(nameLbl, LV_ALIGN_TOP_MID, 0, 50);

        // Catégorie
        if (app.category.length() > 0) {
            lv_obj_t* catLbl = lv_label_create(list_cont);
            lv_label_set_text(catLbl, app.category.c_str());
            lv_obj_set_style_text_color(catLbl, lv_color_hex(0x7799BB), 0);
            lv_obj_set_style_text_font(catLbl, &lv_font_montserrat_12, 0);
            lv_obj_align(catLbl, LV_ALIGN_TOP_MID, 0, 76);
        }

        // Infos
        String infoText = "Auteur: " + app.author + "\nVersion: " + app.version + "\nFichier: " + app.entry;
        if (app.installed) {
            infoText += "\nInstallee: ";
            infoText += (app.local_version.length() > 0) ? app.local_version : "?";
            if (app.update_available) {
                infoText += "  (MAJ disponible)";
            }
        }
        lv_obj_t* infoLbl = lv_label_create(list_cont);
        lv_label_set_long_mode(infoLbl, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(infoLbl, 280);
        lv_label_set_text(infoLbl, infoText.c_str());
        lv_obj_set_style_text_color(infoLbl, lv_color_hex(0xCCDDEE), 0);
        lv_obj_set_style_text_font(infoLbl, &lv_font_montserrat_14, 0);
        lv_obj_align(infoLbl, LV_ALIGN_TOP_MID, 0, 98);

        // Description
        if (app.description.length() > 0) {
            lv_obj_t* descCont = lv_obj_create(list_cont);
            lv_obj_set_size(descCont, 296, 130);
            lv_obj_align(descCont, LV_ALIGN_TOP_MID, 0, 168);
            lv_obj_set_style_bg_color(descCont, lv_color_hex(0x2C3E50), 0);
            lv_obj_set_style_bg_opa(descCont, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(descCont, 0, 0);
            lv_obj_set_style_radius(descCont, 8, 0);
            lv_obj_set_style_pad_all(descCont, 10, 0);

            lv_obj_t* descLbl = lv_label_create(descCont);
            lv_label_set_long_mode(descLbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(descLbl, 272);
            lv_label_set_text(descLbl, app.description.c_str());
            lv_obj_set_style_text_color(descLbl, lv_color_hex(0xAABBCC), 0);
            lv_obj_set_style_text_font(descLbl, &lv_font_montserrat_12, 0);
        }

        // Bouton principal Installer / Mettre a jour / Deja installe
        lv_obj_t* actionBtn = lv_btn_create(list_cont);
        lv_obj_set_size(actionBtn, 250, 50);
        lv_obj_align(actionBtn, LV_ALIGN_BOTTOM_MID, 0, -62);
        lv_obj_t* actionLbl = lv_label_create(actionBtn);
        lv_obj_center(actionLbl);
        lv_obj_set_style_text_font(actionLbl, &lv_font_montserrat_14, 0);

        if (app.installed) {
            if (app.update_available) {
                lv_label_set_text(actionLbl, LV_SYMBOL_REFRESH "  Mettre a jour");
                lv_obj_set_style_bg_color(actionBtn, lv_color_hex(0xEF6C00), 0);
                lv_obj_set_user_data(actionBtn, (void*)(intptr_t)index);
                lv_obj_add_event_cb(actionBtn, [](lv_event_t* e) {
                    if (!instance) return;
                    int idx = (int)(intptr_t)lv_event_get_user_data(e);
                    lv_obj_t* btn = lv_event_get_target(e);
                    lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                    if (!lbl) return;

                    instance->install_index = idx;
                    instance->install_requested = true;

                    lv_label_set_text(lbl, "Mise a jour...");
                    lv_obj_set_style_bg_color(btn, lv_color_hex(0x555555), 0);
                    lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
                }, LV_EVENT_CLICKED, (void*)(intptr_t)index);
            } else {
                lv_label_set_text(actionLbl, LV_SYMBOL_OK "  Deja installe");
                lv_obj_set_style_bg_color(actionBtn, lv_color_hex(0x2E7D32), 0);
            }
        } else {
            lv_label_set_text(actionLbl, LV_SYMBOL_DOWNLOAD "  Installer");
            lv_obj_set_style_bg_color(actionBtn, lv_color_hex(0x1565C0), 0);
            lv_obj_set_user_data(actionBtn, (void*)(intptr_t)index);
            lv_obj_add_event_cb(actionBtn, [](lv_event_t* e) {
                if (!instance) return;
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                lv_obj_t* btn = lv_event_get_target(e);
                lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                if (!lbl) return;  // Sécurité: vérifier que le label existe

                // Demander l'installation en arrière-plan
                instance->install_index = idx;
                instance->install_requested = true;
                
                lv_label_set_text(lbl, "Installation...");
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x555555), 0);
                lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            }, LV_EVENT_CLICKED, (void*)(intptr_t)index);
        }

        // Bouton Desinstaller (si app installee)
        if (app.installed) {
            lv_obj_t* uninstallBtn = lv_btn_create(list_cont);
            lv_obj_set_size(uninstallBtn, 250, 44);
            lv_obj_align(uninstallBtn, LV_ALIGN_BOTTOM_MID, 0, -10);
            lv_obj_set_style_bg_color(uninstallBtn, lv_color_hex(0x8E2430), 0);
            lv_obj_t* uninstallLbl = lv_label_create(uninstallBtn);
            lv_label_set_text(uninstallLbl, LV_SYMBOL_TRASH "  Desinstaller");
            lv_obj_set_style_text_font(uninstallLbl, &lv_font_montserrat_14, 0);
            lv_obj_center(uninstallLbl);

            lv_obj_set_user_data(uninstallBtn, (void*)(intptr_t)index);
            lv_obj_add_event_cb(uninstallBtn, [](lv_event_t* e) {
                if (!instance) return;
                int idx = (int)(intptr_t)lv_event_get_user_data(e);
                lv_obj_t* btn = lv_event_get_target(e);
                lv_obj_t* lbl = lv_obj_get_child(btn, 0);
                if (!lbl) return;

                instance->uninstall_index = idx;
                instance->uninstall_requested = true;

                lv_label_set_text(lbl, "Desinstallation...");
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x555555), 0);
                lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
            }, LV_EVENT_CLICKED, (void*)(intptr_t)index);
        }
    }

    // ─── Callbacks statiques ──────────────────────────────────

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

public:
    void start(lv_obj_t* parent) override {
        instance = this;
        main_bg = parent;
        
        // Debug mémoire
        Serial.printf("[AppStore] START - Free RAM: %d bytes\n", rp2040.getFreeHeap());
        
        lv_obj_set_style_bg_color(main_bg, lv_color_hex(0x1A2634), 0);

        // ── Header ──
        lv_obj_t* header = lv_obj_create(main_bg);
        lv_obj_set_size(header, 320, 50);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x0D1B2A), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(btn_back, go_home, LV_EVENT_CLICKED, NULL);
        lv_obj_t* backIcon = lv_label_create(btn_back);
        lv_label_set_text(backIcon, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(backIcon, lv_color_white(), 0);
        lv_obj_center(backIcon);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, LV_SYMBOL_DOWNLOAD "  App Store");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
        lv_obj_center(title);

        // ── Conteneur liste ──
        list_cont = lv_obj_create(main_bg);
        lv_obj_set_size(list_cont, 320, 430- 18);
        lv_obj_align(list_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_opa(list_cont, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_style_pad_row(list_cont, 8, 0);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // ── Loader ──
        loader = lv_spinner_create(main_bg, 1000, 60);
        lv_obj_set_size(loader, 40, 40);
        lv_obj_center(loader);

        // ── Status label (enfant de main_bg pour survivre à lv_obj_clean) ──
        status_label = lv_label_create(main_bg);
        lv_label_set_text(status_label, "Chargement...");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0x7799BB), 0);
        lv_obj_align(status_label, LV_ALIGN_CENTER, 0, 50);

        // Demander le chargement en arrière-plan
        refresh_requested = true;
    }

    // ─── CORE 0 : Mise à jour UI ─────────────────────────────

    void update() override {
        // Nouvelles données disponibles ?
        if (has_new_data) {
            has_new_data = false;
            lv_obj_add_flag(loader, LV_OBJ_FLAG_HIDDEN);
            
            // Vérifier le succès sans copier le vector
            mutex_enter_blocking(&appStoreMutex);
            bool success = sharedData.success;
            int appCount = sharedData.apps.size();
            String error_msg = sharedData.error_msg;
            mutex_exit(&appStoreMutex);
            
            Serial.printf("[AppStore] UPDATE: success=%d, apps=%d\n", success, appCount);
            
            if (success) {
                Serial.println("[AppStore] UPDATE: Calling showAppList()...");
                showAppList(); // showAppList prendra le mutex
                Serial.println("[AppStore] UPDATE: showAppList() done");
            } else {
                Serial.printf("[AppStore] UPDATE: ERROR: %s\n", error_msg.c_str());
                if (status_label) {
                    lv_label_set_text(status_label, error_msg.c_str());
                    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
                }
            }
        }

        // Installation terminée ?
        if (install_done) {
            install_done = false;
            showProductPage(install_index);
        }

        // Désinstallation terminée ?
        if (uninstall_done) {
            uninstall_done = false;
            showProductPage(uninstall_index);
        }
    }

    // ─── CORE 1 : Travail réseau ─────────────────────────────

    void update1() override {
        // Chargement de la liste ?
        if (refresh_requested) {
            watchdog_update();
            
            AppStoreData newData;
            fetchAppListBackground(newData);
            
            // Déposer les données
            mutex_enter_blocking(&appStoreMutex);
            sharedData = std::move(newData);
            has_new_data = true;
            mutex_exit(&appStoreMutex);
            
            refresh_requested = false;
        }

        // Installation demandée ?
        if (install_requested) {
            watchdog_update();
            
            // Lire les infos nécessaires (court, sous mutex)
            mutex_enter_blocking(&appStoreMutex);
            if (install_index < 0 || install_index >= (int)sharedData.apps.size()) {
                mutex_exit(&appStoreMutex);
                install_requested = false;
                return;
            }
            String appId = sharedData.apps[install_index].id;
            String entry = sharedData.apps[install_index].entry;
            mutex_exit(&appStoreMutex);
            
            // Téléchargement (pas besoin de mutex)
            bool success = downloadAppFiles(appId, entry);
            watchdog_update();
            
            // Mettre à jour le flag installed si succès
            if (success) {
                mutex_enter_blocking(&appStoreMutex);
                if (install_index >= 0 && install_index < (int)sharedData.apps.size()) {
                    sharedData.apps[install_index].installed = true;
                    sharedData.apps[install_index].local_version =
                        sharedData.apps[install_index].version;
                    sharedData.apps[install_index].update_available = false;
                }
                mutex_exit(&appStoreMutex);
            }
            
            install_success = success;
            install_done = true;
            install_requested = false;
        }

        // Désinstallation demandée ?
        if (uninstall_requested) {
            watchdog_update();

            mutex_enter_blocking(&appStoreMutex);
            if (uninstall_index < 0 || uninstall_index >= (int)sharedData.apps.size()) {
                mutex_exit(&appStoreMutex);
                uninstall_requested = false;
                return;
            }
            String appId = sharedData.apps[uninstall_index].id;
            mutex_exit(&appStoreMutex);

            bool success = uninstallAppFiles(appId);
            watchdog_update();

            if (success) {
                mutex_enter_blocking(&appStoreMutex);
                if (uninstall_index >= 0 && uninstall_index < (int)sharedData.apps.size()) {
                    sharedData.apps[uninstall_index].installed = false;
                    sharedData.apps[uninstall_index].local_version = "";
                    sharedData.apps[uninstall_index].update_available = false;
                }
                mutex_exit(&appStoreMutex);
            }

            uninstall_success = success;
            uninstall_done = true;
            uninstall_requested = false;
        }
    }

    void stop() override { instance = nullptr; }
};

AppStoreApp* AppStoreApp::instance = nullptr;

#endif