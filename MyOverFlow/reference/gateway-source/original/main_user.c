#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>

#define UPLINK_MAGIC0        0x7EU
#define UPLINK_MAGIC1        0x7EU
#define UPLINK_TYPE_ANALOG   0x01U
#define UPLINK_TYPE_HEART    0x02U
#define UPLINK_TYPE_DISCRETE 0x03U
#define UPLINK_TYPE_CFG_CHG  0x04U
#define UPLINK_TYPE_CFG_RSP  0x05U
#define UPLINK_TYPE_CFG_REQ  0x85U
#define RX_MAX               2048
#define FRAME_JSON_MAX       1600
#define DEFAULT_DEV          "/dev/ttyS1"
#define DEFAULT_BAUD         115200
#define DEFAULT_SERVER_IP    "101.33.253.45"
#define DEFAULT_AUTH_PORT    23106
#define AUTH_PATH            "/iot/devices/auth/gateway"
#define GATEWAY_SN           "YSHY-TEST-GATEWAY-001"
#define CHECK_ID             "YSHY-TEST-GATEWAY-001"
#define NODE_SN_PREFIX       "1511"
#define MFG_ID               "YSHY"
#define MODEL_EDGE           "HSHY-HK68-510P"
#define MODEL_SUB            "HSHY-HK68-510-001"
#define HTTP_BUF_MAX         8192
#define MQTT_BUF_MAX         4096
#define MAX_SUBDEV           64

typedef struct {
	char local_id[32];
	char inh_sn[16];
	char node_id[80];
	char device_id[32];
	int used;
} subdev_t;

static int g_ser_fd = -1;
static int g_mqtt_fd = -1;
static char g_gateway_id[64];
static char g_mqtt_host[128];
static int g_mqtt_port = 1906;
static uint16_t g_mqtt_pkt = 1;
static subdev_t g_subs[MAX_SUBDEV];
static int g_sub_n;

static uint16_t crc16_modbus(const uint8_t *data, uint16_t len)
{
	uint16_t crc = 0xFFFFU;
	uint16_t i;
	uint8_t j;
	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (j = 0; j < 8; j++) {
			if (crc & 1U) {
				crc = (uint16_t)((crc >> 1) ^ 0xA001U);
			} else {
				crc = (uint16_t)(crc >> 1);
			}
		}
	}
	return crc;
}

