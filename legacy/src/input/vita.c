/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2016 Ilya Zhuravlev, Sunguk Lee, Vasyl Horbachenko
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include <psp2common/ctrl.h>
#include <psp2/shellutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <openssl/rand.h>
#include <openssl/evp.h>

#include "../connection.h"
#include "../config.h"
#include "../debug.h"
#include "psp2/kernel/threadmgr/thread.h"
#include "psp2common/types.h"
#include "vita.h"
#include "mapping.h"
#include "swap_shoulder_buttons.h"
#include "../keyboardsystem.h"


#include "touchabsolute.h"
#include "shortcuts.h"
#include "../connection_overlay.h"

#include <Limelight.h>

#define WIDTH 960
#define HEIGHT 544
#define MOUSE_SENSITIVITY 2400.0

const short Y_MAXIMIUM_DEADZONE = -32383;
const short Y_MINIMUM_DEADZONE = -1024;

double_click_tracker dc_tracker = {
  .y_max_once = false,
  .y_max_once_time = 0,
  .returned_to_center = false,
  .returned_to_center_time = 0,
  .currently_sprinting = false,
  .sprinting_returned_center_time = 0,
  .sprinting_returned_center = false
};

struct mapping map = {0};
SceFQuaternion deviceQuat_old = {0.0f, 0.0f, 0.0f, 0.0f};

typedef struct input_data {
    short button;
    short lx;
    short ly;
    short rx;
    short ry;
    char  lt;
    char  rt;
} input_data;

void check_for_double_click(input_data *curr);
static inline void update_touch_points();

#define lerp(value, from_max, to_max) ((((value*10) * (to_max*10))/(from_max*10))/10)

double mouse_multiplier;

#define PSBTN_DOUBLETAP_DELAY 250000 // 100ms

#define MOUSE_ACTION_DELAY 100000 // 100ms
#define MOTION_ACTION_DELAY 200000 // 200ms

inline bool mouse_click(short finger_count, bool press) {
  int mode;

  if (press) {
    mode = BUTTON_ACTION_PRESS;
  } else {
    mode = BUTTON_ACTION_RELEASE;
  }

  switch (finger_count) {
    case 1:
      LiSendMouseButtonEvent(mode, BUTTON_LEFT);
      return true;
    case 2:
      LiSendMouseButtonEvent(mode, BUTTON_RIGHT);
      return true;
  }
  return false;
}

inline void move_mouse(TouchData old, TouchData cur) {
  double delta_x = (cur.points[0].x - old.points[0].x) / 2.;
  double delta_y = (cur.points[0].y - old.points[0].y) / 2.;

  if (delta_x == 0 && delta_y == 0) {
    return;
  }

  int x = lround(delta_x * mouse_multiplier);
  int y = lround(delta_y * mouse_multiplier);

  LiSendMouseMoveEvent(x, y);
}


inline void move_motion(SceMotionState motionState) {
  const float motion_scalar_x = config.motion_controls_scalar_x;
  const float motion_scalar_y = config.motion_controls_scalar_y;

  // Get the mouse position.
  double delta_x = (deviceQuat_old.y-motionState.deviceQuat.y) * (float)MOUSE_SENSITIVITY * motion_scalar_x;
  double delta_y = (deviceQuat_old.x-motionState.deviceQuat.x) * (float)MOUSE_SENSITIVITY * motion_scalar_y;

  if (delta_x == 0 && delta_y == 0) {
    return;
  }

  int x = lround(delta_x * mouse_multiplier);
  int y = lround(delta_y * mouse_multiplier);

  LiSendMouseMoveEvent(x, y);
}


inline void move_wheel(TouchData old, TouchData cur) {
  int old_y = (old.points[0].y + old.points[1].y) / 2;
  int cur_y = (cur.points[0].y + cur.points[1].y) / 2;
  int delta_y = (cur_y - old_y) / 2;
  if (!delta_y) {
    return;
  }
  LiSendScrollEvent(delta_y);
}

SceCtrlData pad, pad_old;
TouchData touch;
TouchData touch_old, swipe;
SceTouchData front, back;
SceMotionState motionState;

int front_state = NO_TOUCH_ACTION;
short finger_count = 0;
SceRtcTick current, until;


//static int special_status;

input_data curr, old;
int controller_port;
bool _calibrateGyro = true;
bool _motionActivated = false;
bool _motionCalibrated = false;
int _motionResetCount = 0;

// TODO config
static int VERTICAL;
static int HORIZONTAL;

#define IN_SECTION(SECTION, X, Y) \
    ((SECTION).left.x <= (X) && (X) <= (SECTION).right.x && \
     (SECTION).left.y <= (Y) && (Y) <= (SECTION).right.y)

// TODO sections
Section BACK_SECTIONS[4];
Section FRONT_SECTIONS[4];

