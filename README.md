# PicoPhone

PicoPhone is an open-source and DIY smartphone project built around the Raspberry Pi Pico 2 W microcontroller and an A7670E LTE communication module. Developed for the Arduino/PlatformIO ecosystem in C++ and Python, this repository contains all the modular source code necessary to run the system, its drivers, and its graphical interface.

## 🚀 Integrated Applications

The operating system embeds a wide variety of native applications:
*   **Telephony and Messaging:** Call management (`PhoneApp`), SMS (`SmsApp`), and a unified address book (`ContactsApp`, `UnifiedContacts.h`)
*   **Internet and Networks:** Connected applications including weather (`WeatherApp`), online radio (`WebRadioApp`), and a Telegram client (`TelegramApp`)
*   **Practical Utilities:** Calculator (`CalculatorApp`), timer (`TimerApp`), file explorer (`FileExplorerApp`), and a vector map (`VectorMapApp`)
*   **Connected Tools (IoT & Data):** Air quality monitoring (`AirQualityApp`), cryptocurrency rates (`CryptoApp`), Vélib' availability (`VelibApp`),
*   **Entertainment:** 2048 game (`Game2048App`), drawing application (`SketchApp`), virtual assistant (`ChatbotApp`), and an integrated GameBoy emulator via the Peanut GB plugin (`peanut_gb.h`)

## ⚙️ System and Graphical Interface (LVGL)

The user interface relies on the LVGL library, with deep Python integration:
*   **PikaScript (Embedded Python):** The project uses PikaScript to execute Python scripts (`main.py`, `PikaObj.pyi`) and interact directly with LVGL widgets via bindings generated in the `pikascript-api/` folder
*   **System Components:** The OS features a lock screen (`LockScreen.h`), a control center (`ControlCenter.h`), and a notification center (`NotificationCenter.h`)
*   **Touch Input:** A T9 keyboard implementation is available as a plugin (`lv_t9_keyboard.h`) to facilitate typing
*   **Network Management:** Dedicated modules handle LTE connectivity (`LTE.h`), Wi-Fi network memorization (`WifiStore.h`), as well as connection error management (`NetworkErrorHandler.h`, `NETWORK_ERROR_HANDLING.md`)

## 🛠️ Hardware Drivers

The code tree reveals support for several specific hardware components:
*   **Touch Screen:** Driven by the FT6336U controller (`FT6336U.h` and `FT6336U.cpp`)
*   **Audio:** Integration of the ES8311 I2S audio codec, with its complete library hosted in the `lib/ES8311_Codec/` directory
*   **Power:** Monitoring and management of the battery status (`Battery.h`)

## 💻 Development Environment

*   **PlatformIO:** The project is fully configured for the PlatformIO environment, as indicated by the presence of the `platformio.ini` file at the root
*   **Tools and Scripts:** The repository contains various utility scripts for compilation and data generation, such as `generate_map.py` and the `pikaCompiler` executable
*   **VS Code Configuration:** Editor-specific settings are included in the `.vscode/` folder (`settings.json`, `tasks.json`)