static int open_serial(const char *dev, int baud)
{
	struct termios tio;
	int fd;
	speed_t sp;
	fd = open(dev, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		return -1;
	}
	memset(&tio, 0, sizeof(tio));
	cfmakeraw(&tio);
	tio.c_cflag |= (CLOCAL | CREAD);
	tio.c_cflag &= ~CSIZE;
	tio.c_cflag |= CS8;
	tio.c_cflag &= ~PARENB;
	tio.c_cflag &= ~CSTOPB;
	sp = (baud == 9600) ? B9600 : B115200;
	cfsetispeed(&tio, sp);
	cfsetospeed(&tio, sp);
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 5;
	if (tcsetattr(fd, TCSANOW, &tio) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int json_pick_str(const char *json, const char *key, char *out, size_t outsz)
{
	char pat[64];
	const char *p;
	const char *q;
	size_t n;
	if (outsz == 0) {
		return -1;
	}
	out[0] = '\0';
	snprintf(pat, sizeof(pat), "\"%s\"", key);
	p = strstr(json, pat);
	if (!p) {
		return -1;
	}
	p = strchr(p + strlen(pat), ':');
	if (!p) {
		return -1;
	}
	p++;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
		p++;
	}
	if (*p != '"') {
		return -1;
	}
	p++;
	q = strchr(p, '"');
	if (!q) {
		return -1;
	}
	n = (size_t)(q - p);
	if (n >= outsz) {
		n = outsz - 1U;
	}
	memcpy(out, p, n);
	out[n] = '\0';
	return 0;
}

static int json_pick_mqtt_addr(const char *json, char *out, size_t outsz)
{
	const char *p;
	const char *q;
	char pat[] = "\"protocol\"";
	out[0] = '\0';
	p = json;
	while ((p = strstr(p, pat)) != NULL) {
		q = strchr(p, ':');
		if (!q) {
			break;
		}
		q++;
		while (*q == ' ' || *q == '\t' || *q == '"') {
			if (*q == '"') {
				q++;
				break;
			}
			q++;
		}
		if (strncmp(q, "mqtt", 4) == 0) {
			return json_pick_str(p, "addr", out, outsz);
		}
		p = q;
	}
	return -1;
}

static int parse_host_port(const char *addr, char *host, size_t host_sz, int *port)
{
	const char *colon;
	size_t n;
	*port = 1906;
	host[0] = '\0';
	colon = strrchr(addr, ':');
	if (!colon) {
		snprintf(host, host_sz, "%s", addr);
		return 0;
	}
	n = (size_t)(colon - addr);
	if (n >= host_sz) {
		n = host_sz - 1U;
	}
	memcpy(host, addr, n);
	host[n] = '\0';
	*port = atoi(colon + 1);
	if (*port <= 0) {
		*port = 1906;
	}
	return 0;
}

static int http_auth_gateway(const char *server_ip, int auth_port)
{
	int fd = -1;
	struct sockaddr_in addr;
	char body[512];
	char req[1024];
	char resp[HTTP_BUF_MAX];
	int body_len;
	int req_len;
	int n;
	int total = 0;
	const char *json;
	char mqtt_addr[128];

	g_gateway_id[0] = '\0';
	mqtt_addr[0] = '\0';

	body_len = snprintf(body, sizeof(body),
		"{\"gatewaySN\":\"%s\",\"algId\":0,\"checkID\":\"%s\","
		"\"devInfo\":{\"name\":\"monitoringHost\",\"description\":\"test\","
		"\"manufacturerId\":\"%s\",\"model\":\"%s\"}}",
		GATEWAY_SN, CHECK_ID, MFG_ID, MODEL_EDGE);
	if (body_len <= 0 || body_len >= (int)sizeof(body)) {
		return -1;
	}
	req_len = snprintf(req, sizeof(req),
		"POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
		"Content-Length: %d\r\nConnection: close\r\n\r\n%s",
		AUTH_PATH, server_ip, auth_port, body_len, body);
	if (req_len <= 0 || req_len >= (int)sizeof(req)) {
		return -1;
	}
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)auth_port);
	if (inet_pton(AF_INET, server_ip, &addr.sin_addr) != 1) {
		close(fd);
		return -1;
	}
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	if (write(fd, req, (size_t)req_len) != req_len) {
		close(fd);
		return -1;
	}
	while (total < (int)sizeof(resp) - 1) {
		n = (int)read(fd, resp + total, (size_t)(sizeof(resp) - 1 - total));
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (n == 0) {
			break;
		}
		total += n;
	}
	resp[total] = '\0';
	close(fd);
	json = strstr(resp, "\r\n\r\n");
	if (!json) {
		return -1;
	}
	json += 4;
	if (json_pick_str(json, "gatewayId", g_gateway_id, sizeof(g_gateway_id)) != 0) {
		return -1;
	}
	if (json_pick_mqtt_addr(json, mqtt_addr, sizeof(mqtt_addr)) != 0) {
		snprintf(mqtt_addr, sizeof(mqtt_addr), "%s:1906", server_ip);
	}
	parse_host_port(mqtt_addr, g_mqtt_host, sizeof(g_mqtt_host), &g_mqtt_port);
	printf("auth ok gatewayId=%s mqtt=%s:%d\n", g_gateway_id, g_mqtt_host, g_mqtt_port);
	fflush(stdout);
	return 0;
}

static int mqtt_connect(void)
{
	uint8_t pkt[256];
	uint8_t *p;
	size_t rem;
	size_t cid_len;
	struct sockaddr_in addr;
	int n;

	if (g_mqtt_fd >= 0) {
		close(g_mqtt_fd);
		g_mqtt_fd = -1;
	}
	g_mqtt_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (g_mqtt_fd < 0) {
		return -1;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)g_mqtt_port);
	if (inet_pton(AF_INET, g_mqtt_host, &addr.sin_addr) != 1) {
		close(g_mqtt_fd);
		g_mqtt_fd = -1;
		return -1;
	}
	if (connect(g_mqtt_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(g_mqtt_fd);
		g_mqtt_fd = -1;
		return -1;
	}
	cid_len = strlen(g_gateway_id);
	rem = 10 + 2 + cid_len;
	p = pkt;
	*p++ = 0x10;
	*p++ = (uint8_t)rem;
	*p++ = 0x00;
	*p++ = 0x04;
	memcpy(p, "MQTT", 4);
	p += 4;
	*p++ = 0x04;
	*p++ = 0x02;
	*p++ = 0x00;
	*p++ = 0x3C;
	*p++ = (uint8_t)((cid_len >> 8) & 0xFF);
	*p++ = (uint8_t)(cid_len & 0xFF);
	memcpy(p, g_gateway_id, cid_len);
	p += cid_len;
	if (write(g_mqtt_fd, pkt, (size_t)(p - pkt)) < 0) {
		close(g_mqtt_fd);
		g_mqtt_fd = -1;
		return -1;
	}
	n = (int)read(g_mqtt_fd, pkt, sizeof(pkt));
	if (n < 4 || pkt[0] != 0x20) {
		close(g_mqtt_fd);
		g_mqtt_fd = -1;
		return -1;
	}
	printf("mqtt connected\n");
	fflush(stdout);
	return 0;
}

