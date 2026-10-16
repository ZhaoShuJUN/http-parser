#ifndef TEST_MOSQUITTO_MOCK_H
#define TEST_MOSQUITTO_MOCK_H
/* Test-only ABI declarations for offline syntax/integration testing.
 * NOT an implementation of libmosquitto. Real builds use the vendor library.
 */
#include <stdbool.h>
#define MOSQ_ERR_SUCCESS 0
#define MOSQ_ERR_NOMEM 1
#define MOSQ_ERR_INVAL 3
#define MOSQ_ERR_NO_CONN 4
#define MOSQ_ERR_ERRNO 14
#define MQTT_PROTOCOL_V311 4
#define MOSQ_OPT_PROTOCOL_VERSION 1
struct mosquitto;
int mosquitto_lib_init(void);
int mosquitto_lib_cleanup(void);
struct mosquitto *mosquitto_new(const char *, bool, void *);
void mosquitto_destroy(struct mosquitto *);
void mosquitto_connect_callback_set(struct mosquitto *, void (*)(struct mosquitto*,void*,int));
void mosquitto_disconnect_callback_set(struct mosquitto *, void (*)(struct mosquitto*,void*,int));
void mosquitto_publish_callback_set(struct mosquitto *, void (*)(struct mosquitto*,void*,int));
int mosquitto_int_option(struct mosquitto *,int,int);
int mosquitto_reconnect_delay_set(struct mosquitto *,unsigned int,unsigned int,bool);
int mosquitto_tls_set(struct mosquitto *,const char*,const char*,const char*,const char*,int (*)(char*,int,int,void*));
int mosquitto_connect_async(struct mosquitto *,const char*,int,int);
int mosquitto_loop_start(struct mosquitto *);
int mosquitto_loop_stop(struct mosquitto *,bool);
int mosquitto_disconnect(struct mosquitto *);
int mosquitto_publish(struct mosquitto *,int*,const char*,int,const void*,int,bool);
const char *mosquitto_strerror(int);
#endif
