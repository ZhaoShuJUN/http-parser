#define _POSIX_C_SOURCE 200809L
/*
 * GC6511 MQTT/4G uplink using official libmosquitto.
 * 串口主线程只入队，网络工作线程负责 HTTP 认证以及 MQTT PUBLISH；
 * libmosquitto 自有 loop 线程负责底层 TCP、心跳与自动重连。
 * 一个在途报文 + on_publish 确认后出队，断网后原 RAM 数据队列保留。
 * QoS 0 的 on_publish 仅代表网络写出，不代表服务器已持久化。
 */
#include "gateway.h"
#include "auth.h"
#include "net.h"
#include <mosquitto.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define QUEUE_COUNT 48
#define MQTT_KEEPALIVE_DEFAULT 60
#define REAUTH_AFTER_MS 300000ULL
#define ACK_TIMEOUT_MS 60000ULL
#define RETRY_MIN_MS 2000
#define RETRY_MAX_MS 60000

/* 每次只让一个拓扑分包或一条采样报文等待发送回调，保证消息顺序。 */
enum pending_kind { PENDING_NONE=0, PENDING_TOPO, PENDING_DATA };
typedef struct { char payload[PAYLOAD_MAX+1]; } pending_t;
typedef struct {
    pthread_t thread;
    pthread_mutex_t lock;
    int started, running, connected;
    char server[128]; int auth_port, qos, keepalive;
    char ca_file[256];
    subdev_t subs[MAX_SUBDEV]; int sub_count, topo_known;
    unsigned long topo_version;
    int topo_cursor;
    pending_t queue[QUEUE_COUNT]; int q_head, q_count;
    enum pending_kind pending;
    int pending_mid, pending_topo_end;
    int publishing, early_mid;
    unsigned long pending_version;
    uint64_t pending_since;
    uint64_t last_connected_ms;
    unsigned long tx_done, drop_count, topo_done, reconnects;
} net_state_t;
static net_state_t g = {.lock=PTHREAD_MUTEX_INITIALIZER};

