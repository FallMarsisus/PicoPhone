#ifndef HOME_CONFIG_H
#define HOME_CONFIG_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>
#include "../AppManager.h"

struct HomeAppEntry {
    String id;
    String name;
    String symbol;
    uint32_t color;
    int appId;          // AppID enum, -1 pour Python, -2 pour dossier
    String pythonPath;

    bool isFolder() const { return appId == -2; }
};

namespace homeConfig {

    static bool _fs_ok = false;
    static uint32_t _background_color = 0x408A71;

    inline bool ensureFS() {
        if (!_fs_ok) {
            _fs_ok = LittleFS.begin();
            if (!_fs_ok) Serial.println("[HomeConfig] LittleFS FAILED");
        }
        return _fs_ok;
    }

    const char* CONFIG_FILE = "/config/home2.json";

    // Stockage des contenus de dossiers (non-recursif)
    struct FolderContent {
        String folderId;
        std::vector<HomeAppEntry> items;
    };

    // Meyer's singleton pour éviter les problèmes d'initialisation statique
    inline std::vector<FolderContent>& getFolderStore() {
        static std::vector<FolderContent> store;
        return store;
    }

    inline std::vector<HomeAppEntry>& getFolderChildren(const String& folderId) {
        auto& store = getFolderStore();
        for (auto& fc : store) {
            if (fc.folderId == folderId) return fc.items;
        }
        store.push_back({folderId, {}});
        return store.back().items;
    }

    inline int getFolderChildCount(const String& folderId) {
        auto& store = getFolderStore();
        for (const auto& fc : store) {
            if (fc.folderId == folderId) return (int)fc.items.size();
        }
        return 0;
    }

    inline void clearFolderStore() {
        getFolderStore().clear();
    }

    // Liste COMPLETE de toutes les apps systeme (identique a NewHomeApp)
    HomeAppEntry ALL_APPS[] = {
        {"2048",      "2048",         "2048",              0xFF9500, APP_2048, ""},
        {"wallet",    "Wallet",       LV_SYMBOL_SAVE,      0x1E88E5, APP_WALLET, ""},
        {"sketch",    "Ardoise",      LV_SYMBOL_EDIT,      0x5AC8FA, APP_SKETCH, ""},
        {"calc",      "Calculatrice", LV_SYMBOL_PLUS,      0xFF3B30, APP_CALC, ""},
        {"contacts",  "Contacts",     LV_SYMBOL_LIST,      0x5856D6, APP_CONTACTS, ""},
        {"explorer",  "Explorateur",  LV_SYMBOL_DIRECTORY,  0x34C759, APP_EXPLORER, ""},
        {"timer",     "Horloge",      LV_SYMBOL_BELL,      0x007AFF, APP_TIMER, ""},
        {"weather",   "Meteo",        LV_SYMBOL_CHARGE,    0x4CAF50, APP_WEATHER, ""},
        {"phone",     "Phone",        LV_SYMBOL_CALL,      0x4CAF50, APP_PHONE, ""},
        {"settings",  "Settings",     LV_SYMBOL_SETTINGS,  0xFF9800, APP_SETTINGS, ""},
        {"sms",       "SMS",          LV_SYMBOL_KEYBOARD,  0xFF5722, APP_SMS, ""},
        {"telegram",  "Telegram",     LV_SYMBOL_GPS,       0x2196F3, APP_TELEGRAM, ""},
        {"velib",     "Velib",        "V",                 0x9C27B0, APP_VELIB, ""},
        {"wifi",      "WiFi",         LV_SYMBOL_WIFI,      0x00BCD4, APP_WIFI, ""},
        {"webradio",  "Radio",        LV_SYMBOL_HOME,      0x607D8B, APP_WEBRADIO, ""},
        {"store",     "Store",        LV_SYMBOL_DOWNLOAD,  0x1565C0, APP_STORE, ""},
        {"crypto",    "Crypto",       LV_SYMBOL_CHARGE,    0xF7931A, APP_CRYPTO, ""},
        {"news",      "Actualites",   LV_SYMBOL_LIST,      0xFF3B30, APP_NEWS, ""},
        {"bambu",     "Bambu",        LV_SYMBOL_SETTINGS,  0x00A86B, APP_BAMBU, ""},
        {"chatbot",   "Gemini",       "AI",              0x7C4DFF, APP_CHATBOT, ""},
    };
    const int ALL_APPS_COUNT = 20;

