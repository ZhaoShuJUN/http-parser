#ifndef GATEWAY_H
#define GATEWAY_H
#include <stddef.h>
#include <stdint.h>
/* Preserve original MCU uplink wire format and cloud interface. */
#define UPLINK_MAGIC0 0x7EU
#define UPLINK_MAGIC1 0x7EU
#define UPLINK_TYPE_ANALOG   0x01U
#define UPLINK_TYPE_HEART    0x02U
#define UPLINK_TYPE_DISCRETE 0x03U
#define UPLINK_TYPE_CFG_CHG  0x04U
#define UPLINK_TYPE_CFG_RSP  0x05U
#define UPLINK_TYPE_CFG_REQ  0x85U
#define RX_MAX 2048
#define FRAME_JSON_MAX 1600
#define MAX_SUBDEV 64
#define DEFAULT_DEV "/dev/ttyS1"
#define DEFAULT_BAUD 115200
#define DEFAULT_SERVER_IP "101.33.253.45"
#define DEFAULT_AUTH_PORT 23106
#define AUTH_PATH "/iot/devices/auth/gateway"
#define GATEWAY_SN "YSHY-TEST-GATEWAY-001"
#define CHECK_ID "YSHY-TEST-GATEWAY-001"
#define NODE_SN_PREFIX "1511"
#define MFG_ID "YSHY"
#define MODEL_EDGE "HSHY-HK68-510P"
#define MODEL_SUB "HSHY-HK68-510-001"
#define MQTT_BUF_MAX 4096
#define PAYLOAD_MAX 3800

typedef struct {
    char local_id[32];
    char inh_sn[16];
    char node_id[80];
    char device_id[32];
} subdev_t;

uint64_t mono_ms(void);
uint16_t crc16_modbus(const uint8_t *buf, size_t n);
void log_msg(const char *level, const char *fmt, ...);
#endif