inline uint8_t read_backscreen() {
  for (int i = 0; i < back.reportNum; i++) {
    int x = lerp(back.report[i].x, 1919, WIDTH);
    int y = lerp(back.report[i].y, 1087, HEIGHT);

    if ((touch.button & TOUCHSEC_NORTHWEST) == 0) {
      if (IN_SECTION(BACK_SECTIONS[0], x, y)) {
        touch.button |= TOUCHSEC_NORTHWEST;
        continue;
      }
    }

    if ((touch.button & TOUCHSEC_NORTHEAST) == 0) {
      if (IN_SECTION(BACK_SECTIONS[1], x, y)) {
        touch.button |= TOUCHSEC_NORTHEAST;
        continue;
      }
    }

    if ((touch.button & TOUCHSEC_SOUTHWEST) == 0) {
      if (IN_SECTION(BACK_SECTIONS[2], x, y)) {
        touch.button |= TOUCHSEC_SOUTHWEST;
        continue;
      }
    }

    if ((touch.button & TOUCHSEC_SOUTHEAST) == 0) {
      if (IN_SECTION(BACK_SECTIONS[3], x, y)) {
        touch.button |= TOUCHSEC_SOUTHEAST;
        continue;
      }
    }
  }
  return 0;
}


// Nueva función: solo actualiza touch.points y touch.finger
static inline void update_touch_points() {
  touch.finger = 0;
  for (int i = 0; i < front.reportNum; i++) {
    int x = lerp(front.report[i].x, 1919, WIDTH);
    int y = lerp(front.report[i].y, 1087, HEIGHT);
    touch.points[touch.finger].x = x;
    touch.points[touch.finger].y = y;
    touch.finger += 1;
  }
}

inline uint8_t read_frontscreen() {
  for (int i = 0; i < front.reportNum; i++) {
    int x = lerp(front.report[i].x, 1919, WIDTH);
    int y = lerp(front.report[i].y, 1087, HEIGHT);

    if ((touch.button & TOUCHSEC_SPECIAL_NW) == 0) {
      if (IN_SECTION(FRONT_SECTIONS[0], x, y)) {
        touch.button |= TOUCHSEC_SPECIAL_NW;
        continue;
      }
    }

    if ((touch.button & TOUCHSEC_SPECIAL_NE) == 0) {
      if (IN_SECTION(FRONT_SECTIONS[1], x, y)) {
        touch.button |= TOUCHSEC_SPECIAL_NE;
        continue;
      }
    }

    if ((touch.button & TOUCHSEC_SPECIAL_SW) == 0) {
      if (IN_SECTION(FRONT_SECTIONS[2], x, y)) {
        touch.button |= TOUCHSEC_SPECIAL_SW;
        continue;
      }
    }

    if ((touch.button & TOUCHSEC_SPECIAL_SE) == 0) {
      if (IN_SECTION(FRONT_SECTIONS[3], x, y)) {
        touch.button |= TOUCHSEC_SPECIAL_SE;
        continue;
      }
    }

    // Ya no actualiza touch.points ni touch.finger aquí
  }
  return 0;
}

inline uint32_t is_pressed(uint32_t defined) {
  uint32_t dev_type = defined & INPUT_TYPE_MASK;
  uint32_t dev_val  = defined & INPUT_VALUE_MASK;

  switch(dev_type) {
    case INPUT_TYPE_GAMEPAD:
      return pad.buttons & dev_val;
    case INPUT_TYPE_TOUCHSCREEN:
      return touch.button & dev_val;
  }
  return 0;
}

inline uint32_t is_old_pressed(uint32_t defined) {
  uint32_t dev_type = defined & INPUT_TYPE_MASK;
  uint32_t dev_val  = defined & INPUT_VALUE_MASK;

  switch(dev_type) {
    case INPUT_TYPE_GAMEPAD:
      return pad_old.buttons & dev_val;
    case INPUT_TYPE_TOUCHSCREEN:
      return touch_old.button & dev_val;
  }
  return 0;
}

inline short read_analog(uint32_t defined) {
  uint32_t dev_type = defined & INPUT_TYPE_MASK;
  uint32_t dev_val  = defined & INPUT_VALUE_MASK;

  if (dev_type == INPUT_TYPE_ANALOG) {
    int v;
    switch(dev_val) {
      case LEFTX:
        v = pad.lx;
        break;
      case LEFTY:
        v = pad.ly;
        break;
      case RIGHTX:
        v = pad.rx;
        break;
      case RIGHTY:
        v = pad.ry;
        break;
      case LEFT_TRIGGER:
        return pad.lt;
      case RIGHT_TRIGGER:
        return pad.rt;
      default:
        return 0;
    }
    v = v * 256 - (1 << 15) + 128;
    return (short)(v);
  }
  return is_pressed(defined) ? 0xff : 0;
}

