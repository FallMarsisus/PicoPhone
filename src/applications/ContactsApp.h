#ifndef CONTACTS_APP_H
#define CONTACTS_APP_H

#include "App.h"
#include "AppManager.h"
#include <vector>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Theme sombre moderne (style iOS)
#define COL_BG       0x000000
#define COL_SURFACE  0x1C1C1E
#define COL_CARD     0x2C2C2E
#define COL_TEXT     0xFFFFFF
#define COL_SUB      0x8E8E93
#define COL_ACCENT   0x0A84FF
#define COL_OK       0x34C759

static constexpr const char* CONTACTS_PATH = "/contacts_book.json";

struct Contact {
    String nom;
    String prenom;
    String telephone;
    String email;
    String telegramID;
};

struct TgSource {
    String id;
    String name;
};

class ContactsApp : public App {
private:
    std::vector<Contact> contacts;
    std::vector<TgSource> tg_sources;

    bool fs_ok = false;
    int form_step = 0;
    static constexpr int FORM_STEPS = 5;

    lv_obj_t* list_cont = nullptr;
    lv_obj_t* editor_overlay = nullptr;
    lv_obj_t* kb = nullptr;
    lv_obj_t* tg_dropdown = nullptr;
    lv_obj_t* status_lbl = nullptr;
    lv_obj_t* step_lbl = nullptr;
    lv_obj_t* form_title_lbl = nullptr;
    lv_obj_t* form_hint_lbl = nullptr;
    lv_obj_t* btn_prev = nullptr;
    lv_obj_t* btn_next = nullptr;
    lv_obj_t* btn_next_lbl = nullptr;

    lv_obj_t* ta_nom = nullptr;
    lv_obj_t* ta_prenom = nullptr;
    lv_obj_t* ta_tel = nullptr;
    lv_obj_t* ta_email = nullptr;
    lv_obj_t* ta_tg = nullptr;

    static void disable_scroll(lv_obj_t* obj) {
        if (!obj) return;
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
    }

    static void go_home(lv_event_t* e) { AppManager::switchTo(APP_HOME); }

    static void open_editor_cb(lv_event_t* e) {
        ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
        if (!app || !app->editor_overlay) return;
        app->open_editor();
    }

    static void close_editor_cb(lv_event_t* e) {
        ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
        if (!app || !app->editor_overlay) return;
        app->close_editor();
    }

    static void prev_step_cb(lv_event_t* e) {
        ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->go_prev_step();
    }

    static void next_step_cb(lv_event_t* e) {
        ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->go_next_step();
    }

