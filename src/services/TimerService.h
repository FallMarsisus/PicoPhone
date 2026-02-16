#ifndef SERVICES_TIMER_SERVICE_H
#define SERVICES_TIMER_SERVICE_H

#include <Arduino.h>
#include "../system/BackgroundServices.h"
#include "../system/NotificationCenter.h"

class TimerService : public IBackgroundService {
private:
    bool running = false;
    unsigned long end_at = 0;
    unsigned long duration_ms = 0;
    bool notified = false;

public:
    const char* name() const override { return "TimerService"; }

    void startTimer(uint32_t seconds) {
        if (seconds == 0) return;
        duration_ms = seconds * 1000UL;
        end_at = millis() + duration_ms;
        running = true;
        notified = false;
    }

    void stopTimer() {
        running = false;
        notified = false;
        end_at = 0;
        duration_ms = 0;
    }

    bool isRunning() const { return running; }

    uint32_t remainingSeconds() const {
        if (!running) return 0;
        unsigned long now = millis();
        if (now >= end_at) return 0;
        return (uint32_t)((end_at - now + 999) / 1000UL);
    }

    uint32_t totalSeconds() const {
        return duration_ms / 1000UL;
    }

    void update() override {
        if (!running || notified) return;
        if ((long)(millis() - end_at) >= 0) {
            notified = true;
            running = false;
            notifications::push("Timer", "Time is up", "Your timer has finished");
        }
    }
};

namespace timer_service {
    inline TimerService& instance() {
        static TimerService s;
        return s;
    }
}

#endif
