#ifndef SYSTEM_LTE_H
#define SYSTEM_LTE_H

#include <Arduino.h>
#include <sys/time.h>
#include <hardware/watchdog.h>
#include "Settings.h"
#include "Logger.h"

// RP2350 GPIO: modem power key
#define A7670_PWRKEY 2

// ═══════════════════════════════════════════════════════════════════════════
//  LTE – Gestionnaire EXCLUSIF du modem GSM/LTE (A7670E / SIM7670E)
//  + Gestion robuste des erreurs réseau avec notifications
// ═══════════════════════════════════════════════════════════════════════════

// Codes d'erreur réseau
enum class NetworkErrorCode : uint8_t {
    NO_ERROR = 0,
    MODEM_NOT_RESPONDING = 1,
    NO_SIGNAL = 2,
    HTTP_TIMEOUT = 3,
    HTTP_NETWORK_ERROR = 4,
    HTTP_INVALID_RESPONSE = 5,
    SMS_SEND_FAILED = 6,
    TIME_SYNC_FAILED = 7,
    MODEM_CRASH = 8,
    REGISTRATION_FAILED = 9,
    DATA_CONNECTION_FAILED = 10,
    RECOVERY_IN_PROGRESS = 11,
};

class LTE {
private:
    // ── État machine ──
    static bool          s_enabled;
    static bool          s_airplane_mode;
    static int           signal_level;
    static String        operator_name;

    static volatile int  s_pending_enable;
    static volatile int  s_pending_airplane;

    static volatile bool s_modem_confirmed;
    static volatile bool s_http_busy;

    static uint8_t       s_poll_fail_count;
    static uint8_t       s_recovery_fail_count;
    static unsigned long s_recovery_disabled_until;

    // ── Gestion des erreurs réseau ──
    static NetworkErrorCode s_last_error;
    static unsigned long s_last_error_time;
    static uint8_t       s_error_notify_count;
    static unsigned long s_last_error_notify_time;

    static unsigned long last_check;
    static int           state;
    static unsigned long wait_start;
    static unsigned long last_time_sync;
    static unsigned long next_time_sync_try;
    static uint8_t       time_sync_fail_count;
    static unsigned long s_last_recover_ms;
    static unsigned long s_last_operator_poll;
    static bool          s_registered;

    // ── Parsing ligne à ligne ──
    static String        s_line_buf;
    static String        s_cmd_resp;

    // ── Queue SMS entrants ──
    struct SmsEnvelope {
        char  number[32];
        char  text[281];
        long  timestamp;
    };
    static constexpr uint8_t SMS_Q_SIZE = 4;
    static SmsEnvelope       s_sms_q[SMS_Q_SIZE];
    static volatile uint8_t  s_sms_q_head;
    static volatile uint8_t  s_sms_q_tail;

    // ── État inline réception SMS spontané ──
    static bool          s_sms_rx_active;
    static String        s_sms_rx_number;
    static String        s_sms_rx_idx;
    static String        s_sms_rx_text;
    static unsigned long s_sms_rx_timeout;

    // ── Queue SMS sortants ──
    struct SmsSendReq { char number[32]; char text[256]; };
    static SmsSendReq       s_sms_send_req;
    static volatile bool    s_sms_send_pending;
    static volatile bool    s_sms_send_result;

    // ── CMTI différé : évite les appels sendAT() récursifs ──
    // Petite queue pour ne pas perdre les SMS si plusieurs arrivent simultanément
    static constexpr uint8_t CMTI_Q_SIZE = 4;
    static int8_t            s_cmti_q[CMTI_Q_SIZE]; // -1 = vide
    static uint8_t           s_cmti_q_head;
    static uint8_t           s_cmti_q_tail;

    // ── Helpers queue CMTI ──
    static void pushCmtiIdx(int idx) {
        uint8_t next = (s_cmti_q_tail + 1) % CMTI_Q_SIZE;
        if (next == s_cmti_q_head) return; // queue pleine, index perdu
        s_cmti_q[s_cmti_q_tail] = (int8_t)idx;
        s_cmti_q_tail = next;
    }
    static bool popCmtiIdx(int& out) {
        if (s_cmti_q_head == s_cmti_q_tail) return false;
        out = s_cmti_q[s_cmti_q_head];
        s_cmti_q_head = (s_cmti_q_head + 1) % CMTI_Q_SIZE;
        return true;
    }

