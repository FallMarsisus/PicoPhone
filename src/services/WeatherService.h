#ifndef SERVICES_WEATHER_SERVICE_H
#define SERVICES_WEATHER_SERVICE_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../system/BackgroundServices.h"
#include "../system/LTE.h"
#include "../system/Logger.h"

class WeatherService : public IBackgroundService {
private:
    String temp = "--°C";
    String city = "En attente";
    unsigned long last_fetch_ms = 0;
    unsigned long last_try_ms = 0;
    bool pending_fetch = true;

public:
    const char* name() const override { return "Weather"; }

    void update1() override {
        if (!LTE::isReadyForData()) return;

        unsigned long now = millis();
        
        // Si echec (pending), on retente toutes les 60s max. Sinon, toutes les heures.
        bool should_fetch = pending_fetch ? (now - last_try_ms > 60000UL) : (now - last_fetch_ms > 3600000UL);
        if (last_try_ms == 0) should_fetch = true; // Premier demarrage

        if (should_fetch) { 
            last_try_ms = now;
            
            // Utilisation de l'API OpenWeatherMap (identique a WeatherApp)
            String url = "http://api.openweathermap.org/data/2.5/weather?q=Paris,fr&appid=8fdaebc1c5f040d39d2178f811adfeaa&units=metric&lang=fr";
            String response = LTE::httpGetBlocking(url, "", false, true);
            if (response.length() > 0) {
                JsonDocument doc;
                DeserializationError err = deserializeJson(doc, response);
                if (!err) {
                    float t = doc["main"]["temp"] | 0.0f;
                    temp = String((int)round(t)) + "°C";
                    city = doc["name"] | "Paris";
                    Logger::printf("[Weather] Temp mise a jour: %s, Ville: %s\n", temp.c_str(), city.c_str());
                    
                    last_fetch_ms = now;
                    pending_fetch = false; // Succes
                } else {
                    pending_fetch = true;  // Erreur JSON, on retentera dans 1 min
                }
            } else {
                pending_fetch = true;      // Echec requete HTTP
            }
        }
    }

    String getTemp() const { return temp; }
    String getCity() const { return city; }
};

namespace weather_service { inline WeatherService& instance() { static WeatherService svc; return svc; } }
#endif