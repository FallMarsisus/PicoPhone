#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include <lvgl.h>
#endif

#ifdef PIKASCRIPT

#include "PikaObj.h"
#include "BaseObj.h"
#include "pika_lvgl.h"
#include "pika_lvgl_lv_timer_t.h"

PikaEventListener* g_pika_lv_timer_event_listener;
void __pika_timer_cb(lv_timer_t* timer) {
    if (NULL == g_pika_lv_timer_event_listener) {
        /* App stopped, ignore stale timer */
        lv_timer_del(timer);
        return;
    }
    PikaObj* oTimer = newNormalObj(New_pika_lvgl_lv_timer_t);
    obj_setPtr(oTimer, "lv_timer", timer);
    pks_eventListener_send(g_pika_lv_timer_event_listener,
                           (uintptr_t)timer, arg_newObj(oTimer));
}

void pika_lvgl_lv_timer_t_set_period(PikaObj* self, int period) {
    lv_timer_t* lv_timer = obj_getPtr(self, "lv_timer");
    lv_timer_set_period(lv_timer, period);
}

void pika_lvgl_lv_timer_t_set_cb(PikaObj* self, Arg* cb) {
    lv_timer_t* lv_timer = obj_getPtr(self, "lv_timer");
    PikaObj* handler = newNormalObj(New_TinyObj);
    obj_setArg(handler, "eventCallBack", cb);
    lv_timer_set_cb(lv_timer, __pika_timer_cb);
    /* init event_listener for the first time */
    if (NULL == g_pika_lv_timer_event_listener) {
        pks_eventListener_init(&g_pika_lv_timer_event_listener);
    }
    pks_eventListener_registEvent(g_pika_lv_timer_event_listener,
                                  (uintptr_t)lv_timer, handler);
}

void pika_lvgl_lv_timer_t__del(PikaObj* self) {
    lv_timer_t* lv_timer = obj_getPtr(self, "lv_timer");
    lv_timer_del(lv_timer);
}
#endif
