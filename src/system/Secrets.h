#ifndef SYSTEM_SECRETS_H
#define SYSTEM_SECRETS_H

#ifdef __has_include
#if __has_include("Secrets.local.h")
#include "Secrets.local.h"
#endif
#endif

#ifndef TG_BOT_TOKEN
#define TG_BOT_TOKEN ""
#endif

#ifndef GEMINI_API_KEY
#define GEMINI_API_KEY ""
#endif

#ifndef OPENWEATHER_API_KEY
#define OPENWEATHER_API_KEY ""
#endif

#ifndef NEWS_API_KEY
#define NEWS_API_KEY ""
#endif

// --- BAMBU LAB MQTT ---
#ifndef BAMBU_DEVICE_ID
#define BAMBU_DEVICE_ID ""
#endif

#ifndef BAMBU_USER
#define BAMBU_USER ""
#endif

#ifndef BAMBU_PASS
#define BAMBU_PASS ""
#endif

#ifndef BAMBU_HOST
#define BAMBU_HOST "us.mqtt.bambulab.com"
#endif

#ifndef BAMBU_PORT
#define BAMBU_PORT 8883
#endif

#endif
