#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
/* 保留原工程 HTTP 网关鉴权、MQTT 地址发现、JSON 解析与超时处理。 */
#include "gateway.h"
#include "json.h"
#include "auth.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#define HTTP_MAX 8192
#define SOCKET_TIMEOUT_MS 5000

static int sock_timeout(int fd,int ms){
    struct timeval tv={ms/1000,(ms%1000)*1000};
    return setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv))==0 &&
           setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv))==0 ?0:-1;
}
/* IPv4 and DNS, with bounded nonblocking TCP connect. */
static int tcp_connect_timeout(const char *host,int port,int timeout_ms){
    struct addrinfo hints,*addrs=NULL,*p;char portstr[12];int fd=-1;
    memset(&hints,0,sizeof(hints));hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    snprintf(portstr,sizeof(portstr),"%d",port);
    if(getaddrinfo(host,portstr,&hints,&addrs)!=0)return -1;
    for(p=addrs;p;p=p->ai_next){int flags,pr,err=0;socklen_t elen=sizeof(err);struct pollfd f;
        fd=socket(p->ai_family,p->ai_socktype,p->ai_protocol);
        if(fd<0)continue;
        flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);fd=-1;continue;}
        if(connect(fd,p->ai_addr,p->ai_addrlen)!=0){
            if(errno!=EINPROGRESS){close(fd);fd=-1;continue;}
            f.fd=fd;f.events=POLLOUT;f.revents=0;
            do {pr=poll(&f,1,timeout_ms);} while(pr<0&&errno==EINTR);
            if(pr<=0||getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&elen)<0||err!=0){close(fd);fd=-1;continue;}
        }
        if(fcntl(fd,F_SETFL,flags)<0||sock_timeout(fd,SOCKET_TIMEOUT_MS)<0){close(fd);fd=-1;continue;}
        break;
    }
    freeaddrinfo(addrs);return fd;
}
static int send_all(int fd,const void *buf,size_t len){
    const uint8_t *p=buf;size_t off=0;uint64_t until=mono_ms()+SOCKET_TIMEOUT_MS;
    while(off<len){ssize_t n=send(fd,p+off,len-off,MSG_NOSIGNAL);
        if(n>0){off+=(size_t)n;continue;}
        if(n<0&&errno==EINTR)continue;
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)&&mono_ms()<until)continue;
        return -1;
    }
    return 0;
}
/* HTTP body decodes a complete Transfer-Encoding: chunked response if needed. */
static int dechunk(char *p,size_t n,char *out,size_t out_cap){
    size_t pos=0,used=0;
    for(;;){char *end;unsigned long chunk;size_t i=pos;
        while(i+1<n && !(p[i]=='\r'&&p[i+1]=='\n'))i++;
        if(i+1>=n||i-pos>32)return -1;
        p[i]=0;errno=0;chunk=strtoul(p+pos,&end,16);
        if(errno||end==p+pos||(*end&&*end!=';'))return -1;
        pos=i+2;
        if(chunk==0){if(used>=out_cap)return -1;out[used]=0;return 0;}
        if(chunk>n-pos||chunk>out_cap-1-used||pos+chunk+2>n)return -1;
        memcpy(out+used,p+pos,chunk);used+=chunk;pos+=chunk;
        if(p[pos]!='\r'||p[pos+1]!='\n')return -1;
        pos+=2;
    }
}
static int json_mqtt_addr(const jdoc_t *d,int obj,char *out,size_t cap){
    int i;
    if(obj<0||obj>=d->count)return -1;
    if(d->t[obj].type==JT_OBJECT){
        char protocol[32];int pr=json_member(d,obj,"protocol");
        if(pr>=0&&json_get_str(d,pr,protocol,sizeof(protocol))==0){
            if(strcmp(protocol,"mqtt")==0){
                int a=json_member(d,obj,"addr");
                if(a>=0&&json_get_str(d,a,out,cap)==0)return 0;
            }
        }
    }
    if(d->t[obj].type==JT_OBJECT||d->t[obj].type==JT_ARRAY){
        for(i=obj+1;i<d->t[obj].next;){
            int v=i;
            if(d->t[obj].type==JT_OBJECT)v=i+1;
            if(json_mqtt_addr(d,v,out,cap)==0)return 0;
            i=d->t[v].next;
        }
    }
    return -1;
}
static int parse_host_port(const char *addr,char *host,size_t cap,int *port){
    const char *p=addr,*colon;size_t n;char *end;long v;
    if(strncmp(p,"mqtt://",7)==0)p+=7;
    if(strstr(p,"://")||strchr(p,'/')||strchr(p,'@'))return -1;
    colon=strrchr(p,':');
    if(colon){
        n=(size_t)(colon-p);errno=0;v=strtol(colon+1,&end,10);
        if(errno||*end||end==colon+1||v<1||v>65535)return -1;
        *port=(int)v;
    }else{n=strlen(p);*port=1906;}
    if(n==0||n>=cap)return -1;
    memcpy(host,p,n);host[n]=0;return 0;
}
int gateway_http_auth(const char *server,int port,char *gateway_id,size_t id_cap,char *mqtt_host,size_t host_cap,int *mqtt_port){
    char body[512],req[1024],resp[HTTP_MAX+1],decoded[HTTP_MAX+1],addr[128];
    int fd,total=0,n,request_len,body_len,code,content_len=-1;
    char *headend,*headers,*bodyp,*line;size_t headlen,have;
    jdoc_t doc;jtoken_t tokens[256];int idx,chunked=0;
    body_len=snprintf(body,sizeof(body),
        "{\"gatewaySN\":\"%s\",\"algId\":0,\"checkID\":\"%s\","
        "\"devInfo\":{\"name\":\"monitoringHost\",\"description\":\"test\","
        "\"manufacturerId\":\"%s\",\"model\":\"%s\"}}",
        GATEWAY_SN,CHECK_ID,MFG_ID,MODEL_EDGE);
    if(body_len<=0||body_len>=(int)sizeof(body))return -1;
    request_len=snprintf(req,sizeof(req),
        "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\n"
        "Content-Length: %d\r\nConnection: close\r\n\r\n%s",
        AUTH_PATH,server,port,body_len,body);
    if(request_len<=0||request_len>=(int)sizeof(req))return -1;
    fd=tcp_connect_timeout(server,port,SOCKET_TIMEOUT_MS);if(fd<0)return -1;
    if(send_all(fd,req,(size_t)request_len)<0){close(fd);return -1;}
    while(total<(int)sizeof(resp)-1){
        n=(int)recv(fd,resp+total,sizeof(resp)-1-(size_t)total,0);
        if(n<0&&errno==EINTR)continue;
        if(n<=0)break;
        total+=n;resp[total]=0;
        headend=strstr(resp,"\r\n\r\n");
        if(headend){
            /* Content-Length is optional; when present avoid waiting for connection close. */
            if(content_len<0){const char *h=resp;while(h<headend){
                const char *e=strstr(h,"\r\n");if(!e||e>headend)break;
                if(strncasecmp(h,"Content-Length:",15)==0){char *ep;long v=strtol(h+15,&ep,10);
                    if(v>=0&&v<HTTP_MAX)content_len=(int)v;}
                h=e+2;
            }}
            if(content_len>=0 && total>=(int)(headend+4-resp)+content_len)break;
        }
    }
    close(fd);
    if(total<12)return -1;
    resp[total]=0;
    if(sscanf(resp,"HTTP/%*d.%*d %d",&code)!=1||code!=200)return -1;
    headend=strstr(resp,"\r\n\r\n");if(!headend)return -1;
    headlen=(size_t)(headend-resp);headers=resp;
    for(line=headers;line<resp+headlen;){
        char *e=strstr(line,"\r\n");if(!e||e>headend)break;
        if(strncasecmp(line,"Transfer-Encoding:",18)==0){
            char tmp[128];size_t k=(size_t)(e-(line+18));if(k>=sizeof(tmp))return -1;
            memcpy(tmp,line+18,k);tmp[k]=0;if(strcasestr(tmp,"chunked"))chunked=1;
        }
        line=e+2;
    }
    bodyp=headend+4;have=(size_t)(resp+total-bodyp);
    if(chunked){if(dechunk(bodyp,have,decoded,sizeof(decoded))<0)return -1;bodyp=decoded;}
    else if(content_len>=0){if((size_t)content_len>have)return -1;bodyp[content_len]=0;}
    if(json_parse(&doc,bodyp,tokens,256)!=0||tokens[0].type!=JT_OBJECT)return -1;
    idx=json_member(&doc,0,"gatewayId");
    if(idx<0||json_get_str(&doc,idx,gateway_id,id_cap)<0||!gateway_id[0])return -1;
    addr[0]=0;
    if(json_mqtt_addr(&doc,0,addr,sizeof(addr))<0){
        /* Original program fallback, only when platform does not provide MQTT address. */
        if(snprintf(addr,sizeof(addr),"%s:1906",server)>=(int)sizeof(addr))return -1;
    }
    return parse_host_port(addr,mqtt_host,host_cap,mqtt_port);
}
