#ifndef SYSTEM_LTE_H
#define SYSTEM_LTE_H

#include <Arduino.h>
#include <sys/time.h>
#include "Settings.h"

// ─────────────────────────────────────────────────────────────
//  LTE – Gestionnaire du modem GSM/LTE (ex : A7670E)
//
//  TOGGLE 4G   → AT+CGATT (données uniquement, non-bloquant UI)
//  MODE AVION  → AT+CFUN  (radio, non-bloquant UI)
//
//  RÈGLE CRITIQUE : enable() / setAirplaneMode() posent juste
//  un flag. Les AT bloquants sont exécutés depuis Core 1 via
//  update(). Jamais d'AT dans le thread UI (Core 0).
// ─────────────────────────────────────────────────────────────
class LTE {
private:
    static bool   s_enabled;        // données mobiles ON/OFF
    static bool   s_airplane_mode;  // mode avion
    static int    signal_level;     // 0–4
    static String operator_name;

    // Commandes en attente (postées depuis Core 0, exécutées Core 1)
    static volatile int  s_pending_enable;   // -1=rien  0=disable  1=enable
    static volatile int  s_pending_airplane; // -1=rien  0=disable  1=enable

    // Interlock : mis à true par httpGetBlocking pour suspendre le polling
    static volatile bool s_http_busy;

    static unsigned long last_check;
    static int           state;
    static String        serial_buf;
    static unsigned long wait_start;
    static unsigned long last_time_sync;

    // ── Helper bloquant : envoie une commande AT et attend la réponse ──
    static String sendAT(const String& cmd, unsigned long timeout_ms = 1500) {
        while (Serial1.available()) Serial1.read();
        Serial1.println(cmd);
        unsigned long start = millis();
        String resp = "";
        while (millis() - start < timeout_ms) {
            while (Serial1.available()) resp += (char)Serial1.read();
            if (resp.indexOf("OK") != -1 || resp.indexOf("ERROR") != -1) break;
            delay(10);
        }
        return resp;
    }

    // ── Décodage MCC+MNC numérique → nom lisible ──
    static String decodeOperator(const String& num) {
        if (num == "20801" || num == "20802") return "Orange";
        if (num == "20810" || num == "20811" || num == "20813") return "SFR";
        if (num == "20815") return "Free Mobile";
        if (num == "20816") return "NRJ Mobile";
        if (num == "20820" || num == "20821" || num == "20888") return "Bouygues";
        if (num == "26201") return "Telekom";
        if (num == "26202") return "Vodafone DE";
        if (num == "23430") return "EE";
        if (num == "23415") return "Vodafone UK";
        if (num == "31410") return "AT&T";
        return num;
    }

public:
    // ── Initialisation (Core 1, boot) ──
    static void init() {
        s_enabled       = true;
        s_airplane_mode = false;
        signal_level    = 0;
        operator_name   = "Recherche...";
        state           = 0;
        serial_buf      = "";
        last_check      = millis();
        s_pending_enable   = -1;
        s_pending_airplane = -1;
        s_http_busy        = false;
        // Exécute en bloquant car on est AVANT que l'UI soit active
        sendAT("AT+COPS=3,0", 1000); // format nom long
        sendAT("AT+CTZU=1",   1000); // synchro NITZ auto
    }

    // ── APIs non-bloquantes : à appeler depuis Core 0 (boutons UI) ──
    // Le Core 1 exécute la commande AT lors du prochain update()
    static void enable(bool en)         { s_pending_enable   = en ? 1 : 0; }
    static void setAirplaneMode(bool en){ s_pending_airplane  = en ? 1 : 0; }

    static bool   isEnabled()      { return s_enabled; }
    static bool   isAirplaneMode() { return s_airplane_mode; }
    static int    getSignal()      { return signal_level; }
    static String getOperator()    { return operator_name; }

    // ── HTTP GET via AT (bloquant, Core 1) ──
    // Met s_http_busy à true pour suspendre update() pendant la requête.
    static String httpGetBlocking(const String& url, unsigned long timeout_ms = 15000) {
        if (!s_enabled || s_airplane_mode) return "";
        // Suspend le polling asynchrone + purge le buffer partagé
        s_http_busy = true;
        serial_buf  = "";
        state       = 0;

        sendAT("AT+HTTPTERM", 800);
        delay(200);
        String r = sendAT("AT+HTTPINIT", 2000);
        if (r.indexOf("ERROR") != -1) return "";
        r = sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 2000);
        if (r.indexOf("ERROR") != -1) { sendAT("AT+HTTPTERM", 500); return ""; }
        sendAT("AT+HTTPPARA=\"CID\",1", 1000);

