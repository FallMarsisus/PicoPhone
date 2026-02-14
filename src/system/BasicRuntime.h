#ifndef SYSTEM_BASIC_RUNTIME_H
#define SYSTEM_BASIC_RUNTIME_H

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <lvgl.h>

#include "../Hardware.h"

extern "C" {
#include "../plugins/my_basic.h"
}

namespace basicfs {

static bool ensureMounted() {
    if (LittleFS.begin()) return true;
    // Try a few times with small delays instead of formatting automatically.
    for (int i = 0; i < 3; ++i) {
        delay(100);
        if (LittleFS.begin()) return true;
    }
    return false;
}

static String normalizePath(const String& inPath) {
    if (inPath.isEmpty()) return String("/");
    if (inPath[0] == '/') return inPath;
    return String("/") + inPath;
}

static void ensureDefaultScripts() {
    if (!ensureMounted()) return;
    if (!LittleFS.exists("/apps")) {
        Serial.println("[BASICFS] creating /apps directory");
        LittleFS.mkdir("/apps");
    }

    if (!LittleFS.exists("/apps/test_ui.bas")) {
        Serial.println("[BASICFS] writing /apps/test_ui.bas");
        File f = LittleFS.open("/apps/test_ui.bas", "w");
        if (f) {
            f.print(
                "UI_CLS()\n"
                "UI_RECT(8, 8, 304, 410, 0x1E1E1E)\n"
                "UI_LABEL(\"MY-BASIC + LVGL\", 16, 20)\n"
                "UI_LABEL(\"HTTP status:\", 16, 55)\n"
                "status = NET_HTTP_GET(\"http://example.com\")\n"
                "UI_LABEL(\"Code: \" + STR(NET_HTTP_STATUS()), 16, 80)\n"
                "FS_WRITE(\"/apps/last_http.txt\", status)\n"
                "UI_LABEL(\"Touchez l'ecran pour lire X/Y\", 16, 130)\n"
                "x = TOUCH_X()\n"
                "y = TOUCH_Y()\n"
                "UI_LABEL(\"Touch: \" + STR(x) + \",\" + STR(y), 16, 155)\n"
                "UI_LABEL(\"Fichiers /apps:\", 16, 200)\n"
                "UI_LABEL(FS_LIST(\"/apps\"), 16, 225)\n"
                "END\n");
            f.close();
            Serial.println("[BASICFS] /apps/test_ui.bas written");
        } else {
            Serial.println("[BASICFS] FAILED to open /apps/test_ui.bas for writing");
        }
    } else {
        Serial.println("[BASICFS] /apps/test_ui.bas already exists");
    }
}

} // namespace basicfs

class BasicRuntime {
private:
    mb_interpreter_t* engine = nullptr;
    lv_obj_t* uiRoot = nullptr;
    int lastHttpCode = 0;
    String lastError;
    String lastHttpBody;

    static BasicRuntime* self;

    static String decodeUrl(const String& in) {
        String out;
        out.reserve(in.length());
        for (size_t i = 0; i < in.length(); ++i) {
            char c = in[i];
            if (c == '+') {
                out += ' ';
            } else if (c == '%' && i + 2 < in.length()) {
                char hex[3] = { in[i + 1], in[i + 2], 0 };
                out += (char)strtol(hex, nullptr, 16);
                i += 2;
            } else {
                out += c;
            }
        }
        return out;
    }

    static int getTouchX() {
        uint16_t x = 0, y = 0, z = 0;
        touch_read_spi_sdk(x, y, z);
        return (int)x;
    }

    static int getTouchY() {
        uint16_t x = 0, y = 0, z = 0;
        touch_read_spi_sdk(x, y, z);
        return (int)y;
    }

    static int getTouchPressed() {
        uint16_t x = 0, y = 0, z = 0;
        touch_read_spi_sdk(x, y, z);
        return (z > 200) ? 1 : 0;
    }

    static String listFiles(const String& inPath) {
        String path = basicfs::normalizePath(inPath);
        File d = LittleFS.open(path, "r");
        if (!d || !d.isDirectory()) return String("");

        String out;
        File f = d.openNextFile();
        while (f) {
            out += String(f.name());
            if (f.isDirectory()) out += "/";
            out += "\n";
            f = d.openNextFile();
        }
        return out;
    }

    static void onError(mb_interpreter_t* s, mb_error_e err, const char* file, const char* func, int pos, unsigned short row, unsigned short col, int ret) {
        (void)s;
        (void)file;
        (void)func;
        (void)pos;
        (void)row;
        (void)col;
        (void)ret;
        if (self) {
            const char* desc = mb_get_error_desc(err);
            self->lastError = desc ? desc : "BASIC runtime error";
        }
    }