static int mqtt_publish(const char *topic, const char *payload)
{
	uint8_t pkt[MQTT_BUF_MAX];
	size_t tlen;
	size_t plen;
	size_t rem;
	size_t i;
	uint8_t *p;

	if (g_mqtt_fd < 0) {
		printf("PUBLISH %s %s\n", topic, payload);
		fflush(stdout);
		return 0;
	}
	tlen = strlen(topic);
	plen = strlen(payload);
	rem = 2 + tlen + plen;
	if (rem + 5 >= sizeof(pkt)) {
		return -1;
	}
	p = pkt;
	*p++ = 0x30;
	if (rem < 128) {
		*p++ = (uint8_t)rem;
	} else {
		*p++ = (uint8_t)(0x80 | (rem & 0x7F));
		*p++ = (uint8_t)(rem >> 7);
	}
	*p++ = (uint8_t)((tlen >> 8) & 0xFF);
	*p++ = (uint8_t)(tlen & 0xFF);
	memcpy(p, topic, tlen);
	p += tlen;
	memcpy(p, payload, plen);
	p += plen;
	if (write(g_mqtt_fd, pkt, (size_t)(p - pkt)) < 0) {
		return -1;
	}
	(void)i;
	(void)g_mqtt_pkt;
	printf("PUBLISH %s %s\n", topic, payload);
	fflush(stdout);
	return 0;
}

static void ser_send_frame(uint8_t type, const uint8_t *payload, uint16_t plen)
{
	uint8_t tx[512];
	uint16_t crc;
	uint16_t i;
	uint16_t total;

	if (g_ser_fd < 0 || (plen + 7U) > sizeof(tx)) {
		return;
	}
	tx[0] = UPLINK_MAGIC0;
	tx[1] = UPLINK_MAGIC1;
	tx[2] = type;
	tx[3] = (uint8_t)((plen >> 8) & 0xFFU);
	tx[4] = (uint8_t)(plen & 0xFFU);
	for (i = 0; i < plen; i++) {
		tx[5 + i] = payload[i];
	}
	crc = crc16_modbus(&tx[2], (uint16_t)(3U + plen));
	tx[5 + plen] = (uint8_t)(crc & 0xFFU);
	tx[6 + plen] = (uint8_t)((crc >> 8) & 0xFFU);
	total = (uint16_t)(7U + plen);
	(void)write(g_ser_fd, tx, total);
}

static void request_cfg(void)
{
	ser_send_frame(UPLINK_TYPE_CFG_REQ, (const uint8_t *)"{}", 2);
}

typedef struct {
	const char *prefix;
	const char *inh_type;
} inh_type_map_t;

static const inh_type_map_t g_inh_prefix_map[] = {
	{"wl_", "0010"},
	{"th_", "0001"},
	{"cable_", "0018"},
	{"probe_", "0012"},
	{"leak_", "0005"},
	{"door_", "0003"},
	{"gas_", "0002"},
	{"smoke_", "0008"},
	{"pd_", "0009"},
	{"ir_", "0004"},
	{"bat_", "0011"},
	{"pump_", "0006"},
	{"fan_", "0007"},
	{NULL, NULL}
};

static int inh_suffix_index(const char *local_id, const char *prefix)
{
	size_t plen;

	plen = strlen(prefix);
	if (strncmp(local_id, prefix, plen) != 0) {
		return 0;
	}
	return atoi(local_id + plen);
}

