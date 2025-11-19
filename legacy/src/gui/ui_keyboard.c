#include "ui_keyboard.h"
#include "../input/keyboardkeys.h"
#include "ui_settings.h"
#include "guilib.h"
#include "../config.h"
#include <psp2/kernel/clib.h>

static const char* keyboard_layout_names[] = {
    "Ingles (US)",
    "Espanol (Espana)",
    "Espanol (Latinoamerica)",
};

static int keyboard_layout_loop(int id, void *context, const input_data *input) {
    if ((input->buttons & config.btn_confirm) && !(input->buttons & SCE_CTRL_HOLD)) {
        // Selecciona el layout
        keyboardsystem_set_layout((KeyboardLayout)id);
        config.keyboard_layout = id;
        sceClibPrintf("[Moonlight] Layout de teclado seleccionado: %s (id=%d)\n", keyboard_layout_names[id], id);
        ui_settings_save_config();
        return 1; // salir del menú
    }
    return 0;
}

int keyboard_layout_menu(void) {
    // Sincroniza el layout global con la config antes de mostrar el menú
    keyboardsystem_set_layout((KeyboardLayout)config.keyboard_layout);
    menu_entry menu[KB_LAYOUT_COUNT] = {0}; // Inicializa todo a cero
    for (int i = 0; i < KB_LAYOUT_COUNT; ++i) {
        menu[i].name = (char*)keyboard_layout_names[i];
        menu[i].id = i;
        menu[i].disabled = false;
        menu[i].suffix = (i == config.keyboard_layout) ? "*" : "";
    }
    menu_geom geom = make_geom_centered(320, 120);
    int sel = display_menu(menu, KB_LAYOUT_COUNT, &geom, &keyboard_layout_loop, NULL, NULL, menu);
    // El layout ya se aplica en el loop, aquí solo refrescamos el menú principal si es necesario
    (void)sel; // Evita warning de variable no usada
    return 0;
}