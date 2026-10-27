// touchabsolute.h

#ifndef TOUCHABSOLUTE_H
#define TOUCHABSOLUTE_H

typedef struct TouchData TouchData;
typedef struct SceRtcTick SceRtcTick;

#include <stdbool.h>

void touchabsolute_enable(bool enable);
bool touchabsolute_is_enabled();

void touchabsolute_handle_ds4(const TouchData* touch, SceRtcTick* current);
void touchabsolute_handle_absolute(const TouchData* touch, SceRtcTick* current, int* front_state, short* finger_count, TouchData* swipe, TouchData* touch_old);
void touchabsolute_handle_tablet(const TouchData* touch);

#endif // TOUCHABSOLUTE_H