inline void special(uint32_t defined, uint32_t pressed, uint32_t old_pressed) {
  uint32_t dev_type = defined & INPUT_TYPE_MASK;
  uint32_t dev_val  = defined & INPUT_VALUE_MASK;

  if (pressed) {
    if(dev_val == LEFT_TRIGGER || dev_val == RIGHT_TRIGGER) {
        switch(dev_val) {
          case LEFT_TRIGGER:
            curr.lt = 0xff;
            return;
          case RIGHT_TRIGGER:
            curr.rt = 0xff;
            return;
        }
    }

    switch(dev_type) {
      case INPUT_TYPE_SPECIAL:
        // Limpiar input físico ANTES de overlays/eventos modales
        memset(&curr, 0, sizeof(input_data));
        curr.lt = 0;
        curr.rt = 0;
        pad.buttons = 0;
        // Enviar frame vacío al host para limpiar estado
        LiSendMultiControllerEvent(0, 1, 0, 0, 0, 0, 0, 0, 0);
        if (dev_val == INPUT_SPECIAL_KEY_PAUSE) {
          connection_minimize();
          // Limpiar input físico DESPUÉS de overlays/eventos modales
          memset(&curr, 0, sizeof(input_data));
          curr.lt = 0;
          curr.rt = 0;
          pad.buttons = 0;
          // Enviar frame vacío al host para limpiar estado
          LiSendMultiControllerEvent(0, 1, 0, 0, 0, 0, 0, 0, 0);
          return;
        }
        if (dev_val == INPUT_SPECIAL_KEY_KEYBOARD) {
          keyboardsystem_open_keyboard();
          // Limpiar input físico DESPUÉS de overlays/eventos modales
          memset(&curr, 0, sizeof(input_data));
          curr.lt = 0;
          curr.rt = 0;
          pad.buttons = 0;
          // Enviar frame vacío al host para limpiar estado
          LiSendMultiControllerEvent(0, 1, 0, 0, 0, 0, 0, 0, 0);
          return;
        }
        // Si hay otros overlays, añadir aquí
        memset(&curr, 0, sizeof(input_data));
        curr.lt = 0;
        curr.rt = 0;
        pad.buttons = 0;
        // Enviar frame vacío al host para limpiar estado
        LiSendMultiControllerEvent(0, 1, 0, 0, 0, 0, 0, 0, 0);
        return;
      case INPUT_TYPE_GAMEPAD:
        curr.button |= dev_val;
        return;
      case INPUT_TYPE_MOUSE:
        if (!old_pressed) {
          LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, dev_val);
        }
        return;
      case INPUT_TYPE_KEYBOARD:
       if (!old_pressed) {
          LiSendKeyboardEvent(dev_val, KEY_ACTION_DOWN, 0);
       }
       return;
    }
  } else {
    // released
    switch(dev_type) {
      case INPUT_TYPE_MOUSE:
        if (old_pressed) {
          LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, dev_val);
        }
        return;
      case INPUT_TYPE_KEYBOARD:
        if (old_pressed) {
          LiSendKeyboardEvent(dev_val, KEY_ACTION_UP, 0);
        }
        return;
    }
  }

}

float QuatLength(SceFQuaternion v1, SceFQuaternion v2) {
  float x_diff = v1.x - v2.x;
  float y_diff = v1.y - v2.y;
  float z_diff = v1.z - v2.z;

  return sqrt(x_diff * x_diff + y_diff * y_diff + z_diff * z_diff);
}

inline void check_for_double_click(input_data *curr) {
//can uncomment this if I ever need to debug this
//#define DOUBLETAP_DEBUG

  uint64_t current_time = sceKernelGetSystemTimeWide();
  uint32_t doubleclick_step_time = 0;
  if (config.double_tap_sprint_step_time) {
    doubleclick_step_time = config.double_tap_sprint_step_time * 1000;
  }

//Condition 1: Y is maximum
  if (curr->ly < Y_MAXIMIUM_DEADZONE && !dc_tracker.y_max_once && !dc_tracker.currently_sprinting) {
    #ifdef DOUBLETAP_DEBUG
    vita_debug_log("Condition one triggered, current Y: %d", curr.ly);
    #endif
    dc_tracker.y_max_once = true;  
    dc_tracker.y_max_once_time = current_time;
    #ifdef DOUBLETAP_DEBUG
    vita_debug_log("Stamping y_max_once_time at: %llu", dc_tracker.y_max_once_time);
    #endif
  }

  //Condition 2: Y is minimum and less than doubleclicksteptime ms has passed
  if (dc_tracker.y_max_once && curr->ly > Y_MINIMUM_DEADZONE && !dc_tracker.returned_to_center && !dc_tracker.currently_sprinting) {
    #ifdef DOUBLETAP_DEBUG
    vita_debug_log("Condition two triggered, current Y: %d", curr.ly);
    #endif
    if ((current_time - dc_tracker.y_max_once_time) < doubleclick_step_time) {
      #ifdef DOUBLETAP_DEBUG
      vita_debug_log("Condition two: Y max once was less than step time, delta: %llu", current_time-dc_tracker.y_max_once_time);
      #endif
      dc_tracker.returned_to_center = true;
      dc_tracker.returned_to_center_time = current_time;
      #ifdef DOUBLETAP_DEBUG
      vita_debug_log("Condition two: Stamping returned_to_center_time at: %llu", dc_tracker.returned_to_center_time);
      #endif
    } else {
      #ifdef DOUBLETAP_DEBUG
      vita_debug_log("Condition two: Y Max once was more than step time, delta: %llu", current_time-dc_tracker.y_max_once_time);
      #endif
      dc_tracker.y_max_once = false;
    }
  }

  //Condition 3: Y is maximium and condition 2 passed
  if (dc_tracker.returned_to_center && curr->ly < Y_MAXIMIUM_DEADZONE && !dc_tracker.currently_sprinting) {
    #ifdef DOUBLETAP_DEBUG
    vita_debug_log("Condition three triggered, current Y: %d", curr.ly);
    #endif
    dc_tracker.y_max_once = false;
    dc_tracker.returned_to_center = false;
    if ((current_time - dc_tracker.returned_to_center_time) < doubleclick_step_time) {
      #ifdef DOUBLETAP_DEBUG
      vita_debug_log("Condition three: return to center was less than step time, delta: %llu", current_time - dc_tracker.returned_to_center_time);
      vita_debug_log("Should be sprinting");
      #endif
      dc_tracker.currently_sprinting = true;
    }
  }

  //Y is now minimum and we're done sprinting (almost)
  if (dc_tracker.currently_sprinting && curr->ly > Y_MINIMUM_DEADZONE) {

    //if we haven't returned to the center yet, mark that we have
    if (!dc_tracker.sprinting_returned_center) {
      dc_tracker.sprinting_returned_center_time = current_time;
      dc_tracker.sprinting_returned_center = true;
    } else {
      //we've already returned to the center, see how long we've been here
      if ((sceKernelGetSystemTimeWide() - dc_tracker.sprinting_returned_center_time) > 50000) {
        dc_tracker.currently_sprinting = false;
        dc_tracker.sprinting_returned_center = false;
      }
    }
    //Mark that we've returned to center
    #ifdef DOUBLETAP_DEBUG
    vita_debug_log("We stopped sprinting");
    #endif
    dc_tracker.currently_sprinting = false;

  } else {
    dc_tracker.sprinting_returned_center = false;
  }

  if (dc_tracker.currently_sprinting) {
    curr->button |= LS_CLK_FLAG;
  }
}


