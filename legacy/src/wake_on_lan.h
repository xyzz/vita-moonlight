#pragma once
#include <stdbool.h>

bool send_wol_packet(const char *mac, const char *ip_broadcast, int port);