static int build_inh_sn(const char *local_id, char *out, size_t outsz)
{
	int i;
	int idx;

	for (i = 0; g_inh_prefix_map[i].prefix; i++) {
		const char *p = g_inh_prefix_map[i].prefix;
		const char *t = g_inh_prefix_map[i].inh_type;
		if (strncmp(local_id, p, strlen(p)) != 0) {
			continue;
		}
		idx = inh_suffix_index(local_id, p);
		if (idx <= 0 || idx > 99999) {
			return -1;
		}
		snprintf(out, outsz, "INH%s%05d", t, idx);
		return 0;
	}
	return -1;
}

static void build_platform_node_id(const char *inh_sn, char *out, size_t outsz)
{
	snprintf(out, outsz, "%s%s%s", NODE_SN_PREFIX, inh_sn, GATEWAY_SN);
}

static int ensure_sub(const char *local_id)
{
	int i;

	for (i = 0; i < g_sub_n; i++) {
		if (strcmp(g_subs[i].local_id, local_id) == 0) {
			return i;
		}
	}
	if (g_sub_n >= MAX_SUBDEV) {
		return -1;
	}
	snprintf(g_subs[g_sub_n].local_id, sizeof(g_subs[g_sub_n].local_id), "%s", local_id);
	if (build_inh_sn(local_id, g_subs[g_sub_n].inh_sn, sizeof(g_subs[g_sub_n].inh_sn)) != 0) {
		snprintf(g_subs[g_sub_n].inh_sn, sizeof(g_subs[g_sub_n].inh_sn), "%s", local_id);
		snprintf(g_subs[g_sub_n].node_id, sizeof(g_subs[g_sub_n].node_id), "%s", local_id);
		snprintf(g_subs[g_sub_n].device_id, sizeof(g_subs[g_sub_n].device_id), "L%s", local_id);
	} else {
		build_platform_node_id(g_subs[g_sub_n].inh_sn, g_subs[g_sub_n].node_id,
			sizeof(g_subs[g_sub_n].node_id));
		snprintf(g_subs[g_sub_n].device_id, sizeof(g_subs[g_sub_n].device_id), "%s",
			g_subs[g_sub_n].inh_sn);
	}
	g_subs[g_sub_n].used = 1;
	g_sub_n++;
	return g_sub_n - 1;
}

static void publish_topo_from_cfg(const char *cfg_json)
{
	char topic[160];
	char body[2048];
	char tmp[64];
	int pos = 0;
	int first = 1;
	struct {
		const char *key;
		const char *prefix;
	} map[] = {
		{"wl", "wl_"}, {"th", "th_"}, {"cable", "cable_"}, {"probe", "probe_"},
		{"leak", "leak_"}, {"door", "door_"}, {"gas", "gas_"}, {"smoke", "smoke_"},
		{"pm", "pm_"}, {"sound", "sound_"}, {"pd", "pd_"}, {"ir", "ir_"},
		{NULL, NULL}
	};
	int mi;
	g_sub_n = 0;
	pos = snprintf(body, sizeof(body), "{\"deviceInfo\":[");
	for (mi = 0; map[mi].key; mi++) {
		char pat[32];
		const char *p;
		int cnt = 0;
		int k;
		snprintf(pat, sizeof(pat), "\"%s\":", map[mi].key);
		p = strstr(cfg_json, pat);
		if (!p) {
			continue;
		}
		p += strlen(pat);
		cnt = atoi(p);
		for (k = 1; k <= cnt && k < 16; k++) {
			int idx;
			snprintf(tmp, sizeof(tmp), "%s%d", map[mi].prefix, k);
			idx = ensure_sub(tmp);
			if (idx < 0) {
				continue;
			}
			if (!first) {
				pos += snprintf(body + pos, sizeof(body) - (size_t)pos, ",");
			}
			first = 0;
			pos += snprintf(body + pos, sizeof(body) - (size_t)pos,
				"{\"nodeId\":\"%s\",\"name\":\"%s\",\"description\":\"%s\","
				"\"manufacturerId\":\"%s\",\"model\":\"%s\"}",
				g_subs[idx].node_id, g_subs[idx].inh_sn, g_subs[idx].local_id,
				MFG_ID, MODEL_SUB);
			if (pos >= (int)sizeof(body) - 8) {
				break;
			}
		}
	}
	pos += snprintf(body + pos, sizeof(body) - (size_t)pos, "]}");
	snprintf(topic, sizeof(topic), "/v1/devices/%s/topo/add", g_gateway_id[0] ? g_gateway_id : "0");
	mqtt_publish(topic, body);
}