static bool has_specialkey(int key) {
  if(!config.enable_front_touchzones)
    return 0;

  switch(key) {
    case 0: return !!config.special_keys.nw;
    case 1: return !!config.special_keys.ne;
    case 2: return !!config.special_keys.sw;
    case 3: return !!config.special_keys.se;
  }

  return 0;
}

// Callback para enviar eventos de mouse absoluto
// static void send_absolute_mouse_event(int x, int y, bool down) {
//     if (down) {
//         // Usar las dimensiones de referencia de la pantalla Vita
//         LiSendMousePositionEvent(x, y, 960, 544);
//         LiSendMouseButtonEvent(BUTTON_ACTION_PRESS, BUTTON_LEFT);
//     } else {
//         LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, BUTTON_LEFT);
//     }
// }

bool psbutton_locked = 0;
void lock_psbutton() {
  if(!psbutton_locked) {
    sceShellUtilLock((SceShellUtilLockType)SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN | SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);
    psbutton_locked = 1;
  }
}

void unlock_psbutton() {
  if(psbutton_locked) {
    sceShellUtilUnlock((SceShellUtilLockType)SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN | SCE_SHELL_UTIL_LOCK_TYPE_PS_BTN_2);
    psbutton_locked = 0;
  }
}

SceUInt64 psbutton_pressed_time = 0;
void handle_psbutton() {
  SceUInt64 time = sceKernelGetSystemTimeWide();

  if(!config.enable_psbutton_capture) {
    if(psbutton_locked)
      unlock_psbutton();
    return;
  }

  if(is_pressed(SCE_CTRL_PSBUTTON | INPUT_TYPE_GAMEPAD)) {
    if(!is_old_pressed(SCE_CTRL_PSBUTTON | INPUT_TYPE_GAMEPAD)) {
      if(time - psbutton_pressed_time < PSBTN_DOUBLETAP_DELAY)
        unlock_psbutton();
      else
        special(SPECIAL_FLAG | INPUT_TYPE_GAMEPAD, 1, 0);
    }
    else {
      if(psbutton_locked)
        special(SPECIAL_FLAG | INPUT_TYPE_GAMEPAD, 1, 1);
      else
        special(SPECIAL_FLAG | INPUT_TYPE_GAMEPAD, 0, 1);
    }

    psbutton_pressed_time = time;
  } else {
    if(is_old_pressed(SCE_CTRL_PSBUTTON | INPUT_TYPE_GAMEPAD))
      special(SPECIAL_FLAG | INPUT_TYPE_GAMEPAD, 0, 1);

    if(!psbutton_locked && time - psbutton_pressed_time > PSBTN_DOUBLETAP_DELAY)
      lock_psbutton();
  }
}

bool in_front_touchzone() {
  for (int i = 0; i < touch.finger; i++) {
    int x = touch.points[i].x;
    int y = touch.points[i].y;
    for (int s = 0; s < 4; s++) {
      if (has_specialkey(s) && IN_SECTION(FRONT_SECTIONS[s], x, y)) {
        return true;
      }
    }
  }

  return false;
}


void process_touchzones() {
  read_frontscreen();
  special(config.special_keys.nw,
          is_pressed(INPUT_TYPE_TOUCHSCREEN | TOUCHSEC_SPECIAL_NW),
          is_old_pressed(INPUT_TYPE_TOUCHSCREEN | TOUCHSEC_SPECIAL_NW));
  special(config.special_keys.ne,
          is_pressed(INPUT_TYPE_TOUCHSCREEN | TOUCHSEC_SPECIAL_NE),
          is_old_pressed(INPUT_TYPE_TOUCHSCREEN | TOUCHSEC_SPECIAL_NE));
  special(config.special_keys.sw,
          is_pressed(INPUT_TYPE_TOUCHSCREEN | TOUCHSEC_SPECIAL_SW),
          is_old_pressed(INPUT_TYPE_TOUCHSCREEN | TOUCHSEC_SPECIAL_SW));
  special(config.special_keys.se,
          is_pressed(INPUT_TYPE_TOUCHSCREEN | TOUCHSEC_SPECIAL_SE),
          is_old_pressed(INPUT_TYPE_TOUCHSCREEN | TOUCHSEC_SPECIAL_SE));
}