    // Charge la config. Si pas de fichier -> toutes les apps par defaut.
    bool loadConfig(std::vector<HomeAppEntry>& apps) {
        clearFolderStore();
        if (!ensureFS() || !LittleFS.exists(CONFIG_FILE)) {
            apps.assign(ALL_APPS, ALL_APPS + ALL_APPS_COUNT);
            _background_color = 0x408A71;
            return true;
        }
        File f = LittleFS.open(CONFIG_FILE, "r");
        if (!f) { apps.assign(ALL_APPS, ALL_APPS + ALL_APPS_COUNT); return true; }

        JsonDocument doc;
        if (deserializeJson(doc, f)) {
            f.close();
            apps.assign(ALL_APPS, ALL_APPS + ALL_APPS_COUNT);
            _background_color = 0x408A71;
            return true;
        }
        f.close();

        _background_color = doc["backgroundColor"] | 0x408A71;

        apps.clear();
        JsonArray arr = doc["apps"].as<JsonArray>();
        for (const auto& item : arr) {
            HomeAppEntry entry;
            entry.id = item["id"].as<String>();
            entry.name = item["name"].as<String>();
            entry.symbol = item["symbol"].as<String>();
            entry.color = item["color"].as<uint32_t>();
            entry.appId = item["appId"].as<int>();
            entry.pythonPath = item["pythonPath"] | "";
            apps.push_back(entry);
            // Charger les enfants si c'est un dossier
            if (entry.isFolder() && item["children"].is<JsonArray>()) {
                auto& children = getFolderChildren(entry.id);
                children.clear();
                JsonArray ch = item["children"].as<JsonArray>();
                for (const auto& c : ch) {
                    HomeAppEntry child;
                    child.id = c["id"].as<String>();
                    child.name = c["name"].as<String>();
                    child.symbol = c["symbol"].as<String>();
                    child.color = c["color"].as<uint32_t>();
                    child.appId = c["appId"].as<int>();
                    child.pythonPath = c["pythonPath"] | "";
                    children.push_back(child);
                }
            }
        }

        // Migration: garantir la presence des nouvelles apps systeme dans le menu principal.
        bool weather_present = false;
        bool bambu_present = false;
        bool chatbot_present = false;
        for (const auto& app : apps) {
            if (app.id == "weather") {
                weather_present = true;
            }
            if (app.id == "bambu") {
                bambu_present = true;
            }
            if (app.id == "chatbot") {
                chatbot_present = true;
            }
        }

        for (size_t i = 0; i < apps.size();) {
            if (apps[i].id == "airquality") {
                if (weather_present) {
                    apps.erase(apps.begin() + (long)i);
                    continue;
                }
                apps[i].id = "weather";
                apps[i].name = "Meteo";
                apps[i].symbol = LV_SYMBOL_CHARGE;
                apps[i].color = 0x4CAF50;
                apps[i].appId = APP_WEATHER;
                weather_present = true;
            }
            ++i;
        }

        if (!weather_present) {
            for (int i = 0; i < ALL_APPS_COUNT; ++i) {
                if (ALL_APPS[i].id == "weather") {
                    apps.push_back(ALL_APPS[i]);
                    break;
                }
            }
        }

        if (!bambu_present) {
            for (int i = 0; i < ALL_APPS_COUNT; ++i) {
                if (ALL_APPS[i].id == "bambu") {
                    apps.push_back(ALL_APPS[i]);
                    break;
                }
            }
        }

        if (!chatbot_present) {
            for (int i = 0; i < ALL_APPS_COUNT; ++i) {
                if (ALL_APPS[i].id == "chatbot") {
                    apps.push_back(ALL_APPS[i]);
                    break;
                }
            }
        }

        Serial.printf("[HomeConfig] Loaded %d apps\n", (int)apps.size());
        return true;
    }

