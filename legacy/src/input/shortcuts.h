// shortcuts.h
#pragma once
#include <psp2/ctrl.h>
#include <stdbool.h>

// Returns true when a shortcut handled the input and its state must be cleared.
typedef struct SceCtrlData SceCtrlData;
bool process_physical_shortcuts(const SceCtrlData* pad, const SceCtrlData* pad_old);
