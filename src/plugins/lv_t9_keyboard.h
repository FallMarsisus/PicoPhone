/**
 * @file lv_t9_keyboard.h
 * Clavier T9 réutilisable pour LVGL v8+
 */

#ifndef LV_T9_KEYBOARD_H
#define LV_T9_KEYBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * Crée un clavier T9.
 * @param parent L'objet parent (généralement lv_scr_act() ou un conteneur/fenêtre).
 * @return Pointeur vers l'objet conteneur du clavier.
 */
lv_obj_t *lv_t9_kb_create(lv_obj_t *parent);

/**
 * Associe le clavier à une zone de texte (TextArea).
 * @param kb L'objet clavier T9.
 * @param ta L'objet TextArea à contrôler.
 */
void lv_t9_kb_set_textarea(lv_obj_t *kb, lv_obj_t *ta);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_T9_KEYBOARD_H*/


/*
USAGE EXAMPLE:

#include "lvgl.h"
#include "lv_t9_keyboard.h"

// Variables globales (ou dans une structure app)
lv_obj_t * my_textarea;
lv_obj_t * my_t9_kb;

// Callback quand on clique sur la zone de texte
static void ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        // 1. On montre le clavier
        lv_obj_clear_flag(my_t9_kb, LV_OBJ_FLAG_HIDDEN);
        
        // 2. On dit au clavier : "Maintenant, tu écris dans CE text area"
        lv_t9_kb_set_textarea(my_t9_kb, ta);
    }
}

void create_demo_ui(void) {
    // 1. Créer une zone de texte
    my_textarea = lv_textarea_create(lv_scr_act());
    lv_obj_align(my_textarea, LV_ALIGN_TOP_MID, 0, 50);
    lv_textarea_set_placeholder_text(my_textarea, "Cliquez ici...");
    lv_obj_add_event_cb(my_textarea, ta_event_cb, LV_EVENT_ALL, NULL);

    // 2. Créer le clavier T9 (une seule fois)
    // On le crée attaché à l'écran actif
    my_t9_kb = lv_t9_kb_create(lv_scr_act());
    
    // 3. Le cacher par défaut (optionnel, selon design)
    lv_obj_add_flag(my_t9_kb, LV_OBJ_FLAG_HIDDEN);
}

*/