    // ── Helper bloquant SÉCURISÉ : envoie une commande AT (Core 1) ──────────
    static String sendAT(const String& cmd, unsigned long timeout_ms = 1500) {
        // 🔥 PURGE STRICTE AVANT COMMANDE
        unsigned long purge_start = millis();
        while (millis() - purge_start < 50) {
            while (Serial1.available()) Serial1.read();
        }
        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n') {
                s_line_buf.trim();
                if (s_line_buf.length() > 0) {
                    if (s_line_buf.startsWith("+CMTI:")) {
                        // Ne pas appeler dispatchLine/sendAT ici (récursion) :
                        // on extrait l'index et on l'enfile pour traitement ultérieur.
                        int comma = s_line_buf.indexOf(',', 6);
                        if (comma != -1) {
                            String idx = s_line_buf.substring(comma + 1); idx.trim();
                            pushCmtiIdx(idx.toInt());
                        }
                    } else {
                        dispatchLine(s_line_buf);
                    }
                }
                s_line_buf = "";
            } else if (c != '\r') {
                s_line_buf += c;
            }
        }
        
        // --- MODIFICATION ICI : Électrochoc de réveil à 50ms ---
        Serial1.print('\r');
        delay(50); // 20ms c'est souvent trop court pour le A7670E !
        
        Logger::printf("[LTE->GSM] %s\n", cmd.c_str());
        Serial1.println(cmd);
        
        unsigned long start = millis();
        String resp = "";
        String current_line = "";
        
        while (millis() - start < timeout_ms) {
            watchdog_update();
            while (Serial1.available()) {
                char c = Serial1.read();
                resp += c;
                if (c == '\n') {
                    current_line.trim();
                    if (current_line.startsWith("+CMT") || current_line.startsWith("+CGEV") || current_line.startsWith("+CREG")) {
                        dispatchLine(current_line);
                    }
                    current_line = "";
                } else if (c != '\r') {
                    current_line += c;
                }
            }
            if (resp.endsWith("OK\r\n") || resp.endsWith("ERROR\r\n")) {
                break;
            }
            delay(10);
        }
        
        if (resp.length() > 0) {
            String log = resp; log.replace("\r\n", " | "); log.trim();
            Logger::printf("[GSM->LTE] %s\n", log.c_str());
        } else {
            Logger::printf("[LTE] TIMEOUT: %s\n", cmd.c_str());
        }
        return resp;
    }

    static String resolveApnForOperator(const String& op) {
        if (op.indexOf("Orange") != -1 || op.indexOf("Sosh") != -1) return "orange";
        if (op.indexOf("SFR") != -1 || op.indexOf("RED") != -1) return "sl2sfr";
        if (op.indexOf("Bouygues") != -1 || op.indexOf("B&You") != -1) return "mmsbouygtel.com";
        if (op.indexOf("Free") != -1) return "free";
        return "internet";
    }

    static bool preparePacketDataContext() {
        if (!s_modem_confirmed) return false;

        // VERIFICATION RAPIDE : Si on a déjà une IP, on ne refait pas toute la connexion !
        String paddr = sendAT("AT+CGPADDR=1", 1000);
        if (paddr.indexOf("0.0.0.0") == -1 && paddr.indexOf("ERROR") == -1 && paddr.indexOf("+CGPADDR: 1,") != -1) {
            Logger::println("[LTE PDP] Contexte data déjà actif, on continue sans reconnexion.");
            return true;
        }

        Logger::println("[LTE PDP] Activation du contexte data (Auto-APN)...");
        sendAT("AT+CGATT=1", 4000);
        sendAT("AT+CGACT=1,1", 5000);
        
        paddr = sendAT("AT+CGPADDR=1", 3000);
        if (paddr.indexOf("0.0.0.0") == -1 && paddr.indexOf("ERROR") == -1 && paddr.indexOf("+CGPADDR: 1,") != -1) {
            Logger::println("[LTE PDP] Contexte data actif avec succès ! IP reçue.");
            return true;
        }

        Logger::println("[LTE PDP] Echec de l'activation data.");
        return false;
    }
    static bool applyTimeFromCclkResponse(const String& resp, const char* source_tag) {
        int q1 = resp.indexOf('"');
        int q2 = resp.lastIndexOf('"');
        if (q1 < 0 || q2 <= q1 + 16) {
            Logger::printf("[LTE CCLK] %s: timestamp introuvable\n", source_tag ? source_tag : "?");
            return false;
        }

        String ts = resp.substring(q1 + 1, q2);
        if (ts.length() < 17) return false;

        int yy = ts.substring(0, 2).toInt();
        if (yy < 24) {
            Logger::printf("[LTE CCLK] SKIP: année invalide=%d (NITZ en attente)\n", 2000 + yy);
            return false;
        }

        int mo = ts.substring(3, 5).toInt();
        int dd = ts.substring(6, 8).toInt();
        int hh = ts.substring(9, 11).toInt();
        int mm = ts.substring(12, 14).toInt();
        int ss = ts.substring(15, 17).toInt();

        int tz_sec = 0;
        if (ts.length() > 17) {
            char sign = ts[17];
            int tz_q = ts.substring(18).toInt();
            tz_sec = tz_q * 15 * 60;
            if (sign == '-') tz_sec = -tz_sec;
        }

        struct tm t = {};
        t.tm_year = (2000 + yy) - 1900;
        t.tm_mon = mo - 1;
        t.tm_mday = dd;
        t.tm_hour = hh;
        t.tm_min = mm;
        t.tm_sec = ss;
        t.tm_isdst = -1;

        time_t epoch = mktime(&t);
        if (epoch == (time_t)-1) return false;

        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
        last_time_sync = millis();
        time_sync_fail_count = 0;
        next_time_sync_try = millis() + 30UL * 60UL * 1000UL;
        
        Logger::printf("[LTE CCLK] %s OK epoch=%lu\n", source_tag ? source_tag : "?", (unsigned long)epoch);
        return true;
    }

    static void recoverModemIfNeeded(const char* reason) {
        unsigned long now = millis();
        
        // Si trop de crash consécutifs, on désactive le modem pendant 5 min
        if (s_recovery_fail_count >= 5) {
            if (now < s_recovery_disabled_until) {
                Logger::printf("[LTE CRASH] Modem désactivé (trop de crash). Réessai en %lu ms\n", 
                    s_recovery_disabled_until - now);
                s_enabled = false;
                return;
            }
            s_recovery_fail_count = 0;
            s_enabled = true;
            Logger::println("[LTE CRASH] Modem réactivé après repos");
        }
        
        // On évite les recovery en boucle rapide (30s minimum)
        if (now - s_last_recover_ms < 30000UL && s_last_recover_ms != 0) return; 
        
        Logger::printf("[LTE] RECOVERY: %s (attempt %d/5)\n", reason ? reason : "(unknown)", s_recovery_fail_count + 1);
        logNetworkError(NetworkErrorCode::RECOVERY_IN_PROGRESS, "4G/LTE", reason);

        unsigned long recovery_start = millis();
        const unsigned long RECOVERY_TIMEOUT = 60000UL; // 60 sec max

        s_modem_confirmed = false;
        s_line_buf = "";
        s_cmd_resp = "";
        state = 0;

        // ─── HARD POWER CYCLE VIA PWRKEY ───
        Logger::println("[LTE] RECOVERY: Hard power-cycle du modem via PWRKEY...");
        pinMode(A7670_PWRKEY, OUTPUT);
        digitalWrite(A7670_PWRKEY, LOW);
        delay(100);
        digitalWrite(A7670_PWRKEY, HIGH);  
        Logger::println("[LTE] PWRKEY pullé à HIGH... attente 2000ms");
        
        // Boucle non-bloquante pour le Watchdog (20 x 100ms = 2 secondes)
        for(int d = 0; d < 20; d++) { delay(100); watchdog_update(); }
        
        digitalWrite(A7670_PWRKEY, LOW);   
        Logger::println("[LTE] PWRKEY relâché (LOW)");
        
        // Boucle non-bloquante pour le Watchdog (30 x 100ms = 3 secondes)
        for(int d = 0; d < 30; d++) { delay(100); watchdog_update(); }
        
        // ─── ATTENDRE LE CONTACT INITIAL ───
        Logger::println("[LTE] Attente du réveil du modem...");
        bool contact = false;
        for (int i = 0; i < 10 && (millis() - recovery_start < RECOVERY_TIMEOUT); i++) {
            watchdog_update();
            while (Serial1.available()) Serial1.read();
            Serial1.print('\r'); delay(50);
            Serial1.println("AT");
            
            String resp = "";
            unsigned long t0 = millis();
            while (millis() - t0 < 500 && (millis() - recovery_start < RECOVERY_TIMEOUT)) {
                watchdog_update();
                while (Serial1.available()) resp += (char)Serial1.read();
                if (resp.indexOf("OK") != -1) {
                    contact = true;
                    break;
                }
                delay(20);
            }
            if (contact) break;
            
            for (int w = 0; w < 8; w++) {
                watchdog_update();
                delay(100);
            }
        }

        if (!contact) {
            s_recovery_fail_count++;
            s_recovery_disabled_until = now + 5 * 60 * 1000UL;
            Logger::printf("[LTE CRASH] Modem injoignable après power-cycle. Crash count=%d\n", 
                s_recovery_fail_count);
            s_last_recover_ms = millis();
            return;
        }

        // ─── RECONFIG SIMPLE ───
        Logger::println("[LTE] Contact rétabli! Reconfiguration...");
        s_modem_confirmed = true;
        sendAT("ATE0", 800);
        sendAT("AT+CMEE=2", 800);
        sendAT("AT+CFUN=1", 1000);
        
        Logger::println("[LTE] RECOVERY OK");
        s_recovery_fail_count = 0;
        s_last_recover_ms = millis();
        last_check = millis();
    }

    static String decodeOperator(const String& num) {
        if (num == "20801" || num == "20802") return "Orange";
        if (num == "20810" || num == "20811" || num == "20813") return "SFR";
        if (num == "20815") return "Free Mobile";
        if (num == "20816") return "NRJ Mobile";
        if (num == "20820" || num == "20821" || num == "20888") return "Bouygues";
        if (num == "26201") return "Telekom";
        if (num == "26202") return "Vodafone DE";
        return num;
    }

    static void pushIncomingSms(const String& number, const String& text, long ts) {
        uint8_t next = (s_sms_q_tail + 1) % SMS_Q_SIZE;
        if (next == s_sms_q_head) {
            s_sms_q_head = (s_sms_q_head + 1) % SMS_Q_SIZE;
            Serial.println("[LTE] WARN queue SMS pleine");
        }
        String clean_number = number; clean_number.trim();
        String clean_text   = text;   clean_text.trim();
        SmsEnvelope& e = s_sms_q[s_sms_q_tail];
        strncpy(e.number, clean_number.c_str(), sizeof(e.number)-1); e.number[31]  = '\0';
        strncpy(e.text,   clean_text.c_str(),   sizeof(e.text)-1);   e.text[280]   = '\0';
        e.timestamp = ts;
        s_sms_q_tail = next;
    }

    static void finalizeSmsRx() {
        s_sms_rx_text.trim();
        if (s_sms_rx_text.length() > 0 && s_sms_rx_number.length() > 0) {
            time_t tnow; time(&tnow);
            pushIncomingSms(s_sms_rx_number, s_sms_rx_text, (long)tnow);
        }
        if (s_sms_rx_idx.length() > 0) {
            sendAT("AT+CMGD=" + s_sms_rx_idx, 1000);
        }
        s_sms_rx_active  = false;
        s_sms_rx_text    = "";
        s_sms_rx_number  = "";
        s_sms_rx_idx     = "";
    }

    static String csvField(const String& line, int index) {
        int field = 0; bool in_q = false; String out = "";
        for (unsigned int i = 0; i < line.length(); i++) {
            char c = line[i];
            if (c == '"') { in_q = !in_q; continue; }
            if (c == ',' && !in_q) { if (field == index) return out; field++; out = ""; continue; }
            if (field == index) out += c;
        }
        return (field == index) ? out : "";
    }

    // ── Enregistrement et notification des erreurs réseau ──
    static void logNetworkError(NetworkErrorCode code, const char* app = "4G/LTE", const char* details = nullptr) {
        unsigned long now = millis();
        
        // Éviter les notifications spam : max 1 par 3 secondes pour la même erreur
        if (s_last_error == code && (now - s_last_error_notify_time) < 3000) {
            return;
        }
        
        s_last_error = code;
        s_last_error_time = now;
        s_error_notify_count++;
        
        // Enregistrer dans le log
        if (code != NetworkErrorCode::NO_ERROR) {
            Logger::printf("[LTE ERROR] %s: %s\n", getErrorString(code), details ? details : "");
            s_last_error_notify_time = now;
            
            // Notifier via le NotificationCenter si disponible
            // On utilise un pointeur externe pour éviter la dépendance circulaire
            extern void lte_notify_error(const char* title, const char* body);
            String body = String(getErrorString(code));
            if (details) {
                body += "\n";
                body += details;
            }
            lte_notify_error(app, body.c_str());
        }
    }
    
    static void clearNetworkError() {
        s_last_error = NetworkErrorCode::NO_ERROR;
        s_last_error_time = millis();
    }

    static void dispatchLine(const String& line) {
        if (line.length() == 0) return;

        if (s_sms_rx_active &&
            (line.startsWith("+CMTI:") || line.startsWith("+CMT:")  ||
             line.startsWith("+CMGR:") || line == "SMS Ready")) {
            finalizeSmsRx();
        }

        if (s_sms_rx_active) {
            if (line == "OK") {
                finalizeSmsRx();
            } else if (!line.startsWith("+CGEV:") && !line.startsWith("+CREG:") &&
                       !line.startsWith("+CEREG:") && !line.startsWith("+CGREG:") && line != "ERROR") {
                if (s_sms_rx_text.length() > 0) s_sms_rx_text += "\n";
                s_sms_rx_text += line;
                s_sms_rx_timeout = millis();
                if (s_sms_rx_idx.length() == 0) finalizeSmsRx();
            }
            return;
        }

        if (line.startsWith("+CMT:")) {
            String data = line.substring(5);
            String number = csvField(data, 0); number.trim();
            s_sms_rx_number  = number;
            s_sms_rx_idx     = "";
            s_sms_rx_text    = "";
            s_sms_rx_active  = true;
            s_sms_rx_timeout = millis();
            return;
        }

        if (line.startsWith("+CMTI:")) {
            int comma = line.indexOf(',', 6);
            if (comma != -1) {
                String idx = line.substring(comma + 1); idx.trim();
                // Différer la lecture via la queue pour éviter la récursion dans sendAT()
                pushCmtiIdx(idx.toInt());
            }
            return;
        }

        if (line == "SMS Ready" || line == "Call Ready" || line == "PB DONE") {
            if (line == "SMS Ready") {
                s_modem_confirmed = false;
                sendAT("ATE0",             600);
                sendAT("AT+CMGF=1",        600);
                sendAT("AT+CNMI=2,2,0,0,0",600);
                sendAT("AT+CGEREP=0,0",    600);
                sendAT("AT+CTZU=1",        600);
                sendAT("AT+COPS=3,0",      600);
                sendAT("AT+CREG=1",        600);
                sendAT("AT+CGREG=1",       600);
                s_modem_confirmed = true;
            }
            last_check = 0;
            return;
        }

        if (line.startsWith("+CREG:") || line.startsWith("+CGREG:")) {
            int urc_comma = line.indexOf(',');
            int stat = -1;
            if (urc_comma != -1) {
                stat = line.substring(urc_comma+1, urc_comma+2).toInt();
            } else {
                int urc_col = line.indexOf(':');
                if (urc_col != -1) { String sv = line.substring(urc_col+1); sv.trim(); stat = sv.toInt(); }
            }
            if (stat == 1 || stat == 5) {
                s_registered = true;
                if (operator_name == "Recherche..." || operator_name == "Aucun service") last_check = 0;
            } else if (stat == 0 || stat == 2 || stat == 3) {
                s_registered = false;
                if (stat == 2)      operator_name = "Recherche...";
                else if (stat == 3) operator_name = "Réseau refusé";
                else                operator_name = "Aucun service";
                signal_level = 0;
            }
            return;
        }
    }

    static void doSendSms() {
        const char* number = s_sms_send_req.number;
        const char* text   = s_sms_send_req.text;

        Serial1.write(27); delay(300);
        while (Serial1.available()) Serial1.read();
        
        Serial1.print('\r'); delay(20);
        while (Serial1.available()) Serial1.read();
        sendAT("AT+CMGF=1", 600);

        String prompt_resp = "";
        Serial1.print("AT+CMGS=\""); Serial1.print(number); Serial1.print("\"\r");

        unsigned long t0 = millis();
        while (millis() - t0 < 5000) {
            watchdog_update();
            while (Serial1.available()) prompt_resp += (char)Serial1.read();
            if (prompt_resp.indexOf('>') != -1) break;
            delay(20);
        }

        if (prompt_resp.indexOf('>') == -1) {
            recoverModemIfNeeded("CMGS prompt timeout");
            s_sms_send_result  = false;
            s_sms_send_pending = false;
            return;
        }

        Serial1.print(text); delay(100);
        Serial1.write(26);

        String send_resp = "";
        unsigned long t1 = millis();
        while (millis() - t1 < 15000) {
            watchdog_update();
            while (Serial1.available()) send_resp += (char)Serial1.read();
            if (send_resp.indexOf("+CMGS:")  != -1 ||
                send_resp.indexOf("OK\r\n")  != -1 ||
                send_resp.indexOf("ERROR\r\n") != -1) break;
            delay(50);
        }

        if (send_resp.indexOf("+CMS ERROR") != -1 || send_resp.indexOf("ERROR") != -1) {
            logNetworkError(NetworkErrorCode::SMS_SEND_FAILED, "SMS", "Erreur envoi SMS");
            recoverModemIfNeeded("SMS +CMS ERROR");
        }

        s_sms_send_result  = (send_resp.indexOf("+CMGS:") != -1 || send_resp.indexOf("OK") != -1);
        __sync_synchronize(); // Garantit que s_sms_send_result est visible avant s_sms_send_pending = false
        s_sms_send_pending = false;
    }

