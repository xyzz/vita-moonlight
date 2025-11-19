#pragma once
#include "../device.h"

// Llama a este método para mostrar el diálogo y actualizar la IP si el usuario acepta.
// Retorna 1 si se actualizó, 0 si no.
int ui_check_ip_update(device_info_t *info, const char *new_ip);