void process_buttons() {
    // --- LECTURA DE BOTONES Y STICKS SIEMPRE (antes de los modos táctiles) ---
  curr.button |= is_pressed(map.btn_dpad_up)    ? UP_FLAG     : 0;
  curr.button |= is_pressed(map.btn_dpad_left)  ? LEFT_FLAG   : 0;
  curr.button |= is_pressed(map.btn_dpad_down)  ? DOWN_FLAG   : 0;
  curr.button |= is_pressed(map.btn_dpad_right) ? RIGHT_FLAG  : 0;
  curr.button |= is_pressed(map.btn_start)      ? PLAY_FLAG   : 0;
  curr.button |= is_pressed(map.btn_select)     ? BACK_FLAG   : 0;
  curr.button |= is_pressed(map.btn_north)      ? Y_FLAG      : 0;
  curr.button |= is_pressed(map.btn_east)       ? B_FLAG      : 0;
  curr.button |= is_pressed(map.btn_south)      ? A_FLAG      : 0;
  curr.button |= is_pressed(map.btn_west)       ? X_FLAG      : 0;
  // Zonas inferiores: L3/R3 (nunca swap)
  if (is_pressed(map.btn_tl2)) {
    curr.button |= LS_CLK_FLAG; // L3
  }
  if (is_pressed(map.btn_tr2)) {
    curr.button |= RS_CLK_FLAG; // R3
  }
}

void process_triggers() {
  // Swap L1<=>L2 y R1<=>R2 correctamente
  if (swap_shoulder_buttons) {
    // Físicos: L1 manda L2 (analógico), rear touch L2 manda L1 (digital)
    if (is_pressed(map.btn_thumbl)) {
      curr.lt = 0xff; // L1 físico activa L2 analógico
    }
    if (is_pressed(map.btn_tl)) {
      curr.button |= LB_FLAG; // rear touch L2 activa L1 digital
    }
    if (!is_pressed(map.btn_tl)) {
      curr.button &= ~LB_FLAG;
    }
    if (!is_pressed(map.btn_thumbl)) {
      curr.lt = 0;
    }
    // Físicos: R1 manda R2 (analógico), rear touch R2 manda R1 (digital)
    if (is_pressed(map.btn_thumbr)) {
      curr.rt = 0xff; // R1 físico activa R2 analógico
    }
    if (is_pressed(map.btn_tr)) {
      curr.button |= RB_FLAG; // rear touch R2 activa R1 digital
    }
    if (!is_pressed(map.btn_tr)) {
      curr.button &= ~RB_FLAG;
    }
    if (!is_pressed(map.btn_thumbr)) {
      curr.rt = 0;
    }
  } else {
    // Físicos: L1 manda L1 (digital), L2 manda L2 (analógico)
    if (is_pressed(map.btn_thumbl)) {
      curr.button |= LB_FLAG;
    }
    if (is_pressed(map.btn_tl)) {
      curr.lt = 0xff;
      // No modificar curr.button aquí
    }
    // Físicos: R1 manda R1 (digital), R2 manda R2 (analógico)
    if (is_pressed(map.btn_thumbr)) {
      curr.button |= RB_FLAG;
    }
    if (is_pressed(map.btn_tr)) {
      curr.rt = 0xff;
      // No modificar curr.button aquí
    }
  }
}

extern bool keyboardsystem_is_open(void);

void process_touch() {
  if (config.enable_double_tap_sprint) {
    check_for_double_click(&curr);
  }

  if(in_front_touchzone())
    return;

  // --- PROCESAMIENTO DE MODOS TÁCTILES EXCLUSIVOS ---

  if (config.touchscreen_mode == 1) {
    touchabsolute_handle_ds4(&touch, &current);
  } else if (config.touchscreen_mode == 2) {
    touchabsolute_handle_absolute(&touch, &current, &front_state, &finger_count, &swipe, &touch_old);
  } else if (config.touchscreen_mode == 3) {
    touchabsolute_handle_tablet(&touch);
  } else {
    static bool mouse_released __attribute__((unused)) = false;
    mouse_released = false;
  // mouse y gestos solo si no está en modo touchscreen
  // Si el toque está en una sección de special key, NO procesar como ratón

    switch (front_state) {
      case NO_TOUCH_ACTION:
        if (touch.finger > 0) {
          front_state = ON_SCREEN_TOUCH;
          finger_count = touch.finger;
          sceRtcTickAddMicroseconds(&until, &current, MOUSE_ACTION_DELAY);
        }
        break;
      case ON_SCREEN_TOUCH:
        if (sceRtcCompareTick(&current, &until) < 0) {
          if (touch.finger < finger_count) {
            // TAP
            if (mouse_click(finger_count, true)) {
              front_state = SCREEN_TAP;
              sceRtcTickAddMicroseconds(&until, &current, MOUSE_ACTION_DELAY);
            } else {
              front_state = NO_TOUCH_ACTION;
            }
          } else if (touch.finger > finger_count) {
            // finger count changed
            finger_count = touch.finger;
          }
        } else {
          front_state = SWIPE_START;
        }
        break;
      case SCREEN_TAP:
        if (sceRtcCompareTick(&current, &until) >= 0) {
          mouse_click(finger_count, false);
          front_state = NO_TOUCH_ACTION;
        }
        break;
      case SWIPE_START:
        memcpy(&swipe, &touch, sizeof(swipe));
        front_state = ON_SCREEN_SWIPE;
        break;
      case ON_SCREEN_SWIPE:
        if (touch.finger > 0) {
          switch (touch.finger) {
            case 1:
              move_mouse(swipe, touch);
              break;
            case 2:
              move_wheel(swipe, touch);
              break;
          }
          memcpy(&swipe, &touch, sizeof(swipe));
        } else {
          front_state = NO_TOUCH_ACTION;
        }
        break;
    }
  }
}

