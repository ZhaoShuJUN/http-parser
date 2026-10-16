#define _POSIX_C_SOURCE 200809L
/* OFFLINE TEST DOUBLE ONLY - not the real libmosquitto implementation.
 * Implements just enough MQTT 3.1.1 for Python's simulated broker tests.
 */
#include "mosquitto.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

struct mosquitto {
    char id[160],host[128];int port,keepalive,qos,fd,started,run,mid;
    pthread_t th;pthread_mutex_t lock;
    void(*on_connect)(struct mosquitto*,void*,int);
    void(*on_disconnect)(struct mosquitto*,void*,int);
    void(*on_publish)(struct mosquitto*,void*,int);
};
static void delay_ms(int n){struct timespec ts={n/1000,(long)(n%1000)*1000000};nanosleep(&ts,NULL);}
static int sendall(int fd,const void *data,size_t n){
    const uint8_t *p=data;size_t off=0;
    while(off<n){ssize_t m=send(fd,p+off,n-off,MSG_NOSIGNAL);if(m<0&&errno==EINTR)continue;
        if(m<=0)return -1;
        off+=(size_t)m;}return 0;
}
static int readn(int fd,void *data,size_t n){
    uint8_t *p=data;size_t off=0;while(off<n){ssize_t m=recv(fd,p+off,n-off,0);
        if(m<0&&errno==EINTR)continue;
        if(m<=0)return -1;
        off+=(size_t)m;}return 0;
}
static int connect_broker(const char *host,int port){
    struct addrinfo h,*a,*p;char buf[16];int fd=-1;
    memset(&h,0,sizeof(h));h.ai_family=AF_INET;h.ai_socktype=SOCK_STREAM;
    snprintf(buf,sizeof(buf),"%d",port);if(getaddrinfo(host,buf,&h,&a))return -1;
    for(p=a;p;p=p->ai_next){fd=socket(p->ai_family,p->ai_socktype,0);if(fd<0)continue;
        if(connect(fd,p->ai_addr,p->ai_addrlen)==0)break;
        close(fd);fd=-1;}
    freeaddrinfo(a);return fd;
}
static int packet(int fd,int *kind,uint8_t *payload,size_t cap,size_t *size){
    uint8_t b;size_t n=0,mult=1;int i;
    if(readn(fd,&b,1)<0)return -1;
    *kind=b;
    for(i=0;i<4;i++){
        if(readn(fd,&b,1)<0)return -1;
        n+=(size_t)(b&127)*mult;if(!(b&128))break;mult*=128;
    }
    if(i>=4||n>cap)return -1;
    if(readn(fd,payload,n)<0)return -1;
    *size=n;return 0;
}
static int remaining_len(uint8_t *p,size_t n){
    int i=0;do{uint8_t b=(uint8_t)(n%128);n/=128;if(n)b|=128;p[i++]=b;}while(n&&i<4);
    return n?-1:i;
}
static void *worker(void *arg){
    struct mosquitto *m=arg;
    for(;;){
        int fd,rc=0,kind;size_t n;uint8_t resp[4096],pkt[300],hdr[5];int hl;size_t idlen=strlen(m->id);
        pthread_mutex_lock(&m->lock);rc=m->run;pthread_mutex_unlock(&m->lock);if(!rc)break;
        fd=connect_broker(m->host,m->port);
        if(fd<0){delay_ms(500);continue;}
        hl=remaining_len(hdr,12+idlen);
        pkt[0]=0x10;memcpy(pkt+1,hdr,(size_t)hl);
        {size_t p=1+(size_t)hl;
            pkt[p++]=0;pkt[p++]=4;memcpy(pkt+p,"MQTT",4);p+=4;
            pkt[p++]=4;pkt[p++]=2;pkt[p++]=(uint8_t)(m->keepalive>>8);pkt[p++]=(uint8_t)m->keepalive;
            pkt[p++]=(uint8_t)(idlen>>8);pkt[p++]=(uint8_t)idlen;
            memcpy(pkt+p,m->id,idlen);p+=idlen;
            if(sendall(fd,pkt,p)<0){close(fd);delay_ms(500);continue;}
        }
        if(packet(fd,&kind,resp,sizeof(resp),&n)<0||kind!=0x20||n!=2){close(fd);delay_ms(500);continue;}
        if(resp[1]){if(m->on_connect)m->on_connect(m,NULL,resp[1]);close(fd);delay_ms(500);continue;}
        pthread_mutex_lock(&m->lock);m->fd=fd;pthread_mutex_unlock(&m->lock);
        if(m->on_connect)m->on_connect(m,NULL,0);
        while(1){
            struct pollfd pfd={fd,POLLIN,0};int pr=poll(&pfd,1,100);
            pthread_mutex_lock(&m->lock);rc=m->run;pthread_mutex_unlock(&m->lock);
            if(!rc||pr<0||(pr>0&&(pfd.revents&(POLLHUP|POLLERR|POLLNVAL))))break;
            if(pr>0&&(pfd.revents&POLLIN)){
                if(packet(fd,&kind,resp,sizeof(resp),&n)<0)break;
                if(kind==0x40&&n==2){int mid=((int)resp[0]<<8)|resp[1];
                    if(m->on_publish)m->on_publish(m,NULL,mid);
                }
            }
        }
        pthread_mutex_lock(&m->lock);if(m->fd==fd)m->fd=-1;
        rc=m->run;pthread_mutex_unlock(&m->lock);
        close(fd);
        if(m->on_disconnect)m->on_disconnect(m,NULL,rc?7:0);
        if(rc)delay_ms(500);
    }
    return NULL;
}
int mosquitto_lib_init(void){return 0;}
int mosquitto_lib_cleanup(void){return 0;}
struct mosquitto *mosquitto_new(const char *id,bool clean,void *obj){
    struct mosquitto *m=calloc(1,sizeof(*m));(void)clean;(void)obj;
    if(!m)return NULL;
    if(!id||strlen(id)>=sizeof(m->id)){free(m);return NULL;}
    strcpy(m->id,id);m->fd=-1;pthread_mutex_init(&m->lock,NULL);return m;
}
void mosquitto_destroy(struct mosquitto *m){if(m){pthread_mutex_destroy(&m->lock);free(m);}}
void mosquitto_connect_callback_set(struct mosquitto *m,void(*cb)(struct mosquitto*,void*,int)){m->on_connect=cb;}
void mosquitto_disconnect_callback_set(struct mosquitto *m,void(*cb)(struct mosquitto*,void*,int)){m->on_disconnect=cb;}
void mosquitto_publish_callback_set(struct mosquitto *m,void(*cb)(struct mosquitto*,void*,int)){m->on_publish=cb;}
int mosquitto_int_option(struct mosquitto *m,int opt,int val){(void)m;return opt==MOSQ_OPT_PROTOCOL_VERSION&&val==MQTT_PROTOCOL_V311?0:MOSQ_ERR_INVAL;}
int mosquitto_reconnect_delay_set(struct mosquitto *m,unsigned a,unsigned b,bool e){(void)m;(void)a;(void)b;(void)e;return 0;}
int mosquitto_tls_set(struct mosquitto *m,const char *ca,const char *cap,const char *cert,const char *key,int(*pw)(char*,int,int,void*)){
    (void)m;(void)ca;(void)cap;(void)cert;(void)key;(void)pw;return MOSQ_ERR_INVAL;
}
int mosquitto_connect_async(struct mosquitto *m,const char *host,int port,int keepalive){
    if(!host||strlen(host)>=sizeof(m->host))return MOSQ_ERR_INVAL;
    strcpy(m->host,host);m->port=port;m->keepalive=keepalive;return 0;
}
int mosquitto_loop_start(struct mosquitto *m){m->run=1;return pthread_create(&m->th,NULL,worker,m)==0?0:MOSQ_ERR_ERRNO;}
int mosquitto_disconnect(struct mosquitto *m){
    pthread_mutex_lock(&m->lock);m->run=0;
    if(m->fd>=0)shutdown(m->fd,SHUT_RDWR);
    pthread_mutex_unlock(&m->lock);return 0;
}
int mosquitto_loop_stop(struct mosquitto *m,bool force){(void)force;pthread_join(m->th,NULL);return 0;}
int mosquitto_publish(struct mosquitto *m,int *mid,const char *topic,int payloadlen,const void *payload,int qos,bool retain){
    int fd,seq,hlen,kind;size_t tlen=strlen(topic),len=2+tlen+(size_t)payloadlen+(qos>0?2:0);
    uint8_t header[5];uint8_t *buf;size_t used=0;int rc;
    (void)retain;
    pthread_mutex_lock(&m->lock);fd=m->fd;seq=++m->mid;if(m->mid>=65535)m->mid=1;
    if(fd<0){pthread_mutex_unlock(&m->lock);return MOSQ_ERR_NO_CONN;}
    hlen=remaining_len(header,len);if(hlen<0){pthread_mutex_unlock(&m->lock);return MOSQ_ERR_INVAL;}
    buf=malloc(len+(size_t)hlen+1);if(!buf){pthread_mutex_unlock(&m->lock);return MOSQ_ERR_NOMEM;}
    kind=qos?0x32:0x30;buf[used++]=(uint8_t)kind;memcpy(buf+used,header,(size_t)hlen);used+=(size_t)hlen;
    buf[used++]=(uint8_t)(tlen>>8);buf[used++]=(uint8_t)tlen;memcpy(buf+used,topic,tlen);used+=tlen;
    if(qos){buf[used++]=(uint8_t)(seq>>8);buf[used++]=(uint8_t)seq;}
    memcpy(buf+used,payload,(size_t)payloadlen);used+=(size_t)payloadlen;
    rc=sendall(fd,buf,used);free(buf);pthread_mutex_unlock(&m->lock);
    if(mid)*mid=seq;
    if(rc<0)return MOSQ_ERR_NO_CONN;
    /* Critical stress case: immediate QoS0 callback *before* publish returns. */
    if(qos==0&&m->on_publish)m->on_publish(m,NULL,seq);
    return 0;
}
const char *mosquitto_strerror(int err){return err==0?"Success":err==MOSQ_ERR_NO_CONN?"No connection":"Mock library error";}
