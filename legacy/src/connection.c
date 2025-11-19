
/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015 Iwan Timmer
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

#include "connection.h"
#include "Limelight.h"
#include "config.h"
#include "power/vita.h"
#include "input/vita.h"
#include "video/vita.h"
#include "audio/vita.h"
#include <stdbool.h>
#include "connection_overlay.h"
#include "debug.h"

static int connection_status = LI_DISCONNECTED;

extern motion_data_state motion_state;

int connection_stage = 0;

bool pause_overlay_is_open(void) {
    return connection_status == LI_MINIMIZED;
}

void pause_output() {
  vitainput_stop();
  vitavideo_stop();
  vitaaudio_stop();
}

void stop_output() {
  vitainput_stop();
  vitapower_stop();
  vitavideo_stop();
  vitaaudio_stop();
}

void start_output() {
  vitainput_start();
  vitapower_start();
  vitavideo_start();
  vitaaudio_start();
}

void connection_connection_started() {
  if (connection_status != LI_PAIRED) {
    vita_debug_log("connection_connection_started error: %d\n", connection_status);
    return;
  }
  vita_debug_log("connection started\n");
  connection_status = LI_CONNECTED;
  start_output();
  vitavideo_hide_poor_net_indicator();
}

static void connection_connection_terminated(int error_code) {
  if (connection_status != LI_PAIRED && connection_status != LI_CONNECTED &&
      connection_status != LI_MINIMIZED) {
    vita_debug_log("connection_connection_terminated error: %d\n", connection_status);
  }

  switch (error_code) {
    case ML_ERROR_GRACEFUL_TERMINATION:
      break;
    case ML_ERROR_NO_VIDEO_TRAFFIC:
      vita_debug_log("No video received from host. Check the host PC's firewall and port forwarding rules.\n");
      break;
    case ML_ERROR_NO_VIDEO_FRAME:
      vita_debug_log("Your network connection isn't performing well. Reduce your video bitrate setting or try a faster connection.\n");
      break;
    case ML_ERROR_UNEXPECTED_EARLY_TERMINATION:
      vita_debug_log("The connection was unexpectedly terminated by the host due to a video capture error. Make sure no DRM-protected content is playing on the host.\n");
      break;
    case ML_ERROR_PROTECTED_CONTENT:
      vita_debug_log("The connection was terminated by the host due to DRM-protected content. Close any DRM-protected content on the host and try again.\n");
      break;
    default:
      vita_debug_log("Connection terminated with error: %d\n", error_code);
      break;    
  }

  LiStopConnection();

  if (connection_status == LI_CONNECTED) {
    stop_output();
  }
  vita_debug_log("connection terminated\n");
  connection_status = LI_DISCONNECTED;
}

int connection_reset() {
  if (connection_status != LI_DISCONNECTED) {
    vita_debug_log("connection_reset error: %d\n", connection_status);
    return -1;
  }
  connection_status = LI_READY;
  return 0;
}

int connection_paired() {
  if (connection_status != LI_READY && connection_status != LI_PAIRED &&
      connection_status != LI_CONNECTED) {
    vita_debug_log("connection_paired error: %d\n", connection_status);
    return -1;
  }
  connection_status = LI_PAIRED;
  return 0;
}

int connection_minimize() {
  if (connection_status != LI_CONNECTED) {
    vita_debug_log("connection_minimize error: %d\n", connection_status);
    return -1;
  }
  pause_output();
  connection_status = LI_MINIMIZED;
  return 0;
}

int connection_resume() {
  if (connection_status != LI_MINIMIZED) {
    vita_debug_log("connection_resume error: %d\n", connection_status);
    return -1;
  }
  start_output();
  connection_status = LI_CONNECTED;
  return 0;
}

int connection_terminate() {
  if (connection_status != LI_PAIRED && connection_status != LI_CONNECTED &&
      connection_status != LI_MINIMIZED) {
    vita_debug_log("connection_terminate error: %d\n", connection_status);
    return -1;
  }
  connection_connection_terminated(0);
  return 0;
}

void connection_stage_starting(int stage) {
  connection_stage = stage;
  const char* connection_stage_name = LiGetStageName(stage);
  vita_debug_log("connection_stage_starting - stage: %s\n", connection_stage_name);
}
void connection_stage_complate(int stage) {
  connection_stage = stage;
  const char* connection_stage_name = LiGetStageName(stage);
  vita_debug_log("connection_stage_complete - stage: %s\n", connection_stage_name);
}

void connection_stage_failed(int stage, int code) {
  connection_stage = stage;
  const char* connection_stage_name = LiGetStageName(stage);
  vita_debug_log("connection_stage_failed - stage: %s, %d\n", connection_stage_name, code);
}

bool connection_is_ready() {
  return connection_status != LI_DISCONNECTED;
}

bool connection_is_connected() {
  return connection_status == LI_CONNECTED;
}

int connection_get_status() {
  return connection_status;
}

void connection_status_update(int status) {
  switch (status) {
    case CONN_STATUS_POOR:
      vitavideo_show_poor_net_indicator();
      break;
    case CONN_STATUS_OKAY:
      vitavideo_hide_poor_net_indicator();
      break;
  }
}

void connection_set_motion_state(uint16_t controller, uint8_t motion_type, uint16_t report_rate) {
  vita_debug_log("Set motion state called, controller: %u, Type: %u, Report rate: %u", controller, motion_type, report_rate);

  //TODO: Multicontroller support here someday? Can't afford pstv tho
  if (config.enable_motion_controls == false) {
    vita_debug_log("Ignored gyro request due to config");
    return;
  }

  if (report_rate == 0) {
    switch (motion_type) {
      case LI_MOTION_TYPE_GYRO:
        motion_state.motion_type_gyro_enabled = false;
        motion_state.report_rate_gyro = 1;
        break;
      case LI_MOTION_TYPE_ACCEL:
        motion_state.motion_type_accel_enabled = false;
        motion_state.report_rate_accel = 1;
        break;
    }
  } else {

    switch (motion_type) {
      case LI_MOTION_TYPE_GYRO:
        motion_state.motion_type_gyro_enabled = true;
        motion_state.report_rate_gyro = report_rate;
        vita_debug_log("Setting gyro state true");
        break;
      case LI_MOTION_TYPE_ACCEL:
        motion_state.motion_type_accel_enabled = true;
        motion_state.report_rate_accel = report_rate;
        vita_debug_log("Setting accel state true");
        break;
    }
  }
}

CONNECTION_LISTENER_CALLBACKS connection_callbacks = {
  .stageStarting = connection_stage_starting,
  .stageComplete = connection_stage_complate,
  .stageFailed = connection_stage_failed,
  .connectionStarted = connection_connection_started,
  .connectionTerminated = connection_connection_terminated,
  .connectionStatusUpdate = connection_status_update,
  .logMessage = vita_debug_log,
  .setMotionEventState = connection_set_motion_state
};
