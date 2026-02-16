#ifndef SYSTEM_BACKGROUND_SERVICES_H
#define SYSTEM_BACKGROUND_SERVICES_H

#include <Arduino.h>

class IBackgroundService {
public:
    virtual ~IBackgroundService() {}
    virtual const char* name() const = 0;
    virtual void begin() {}
    virtual void update() {}
    virtual void update1() {}
};

class BackgroundServices {
private:
    static constexpr uint8_t MAX_SERVICES = 8;
    IBackgroundService* services[MAX_SERVICES]{};
    uint8_t count = 0;
    bool started = false;

public:
    bool registerService(IBackgroundService* service) {
        if (!service) return false;
        if (count >= MAX_SERVICES) return false;
        services[count++] = service;
        return true;
    }

    void begin() {
        if (started) return;
        started = true;
        for (uint8_t i = 0; i < count; i++) {
            services[i]->begin();
        }
    }

    void update() {
        if (!started) return;
        for (uint8_t i = 0; i < count; i++) {
            services[i]->update();
        }
    }

    void update1() {
        if (!started) return;
        for (uint8_t i = 0; i < count; i++) {
            services[i]->update1();
        }
    }
};

namespace background_services {
    inline BackgroundServices& manager() {
        static BackgroundServices m;
        return m;
    }
}

#endif
