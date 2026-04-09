#pragma once

#include <Arduino.h>

#ifndef log_e
#define log_e(fmt, ...) do { if (Serial) { Serial.printf("[ES8311][E] " fmt "\n", ##__VA_ARGS__); } } while (0)
#endif

#ifndef log_w
#define log_w(fmt, ...) do { if (Serial) { Serial.printf("[ES8311][W] " fmt "\n", ##__VA_ARGS__); } } while (0)
#endif

#ifndef log_i
#define log_i(fmt, ...) do { if (Serial) { Serial.printf("[ES8311][I] " fmt "\n", ##__VA_ARGS__); } } while (0)
#endif

#ifndef log_d
#define log_d(fmt, ...) do { if (Serial) { Serial.printf("[ES8311][D] " fmt "\n", ##__VA_ARGS__); } } while (0)
#endif
