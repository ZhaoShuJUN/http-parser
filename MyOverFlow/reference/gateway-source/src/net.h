#ifndef NET_H
#define NET_H
#include "gateway.h"
/* Only worker thread touches the TCP socket; producer/worker share bounded RAM queue. */
int net_start(const char *auth_host, int auth_port);
void net_stop(void);
int net_queue_data(const char *json_payload); /* 0=enqueued; -1=full/invalid */
void net_set_topology(const subdev_t *subs, int count); /* replaces cached snapshot */
#endif
