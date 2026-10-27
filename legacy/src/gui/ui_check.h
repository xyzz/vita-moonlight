#pragma once
#include "../device.h"

// Request confirmation before updating the host IP.
int ui_check_ip_update(device_info_t* info, const char* new_ip);
