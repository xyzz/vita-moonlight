// ui_hotkey.h
#ifndef UI_HOTKEY_H
#define UI_HOTKEY_H

#include <psp2/ctrl.h>
#include <stdbool.h>

#define MAX_HOTKEYS 8
#define HOTKEY_MAX_BUTTONS 3
#define HOTKEY_FUNC_NAME_MAX 32
#define HOTKEY_MENU_MAX 16 // Maximum hotkeys in the menu

typedef enum
{
    HOTKEY_FUNC_NONE = 0,
    HOTKEY_FUNC_PAUSE_STREAM,
    HOTKEY_FUNC_OPEN_KEYBOARD,
    HOTKEY_FUNC_MAX
} hotkey_func_t;

typedef void (*hotkey_func_cb)(void);

// Estructura de un hotkey
typedef struct
{
    SceCtrlButtons buttons[HOTKEY_MAX_BUTTONS];
    int num_buttons;
    hotkey_func_t function;
} hotkey_t;

extern hotkey_t g_hotkeys[MAX_HOTKEYS];
extern int g_hotkey_count;
extern char* settings_special_names[];

void hotkey_init_defaults();
void hotkey_load_config();
void hotkey_save_config();
void hotkey_process(const SceCtrlData* pad);
int ui_hotkey_menu();
void hotkey_func_name(int idx, char* out, int outlen);
void hotkey_buttons_name(const hotkey_t* hk, char* out, int outlen);
int add_hotkey_menu();

#endif // UI_HOTKEY_H
