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

#include <stdbool.h>
#include <stdint.h>
#include <psp2/net/net.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/motion.h>
#include "../keyboardsystem.h"

#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/rtc.h>


typedef enum {
  NO_TOUCH_ACTION = 0,
  ON_SCREEN_TOUCH,
  SCREEN_TAP,
  SWIPE_START,
  ON_SCREEN_SWIPE
} TouchScreenState;

typedef enum {
  TOUCHSEC_NORTHWEST  = 0x1,
  TOUCHSEC_NORTHEAST  = 0x2,
  TOUCHSEC_SOUTHWEST  = 0x4,
  TOUCHSEC_SOUTHEAST  = 0x8,
  TOUCHSEC_SPECIAL_NW = 0x10,
  TOUCHSEC_SPECIAL_NE = 0x20,
  TOUCHSEC_SPECIAL_SW = 0x40,
  TOUCHSEC_SPECIAL_SE = 0x80,
} TouchScreenSection;

typedef enum {
  LEFTX,
  LEFTY,
  RIGHTX,
  RIGHTY,
  LEFT_TRIGGER,
  RIGHT_TRIGGER
} PadSection;

typedef struct motion_data_state {
    bool motion_type_gyro_enabled;
    bool motion_type_accel_enabled;
    uint16_t report_rate_gyro;
    uint16_t report_rate_accel;
} motion_data_state;

typedef struct double_click_tracker {
  bool y_max_once;
  uint64_t y_max_once_time;
  bool returned_to_center;
  uint64_t returned_to_center_time;
  bool currently_sprinting;
  uint64_t sprinting_returned_center_time;
  bool sprinting_returned_center;
} double_click_tracker;

#define INPUT_TYPE_MASK         0xfff00000
#define INPUT_VALUE_MASK        0x000fffff

#define INPUT_TYPE_KEYBOARD     0x00000000
#define INPUT_TYPE_SPECIAL      0x00100000
#define INPUT_TYPE_MOUSE        0x00200000
#define INPUT_TYPE_GAMEPAD      0x00300000
#define INPUT_TYPE_ANALOG       0x00400000
#define INPUT_TYPE_TOUCHSCREEN  0x00500000
#define INPUT_TYPE_DEF_NAME     0xf0000000

enum {
  INPUT_SPECIAL_KEY_PAUSE,
  INPUT_SPECIAL_KEY_KEYBOARD
};

typedef struct Point {
  short x;
  short y;
} Point;

typedef struct Section {
  Point left;
  Point right;
} Section;

typedef struct TouchData {
  short button;
  short finger;
  Point points[4];
} TouchData;

bool vitainput_init();
void vitainput_config(CONFIGURATION config);

void vitainput_start(void);
void vitainput_stop(void);

uint8_t read_backscreen();
uint8_t read_frontscreen();
uint32_t is_pressed(uint32_t defined);
uint32_t is_old_pressed(uint32_t defined);
short read_analog(uint32_t defined);
void special(uint32_t defined, uint32_t pressed, uint32_t old_pressed);
void vitainput_process(void);
void move_mouse(TouchData old, TouchData cur);
bool mouse_click(short finger_count, bool press);
void move_motion(SceMotionState motionState);
void move_wheel(TouchData old, TouchData cur);
// void open_keyboard(void); // Ahora se usa keyboardsystem_open_keyboard()