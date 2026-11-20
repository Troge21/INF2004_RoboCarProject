/* Actual S1 collector/serializer with deterministic protected producer/RTOS
 * substitutes. No sockets, simulated live feed, GPIO or actuator commands. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#define APP_H
#ifndef APP_TEST
#define APP_TEST 1
#endif
#define APP_BENCH_MODE 0
#define IMPORT extern
#define EXPORT
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_TMOUT (-50)
#define E_NOSPT (-9)
#define TMO_POL 0
#define TMO_FEVR (-1)
#define TA_TFIFO 1
#define TA_HLNG 2
#define TA_RNG0 4
#define TASK_STACK_SIZE 2048
#define APP_IR_MAX_AGE_MS 100U
typedef uint32_t UW,UINT;
typedef uint8_t UB;
typedef int16_t H;
typedef uint16_t UH;
typedef int32_t W,INT,ID,ER,BOOL;
typedef void (*FP)(INT,void *);
typedef struct {UW lo,hi;} SYSTIM;
typedef struct {UW sematr;INT isemcnt,maxsem;} T_CSEM;
typedef struct {UW tskatr;FP task;INT itskpri,stksz;} T_CTSK;
static int failures,checks,blocked,ticks,source_calls;
static UW calendar_ms=1000000,boot_ms=1000;
static jmp_buf stopped;
static ER tk_get_tim(SYSTIM *t){t->lo=calendar_ms;t->hi=0;return 0;}
static ER tk_get_otm(SYSTIM *t){t->lo=boot_ms;t->hi=0;return 0;}
static ID tk_cre_sem(const T_CSEM *s){if(s->isemcnt!=1||s->maxsem!=1)abort();return 1;}
static ER tk_wai_sem(ID id,INT n,INT timeout){
    if(id!=1||n!=1||(timeout!=0&&timeout!=-1))abort();
    return blocked?E_TMOUT:0;
}
static ER tk_sig_sem(ID id,INT n){if(id!=1||n!=1)abort();return 0;}
static ID tk_cre_tsk(const T_CTSK *s){if(s->stksz!=2048)abort();return 2;}
static ER tk_sta_tsk(ID id,INT code){(void)code;return id==2?0:E_OBJ;}
static ER tk_dly_tsk(INT delay){
    if(--ticks<=0)longjmp(stopped,1);
    calendar_ms+=(UW)delay;boot_ms+=(UW)delay;return 0;
}
#if APP_TEST
static INT tm_printf(UB *format,...){
    char text[1024];va_list args;int n;va_start(args,format);
    n=vsnprintf(text,sizeof(text),(const char *)format,args);va_end(args);
    fputs(text,stdout);if(strstr(text," FAIL "))++failures;return n;
}
#endif
#include "../../../app_program/subsystem2/subsystem2.h"
#include "../../../app_program/subsystem3/subsystem3.h"
#include "../../../app_program/subsystem4/subsystem4.h"
#include "../../../app_program/subsystem5/subsystem5.h"
#include "../../../app_program/telemetry_sources.h"
static S2_MOTION_DATA motion;
static S3_LINE_DATA line;
static S5_RANGE_DATA range;
static HUMP_TELEMETRY_T imu;
static TELEMETRY_SCAN scan_source;
static TELEMETRY_TRAVEL travel_source;
static int motion_ready,range_ready,line_ready,imu_ready,scan_ready,travel_ready;
ER subsystem2_get_motion(S2_MOTION_DATA *s){++source_calls;*s=motion;return motion_ready?0:E_OBJ;}
ER subsystem3_get_line(S3_LINE_DATA *s){++source_calls;*s=line;return line_ready?0:E_OBJ;}
ER subsystem4_get_telemetry(HUMP_TELEMETRY_T *s){++source_calls;*s=imu;return imu_ready?0:E_OBJ;}
ER subsystem5_get_range(S5_RANGE_DATA *s){++source_calls;*s=range;return range_ready?0:E_OBJ;}
int telemetry_get_scan(TELEMETRY_SCAN *s){*s=scan_source;return scan_ready?0:E_NOSPT;}
int telemetry_get_barcode(TELEMETRY_BARCODE *s){(void)s;return E_NOSPT;}
int telemetry_get_travel(TELEMETRY_TRAVEL *s){if(!travel_ready)return E_OBJ;*s=travel_source;return E_OK;}
int telemetry_get_faults(TELEMETRY_FAULTS *s){(void)s;return E_NOSPT;}
int telemetry_get_resources(TELEMETRY_RESOURCES *s){
    s->timestamp_ms=calendar_ms;s->free_heap=5000;s->largest_free_block=4000;
    s->allocated_stacks=2048;s->stack_high_water=UINT32_MAX;return 0;
}
#include "../../../app_program/subsystem1/s1_protocol.c"
static unsigned mock_controller_word=(APP_TEST?APP_CTRL_TEST:0)|APP_CTRL_DISABLED;
static int arm_result=APP_RESUME_NOT_READY;
int app_controller_snapshot(uint8_t b[5]){
    s1_write_u32(b,boot_ms);b[4]=mock_controller_word;return 0;
}
int app_controller_submit_stop(uint32_t id,uint32_t session,uint32_t deadline){
    (void)id;(void)session;(void)deadline;return 0;
}
int app_controller_take_reply(uint8_t b[10]){(void)b;return E_TMOUT;}
int app_controller_arm_resume(uint32_t deadline,uint32_t *ticket){(void)deadline;if(!arm_result)*ticket=12345;return arm_result;}
int app_controller_submit_resume(uint32_t ticket,uint32_t session,uint32_t deadline){(void)ticket;(void)session;(void)deadline;return APP_RESUME_NOT_READY;}
#include "../../../app_program/subsystem1/subsystem1.c"
#include "../../../app_program/subsystem1/subsystem1_test.c"
static void expect(int value,const char *message){
    ++checks;printf("[S1 bridge] %s %s\n",value?"PASS":"FAIL",message);if(!value)++failures;
}
static void collect(void){ticks=1;if(setjmp(stopped)==0)telemetry_task(0,NULL);}
static void advance(UW ms){calendar_ms+=ms;boot_ms+=ms;}
int main(void){
    UB b[S1_TX_CAPACITY],request[5]={0xa0,1,0,0,0};uint32_t counters[3]={0};int calls,n;
#if APP_TEST
    subsystem1_test();
#endif
    expect(subsystem1_init()==0&&subsystem1_start()==0,"collector starts with one protected publication and no input queue");
    motion_ready=1;motion.encoder_valid=motion.speed_valid=1;
    motion.timestamp_ms=motion.speed_timestamp_ms=calendar_ms;
    motion.actual_left_rpm_x10=motion.actual_right_rpm_x10=350;
    motion.count_left=100;motion.count_right=200;motion.distance_centre_mm_x10=333;
    travel_ready=1;travel_source.timestamp_ms=calendar_ms;travel_source.distance_mm=0;
    collect();calls=source_calls;
    n=s1_network_poll(b,sizeof(b),17);
    expect(n==46&&s1_get_bits(b+13,S1_B_RANGE,12)==0&&s1_get_bits(b+13,S1_B_COUNT_L,32)==100,
        "motion publishes when ultrasonic source is absent");
    expect(s1_get_bits(b+13,S1_B_LEFT,14)==6191,"S2 RPM is converted to actual speed with its tyre geometry");
    expect(s1_get_bits(b+13,S1_B_TRAVEL,32)==0,"real zero path is serialized independently of net travel");
    expect(s1_network_poll(b,sizeof(b),17)==E_TMOUT,"telemetry rate is bounded independently of producer rates");
    expect(s1_network_status(request,5,b,sizeof(b),17)==62&&source_calls==calls,
        "STATUS reserializes snapshots without calling a producer/driver");
    expect(s1_network_aux(b,sizeof(b),17,counters)==40&&s1_get_bits(b+9,19,19)==5000&&
        s1_get_bits(b+9,95,19)==S1_RESOURCE_UNKNOWN,"actual resource inputs and unsupported high-water remain distinct");
    blocked=1;advance(400);
    expect(s1_network_poll(b,sizeof(b),17)==E_TMOUT&&s1_network_status(request,5,b,sizeof(b),17)==16,
        "radio-owner snapshot contention never blocks or invents a measurement");
    blocked=0;advance(101);
    expect(s1_network_poll(b,sizeof(b),17)==46&&s1_get_bits(b+13,S1_B_COUNT_L,32)==0x80000000U,
        "stale motion expires even while the network continues publishing");
    s1_resource_add_stack(4096);
    expect(s1_resource_stack_capacity()==6144,"requested application stack accounting includes successful task starts");
    scan_ready=1;scan_source.count=181;scan_source.width_mm=126;scan_source.sequence=7;scan_source.timestamp_ms=calendar_ms;
    for(unsigned i=0;i<181;++i){scan_source.points[i].angle=i;scan_source.points[i].distance_mm=451;}
    collect();n=s1_network_aux(b,sizeof(b),17,counters);
    expect(n==58&&b[0]==0xb5&&b[14]==0&&b[15]==(16|S1_SCAN_WIDTH_FLAG)&&b[56]==126&&b[57]==0,
        "obstacle width follows the first chunk of its scan");
    s1_network_aux_result(0xb5,0);advance(99);
    expect(s1_network_aux(b,sizeof(b),17,counters)==E_TMOUT,"scan retry has a bounded backoff");
    advance(1);n=s1_network_aux(b,sizeof(b),17,counters);
    expect(n==58&&b[14]==0&&b[56]==126,"rejected scan chunk retries the same indices and width");
    {uint32_t source_boot=s1_read_u32(b+9);unsigned packets=1;
        while(scan_cursor<scan_count){advance(100);n=s1_network_aux(b,sizeof(b),17,counters);
            expect(n>0&&n<=56&&b[0]==0xb5&&!(b[15]&S1_SCAN_WIDTH_FLAG)&&s1_read_u32(b+9)==source_boot,
                "later scan chunks retain the source time and omit the width trailer");++packets;}
        expect(packets==12&&b[15]==5,"all 181 scan points fit twelve bounded packets");
    }
    scan_count=0;scan_source.sequence=8;scan_source.count=2;
    scan_source.points[1].angle=scan_source.points[0].angle;gather_scan();
    expect(scan_count==0,"malformed future scan does not corrupt the published cache");
    advance(400);travel_source.timestamp_ms=calendar_ms;travel_source.distance_mm=1234;
    collect();n=s1_network_poll(b,sizeof(b),17);
    expect(n==46&&s1_get_bits(b+13,S1_B_TRAVEL,32)==1234,"owner path snapshot reaches the existing 46-byte wire field");
    advance(2001);n=s1_network_poll(b,sizeof(b),17);
    expect(n==46&&s1_get_bits(b+13,S1_B_TRAVEL,32)==UINT32_MAX,"stale path expires instead of appearing current");
    imu_ready=1;imu.state=S4_STATE_MONITORING;
    imu.timestamp_ms=imu.pitch_timestamp_ms=calendar_ms;
    imu.flags=S4_FLAG_SENSOR_OK|S4_FLAG_PITCH_VALID|S4_FLAG_ENCODER_HEIGHT;
    imu.current_hump_mm=10;imu.max_hump_mm=20;
    s1_set_bits(imu.terrain,0,11,849);
    s1_set_bits(imu.terrain,11,9,256);
    s1_set_bits(imu.terrain,20,4,S4_MOTION_FORWARD);
    s1_set_bits(imu.terrain,24,2,S4_HUMP_RIGHT);
    s1_set_bits(imu.terrain,26,16,1);
    collect();
    (void)s1_network_aux(b,sizeof(b),17,counters); /* a resource packet may be due */
    advance(500);imu.timestamp_ms=imu.pitch_timestamp_ms=calendar_ms;collect();
    n=s1_network_aux(b,sizeof(b),17,counters);
    expect(n==S1_TERRAIN_BYTES&&b[0]==0xb7&&s1_get_bits(b+9,0,11)==849&&
        s1_get_bits(b+9,24,2)==S4_HUMP_RIGHT,"S4 owner snapshot reaches the bounded terrain auxiliary path");
    advance(500);n=s1_network_aux(b,sizeof(b),17,counters);
    if(n==S1_DIAGNOSTIC_BYTES)n=s1_network_aux(b,sizeof(b),17,counters);
    expect(n==S1_TERRAIN_BYTES&&s1_get_bits(b+9,0,24)==0&&
        s1_get_bits(b+9,24,2)==S4_HUMP_RIGHT,"old live S4 motion expires while the last measured hump remains");

    {
        UB arm[13]={0xa5};
        s1_write_u32(arm+1,99);s1_write_u32(arm+5,17);s1_write_u32(arm+9,boot_ms+1000U);
        n=s1_network_arm_resume(arm,sizeof(arm),b,sizeof(b),17);
        expect(n==11&&b[0]==0xa8&&(((b[9]|((unsigned)b[10]<<8))>>6)&7U)==APP_RESUME_NOT_READY,
            "disabled/test controller returns an explicit Resume rejection");
#if !APP_TEST
        mock_controller_word=APP_CTRL_STOP_LATCHED|APP_CTRL_LATCH;arm_result=0;collect();
        n=s1_network_arm_resume(arm,sizeof(arm),b,sizeof(b),17);
        expect(n==66&&b[0]==0xa6&&s1_read_u32(b+16)==12345&&b[20]==0xb4&&
               s1_get_bits(b+33,S1_B_CONTROL,6)==mock_controller_word,
            "actual S1 packs server authorization with the current normal latched sensor snapshot");
        advance(2001);n=s1_network_arm_resume(arm,sizeof(arm),b,sizeof(b),17);
        expect(n==20&&b[0]==0xa6&&s1_read_u32(b+16)==12345,
            "authorization with no recent publication is header-only, never invented sensor readings");
#endif
        s1_write_u32(arm+5,18);n=s1_network_arm_resume(arm,sizeof(arm),b,sizeof(b),17);
        expect(n==11&&b[0]==0xa8&&(b[9]>>6)==APP_RESUME_SESSION,
            "wrong MQTT session cannot obtain a Resume ticket through S1");
    }
    printf("[S1 bridge] SUMMARY %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