    static int fn_cls(mb_interpreter_t* s, void** l) {
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_attempt_func_end(s, l));
        if (self && self->uiRoot) lv_obj_clean(self->uiRoot);
        return MB_FUNC_OK;
    }

    static int fn_label(mb_interpreter_t* s, void** l) {
        char* text = nullptr;
        int_t x = 0;
        int_t y = 0;

        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_string(s, l, &text));
        mb_check(mb_pop_int(s, l, &x));
        mb_check(mb_pop_int(s, l, &y));
        mb_check(mb_attempt_func_end(s, l));

        if (self && self->uiRoot) {
            lv_obj_t* lbl = lv_label_create(self->uiRoot);
            lv_label_set_text(lbl, text ? text : "");
            lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, (lv_coord_t)x, (lv_coord_t)y);
            lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        }

        mb_check(mb_push_int(s, l, 1));
        return MB_FUNC_OK;
    }

    static int fn_rect(mb_interpreter_t* s, void** l) {
        int_t x = 0, y = 0, w = 0, h = 0, rgb = 0;

        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_int(s, l, &x));
        mb_check(mb_pop_int(s, l, &y));
        mb_check(mb_pop_int(s, l, &w));
        mb_check(mb_pop_int(s, l, &h));
        mb_check(mb_pop_int(s, l, &rgb));
        mb_check(mb_attempt_func_end(s, l));

        if (self && self->uiRoot) {
            lv_obj_t* box = lv_obj_create(self->uiRoot);
            lv_obj_set_size(box, (lv_coord_t)w, (lv_coord_t)h);
            lv_obj_align(box, LV_ALIGN_TOP_LEFT, (lv_coord_t)x, (lv_coord_t)y);
            lv_obj_set_style_bg_color(box, lv_color_hex((uint32_t)rgb), 0);
            lv_obj_set_style_border_width(box, 0, 0);
            lv_obj_set_style_radius(box, 10, 0);
            lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        }

        mb_check(mb_push_int(s, l, 1));
        return MB_FUNC_OK;
    }

    static int fn_label_i(mb_interpreter_t* s, void** l) {
        int_t val = 0;
        int_t x = 0;
        int_t y = 0;

        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_int(s, l, &val));
        mb_check(mb_pop_int(s, l, &x));
        mb_check(mb_pop_int(s, l, &y));
        mb_check(mb_attempt_func_end(s, l));

        if (self && self->uiRoot) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%ld", (long)val);
            lv_obj_t* lbl = lv_label_create(self->uiRoot);
            lv_label_set_text(lbl, buf);
            lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, (lv_coord_t)x, (lv_coord_t)y);
            lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        }

        mb_check(mb_push_int(s, l, 1));
        return MB_FUNC_OK;
    }

    static int fn_touch_x(mb_interpreter_t* s, void** l) {
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_attempt_func_end(s, l));
        mb_check(mb_push_int(s, l, getTouchX()));
        return MB_FUNC_OK;
    }

    static int fn_touch_y(mb_interpreter_t* s, void** l) {
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_attempt_func_end(s, l));
        mb_check(mb_push_int(s, l, getTouchY()));
        return MB_FUNC_OK;
    }

    static int fn_touch_pressed(mb_interpreter_t* s, void** l) {
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_attempt_func_end(s, l));
        mb_check(mb_push_int(s, l, getTouchPressed()));
        return MB_FUNC_OK;
    }

    static int fn_sleep(mb_interpreter_t* s, void** l) {
        int_t ms = 0;
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_int(s, l, &ms));
        mb_check(mb_attempt_func_end(s, l));
        if (ms > 0) delay((uint32_t)ms);
        mb_check(mb_push_int(s, l, 1));
        return MB_FUNC_OK;
    }

    static int fn_http_get(mb_interpreter_t* s, void** l) {
        char* url = nullptr;
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_string(s, l, &url));
        mb_check(mb_attempt_func_end(s, l));

        if (!self) {
            mb_check(mb_push_string(s, l, (char*)""));
            return MB_FUNC_OK;
        }

        self->lastHttpBody = "";
        self->lastHttpCode = -1;

        if (WiFi.status() == WL_CONNECTED && url && *url) {
            HTTPClient http;
            http.setTimeout(8000);
            if (http.begin(url)) {
                int code = http.GET();
                self->lastHttpCode = code;
                if (code > 0) {
                    self->lastHttpBody = http.getString();
                }
                http.end();
            }
        }

        mb_check(mb_push_string(s, l, (char*)self->lastHttpBody.c_str()));
        return MB_FUNC_OK;
    }

    static int fn_http_status(mb_interpreter_t* s, void** l) {
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_attempt_func_end(s, l));
        int_t code = self ? (int_t)self->lastHttpCode : (int_t)-1;
        mb_check(mb_push_int(s, l, code));
        return MB_FUNC_OK;
    }

    static int fn_read_file(mb_interpreter_t* s, void** l) {
        char* path = nullptr;
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_string(s, l, &path));
        mb_check(mb_attempt_func_end(s, l));

        String content;
        if (path && basicfs::ensureMounted()) {
            String normalized = basicfs::normalizePath(path);
            File f = LittleFS.open(normalized, "r");
            if (f && !f.isDirectory()) {
                content = f.readString();
                f.close();
            }
        }

        mb_check(mb_push_string(s, l, (char*)content.c_str()));
        return MB_FUNC_OK;
    }

    static int fn_write_file(mb_interpreter_t* s, void** l) {
        char* path = nullptr;
        char* content = nullptr;
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_string(s, l, &path));
        mb_check(mb_pop_string(s, l, &content));
        mb_check(mb_attempt_func_end(s, l));

        int_t ok = 0;
        if (path && basicfs::ensureMounted()) {
            String normalized = basicfs::normalizePath(path);
            int slash = normalized.lastIndexOf('/');
            if (slash > 0) {
                String dir = normalized.substring(0, slash);
                if (!dir.isEmpty() && !LittleFS.exists(dir)) {
                    LittleFS.mkdir(dir);
                }
            }

            File f = LittleFS.open(normalized, "w");
            if (f) {
                if (content) f.print(content);
                f.close();
                ok = 1;
            }
        }

        mb_check(mb_push_int(s, l, ok));
        return MB_FUNC_OK;
    }

    static int fn_list_files(mb_interpreter_t* s, void** l) {
        char* path = nullptr;
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_string(s, l, &path));
        mb_check(mb_attempt_func_end(s, l));

        String p = path ? String(path) : String("/");
        String files = basicfs::ensureMounted() ? listFiles(p) : String("");
        mb_check(mb_push_string(s, l, (char*)files.c_str()));
        return MB_FUNC_OK;
    }

    static int fn_url_decode(mb_interpreter_t* s, void** l) {
        char* in = nullptr;
        mb_check(mb_attempt_func_begin(s, l));
        mb_check(mb_pop_string(s, l, &in));
        mb_check(mb_attempt_func_end(s, l));

        String decoded = decodeUrl(in ? String(in) : String(""));
        mb_check(mb_push_string(s, l, (char*)decoded.c_str()));
        return MB_FUNC_OK;
    }

