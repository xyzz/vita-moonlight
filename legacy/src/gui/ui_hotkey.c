// ui_hotkey.c
// Sistema de hotkeys personalizables para Moonlight Vita Motion
// Solo funciones de menú y helpers UI, sin lógica de almacenamiento ni callbacks
#include "ui_hotkey.h"
#include "ui_settings.h"
#include "guilib.h"
#include "ime.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Prototipo para strdup si falta
char *strdup(const char *s);

// Prototipo para nombre_boton (debe estar implementada en algún lado)
void nombre_boton(int btn, char* out, int outlen);

// Implementación de nombre_boton para evitar error de linker y mostrar nombres correctos
void nombre_boton(int btn, char* out, int outlen) {
    const char* name = "?";
    switch (btn) {
        case SCE_CTRL_START: name = "START"; break;
        case SCE_CTRL_SELECT: name = "SELECT"; break;
        case SCE_CTRL_UP: name = "UP"; break;
        case SCE_CTRL_DOWN: name = "DOWN"; break;
        case SCE_CTRL_LEFT: name = "LEFT"; break;
        case SCE_CTRL_RIGHT: name = "RIGHT"; break;
        case SCE_CTRL_L1: name = "L1"; break;
        case SCE_CTRL_R1: name = "R1"; break;
        case SCE_CTRL_L2: name = "L2"; break;
        case SCE_CTRL_R2: name = "R2"; break;
        case SCE_CTRL_L3: name = "L3"; break;
        case SCE_CTRL_R3: name = "R3"; break;
        case SCE_CTRL_TRIANGLE: name = "TRIANGLE"; break;
        case SCE_CTRL_CIRCLE: name = "CIRCLE"; break;
        case SCE_CTRL_CROSS: name = "CROSS"; break;
        case SCE_CTRL_SQUARE: name = "SQUARE"; break;
        default: break;
    }
    snprintf(out, outlen, "%s", name);
}

// Declaraciones con atributo unused para evitar warnings
static int hotkey_edit_menu(int idx) __attribute__((unused));
static int hotkey_buttons_menu(SceCtrlButtons* out_buttons, int* out_count) __attribute__((unused));

// helpers para obtener el nombre de la función y botones
void hotkey_func_name(int idx, char* out, int outlen) {
    extern char *settings_special_names[];
    if (idx >= 0 && out && outlen > 0) {
        strncpy(out, settings_special_names[idx], outlen-1);
        out[outlen-1] = 0;
    } else {
        snprintf(out, outlen, "Func %d", idx);
    }
}

void hotkey_buttons_name(const hotkey_t* hk, char* out, int outlen) {
    char* ptr = out;
    int left = outlen-1;
    for (int i = 0; i < hk->num_buttons; ++i) {
        const char* name = NULL;
        switch (hk->buttons[i]) {
            case SCE_CTRL_START: name = "START"; break;
            case SCE_CTRL_SELECT: name = "SELECT"; break;
            case SCE_CTRL_UP: name = "UP"; break;
            case SCE_CTRL_DOWN: name = "DOWN"; break;
            case SCE_CTRL_LEFT: name = "LEFT"; break;
            case SCE_CTRL_RIGHT: name = "RIGHT"; break;
            case SCE_CTRL_L1: name = "L1"; break;
            case SCE_CTRL_R1: name = "R1"; break;
            case SCE_CTRL_L2: name = "L2"; break;
            case SCE_CTRL_R2: name = "R2"; break;
            case SCE_CTRL_L3: name = "L3"; break;
            case SCE_CTRL_R3: name = "R3"; break;
            case SCE_CTRL_TRIANGLE: name = "TRIANGLE"; break;
            case SCE_CTRL_CIRCLE: name = "CIRCLE"; break;
            case SCE_CTRL_CROSS: name = "CROSS"; break;
            case SCE_CTRL_SQUARE: name = "SQUARE"; break;
            // Elimina SCE_CTRL_PS para evitar duplicado
            default: name = "?"; break;
        }
        int n = snprintf(ptr, left, "%s%s", (i>0)?"+":"", name);
        ptr += n; left -= n;
        if (left <= 0) break;
    }
    *ptr = 0;
}

static int hotkey_func_menu(int* func_idx) {
    extern char *settings_special_names[];
    int count = 0;
    while (settings_special_names[count]) ++count;
    menu_entry menu[count+2];
    int idx = 0;
    for (int i = 0; i < count; ++i) {
        menu[idx++] = (menu_entry){ .name = settings_special_names[i], .id = i };
    }
    menu[idx++] = (menu_entry){ .name = "Cancelar", .id = -1 };
    int sel = display_menu(menu, idx, NULL, NULL, NULL, NULL, NULL);
    if (sel >= 0 && sel < count) {
        *func_idx = sel;
        return 1;
    }
    return 0;
}

// Definición normal, sin atributo
static int hotkey_buttons_menu(SceCtrlButtons* out_buttons, int* out_count) {
    display_error("Mantén pulsados los botones para el hotkey y pulsa START para confirmar.");
    // ...lógica de UI para capturar botones...
    return 1;
}

