#ifndef FILE_TRANSFER_APP_H
#define FILE_TRANSFER_APP_H

#include "App.h"
#include "AppManager.h"
#include "../system/BasicRuntime.h"
#include <WiFi.h>
#include <LittleFS.h>

class FileTransferApp : public App {
private:
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* urlLabel = nullptr;
    lv_obj_t* filesLabel = nullptr;

    WiFiServer server = WiFiServer(80);
    bool running = false;
    volatile uint32_t uploadCount = 0;
    String lastMessage = "Serveur arrete";

    static String urlDecode(const String& in) {
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

    static String guessMime(const String& path) {
        if (path.endsWith(".html")) return "text/html";
        if (path.endsWith(".css")) return "text/css";
        if (path.endsWith(".js")) return "application/javascript";
        if (path.endsWith(".json")) return "application/json";
        if (path.endsWith(".txt") || path.endsWith(".bas")) return "text/plain";
        return "application/octet-stream";
    }

    static void go_home(lv_event_t* e) {
        (void)e;
        AppManager::switchTo(APP_HOME);
    }

    static void go_explorer(lv_event_t* e) {
        (void)e;
        AppManager::switchTo(APP_FILE_EXPLORER);
    }

    static void toggle_server(lv_event_t* e) {
        FileTransferApp* app = (FileTransferApp*)lv_event_get_user_data(e);
        if (!app) return;
        if (app->running) {
            app->stopServer();
        } else {
            app->startServer();
        }
    }

    String listAppFiles() {
        if (!LittleFS.exists("/apps")) return "(vide)";
        File dir = LittleFS.open("/apps", "r");
        if (!dir || !dir.isDirectory()) return "(vide)";
        String out;
        File f = dir.openNextFile();
        while (f) {
            out += String(f.name());
            out += "\n";
            f = dir.openNextFile();
        }
        if (out.isEmpty()) out = "(vide)";
        return out;
    }

    void updateUi() {
        if (!statusLabel || !urlLabel || !filesLabel) return;

        lv_label_set_text(statusLabel, lastMessage.c_str());
        if (WiFi.status() == WL_CONNECTED && running) {
            String url = "http://" + WiFi.localIP().toString() + "/";
            lv_label_set_text(urlLabel, url.c_str());
        } else {
            lv_label_set_text(urlLabel, "WiFi non connecte");
        }

        String files = "Fichiers /apps:\n" + listAppFiles();
        lv_label_set_text(filesLabel, files.c_str());
    }

    void startServer() {
        if (!basicfs::ensureMounted()) {
            lastMessage = "LittleFS indisponible";
            return;
        }
        if (!LittleFS.exists("/apps")) LittleFS.mkdir("/apps");

        server.begin();
        running = true;
        lastMessage = "Serveur actif (upload HTTP)";
    }

    void stopServer() {
        server.stop();
        running = false;
        lastMessage = "Serveur arrete";
    }

    static bool readLine(WiFiClient& client, String& line, uint32_t timeoutMs = 1500) {
        line = "";
        uint32_t start = millis();
        while (millis() - start < timeoutMs) {
            while (client.available()) {
                char c = (char)client.read();
                if (c == '\r') continue;
                if (c == '\n') return true;
                line += c;
            }
            delay(1);
        }
        return false;
    }

    static void sendResponse(WiFiClient& client, int code, const String& type, const String& body) {
        client.printf("HTTP/1.1 %d OK\r\n", code);
        client.printf("Content-Type: %s\r\n", type.c_str());
        client.printf("Content-Length: %u\r\n", (unsigned)body.length());
        client.print("Connection: close\r\n\r\n");
        client.print(body);
    }

    String getQueryParam(const String& path, const String& key) {
        int q = path.indexOf('?');
        if (q < 0) return "";
        String query = path.substring(q + 1);
        int from = 0;
        while (from < (int)query.length()) {
            int amp = query.indexOf('&', from);
            if (amp < 0) amp = query.length();
            String pair = query.substring(from, amp);
            int eq = pair.indexOf('=');
            if (eq > 0) {
                String k = pair.substring(0, eq);
                String v = pair.substring(eq + 1);
                if (k == key) return urlDecode(v);
            }
            from = amp + 1;
        }
        return "";
    }

    String normalizeUploadPath(const String& inName) {
        String name = inName;
        name.replace("\\", "_");
        name.replace("..", "_");
        while (name.startsWith("/")) name.remove(0, 1);
        if (name.isEmpty()) name = "upload.bas";
        return String("/apps/") + name;
    }

    void handleClient(WiFiClient& client) {
        String requestLine;
        if (!readLine(client, requestLine)) return;

        int s1 = requestLine.indexOf(' ');
        int s2 = requestLine.indexOf(' ', s1 + 1);
        if (s1 < 0 || s2 < 0) {
            sendResponse(client, 400, "text/plain", "Bad Request");
            return;
        }

        String method = requestLine.substring(0, s1);
        String path = requestLine.substring(s1 + 1, s2);

        int contentLength = 0;
        while (true) {
            String line;
            if (!readLine(client, line)) break;
            if (line.length() == 0) break;

            if (line.startsWith("Content-Length:")) {
                contentLength = line.substring(strlen("Content-Length:")).toInt();
            }
        }

        if (method == "GET" && (path == "/" || path.startsWith("/?"))) {
            String html =
                "<!doctype html><html><head><meta charset='utf-8'><title>FileTransfer</title></head><body>"
                "<h2>Upload BASIC file</h2>"
                "<input id='f' type='file'/>"
                "<button onclick='u()'>Upload</button>"
                "<pre id='o'></pre>"
                "<script>async function u(){const f=document.getElementById('f').files[0];"
                "if(!f){return;}const r=await fetch('/upload?name='+encodeURIComponent(f.name),{method:'POST',body:f});"
                "document.getElementById('o').textContent=await r.text();}</script>"
                "</body></html>";
            sendResponse(client, 200, "text/html", html);
            return;
        }

        if (method == "GET" && path.startsWith("/files")) {
            sendResponse(client, 200, "text/plain", listAppFiles());
            return;
        }

        if (method == "POST" && path.startsWith("/upload")) {
            if (contentLength <= 0) {
                sendResponse(client, 400, "text/plain", "Content-Length required");
                return;
            }

            String name = getQueryParam(path, "name");
            String filePath = normalizeUploadPath(name);
            File out = LittleFS.open(filePath, "w");
            if (!out) {
                sendResponse(client, 500, "text/plain", "Cannot open destination");
                return;
            }

            int remaining = contentLength;
            uint32_t start = millis();
            uint8_t buf[512];

            while (remaining > 0 && (millis() - start) < 15000) {
                int avail = client.available();
                if (avail <= 0) {
                    delay(1);
                    continue;
                }

                int chunk = avail;
                if (chunk > (int)sizeof(buf)) chunk = (int)sizeof(buf);
                if (chunk > remaining) chunk = remaining;
                int got = client.read(buf, chunk);
                if (got > 0) {
                    out.write(buf, (size_t)got);
                    remaining -= got;
                    start = millis();
                }
            }
            out.close();

            if (remaining == 0) {
                uploadCount++;
                lastMessage = "Upload OK: " + filePath;
                sendResponse(client, 200, "text/plain", "OK " + filePath);
            } else {
                lastMessage = "Upload incomplet";
                sendResponse(client, 408, "text/plain", "Timeout upload");
            }
            return;
        }

        if (method == "GET" && path.startsWith("/apps/")) {
            String fullPath = normalizeUploadPath(path.substring(strlen("/apps/")));
            File f = LittleFS.open(fullPath, "r");
            if (!f || f.isDirectory()) {
                sendResponse(client, 404, "text/plain", "Not found");
                return;
            }
            String body = f.readString();
            f.close();
            sendResponse(client, 200, guessMime(fullPath), body);
            return;
        }

        sendResponse(client, 404, "text/plain", "Not found");
    }

public:
    void start(lv_obj_t* parent) override {
        basicfs::ensureMounted();
        if (!LittleFS.exists("/apps")) LittleFS.mkdir("/apps");

        lv_obj_set_style_bg_color(parent, lv_color_hex(0x101014), 0);
        lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* header = lv_obj_create(parent);
        lv_obj_set_size(header, 320, 50);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(0x1f2330), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* backBtn = lv_btn_create(header);
        lv_obj_set_size(backBtn, 40, 40);
        lv_obj_align(backBtn, LV_ALIGN_LEFT_MID, -10, 0);
        lv_obj_add_event_cb(backBtn, go_home, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(lv_label_create(backBtn), LV_SYMBOL_LEFT);

        lv_obj_t* explorerBtn = lv_btn_create(header);
        lv_obj_set_size(explorerBtn, 40, 40);
        lv_obj_align(explorerBtn, LV_ALIGN_LEFT_MID, 36, 0);
        lv_obj_add_event_cb(explorerBtn, go_explorer, LV_EVENT_CLICKED, nullptr);
        lv_label_set_text(lv_label_create(explorerBtn), LV_SYMBOL_DIRECTORY);

        lv_obj_t* toggleBtn = lv_btn_create(header);
        lv_obj_set_size(toggleBtn, 40, 40);
        lv_obj_align(toggleBtn, LV_ALIGN_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(toggleBtn, toggle_server, LV_EVENT_CLICKED, this);
        lv_label_set_text(lv_label_create(toggleBtn), LV_SYMBOL_POWER);

        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "FileTransfer");
        lv_obj_set_style_text_color(title, lv_color_white(), 0);
        lv_obj_center(title);

        statusLabel = lv_label_create(parent);
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0x7fd1ff), 0);
        lv_obj_align(statusLabel, LV_ALIGN_TOP_LEFT, 10, 60);

        urlLabel = lv_label_create(parent);
        lv_obj_set_style_text_color(urlLabel, lv_color_hex(0xc0ffc0), 0);
        lv_obj_align(urlLabel, LV_ALIGN_TOP_LEFT, 10, 90);

        filesLabel = lv_label_create(parent);
        lv_obj_set_style_text_color(filesLabel, lv_color_hex(0xd0d0d0), 0);
        lv_obj_align(filesLabel, LV_ALIGN_TOP_LEFT, 10, 130);
        lv_obj_set_width(filesLabel, 300);

        startServer();
        updateUi();
    }

    void stop() override {
        stopServer();
    }

    void update() override {
        static uint32_t lastUi = 0;
        if (millis() - lastUi > 1000) {
            lastUi = millis();
            updateUi();
        }
    }

    void update1() override {
        if (!running || WiFi.status() != WL_CONNECTED) return;

        WiFiClient client = server.available();
        if (client) {
            handleClient(client);
            delay(2);
            client.stop();
        }
    }
};

#endif