public:
    bool begin(lv_obj_t* root) {
        uiRoot = root;
        lastError = "";
        lastHttpBody = "";
        lastHttpCode = 0;

        if (!basicfs::ensureMounted()) {
            lastError = "LittleFS init failed";
            Serial.println("[BASICRT] LittleFS init failed");
            return false;
        }

        basicfs::ensureDefaultScripts();

        Serial.println("[BASICRT] opening my_basic engine");
        int rc = mb_open(&engine);
        Serial.printf("[BASICRT] mb_open rc=%d engine=%p\n", rc, engine);
        if (rc != MB_FUNC_OK || !engine) {
            lastError = "mb_open failed";
            return false;
        }

        self = this;
        mb_set_error_handler(engine, onError);

        mb_register_func(engine, "UI_CLS", fn_cls);
        mb_register_func(engine, "UI_LABEL", fn_label);
        mb_register_func(engine, "UI_RECT", fn_rect);
        mb_register_func(engine, "UI_LABEL_I", fn_label_i);
        mb_register_func(engine, "TOUCH_X", fn_touch_x);
        mb_register_func(engine, "TOUCH_Y", fn_touch_y);
        mb_register_func(engine, "TOUCH_PRESSED", fn_touch_pressed);
        mb_register_func(engine, "UI_SLEEP", fn_sleep);
        mb_register_func(engine, "NET_HTTP_GET", fn_http_get);
        mb_register_func(engine, "NET_HTTP_STATUS", fn_http_status);
        mb_register_func(engine, "FS_READ", fn_read_file);
        mb_register_func(engine, "FS_WRITE", fn_write_file);
        mb_register_func(engine, "FS_LIST", fn_list_files);
        mb_register_func(engine, "URL_DECODE", fn_url_decode);

        Serial.println("[BASICRT] registered my_basic functions");

        return true;
    }

    void end() {
        if (engine) {
            mb_close(&engine);
            engine = nullptr;
        }
        self = nullptr;
    }

    bool runText(const String& script) {
        if (!engine) {
            lastError = "Runtime not initialized";
            return false;
        }

        if (mb_load_string(engine, script.c_str(), true) != MB_FUNC_OK) {
            if (lastError.isEmpty()) lastError = "mb_load_string failed";
            return false;
        }

        int rc = mb_run(engine, true);
        if (rc != MB_FUNC_OK && rc != MB_FUNC_END && rc != MB_FUNC_BYE) {
            if (lastError.isEmpty()) lastError = "mb_run failed";
            return false;
        }

        return true;
    }

    bool runFile(const String& inPath) {
        if (!basicfs::ensureMounted()) {
            lastError = "LittleFS unavailable";
            return false;
        }

        String path = basicfs::normalizePath(inPath);
        Serial.printf("[BASICRT] runFile path='%s'\n", path.c_str());
        if (!LittleFS.exists(path)) {
            Serial.println("[BASICRT] file does not exist");
            lastError = "Script introuvable";
            return false;
        }
        File f = LittleFS.open(path, "r");
        if (!f || f.isDirectory()) {
            Serial.println("[BASICRT] failed to open file or it's a directory");
            lastError = "Script introuvable";
            return false;
        }
        Serial.printf("[BASICRT] file size=%u\n", (unsigned)f.size());

        String script = f.readString();
        f.close();
        return runText(script);
    }

    const String& error() const {
        return lastError;
    }
};

BasicRuntime* BasicRuntime::self = nullptr;

#endif
