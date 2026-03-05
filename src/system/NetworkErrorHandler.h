#ifndef SYSTEM_NETWORK_ERROR_HANDLER_H
#define SYSTEM_NETWORK_ERROR_HANDLER_H

#include "LTE.h"
#include "NotificationCenter.h"

class NetworkErrorHandler {
public:
    // Affiche une alerte à l'écran et notifie si une erreur réseau est présente
    static void showIfError(const char* app_name, const char* operation) {
        if (LTE::hasActiveError()) {
            NetworkErrorCode error = LTE::getLastError();
            String msg = String(operation) + " - " + LTE::getErrorString(error);
            notifications::push(app_name, "Connectivité réseau", msg.c_str());
        }
    }
    
    // Retourne vrai si on peut réessayer l'opération
    static bool canRetry() {
        return LTE::shouldRetryOperation();
    }
    
    // Retourne l'état lisible du réseau
    static const char* getNetworkStatus() {
        if (!LTE::isEnabled()) return "Données 4G désactivées";
        if (LTE::isAirplaneMode()) return "Mode Avion activé";
        if (!LTE::isReadyForData()) return "Pas de signal 4G";
        if (LTE::hasActiveError()) return "Erreur réseau en cours";
        return "OK - Réseau opérationnel";
    }
    
    // Pour debug : affiche le dernier code d'erreur
    static void debugPrintLastError() {
        if (LTE::hasActiveError()) {
            unsigned long time_ago = millis() - LTE::getLastErrorTime();
            Logger::printf("[NetworkErrorHandler] Dernière erreur: %s (il y a %lu ms)\n", 
                LTE::getErrorString(LTE::getLastError()),
                time_ago);
        } else {
            Logger::println("[NetworkErrorHandler] Aucune erreur active");
        }
    }
};

#endif