static void nap_ms(int ms){
    struct timespec ts={ms/1000,(long)(ms%1000)*1000000L};
    while(nanosleep(&ts,&ts)<0&&errno==EINTR){}
}
static int running(void){
    int r;pthread_mutex_lock(&g.lock);r=g.running;pthread_mutex_unlock(&g.lock);return r;
}
static int read_range_env(const char *name,int fallback,int min,int max){
    const char *s=getenv(name);char *end;long x;
    if(!s||!*s)return fallback;
    errno=0;x=strtol(s,&end,10);
    if(errno||*end||end==s||x<min||x>max){
        log_msg("WARN","invalid %s, using %d",name,fallback);
        return fallback;
    }
    return (int)x;
}
/* MQTT 回调在 libmosquitto loop 线程中执行，不能在回调里等待/睡眠。 */
static void on_connect(struct mosquitto *mosq,void *obj,int rc){
    (void)mosq;(void)obj;
    pthread_mutex_lock(&g.lock);
    if(rc==0){
        g.connected=1;
        g.last_connected_ms=mono_ms();
        g.topo_cursor=0; /* 每次重连，都重新通知当前设备拓扑 */
        g.pending=PENDING_NONE;
        g.pending_mid=0;
        ++g.reconnects;
    }else g.connected=0;
    pthread_mutex_unlock(&g.lock);
    if(rc==0)log_msg("INFO","MQTT CONNACK accepted, qos=%d",g.qos);
    else log_msg("WARN","MQTT CONNACK refused (rc=%d)",rc);
}
static void on_disconnect(struct mosquitto *mosq,void *obj,int rc){
    (void)mosq;(void)obj;
    pthread_mutex_lock(&g.lock);
    g.connected=0;
    g.pending=PENDING_NONE; /* 未收到回调的 DATA 仍保存在队首 */
    g.pending_mid=0;
    g.topo_cursor=0;
    pthread_mutex_unlock(&g.lock);
    if(rc)log_msg("WARN","MQTT unexpected disconnect rc=%d; reconnect is managed by libmosquitto",rc);
    else log_msg("INFO","MQTT intentionally disconnected");
}
/* Caller holds lock. Handles QoS0 immediate callbacks and QoS1 PUBACK identically. */
static void finish_publish_locked(int mid){
    if(g.pending==PENDING_NONE||g.pending_mid!=mid)return;
    if(g.pending==PENDING_DATA && g.q_count>0){
        g.q_head=(g.q_head+1)%QUEUE_COUNT;
        --g.q_count;
        ++g.tx_done;
    }else if(g.pending==PENDING_TOPO){
        if(g.pending_version==g.topo_version)g.topo_cursor=g.pending_topo_end;
        else g.topo_cursor=0;
        ++g.topo_done;
    }
    g.pending=PENDING_NONE;
    g.pending_mid=0;
}
static void on_publish(struct mosquitto *mosq,void *obj,int mid){
    (void)mosq;(void)obj;
    pthread_mutex_lock(&g.lock);
    if(g.publishing && g.pending==PENDING_NONE){
        /* libmosquitto may invoke a QoS0 callback before publish() returns. */
        g.early_mid=mid;
    }else finish_publish_locked(mid);
    pthread_mutex_unlock(&g.lock);
}
static int make_topic(char *out,size_t cap,const char *gateway,const char *suffix){
    int n=snprintf(out,cap,"/v1/devices/%s/%s",gateway,suffix);
    return n>0&&(size_t)n<cap?0:-1;
}
/* caller holds g.lock; results in one JSON topology chunk. */
static int make_topology_chunk(char *body,size_t cap,int *end_cursor){
    int pos,i,first=1,n;char item[320];
    pos=snprintf(body,cap,"{\"deviceInfo\":[");
    if(pos<=0||(size_t)pos>=cap)return -1;
    for(i=g.topo_cursor;i<g.sub_count;i++){
        n=snprintf(item,sizeof(item),
            "%s{\"nodeId\":\"%s\",\"name\":\"%s\",\"description\":\"%s\","
            "\"manufacturerId\":\"%s\",\"model\":\"%s\"}",
            first?"":",",g.subs[i].node_id,g.subs[i].inh_sn,g.subs[i].local_id,
            MFG_ID,MODEL_SUB);
        if(n<1||(size_t)n>=sizeof(item))return -1;
        if((size_t)pos+(size_t)n+3>cap){
            if(first)return -1;
            break;
        }
        memcpy(body+pos,item,(size_t)n);pos+=n;first=0;
    }
    memcpy(body+pos,"]}",3);
    *end_cursor=i;
    return 0;
}
/* 非阻塞：库会从内部网络循环发送，回调再弹出队首。 */
static int try_publish(struct mosquitto *mosq,const char *gateway){
    char topic[160],payload[PAYLOAD_MAX+1];
    int end=0,mid=0,rc=0,early=0,link_ok=0;
    enum pending_kind kind=PENDING_NONE;
    unsigned long version=0;
    pthread_mutex_lock(&g.lock);
    if(!g.connected || g.pending!=PENDING_NONE || g.publishing){
        pthread_mutex_unlock(&g.lock);return 0;
    }
    if(g.topo_known && g.topo_cursor<g.sub_count){
        if(make_topic(topic,sizeof(topic),gateway,"topo/add")<0 ||
           make_topology_chunk(payload,sizeof(payload),&end)<0){
            pthread_mutex_unlock(&g.lock);
            log_msg("ERROR","topology item over maximum payload");return -1;
        }
        version=g.topo_version;
        kind=PENDING_TOPO;
    }else if(g.q_count>0){
        if(make_topic(topic,sizeof(topic),gateway,"datas")<0){
            pthread_mutex_unlock(&g.lock);return -1;
        }
        strcpy(payload,g.queue[g.q_head].payload);
        kind=PENDING_DATA;
    }
    if(kind==PENDING_NONE){pthread_mutex_unlock(&g.lock);return 0;}
    g.publishing=1;
    g.early_mid=0;
    pthread_mutex_unlock(&g.lock);
    /* No state lock held: QoS0 completion callback can be synchronous in some versions. */
    rc=mosquitto_publish(mosq,&mid,topic,(int)strlen(payload),payload,g.qos,false);
    pthread_mutex_lock(&g.lock);
    early=g.early_mid;
    link_ok=g.connected;
    if(rc==MOSQ_ERR_SUCCESS && link_ok){
        g.pending=kind;
        g.pending_mid=mid;
        g.pending_topo_end=end;
        g.pending_version=version;
        g.pending_since=mono_ms();
        if(early==mid)finish_publish_locked(mid);
    }
    g.publishing=0;
    g.early_mid=0;
    pthread_mutex_unlock(&g.lock);
    if(rc!=MOSQ_ERR_SUCCESS){
        log_msg("WARN","mosquitto_publish failed: %s",mosquitto_strerror(rc));
        return rc==MOSQ_ERR_NO_CONN?0:-1;
    }
    return 0;
}
static void reset_link_state(void){
    pthread_mutex_lock(&g.lock);
    g.connected=0;g.pending=PENDING_NONE;g.pending_mid=0;g.topo_cursor=0;
    pthread_mutex_unlock(&g.lock);
}
static void close_client(struct mosquitto *mosq,int loop_started){
    if(!mosq)return;
    /* Do not hold g.lock while stopping callback/network threads. */
    if(loop_started){
        mosquitto_disconnect(mosq);
        mosquitto_loop_stop(mosq,true);
    }
    mosquitto_destroy(mosq);
    reset_link_state();
}
static void pause_while_running(int ms){
    int elapsed=0;
    while(running()&&elapsed<ms){nap_ms(100);elapsed+=100;}
}
static void *net_worker(void *unused){
    int delay=RETRY_MIN_MS;
    (void)unused;
    if(mosquitto_lib_init()!=MOSQ_ERR_SUCCESS){
        log_msg("ERROR","mosquitto_lib_init failed");return NULL;
    }
    while(running()){
        char gateway[64]={0},host[128]={0};int port=1906;
        int rc,started=0,reset=0;uint64_t begin;
        struct mosquitto *mosq;
        if(gateway_http_auth(g.server,g.auth_port,gateway,sizeof(gateway),host,sizeof(host),&port)<0){
            log_msg("WARN","HTTP authentication failed; retry after %dms",delay);
            pause_while_running(delay);
            if(delay<RETRY_MAX_MS){delay*=2;if(delay>RETRY_MAX_MS)delay=RETRY_MAX_MS;}
            continue;
        }
        log_msg("INFO","HTTP auth ok gatewayId=%s MQTT=%s:%d",gateway,host,port);
        /* Client id from cloud's gatewayId, clean session matches original MQTT 3.1.1 CONNECT. */
        mosq=mosquitto_new(gateway,true,NULL);
        if(!mosq){log_msg("ERROR","mosquitto_new failed");pause_while_running(delay);continue;}
        mosquitto_connect_callback_set(mosq,on_connect);
        mosquitto_disconnect_callback_set(mosq,on_disconnect);
        mosquitto_publish_callback_set(mosq,on_publish);
        rc=mosquitto_int_option(mosq,MOSQ_OPT_PROTOCOL_VERSION,MQTT_PROTOCOL_V311);
        if(rc==MOSQ_ERR_SUCCESS)rc=mosquitto_reconnect_delay_set(mosq,2,60,true);
        if(rc==MOSQ_ERR_SUCCESS && g.ca_file[0]){
            /* Optional server certificate verification: never silently disable TLS validation. */
            rc=mosquitto_tls_set(mosq,g.ca_file,NULL,NULL,NULL,NULL);
        }
        if(rc==MOSQ_ERR_SUCCESS)rc=mosquitto_connect_async(mosq,host,port,g.keepalive);
        if(rc==MOSQ_ERR_SUCCESS){rc=mosquitto_loop_start(mosq);if(rc==MOSQ_ERR_SUCCESS)started=1;}
        if(rc!=MOSQ_ERR_SUCCESS){
            log_msg("WARN","MQTT initialization failed: %s",mosquitto_strerror(rc));
            close_client(mosq,started);pause_while_running(delay);
            if(delay<RETRY_MAX_MS){delay*=2;if(delay>RETRY_MAX_MS)delay=RETRY_MAX_MS;}
            continue;
        }
        delay=RETRY_MIN_MS;
        begin=mono_ms();
        log_msg("INFO","MQTT network loop started; keepalive=%d qos=%d",g.keepalive,g.qos);
        /* libmosquitto handles network reconnect in background. Periodic reauth restores
           gatewayId/broker address if authentication or service routing changed. */
        while(running()){
            int connected;enum pending_kind pending;uint64_t last_ok,since;
            pthread_mutex_lock(&g.lock);
            connected=g.connected;pending=g.pending;since=g.pending_since;
            last_ok=g.last_connected_ms;
            pthread_mutex_unlock(&g.lock);
            if(connected){
                if(try_publish(mosq,gateway)<0){reset=1;break;}
                if(pending!=PENDING_NONE && mono_ms()-since>ACK_TIMEOUT_MS){
                    log_msg("WARN","MQTT publish callback timeout: resetting connection");
                    reset=1;break;
                }
            }else{
                uint64_t reference=last_ok>begin?last_ok:begin;
                if(mono_ms()-reference>=REAUTH_AFTER_MS){
                    log_msg("WARN","MQTT offline too long: re-authenticate and recreate client");
                    reset=1;break;
                }
            }
            nap_ms(20);
        }
        close_client(mosq,started);
        if(reset)pause_while_running(RETRY_MIN_MS);
    }
    mosquitto_lib_cleanup();
    return NULL;
}
int net_start(const char *server,int port){
    const char *ca=getenv("GW_MQTT_CA_FILE");
    if(!server||!*server||strlen(server)>=sizeof(g.server)||port<1||port>65535)return -1;
    pthread_mutex_lock(&g.lock);
    strcpy(g.server,server);g.auth_port=port;
    g.qos=read_range_env("GW_MQTT_QOS",0,0,1);
    g.keepalive=read_range_env("GW_MQTT_KEEPALIVE",MQTT_KEEPALIVE_DEFAULT,5,65535);
    if(ca&&*ca){
        if(strlen(ca)>=sizeof(g.ca_file)){pthread_mutex_unlock(&g.lock);return -1;}
        strcpy(g.ca_file,ca);
    }
    g.running=1;g.last_connected_ms=0;
    pthread_mutex_unlock(&g.lock);
    if(pthread_create(&g.thread,NULL,net_worker,NULL)!=0){
        pthread_mutex_lock(&g.lock);g.running=0;pthread_mutex_unlock(&g.lock);return -1;
    }
    g.started=1;
    return 0;
}
void net_stop(void){
    pthread_mutex_lock(&g.lock);g.running=0;pthread_mutex_unlock(&g.lock);
    if(g.started){pthread_join(g.thread,NULL);g.started=0;}
    log_msg("INFO","net stats: pending_ram=%d confirmed_by_callback=%lu dropped=%lu topo_chunks=%lu connections=%lu",
        g.q_count,g.tx_done,g.drop_count,g.topo_done,g.reconnects);
}
int net_queue_data(const char *payload){
    size_t n;int idx;
    if(!payload||(n=strlen(payload))==0||n>PAYLOAD_MAX)return -1;
    pthread_mutex_lock(&g.lock);
    if(g.q_count==QUEUE_COUNT){++g.drop_count;pthread_mutex_unlock(&g.lock);return -1;}
    idx=(g.q_head+g.q_count)%QUEUE_COUNT;
    memcpy(g.queue[idx].payload,payload,n+1);++g.q_count;
    pthread_mutex_unlock(&g.lock);
    return 0;
}
void net_set_topology(const subdev_t *subs,int count){
    if(count<0||count>MAX_SUBDEV||(count>0&&!subs))return;
    pthread_mutex_lock(&g.lock);
    if(count)memcpy(g.subs,subs,(size_t)count*sizeof(*subs));
    g.sub_count=count;g.topo_known=1;
    ++g.topo_version;
    /* If a chunk is in flight, let callback settle it and then restart topology. */
    if(g.pending!=PENDING_TOPO)g.topo_cursor=0;
    pthread_mutex_unlock(&g.lock);
}
