/**
 * @file lv_t9_keyboard.c
 */

#include "lv_t9_keyboard.h"
#include <string.h>

/*********************
 * DEFINES
 *********************/
#define KB_HEIGHT_PERCENT 60
#define TOP_BAR_HEIGHT    45

/*********************
 * STATIC VARIABLES
 *********************/

static bool shift_active = false;

// Map principale
static const char *t9_main_map[] = {
    "1", "2 abc", "3 def", "\n",
    "4 ghi", "5 jkl", "6 mno", "\n",
    "7 pqrs", "8 tuv", "9 wxyz", "\n",
    LV_SYMBOL_UP, "0", LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, "" 
};

// Map "Vide" : On met un espace insécable ou vide pour garder la structure sans bug graphique
static const char *map_empty[] = { "" };

// --- MAPS MINUSCULES ---
static const char *map_1[]     = {"1", ".", ",", "!", "?", "@", "_", ""};
static const char *map_2_lc[]  = {"a", "b", "c", "2", ""};
static const char *map_3_lc[]  = {"d", "e", "f", "3", ""};
static const char *map_4_lc[]  = {"g", "h", "i", "4", ""};
static const char *map_5_lc[]  = {"j", "k", "l", "5", ""};
static const char *map_6_lc[]  = {"m", "n", "o", "6", ""};
static const char *map_7_lc[]  = {"p", "q", "r", "s", "7", ""};
static const char *map_8_lc[]  = {"t", "u", "v", "8", ""};
static const char *map_9_lc[]  = {"w", "x", "y", "z", "9", ""};
static const char *map_0[]     = {" ", "0", "-", "+", "=", "/", ""};

// --- MAPS MAJUSCULES ---
static const char *map_2_uc[]  = {"A", "B", "C", "2", ""};
static const char *map_3_uc[]  = {"D", "E", "F", "3", ""};
static const char *map_4_uc[]  = {"G", "H", "I", "4", ""};
static const char *map_5_uc[]  = {"J", "K", "L", "5", ""};
static const char *map_6_uc[]  = {"M", "N", "O", "6", ""};
static const char *map_7_uc[]  = {"P", "Q", "R", "S", "7", ""};
static const char *map_8_uc[]  = {"T", "U", "V", "8", ""};
static const char *map_9_uc[]  = {"W", "X", "Y", "Z", "9", ""};

/*********************
 * STATIC PROTOTYPES
 *********************/
static void main_kb_event_cb(lv_event_t *e);
static void suggestion_kb_event_cb(lv_event_t *e);
static void close_kb_event_cb(lv_event_t *e);

/*********************
 * GLOBAL FUNCTIONS
 *********************/