inline void vitainput_process(void) {
  memset(&pad, 0, sizeof(pad));
  memset(&touch, 0, sizeof(TouchData));
  memset(&curr, 0, sizeof(input_data));
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  sceCtrlPeekBufferPositiveExt2(controller_port, &pad, 1);
  sceTouchPeek(SCE_TOUCH_PORT_FRONT, &front, 1);
  sceTouchPeek(SCE_TOUCH_PORT_BACK, &back, 1);
  // Siempre actualizar los puntos táctiles del frente
  update_touch_points();
  // Siempre procesar las esquinas del back (para compatibilidad o futuros usos)
  read_backscreen();

  sceRtcGetCurrentTick(&current);
  // analogs: solo asignar si no están activos por rear touch
  if (!swap_shoulder_buttons && !is_pressed(map.btn_tl2))
    curr.lt = read_analog(map.btn_tl); // l2
  if (!swap_shoulder_buttons && !is_pressed(map.btn_tr2))
    curr.rt = read_analog(map.btn_tr); // r2
  if (config.enable_front_touchzones) {
    process_touchzones();
  }
  // --- FIN BLOQUE SPECIAL KEYS/ESQUINAS DEL FRENTE ---

  process_buttons();
  handle_psbutton();
  process_triggers();

  // --- GESTIÓN DE LIMPIEZA DE INPUT AL ABRIR/CERRAR TECLADO VIRTUAL Y PAUSA ---
  static bool keyboard_overlay_active = false;
  static bool pause_overlay_active = false;
  static SceCtrlData pad_snapshot = {0};
  static input_data curr_snapshot = {0};

  // Hook para saber si el teclado virtual está abierto

  bool shortcut_triggered = process_physical_shortcuts(&pad, &pad_old);
  bool keyboard_now = keyboardsystem_is_open();
  bool pause_now = pause_overlay_is_open();

  // --- BLOQUEO Y LIMPIEZA DE INPUT AL ABRIR TECLADO VIRTUAL ---
  if (keyboard_now && !keyboard_overlay_active) {
    memcpy(&pad_snapshot, &pad, sizeof(SceCtrlData));
    memcpy(&curr_snapshot, &curr, sizeof(input_data));
    memset(&pad, 0, sizeof(SceCtrlData));
    memset(&curr, 0, sizeof(input_data));
    // Centrar sticks al bloquear input
    pad.lx = 128; pad.ly = 128; pad.rx = 128; pad.ry = 128;
    curr.lx = 128; curr.ly = 128; curr.rx = 128; curr.ry = 128;
    curr.lt = 0;
    curr.rt = 0;
    vita_debug_log("[VITA.C] Overlay activo: ABRIR teclado, input bloqueado (sticks centrados, sin enviar frame vacío)");
    keyboard_overlay_active = true;
  } else if (!keyboard_now && keyboard_overlay_active) {
    vita_debug_log("[VITA.C] Teclado virtual CERRADO: restaurando snapshot (sin enviar frame vacío)");
    memcpy(&pad, &pad_snapshot, sizeof(SceCtrlData));
    memcpy(&curr, &curr_snapshot, sizeof(input_data));
    keyboard_overlay_active = false;
  } else if (keyboard_overlay_active) {
    memset(&pad, 0, sizeof(SceCtrlData));
    memset(&curr, 0, sizeof(input_data));
    pad.lx = 128; pad.ly = 128; pad.rx = 128; pad.ry = 128;
    curr.lx = 128; curr.ly = 128; curr.rx = 128; curr.ry = 128;
    curr.lt = 0;
    curr.rt = 0;
  }

  // --- BLOQUEO Y LIMPIEZA DE INPUT AL ABRIR/CERRAR MENÚ DE PAUSA ---
  if (pause_now && !pause_overlay_active) {
    memcpy(&pad_snapshot, &pad, sizeof(SceCtrlData));
    memcpy(&curr_snapshot, &curr, sizeof(input_data));
    memset(&pad, 0, sizeof(SceCtrlData));
    memset(&curr, 0, sizeof(input_data));
    pad.lx = 128; pad.ly = 128; pad.rx = 128; pad.ry = 128;
    curr.lx = 128; curr.ly = 128; curr.rx = 128; curr.ry = 128;
    curr.lt = 0;
    curr.rt = 0;
    vita_debug_log("[VITA.C] Overlay activo: ABRIR menú de pausa, input bloqueado (sticks centrados, sin enviar frame vacío)");
    pause_overlay_active = true;
  } else if (!pause_now && pause_overlay_active) {
    vita_debug_log("[VITA.C] Menú de pausa CERRADO: restaurando snapshot (sin enviar frame vacío)");
    memcpy(&pad, &pad_snapshot, sizeof(SceCtrlData));
    memcpy(&curr, &curr_snapshot, sizeof(input_data));
    pause_overlay_active = false;
  } else if (pause_overlay_active) {
    memset(&pad, 0, sizeof(SceCtrlData));
    memset(&curr, 0, sizeof(input_data));
    pad.lx = 128; pad.ly = 128; pad.rx = 128; pad.ry = 128;
    curr.lx = 128; curr.ly = 128; curr.rx = 128; curr.ry = 128;
    curr.lt = 0;
    curr.rt = 0;
  }

  curr.lx = read_analog(map.abs_x);
  curr.ly = read_analog(map.abs_y);
  curr.rx = read_analog(map.abs_rx);
  curr.ry = read_analog(map.abs_ry);

  process_touch();

  // --- ENVÍO DE EVENTOS DE GAMEPAD SOLO SI NO hay overlay de teclado activo ---
  // O si el overlay está activo pero NO están ambos botones del shortcut presionados
  bool shortcut_both_pressed = (pad.buttons & SCE_CTRL_START) && (pad.buttons & SCE_CTRL_LEFT);
  if (!keyboard_overlay_active || (keyboard_overlay_active && !shortcut_both_pressed)) {
    if (memcmp(&curr, &old, sizeof(input_data)) != 0) {
      LiSendMultiControllerEvent(0, 1, curr.button, curr.lt, curr.rt, curr.lx, -1 * curr.ly, curr.rx, -1 * curr.ry);
      memcpy(&old, &curr, sizeof(input_data));
      memcpy(&pad_old, &pad, sizeof(SceCtrlData));
    }
    if (memcmp(&touch, &touch_old, sizeof(TouchData)) != 0) {
      memcpy(&touch_old, &touch, sizeof(TouchData));
    }
  }
}

