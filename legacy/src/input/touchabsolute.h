// touchabsolute.h
// Header para touchabsolute.c

#ifndef TOUCHABSOLUTE_H
#define TOUCHABSOLUTE_H


// Forward declarations para evitar dependencias cruzadas
typedef struct TouchData TouchData;
typedef struct SceRtcTick SceRtcTick;

#include <stdbool.h>


void touchabsolute_enable(bool enable);
bool touchabsolute_is_enabled();

// Nuevas funciones para manejar los 3 modos de touchscreen
void touchabsolute_handle_ds4(const TouchData* touch, SceRtcTick* current);
void touchabsolute_handle_absolute(const TouchData* touch, SceRtcTick* current, int* front_state, short* finger_count, TouchData* swipe, TouchData* touch_old);
void touchabsolute_handle_tablet(const TouchData* touch);

#endif // TOUCHABSOLUTE_H
