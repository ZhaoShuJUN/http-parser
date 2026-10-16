#include "json.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static void whitespace(const char *s, size_t *p) {
    while(s[*p]==' '||s[*p]=='\n'||s[*p]=='\t'||s[*p]=='\r') ++*p;
}
static int hex4(const char *s, size_t p){
    int n=0, i, h;
    for(i=0;i<4;i++) { char c=s[p+i]; if(!c)return -1;
        h=(c>='0'&&c<='9')?c-'0':(c>='a'&&c<='f')?c-'a'+10:(c>='A'&&c<='F')?c-'A'+10:-1;
        if(h<0)return -1;
        n=(n<<4)|h;
    }
    return n;
}
static int parse_string(const char *s,size_t *p){
    if(s[(*p)++]!='"')return -1;
    while(s[*p] && s[*p]!='"') {
        unsigned char c=(unsigned char)s[(*p)++];
        if(c<0x20) return -1;
        if(c=='\\') {
            char e=s[(*p)++];
            if(e=='u') { if(hex4(s,*p)<0)return -1; *p+=4; }
            else if(e!='"' && e!='\\' && e!='/' && e!='b' && e!='f' && e!='n' && e!='r' && e!='t')return -1;
        }
    }
    if(s[*p]!='"')return -1;
    ++*p;
    return 0;
}
static int token(jdoc_t *d,int start,jtype_t type){
    int i=d->count++;
    if(i>=d->max){d->count=d->max;return -1;}
    d->t[i].start=start;d->t[i].type=type;d->t[i].next=0;d->t[i].end=0;
    return i;
}
static int parse_value(jdoc_t *d,size_t *p,int depth){
    const char *s=d->s; int id;
    if(depth>24)return -1;
    whitespace(s,p);
    if(s[*p]=='{') {
        id=token(d,(int)*p,JT_OBJECT); if(id<0)return -1;
        ++*p;whitespace(s,p);
        if(s[*p]!='}') for(;;){
            int k,v;
            if(s[*p]!='"')return -1;
            k=token(d,(int)*p,JT_STRING); if(k<0||parse_string(s,p)<0)return -1;
            d->t[k].end=(int)*p;d->t[k].next=d->count;
            whitespace(s,p);if(s[(*p)++]!=':')return -1;
            v=parse_value(d,p,depth+1);if(v<0)return -1;
            whitespace(s,p);
            if(s[*p]=='}')break;
            if(s[(*p)++]!=',')return -1;
            whitespace(s,p);
        }
        if(s[*p]!='}')return -1;
        ++*p;
    }else if(s[*p]=='['){
        id=token(d,(int)*p,JT_ARRAY); if(id<0)return -1;
        ++*p;whitespace(s,p);
        if(s[*p]!=']')for(;;){
            if(parse_value(d,p,depth+1)<0)return -1;
            whitespace(s,p);
            if(s[*p]==']')break;
            if(s[(*p)++]!=',')return -1;
        }
        if(s[*p]!=']')return -1;
        ++*p;
    }else if(s[*p]=='"'){
        id=token(d,(int)*p,JT_STRING);if(id<0||parse_string(s,p)<0)return -1;
    }else{
        size_t st=*p;
        id=token(d,(int)st,JT_PRIMITIVE);if(id<0)return -1;
        while(s[*p] && s[*p]!=',' && s[*p]!=']' && s[*p]!='}' && !isspace((unsigned char)s[*p]))++*p;
        if(st==*p)return -1;
        if(!( (*p-st==4 && !memcmp(s+st,"true",4)) || (*p-st==5 && !memcmp(s+st,"false",5)) ||
             (*p-st==4 && !memcmp(s+st,"null",4)) )){
            /* Complete RFC8259 JSON number grammar. */
            size_t q=st;
            if(s[q]=='-')q++;
            if(s[q]=='0')q++;
            else {if(s[q]<'1'||s[q]>'9')return -1;do{q++;}while(s[q]>='0'&&s[q]<='9');}
            if(s[q]=='.'){q++;if(s[q]<'0'||s[q]>'9')return -1;do{q++;}while(s[q]>='0'&&s[q]<='9');}
            if(s[q]=='e'||s[q]=='E'){q++;if(s[q]=='-'||s[q]=='+')q++;if(s[q]<'0'||s[q]>'9')return -1;do{q++;}while(s[q]>='0'&&s[q]<='9');}
            if(q!=*p)return -1;
        }
    }
    d->t[id].end=(int)*p;d->t[id].next=d->count;
    return id;
}
int json_parse(jdoc_t *d,const char *s,jtoken_t *t,int max){
    size_t p=0;
    d->s=s;d->t=t;d->count=0;d->max=max;
    if(max<=0||!s||parse_value(d,&p,0)!=0)return -1;
    whitespace(s,&p);
    return s[p]=='\0'?0:-1;
}
static int string_equals(const jdoc_t *d,int i,const char *key){
    /* Key names used by the protocol are plain ASCII; escaped keys are intentionally not matched. */
    size_t len=strlen(key);const jtoken_t *t=&d->t[i];
    return t->type==JT_STRING && t->end-t->start==(int)(len+2) &&
        memcmp(d->s+t->start+1,key,len)==0;
}
int json_member(const jdoc_t *d,int obj,const char *key){
    int i;
    if(obj<0||obj>=d->count||d->t[obj].type!=JT_OBJECT)return -1;
    for(i=obj+1;i<d->t[obj].next;) {
        int val=i+1;
        if(val>=d->count)return -1;
        if(string_equals(d,i,key))return val;
        i=d->t[val].next;
    }
    return -1;
}
int json_get_str(const jdoc_t *d,int i,char *out,size_t cap){
    const jtoken_t *t;int q,end;size_t n=0;
    if(i<0||i>=d->count||!cap||d->t[i].type!=JT_STRING)return -1;
    t=&d->t[i];q=t->start+1;end=t->end-1;
    while(q<end){unsigned char c=(unsigned char)d->s[q++];
        if(c=='\\') { char e=d->s[q++];
            if(e=='u') {int u=hex4(d->s,q);q+=4;
                /* Identifiers expected here are ASCII. Reject non-ASCII escapes instead of corrupting IDs. */
                if(u<0||u>0x7f||u==0)return -1;
                c=(unsigned char)u;
            } else {switch(e){case 'n':c='\n';break;case 'r':c='\r';break;case 't':c='\t';break;case 'b':c='\b';break;case 'f':c='\f';break;default:c=(unsigned char)e;}}
        }
        if(c==0||n+1>=cap)return -1;
        out[n++]=(char)c;
    }
    out[n]=0;return 0;
}
int json_get_int(const jdoc_t *d,int i,int *out){
    char tmp[32],*end;long x;size_t n;
    if(i<0||i>=d->count||d->t[i].type!=JT_PRIMITIVE)return -1;
    n=(size_t)(d->t[i].end-d->t[i].start);if(!n||n>=sizeof(tmp))return -1;
    memcpy(tmp,d->s+d->t[i].start,n);tmp[n]=0;
    if(tmp[0]=='+'||strchr(tmp,'.')||strchr(tmp,'e')||strchr(tmp,'E'))return -1;
    errno=0;x=strtol(tmp,&end,10);if(errno||*end||x<INT_MIN||x>INT_MAX)return -1;
    *out=(int)x;return 0;
}
int json_copy_raw(const jdoc_t *d,int i,char *out,size_t cap){
    size_t n;
    if(i<0||i>=d->count)return -1;
    n=(size_t)(d->t[i].end-d->t[i].start);
    if(n+1>cap)return -1;
    memcpy(out,d->s+d->t[i].start,n);out[n]=0;return 0;
}