    bool saveConfig(const std::vector<HomeAppEntry>& apps) {
        if (!ensureFS()) return false;
        LittleFS.mkdir("/config");

        JsonDocument doc;
        doc["backgroundColor"] = _background_color;
        JsonArray arr = doc["apps"].to<JsonArray>();
        for (const auto& e : apps) {
            JsonObject obj = arr.add<JsonObject>();
            obj["id"] = e.id;
            obj["name"] = e.name;
            obj["symbol"] = e.symbol;
            obj["color"] = e.color;
            obj["appId"] = e.appId;
            if (e.pythonPath.length() > 0)
                obj["pythonPath"] = e.pythonPath;
            // Sauver les enfants si c'est un dossier
            if (e.isFolder()) {
                auto& ch_items = getFolderChildren(e.id);
                if (!ch_items.empty()) {
                    JsonArray ch = obj["children"].to<JsonArray>();
                    for (const auto& c : ch_items) {
                        JsonObject co = ch.add<JsonObject>();
                        co["id"] = c.id;
                        co["name"] = c.name;
                        co["symbol"] = c.symbol;
                        co["color"] = c.color;
                        co["appId"] = c.appId;
                        if (c.pythonPath.length() > 0)
                            co["pythonPath"] = c.pythonPath;
                    }
                }
            }
        }
        File f = LittleFS.open(CONFIG_FILE, "w");
        if (!f) return false;
        bool ok = serializeJson(doc, f) > 0;
        f.close();
        Serial.printf("[HomeConfig] Saved %d apps\n", (int)apps.size());
        return ok;
    }

    bool hasAppId(const std::vector<HomeAppEntry>& apps, const String& id) {
        for (const auto& a : apps) {
            if (a.id == id) return true;
        }
        return false;
    }

    void removeAppById(std::vector<HomeAppEntry>& apps, const String& id) {
        for (auto it = apps.begin(); it != apps.end(); ++it) {
            if (it->id == id) { apps.erase(it); return; }
        }
    }

    void listPythonApps(std::vector<HomeAppEntry>& out) {
        out.clear();
        if (!ensureFS()) return;
        File root = LittleFS.open("/apps", "r");
        if (!root || !root.isDirectory()) return;

        File dir = root.openNextFile();
        while (dir) {
            if (dir.isDirectory()) {
                String dirName = String(dir.name());
                int slash = dirName.lastIndexOf('/');
                String appId = (slash >= 0) ? dirName.substring(slash + 1) : dirName;
                String base = "/apps/" + appId;

                if (LittleFS.exists(base + "/manifest.json") && LittleFS.exists(base + "/main.py")) {
                    String name = appId;
                    String symbol = LV_SYMBOL_FILE;
                    uint32_t color = 0x5AC8FA;

                    File mf = LittleFS.open(base + "/manifest.json", "r");
                    if (mf) {
                        JsonDocument mdoc;
                        if (!deserializeJson(mdoc, mf)) {
                            if (mdoc["name"].is<const char*>()) name = mdoc["name"].as<String>();
                            if (mdoc["icon"].is<const char*>()) symbol = mdoc["icon"].as<String>();
                            if (mdoc["color"].is<int>()) color = (uint32_t)mdoc["color"].as<int>();
                        }
                        mf.close();
                    }

                    HomeAppEntry entry;
                    entry.id = "py:" + appId;
                    entry.name = name;
                    entry.symbol = symbol;
                    entry.color = color;
                    entry.appId = -1;
                    entry.pythonPath = base + "/main.py";
                    out.push_back(entry);
                }
            }
            dir = root.openNextFile();
        }
        Serial.printf("[HomeConfig] Found %d Python apps\n", (int)out.size());
    }

    void resetToDefault(std::vector<HomeAppEntry>& apps) {
        clearFolderStore();
        apps.assign(ALL_APPS, ALL_APPS + ALL_APPS_COUNT);
        _background_color = 0x408A71;
        saveConfig(apps);
        Serial.println("[HomeConfig] Reset to default");
    }

    inline uint32_t getBackgroundColor() {
        return _background_color;
    }

    inline void setBackgroundColor(uint32_t color) {
        _background_color = color;
    }
}

#endif