        while (Serial1.available()) Serial1.read();
        Serial1.println("AT+HTTPACTION=0");

        unsigned long start  = millis();
        String action_resp   = "";
        int    http_code     = -1;
        int    data_len      = 0;
        while (millis() - start < timeout_ms) {
            while (Serial1.available()) action_resp += (char)Serial1.read();
            int idx = action_resp.indexOf("+HTTPACTION:");
            if (idx != -1) {
                String part = action_resp.substring(idx + 12);
                int c1 = part.indexOf(',');
                int c2 = part.indexOf(',', c1 + 1);
                if (c1 > 0 && c2 > c1) {
                    http_code = part.substring(c1 + 1, c2).toInt();
                    data_len  = part.substring(c2 + 1).toInt();
                }
                break;
            }
            delay(50);
        }
        if (http_code != 200 || data_len <= 0) { sendAT("AT+HTTPTERM", 500); return ""; }

        int read_len = min(data_len, 4096);
        r = sendAT("AT+HTTPREAD=0," + String(read_len), 6000);
        sendAT("AT+HTTPTERM", 500);

        // Remet le polling en état propre
        serial_buf  = "";
        last_check  = millis(); // évite un poll immédiat après
        s_http_busy = false;

        int body_start = r.indexOf("+HTTPREAD:");
        if (body_start != -1) {
            body_start = r.indexOf('\n', body_start) + 1;
            int body_end = r.lastIndexOf("\r\nOK");
            if (body_end < 0) body_end = r.lastIndexOf("OK");
            if (body_start > 0 && body_end > body_start)
                return r.substring(body_start, body_end);
        }
        return r;
    }

    // ── Boucle asynchrone (Core 1, appelez depuis loop1()) ──
    // États : 0=idle  1=attente COPS  2=attente CSQ  3=attente CCLK
    static void update() {
        // 1. Si une requête HTTP est en cours, vider le buffer et ne rien faire
        if (s_http_busy) {
            while (Serial1.available()) Serial1.read();
            serial_buf = "";
            return;
        }

        // 2. Commandes en attente (postées par Core 0 via les boutons)
        if (s_pending_airplane != -1) {
            int cmd = s_pending_airplane;
            s_pending_airplane = -1;
            serial_buf = "";
            state      = 0;
            if (cmd == 1) {
                sendAT("AT+CFUN=4", 4000);
                s_airplane_mode = true;
                s_enabled       = false;
                signal_level    = 0;
                operator_name   = "Mode Avion";
            } else {
                sendAT("AT+CFUN=1", 4000);
                s_airplane_mode = false;
                s_enabled       = true;
                operator_name   = "Recherche...";
                last_check      = 0;
            }
            serial_buf = "";
            return;
        }
        if (s_pending_enable != -1) {
            int cmd = s_pending_enable;
            s_pending_enable = -1;
            serial_buf = "";
            state      = 0;
            if (cmd == 1) {
                sendAT("AT+CGATT=1", 3000);
                s_enabled     = true;
                operator_name = "Recherche...";
                last_check    = 0;
            } else {
                sendAT("AT+CGATT=0", 3000);
                s_enabled     = false;
                operator_name = "Données OFF";
                signal_level  = 0;
            }
            serial_buf = "";
            return;
        }

        // 3. Mode avion : rien à faire
        if (s_airplane_mode) return;

        // 4. Machine à états polling asynchrone
        while (Serial1.available()) serial_buf += (char)Serial1.read();

        if (state == 0) {
            if (millis() - last_check > 5000) {
                serial_buf = "";
                Serial1.println("AT+COPS?");
                state      = 1;
                wait_start = millis();
            }
        }
        else if (state == 1) { // Attente réponse opérateur
            if (serial_buf.indexOf("OK")    != -1 ||
                serial_buf.indexOf("ERROR") != -1 ||
                millis() - wait_start > 1800) {

                if (!s_enabled) {
                    operator_name = "Données OFF";
                } else {
                    int q1 = serial_buf.indexOf('"');
                    if (q1 != -1) {
                        int q2 = serial_buf.indexOf('"', q1 + 1);
                        if (q2 != -1) {
                            String raw = serial_buf.substring(q1 + 1, q2);
                            bool is_num = (raw.length() >= 5 && raw.length() <= 6);
                            for (size_t i = 0; i < raw.length() && is_num; i++)
                                if (!isDigit(raw[i])) is_num = false;
                            operator_name = is_num ? decodeOperator(raw) : raw;
                        }
                    } else if (serial_buf.indexOf("OK") != -1) {
                        operator_name = "Aucun service";
                    }
                }
                serial_buf = "";
                Serial1.println("AT+CSQ");
                state      = 2;
                wait_start = millis();
            }
        }
        else if (state == 2) { // Attente qualité signal
            if (serial_buf.indexOf("OK")    != -1 ||
                serial_buf.indexOf("ERROR") != -1 ||
                millis() - wait_start > 1800) {

                if (!s_enabled) {
                    signal_level = 0;
                } else {
                    int col = serial_buf.indexOf("+CSQ:");
                    if (col != -1) {
                        int comma = serial_buf.indexOf(',', col);
                        if (comma != -1) {
                            int val = serial_buf.substring(col + 5, comma).toInt();
                            if      (val == 99) signal_level = 0;
                            else if (val < 10)  signal_level = 1;
                            else if (val < 15)  signal_level = 2;
                            else if (val < 20)  signal_level = 3;
                            else                signal_level = 4;
                        }
                    }
                }

                // Vérifie si une synchro d'heure est nécessaire
                bool time_missing = (time(nullptr) < 1000000000L);
                bool time_stale   = (millis() - last_time_sync > 30UL * 60UL * 1000UL);
                if (s_enabled && (time_missing || time_stale)) {
                    serial_buf = "";
                    Serial1.println("AT+CCLK?");
                    state      = 3;
                    wait_start = millis();
                } else {
                    state      = 0;
                    last_check = millis();
                }
            }
        }
        else if (state == 3) { // Synchro heure via AT+CCLK?
            if (serial_buf.indexOf("OK")    != -1 ||
                serial_buf.indexOf("ERROR") != -1 ||
                millis() - wait_start > 2000) {

                // +CCLK: "yy/MM/dd,HH:mm:ss±tz"
                int q1 = serial_buf.indexOf('"');
                int q2 = serial_buf.lastIndexOf('"');
                if (q1 >= 0 && q2 > q1 + 16) {
                    String ts = serial_buf.substring(q1 + 1, q2);
                    if (ts.length() >= 17) {
                        int yy = ts.substring(0,  2).toInt();
                        int mo = ts.substring(3,  5).toInt();
                        int dd = ts.substring(6,  8).toInt();
                        int hh = ts.substring(9,  11).toInt();
                        int mm = ts.substring(12, 14).toInt();
                        int ss = ts.substring(15, 17).toInt();
                        int tz_offset = 0;
                        if (ts.length() > 18) {
                            char sign = ts[17];
                            int tz_q  = ts.substring(18).toInt();
                            tz_offset = tz_q * 15 * 60;
                            if (sign == '-') tz_offset = -tz_offset;
                        }
                        struct tm t = {};
                        t.tm_year  = (2000 + yy) - 1900;
                        t.tm_mon   = mo - 1;
                        t.tm_mday  = dd;
                        t.tm_hour  = hh;
                        t.tm_min   = mm;
                        t.tm_sec   = ss;
                        time_t epoch = mktime(&t);
                        if (epoch != (time_t)-1) {
                            epoch -= tz_offset;
                            struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
                            settimeofday(&tv, nullptr);
                            last_time_sync = millis();
                        }
                    }
                }
                serial_buf = "";
                state      = 0;
                last_check = millis();
            }
        }
    }
};

// ── Initialisation des membres statiques ──
bool          LTE::s_enabled        = true;
bool          LTE::s_airplane_mode  = false;
int           LTE::signal_level     = 0;
String        LTE::operator_name    = "Init...";
volatile int  LTE::s_pending_enable   = -1;
volatile int  LTE::s_pending_airplane = -1;
volatile bool LTE::s_http_busy        = false;
unsigned long LTE::last_check       = 0;
int           LTE::state            = 0;
String        LTE::serial_buf       = "";
unsigned long LTE::wait_start       = 0;
unsigned long LTE::last_time_sync   = 0;

#endif