static int hotkey_edit_menu(int idx) {
    extern hotkey_t g_hotkeys[];
    hotkey_t* hk = &g_hotkeys[idx];
    char funcname[256], btns[256];
    hotkey_func_name(hk->function, funcname, sizeof(funcname));
    hotkey_buttons_name(hk, btns, sizeof(btns));
    menu_entry menu[4];
    int m = 0;
    menu[m++] = (menu_entry){ .name = "Cambiar función", .id = 1, .subname = "" };
    menu[m++] = (menu_entry){ .name = "Cambiar combinación", .id = 2, .subname = "" };
    menu[m++] = (menu_entry){ .name = "Eliminar", .id = 3 };
    menu[m++] = (menu_entry){ .name = "Volver", .id = 0 };
    int sel = display_menu(menu, m, NULL, NULL, NULL, NULL, NULL);
    if (sel == 1) {
        int fidx = hk->function;
        if (hotkey_func_menu(&fidx)) hk->function = fidx;
        return 1;
    } else if (sel == 2) {
        SceCtrlButtons btns[HOTKEY_MAX_BUTTONS]; int cnt=0;
        if (hotkey_buttons_menu(btns, &cnt)) {
            memcpy(hk->buttons, btns, sizeof(SceCtrlButtons)*cnt);
            hk->num_buttons = cnt;
        }
        return 1;
    } else if (sel == 3) {
        for (int i = idx; i < g_hotkey_count-1; ++i) g_hotkeys[i] = g_hotkeys[i+1];
        g_hotkey_count--;
        return 2;
    }
    return 0;
}

// Declaración de HOTKEY_MENU_MAX
#define HOTKEY_MENU_MAX 16

// --- NUEVO SUBMENÚ PARA AGREGAR HOTKEY ---
int add_hotkey_menu();

int add_hotkey_menu() {
    extern hotkey_t g_hotkeys[];
    extern int g_hotkey_count;
    int first_btn = -1, second_btn = -1, action = -1;
    int done = 0;
    while (!done) {
        menu_entry menu[8] = {0};
        int idx = 0;
        // Mostrar selección actual
        char btn1[32] = "No asignado", btn2[32] = "Ninguno", act[64] = "No asignada";
        if (first_btn >= 0) nombre_boton(first_btn, btn1, sizeof(btn1));
        if (second_btn >= 0) nombre_boton(second_btn, btn2, sizeof(btn2));
        if (action >= 0) hotkey_func_name(action, act, sizeof(act));
        menu[idx].name = "Primer botón";
        strncpy(menu[idx].subname, btn1, sizeof(menu[idx].subname));
        menu[idx].id = 1;
        idx++;
        menu[idx].name = "Segundo botón (opcional)";
        strncpy(menu[idx].subname, btn2, sizeof(menu[idx].subname));
        menu[idx].id = 2;
        idx++;
        menu[idx].name = "Acción";
        strncpy(menu[idx].subname, act, sizeof(menu[idx].subname));
        menu[idx].id = 3;
        idx++;
        menu[idx++] = (menu_entry){ .name = "Guardar atajo", .id = 4, .disabled = (first_btn < 0 || action < 0) };
        menu[idx++] = (menu_entry){ .name = "Volver", .id = 0 };
        int sel = display_menu(menu, idx, NULL, NULL, NULL, NULL, NULL);
        if (sel == 1) {
            // Selección de primer botón
            menu_entry btn_menu[18];
            int bidx = 0;
            const char* btn_names[] = {"START","SELECT","UP","DOWN","LEFT","RIGHT","L1","R1","L2","R2","L3","R3","TRIANGLE","CIRCLE","CROSS","SQUARE"};
            int btn_vals[] = {SCE_CTRL_START,SCE_CTRL_SELECT,SCE_CTRL_UP,SCE_CTRL_DOWN,SCE_CTRL_LEFT,SCE_CTRL_RIGHT,SCE_CTRL_L1,SCE_CTRL_R1,SCE_CTRL_L2,SCE_CTRL_R2,SCE_CTRL_L3,SCE_CTRL_R3,SCE_CTRL_TRIANGLE,SCE_CTRL_CIRCLE,SCE_CTRL_CROSS,SCE_CTRL_SQUARE};
            for (int i = 0; i < 16; ++i) btn_menu[bidx++] = (menu_entry){ .name = (char*)btn_names[i], .id = btn_vals[i] };
            int bsel = display_menu(btn_menu, bidx, NULL, NULL, NULL, NULL, NULL);
            if (bsel >= 0) first_btn = btn_menu[bsel].id;
        } else if (sel == 2) {
            // Selección de segundo botón (opcional)
            menu_entry btn_menu[18];
            int bidx = 0;
            btn_menu[bidx++] = (menu_entry){ .name = "Ninguno", .id = -1 };
            const char* btn_names[] = {"START","SELECT","UP","DOWN","LEFT","RIGHT","L1","R1","L2","R2","L3","R3","TRIANGLE","CIRCLE","CROSS","SQUARE"};
            int btn_vals[] = {SCE_CTRL_START,SCE_CTRL_SELECT,SCE_CTRL_UP,SCE_CTRL_DOWN,SCE_CTRL_LEFT,SCE_CTRL_RIGHT,SCE_CTRL_L1,SCE_CTRL_R1,SCE_CTRL_L2,SCE_CTRL_R2,SCE_CTRL_L3,SCE_CTRL_R3,SCE_CTRL_TRIANGLE,SCE_CTRL_CIRCLE,SCE_CTRL_CROSS,SCE_CTRL_SQUARE};
            for (int i = 0; i < 16; ++i) btn_menu[bidx++] = (menu_entry){ .name = (char*)btn_names[i], .id = btn_vals[i] };
            int bsel = display_menu(btn_menu, bidx, NULL, NULL, NULL, NULL, NULL);
            if (bsel == 0) second_btn = -1;
            else if (bsel > 0) second_btn = btn_menu[bsel].id;
        } else if (sel == 3) {
            // Selección de acción
            int fidx = action;
            if (hotkey_func_menu(&fidx)) action = fidx;
        } else if (sel == 4) {
            // Guardar atajo
            if (first_btn < 0 || action < 0) {
                display_error("Debes seleccionar al menos un botón y una acción.");
            } else if (g_hotkey_count < MAX_HOTKEYS) {
                g_hotkeys[g_hotkey_count].buttons[0] = first_btn;
                g_hotkeys[g_hotkey_count].num_buttons = 1;
                if (second_btn >= 0) {
                    g_hotkeys[g_hotkey_count].buttons[1] = second_btn;
                    g_hotkeys[g_hotkey_count].num_buttons = 2;
                }
                g_hotkeys[g_hotkey_count].function = action;
                g_hotkey_count++;
                hotkey_save_config();
                flash_message("Atajo guardado correctamente.");
                done = 1;
            }
        } else if (sel == 0 || sel == -1) {
            // Volver
            return 0;
        }
    }
    return 1;
}

