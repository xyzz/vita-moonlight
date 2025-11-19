#include "psp2common/types.h"
#include "stdbool.h"

bool vita_motion_init();

int vitainput_motion_gyro_thread(SceSize args, void *argp);
int vitainput_motion_accel_thread(SceSize args, void *argp);

void motion_process_gyro();
void motion_process_accel();