static uint8_t active_input_thread = 0;

int vitainput_thread(SceSize args, void *argp) {
  while (1) {
    if (active_input_thread) {
      vitainput_process();
    }

    sceKernelDelayThread(2000); // 2 ms
  }

  return 0;
}

bool vitainput_init() {
  sceCtrlSetSamplingModeExt(SCE_CTRL_MODE_ANALOG_WIDE);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
  sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

  SceUID thid = sceKernelCreateThread("vitainput_thread", vitainput_thread, 0, 0x40000, 0, 0, NULL);
  if (thid >= 0) {
    sceKernelStartThread(thid, 0, NULL);
    return true;
  }

  return false;
}

void vitainput_config(CONFIGURATION config) {
  // Sincroniza el modo swap global con la configuración cargada
  swap_shoulder_buttons = config.swap_shoulder_buttons;
  map.abs_x           = LEFTX               | INPUT_TYPE_ANALOG;
  map.abs_y           = LEFTY               | INPUT_TYPE_ANALOG;
  map.abs_rx          = RIGHTX              | INPUT_TYPE_ANALOG;
  map.abs_ry          = RIGHTY              | INPUT_TYPE_ANALOG;

  map.btn_dpad_up     = SCE_CTRL_UP         | INPUT_TYPE_GAMEPAD;
  map.btn_dpad_down   = SCE_CTRL_DOWN       | INPUT_TYPE_GAMEPAD;
  map.btn_dpad_left   = SCE_CTRL_LEFT       | INPUT_TYPE_GAMEPAD;
  map.btn_dpad_right  = SCE_CTRL_RIGHT      | INPUT_TYPE_GAMEPAD;
  map.btn_south       = SCE_CTRL_CROSS      | INPUT_TYPE_GAMEPAD;
  map.btn_east        = SCE_CTRL_CIRCLE     | INPUT_TYPE_GAMEPAD;
  map.btn_north       = SCE_CTRL_TRIANGLE   | INPUT_TYPE_GAMEPAD;
  map.btn_west        = SCE_CTRL_SQUARE     | INPUT_TYPE_GAMEPAD;

  map.btn_select      = SCE_CTRL_SELECT     | INPUT_TYPE_GAMEPAD;
  map.btn_start       = SCE_CTRL_START      | INPUT_TYPE_GAMEPAD;

  map.btn_thumbl      = SCE_CTRL_L1         | INPUT_TYPE_GAMEPAD;
  map.btn_thumbr      = SCE_CTRL_R1         | INPUT_TYPE_GAMEPAD;

  if (config.model == SCE_KERNEL_MODEL_VITATV) {
    map.btn_tl        = LEFT_TRIGGER        | INPUT_TYPE_ANALOG;
    map.btn_tr        = RIGHT_TRIGGER       | INPUT_TYPE_ANALOG;
    map.btn_tl2       = SCE_CTRL_L3         | INPUT_TYPE_GAMEPAD;
    map.btn_tr2       = SCE_CTRL_R3         | INPUT_TYPE_GAMEPAD;
  } else {
    map.btn_tl        = TOUCHSEC_NORTHWEST  | INPUT_TYPE_TOUCHSCREEN;
    map.btn_tr        = TOUCHSEC_NORTHEAST  | INPUT_TYPE_TOUCHSCREEN;
    map.btn_tl2       = TOUCHSEC_SOUTHWEST  | INPUT_TYPE_TOUCHSCREEN;
    map.btn_tr2       = TOUCHSEC_SOUTHEAST  | INPUT_TYPE_TOUCHSCREEN;
  }

    if (config.mapping) {
        char mapping_file_path[256];
        snprintf(mapping_file_path, sizeof(mapping_file_path), "%s/%s", config.key_dir, config.mapping);
        printf("Loading mapping at %s\n", mapping_file_path);
        mapping_load(mapping_file_path, &map);
    }

  controller_port = config.model == SCE_KERNEL_MODEL_VITATV ? 1 : 0;

  VERTICAL   = (WIDTH - config.back_deadzone.left - config.back_deadzone.right) / 2
             + config.back_deadzone.left;
  HORIZONTAL = (HEIGHT - config.back_deadzone.top - config.back_deadzone.bottom) / 2
             + config.back_deadzone.top;

  BACK_SECTIONS[0].left.x  = config.back_deadzone.left;
  BACK_SECTIONS[0].left.y  = config.back_deadzone.top;
  BACK_SECTIONS[0].right.x = VERTICAL;
  BACK_SECTIONS[0].right.y = HORIZONTAL;

  BACK_SECTIONS[1].left.x  = VERTICAL;
  BACK_SECTIONS[1].left.y  = config.back_deadzone.top;
  BACK_SECTIONS[1].right.x = WIDTH - config.back_deadzone.right;
  BACK_SECTIONS[1].right.y = HORIZONTAL;

  BACK_SECTIONS[2].left.x  = config.back_deadzone.left;
  BACK_SECTIONS[2].left.y  = HORIZONTAL;
  BACK_SECTIONS[2].right.x = VERTICAL;
  BACK_SECTIONS[2].right.y = HEIGHT - config.back_deadzone.bottom;

  BACK_SECTIONS[3].left.x  = VERTICAL;
  BACK_SECTIONS[3].left.y  = HORIZONTAL;
  BACK_SECTIONS[3].right.x = WIDTH - config.back_deadzone.right;
  BACK_SECTIONS[3].right.y = HEIGHT - config.back_deadzone.bottom;

  FRONT_SECTIONS[0].left.x  = config.special_keys.offset;
  FRONT_SECTIONS[0].left.y  = config.special_keys.offset;
  FRONT_SECTIONS[0].right.x = config.special_keys.offset + config.special_keys.size;
  FRONT_SECTIONS[0].right.y = config.special_keys.offset + config.special_keys.size;

  FRONT_SECTIONS[1].left.x  = WIDTH - config.special_keys.offset - config.special_keys.size;
  FRONT_SECTIONS[1].left.y  = config.special_keys.offset;
  FRONT_SECTIONS[1].right.x = WIDTH - config.special_keys.offset;
  FRONT_SECTIONS[1].right.y = config.special_keys.offset + config.special_keys.size;

  FRONT_SECTIONS[2].left.x  = config.special_keys.offset;
  FRONT_SECTIONS[2].left.y  = HEIGHT - config.special_keys.offset - config.special_keys.size;
  FRONT_SECTIONS[2].right.x = config.special_keys.offset + config.special_keys.size;
  FRONT_SECTIONS[2].right.y = HEIGHT - config.special_keys.offset;

  FRONT_SECTIONS[3].left.x  = WIDTH - config.special_keys.offset - config.special_keys.size;
  FRONT_SECTIONS[3].left.y  = HEIGHT - config.special_keys.offset - config.special_keys.size;
  FRONT_SECTIONS[3].right.x = WIDTH - config.special_keys.offset;
  FRONT_SECTIONS[3].right.y = HEIGHT - config.special_keys.offset;

  mouse_multiplier = 1 + (0.01 * config.mouse_acceleration);
}

