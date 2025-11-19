
#include "../libgamestream/xml.h"
#include "psp2/net/net.h"

void sort_app_list(PAPP_LIST list);
int ensure_buf_size(void **buf, size_t *buf_size, size_t required_size);
void cycle_vita_sockets();
int load_or_refresh_vita_network(SceNetInitParam *net_param);