public:

    // ─── Séquence standard d'allumage SIMCom A7670E ───
    static void powerOnA7670E() {
        Logger::println("[LTE] Séquence d'allumage A7670E (Logique INVERSÉE)...");
        
        // Configure le pin PWRKEY en sortie
        pinMode(A7670_PWRKEY, OUTPUT);
        
        // Étape 1: Assurer que PWRKEY est relâché (LOW avec transistor)
        digitalWrite(A7670_PWRKEY, LOW);
        delay(100);
        Logger::println("[LTE] PWRKEY initié à LOW (Relâché)");
        
        // Étape 2: Tirer PWRKEY à HIGH pour simuler l'appui
        digitalWrite(A7670_PWRKEY, HIGH);
        Logger::println("[LTE] PWRKEY tiré à HIGH (Appui en cours)...");
        delay(1500); // 1.5s est recommandé pour le A7670E
        
        // Étape 3: Relâcher PWRKEY (retour à LOW)
        digitalWrite(A7670_PWRKEY, LOW);
        Logger::println("[LTE] PWRKEY relâché (LOW)");
        
        // Étape 4: Délai de sécurité pour laisser le modem démarrer
        Logger::println("[LTE] Attente du démarrage du modem (4000ms)...");
        delay(4000); // Un peu plus long pour s'assurer que la carte SIM est lue
        
        Logger::println("[LTE] Séquence d'allumage terminée");
    }

    static void init() {
        s_enabled          = true;
        s_airplane_mode    = false;
        signal_level       = 0;
        operator_name      = "Recherche...";
        state              = 0;
        s_line_buf         = "";
        s_cmd_resp         = "";
        last_check         = millis();
        next_time_sync_try = millis() + 5000UL; 
        time_sync_fail_count = 0;
        s_last_operator_poll = 0;
        s_registered       = false;
        s_pending_enable   = -1;
        s_pending_airplane = -1;
        s_http_busy        = false;
        s_modem_confirmed  = false;
        s_poll_fail_count  = 0;
        s_sms_q_head       = 0;
        s_sms_q_tail       = 0;
        s_sms_rx_active    = false;
        s_sms_send_pending = false;
        s_cmti_q_head      = 0;
        s_cmti_q_tail      = 0;

        if (!Logger::isOk()) {
            Logger::begin();
        }
        Logger::println("[LTE] Initialisation au démarrage...");

        // Séquence d'allumage du modem A7670E
        powerOnA7670E();

        // 1. Purge et électrochoc de réveil de l'UART
        while (Serial1.available()) Serial1.read();
        Serial1.print('\r');
        delay(50);
        while (Serial1.available()) Serial1.read();

        // 2. Test de communication avec le modem
        bool ok = false;
        for (int i = 0; i < 5; i++) {
            Logger::printf("[LTE] Ping de démarrage %d/5...\n", i+1);
            if (sendAT("AT", 1000).indexOf("OK") != -1) {
                ok = true;
                break;
            }
            delay(500);
        }

        // 3. Aiguillage : Configuration LÉGÈRE pour laisser la radio s'accrocher
        if (ok) {
            Logger::println("[LTE] Modem détecté ! Configuration de base...");
            s_modem_confirmed = true;
            
            sendAT("ATE0",        1000); // Désactive l'écho
            sendAT("AT+CMEE=2",   1000); // Erreurs textuelles explicites
            sendAT("AT+CFUN=1",   2000); // Force l'allumage de la puce Radio
            sendAT("AT+COPS=0",   2000); // Force la recherche réseau AUTOMATIQUE
            sendAT("AT+COPS=3,0", 1000); // Format du nom de l'opérateur en texte
            sendAT("AT+CTZU=1",   1000); // Mise à jour auto de l'heure
            sendAT("AT+CSCS=\"GSM\"", 1000); 
            sendAT("AT+CMGF=1",       1000); // Mode SMS en texte
            sendAT("AT+CNMI=2,2,0,0,0", 1000); 
            sendAT("AT+CGEREP=0,0",   1000); 
            sendAT("AT+CREG=1",       1000); // Active les notifications réseau
            sendAT("AT+CGREG=1",      1000); 
            
            // On a supprimé AT+CGATT=1 et preparePacketDataContext() ici.
            // On le laisse chercher le réseau tranquillement en arrière-plan.
            
            Logger::println("[LTE] Modem prêt ! En attente d'accroche réseau...");
        } else {
            Logger::println("[LTE] ALERTE: Modem injoignable au boot ! Lancement du Recovery...");
            logNetworkError(NetworkErrorCode::MODEM_NOT_RESPONDING, "4G/LTE", "Modem injoignable au boot");
            s_last_recover_ms = 0; 
            recoverModemIfNeeded("Echec de synchronisation au démarrage");
        }
    }

    static void enable(bool en)          { s_pending_enable   = en ? 1 : 0; }
    static void setAirplaneMode(bool en) { s_pending_airplane = en ? 1 : 0; }
    static bool isEnabled()              { return s_enabled; }
    static bool isAirplaneMode()         { return s_airplane_mode; }
    static int  getSignal()              { return signal_level; }
    static String getOperator()          { return operator_name; }
    static bool isReadyForData()         { return s_enabled && !s_airplane_mode && s_modem_confirmed && signal_level > 0; }
    static void setLowPower(bool enable) {
        if (enable) {
            // Mode économie : Désactive la RF (Radio Fréquence)
            // Consommation chute drastiquement (~1.5mA au lieu de 20-40mA)
            Serial1.println("AT+CFUN=0"); 
            Serial.println("[LTE] RF OFF (Mode Eco)");
        } else {
            // Mode normal : Réactive la 4G et la SIM
            Serial1.println("AT+CFUN=1");
            Serial.println("[LTE] RF ON (Full Function)");
            // On peut ajouter un AT+CREG? après quelques secondes pour vérifier le réseau
        }
    }

    // ── Gestion des erreurs réseau ──
    static NetworkErrorCode getLastError()       { return s_last_error; }
    static unsigned long getLastErrorTime()      { return s_last_error_time; }
    static bool hasActiveError()                 { return s_last_error != NetworkErrorCode::NO_ERROR; }
    
    static const char* getErrorString(NetworkErrorCode code) {
        switch(code) {
            case NetworkErrorCode::NO_ERROR: return "OK";
            case NetworkErrorCode::MODEM_NOT_RESPONDING: return "Modem non réactif";
            case NetworkErrorCode::NO_SIGNAL: return "Pas de signal";
            case NetworkErrorCode::HTTP_TIMEOUT: return "Timeout réseau";
            case NetworkErrorCode::HTTP_NETWORK_ERROR: return "Erreur réseau";
            case NetworkErrorCode::HTTP_INVALID_RESPONSE: return "Réponse invalide";
            case NetworkErrorCode::SMS_SEND_FAILED: return "SMS non envoyé";
            case NetworkErrorCode::TIME_SYNC_FAILED: return "Sync heure échouée";
            case NetworkErrorCode::MODEM_CRASH: return "Modem crashé!";
            case NetworkErrorCode::REGISTRATION_FAILED: return "Enregistrement échoué";
            case NetworkErrorCode::DATA_CONNECTION_FAILED: return "Connexion données échouée";
            case NetworkErrorCode::RECOVERY_IN_PROGRESS: return "Récupération en cours...";
            default: return "Erreur inconnue";
        }
    }

    static bool shouldRetryOperation() {
        // Peut réessayer si pas d'erreur active ou si l'erreur est ancienne (> 5 secondes)
        return !hasActiveError() || (millis() - s_last_error_time > 5000);
    }

    static bool scheduleSendSms(const char* number, const char* text) {
        if (s_sms_send_pending) return false;
        strncpy(s_sms_send_req.number, number, sizeof(s_sms_send_req.number)-1);
        strncpy(s_sms_send_req.text,   text,   sizeof(s_sms_send_req.text)-1);
        s_sms_send_result  = false;
        s_sms_send_pending = true;
        return true;
    }
    
    static bool isSendDone()    { return !s_sms_send_pending; }
    static bool getSendResult() { return s_sms_send_result; }

    static bool popIncomingSms(char* out_number, char* out_text, long* out_ts) {
        if (s_sms_q_head == s_sms_q_tail) return false;
        const SmsEnvelope& e = s_sms_q[s_sms_q_head];
        if (out_number) strncpy(out_number, e.number, 31);
        if (out_text)   strncpy(out_text,   e.text,  280);
        if (out_ts)     *out_ts = e.timestamp;
        s_sms_q_head = (s_sms_q_head + 1) % SMS_Q_SIZE;
        return true;
    }

    static String httpGetBlocking(const String& url) {
    const size_t MAX_HTTP = 12000;

    if (!s_enabled || s_airplane_mode || !s_modem_confirmed) {
        return "";
    }

    s_http_busy = true;

    for (int attempt = 1; attempt <= 2; attempt++) {

        Logger::printf("[LTE HTTP] GET %s (%d/2)\n", url.c_str(), attempt);

        if (!preparePacketDataContext()) continue;

        sendAT("AT+HTTPTERM", 1000);
        delay(100);

        if (sendAT("AT+HTTPINIT", 3000).indexOf("OK") == -1) continue;

        if (sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 3000).indexOf("OK") == -1) {
            sendAT("AT+HTTPTERM", 500);
            continue;
        }

        Serial1.println("AT+HTTPACTION=0");

        unsigned long start = millis();
        String resp;
        resp.reserve(128);

        int http_code = -1;
        int data_len = 0;

        while (millis() - start < 20000) {
            watchdog_update();

            while (Serial1.available()) {
                char c = Serial1.read();
                resp += c;
            }

            int idx = resp.indexOf("+HTTPACTION:");
            if (idx != -1) {
                String part = resp.substring(idx + 12);
                int c1 = part.indexOf(',');
                int c2 = part.indexOf(',', c1 + 1);

                if (c1 > 0 && c2 > c1) {
                    http_code = part.substring(c1+1, c2).toInt();
                    data_len  = part.substring(c2+1).toInt();
                }
                break;
            }

            delay(10);
        }

        if (http_code != 200 || data_len <= 0) {
            sendAT("AT+HTTPTERM", 500);
            continue;
        }

        if (data_len > MAX_HTTP) {
            Logger::println("[LTE HTTP] TRUNCATED");
            data_len = MAX_HTTP;
        }

        String body;
        body.reserve(data_len + 1);

        int total = 0;

        while (total < data_len) {
            watchdog_update();

            int chunk = min(512, data_len - total);

            while (Serial1.available()) Serial1.read();

            Serial1.print("AT+HTTPREAD=");
            Serial1.println(chunk);

            unsigned long t0 = millis();
            bool header_ok = false;
            int expected = 0;
            String header;
            header.reserve(64);

            while (millis() - t0 < 3000) {
                watchdog_update();

                while (Serial1.available()) {
                    char c = Serial1.read();
                    header += c;

                    int p = header.indexOf("+HTTPREAD:");
                    if (p != -1) {
                        int nl = header.indexOf('\n', p);
                        if (nl != -1) {
                            int col = header.indexOf(':', p);
                            expected = header.substring(col + 1).toInt();
                            header_ok = true;
                            break;
                        }
                    }
                }

                if (header_ok) break;
            }

            if (!header_ok || expected <= 0) break;

            int read_bytes = 0;
            t0 = millis();

            while (read_bytes < expected && millis() - t0 < 5000) {
                watchdog_update();

                while (Serial1.available() && read_bytes < expected) {
                    char c = Serial1.read();

                    if (body.length() < MAX_HTTP) {
                        body.concat(c);
                        total++;
                    }

                    read_bytes++;
                }
            }

            if (read_bytes != expected) break;
        }

        sendAT("AT+HTTPTERM", 500);

        if (body.length() > 0) {
            s_http_busy = false;
            return body;
        }
    }

    s_http_busy = false;
    return "";
}
    
    static void update() {
        if (s_http_busy) {
            // CRUCIAL : Ne SURTOUT PAS toucher à Serial1 ici ! 
            // Le Cœur 1 est en train de s'en servir pour télécharger.
            return;
        }

        if (s_sms_rx_active && (millis() - s_sms_rx_timeout > 5000)) {
            finalizeSmsRx();
        }

        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\r') continue;
            if (c == '\n') {
                String line = s_line_buf; s_line_buf = ""; line.trim();
                if (line.length() == 0) continue;

                if (s_sms_rx_active || line.startsWith("+CMT") || line.startsWith("+CMTI") || line.startsWith("+CGE") || line.startsWith("+CEREG") || line.startsWith("+CREG") || line.startsWith("+CGREG") || line == "SMS Ready" || line == "Call Ready" || line == "PB DONE") {
                    dispatchLine(line);
                } else {
                    s_cmd_resp += line + "\n";
                }
            } else {
                s_line_buf += c;
                if (s_line_buf == "> ") s_line_buf = ""; 
            }
        }

        if (s_pending_airplane != -1) {
            if (s_pending_airplane == 1) {
                sendAT("AT+CFUN=4", 5000);
                s_airplane_mode = true; s_enabled = false; signal_level = 0; operator_name = "Mode Avion";
            } else {
                sendAT("AT+CFUN=1", 6000);
                s_airplane_mode = false; s_enabled = true; operator_name = "Recherche..."; last_check = 0;
            }
            s_pending_airplane = -1; state = 0; return;
        }

        if (s_pending_enable != -1) {
            if (s_pending_enable == 1) {
                sendAT("AT+CGATT=1", 10000);
                s_enabled = true; operator_name = "Recherche..."; last_check = 0;
            } else {
                sendAT("AT+CGATT=0", 5000);
                s_enabled = false; operator_name = "Données OFF"; signal_level = 0;
            }
            s_pending_enable = -1; state = 0; return;
        }

        if (s_airplane_mode) return;

        if (s_sms_send_pending && state == 0) {
            doSendSms();
            last_check = millis();
            return;
        }

        // Traitement différé des SMS notifiés via +CMTI: (évite la récursion dans sendAT)
        if (state == 0 && !s_sms_send_pending) {
            int cmti_idx = -1;
            if (popCmtiIdx(cmti_idx)) {
                String resp = sendAT("AT+CMGR=" + String(cmti_idx), 3000);
                int cmgr_pos = resp.indexOf("+CMGR:");
                if (cmgr_pos != -1) {
                    int nl1 = resp.indexOf('\n', cmgr_pos);
                    if (nl1 != -1) {
                        String hdr = resp.substring(cmgr_pos + 6, nl1); hdr.trim();
                        String number = csvField(hdr, 1); number.trim();
                        int text_start = nl1 + 1;
                        while (text_start < (int)resp.length() && resp[text_start] == '\r') text_start++;
                        int nl2 = resp.indexOf('\n', text_start);
                        String text = (nl2 != -1) ? resp.substring(text_start, nl2) : resp.substring(text_start);
                        text.trim();
                        if (number.length() > 0 && text.length() > 0) {
                            time_t tnow; time(&tnow);
                            pushIncomingSms(number, text, (long)tnow);
                        }
                    }
                }
                sendAT("AT+CMGD=" + String(cmti_idx), 1000);
                return;
            }
        }

        if (state == 0) {
            if (millis() - last_check > 15000) {
                s_cmd_resp = "";
                bool need_op = (operator_name == "Recherche..." || operator_name == "Aucun service" || millis() - s_last_operator_poll > 120000UL);
                if (need_op) {
                    Serial1.println("AT+COPS?");
                    state = 1;
                } else {
                    Serial1.println("AT+CSQ");
                    state = 2;
                }
                wait_start = millis();
            }
            return;
        }
