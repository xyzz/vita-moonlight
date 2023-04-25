#include <stdbool.h>
#include "../device.h"

#define HOST_PORT 47989

void ui_connect_address(char *addr);
device_info_t* ui_connect_and_pairing(device_info_t *info);
bool ui_connect_connected();

void ui_connect_resume();
void ui_connect_manual();
void ui_connect_paired_device(device_info_t *info);