lv_obj_t *lv_t9_kb_create(lv_obj_t *parent)
{
    shift_active = false;

    // 1. Conteneur GLOBAL
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, lv_pct(100), lv_pct(KB_HEIGHT_PERCENT));
    lv_obj_align(container, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_pad_gap(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(container, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_user_data(container, NULL);

    // ========================================================
    // ZONE DU HAUT : [ SUGGESTIONS ] [ X ]
    // ========================================================
    lv_obj_t *top_row = lv_obj_create(container);
    // HAUTEUR FIXE IMPERATIVE pour éviter que ça disparaisse
    lv_obj_set_size(top_row, lv_pct(100), TOP_BAR_HEIGHT);
    
    
    lv_obj_set_style_bg_opa(top_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_row, 0, 0);
    lv_obj_set_style_pad_all(top_row, 2, 0);
    lv_obj_set_style_pad_gap(top_row, 5, 0);
    lv_obj_clear_flag(top_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(top_row, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top_row, LV_FLEX_FLOW_ROW);

    // 2.a Matrice de Suggestions (Child 0 de top_row)
    lv_obj_t *suggestion_btnm = lv_btnmatrix_create(top_row);
    lv_obj_set_flex_grow(suggestion_btnm, 1);
    lv_obj_set_height(suggestion_btnm, lv_pct(100)); // Force la hauteur
    
    // Style
    lv_obj_set_style_bg_opa(suggestion_btnm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(suggestion_btnm, 0, 0);
    lv_obj_set_style_pad_all(suggestion_btnm, 0, 0);
    lv_obj_set_style_radius(suggestion_btnm, 5, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(suggestion_btnm, lv_color_hex(0x505050), LV_PART_ITEMS);
    lv_obj_set_style_text_color(suggestion_btnm, lv_color_white(), LV_PART_ITEMS);

    // Initialisation map vide
    lv_btnmatrix_set_map(suggestion_btnm, map_empty);
    // IMPORTANT : On ne cache JAMAIS l'objet, on met juste une map vide
    lv_obj_clear_flag(suggestion_btnm, LV_OBJ_FLAG_HIDDEN); 
    
    lv_obj_add_event_cb(suggestion_btnm, suggestion_kb_event_cb, LV_EVENT_VALUE_CHANGED, container);

    // 2.b Bouton Fermer (Child 1 de top_row)
    lv_obj_t *close_btn = lv_btn_create(top_row);
    lv_obj_set_size(close_btn, 45, lv_pct(100));
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0xFF3B30), 0);
    lv_obj_set_style_radius(close_btn, 5, 0);
    lv_obj_add_event_cb(close_btn, close_kb_event_cb, LV_EVENT_CLICKED, container);

    lv_obj_t *lbl_close = lv_label_create(close_btn);
    lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl_close);

    // ========================================================
    // ZONE DU BAS : CLAVIER PRINCIPAL (Child 1 de container)
    // ========================================================
    lv_obj_t *main_btnm = lv_btnmatrix_create(container);
    lv_obj_set_width(main_btnm, lv_pct(100));
    lv_obj_set_flex_grow(main_btnm, 1);
    
    lv_btnmatrix_set_map(main_btnm, t9_main_map);
    lv_obj_add_event_cb(main_btnm, main_kb_event_cb, LV_EVENT_VALUE_CHANGED, container);

    // Style
    lv_obj_set_style_pad_all(main_btnm, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(main_btnm, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(main_btnm, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(main_btnm, LV_OPA_TRANSP, LV_PART_MAIN);
    
    lv_obj_set_style_radius(main_btnm, 8, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(main_btnm, lv_color_hex(0x2C2C2E), LV_PART_ITEMS);
    lv_obj_set_style_text_color(main_btnm, lv_color_white(), LV_PART_ITEMS);
    
    // Shift Checkable
    lv_btnmatrix_set_btn_ctrl(main_btnm, 12, LV_BTNMATRIX_CTRL_CHECKABLE);

    return container;
}

void lv_t9_kb_set_textarea(lv_obj_t *kb, lv_obj_t *ta)
{
    if(kb == NULL) return;
    
    // 1. Liaison Textarea
    lv_obj_set_user_data(kb, ta);
    lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN); // Affiche le conteneur principal
    
    // 2. Reset Shift
    shift_active = false;
    // Index 1 = Clavier Principal
    lv_obj_t *main_btnm = lv_obj_get_child(kb, 1); 
    if(main_btnm) {
        lv_btnmatrix_clear_btn_ctrl(main_btnm, 12, LV_BTNMATRIX_CTRL_CHECKED);
    }

    // 3. Reset Suggestions & FIX BARRE DISPARUE
    // Index 0 = Top Row
    lv_obj_t *top_row = lv_obj_get_child(kb, 0);
    if(top_row) {
        // On s'assure que la barre du haut est visible
        lv_obj_clear_flag(top_row, LV_OBJ_FLAG_HIDDEN);

        // Index 0 = Suggestion Matrix
        lv_obj_t *suggestion_btnm = lv_obj_get_child(top_row, 0);
        if(suggestion_btnm) {
            lv_obj_clear_flag(suggestion_btnm, LV_OBJ_FLAG_HIDDEN);
            // Nettoyage complet des carrés blancs
            lv_btnmatrix_clear_btn_ctrl_all(suggestion_btnm, LV_BTNMATRIX_CTRL_CHECKED | LV_BTNMATRIX_CTRL_DISABLED | LV_BTNMATRIX_CTRL_HIDDEN);
            lv_btnmatrix_set_map(suggestion_btnm, map_empty);
        }
    }
}

/*********************
 * STATIC FUNCTIONS
 *********************/

static void close_kb_event_cb(lv_event_t *e) {
    lv_obj_t *container = lv_event_get_user_data(e);
    lv_event_send(container, LV_EVENT_CANCEL, NULL);
}

static void main_kb_event_cb(lv_event_t *e)
{
    lv_obj_t *btnm = lv_event_get_target(e);
    lv_obj_t *container = lv_event_get_user_data(e);
    lv_obj_t *ta = lv_obj_get_user_data(container);
    
    lv_obj_t *top_row = lv_obj_get_child(container, 0);
    lv_obj_t *suggestion_btnm = lv_obj_get_child(top_row, 0);

    uint32_t btn_id = lv_btnmatrix_get_selected_btn(btnm);
    const char *txt = lv_btnmatrix_get_btn_text(btnm, btn_id);

    if (txt == NULL) return;

    // --- SHIFT ---
    if (strcmp(txt, LV_SYMBOL_UP) == 0) {
        shift_active = !shift_active;
        if(shift_active) lv_btnmatrix_set_btn_ctrl(btnm, btn_id, LV_BTNMATRIX_CTRL_CHECKED);
        else lv_btnmatrix_clear_btn_ctrl(btnm, btn_id, LV_BTNMATRIX_CTRL_CHECKED);
        
        // Reset suggestions si on change de mode
        lv_btnmatrix_set_map(suggestion_btnm, map_empty);
        return;
    }
    else if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        if(ta) lv_textarea_del_char(ta);
        lv_btnmatrix_set_map(suggestion_btnm, map_empty);
        return;
    } 
    else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
        lv_event_send(container, LV_EVENT_READY, NULL);
        return;
    }

    // --- T9 LOGIQUE ---
    const char **map_to_set = NULL;
    char first_char = txt[0];
    
    switch(first_char) {
        case '1': map_to_set = map_1; break;
        case '2': map_to_set = shift_active ? map_2_uc : map_2_lc; break;
        case '3': map_to_set = shift_active ? map_3_uc : map_3_lc; break;
        case '4': map_to_set = shift_active ? map_4_uc : map_4_lc; break;
        case '5': map_to_set = shift_active ? map_5_uc : map_5_lc; break;
        case '6': map_to_set = shift_active ? map_6_uc : map_6_lc; break;
        case '7': map_to_set = shift_active ? map_7_uc : map_7_lc; break;
        case '8': map_to_set = shift_active ? map_8_uc : map_8_lc; break;
        case '9': map_to_set = shift_active ? map_9_uc : map_9_lc; break;
        case '0': map_to_set = map_0; break;
        default: return;
    }

    if (map_to_set) {
        // FIX : Invalider l'objet pour forcer le redessin et éviter les artefacts
        lv_obj_invalidate(suggestion_btnm);
        // On s'assure que c'est propre
        lv_btnmatrix_clear_btn_ctrl_all(suggestion_btnm, LV_BTNMATRIX_CTRL_CHECKED | LV_BTNMATRIX_CTRL_DISABLED | LV_BTNMATRIX_CTRL_HIDDEN);
        lv_btnmatrix_set_map(suggestion_btnm, map_to_set);
    }
}

static void suggestion_kb_event_cb(lv_event_t *e)
{
    lv_obj_t *btnm = lv_event_get_target(e);
    lv_obj_t *container = lv_event_get_user_data(e);
    lv_obj_t *ta = lv_obj_get_user_data(container);
    lv_obj_t *main_btnm = lv_obj_get_child(container, 1); 

    uint32_t btn_id = lv_btnmatrix_get_selected_btn(btnm);
    const char *txt = lv_btnmatrix_get_btn_text(btnm, btn_id);

    if (txt && ta && strlen(txt) > 0) {
        lv_textarea_add_text(ta, txt);
    }
    
    // Reset map vide
    lv_btnmatrix_set_map(btnm, map_empty);

    // Auto un-shift
    if (shift_active) {
        shift_active = false;
        lv_btnmatrix_clear_btn_ctrl(main_btnm, 12, LV_BTNMATRIX_CTRL_CHECKED);
    }
}