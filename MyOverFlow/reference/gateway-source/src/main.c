#define _POSIX_C_SOURCE 200809L
/*
 * GC6511 data gateway: hardened version of the uploaded main(1).c.
 * 保留原始串口协议、HTTP 认证路径、MQTT 主题及设备号转换规则。
 * 变更：独立网络线程、自动重连/保活、CRC 帧重同步、边界检查和有界内存缓存。
 */
#include "gateway.h"
#include "json.h"
#include "net.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define CFG_RETRY_MS 30000ULL
#define SERIAL_RETRY_MS 2000ULL
#define PARTIAL_TIMEOUT_MS 3000ULL
#define JSON_TOKENS 512

static volatile sig_atomic_t g_stopping=0;
static pthread_mutex_t g_log_mutex=PTHREAD_MUTEX_INITIALIZER;
static subdev_t g_subs[MAX_SUBDEV];
static int g_sub_n=0,g_cfg_known=0,g_building_cfg=0;
static int g_ser_fd=-1;
static uint64_t g_last_cfg_req=0;
static int g_cfg_request_now=1;
static unsigned long g_frame_ok=0,g_frame_bad=0,g_json_bad=0,g_queue_bad=0;

uint64_t mono_ms(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000ULL+(uint64_t)ts.tv_nsec/1000000ULL;}
void log_msg(const char *level,const char *fmt,...){
    struct timespec t;struct tm tm;va_list ap;char date[40];
    clock_gettime(CLOCK_REALTIME,&t);localtime_r(&t.tv_sec,&tm);
    strftime(date,sizeof(date),"%Y-%m-%d %H:%M:%S",&tm);
    pthread_mutex_lock(&g_log_mutex);
    fprintf(stderr,"[%s.%03ld] [%s] ",date,t.tv_nsec/1000000L,level);
    va_start(ap,fmt);vfprintf(stderr,fmt,ap);va_end(ap);
    fputc('\n',stderr);fflush(stderr);pthread_mutex_unlock(&g_log_mutex);
}
uint16_t crc16_modbus(const uint8_t *data,size_t len){
    uint16_t crc=0xFFFFU;size_t i;int j;
    for(i=0;i<len;i++){
        crc^=data[i];for(j=0;j<8;j++)
            crc=(uint16_t)((crc&1U)?((crc>>1)^0xA001U):(crc>>1));
    }
    return crc;
}
static void on_signal(int sig){(void)sig;g_stopping=1;}

/* 串口 UART: 8N1 原始二进制，无标准 Modbus-RTU 时序要求。 */
static int open_serial(const char *dev,int baud){
    struct termios tio;int fd;speed_t speed;
    switch(baud){
        case 9600:speed=B9600;break;case 19200:speed=B19200;break;
        case 38400:speed=B38400;break;case 57600:speed=B57600;break;
        case 115200:speed=B115200;break;default:return -1;
    }
    fd=open(dev,O_RDWR|O_NOCTTY|O_NONBLOCK);
    if(fd<0)return -1;
    if(tcgetattr(fd,&tio)<0){close(fd);return -1;}
    cfmakeraw(&tio);tio.c_cflag|=CLOCAL|CREAD;
    tio.c_cflag&=~CSIZE;tio.c_cflag|=CS8;
    tio.c_cflag&=~(PARENB|CSTOPB);
#ifdef CRTSCTS
    tio.c_cflag&=~CRTSCTS;
#endif
    cfsetispeed(&tio,speed);cfsetospeed(&tio,speed);
    tio.c_cc[VMIN]=0;tio.c_cc[VTIME]=0;
    if(tcsetattr(fd,TCSANOW,&tio)<0){close(fd);return -1;}
    tcflush(fd,TCIFLUSH);
    return fd;
}
/* A full UART frame is: 7E 7E TYPE LEN_H LEN_L JSON CRC_L CRC_H. */
static int serial_write_all(const uint8_t *data,size_t len){
    size_t sent=0;uint64_t deadline=mono_ms()+1000;
    while(sent<len && !g_stopping){
        ssize_t n=write(g_ser_fd,data+sent,len-sent);
        if(n>0){sent+=(size_t)n;continue;}
        if(n<0&&errno==EINTR)continue;
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)&&mono_ms()<deadline){
            struct pollfd f={g_ser_fd,POLLOUT,0};poll(&f,1,100);continue;
        }
        return -1;
    }
    return sent==len?0:-1;
}
static int ser_send_frame(uint8_t type,const uint8_t *payload,size_t plen){
    uint8_t tx[512];uint16_t crc;
    if(g_ser_fd<0||plen>sizeof(tx)-7)return -1;
    tx[0]=UPLINK_MAGIC0;tx[1]=UPLINK_MAGIC1;tx[2]=type;
    tx[3]=(uint8_t)(plen>>8);tx[4]=(uint8_t)(plen&255);
    if(plen)memcpy(tx+5,payload,plen);
    crc=crc16_modbus(tx+2,3+plen);
    tx[5+plen]=(uint8_t)crc;tx[6+plen]=(uint8_t)(crc>>8);
    return serial_write_all(tx,7+plen);
}
static void request_cfg(void){
    if(ser_send_frame(UPLINK_TYPE_CFG_REQ,(const uint8_t *)"{}",2)==0){
        g_last_cfg_req=mono_ms();g_cfg_request_now=0;
        log_msg("INFO","UART CFG_REQ (0x85) sent");
    }else log_msg("WARN","UART CFG_REQ send failed");
}

