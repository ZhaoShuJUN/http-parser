#ifndef AUTH_H
#define AUTH_H
#include <stddef.h>
/* HTTP Gateway Authentication: produces gatewayId and MQTT address. */
int gateway_http_auth(const char *server, int port, char *gateway_id, size_t id_cap,
                      char *mqtt_host, size_t host_cap, int *mqtt_port);
#endif
