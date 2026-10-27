#include "ui_keyboard.h"

#include <psp2/kernel/clib.h>

#include "../config.h"
#include "../input/keyboardkeys.h"
#include "guilib.h"
#include "ui_settings.h"

static const char* keyboard_layout_names[] = {
    "Ingles (US)",
    "Espanol (Espana)",
    "Espanol (Latinoamerica)",
};

static int keyboard_layout_loop(int id, void* context, const input_data* input)
{
    if ((input->buttons & config.btn_confirm) && !(input->buttons & SCE_CTRL_HOLD))
    {
        keyboardsystem_set_layout((KeyboardLayout)id);
        config.keyboard_layout = id;
        sceClibPrintf("[Moonlight] Selected keyboard layout: %s (id=%d)\n", keyboard_layout_names[id], id);
        ui_settings_save_config();
        return 1;
    }
    return 0;
}

int keyboard_layout_menu(void)
{
    // Sync the global keyboard layout before displaying the menu.
    keyboardsystem_set_layout((KeyboardLayout)config.keyboard_layout);
    menu_entry menu[KB_LAYOUT_COUNT] = { 0 };
    for (int i = 0; i < KB_LAYOUT_COUNT; ++i)
    {
        menu[i].name     = (char*)keyboard_layout_names[i];
        menu[i].id       = i;
        menu[i].disabled = false;
        menu[i].suffix   = (i == config.keyboard_layout) ? "*" : "";
    }
    menu_geom geom = make_geom_centered(320, 120);
    int sel        = display_menu(menu, KB_LAYOUT_COUNT, &geom, &keyboard_layout_loop, NULL, NULL, menu);
    (void)sel;
    return 0;
}