unsigned long state_timeout = 3000UL;
        if (state == 1) state_timeout = 45000UL; // 45 sec pour la recherche d'opérateur
        else if (state == 3) state_timeout = 5000UL;
        else if (state == 5) state_timeout = 15000UL; // Le serveur NTP peut mettre 10 secondes à répondre

        bool done = false;
        if (state == 5) {
            // Cas spécial pour le NTP : on n'attend pas "OK" mais le résultat "+CNTP:"
            done = (s_cmd_resp.indexOf("+CNTP:") != -1 || s_cmd_resp.indexOf("ERROR\n") != -1 || millis() - wait_start > state_timeout);
        } else {
            done = (s_cmd_resp.indexOf("OK\n") != -1 || s_cmd_resp.indexOf("ERROR\n") != -1 || millis() - wait_start > state_timeout);
        }
        
        if (!done) return;

        bool timed_out = (s_cmd_resp.indexOf("OK\n") == -1 && s_cmd_resp.indexOf("ERROR\n") == -1 && s_cmd_resp.indexOf("+CNTP:") == -1);

        // Si le réseau met trop de temps sur une commande non vitale, on applique un délai avant de réessayer
        if (timed_out && (state == 3 || state == 5)) {
            if (time_sync_fail_count < 12) time_sync_fail_count++;
            next_time_sync_try = millis() + (30000UL * time_sync_fail_count);
            s_cmd_resp = ""; state = 0; last_check = millis();
            return;
        }

        if (timed_out) {
            s_poll_fail_count++;
            if (s_poll_fail_count >= 5) {
                s_poll_fail_count = 0;
                logNetworkError(NetworkErrorCode::MODEM_NOT_RESPONDING, "4G/LTE", "5 timeouts consécutifs");
                recoverModemIfNeeded("5 timeouts consecutives");
            }
            last_check = millis();
            state = 0;
            return;
        } else {
            s_poll_fail_count = 0;
        }

        if (state == 1) {
            if (s_enabled) {
                int q1 = s_cmd_resp.indexOf('"');
                if (q1 != -1) {
                    int q2 = s_cmd_resp.indexOf('"', q1+1);
                    if (q2 != -1) {
                        String raw = s_cmd_resp.substring(q1+1, q2);
                        operator_name = decodeOperator(raw);
                    }
                } else if (!s_registered) {
                    operator_name = "Aucun service";
                }
            }
            s_last_operator_poll = millis();
            s_cmd_resp = "";
            Serial1.println("AT+CSQ");
            state = 2; wait_start = millis();
            return;
        }

        if (state == 2) {
            if (s_enabled) {
                int col = s_cmd_resp.indexOf("+CSQ:");
                if (col != -1) {
                    int comma = s_cmd_resp.indexOf(',', col);
                    if (comma != -1) {
                        int val = s_cmd_resp.substring(col+5, comma).toInt();
                        if (val == 99) signal_level = 0;
                        else if (val < 10) signal_level = 1;
                        else if (val < 15) signal_level = 2;
                        else if (val < 20) signal_level = 3;
                        else signal_level = 4;
                    }
                }
            }
            
            bool time_missing = (time(nullptr) < 1000000000L);
            bool time_stale   = (millis() - last_time_sync > 30UL*60UL*1000UL);
            
            if (s_enabled && !s_airplane_mode && s_modem_confirmed && (time_missing || time_stale) && millis() >= next_time_sync_try) {
                s_cmd_resp = "";
                Serial1.println("AT+CCLK?");
                state = 3; 
                wait_start = millis();
            } else {
                state = 0; last_check = millis();
            }
            return;
        }

        if (state == 3) {
            if (!applyTimeFromCclkResponse(s_cmd_resp, "4G/NITZ")) {
                // Heure invalide ! Le pylône ne nous l'a pas envoyée.
                // Si la 4G est bien connectée, on force une synchro NTP sur Internet
                if (s_enabled && s_registered && signal_level > 0) {
                    s_cmd_resp = "";
                    // 4 = Fuseau UTC+1 (Paris Hiver). Pour l'été, tu peux mettre 8 (UTC+2)
                    Serial1.println("AT+CNTP=\"pool.ntp.org\",4");
                    state = 4;
                    wait_start = millis();
                    return;
                } else {
                    // Pas de réseau, on réessaie plus tard
                    if (time_sync_fail_count < 12) time_sync_fail_count++;
                    next_time_sync_try = millis() + 60000UL;
                }
            }
            s_cmd_resp = ""; state = 0; last_check = millis();
            return;
        }

        if (state == 4) {
            // Le serveur NTP est configuré, on lance l'action !
            s_cmd_resp = "";
            Serial1.println("AT+CNTP"); 
            state = 5;
            wait_start = millis();
            return;
        }

        if (state == 5) {
            // Analyse du retour du serveur NTP (+CNTP: 0 = Succès)
            if (s_cmd_resp.indexOf("+CNTP: 0") != -1 || s_cmd_resp.indexOf("+CNTP: 1") != -1) {
                Logger::println("[LTE NTP] Synchro Internet réussie !");
                s_cmd_resp = "";
                // Le modem a maintenant la bonne heure, on reboucle sur l'état 3 pour la lire
                Serial1.println("AT+CCLK?");
                state = 3; 
                wait_start = millis();
            } else {
                Logger::println("[LTE NTP] Echec de la synchro Internet");
                if (time_sync_fail_count < 12) time_sync_fail_count++;
                next_time_sync_try = millis() + 60000UL;
                s_cmd_resp = ""; state = 0; last_check = millis();
            }
            return;
        }
    }
};

