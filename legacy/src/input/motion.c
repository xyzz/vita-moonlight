#include <stdbool.h>
#include <time.h>
#include "../config.h"
#include "psp2/motion.h"
#include "Limelight.h"
#include "motion.h"
#include "../debug.h"
#include "vita.h"

#include "psp2/power.h"



// TODO: Config Value
static uint32_t MOTION_SCALE = 28;

bool active_motion_threads = false;

motion_data_state motion_state = {
    .motion_type_gyro_enabled = 0,
    .motion_type_accel_enabled = 0,
    .report_rate_gyro = 1,
    .report_rate_accel = 1,
};

bool vita_motion_init() {

    sceMotionStartSampling();
    sceMotionReset();

    SceUID thid = sceKernelCreateThread("vitainput_motion_gyro_thread", vitainput_motion_gyro_thread, 0, 0x40000, 0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    } else {
      return false;
    }

    thid = sceKernelCreateThread("vitainput_motion_accel_thread", vitainput_motion_accel_thread, 0, 0x40000, 0, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
        return true;
    }

    return false;
}

int vitainput_motion_gyro_thread(SceSize args, void *argp) {
  while (1) {
    if (motion_state.motion_type_gyro_enabled == true && active_motion_threads == true) {
      motion_process_gyro();

      //TODO: Reporting battery events more than one time per second seems unwise
      //Possible solutions: New thread (heavy handed), using main thread (Idk)

      /* uint32_t battery_percent = scePowerGetBatteryLifePercent();
      
      uint8_t battery_state;
      if (scePowerIsBatteryCharging()) {
        battery_state = LI_BATTERY_STATE_CHARGING;
      } else {
        battery_state = LI_BATTERY_STATE_DISCHARGING;
      }

      LiSendControllerBatteryEvent(0, battery_state, battery_percent); */

    } else {
      sceKernelDelayThread(1000000);
    }
    sceKernelDelayThread((1000/motion_state.report_rate_gyro) * 1000);
  }
  
  return 0;
}

int vitainput_motion_accel_thread(SceSize args, void *argp) {
  while (1) {
    if (motion_state.motion_type_accel_enabled == true && active_motion_threads == true) {
      motion_process_accel();
    } else {
      sceKernelDelayThread(1000000);
    }

    sceKernelDelayThread((1000/motion_state.report_rate_accel) * 1000);
  }

  return 0;
}

void motion_process_gyro() {
  SceMotionState motionState;
  sceMotionGetState(&motionState);

  //Angular velocity should be in deg/s

  float vx = motionState.angularVelocity.x;
  float vy = motionState.angularVelocity.z;
  float vz = motionState.angularVelocity.y * -1;

  vx = vx * MOTION_SCALE;
  vy = vy * MOTION_SCALE;
  vz = vz * MOTION_SCALE;

  //vita_debug_log("Gyro: vx: %f vy: %f vz: %f", vx, vy, vz);
  LiSendControllerMotionEvent(0, LI_MOTION_TYPE_GYRO, vx, vy, vz);
}

void motion_process_accel() {
  SceMotionState motionState;
  sceMotionGetState(&motionState);

  float vx = motionState.acceleration.x;
  float vy = motionState.acceleration.z;
  float vz = motionState.acceleration.y * -1;

  vx = vx * MOTION_SCALE;
  vy = vy * MOTION_SCALE;
  vz = vz * MOTION_SCALE;

  //vita_debug_log("Accel: vx: %f\nvy: %f\nvz: %f\n", vx, vy, vz);
  LiSendControllerMotionEvent(0, LI_MOTION_TYPE_ACCEL, vx, vy, vz);

}