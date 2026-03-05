# Gestion des Erreurs Réseau LTE - Documentation

## Vue d'ensemble

Un nouveau système robuste de gestion des erreurs réseau a été implémenté pour :
- **Prévenir les crashes MCU** : Les erreurs réseau sont capturées et traitées sans crash
- **Notifier les utilisateurs** : Les applications affichent les erreurs via le NotificationCenter
- **Traçabilité** : Toutes les erreurs sont loggées pour debug via le Logger

## Architecture

### 1. **Codes d'erreur réseau (NetworkErrorCode)**

Situé dans `LTE.h`, énumère les types d'erreurs possibles :

```cpp
enum class NetworkErrorCode : uint8_t {
    NO_ERROR = 0,
    MODEM_NOT_RESPONDING = 1,      // Modem injoignable
    NO_SIGNAL = 2,                 // Pas de signal 4G
    HTTP_TIMEOUT = 3,              // Timeout requête HTTP
    HTTP_NETWORK_ERROR = 4,        // Erreur réseau HTTP
    HTTP_INVALID_RESPONSE = 5,     // Réponse HTTP invalide
    SMS_SEND_FAILED = 6,           // Envoi SMS échoué
    TIME_SYNC_FAILED = 7,          // Sync heure échouée
    MODEM_CRASH = 8,               // Modem crashé
    REGISTRATION_FAILED = 9,       // Enregistrement réseau échoué
    DATA_CONNECTION_FAILED = 10,   // Connexion données échouée
    RECOVERY_IN_PROGRESS = 11,     // Récupération en cours
};
```

### 2. **API LTE pour les erreurs**

Dans la classe `LTE` :

```cpp
// Récupérer le dernier code d'erreur
NetworkErrorCode LTE::getLastError()

// Obtenir la description textuelle
static const char* LTE::getErrorString(NetworkErrorCode code)

// Vérifier s'il y a une erreur active
bool LTE::hasActiveError()

// Peut réessayer l'opération ?
bool LTE::shouldRetryOperation()

// Enregistrer manuellement une erreur
static void LTE::logNetworkError(NetworkErrorCode code, const char* app, const char* details)

// Effacer l'erreur après correction
static void LTE::clearNetworkError()
```

### 3. **NetworkErrorHandler - Helper pour les applis**

Situé dans `system/NetworkErrorHandler.h`, fournit des fonctions simplifiées :

```cpp
// Afficher une notification si erreur présente
NetworkErrorHandler::showIfError("App Name", "Opération effectuée");

// Peut réessayer ?
if (NetworkErrorHandler::canRetry()) { /* retry */ }

// Statut lisible du réseau
const char* status = NetworkErrorHandler::getNetworkStatus();
// Retourne : "OK - Réseau opérationnel", "Pas de signal", etc.

// Debug
NetworkErrorHandler::debugPrintLastError();
```

### 4. **Notification automatique**

Quand la classe `LTE` enregistre une erreur via `logNetworkError()`, elle notifie automatiquement 
via le `NotificationCenter` tant que la même erreur n'a pas été notifiée dans les 3 secondes 
(pour éviter le spam).

## Zones où les erreurs sont enregistrées

### HTTP
- HTTPINIT pas de réponse → `MODEM_CRASH`
- URL rejetée → `HTTP_NETWORK_ERROR`
- HTTPACTION timeout → `HTTP_TIMEOUT`
- Code HTTP != 200 → `HTTP_NETWORK_ERROR`
- HTTPREAD timeout → `HTTP_TIMEOUT`

### SMS
- Envoi échoué (+CMS ERROR) → `SMS_SEND_FAILED`

### Modem
- 5 timeouts consécutifs → `MODEM_NOT_RESPONDING`
- Modem injoignable au boot → `MODEM_NOT_RESPONDING`
- Récupération lancée → `RECOVERY_IN_PROGRESS`
- Récupération réussie → Erreur effacée

## Intégration dans les applications

### Exemple 1: WeatherApp

```cpp
#include "../system/NetworkErrorHandler.h"

// Dans update1() si pas de réseau :
if (!use_wifi && !use_lte) {
    NetworkErrorHandler::showIfError("Météo", "Aucune connexion réseau disponible");
    // ...
}

// Dans update() si erreur :
} else {
    NetworkErrorHandler::showIfError("Météo", "Impossible de récupérer les données");
    String error_msg = "Erreur: " + String(NetworkErrorHandler::getNetworkStatus());
    lv_label_set_text(lbl_desc, error_msg.c_str());
}
```

### Exemple 2: Vérifier avant de faire une requête

```cpp
// Si une opération réseau est en cours et a échoué
if (!NetworkErrorHandler::canRetry()) {
    // Ne pas faire de nouvelle tentative immédiatement
    return;
}

// Chercher des données
String result = LTE::httpGetBlocking(url);
if (!result.length()) {
    // La fonction LTE a déjà enregistré l'erreur
    NetworkErrorHandler::showIfError("MonApp", "Requête échouée");
}
```

### Exemple 3: Afficher le statut du réseau

```cpp
lv_label_set_text(lbl_status, NetworkErrorHandler::getNetworkStatus());
// Affiche : "OK - Réseau opérationnel" ou "Pas de signal 4G", etc.
```

## Logging

Toutes les erreurs réseau apparaissent dans le log LTE :
- Consulter via le Logger : `Logger::println()` 
- Fichier : `/lte_log.txt` (56 KB max)
- Visible en terminal Serial

Format :
```
[LTE ERROR] Description courte: détails supplémentaires
```

## Flux d'erreur complet

1. **LTE détecte une erreur** (ex: HTTPREAD timeout)
   ↓
2. **LTE appelle `logNetworkError()`** avec le code et détails
   ↓
3. **logNetworkError() fait 3 choses :**
   - Enregistre dans le log LTE
   - Notifie NotificationCenter (max 1x par 3 sec)
   - Stocke l'erreur dans `s_last_error`
   ↓
4. **NotificationCenter affiche la notification** à l'écran
   ↓
5. **L'app récupère l'erreur** via `LTE::getLastError()` ou `NetworkErrorHandler`
   ↓
6. **Après recovery réussi** : `LTE::clearNetworkError()` efface l'erreur

## Points clés

✅ **Anti-freeze** : Les timeouts n'ont pas de valeur infinies
✅ **Anti-spam** : Max 1 notification par 3 secondes pour la même erreur
✅ **Anti-crash** : Tous les appels réseau sont pris dans un try/catch implicite (watchdog)
✅ **Traçabilité** : Tous les détails loggés dans LTE.h (512 KB)
✅ **Retry smart** : `shouldRetryOperation()` attend avant de réessayer

## Tests

Pour tester le système :

1. Éteindre le modem physiquement (broche pin 26)
2. Observer les notifications "Récupération en cours..."
3. Rallumer le modem
4. Vérifier que "OK - Réseau opérationnel" apparaît

## Modifications futures possibles

- Implémenter un backoff exponentiel pour les retries
- Ajouter des statistiques (nombre d'erreurs par type)
- Historique des erreurs dans le ControlCenter
- Son d'alerte différent selon le type d'erreur

