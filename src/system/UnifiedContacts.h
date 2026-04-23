#ifndef SYSTEM_UNIFIED_CONTACTS_H
#define SYSTEM_UNIFIED_CONTACTS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <vector>

namespace unified_contacts {

static constexpr const char* CONTACTS_PATH = "/contacts_book.json";

struct Entry {
    String first_name;
    String last_name;
    String phone;
    String telegram_id;
};

inline std::vector<Entry>& cache() {
    static std::vector<Entry> v;
    return v;
}

inline uint32_t& last_load_ms() {
    static uint32_t t = 0;
    return t;
}

inline bool ieq(const String& a, const String& b) {
    if (a.length() != b.length()) return false;
    for (int i = 0; i < a.length(); ++i) {
        char ca = a.charAt(i);
        char cb = b.charAt(i);
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

inline String normalize_phone(const String& in) {
    String out;
    out.reserve(in.length());
    for (int i = 0; i < in.length(); ++i) {
        char c = in.charAt(i);
        if ((c >= '0' && c <= '9') || (c == '+' && out.length() == 0)) {
            out += c;
        }
    }
    return out;
}

inline String full_name(const Entry& e) {
    String n = e.first_name;
    if (e.last_name.length() > 0) {
        if (n.length() > 0) n += " ";
        n += e.last_name;
    }
    n.trim();
    return n;
}

inline void reload() {
    std::vector<Entry>& c = cache();
    c.clear();

    if (!LittleFS.begin()) {
        last_load_ms() = millis();
        return;
    }
    if (!LittleFS.exists(CONTACTS_PATH)) {
        last_load_ms() = millis();
        return;
    }

    File f = LittleFS.open(CONTACTS_PATH, "r");
    if (!f) {
        last_load_ms() = millis();
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonArray>()) {
        last_load_ms() = millis();
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    c.reserve(arr.size());
    for (JsonObject obj : arr) {
        Entry e;
        e.first_name = obj["prenom"] | "";
        e.last_name = obj["nom"] | "";
        e.phone = normalize_phone(String(obj["tel"] | ""));
        e.telegram_id = String(obj["tg"] | "");
        e.telegram_id.trim();
        c.push_back(e);
    }

    last_load_ms() = millis();
}

inline void ensure_fresh(uint32_t max_age_ms = 5000) {
    const uint32_t now = millis();
    if (last_load_ms() == 0 || (uint32_t)(now - last_load_ms()) > max_age_ms) {
        reload();
    }
}

inline String display_name_for_telegram(const String& telegram_id, const String& fallback) {
    ensure_fresh();
    String id = telegram_id;
    id.trim();
    if (id.length() == 0) return fallback;

    for (const auto& e : cache()) {
        if (e.telegram_id.length() > 0 && e.telegram_id == id) {
            String n = full_name(e);
            if (n.length() > 0) return n;
            break;
        }
    }
    return fallback;
}

inline String display_name_for_phone(const String& phone, const String& fallback) {
    ensure_fresh();
    String p = normalize_phone(phone);
    if (p.length() == 0) return fallback;

    for (const auto& e : cache()) {
        if (e.phone.length() > 0 && normalize_phone(e.phone) == p) {
            String n = full_name(e);
            if (n.length() > 0) return n;
            break;
        }
    }
    return fallback;
}

inline bool phone_for_name(const String& name, String& out_phone) {
    ensure_fresh();
    String target = name;
    target.trim();
    if (target.length() == 0) return false;

    for (const auto& e : cache()) {
        String n = full_name(e);
        if (n.length() == 0 || e.phone.length() == 0) continue;
        if (ieq(n, target)) {
            out_phone = e.phone;
            return true;
        }
    }
    return false;
}

} // namespace unified_contacts

#endif