int ui_hotkey_menu() {
    extern hotkey_t g_hotkeys[];
    extern int g_hotkey_count;
    char funcname[128], btns[128];
    int exit_menu = 0;
    while (!exit_menu) {
        menu_entry menu[HOTKEY_MENU_MAX+8] = {0};
        int idx = 0;
        // --- Resumen decorativo como item de menú deshabilitado ---
        if (g_hotkey_count == 0) {
            menu[idx].name = "-----------------------------";
            menu[idx].disabled = true;
            idx++;
            menu[idx].name = "No hay atajos, añade uno";
            menu[idx].disabled = true;
            idx++;
            menu[idx].name = "-----------------------------";
            menu[idx].disabled = true;
            idx++;
        } else {
            menu[idx].name = "-----------------------------";
            menu[idx].disabled = true;
            idx++;
            for (int i = 0; i < g_hotkey_count; ++i) {
                hotkey_func_name(g_hotkeys[i].function, funcname, sizeof(funcname));
                hotkey_buttons_name(&g_hotkeys[i], btns, sizeof(btns));
                static char resumen_lineas[HOTKEY_MENU_MAX][512];
                snprintf(resumen_lineas[i], sizeof(resumen_lineas[i]), "%s = %s", btns, funcname);
                menu[idx].name = resumen_lineas[i];
                menu[idx].disabled = true;
                idx++;
            }
            menu[idx].name = "-----------------------------";
            menu[idx].disabled = true;
            idx++;
        }
        // Opciones principales
        menu[idx++] = (menu_entry){ .name = "Agregar nuevo atajo", .id = 100 };
        if (g_hotkey_count > 0) {
            menu[idx++] = (menu_entry){ .name = "Eliminar atajo", .id = 200 };
        }
        menu[idx++] = (menu_entry){ .name = "Volver", .id = -1 };

        int sel = display_menu(menu, idx, NULL, NULL, NULL, NULL, NULL);
        if (sel == 100) {
            add_hotkey_menu();
        } else if (sel == 200) {
            // Eliminar: muestra lista de atajos para elegir cuál borrar
            menu_entry del_menu[HOTKEY_MENU_MAX+1] = {0};
            int didx = 0;
            for (int i = 0; i < g_hotkey_count; ++i) {
                hotkey_func_name(g_hotkeys[i].function, funcname, sizeof(funcname));
                hotkey_buttons_name(&g_hotkeys[i], btns, sizeof(btns));
                char nombre[512];
                snprintf(nombre, sizeof(nombre), "%s = %s", btns, funcname);
                del_menu[didx].name = strdup(nombre);
                del_menu[didx].id = i;
                ++didx;
            }
            del_menu[didx++] = (menu_entry){ .name = "Cancelar", .id = -1 };
            int which = display_menu(del_menu, didx, NULL, NULL, NULL, NULL, NULL);
            if (which >= 0 && which < g_hotkey_count) {
                for (int i = which; i < g_hotkey_count-1; ++i) g_hotkeys[i] = g_hotkeys[i+1];
                g_hotkey_count--;
            }
        } else if (sel == -1) {
            return 0;
        }
    }
    return 0;
}
