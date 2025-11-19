// touchabsolute.c
// Lógica de mouse absoluto (touch-to-absolute-mouse) para Moonlight Vita Motion
// Inspirado en vita-moonlight/src/input/vita.c

#include "touchabsolute.h"
#include "../config.h" // Para CONFIGURATION
#include "vita.h" // Para TouchData y dependencias
#include "../debug.h"
#include <psp2/touch.h>
#include <psp2/ctrl.h>
#include <stdbool.h>
#include <string.h>

static bool absolute_mouse_enabled = false;
static int last_abs_x = -1, last_abs_y = -1;

void touchabsolute_enable(bool enable) {
    absolute_mouse_enabled = enable;
    if (!enable) {
        last_abs_x = -1;
        last_abs_y = -1;
    }
}

bool touchabsolute_is_enabled() {
    return absolute_mouse_enabled;
}

// Convierte coordenadas de touchpad a coordenadas absolutas de mouse
static void touch_to_absolute(int tx, int ty, int *mx, int *my) {
    // El touchpad frontal de Vita es 1920x1088, la pantalla es 960x544
    *mx = tx * 960 / 1920;
    *my = ty * 544 / 1088;
}


// --- DS4 Touchpad Mode ---
void touchabsolute_handle_ds4(const TouchData* touch, SceRtcTick* current) {
    static uint8_t prev_finger_active[10] = {0};
    static int prev_x[10] = {0};
    static int prev_y[10] = {0};
    const int DS4_TOUCHPAD_MOVE_THRESHOLD = 1; // Más sensible
    for (int i = 0; i < 10; ++i) {
        int x = (i < touch->finger) ? touch->points[i].x : 0;
        int y = (i < touch->finger) ? touch->points[i].y : 0;
        float norm_x = (float)x / (float)960;
        float norm_y = (float)y / (float)544;
        int is_active = (x != 0 || y != 0);
        if (is_active && !prev_finger_active[i]) {
            LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_DOWN, i, norm_x, norm_y, 1.0f);
            vita_debug_log("[DS4_TOUCHPAD] DOWN finger=%d x=%.3f y=%.3f", i, norm_x, norm_y);
        } else if (is_active && prev_finger_active[i]) {
            int dx = abs(x - prev_x[i]);
            int dy = abs(y - prev_y[i]);
            if (dx >= DS4_TOUCHPAD_MOVE_THRESHOLD || dy >= DS4_TOUCHPAD_MOVE_THRESHOLD) {
                LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_MOVE, i, norm_x, norm_y, 1.0f);
                vita_debug_log("[DS4_TOUCHPAD] MOVE finger=%d x=%.3f y=%.3f (dx=%d dy=%d)", i, norm_x, norm_y, dx, dy);
            }
        } else if (!is_active && prev_finger_active[i]) {
            LiSendControllerTouchEvent(0, LI_TOUCH_EVENT_UP, i, norm_x, norm_y, 0.0f);
            vita_debug_log("[DS4_TOUCHPAD] UP finger=%d", i);
        }
        prev_finger_active[i] = is_active;
        prev_x[i] = x;
        prev_y[i] = y;
    }
}