extern bool active_motion_threads;

void vitainput_start(void) {
  uint16_t gamepadMask = 1;
  uint32_t gamepadCapabilites = LI_CCAP_GYRO | LI_CCAP_BATTERY_STATE | LI_CCAP_ACCEL | LI_CCAP_TOUCHPAD;

  uint32_t gamepadSupportedButtonFlags = 0xffff;
  gamepadSupportedButtonFlags |= TOUCHPAD_FLAG;
  gamepadSupportedButtonFlags |= MISC_FLAG;

  // Determinar tipo de control a enviar según config.controller_type
  uint8_t controller_type = LI_CTYPE_PS;
  switch (config.controller_type) {
    case 1: controller_type = LI_CTYPE_XBOX; break;
    case 2: controller_type = LI_CTYPE_PS; break;
    case 3: controller_type = LI_CTYPE_NINTENDO; break;
    case 4: controller_type = LI_CTYPE_UNKNOWN; break;
    default: controller_type = LI_CTYPE_PS; break;
  }
  LiSendControllerArrivalEvent(0, gamepadMask, controller_type, gamepadSupportedButtonFlags, gamepadCapabilites);

  LiSendControllerBatteryEvent(0, LI_BATTERY_STATE_FULL, 100);

  if(config.enable_psbutton_capture)
    lock_psbutton();

  active_input_thread = true;
  active_motion_threads = true;
}

void vitainput_stop(void) {
  unlock_psbutton();
  active_input_thread = false;
  active_motion_threads = false;
}