    static void kb_event_cb(lv_event_t* e) {
        ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
        if (!app) return;
        const lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_READY) {
            app->go_next_step();
        } else if (code == LV_EVENT_CANCEL) {
            app->close_editor();
        }
    }

    static void tg_dropdown_cb(lv_event_t* e) {
        ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
        if (!app || !app->tg_dropdown || !app->ta_tg) return;

        uint16_t selected = lv_dropdown_get_selected(app->tg_dropdown);
        if (selected == 0) {
            lv_textarea_set_text(app->ta_tg, "");
            return;
        }

        size_t idx = (size_t)(selected - 1);
        if (idx >= app->tg_sources.size()) return;

        const TgSource& src = app->tg_sources[idx];
        lv_textarea_set_text(app->ta_tg, src.id.c_str());

        const char* prenom = lv_textarea_get_text(app->ta_prenom);
        const char* nom = lv_textarea_get_text(app->ta_nom);
        bool empty_name = (!prenom || prenom[0] == '\0') && (!nom || nom[0] == '\0');
        if (empty_name && src.name.length() > 0) {
            int space = src.name.indexOf(' ');
            if (space > 0) {
                String first = src.name.substring(0, space);
                String last = src.name.substring(space + 1);
                lv_textarea_set_text(app->ta_prenom, first.c_str());
                lv_textarea_set_text(app->ta_nom, last.c_str());
            } else {
                lv_textarea_set_text(app->ta_prenom, src.name.c_str());
            }
        }
    }

    static bool has_suffix(const String& value, const char* suffix) {
        const size_t vlen = value.length();
        const size_t slen = strlen(suffix);
        if (vlen < slen) return false;
        return value.substring(vlen - slen).equals(suffix);
    }

    int find_tg_source_by_id(const String& id) {
        for (size_t i = 0; i < tg_sources.size(); i++) {
            if (tg_sources[i].id == id) return (int)i;
        }
        return -1;
    }

    void add_tg_source_if_valid(const String& id, const String& name) {
        if (id.length() == 0) return;

        int idx = find_tg_source_by_id(id);
        if (idx < 0) {
            TgSource s;
            s.id = id;
            s.name = name;
            tg_sources.push_back(s);
            return;
        }

        if (tg_sources[(size_t)idx].name.length() == 0 && name.length() > 0) {
            tg_sources[(size_t)idx].name = name;
        }
    }

    void load_tg_ids_from_telegram_index() {
        if (!fs_ok) return;
        if (!LittleFS.exists("/contacts.json")) return;

        File f = LittleFS.open("/contacts.json", "r");
        if (!f) return;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err || !doc.is<JsonArray>()) return;

        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject obj : arr) {
            String id = obj["id"] | "";
            String name = obj["n"] | "";
            add_tg_source_if_valid(id, name);
        }
    }

    void load_tg_ids_from_chat_files() {
        if (!fs_ok) return;

        File root = LittleFS.open("/", "r");
        if (!root) return;

        File file = root.openNextFile();
        while (file) {
            String name = file.name();
            file.close();

            if (name == "/contacts.json" || name == CONTACTS_PATH) {
                file = root.openNextFile();
                continue;
            }

            if (!has_suffix(name, ".json")) {
                file = root.openNextFile();
                continue;
            }

            String id = name;
            if (id.startsWith("/")) id.remove(0, 1);
            id.remove(id.length() - 5);
            add_tg_source_if_valid(id, "");

            file = root.openNextFile();
        }
        root.close();
    }

    void refresh_tg_dropdown() {
        if (!tg_dropdown) return;

        String options = "No link";
        for (const auto& src : tg_sources) {
            options += "\n";
            if (src.name.length() > 0) {
                options += src.name;
                options += " (";
                options += src.id;
                options += ")";
            } else {
                options += src.id;
            }
        }
        lv_dropdown_set_options(tg_dropdown, options.c_str());
    }

    void load_telegram_sources() {
        tg_sources.clear();
        load_tg_ids_from_telegram_index();
        load_tg_ids_from_chat_files();
        refresh_tg_dropdown();
    }

    void clear_form() {
        if (ta_prenom) lv_textarea_set_text(ta_prenom, "");
        if (ta_nom) lv_textarea_set_text(ta_nom, "");
        if (ta_tel) lv_textarea_set_text(ta_tel, "");
        if (ta_email) lv_textarea_set_text(ta_email, "");
        if (ta_tg) lv_textarea_set_text(ta_tg, "");
        if (tg_dropdown) lv_dropdown_set_selected(tg_dropdown, 0);
    }

    lv_obj_t* get_step_textarea(int step) {
        switch (step) {
            case 0: return ta_prenom;
            case 1: return ta_nom;
            case 2: return ta_tel;
            case 3: return ta_email;
            case 4: return ta_tg;
            default: return ta_prenom;
        }
    }

    const char* get_step_title(int step) {
        switch (step) {
            case 0: return "Step 1/5 - First name";
            case 1: return "Step 2/5 - Last name";
            case 2: return "Step 3/5 - Phone";
            case 3: return "Step 4/5 - Email";
            case 4: return "Step 5/5 - Link Telegram";
            default: return "Step";
        }
    }

    const char* get_step_hint(int step) {
        switch (step) {
            case 0: return "Type first name, then press OK";
            case 1: return "Type last name, then press OK";
            case 2: return "Type phone number, then press OK";
            case 3: return "Type email, then press OK";
            case 4: return "Link this contact to a Telegram ID";
            default: return "";
        }
    }

    void update_step_ui() {
        if (!form_title_lbl || !form_hint_lbl || !step_lbl || !btn_next_lbl) return;

        lv_label_set_text(form_title_lbl, get_step_title(form_step));
        lv_label_set_text(form_hint_lbl, get_step_hint(form_step));

        char step_text[32];
        snprintf(step_text, sizeof(step_text), "Step %d of %d", form_step + 1, FORM_STEPS);
        lv_label_set_text(step_lbl, step_text);

        lv_obj_t* ta_current = get_step_textarea(form_step);
        lv_obj_t* all_fields[5] = {ta_prenom, ta_nom, ta_tel, ta_email, ta_tg};
        for (lv_obj_t* field : all_fields) {
            if (!field) continue;
            if (field == ta_current) lv_obj_clear_flag(field, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(field, LV_OBJ_FLAG_HIDDEN);
        }

        if (form_step == 4) lv_obj_clear_flag(tg_dropdown, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(tg_dropdown, LV_OBJ_FLAG_HIDDEN);

        if (form_step == 0) lv_obj_add_flag(btn_prev, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(btn_prev, LV_OBJ_FLAG_HIDDEN);

        if (form_step == FORM_STEPS - 1) lv_label_set_text(btn_next_lbl, "Save");
        else lv_label_set_text(btn_next_lbl, "Next");

        if (kb && ta_current) lv_keyboard_set_textarea(kb, ta_current);
    }

    void open_editor() {
        load_telegram_sources();
        clear_form();
        form_step = 0;
        lv_obj_clear_flag(editor_overlay, LV_OBJ_FLAG_HIDDEN);
        update_step_ui();
    }

    void close_editor() {
        lv_obj_add_flag(editor_overlay, LV_OBJ_FLAG_HIDDEN);
        if (kb) lv_keyboard_set_textarea(kb, nullptr);
    }

    bool save_contact_from_form() {
        Contact newC = {
            lv_textarea_get_text(ta_nom),
            lv_textarea_get_text(ta_prenom),
            lv_textarea_get_text(ta_tel),
            lv_textarea_get_text(ta_email),
            lv_textarea_get_text(ta_tg)
        };

        if (newC.nom.length() == 0 && newC.prenom.length() == 0 && newC.telephone.length() == 0 && newC.email.length() == 0 && newC.telegramID.length() == 0) {
            return false;
        }

        if (newC.prenom.length() == 0 && newC.nom.length() == 0 && newC.telegramID.length() > 0) {
            int idx = find_tg_source_by_id(newC.telegramID);
            if (idx >= 0 && tg_sources[(size_t)idx].name.length() > 0) {
                String full = tg_sources[(size_t)idx].name;
                int sp = full.indexOf(' ');
                if (sp > 0) {
                    newC.prenom = full.substring(0, sp);
                    newC.nom = full.substring(sp + 1);
                } else {
                    newC.prenom = full;
                }
            }
        }

        contacts.push_back(newC);
        save_contacts_to_flash();
        refresh_list();
        return true;
    }

    void go_prev_step() {
        if (form_step <= 0) return;
        form_step--;
        update_step_ui();
    }

    void go_next_step() {
        if (form_step < FORM_STEPS - 1) {
            form_step++;
            update_step_ui();
            return;
        }

        bool saved = save_contact_from_form();
        if (saved) {
            if (status_lbl) {
                lv_label_set_text(status_lbl, "Contact saved");
                lv_obj_set_style_text_color(status_lbl, lv_color_hex(COL_OK), 0);
            }
            close_editor();
        } else if (status_lbl) {
            lv_label_set_text(status_lbl, "Fill at least one field");
            lv_obj_set_style_text_color(status_lbl, lv_color_hex(0xFF9500), 0);
        }
    }

    void save_contacts_to_flash() {
        if (!fs_ok) return;
        File f = LittleFS.open(CONTACTS_PATH, "w");
        if (!f) return;

        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& c : contacts) {
            JsonObject obj = arr.add<JsonObject>();
            obj["nom"] = c.nom;
            obj["prenom"] = c.prenom;
            obj["tel"] = c.telephone;
            obj["email"] = c.email;
            obj["tg"] = c.telegramID;
        }

        serializeJson(arr, f);
        f.close();
    }

    void load_contacts_from_flash() {
        contacts.clear();
        if (!fs_ok) return;
        if (!LittleFS.exists(CONTACTS_PATH)) return;

        File f = LittleFS.open(CONTACTS_PATH, "r");
        if (!f) return;

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, f);
        f.close();
        if (err || !doc.is<JsonArray>()) return;

        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject obj : arr) {
            Contact c;
            c.nom = obj["nom"] | "";
            c.prenom = obj["prenom"] | "";
            c.telephone = obj["tel"] | "";
            c.email = obj["email"] | "";
            c.telegramID = obj["tg"] | "";
            contacts.push_back(c);
        }
    }

    static void save_contact_cb(lv_event_t* e) {
        ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
        if (!app) return;
        app->go_next_step();
    }

    void refresh_list() {
        if (!list_cont) return;
        lv_obj_clean(list_cont);

        if (contacts.empty()) {
            lv_obj_t* empty = lv_label_create(list_cont);
            lv_label_set_text(empty, "Aucun contact");
            lv_obj_set_style_text_color(empty, lv_color_hex(COL_SUB), 0);
            lv_obj_center(empty);
            return;
        }

        for (const auto& c : contacts) {
            lv_obj_t* card = lv_obj_create(list_cont);
            lv_obj_set_size(card, 304, 92);
            lv_obj_set_style_bg_color(card, lv_color_hex(COL_CARD), 0);
            lv_obj_set_style_radius(card, 16, 0);
            lv_obj_set_style_border_width(card, 0, 0);
            lv_obj_set_style_pad_all(card, 12, 0);
            disable_scroll(card);

            lv_obj_t* avatar = lv_obj_create(card);
            lv_obj_set_size(avatar, 44, 44);
            lv_obj_set_style_bg_color(avatar, lv_color_hex(COL_ACCENT), 0);
            lv_obj_set_style_border_width(avatar, 0, 0);
            lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
            lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 0, -2);
            disable_scroll(avatar);

            lv_obj_t* initial = lv_label_create(avatar);
            char first[2] = {'?', '\0'};
            if (c.prenom.length() > 0) first[0] = c.prenom[0];
            else if (c.nom.length() > 0) first[0] = c.nom[0];
            lv_label_set_text(initial, first);
            lv_obj_set_style_text_color(initial, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_text_font(initial, &lv_font_montserrat_14, 0);
            lv_obj_center(initial);

            String display_name = c.prenom;
            if (c.nom.length() > 0) {
                if (display_name.length() > 0) display_name += " ";
                display_name += c.nom;
            }
            display_name.trim();
            if (display_name.length() == 0) {
                display_name = "Sans nom";
            }

            lv_obj_t* name_lbl = lv_label_create(card);
            lv_label_set_text(name_lbl, display_name.c_str());
            lv_obj_set_style_text_color(name_lbl, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_text_font(name_lbl, &lv_font_montserrat_14, 0);
            lv_obj_set_width(name_lbl, 200);
            lv_label_set_long_mode(name_lbl, LV_LABEL_LONG_DOT);
            lv_obj_align(name_lbl, LV_ALIGN_TOP_LEFT, 56, 6);

            String meta = "Tel: ";
            meta += (c.telephone.length() > 0) ? c.telephone : "-";
            meta += "  |  TG: ";
            meta += (c.telegramID.length() > 0) ? c.telegramID : "-";

            lv_obj_t* meta_lbl = lv_label_create(card);
            lv_label_set_text(meta_lbl, meta.c_str());
            lv_obj_set_style_text_color(meta_lbl, lv_color_hex(COL_SUB), 0);
            lv_obj_set_style_text_font(meta_lbl, &lv_font_montserrat_12, 0);
            lv_obj_set_width(meta_lbl, 210);
            lv_label_set_long_mode(meta_lbl, LV_LABEL_LONG_DOT);
            lv_obj_align(meta_lbl, LV_ALIGN_TOP_LEFT, 56, 34);

            if (c.telegramID.length() > 0) {
                lv_obj_t* btn_tg = lv_btn_create(card);
                lv_obj_set_size(btn_tg, 32, 32);
                lv_obj_align(btn_tg, LV_ALIGN_RIGHT_MID, 0, 0);
                lv_obj_set_style_bg_color(btn_tg, lv_color_hex(COL_ACCENT), 0);
                lv_obj_set_style_border_width(btn_tg, 0, 0);
                lv_obj_set_style_radius(btn_tg, LV_RADIUS_CIRCLE, 0);
                lv_obj_t* l_tg = lv_label_create(btn_tg);
                lv_label_set_text(l_tg, LV_SYMBOL_GPS);
                lv_obj_set_style_text_font(l_tg, &lv_font_montserrat_12, 0);
                lv_obj_center(l_tg);
            }
        }
    }

public:
    void start(lv_obj_t* parent) override {
        fs_ok = LittleFS.begin();
        load_contacts_from_flash();

        lv_obj_set_style_bg_color(parent, lv_color_hex(COL_BG), 0);
        disable_scroll(parent);

        // --- INTERFACE PRINCIPALE (INTACTE) ---
        lv_obj_t* header = lv_obj_create(parent);
        lv_obj_set_size(header, lv_pct(100), 56);
        lv_obj_set_style_bg_color(header, lv_color_hex(COL_SURFACE), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        disable_scroll(header);

        lv_obj_t* btn_back = lv_btn_create(header);
        lv_obj_set_size(btn_back, 40, 40);
        lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn_back, 0, 0);
        lv_obj_add_event_cb(btn_back, go_home, LV_EVENT_CLICKED, nullptr);
        lv_obj_t* lbl_back = lv_label_create(btn_back);
        lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(lbl_back, lv_color_hex(COL_ACCENT), 0);
        lv_obj_center(lbl_back);
        
        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Contacts");
        lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
        lv_obj_center(title);

        lv_obj_t* btn_add = lv_btn_create(header);
        lv_obj_set_size(btn_add, 40, 40);
        lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn_add, 0, 0);
        lv_obj_t* lbl_add = lv_label_create(btn_add);
        lv_label_set_text(lbl_add, LV_SYMBOL_PLUS);
        lv_obj_set_style_text_color(lbl_add, lv_color_hex(COL_ACCENT), 0);
        lv_obj_center(lbl_add);
        lv_obj_add_event_cb(btn_add, open_editor_cb, LV_EVENT_CLICKED, this);

        status_lbl = nullptr;

        list_cont = lv_obj_create(parent);
        lv_obj_set_size(list_cont, lv_pct(100), 406);
        lv_obj_align(list_cont, LV_ALIGN_TOP_MID, 0, 56);
        lv_obj_set_flex_flow(list_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(list_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(list_cont, 0, 0);
        lv_obj_set_style_border_width(list_cont, 0, 0);
        lv_obj_set_style_pad_gap(list_cont, 10, 0);
        lv_obj_set_style_pad_top(list_cont, 8, 0);
        lv_obj_set_style_pad_bottom(list_cont, 8, 0);
        lv_obj_set_scrollbar_mode(list_cont, LV_SCROLLBAR_MODE_OFF);


        // --- ECRAN D'AJOUT DE CONTACT (AMELIORE & FIXE) ---
        editor_overlay = lv_obj_create(parent);
        lv_obj_set_size(editor_overlay, lv_pct(100), lv_pct(100));
        lv_obj_add_flag(editor_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(editor_overlay, lv_color_hex(COL_BG), 0);
        lv_obj_set_style_border_width(editor_overlay, 0, 0);
        
        // C'EST CECI QUI REGLE LES BORDURES NOIRES ET LE SCROLL:
        lv_obj_set_style_pad_all(editor_overlay, 0, 0); 
        disable_scroll(editor_overlay);

        lv_obj_t* ov_header = lv_obj_create(editor_overlay);
        lv_obj_set_size(ov_header, lv_pct(100), 52);
        lv_obj_set_style_bg_color(ov_header, lv_color_hex(COL_SURFACE), 0);
        lv_obj_set_style_border_width(ov_header, 0, 0);
        lv_obj_set_style_radius(ov_header, 0, 0);
        lv_obj_set_style_pad_all(ov_header, 0, 0);
        lv_obj_align(ov_header, LV_ALIGN_TOP_MID, 0, 0);
        disable_scroll(ov_header);

        lv_obj_t* ov_title = lv_label_create(ov_header);
        lv_label_set_text(ov_title, "Create contact");
        lv_obj_set_style_text_color(ov_title, lv_color_hex(COL_TEXT), 0);
        lv_obj_align(ov_title, LV_ALIGN_CENTER, 0, 0);

        lv_obj_t* btn_close = lv_btn_create(ov_header);
        lv_obj_set_size(btn_close, 40, 40);
        lv_obj_align(btn_close, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn_close, 0, 0);
        lv_obj_add_event_cb(btn_close, close_editor_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* lbl_close = lv_label_create(btn_close);
        lv_label_set_text(lbl_close, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_color(lbl_close, lv_color_hex(COL_ACCENT), 0);
        lv_obj_center(lbl_close);

        // --- Textes descriptifs centrés proprement ---
        step_lbl = lv_label_create(editor_overlay);
        lv_label_set_text(step_lbl, "Step 1 of 5");
        lv_obj_set_style_text_color(step_lbl, lv_color_hex(COL_SUB), 0);
        lv_obj_align(step_lbl, LV_ALIGN_TOP_MID, 0, 65);

        form_title_lbl = lv_label_create(editor_overlay);
        lv_label_set_text(form_title_lbl, "Step 1/5 - First name");
        lv_obj_set_style_text_color(form_title_lbl, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_text_font(form_title_lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(form_title_lbl, LV_ALIGN_TOP_MID, 0, 85);

        form_hint_lbl = lv_label_create(editor_overlay);
        lv_label_set_text(form_hint_lbl, "Type first name, then press OK");
        lv_obj_set_style_text_color(form_hint_lbl, lv_color_hex(COL_SUB), 0);
        lv_obj_align(form_hint_lbl, LV_ALIGN_TOP_MID, 0, 105);

        // --- Fonction de création des champs centrés ---
        auto create_input = [&](const char* placeholder) {
            lv_obj_t* ta = lv_textarea_create(editor_overlay);
            lv_obj_set_size(ta, lv_pct(90), 40);           // S'adapte à 90% de l'écran
            lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 135);    // Toujours bien au centre
            lv_textarea_set_placeholder_text(ta, placeholder);
            lv_textarea_set_one_line(ta, true);
            lv_obj_set_style_bg_color(ta, lv_color_hex(COL_CARD), 0);
            lv_obj_set_style_text_color(ta, lv_color_hex(COL_TEXT), 0);
            lv_obj_set_style_border_width(ta, 0, 0);
            lv_obj_set_style_radius(ta, 10, 0);
            
            // Tes événements d'origine (safe)
            lv_obj_add_event_cb(ta, [](lv_event_t* e){
                ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
                if (!app || !app->kb) return;
                lv_keyboard_set_textarea(app->kb, (lv_obj_t*)lv_event_get_target(e));
            }, LV_EVENT_FOCUSED, this);
            
            lv_obj_add_event_cb(ta, [](lv_event_t* e){
                ContactsApp* app = (ContactsApp*)lv_event_get_user_data(e);
                if (!app || !app->kb) return;
                lv_keyboard_set_textarea(app->kb, (lv_obj_t*)lv_event_get_target(e));
            }, LV_EVENT_CLICKED, this);
            return ta;
        };

        // On ne passe plus le 'y', le lambda centre tout à y=135
        ta_prenom = create_input("Prenom");
        ta_nom = create_input("Nom");
        ta_tel = create_input("Telephone");
        ta_email = create_input("Email");
        ta_tg = create_input("Telegram ID");

        // Menu déroulant Telegram
        tg_dropdown = lv_dropdown_create(editor_overlay);
        lv_obj_set_size(tg_dropdown, lv_pct(90), 40);
        lv_obj_align(tg_dropdown, LV_ALIGN_TOP_MID, 0, 135); // Au même endroit que les inputs !
        lv_obj_set_style_bg_color(tg_dropdown, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_text_color(tg_dropdown, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_border_width(tg_dropdown, 0, 0);
        lv_obj_set_style_radius(tg_dropdown, 10, 0);
        lv_obj_add_event_cb(tg_dropdown, tg_dropdown_cb, LV_EVENT_VALUE_CHANGED, this);
        load_telegram_sources();

        // --- Boutons Précédent et Suivant ---
        btn_prev = lv_btn_create(editor_overlay);
        lv_obj_set_size(btn_prev, lv_pct(42), 42); 
        lv_obj_align(btn_prev, LV_ALIGN_TOP_LEFT, 16, 190); // Positionné juste sous le champ de texte
        lv_obj_set_style_bg_color(btn_prev, lv_color_hex(COL_CARD), 0);
        lv_obj_set_style_border_width(btn_prev, 0, 0);
        lv_obj_set_style_radius(btn_prev, 12, 0);
        lv_obj_add_event_cb(btn_prev, prev_step_cb, LV_EVENT_CLICKED, this);
        lv_obj_t* prev_lbl = lv_label_create(btn_prev);
        lv_label_set_text(prev_lbl, "Prev");
        lv_obj_set_style_text_color(prev_lbl, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(prev_lbl);

        btn_next = lv_btn_create(editor_overlay);
        lv_obj_set_size(btn_next, lv_pct(42), 42);
        lv_obj_align(btn_next, LV_ALIGN_TOP_RIGHT, -16, 190); // Aligné proprement sur la droite
        lv_obj_set_style_bg_color(btn_next, lv_color_hex(COL_ACCENT), 0);
        lv_obj_set_style_border_width(btn_next, 0, 0);
        lv_obj_set_style_radius(btn_next, 12, 0);
        lv_obj_add_event_cb(btn_next, next_step_cb, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(btn_next, save_contact_cb, LV_EVENT_LONG_PRESSED, this);
        btn_next_lbl = lv_label_create(btn_next);
        lv_label_set_text(btn_next_lbl, "Next");
        lv_obj_set_style_text_color(btn_next_lbl, lv_color_hex(COL_TEXT), 0);
        lv_obj_center(btn_next_lbl);

        // --- Clavier ---
        kb = lv_keyboard_create(editor_overlay);
        lv_obj_set_size(kb, lv_pct(100), 200);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, this);
        lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, this);
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

        // On cache le nécessaire pour le Step 0
        lv_obj_add_flag(tg_dropdown, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ta_nom, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ta_tel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ta_email, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ta_tg, LV_OBJ_FLAG_HIDDEN);
        
        refresh_list();
    }

    void update() override {}
};

#undef COL_BG
#undef COL_SURFACE
#undef COL_CARD
#undef COL_TEXT
#undef COL_SUB
#undef COL_ACCENT
#undef COL_OK

#endif