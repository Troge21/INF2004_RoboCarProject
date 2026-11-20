/* Offline actual-C vectors. Output is never sent to a broker. */
#include <stdio.h>
#include <string.h>
#include "../../../app_program/subsystem1/s1_protocol.h"
#include "../../../app_program/subsystem3/barcode_decoder.h"
static void emit(const char *kind,const uint8_t *bytes,unsigned n,const char *fields){
    unsigned i;printf("{\"kind\":\"%s\",\"hex\":\"",kind);
    for(i=0;i<n;++i)printf("%02x",bytes[i]);
    printf("\"%s}\n",fields);
}
int main(void){
    S1_SNAPSHOT s;S1_MOTION_INPUT m={0};uint8_t out[S1_TX_CAPACITY],frame[S1_FRAME_BYTES];
    uint8_t scan[S1_SCAN_PACKED_BYTES]={0};unsigned i,j,d,line,n;
    char fields[256];const unsigned lines[]={0,4,5,6,7};
    const int speeds[]={-5001,-5000,-1,0,1,5000,5001};
    const int64_t counts[]={-2147483648LL,-2147483647LL,-1,0,1,2147483647LL,2147483648LL};
    s1_snapshot_init(&s,1);
    n=s1_pack_frame(&s,0,0,17,0,out,sizeof(out));if(n!=46)return 1;
    emit("empty",out,n,"");
    for(d=20;d<=4000;++d)for(j=0;j<5;++j){
        line=lines[j];s1_snapshot_init(&s,d*5+j);
        if(s1_set_range(&s,d,1,1000)||s1_set_line(&s,line&3,line!=0,1000))return 2;
        n=s1_pack_frame(&s,1000,1000,17,APP_CTRL_FOLLOWING,out,sizeof(out));if(n!=46)return 3;
        snprintf(fields,sizeof(fields),",\"distance\":%u,\"line\":%u,\"sequence\":%u",d,line,d*5+j);
        emit("base",out,n,fields);
    }
    s1_snapshot_init(&s,UINT32_MAX);(void)s1_set_range(&s,0,0,1000);
    n=s1_pack_frame(&s,1000,1000,17,APP_CTRL_FOLLOWING,out,sizeof(out));
    emit("base",out,n,",\"distance\":0,\"line\":0,\"sequence\":4294967295");
    m.encoder_valid=m.speed_valid=1;m.timestamp_ms=m.speed_timestamp_ms=1000;
    for(i=0;i<7;++i)for(j=0;j<7;++j){
        s1_snapshot_init(&s,12);m.left_speed_x10=speeds[i];m.right_speed_x10=0;
        m.left_count=counts[j];m.right_count=0;m.net_mm_x10=-1234;
        if(s1_set_motion(&s,&m))return 4;
        n=s1_pack_frame(&s,1000,1000,17,APP_CTRL_FOLLOWING,out,sizeof(out));
        snprintf(fields,sizeof(fields),",\"speed\":%d,\"count\":%lld",speeds[i],(long long)counts[j]);
        emit("motion",out,n,fields);
    }
    for(i=0;i<43;++i)for(j=0;j<2;++j){
        unsigned code=128U|i|(j?64U:0U);s1_snapshot_init(&s,12);
        if(s1_set_barcode(&s,code,i%5,65535,1000))return 5;
        n=s1_pack_frame(&s,1000,1000,17,APP_CTRL_FOLLOWING,out,sizeof(out));
        snprintf(fields,sizeof(fields),",\"character\":\"%c\",\"reversed\":%u,\"navigation\":%u",
                 s3_barcode_character((uint8_t)code),j,i%5);emit("barcode",out,n,fields);
    }
    for(int pitch=-900;pitch<=900;pitch+=75){
        s1_snapshot_init(&s,12);if(s1_set_imu(&s,pitch,12,34,2,17,1000,1000))return 6;
        n=s1_pack_frame(&s,1000,1000,17,APP_CTRL_FOLLOWING,out,sizeof(out));
        snprintf(fields,sizeof(fields),",\"pitch\":%d",pitch);emit("imu",out,n,fields);
    }
    s1_snapshot_init(&s,12);(void)s1_set_range(&s,451,1,1000);
    (void)s1_pack_frame(&s,1000,1000,17,APP_CTRL_FOLLOWING,frame,sizeof(frame));
    for(i=0;i<2;++i){
        n=s1_pack_status_reply(UINT32_MAX,1000,i?2047:2000,APP_CTRL_FOLLOWING,17,
                               i?NULL:frame,out,sizeof(out));
        if((i&&n!=16)||(!i&&n!=62))return 7;
        snprintf(fields,sizeof(fields),",\"missing\":%u",i);emit("status",out,n,fields);
    }
    for(i=0;i<=5;++i){
        unsigned control=i==0?(APP_CTRL_LATCH|APP_CTRL_STOP_LATCHED):
            i==5?(APP_CTRL_LATCH|APP_CTRL_FAULT):APP_CTRL_FOLLOWING;
        n=s1_pack_stop_reply(UINT32_MAX,17,i,control,out,sizeof(out));if(n!=11)return 8;
        snprintf(fields,sizeof(fields),",\"result\":%u",i);emit("stop",out,n,fields);
    }

    (void)s1_pack_frame(&s,1000,1000,17,APP_CTRL_STOP_LATCHED|APP_CTRL_LATCH,frame,sizeof(frame));
    for(i=0;i<2;++i){
        n=s1_pack_resume_arm_reply(UINT32_MAX,1000,i?2047:2000,
             APP_CTRL_STOP_LATCHED|APP_CTRL_LATCH,17,UINT32_MAX,i?NULL:frame,out,sizeof(out));
        if(n!=(i?20:66))return 11;
        snprintf(fields,sizeof(fields),",\"missing\":%u",i);emit("resume_arm",out,n,fields);
    }
    for(i=0;i<8;++i){
        unsigned control=i?APP_CTRL_STOP_LATCHED|APP_CTRL_LATCH:APP_CTRL_FOLLOWING;
        n=s1_pack_resume_reply(UINT32_MAX,17,i,control,out,sizeof(out));if(n!=11)return 12;
        snprintf(fields,sizeof(fields),",\"result\":%u",i);emit("resume",out,n,fields);
    }
    for(i=0;i<181;++i){s1_set_bits(scan,20*i,8,i);s1_set_bits(scan,20*i+8,12,i%7?451:0);}
    for(i=0;i<181;i+=16){
        n=s1_pack_scan(17,99,900,scan,181,i,0U,out,sizeof(out));if(n>56||n<19)return 9;
        snprintf(fields,sizeof(fields),",\"start\":%u",i);emit("scan",out,n,fields);
    }
    {uint32_t values[6]={82596,0,0,0,12288,UINT32_MAX},counters[4]={0,UINT32_MAX,3,4};
        n=s1_pack_diagnostics(17,1000,values,counters,out,sizeof(out));if(n!=40)return 10;emit("resources",out,n,"");}
    {
        uint8_t terrain[S1_TERRAIN_BODY_BYTES]={0};
        s1_set_bits(terrain,0,11,849); /* -5.2 degrees roll */
        s1_set_bits(terrain,11,9,369); /* +113 degrees/s yaw */
        s1_set_bits(terrain,20,4,6);   /* turning left */
        s1_set_bits(terrain,24,2,3);   /* last hump under right wheel */
        s1_set_bits(terrain,26,16,UINT16_MAX);
        n=s1_pack_terrain(17,1000,terrain,out,sizeof(out));
        if(n!=S1_TERRAIN_BYTES)return 13;
        emit("terrain",out,n,"");
        terrain[5]|=128;
        if(s1_pack_terrain(17,1000,terrain,out,sizeof(out))>=0)return 14;
    }
    n=s1_pack_heartbeat(UINT32_MAX,out,sizeof(out));emit("heartbeat",out,n,"");
    return 0;
}