typedef struct {const char *key,*prefix,*inh_type;} type_map_t;
/* Keep original type IDs. pm/sound have no cloud type-code mapping in supplied code. */
static const type_map_t g_type_map[]={
    {"wl","wl_","0010"},{"th","th_","0001"},{"cable","cable_","0018"},
    {"probe","probe_","0012"},{"leak","leak_","0005"},{"door","door_","0003"},
    {"gas","gas_","0002"},{"smoke","smoke_","0008"},{"pd","pd_","0009"},
    {"ir","ir_","0004"},{"bat","bat_","0011"},{"pump","pump_","0006"},
    {"fan","fan_","0007"},{"pm","pm_",NULL},{"sound","sound_",NULL},
    {NULL,NULL,NULL}
};
static int safe_identifier(const char *s){
    size_t i,n=strlen(s);if(n==0||n>=32)return 0;
    for(i=0;i<n;i++){
        unsigned char c=(unsigned char)s[i];
        if(!(isalnum(c)||c=='_'||c=='-'))return 0;
    }
    return 1;
}
/* Original code's model: INH + 4-digit type + 5-digit device number. */
static int build_inh_sn(const char *local_id,char *out,size_t outsz){
    int i;const char *p;char *end;long idx;
    for(i=0;g_type_map[i].key;i++){
        p=g_type_map[i].prefix;
        if(strncmp(local_id,p,strlen(p))!=0)continue;
        if(!g_type_map[i].inh_type)return -1;
        errno=0;idx=strtol(local_id+strlen(p),&end,10);
        if(errno||*end||idx<1||idx>99999)return -1;
        return snprintf(out,outsz,"INH%s%05ld",g_type_map[i].inh_type,idx)<(int)outsz?0:-1;
    }
    return -1;
}
static int ensure_sub(const char *local_id){
    int i;subdev_t *d;
    if(!safe_identifier(local_id))return -1;
    for(i=0;i<g_sub_n;i++)if(strcmp(g_subs[i].local_id,local_id)==0)return i;
    if(g_sub_n==MAX_SUBDEV)return -1;
    d=&g_subs[g_sub_n];memset(d,0,sizeof(*d));strcpy(d->local_id,local_id);
    if(build_inh_sn(local_id,d->inh_sn,sizeof(d->inh_sn))==0){
        strcpy(d->device_id,d->inh_sn);
        {
            char node_buf[sizeof(d->node_id)];
            if(snprintf(node_buf,sizeof(node_buf),"%s%s%s",NODE_SN_PREFIX,d->device_id,GATEWAY_SN)>=(int)sizeof(node_buf))return -1;
            strcpy(d->node_id,node_buf);
        }
    }else{
        /* Original unknown-type fallback: deviceId=L<localId>, nodeId=localId. */
        if(strlen(local_id)>=sizeof(d->inh_sn))return -1;
        strcpy(d->inh_sn,local_id);strcpy(d->node_id,local_id);
        if(snprintf(d->device_id,sizeof(d->device_id),"L%s",local_id)>=(int)sizeof(d->device_id))return -1;
    }
    ++g_sub_n;
    /* Newly detected devices must be registered even if initial CFG_RSP is delayed. */
    if(!g_building_cfg)net_set_topology(g_subs,g_sub_n);
    return g_sub_n-1;
}
static int parse_doc(const char *s,jdoc_t *d,jtoken_t *ts){
    return json_parse(d,s,ts,JSON_TOKENS)==0&&d->t[0].type==JT_OBJECT?0:-1;
}
static int topo_from_cfg(const char *cfg){
    jdoc_t d;jtoken_t tok[JSON_TOKENS];int i,k,count,idx,errors=0;char id[32];
    if(parse_doc(cfg,&d,tok)<0)return -1;
    /* Do not destroy earlier topology unless the whole configuration parses. */
    { /* Save old configuration, and roll back entirely on malformed count fields. */
      subdev_t old[MAX_SUBDEV];int old_n=g_sub_n;
      memcpy(old,g_subs,sizeof(old));
      g_building_cfg=1;
      g_sub_n=0;memset(g_subs,0,sizeof(g_subs));
    for(i=0;g_type_map[i].key;i++){
        idx=json_member(&d,0,g_type_map[i].key);
        if(idx<0)continue;
        if(json_get_int(&d,idx,&count)<0 || count<0){errors++;continue;}
        for(k=1;k<=count;k++){
            if(g_sub_n>=MAX_SUBDEV){log_msg("WARN","topology limited to %d devices",MAX_SUBDEV);break;}
            if(snprintf(id,sizeof(id),"%s%d",g_type_map[i].prefix,k)>=(int)sizeof(id)||ensure_sub(id)<0){errors++;break;}
        }
    }
    g_building_cfg=0;
    if(errors){
        memcpy(g_subs,old,sizeof(old));g_sub_n=old_n;
        log_msg("WARN","CFG_RSP invalid: %d bad fields, preserved previous topology",errors);
        return -1;
    }
    }
    g_cfg_known=1;net_set_topology(g_subs,g_sub_n);
    log_msg("INFO","CFG_RSP accepted, devices=%d",g_sub_n);
    return 0;
}
static void utc_event_time(char *out,size_t cap){
    time_t now=time(NULL);struct tm t;
    gmtime_r(&now,&t);strftime(out,cap,"%Y%m%dT%H%M%SZ",&t);
}
static int queue_json_batch(const char *body){
    if(net_queue_data(body)==0)return 0;
    ++g_queue_bad;
    if(g_queue_bad<5 || !(g_queue_bad%100))
        log_msg("WARN","data queue full or message too large (dropped=%lu)",g_queue_bad);
    return -1;
}
static int create_device_item(const jdoc_t *d,int obj,const char *service,const char *time_text,
                             char *item,size_t cap){
    int li,di,idx,n;char local[32],data[FRAME_JSON_MAX];
    if(d->t[obj].type!=JT_OBJECT)return -1;
    li=json_member(d,obj,"localId");di=json_member(d,obj,"data");
    if(li<0||json_get_str(d,li,local,sizeof(local))<0||!safe_identifier(local))return -1;
    if(di<0||d->t[di].type!=JT_OBJECT||json_copy_raw(d,di,data,sizeof(data))<0)return -1;
    idx=ensure_sub(local);
    if(idx<0)return -1;
    n=snprintf(item,cap,"{\"deviceId\":\"%s\",\"serviceId\":\"%s\",\"data\":[%s],\"eventTime\":\"%s\"}",
               g_subs[idx].device_id,service,data,time_text);
    return (n>0&&(size_t)n<cap)?0:-1;
}
static void publish_datas_wrap(const char *payload,uint8_t type){
    jdoc_t doc;jtoken_t ts[JSON_TOKENS];char service[32],et[32],item[PAYLOAD_MAX+1],out[PAYLOAD_MAX+1];
    int svc,devs,i,first=1,used=0,n,seen=0,errors=0;
    if(parse_doc(payload,&doc,ts)<0){++g_json_bad;return;}
    svc=json_member(&doc,0,"serviceId");
    if(svc<0){strcpy(service,type==UPLINK_TYPE_DISCRETE?"discrete":"analog");}
    else if(json_get_str(&doc,svc,service,sizeof(service))<0||!safe_identifier(service)){++g_json_bad;return;}
    utc_event_time(et,sizeof(et));
    used=snprintf(out,sizeof(out),"{\"devices\":[");
    devs=json_member(&doc,0,"devices");
    if(devs<0){
        if(create_device_item(&doc,0,service,et,item,sizeof(item))<0){++g_json_bad;return;}
        n=(int)strlen(item);if(used+n+2>(int)sizeof(out)){++g_json_bad;return;}
        memcpy(out+used,item,(size_t)n);used+=n;first=0;
    }else{
        if(doc.t[devs].type!=JT_ARRAY){++g_json_bad;return;}
        for(i=devs+1;i<doc.t[devs].next;i=doc.t[i].next){
            if(create_device_item(&doc,i,service,et,item,sizeof(item))<0){errors++;continue;}
            n=(int)strlen(item);
            if(used+n+3>(int)sizeof(out)){
                if(first){errors++;continue;}
                memcpy(out+used,"]}",3);queue_json_batch(out);++seen;
                used=snprintf(out,sizeof(out),"{\"devices\":[");first=1;
            }
            if(used+n+2>(int)sizeof(out)){errors++;continue;}
            if(!first)out[used++]=',';
            memcpy(out+used,item,(size_t)n);used+=n;first=0;
        }
    }
    if(!first){memcpy(out+used,"]}",3);queue_json_batch(out);++seen;}
    if(errors)log_msg("WARN","batch skipped %d invalid devices",errors);
    if(!seen && !errors){++g_json_bad;log_msg("WARN","no valid devices in incoming frame");}
}
static void handle_frame(uint8_t type,const uint8_t *payload,size_t length){
    char json[FRAME_JSON_MAX];
    if(type==UPLINK_TYPE_HEART){log_msg("DEBUG","MCU HEART received");return;}
    if(type==UPLINK_TYPE_CFG_CHG){
        g_cfg_request_now=1;log_msg("INFO","MCU configuration changed");return;
    }
    if(length==0||length>=sizeof(json)||memchr(payload,0,length)){
        ++g_json_bad;return;
    }
    memcpy(json,payload,length);json[length]=0;
    if(type==UPLINK_TYPE_CFG_RSP){
        if(topo_from_cfg(json)<0)++g_json_bad;
    }else if(type==UPLINK_TYPE_ANALOG||type==UPLINK_TYPE_DISCRETE){
        publish_datas_wrap(json,type);
    }
}
/* 异常 CRC 时仅滑过 1 字节；支持半包/粘包/帧头重同步。 */
static void parse_rx(uint8_t *buf,size_t *pos){
    size_t i=0;
    while(i+2<=*pos){size_t len,need;uint16_t expected,actual;
        if(buf[i]!=UPLINK_MAGIC0||buf[i+1]!=UPLINK_MAGIC1){i++;continue;}
        if(*pos-i<7)break;
        len=((size_t)buf[i+3]<<8)|buf[i+4];
        if((len==0 && buf[i+2]!=UPLINK_TYPE_HEART && buf[i+2]!=UPLINK_TYPE_CFG_CHG)||len>=FRAME_JSON_MAX||len+7>RX_MAX){++g_frame_bad;i++;continue;}
        need=len+7;if(*pos-i<need)break;
        expected=crc16_modbus(buf+i+2,3+len);
        actual=(uint16_t)((uint16_t)buf[i+5+len]|((uint16_t)buf[i+6+len]<<8));
        if(expected==actual){++g_frame_ok;handle_frame(buf[i+2],buf+i+5,len);i+=need;}
        else {++g_frame_bad;i++;}
    }
    if(i){memmove(buf,buf+i,*pos-i);*pos-=i;}
    if(*pos==RX_MAX){++g_frame_bad;*pos=0;}
}
static void help(const char *name){
    fprintf(stderr,"Usage: %s [serial_device] [baud] [auth_IP_or_DNS] [auth_port]\n",name);
    fprintf(stderr,"Defaults: %s %d %s %d\n",DEFAULT_DEV,DEFAULT_BAUD,DEFAULT_SERVER_IP,DEFAULT_AUTH_PORT);
}
int main(int argc,char **argv){
    const char *serial_dev=DEFAULT_DEV,*server=DEFAULT_SERVER_IP;
    int baud=DEFAULT_BAUD,port=DEFAULT_AUTH_PORT;char *end;long x;
    uint8_t rx[RX_MAX];size_t pos=0;uint64_t next_serial=0,last_rx=0;
    struct sigaction sa;
    if(argc>5){help(argv[0]);return 2;}
    if(argc>1)serial_dev=argv[1];
    if(argc>2){errno=0;x=strtol(argv[2],&end,10);if(errno||*end||x<1||x>1000000){help(argv[0]);return 2;}baud=(int)x;}
    if(argc>3)server=argv[3];
    if(argc>4){errno=0;x=strtol(argv[4],&end,10);if(errno||*end||x<1||x>65535){help(argv[0]);return 2;}port=(int)x;}
    memset(&sa,0,sizeof(sa));sa.sa_handler=on_signal;sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,&sa,NULL);sigaction(SIGTERM,&sa,NULL);
    signal(SIGPIPE,SIG_IGN);
    log_msg("INFO","start uart=%s baud=%d http=%s:%d",serial_dev,baud,server,port);
    if(net_start(server,port)<0){log_msg("ERROR","network worker start failed");return 1;}
    while(!g_stopping){
        uint64_t now=mono_ms();struct pollfd f;int pr;
        if(g_ser_fd<0){
            if(now<next_serial){struct timespec ts={0,100000000L};nanosleep(&ts,NULL);continue;}
            g_ser_fd=open_serial(serial_dev,baud);next_serial=now+SERIAL_RETRY_MS;
            if(g_ser_fd<0){log_msg("WARN","serial open failed (retry): %s",serial_dev);continue;}
            log_msg("INFO","serial opened %s",serial_dev);
            pos=0;g_cfg_request_now=1;g_last_cfg_req=0;
        }
        now=mono_ms();
        if(g_cfg_request_now || (!g_cfg_known && now-g_last_cfg_req>=CFG_RETRY_MS)){
            request_cfg();
            /* If send fails, prevent a hot loop. */
            if(g_cfg_request_now)g_last_cfg_req=now,g_cfg_request_now=0;
        }
        if(pos&&now-last_rx>PARTIAL_TIMEOUT_MS){log_msg("WARN","UART partial frame timeout, resync");pos=0;}
        f.fd=g_ser_fd;f.events=POLLIN;f.revents=0;
        pr=poll(&f,1,200);
        if(pr<0 && errno==EINTR)continue;
        if(pr<0||(pr>0&&(f.revents&(POLLHUP|POLLERR|POLLNVAL)))){
            log_msg("WARN","UART disconnected, reopen");close(g_ser_fd);g_ser_fd=-1;continue;
        }
        if(pr>0 && (f.revents&POLLIN)){
            ssize_t n=read(g_ser_fd,rx+pos,sizeof(rx)-pos);
            if(n>0){pos+=(size_t)n;last_rx=mono_ms();parse_rx(rx,&pos);}
            else if(n==0 || (errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINTR)){
                log_msg("WARN","UART read failure, reopen");close(g_ser_fd);g_ser_fd=-1;
            }
        }
    }
    if(g_ser_fd>=0)close(g_ser_fd);
    net_stop();
    log_msg("INFO","uart stats: valid=%lu bad_crc_or_length=%lu bad_json=%lu queue_rejected=%lu",
        g_frame_ok,g_frame_bad,g_json_bad,g_queue_bad);
    return 0;
}