static void utc_event_time(char *out, size_t outsz)
{
	time_t t = time(NULL);
	struct tm tm;
	gmtime_r(&t, &tm);
	strftime(out, outsz, "%Y%m%dT%H%M%SZ", &tm);
}

static int extract_brace_obj(const char *p, char *out, size_t outsz)
{
	int depth;
	size_t n;
	const char *s;
	if (!p || *p != '{') {
		return -1;
	}
	s = p;
	depth = 0;
	do {
		if (*p == '{') {
			depth++;
		} else if (*p == '}') {
			depth--;
		}
		if (*p == '\0') {
			return -1;
		}
		p++;
	} while (depth > 0);
	n = (size_t)(p - s);
	if (n >= outsz) {
		return -1;
	}
	memcpy(out, s, n);
	out[n] = '\0';
	return 0;
}

static void publish_one_device(const char *service_id, const char *local_id, const char *data_obj, const char *et)
{
	char topic[160];
	char out[MQTT_BUF_MAX];
	const char *dev_id;
	int idx;

	idx = ensure_sub(local_id);
	dev_id = (idx >= 0) ? g_subs[idx].device_id : local_id;
	snprintf(out, sizeof(out),
		"{\"devices\":[{\"deviceId\":\"%s\",\"serviceId\":\"%s\",\"data\":[%s],\"eventTime\":\"%s\"}]}",
		dev_id, service_id, data_obj, et);
	snprintf(topic, sizeof(topic), "/v1/devices/%s/datas", g_gateway_id[0] ? g_gateway_id : "0");
	mqtt_publish(topic, out);
}

static void publish_devices_batch(const char *service_id, const char *devices_json, const char *et)
{
	char topic[160];
	char out[MQTT_BUF_MAX];
	char item[256];
	char local_id[32];
	char data_obj[192];
	char one[320];
	const char *p;
	const char *dev_id;
	int idx;
	int first;
	int pos;
	int n;

	p = strchr(devices_json, '[');
	if (!p) {
		return;
	}
	p++;
	first = 1;
	pos = snprintf(out, sizeof(out), "{\"devices\":[");
	while (*p) {
		while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',') {
			p++;
		}
		if (*p == ']' || *p == '\0') {
			break;
		}
		if (extract_brace_obj(p, item, sizeof(item)) != 0) {
			break;
		}
		p += strlen(item);
		if (json_pick_str(item, "localId", local_id, sizeof(local_id)) != 0) {
			continue;
		}
		data_obj[0] = '{';
		data_obj[1] = '}';
		data_obj[2] = '\0';
		{
			char *dp = strstr(item, "\"data\":");
			if (dp) {
				dp = strchr(dp, '{');
				if (dp) {
					(void)extract_brace_obj(dp, data_obj, sizeof(data_obj));
				}
			}
		}
		idx = ensure_sub(local_id);
		dev_id = (idx >= 0) ? g_subs[idx].device_id : local_id;
		n = snprintf(one, sizeof(one),
			"%s{\"deviceId\":\"%s\",\"serviceId\":\"%s\",\"data\":[%s],\"eventTime\":\"%s\"}",
			first ? "" : ",",
			dev_id, service_id, data_obj, et);
		if (n < 0) {
			continue;
		}
		if (pos + n + 2 >= (int)sizeof(out)) {
			pos += snprintf(out + pos, sizeof(out) - (size_t)pos, "]}");
			snprintf(topic, sizeof(topic), "/v1/devices/%s/datas", g_gateway_id[0] ? g_gateway_id : "0");
			mqtt_publish(topic, out);
			pos = snprintf(out, sizeof(out), "{\"devices\":[");
			first = 1;
			n = snprintf(one, sizeof(one),
				"{\"deviceId\":\"%s\",\"serviceId\":\"%s\",\"data\":[%s],\"eventTime\":\"%s\"}",
				dev_id, service_id, data_obj, et);
			if (n < 0) {
				continue;
			}
		}
		memcpy(out + pos, one, (size_t)n);
		pos += n;
		first = 0;
	}
	if (!first) {
		pos += snprintf(out + pos, sizeof(out) - (size_t)pos, "]}");
		snprintf(topic, sizeof(topic), "/v1/devices/%s/datas", g_gateway_id[0] ? g_gateway_id : "0");
		mqtt_publish(topic, out);
	}
}

