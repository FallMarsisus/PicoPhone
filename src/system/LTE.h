#ifndef SYSTEM_LTE_H
#define SYSTEM_LTE_H

#include <Arduino.h>
#include <sys/time.h>
#include <hardware/watchdog.h>
#include "Settings.h"
#include "Logger.h"

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
            if (resp.indexOf("OK\r\n") != -1 || resp.indexOf("ERROR\r\n") != -1) break;
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
        // On évite les recovery en boucle
        if (now - s_last_recover_ms < 30000UL && s_last_recover_ms != 0) return; 
        
        Logger::printf("[LTE] RECOVERY: %s\n", reason ? reason : "(unknown)");
        logNetworkError(NetworkErrorCode::RECOVERY_IN_PROGRESS, "4G/LTE", reason);

            digitalWrite(26, HIGH);
    delay(100);
    digitalWrite(26, LOW); // Tirer à la masse
    delay(1500);                   // Maintenir bas pendant au moins 1.5s
    digitalWrite(26, HIGH);
    delay(3000);
        s_modem_confirmed = false;
        s_line_buf = "";
        s_cmd_resp = "";
        state = 0;

        // --- ÉTAPE 1 : SORTIR DU MODE DONNÉES (SÉQUENCE HAYES) ---
        Logger::println("[LTE] Tentative de sortie du mode données (+++)...");
        delay(1200); // 1. Silence absolu de >1s obligatoire
        while (Serial1.available()) Serial1.read();
        
        Serial1.print("+++"); // 2. Envoi de l'échappement (SANS retour à la ligne !)
        
        delay(1200); // 3. Silence absolu de >1s obligatoire
        while (Serial1.available()) Serial1.read();

        // --- ÉTAPE 2 : REPRISE DE CONTACT ET REBOOT MODEM ---
        bool contact = false;
        for (int i = 0; i < 5; i++) {
            Serial1.print('\r'); delay(50);
            Serial1.println("AT");
            delay(300);
            String resp = "";
            while (Serial1.available()) resp += (char)Serial1.read();
            if (resp.indexOf("OK") != -1) {
                contact = true;
                break;
            }
            watchdog_update();
        }

        if (contact) {
            Logger::println("[LTE] Contact rétabli ! Envoi de AT+CRESET pour forcer le redémarrage du modem...");
            Serial1.println("AT+CRESET");
            
            // Le modem s'éteint et se rallume. On lui laisse 15 secondes pour le faire.
            Logger::println("[LTE] Attente du reboot matériel de la puce (15s)...");
            for (int i = 0; i < 150; i++) {
                watchdog_update();
                delay(100);
            }
        } else {
            Logger::println("[LTE] Le modem est sourd aux '+++'. Tentative de resynchronisation brutale...");
        }

        // --- ÉTAPE 3 : ATTENTE DU RÉVEIL DÉFINITIF ---
        int retry = 1;
        const int MAX_WAKE_RETRIES = 20; // ~60s max (20 × 3s)
        while (retry <= MAX_WAKE_RETRIES) {
            watchdog_update();
            Serial1.print('\r'); delay(50);
            Serial1.println("AT");
            delay(500);
            
            String r = "";
            while(Serial1.available()) r += (char)Serial1.read();
            if (r.indexOf("OK") != -1) break; // Il est en vie !

            Logger::printf("[LTE] Attente du réveil du modem... (Tentative %d/%d)\n", retry, MAX_WAKE_RETRIES);
            for(int i=0; i<30; i++) { watchdog_update(); delay(100); } // Pause de 3s
            retry++;
        }

        if (retry > MAX_WAKE_RETRIES) {
            Logger::println("[LTE] RECOVERY: modem toujours injoignable après 60s, abandon.");
            s_last_recover_ms = millis();
            return;
        }

        // --- ÉTAPE 4 : RECONFIGURATION À ZÉRO ---
        Logger::println("[LTE] Modem en ligne et purgé ! Reconfiguration en cours...");
        s_modem_confirmed = true;
        sendAT("ATE0", 800);
        sendAT("AT+CMEE=2", 800);
        sendAT("AT+CMGF=1", 800);
        sendAT("AT+CNMI=2,2,0,0,0", 800);
        sendAT("AT+CGEREP=0,0", 800);
        sendAT("AT+CTZU=1", 800);
        sendAT("AT+COPS=3,0", 800);
        sendAT("AT+CREG=1", 800);
        sendAT("AT+CGREG=1", 800);
        sendAT("AT+CGATT=1", 2500);
        
        Logger::println("[LTE] RECOVERY terminée avec succès. Téléphone 100% opérationnel.");
        clearNetworkError();

        s_last_recover_ms = millis();
        last_check = millis();
        next_time_sync_try = millis() + 60000UL;
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

        Logger::begin(); 
        Logger::println("[LTE] Initialisation au démarrage...");

        // 1. Purge et électrochoc de réveil de l'UART
        while (Serial1.available()) Serial1.read();
        Serial1.print('\r');
        delay(50);
        while (Serial1.available()) Serial1.read();

        // 2. Test de communication avec le modem
        bool ok = false;
        for (int i = 0; i < 5; i++) {
            Logger::printf("[LTE] Ping de démarrage %d/5...\n", i+1);
            String r = sendAT("AT", 1000);
            if (r.indexOf("OK") != -1) {
                ok = true;
                break;
            }
            delay(500);
        }

        // 3. Aiguillage
        if (ok) {
            Logger::println("[LTE] Modem détecté ! Configuration initiale...");
            s_modem_confirmed = true;
            sendAT("ATE0",        1000); 
            sendAT("AT+CMEE=2",   1000); 
            sendAT("AT+COPS=3,0", 1000); 
            sendAT("AT+CTZU=1",   1000); 
            sendAT("AT+CSCS=\"GSM\"", 1000); 
            sendAT("AT+CMGF=1",       1000); 
            sendAT("AT+CNMI=2,2,0,0,0", 1000); 
            sendAT("AT+CGEREP=0,0",   1000); 
            sendAT("AT+CREG=1",       1000); 
            sendAT("AT+CGREG=1",      1000); 
            sendAT("AT+CGATT=1",      2000); 
            Logger::println("[LTE] Modem prêt !");
        } else {
            Logger::println("[LTE] ALERTE: Modem injoignable au boot ! Lancement du Recovery...");
            logNetworkError(NetworkErrorCode::MODEM_NOT_RESPONDING, "4G/LTE", "Modem injoignable au boot");
            // Pas de rp2040.reboot() ici ! On laisse le code s'occuper du modem.
            s_last_recover_ms = 0; // Force l'exécution immédiate
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

    static String httpGetBlocking(const String& url, unsigned long timeout_ms = 20000) {
        if (!s_enabled || s_airplane_mode) {
            Logger::println("[LTE HTTP] Annulé : Mode avion ou désactivé");
            return "";
        }
        
        s_http_busy = true;

        // Boucle de tentatives : on essaie 2 fois maximum (1 tentative normale + 1 retry en cas de crash)
        for (int attempt = 1; attempt <= 2; attempt++) {
            s_line_buf  = ""; 
            s_cmd_resp  = ""; 
            state = 0;

            Logger::printf("[LTE HTTP] GET %s (Tentative %d/2)\n", url.c_str(), attempt);

            // 1. Nettoyage de sécurité
            sendAT("AT+HTTPTERM", 1000); 
            delay(300);

            // 2. Initialisation HTTP
            String init_resp = sendAT("AT+HTTPINIT", 3000);
            if (init_resp.indexOf("ERROR") != -1 || init_resp == "") { 
                Logger::println("[LTE HTTP] ERR/TIMEOUT: HTTPINIT a échoué. Modem crashé ?");
                logNetworkError(NetworkErrorCode::MODEM_CRASH, "HTTPClient", "Échec HTTPINIT");
                recoverModemIfNeeded("HTTPINIT timeout/crash");
                continue; // On passe directement au retry
            }

            // 3. Configuration de l'URL
            String url_resp = sendAT("AT+HTTPPARA=\"URL\",\"" + url + "\"", 3000);
            if (url_resp.indexOf("ERROR") != -1 || url_resp == "") { 
                Logger::println("[LTE HTTP] ERR/TIMEOUT: URL rejetée.");
                logNetworkError(NetworkErrorCode::HTTP_NETWORK_ERROR, "HTTPClient", "URL rejetée");
                sendAT("AT+HTTPTERM", 500); 
                recoverModemIfNeeded("HTTPPARA timeout/crash");
                continue; // On passe au retry
            }

            // 4. Lancement de la requête HTTP
            while (Serial1.available()) Serial1.read(); 
            Serial1.print('\r'); 
            delay(20);
            while (Serial1.available()) Serial1.read(); 
            
            Logger::println("[LTE->GSM] AT+HTTPACTION=0");
            Serial1.println("AT+HTTPACTION=0");

            unsigned long start = millis();
            String action_resp  = "";
            int http_code = -1, data_len = 0;
            bool action_timeout = true;
            
            // 5. Attente de la réponse
            while (millis() - start < timeout_ms) {
                watchdog_update();
                while (Serial1.available()) {
                    action_resp += (char)Serial1.read();
                }
                
                int idx = action_resp.indexOf("+HTTPACTION:");
                if (idx != -1) {
                    action_timeout = false; // On a eu une réponse, ce n'est pas un crash total
                    String part = action_resp.substring(idx + 12);
                    int c1 = part.indexOf(',');
                    int c2 = part.indexOf(',', c1 + 1);
                    if (c1 > 0 && c2 > c1) {
                        http_code = part.substring(c1+1, c2).toInt();
                        data_len  = part.substring(c2+1).toInt();
                    }
                    break;
                }
                
                if (action_resp.indexOf("ERROR\r\n") != -1) {
                    action_timeout = false;
                    break;
                }
                delay(50);
            }

            // --- GESTION DU CRASH LORS DU HTTPACTION ---
            if (action_timeout) {
                Logger::println("[LTE HTTP] TIMEOUT total sur HTTPACTION ! Le modem a crashé.");
                logNetworkError(NetworkErrorCode::HTTP_TIMEOUT, "HTTPClient", "HTTPACTION timeout");
                recoverModemIfNeeded("HTTPACTION timeout (Pic de courant 4G)");
                continue; // Retry
            }

            Logger::printf("[LTE HTTP] Résultat HTTP_CODE: %d | TAILLE: %d octets\n", http_code, data_len);

            if (http_code != 200 || data_len <= 0) {
                Logger::println("[LTE HTTP] ECHEC: Erreur réseau ou code HTTP invalide");
                logNetworkError(NetworkErrorCode::HTTP_NETWORK_ERROR, "HTTPClient", 
                    String("Code HTTP: " + String(http_code)).c_str());
                sendAT("AT+HTTPTERM", 500); 
                // Si le code est -1, ça veut dire que l'action a échoué lamentablement côté réseau
                if (http_code == -1) recoverModemIfNeeded("HTTPACTION network error");
                continue; // Retry
            }

            // 6. Lecture du contenu téléchargé
            int read_len = min(data_len, 4096); 
            while (Serial1.available()) Serial1.read();
            
            Serial1.print('\r'); 
            delay(20); 
            Serial1.println("AT+HTTPREAD=0," + String(read_len));
            
            String r = "";
            unsigned long hr_start = millis();
            bool read_timeout = true;
            
            while (millis() - hr_start < 15000) {
                watchdog_update();
                while (Serial1.available()) {
                    r += (char)Serial1.read();
                }
                int hr_pos = r.indexOf("+HTTPREAD:");
                if (hr_pos != -1) {
                    int nl = r.indexOf('\n', hr_pos);
                    if (nl != -1 && (int)r.length() >= nl + 1 + read_len) {
                        read_timeout = false;
                        break;
                    }
                }
                if (r.indexOf("ERROR") != -1) {
                    read_timeout = false;
                    break;
                }
                delay(10);
            }
            
            sendAT("AT+HTTPTERM", 500);
            last_check = millis();
            
            // --- GESTION DU CRASH LORS DE LA LECTURE ---
            if (read_timeout) {
                Logger::println("[LTE HTTP] TIMEOUT sur HTTPREAD ! Le modem a crashé.");
                logNetworkError(NetworkErrorCode::HTTP_TIMEOUT, "HTTPClient", "HTTPREAD timeout");
                recoverModemIfNeeded("HTTPREAD timeout");
                continue; // Retry
            }

            // Si on arrive ici, c'est que la requête a réussi ! On sort de la boucle avec succès.
            s_http_busy = false;

            // 8. Découpage du résultat
            int body_start = r.indexOf("+HTTPREAD:");
            if (body_start != -1) {
                body_start = r.indexOf('\n', body_start) + 1;
                while (body_start < (int)r.length() && r[body_start] == '\r') body_start++;
                int body_end = (int)r.length();
                while (body_end > body_start && (r[body_end-1] == '\r' || r[body_end-1] == '\n')) body_end--;
                if (body_start < body_end) {
                    return r.substring(body_start, body_end);
                }
            }
            return r; // Renvoie tout si on ne trouve pas les marqueurs
        }

        // Si la boucle se termine sans "return", c'est que les 2 tentatives ont échoué.
        s_http_busy = false;
        Logger::println("[LTE HTTP] Echec définitif après les retentatives.");
        return "";
    }

    static void update() {
        if (s_http_busy) {
            while (Serial1.available()) Serial1.read();
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