#include "s1_protocol.h"
#include "../telemetry_sources.h"
#include <string.h>
#include <limits.h>
#define SPEED_NULL 16383U
#define SIGNED_NULL 0x80000000U
#define FAULT_MOTOR 63U
#define FAULT_IMU 64U
#define FAULT_CONTROL 128U
#define S1_BARCODE_MAX_AGE_MS 15000U
uint32_t s1_read_u32(const uint8_t *b) {
    return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
void s1_write_u32(uint8_t *b,uint32_t v) {
    unsigned i;for(i=0;i<4;++i)b[i]=(uint8_t)(v>>(8U*i));
}
uint32_t s1_get_bits(const uint8_t *b,unsigned bit,unsigned width) {
    uint32_t v=0;unsigned i;
    for(i=0;i<width;++i)v|=(uint32_t)((b[(bit+i)/8]>>((bit+i)%8))&1U)<<i;
    return v;
}
void s1_set_bits(uint8_t *b,unsigned bit,unsigned width,uint32_t v) {
    unsigned i;for(i=0;i<width;++i) {
        unsigned n=bit+i,mask=1U<<(n%8);
        b[n/8]=(uint8_t)((b[n/8]&~mask)|(((v>>i)&1U)<<(n%8)));
    }
}
static uint64_t get_imu(const uint8_t *b) {
    return s1_get_bits(b,S1_B_IMU,32)|((uint64_t)s1_get_bits(b,S1_B_IMU+32,2)<<32);
}
static void put_imu(uint8_t *b,uint64_t v) {
    s1_set_bits(b,S1_B_IMU,32,(uint32_t)v);s1_set_bits(b,S1_B_IMU+32,2,(uint32_t)(v>>32));
}
static int height_ok(unsigned h) {return !h||(h>=6&&h<=80);}
static int imu_ok(uint64_t v) {
    unsigned p=v&2047U,h=(v>>11)&127U,m=(v>>18)&127U,s=(v>>25)&7U,f=(v>>28)&63U;
    if(v>>34||p>1801||!height_ok(h)||!height_ok(m)||h>m)return 0;
    if(s==7)return v==((uint64_t)7<<25);
    return !((f&1U)&&(f&32U))&&(!p||((f&1U)&&!(f&32U)&&s>=2&&s<=5));
}
static void time_set(S1_SNAPSHOT *s,unsigned field,uint32_t t){s1_write_u32(s->timestamp[field],t);}
static int expired(const S1_SNAPSHOT *s,unsigned field,uint32_t now,unsigned limit) {
    return (uint32_t)(now-s1_read_u32(s->timestamp[field]))>limit;
}
static void faults(uint8_t *b,unsigned mask,unsigned known,unsigned active) {
    unsigned k=s1_get_bits(b,S1_B_KNOWN,11),a=s1_get_bits(b,S1_B_FAULT,11);
    s1_set_bits(b,S1_B_KNOWN,11,(k&~mask)|(known&mask));
    s1_set_bits(b,S1_B_FAULT,11,(a&~mask)|(active&known&mask));
}
void s1_snapshot_init(S1_SNAPSHOT *s,uint32_t seq) {
    memset(s,0,sizeof(*s));s1_write_u32(s->sequence,seq);
    put_imu(s->body,(uint64_t)7<<25);
    s1_set_bits(s->body,S1_B_LEFT,14,SPEED_NULL);s1_set_bits(s->body,S1_B_RIGHT,14,SPEED_NULL);
    s1_set_bits(s->body,S1_B_COUNT_L,32,SIGNED_NULL);s1_set_bits(s->body,S1_B_COUNT_R,32,SIGNED_NULL);
    s1_set_bits(s->body,S1_B_NET,32,SIGNED_NULL);s1_set_bits(s->body,S1_B_TRAVEL,32,UINT32_MAX);
    s1_set_bits(s->body,S1_B_MODE,2,3);s1_set_bits(s->body,S1_B_REASON,3,7);
}
int s1_set_range(S1_SNAPSHOT *s,uint32_t mm,unsigned valid,uint32_t t) {
    if(!s||valid>1||(valid&&(mm<20||mm>4000)))return -1;
    s1_set_bits(s->body,S1_B_RANGE,12,valid?mm:0);time_set(s,S1_AGE_RANGE,t);return 0;
}
int s1_set_line(S1_SNAPSHOT *s,unsigned pattern,unsigned valid,uint32_t t) {
    if(!s||valid>1||pattern>3)return -1;
    s1_set_bits(s->body,S1_B_LINE,3,valid?4U|pattern:0);time_set(s,S1_AGE_LINE,t);return 0;
}
int s1_set_imu(S1_SNAPSHOT *s,int pitch,unsigned h,unsigned m,unsigned state,
                unsigned flags,uint32_t t,uint32_t pt) {
    uint64_t v;unsigned compact;
    if(!s||state>7||(flags&~0xbfU)||!height_ok(h)||!height_ok(m)||h>m||
       ((flags&16U)&&(pitch< -900||pitch>900)))return -1;
    compact=(flags&15U)|((flags&32U)>>1)|((flags&128U)>>2);
    v=((flags&16U)?(unsigned)(pitch+901):0U)|((uint64_t)h<<11)|
        ((uint64_t)m<<18)|((uint64_t)state<<25)|((uint64_t)compact<<28);
    if(!imu_ok(v))return -1;
    put_imu(s->body,v);time_set(s,S1_AGE_IMU,t);time_set(s,S1_AGE_PITCH,pt);
    faults(s->body,FAULT_IMU,((flags&129U)||state==6)?FAULT_IMU:0,
           ((flags&128U)||state==6)?FAULT_IMU:0);
    return 0;
}
static uint32_t signed_value(int64_t n,unsigned valid) {
    return valid&&n>=-2147483647LL&&n<=2147483647LL?(uint32_t)n:SIGNED_NULL;
}
int s1_set_motion(S1_SNAPSHOT *s,const S1_MOTION_INPUT *m) {
    if(!s||!m||m->encoder_valid>1||m->speed_valid>1||m->mode>2||
       m->stop_reason>5||(m->faults&~FAULT_MOTOR))return -1;
    s1_set_bits(s->body,S1_B_LEFT,14,m->speed_valid&&m->left_speed_x10>=-5000&&m->left_speed_x10<=5000?
                (uint32_t)(m->left_speed_x10+5000):SPEED_NULL);
    s1_set_bits(s->body,S1_B_RIGHT,14,m->speed_valid&&m->right_speed_x10>=-5000&&m->right_speed_x10<=5000?
                (uint32_t)(m->right_speed_x10+5000):SPEED_NULL);
    s1_set_bits(s->body,S1_B_COUNT_L,32,signed_value(m->left_count,m->encoder_valid));
    s1_set_bits(s->body,S1_B_COUNT_R,32,signed_value(m->right_count,m->encoder_valid));
    s1_set_bits(s->body,S1_B_NET,32,signed_value(m->net_mm_x10,m->encoder_valid));
    s1_set_bits(s->body,S1_B_MODE,2,m->mode);s1_set_bits(s->body,S1_B_REASON,3,m->stop_reason);
    faults(s->body,FAULT_MOTOR,FAULT_MOTOR,m->faults);
    time_set(s,S1_AGE_MOTION,m->timestamp_ms);time_set(s,S1_AGE_SPEED,m->speed_timestamp_ms);return 0;
}
int s1_set_travel(S1_SNAPSHOT *s,uint64_t mm,uint32_t t) {
    if(!s||mm>=UINT32_MAX)return -1;
    s1_set_bits(s->body,S1_B_TRAVEL,32,(uint32_t)mm);time_set(s,S1_AGE_TRAVEL,t);return 0;
}
int s1_set_barcode(S1_SNAPSHOT *s,unsigned code,unsigned navigation,uint16_t seq,uint32_t t) {
    unsigned compact=0;
    if(!s||navigation>4||(!code&&navigation)||code>255||(code&&(!(code&128U)||(code&63U)>=43)))return -1;
    if(code)compact=(code&64U)|((code&63U)+1U);
    s1_set_bits(s->body,S1_B_BARCODE,7,compact);s1_set_bits(s->body,S1_B_EVENT,16,seq);
    s1_set_bits(s->body,S1_B_NAV,3,navigation);
    time_set(s,S1_AGE_BARCODE,t);return 0;
}
int s1_set_faults(S1_SNAPSHOT *s,unsigned known,unsigned active,uint32_t t) {
    if(!s||(known&~S1_EXTERNAL_FAULT_MASK)||(active&~known))return -1;
    faults(s->body,S1_EXTERNAL_FAULT_MASK,known,active);time_set(s,S1_AGE_FAULTS,t);return 0;
}
int s1_valid_control_word(unsigned w) {
    unsigned s=w&APP_CTRL_STATE_MASK;
    return w<=APP_CTRL_WORD_MASK&&s<=APP_CTRL_STOPPED&&
        (s!=APP_CTRL_STOP_LATCHED||(w&APP_CTRL_LATCH))&&
        (!(w&APP_CTRL_LATCH)||s==APP_CTRL_DISABLED||s==APP_CTRL_STOP_LATCHED||s==APP_CTRL_FAULT);
}
static int body_ok(const uint8_t *b) {
    unsigned d=s1_get_bits(b,S1_B_RANGE,12),l=s1_get_bits(b,S1_B_LINE,3);
    unsigned a=s1_get_bits(b,S1_B_LEFT,14),c=s1_get_bits(b,S1_B_RIGHT,14);
    unsigned bc=s1_get_bits(b,S1_B_BARCODE,7),reason=s1_get_bits(b,S1_B_REASON,3);
    return s1_get_bits(b,S1_B_NAV,3)<=4&&(!s1_get_bits(b,S1_B_NAV,3)||bc)&&
        (!d||(d>=20&&d<=4000))&&(!l||l>=4)&&
        imu_ok(get_imu(b))&&(a<=10000||a==SPEED_NULL)&&(c<=10000||c==SPEED_NULL)&&
        (!bc||((bc&63U)>=1&&(bc&63U)<=43))&&
        !(s1_get_bits(b,S1_B_FAULT,11)&~s1_get_bits(b,S1_B_KNOWN,11))&&
        s1_valid_control_word(s1_get_bits(b,S1_B_CONTROL,6))&&(reason<=5||reason==7);
}
int s1_validate_frame(const uint8_t *b,unsigned len) {
    return b&&len==S1_FRAME_BYTES&&b[0]==0xb4U&&s1_read_u32(b+9)&&body_ok(b+13)?0:-1;
}
int s1_pack_frame(const S1_SNAPSHOT *s,uint32_t now,uint32_t boot,uint32_t session,
                   unsigned control,uint8_t *out,unsigned capacity) {
    uint8_t f[S1_FRAME_BYTES]={0},*b=f+13;unsigned state=control&APP_CTRL_STATE_MASK;
    if(!s||!out||capacity<sizeof(f)||!session||!s1_valid_control_word(control)||!body_ok(s->body))return -1;
    memcpy(b,s->body,S1_BODY_BYTES);
    if(expired(s,S1_AGE_RANGE,now,2000))s1_set_bits(b,S1_B_RANGE,12,0);
    if(expired(s,S1_AGE_LINE,now,100))s1_set_bits(b,S1_B_LINE,3,0);
    if(expired(s,S1_AGE_IMU,now,1000)){put_imu(b,(uint64_t)7<<25);faults(b,FAULT_IMU,0,0);}
    else if(expired(s,S1_AGE_PITCH,now,100))put_imu(b,get_imu(b)&~(uint64_t)2047);
    if(expired(s,S1_AGE_MOTION,now,500)) {
        s1_set_bits(b,S1_B_COUNT_L,32,SIGNED_NULL);s1_set_bits(b,S1_B_COUNT_R,32,SIGNED_NULL);
        s1_set_bits(b,S1_B_NET,32,SIGNED_NULL);s1_set_bits(b,S1_B_MODE,2,3);
        s1_set_bits(b,S1_B_REASON,3,7);faults(b,FAULT_MOTOR,0,0);
        s1_set_bits(b,S1_B_LEFT,14,SPEED_NULL);s1_set_bits(b,S1_B_RIGHT,14,SPEED_NULL);
    } else if(expired(s,S1_AGE_SPEED,now,500)) {
        s1_set_bits(b,S1_B_LEFT,14,SPEED_NULL);s1_set_bits(b,S1_B_RIGHT,14,SPEED_NULL);
    }
    if(expired(s,S1_AGE_TRAVEL,now,2000))s1_set_bits(b,S1_B_TRAVEL,32,UINT32_MAX);
    if(expired(s,S1_AGE_BARCODE,now,S1_BARCODE_MAX_AGE_MS)){
        s1_set_bits(b,S1_B_BARCODE,7,0);s1_set_bits(b,S1_B_EVENT,16,0);s1_set_bits(b,S1_B_NAV,3,0);
    }
    if(expired(s,S1_AGE_FAULTS,now,2000))faults(b,S1_EXTERNAL_FAULT_MASK,0,0);
    faults(b,FAULT_CONTROL,state?FAULT_CONTROL:0,state==APP_CTRL_FAULT?FAULT_CONTROL:0);
    s1_set_bits(b,S1_B_CONTROL,6,control);
    f[0]=0xb4;s1_write_u32(f+1,s1_read_u32(s->sequence));s1_write_u32(f+5,boot);
    s1_write_u32(f+9,session);memcpy(out,f,sizeof(f));return sizeof(f);
}
int s1_pack_heartbeat(uint32_t counter,uint8_t *out,unsigned capacity) {
    if(!out||capacity<5)return -1;
    out[0]=0x80;s1_write_u32(out+1,counter);return 5;
}
int s1_pack_terrain(uint32_t session, uint32_t uptime, const uint8_t terrain[S1_TERRAIN_BODY_BYTES],
                    uint8_t *out, unsigned capacity) {
    if(!session||!terrain||!out||capacity<S1_TERRAIN_BYTES)return -1;
    const unsigned roll=s1_get_bits(terrain,0,11);
    if(roll>1801||s1_get_bits(terrain,20,4)>11||(terrain[5]&0xfcU)||
       (!roll&&s1_get_bits(terrain,11,13)))return -1;
    out[0]=0xb7U;s1_write_u32(out+1,session);s1_write_u32(out+5,uptime);
    memcpy(out+9,terrain,S1_TERRAIN_BODY_BYTES);
    return S1_TERRAIN_BYTES;
}
int s1_parse_status_request(const uint8_t *b,unsigned n,uint32_t *id) {
    if(!b||!id||n!=5||b[0]!=0xa0||!s1_read_u32(b+1))return -1;
    *id=s1_read_u32(b+1);return 0;
}
int s1_parse_stop_request(const uint8_t *b,unsigned n,uint32_t *id,uint32_t *session,uint32_t *deadline) {
    if(!b||!id||!session||!deadline||n!=13||b[0]!=0xa2||!s1_read_u32(b+1)||!s1_read_u32(b+5))return -1;
    *id=s1_read_u32(b+1);*session=s1_read_u32(b+5);*deadline=s1_read_u32(b+9);return 0;
}
int s1_parse_resume_request(const uint8_t *b,unsigned n,uint8_t marker,
                          uint32_t *id,uint32_t *session,uint32_t *deadline) {
    if(!b||!id||!session||!deadline||n!=13||(marker!=0xa5&&marker!=0xa7)||
       b[0]!=marker||!s1_read_u32(b+1)||!s1_read_u32(b+5))return -1;
    *id=s1_read_u32(b+1);*session=s1_read_u32(b+5);*deadline=s1_read_u32(b+9);return 0;
}
int s1_pack_status_reply(uint32_t id,uint32_t uptime,unsigned age,unsigned control,
                          uint32_t session,const uint8_t *frame,uint8_t *out,unsigned capacity) {
    uint8_t b[S1_STATUS_MAX_BYTES]={0};unsigned n=S1_STATUS_HEADER_BYTES,word=age|(control<<11);
    if(!id||!session||!out||!s1_valid_control_word(control)||(age>2000&&age!=2047))return -1;
    if(age==2047){if(frame)return -1;}
    else {
        if(s1_validate_frame(frame,S1_FRAME_BYTES)||s1_read_u32(frame+9)!=session||
           s1_read_u32(frame+5)!=uptime||s1_get_bits(frame+13,S1_B_CONTROL,6)!=control)return -1;
        n+=S1_FRAME_BYTES;memcpy(b+S1_STATUS_HEADER_BYTES,frame,S1_FRAME_BYTES);
    }
    if(capacity<n)return -1;
    b[0]=0xa1;s1_write_u32(b+1,id);s1_write_u32(b+5,uptime);
    b[9]=word;b[10]=word>>8;b[11]=word>>16;s1_write_u32(b+12,session);
    memcpy(out,b,n);return n;
}
int s1_pack_stop_reply(uint32_t id,uint32_t session,unsigned result,unsigned control,uint8_t *out,unsigned capacity) {
    unsigned word=control|(result<<6);
    if(!id||!session||result>5||!out||capacity<11||!s1_valid_control_word(control)||
       (!result&&(!(control&APP_CTRL_LATCH)||(control&APP_CTRL_STATE_MASK)==APP_CTRL_FAULT)))return -1;
    out[0]=0xa4;s1_write_u32(out+1,id);s1_write_u32(out+5,session);out[9]=word;out[10]=word>>8;return 11;
}
int s1_pack_resume_arm_reply(uint32_t id,uint32_t boot,unsigned age,unsigned control,
                           uint32_t session,uint32_t ticket,const uint8_t *frame,uint8_t *out,unsigned capacity) {
    uint8_t status[S1_STATUS_MAX_BYTES];int n;
    if(!ticket||!out||!(control&APP_CTRL_LATCH)||(control&APP_CTRL_TEST)||
       (control&APP_CTRL_STATE_MASK)!=APP_CTRL_STOP_LATCHED)return -1;
    n=s1_pack_status_reply(id,boot,age,control,session,frame,status,sizeof(status));
    if(n<0||capacity<(unsigned)n+4)return -1;
    memcpy(out,status,16);out[0]=0xa6;s1_write_u32(out+16,ticket);
    if(n>16)memcpy(out+20,status+16,(unsigned)n-16);
    return n+4;
}
int s1_pack_resume_reply(uint32_t id,uint32_t session,unsigned result,unsigned control,uint8_t *out,unsigned capacity) {
    unsigned word=control|(result<<6),state=control&APP_CTRL_STATE_MASK;
    if(!id||!session||result>7||!out||capacity<11||!s1_valid_control_word(control)||
       (!result&&((control&(APP_CTRL_LATCH|APP_CTRL_TEST))||state==APP_CTRL_UNAVAILABLE||
                  state==APP_CTRL_DISABLED||state==APP_CTRL_FAULT)))return -1;
    out[0]=0xa8;s1_write_u32(out+1,id);s1_write_u32(out+5,session);out[9]=word;out[10]=word>>8;return 11;
}
int s1_pack_scan(uint32_t session,uint32_t sequence,uint32_t boot,const uint8_t *scan,
                  unsigned total,unsigned start,unsigned width_mm,uint8_t *out,unsigned capacity) {
    uint8_t b[S1_SCAN_PACKET_BYTES]={0};unsigned i,count,n,previous=0;
    if(!session||!scan||!out||!total||total>181||start>=total||width_mm>TELEMETRY_SCAN_WIDTH_MAX_MM)return -1;
    for(i=0;i<total;++i) {
        unsigned a=s1_get_bits(scan,20*i,8),d=s1_get_bits(scan,20*i+8,12);
        if(a>180||(i&&a<=previous)||(d&&(d<20||d>4000)))return -1;
        previous=a;
    }
    count=total-start;if(count>S1_SCAN_CHUNK_POINTS)count=S1_SCAN_CHUNK_POINTS;
    n=S1_SCAN_HEADER_BYTES+(count*20+7)/8;
    const unsigned include_width=start==0U&&width_mm!=0U;
    if(capacity<n+2U*include_width)return -1;
    b[0]=0xb5;s1_write_u32(b+1,session);s1_write_u32(b+5,sequence);s1_write_u32(b+9,boot);
    b[13]=total;b[14]=start;b[15]=count;
    for(i=0;i<count;++i)s1_set_bits(b+16,20*i,20,s1_get_bits(scan,20*(start+i),20));
    if(include_width){
        b[15]|=S1_SCAN_WIDTH_FLAG;
        b[n++]=(uint8_t)width_mm;b[n++]=(uint8_t)(width_mm>>8);
    }
    memcpy(out,b,n);return n;
}
int s1_pack_diagnostics(uint32_t session,uint32_t uptime,const uint32_t values[6],
                         const uint32_t counters[4],uint8_t *out,unsigned capacity) {
    uint8_t b[S1_DIAGNOSTIC_BYTES]={0};unsigned i;
    if(!session||!values||!counters||!out||capacity<sizeof(b))return -1;
    b[0]=0xb6;s1_write_u32(b+1,session);s1_write_u32(b+5,uptime);
    for(i=0;i<6;++i) {
        uint32_t v=values[i];if(v>270336U)v=S1_RESOURCE_UNKNOWN;
        s1_set_bits(b+9,19*i,19,v);
    }
    for(i=0;i<4;++i)s1_write_u32(b+24+4*i,counters[i]);
    memcpy(out,b,sizeof(b));return sizeof(b);
}
