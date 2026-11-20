/* Pure codec checks in TEST=1; never publish synthetic telemetry or move motors. */
#include "subsystem1.h"
#if APP_TEST
#include "../test_report.h"
#ifdef PICO_RP2040
#include <sys/sysdef.h>
#include <tk/syslib.h>
#endif
EXPORT void subsystem1_test(void)
{
    /* Startup calls this once from the 1 KiB initial task. Keep the large
     * pure-code vectors out of that stack: the console formatter and
     * kernel/interrupt calls also need space while each check prints. */
    static S1_SNAPSHOT s;
    static S1_MOTION_INPUT motion;
    static UB frame[S1_TX_CAPACITY],reply[S1_STATUS_MAX_BYTES];
    static UB scan[S1_SCAN_PACKED_BYTES];
    UB tiny[2]={0xa5,0xa5};
    UB request[13]={0xa2,42,0,0,0,17,0,0,0,255,255,255,255};
    uint32_t id=0,session=0,deadline=0,values[6]={82596,4000,2000,3000,12288,UINT32_MAX};
    uint32_t counters[4]={1,2,3,UINT32_MAX};unsigned i;
    TEST_INIT();
    motion=(S1_MOTION_INPUT){0};
    s1_snapshot_init(&s,UINT32_MAX);
    TEST_CHECK("S1-V4-01",sizeof(s)==79&&s1_pack_frame(&s,0,100,17,APP_CTRL_STARTING,frame,sizeof(frame))==46,
        "one packed frame and byte-packed shared snapshot");
    TEST_CHECK("S1-V4-02",s1_read_u32(frame+1)==UINT32_MAX&&s1_validate_frame(frame,46)==0,
        "full sequence and current framing preserved");
    TEST_CHECK("S1-V4-03",s1_get_bits(frame+13,S1_B_LEFT,14)==16383&&
        s1_get_bits(frame+13,S1_B_COUNT_L,32)==0x80000000U,
        "missing motion is not measured stationary zero");
    TEST_CHECK("S1-V4-04",s1_pack_frame(&s,0,100,17,APP_CTRL_STARTING,tiny,2)<0&&tiny[0]==0xa5&&tiny[1]==0xa5,
        "short output rejected without writes");
    TEST_CHECK("S1-V4-05",s1_set_range(&s,19,1,100)<0&&s1_set_range(&s,4001,1,100)<0&&
        s1_set_range(&s,0,2,100)<0,"range and validity bounds");
    (void)s1_set_range(&s,451,1,100);(void)s1_set_line(&s,3,1,0);
    (void)s1_pack_frame(&s,101,101,17,APP_CTRL_STARTING,frame,sizeof(frame));
    TEST_CHECK("S1-V4-06",s1_get_bits(frame+13,S1_B_RANGE,12)==451&&s1_get_bits(frame+13,S1_B_LINE,3)==0,
        "IR can expire independently of range");
    for(i=0;i<4;++i){
        (void)s1_set_line(&s,i,1,100);
        (void)s1_pack_frame(&s,100,100,17,APP_CTRL_STARTING,frame,sizeof(frame));
        TEST_CHECK("S1-V4-07",s1_get_bits(frame+13,S1_B_LINE,3)==4+i,"all line combinations");
    }
    TEST_CHECK("S1-V4-08",s1_set_imu(&s,150,12,34,2,17,200,200)==0,"real IMU view can be packed");
    (void)s1_pack_frame(&s,301,301,17,APP_CTRL_STARTING,frame,sizeof(frame));
    TEST_CHECK("S1-V4-09",s1_get_bits(frame+13,S1_B_IMU,11)==0&&s1_get_bits(frame+13,S1_B_IMU+11,7)==12,
        "stale pitch clears independently of retained height/status");
    motion.timestamp_ms=motion.speed_timestamp_ms=300;
    motion.encoder_valid=motion.speed_valid=1;motion.mode=0;motion.stop_reason=0;
    (void)s1_set_motion(&s,&motion);(void)s1_pack_frame(&s,301,301,17,APP_CTRL_STARTING,frame,sizeof(frame));
    TEST_CHECK("S1-V4-10",s1_get_bits(frame+13,S1_B_LEFT,14)==5000&&s1_get_bits(frame+13,S1_B_COUNT_L,32)==0,
        "measured zero motion stays valid");
    motion.left_count=2147483648LL;motion.left_speed_x10=5001;
    (void)s1_set_motion(&s,&motion);(void)s1_pack_frame(&s,301,301,17,APP_CTRL_STARTING,frame,sizeof(frame));
    TEST_CHECK("S1-V4-11",s1_get_bits(frame+13,S1_B_LEFT,14)==16383&&
        s1_get_bits(frame+13,S1_B_COUNT_L,32)==0x80000000U,"overflow becomes unavailable, never wraps/clamps");
    TEST_CHECK("S1-V4-12",s1_set_barcode(&s,138,1,42,300)==0&&s1_set_barcode(&s,127,1,42,300)<0,
        "Code39 validity is independent of navigation mapping");
    (void)s1_pack_frame(&s,301,301,17,APP_CTRL_STARTING,frame,sizeof(frame));
    TEST_CHECK("S1-V4-13",s1_get_bits(frame+13,S1_B_BARCODE,7)==11&&s1_get_bits(frame+13,S1_B_NAV,3)==1,
        "barcode A and controller-supplied left intent preserved");
    TEST_CHECK("S1-V4-14",s1_set_faults(&s,256,512,300)<0&&s1_set_faults(&s,256,256,300)==0,
        "faults must be explicitly known before active");
    (void)s1_pack_frame(&s,301,301,17,APP_CTRL_STARTING,frame,sizeof(frame));
    TEST_CHECK("S1-V4-15",s1_pack_status_reply(42,301,0,APP_CTRL_STARTING,17,frame,reply,sizeof(reply))==62&&
        s1_pack_status_reply(42,301,2047,APP_CTRL_STARTING,17,NULL,reply,sizeof(reply))==16,
        "current STATUS has canonical snapshot presence");
    TEST_CHECK("S1-V4-16",s1_parse_stop_request(request,13,&id,&session,&deadline)==0&&
        id==42&&session==17&&deadline==UINT32_MAX,"full STOP lease clock preserved");
    TEST_CHECK("S1-V4-17",s1_pack_stop_reply(42,17,0,APP_CTRL_STOP_LATCHED|APP_CTRL_LATCH,reply,sizeof(reply))==11&&
        s1_pack_stop_reply(42,17,0,APP_CTRL_FAULT|APP_CTRL_LATCH,reply,sizeof(reply))<0,
        "only applied nonfault controller stop can acknowledge success");
    TEST_CHECK("S1-V4-18",s1_valid_control_word(APP_CTRL_SCANNING)&&
        !s1_valid_control_word(APP_CTRL_READY|APP_CTRL_LATCH),"future controller vocabulary retains latch invariants");
    for(i=0;i<181;++i){s1_set_bits(scan,20*i,8,i);s1_set_bits(scan,20*i+8,12,200);}
    TEST_CHECK("S1-V4-19",s1_pack_scan(17,1,301,scan,181,176,0U,reply,sizeof(reply))==29&&reply[15]==5,
        "181 point profile is bounded into small exact chunks");
    TEST_CHECK("S1-V4-20",s1_pack_diagnostics(17,301,values,counters,reply,sizeof(reply))==40&&
        s1_get_bits(reply+9,95,19)==S1_RESOURCE_UNKNOWN,"unsupported high-water stays unavailable");
#ifdef PICO_RP2040
    {
        UW raw_before, elapsed_us, tick;
        UINT intsts;
        /* A kernel sleep can lose clock ticks during long masked startup
         * work in another driver. Use a 1 ms CPU/SysTick busy interval,
         * with local interrupts masked, to check only the hardware timebase.
         * This is shorter than the 10 ms kernel tick; no kernel calls occur
         * while interrupts are masked. */
        DI(intsts);
        raw_before = in_w(TIMER_TIMERAWL);
        WaitUsec(1000);
        elapsed_us = (UW)(in_w(TIMER_TIMERAWL) - raw_before);
        tick = in_w(0x4005802cU);
        EI(intsts);
        tm_printf((UB *)"[CLOCK] tick=%u timer_us=%u cpu_wait_us=1000\n",
                  (unsigned int)tick, (unsigned int)elapsed_us);
        TEST_CHECK("S1-U43", (tick & 0x3ffU) == ((1U << 9) | XOSC_MHz) &&
                   (tick & (1U << 10)) != 0 &&
                   elapsed_us >= 900U && elapsed_us <= 1100U,
                   "microsecond TIMER configured and advancing at 1 MHz");
    }
#endif
    TEST_SUMMARY("subsystem1 v4 codec and startup clock");
}
#endif