// --- Mouse Absolute Mode ---
void touchabsolute_handle_absolute(const TouchData* touch, SceRtcTick* current, int* front_state, short* finger_count, TouchData* swipe, TouchData* touch_old) {
    // Lógica movida desde vita.c (modo 2)
    if (touchabsolute_is_enabled()) {
        if (touch->finger > 0) {
            int x = touch->points[0].x;
            int y = touch->points[0].y;
            LiSendMousePositionEvent(x, y, 960, 544);
        }
        // Click izquierdo y derecho según número de dedos
        static int prev_finger_count __attribute__((unused)) = 0;
        static int left_down __attribute__((unused)) = 0;
        static int right_down __attribute__((unused)) = 0;
        static int prev_one_finger = 0;
        if (touch->finger == 1 && !prev_one_finger) {
            LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
            prev_one_finger = 1;
        } else if (touch->finger != 1 && prev_one_finger) {
            LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
            prev_one_finger = 0;
        }
        static int two_finger_active __attribute__((unused)) = 0;
        static int two_finger_scroll __attribute__((unused)) = 0;
        static int two_finger_start_y __attribute__((unused)) = 0;
        static int two_finger_last_y __attribute__((unused)) = 0;
        static int right_click_sent __attribute__((unused)) = 0;
        const int SCROLL_THRESHOLD = 12;
        if (touch->finger == 2) {
            int avg_y = (touch->points[0].y + touch->points[1].y) / 2;
            if (!two_finger_active) {
                two_finger_active = 1;
                two_finger_scroll = 0;
                two_finger_start_y = avg_y;
                two_finger_last_y = avg_y;
            } else {
                int delta = avg_y - two_finger_last_y;
                if (abs(avg_y - two_finger_start_y) > SCROLL_THRESHOLD) {
                    two_finger_scroll = 1;
                }
                if (two_finger_scroll && abs(delta) > 0) {
                    LiSendScrollEvent(delta);
                }
                two_finger_last_y = avg_y;
            }
        } else if (two_finger_active && touch->finger < 2) {
            if (!two_finger_scroll && !right_click_sent) {
                vita_debug_log("[ABS_MOUSE] Click derecho: tap dos dedos");
                LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_RIGHT);
                LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
                right_click_sent = 1;
            } else {
                right_click_sent = 0;
            }
            two_finger_active = 0;
            two_finger_scroll = 0;
        } else if (touch->finger != 2) {
            two_finger_active = 0;
            two_finger_scroll = 0;
            right_click_sent = 0;
        }
        memcpy(touch_old, touch, sizeof(TouchData));
    }
    // mouse y gestos solo en modo mouse absoluto
    switch (*front_state) {
      case 0: // NO_TOUCH_ACTION
        if (touch->finger > 0) {
          *front_state = 1; // ON_SCREEN_TOUCH
          *finger_count = touch->finger;
          sceRtcTickAddMicroseconds(&(*current), &(*current), 100000);
        }
        break;
      case 1: // ON_SCREEN_TOUCH
        if (sceRtcCompareTick(current, current) < 0) {
          if (touch->finger < *finger_count) {
            if (mouse_click(*finger_count, true)) {
              *front_state = 2; // SCREEN_TAP
              sceRtcTickAddMicroseconds(&(*current), &(*current), 100000);
            } else {
              *front_state = 0;
            }
          } else if (touch->finger > *finger_count) {
            *finger_count = touch->finger;
          }
        } else {
          *front_state = 3; // SWIPE_START
        }
        break;
      case 2: // SCREEN_TAP
        if (sceRtcCompareTick(current, current) >= 0) {
          mouse_click(*finger_count, false);
          *front_state = 0;
        }
        break;
      case 3: // SWIPE_START
        memcpy(swipe, touch, sizeof(TouchData));
        *front_state = 4; // ON_SCREEN_SWIPE
        break;
      case 4: // ON_SCREEN_SWIPE
        if (touch->finger > 0) {
          switch (touch->finger) {
            case 1:
              move_mouse(*swipe, *touch);
              break;
            case 2:
              move_wheel(*swipe, *touch);
              break;
          }
          memcpy(swipe, touch, sizeof(TouchData));
        } else {
          *front_state = 0;
        }
        break;
    }
}

// --- Tablet/Sunshine Mode ---
void touchabsolute_handle_tablet(const TouchData* touch) {
    static bool mouse_released __attribute__((unused)) = false;
    if (!mouse_released) {
      LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
      LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_RIGHT);
      LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_MIDDLE);
      LiSendMousePositionEvent(-1, -1, 960, 544);
      vita_debug_log("[TOUCHSCREEN] Mouse liberado y fuera de pantalla");
      mouse_released = true;
    }
    static uint8_t prev_finger_active[10] __attribute__((unused)) = {0};
    for (int i = 0; i < 10; ++i) {
      int x = (i < touch->finger) ? touch->points[i].x : 0;
      int y = (i < touch->finger) ? touch->points[i].y : 0;
      float norm_x = (float)x / (float)960;
      float norm_y = (float)y / (float)544;
      int is_active = (x != 0 || y != 0);
      if (is_active && !prev_finger_active[i]) {
        LiSendTouchEvent(LI_TOUCH_EVENT_DOWN, i, norm_x, norm_y, 1.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        vita_debug_log("[TOUCHSCREEN] DOWN finger=%d x=%.3f y=%.3f", i, norm_x, norm_y);
      } else if (is_active && prev_finger_active[i]) {
        LiSendTouchEvent(LI_TOUCH_EVENT_MOVE, i, norm_x, norm_y, 1.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        vita_debug_log("[TOUCHSCREEN] MOVE finger=%d x=%.3f y=%.3f", i, norm_x, norm_y);
      } else if (!is_active && prev_finger_active[i]) {
        LiSendTouchEvent(LI_TOUCH_EVENT_UP, i, norm_x, norm_y, 0.0f, 0.0f, 0.0f, LI_ROT_UNKNOWN);
        vita_debug_log("[TOUCHSCREEN] UP finger=%d", i);
      }
      prev_finger_active[i] = is_active;
    }
}