bool          LTE::s_enabled          = true;
bool          LTE::s_airplane_mode    = false;
int           LTE::signal_level       = 0;
String        LTE::operator_name      = "Init...";
volatile int  LTE::s_pending_enable   = -1;
volatile int  LTE::s_pending_airplane = -1;
volatile bool LTE::s_http_busy        = false;
volatile bool LTE::s_modem_confirmed  = false;
uint8_t       LTE::s_poll_fail_count  = 0;
uint8_t       LTE::s_recovery_fail_count     = 0;
unsigned long LTE::s_recovery_disabled_until = 0;

// Variables d'erreur réseau
NetworkErrorCode LTE::s_last_error           = NetworkErrorCode::NO_ERROR;
unsigned long LTE::s_last_error_time         = 0;
uint8_t       LTE::s_error_notify_count      = 0;
unsigned long LTE::s_last_error_notify_time  = 0;

unsigned long LTE::last_check         = 0;
int           LTE::state              = 0;
String        LTE::s_line_buf         = "";
String        LTE::s_cmd_resp         = "";
unsigned long LTE::wait_start         = 0;
unsigned long LTE::last_time_sync     = 0;
unsigned long LTE::next_time_sync_try = 0;
uint8_t       LTE::time_sync_fail_count = 0;
unsigned long LTE::s_last_recover_ms  = 0;
unsigned long LTE::s_last_operator_poll = 0;
bool          LTE::s_registered       = false;

LTE::SmsEnvelope LTE::s_sms_q[LTE::SMS_Q_SIZE] = {};
volatile uint8_t LTE::s_sms_q_head = 0;
volatile uint8_t LTE::s_sms_q_tail = 0;

bool          LTE::s_sms_rx_active   = false;
String        LTE::s_sms_rx_number   = "";
String        LTE::s_sms_rx_idx      = "";
String        LTE::s_sms_rx_text     = "";
unsigned long LTE::s_sms_rx_timeout  = 0;

LTE::SmsSendReq  LTE::s_sms_send_req    = {};
volatile bool    LTE::s_sms_send_pending = false;
volatile bool    LTE::s_sms_send_result  = false;
int8_t           LTE::s_cmti_q[LTE::CMTI_Q_SIZE] = {};
uint8_t          LTE::s_cmti_q_head = 0;
uint8_t          LTE::s_cmti_q_tail = 0;

#endif