static void publish_datas_wrap(const char *payload_json)
{
	char service_id[32];
	char local_id[32];
	char et[32];
	char data_obj[512];
	char *devices_p;
	char *data_p;

	utc_event_time(et, sizeof(et));
	if (json_pick_str(payload_json, "serviceId", service_id, sizeof(service_id)) != 0) {
		snprintf(service_id, sizeof(service_id), "analog");
	}
	devices_p = strstr(payload_json, "\"devices\"");
	if (devices_p) {
		publish_devices_batch(service_id, devices_p, et);
		return;
	}
	if (json_pick_str(payload_json, "localId", local_id, sizeof(local_id)) != 0) {
		snprintf(local_id, sizeof(local_id), "unknown");
	}
	data_obj[0] = '{';
	data_obj[1] = '}';
	data_obj[2] = '\0';
	data_p = strstr(payload_json, "\"data\":");
	if (data_p) {
		data_p = strchr(data_p, '{');
		if (data_p) {
			(void)extract_brace_obj(data_p, data_obj, sizeof(data_obj));
		}
	}
	publish_one_device(service_id, local_id, data_obj, et);
}

static void handle_frame(uint8_t type, const uint8_t *payload, uint16_t plen)
{
	char json[FRAME_JSON_MAX];
	if (plen >= sizeof(json)) {
		plen = (uint16_t)(sizeof(json) - 1U);
	}
	memcpy(json, payload, plen);
	json[plen] = '\0';

	if (type == UPLINK_TYPE_CFG_RSP) {
		printf("CFG_RSP %s\n", json);
		fflush(stdout);
		publish_topo_from_cfg(json);
	} else if (type == UPLINK_TYPE_CFG_CHG) {
		printf("CFG_CHG %s\n", json);
		fflush(stdout);
		request_cfg();
	} else if (type == UPLINK_TYPE_ANALOG || type == UPLINK_TYPE_DISCRETE) {
		publish_datas_wrap(json);
	} else if (type == UPLINK_TYPE_HEART) {
		printf("HEART\n");
		fflush(stdout);
	}
}

int main(int argc, char **argv)
{
	const char *dev = DEFAULT_DEV;
	int baud = DEFAULT_BAUD;
	const char *server_ip = DEFAULT_SERVER_IP;
	int auth_port = DEFAULT_AUTH_PORT;
	uint8_t buf[RX_MAX];
	int n;
	int i;
	uint16_t plen;
	uint16_t need;
	uint16_t crc;
	uint16_t got;
	uint8_t type;
	int pos = 0;

	if (argc >= 2) {
		dev = argv[1];
	}
	if (argc >= 3) {
		baud = atoi(argv[2]);
	}
	if (argc >= 4) {
		server_ip = argv[3];
	}
	if (argc >= 5) {
		auth_port = atoi(argv[4]);
	}

	printf("serial=%s baud=%d auth=%s:%d\n", dev, baud, server_ip, auth_port);
	fflush(stdout);

	if (http_auth_gateway(server_ip, auth_port) == 0) {
		(void)mqtt_connect();
	} else {
		fprintf(stderr, "auth failed (serial only)\n");
		fflush(stderr);
	}

	g_ser_fd = open_serial(dev, baud);
	if (g_ser_fd < 0) {
		perror("open");
		return 1;
	}

	request_cfg();

	for (;;) {
		n = (int)read(g_ser_fd, buf + pos, (size_t)(RX_MAX - pos));
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			perror("read");
			break;
		}
		if (n == 0) {
			continue;
		}
		pos += n;
		i = 0;
		while (i + 7 <= pos) {
			if (buf[i] != UPLINK_MAGIC0 || buf[i + 1] != UPLINK_MAGIC1) {
				i++;
				continue;
			}
			type = buf[i + 2];
			plen = (uint16_t)(((uint16_t)buf[i + 3] << 8) | buf[i + 4]);
			need = (uint16_t)(7U + plen);
			if (i + need > pos) {
				break;
			}
			crc = crc16_modbus(&buf[i + 2], (uint16_t)(3U + plen));
			got = (uint16_t)((uint16_t)buf[i + 5 + plen] |
					((uint16_t)buf[i + 6 + plen] << 8));
			if (crc == got) {
				handle_frame(type, &buf[i + 5], plen);
			}
			i += (int)need;
		}
		if (i > 0) {
			memmove(buf, buf + i, (size_t)(pos - i));
			pos -= i;
		}
		if (pos >= RX_MAX) {
			pos = 0;
		}
	}

	if (g_mqtt_fd >= 0) {
		close(g_mqtt_fd);
	}
	close(g_ser_fd);
	return 0;
}
