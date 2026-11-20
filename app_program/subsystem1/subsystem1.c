#include "subsystem1.h"
#include "../subsystem2/subsystem2.h"
#include "../subsystem3/subsystem3.h"
#include "../subsystem4/subsystem4.h"
#include "../subsystem5/subsystem5.h"
/* Avoid newlib size_t in a kernel-header translation unit. */
static void copy_bytes(UB *out,const UB *in,unsigned count)
{
    unsigned i;for(i=0;i<count;++i)out[i]=in[i];
}
static ID publication_sem,telemetry_tid;
static S1_SNAPSHOT publication;
static UB publication_ms[4];
/* Packed completed scan and metadata; one bounded latest profile, not a queue. */
static UB scan_data[S1_SCAN_PACKED_BYTES],scan_time[4],scan_sequence[4],scan_width_mm[2],scan_count;
/* Collector-owned workspace. A native 181-point view plus a second packed
 * scratch array consumed most of this task's stack before any kernel/IRQ
 * frames. Validate here, then pack directly into the protected cache. */
static TELEMETRY_SCAN scan_workspace;
/* Six 19-bit resource fields plus publication time. */
static UB resource_data[19],sampled_heap_min[3]={255,255,7};
static UB requested_stack_bytes[3];
/* Network owner only. Full-width counters/clocks wrap with unsigned arithmetic. */
static UW next_sequence,last_tx,last_aux,last_terrain,last_chunk,last_scan,last_session,network_busy,scan_boot;
static UB scan_cursor,last_chunk_count;
static UW source_now(void){SYSTIM t={0};(void)tk_get_tim(&t);return t.lo;}
EXPORT void s1_resource_add_stack(uint32_t bytes)
{
    uint32_t total;
    /* Initialization only; wait for the tiny copy critical section so stack
     * reservations cannot be silently omitted by a concurrent publication. */
    if(publication_sem<=0||tk_wai_sem(publication_sem,1,TMO_FEVR)!=E_OK)return;
    total=s1_get_bits(requested_stack_bytes,0,19);
    total=bytes>270336U||total>S1_RESOURCE_UNKNOWN-bytes?S1_RESOURCE_UNKNOWN:total+bytes;
    if(total>270336U)total=S1_RESOURCE_UNKNOWN;
    s1_set_bits(requested_stack_bytes,0,19,total);
    (void)tk_sig_sem(publication_sem,1);
}
EXPORT uint32_t s1_resource_stack_capacity(void)
{
    uint32_t total;
    if(publication_sem<=0||tk_wai_sem(publication_sem,1,TMO_POL)!=E_OK)return UINT32_MAX;
    total=s1_get_bits(requested_stack_bytes,0,19);
    (void)tk_sig_sem(publication_sem,1);
    return total==S1_RESOURCE_UNKNOWN?UINT32_MAX:total;
}
EXPORT int s1_network_uptime(uint32_t *out)
{
    SYSTIM t={0};if(!out)return E_PAR;
    if(tk_get_otm(&t)!=E_OK)return E_OBJ;
    *out=t.lo;return E_OK;
}
EXPORT unsigned s1_network_controller_word(void)
{
    UB b[5];uint32_t now;unsigned w=APP_TEST?APP_CTRL_TEST:0U;
    if(app_controller_snapshot(b)!=E_OK||s1_network_uptime(&now)!=E_OK)return w;
    if((uint32_t)(now-s1_read_u32(b))>APP_CTRL_MAX_AGE_MS)return w;
    w|=b[4]&(APP_CTRL_STATE_MASK|APP_CTRL_LATCH);
    return s1_valid_control_word(w)?w:(APP_TEST?APP_CTRL_TEST:0U);
}
static int copy_publication(S1_SNAPSHOT *out,uint32_t *published)
{
    int er;if(publication_sem<=0)return E_OBJ;
    er=tk_wai_sem(publication_sem,1,TMO_POL);
    if(er!=E_OK){++network_busy;return er;}
    *out=publication;*published=s1_read_u32(publication_ms);
    (void)tk_sig_sem(publication_sem,1);return E_OK;
}
EXPORT int s1_network_poll(unsigned char *out,unsigned capacity,uint32_t session)
{
    S1_SNAPSHOT s;uint32_t boot,published;int n;
    if(!out||capacity<S1_FRAME_BYTES||!session)return E_PAR;
    if(s1_network_uptime(&boot)!=E_OK)return E_OBJ;
    if((UW)(boot-last_tx)<S1_PUBLISH_MS)return E_TMOUT;
    if(copy_publication(&s,&published)!=E_OK)return E_TMOUT;
    s1_write_u32(s.sequence,next_sequence++);
    n=s1_pack_frame(&s,source_now(),boot,session,s1_network_controller_word(),out,capacity);
    if(n>0)last_tx=boot;
    return n<0?E_PAR:n;
}
EXPORT int s1_network_status(const unsigned char *request,unsigned length,
                              unsigned char *out,unsigned capacity,uint32_t session)
{
    S1_SNAPSHOT s;UB frame[S1_FRAME_BYTES];uint32_t id,boot,published;
    unsigned age=S1_STATUS_NO_SAMPLE,control=s1_network_controller_word();const UB *ptr=NULL;
    if(s1_parse_status_request(request,length,&id)!=0)return E_PAR;
    if(s1_network_uptime(&boot)!=E_OK)return E_OBJ;
    if(copy_publication(&s,&published)==E_OK) {
        age=(UW)(source_now()-published);
        s1_write_u32(s.sequence,next_sequence-1U);
        if(age<=2000&&s1_pack_frame(&s,source_now(),boot,session,control,frame,sizeof(frame))>0)ptr=frame;
    }
    if(!ptr)age=S1_STATUS_NO_SAMPLE;
    return s1_pack_status_reply(id,boot,age,control,session,ptr,out,capacity);
}
EXPORT int s1_network_submit_stop(uint32_t id,uint32_t session,uint32_t deadline)
{
    if(!(s1_network_controller_word()&APP_CTRL_STATE_MASK))return E_OBJ;
    return app_controller_submit_stop(id,session,deadline);
}
EXPORT int s1_network_command_reply(uint8_t out[10])
{
    int er=app_controller_take_reply(out);return er==E_OK?10:er;
}
EXPORT int s1_network_submit_resume(uint32_t ticket,uint32_t session,uint32_t deadline)
{
    return app_controller_submit_resume(ticket,session,deadline);
}
EXPORT int s1_network_arm_resume(const unsigned char *request,unsigned length,
                                 unsigned char *out,unsigned capacity,uint32_t session)
{
    S1_SNAPSHOT s;UB frame[S1_FRAME_BYTES];uint32_t id,token,deadline,ticket=0,boot,published;
    unsigned control=s1_network_controller_word(),age=S1_STATUS_NO_SAMPLE;const UB *ptr=NULL;int result;
    if(s1_parse_resume_request(request,length,0xa5,&id,&token,&deadline))return E_PAR;
    result=token==session?app_controller_arm_resume(deadline,&ticket):(int)APP_RESUME_SESSION;
    if(result!=0)return s1_pack_resume_reply(id,session,result<0?APP_RESUME_FAULT:(unsigned)result,control,out,capacity);
    if(s1_network_uptime(&boot)!=E_OK)return E_OBJ;
    control=s1_network_controller_word();
    if(copy_publication(&s,&published)==E_OK) {
        age=(UW)(source_now()-published);s1_write_u32(s.sequence,next_sequence-1U);
        if(age<=2000&&s1_pack_frame(&s,source_now(),boot,session,control,frame,sizeof(frame))>0)ptr=frame;
    }
    if(!ptr)age=S1_STATUS_NO_SAMPLE;
    return s1_pack_resume_arm_reply(id,boot,age,control,session,ticket,ptr,out,capacity);
}
/* Optional traffic shares the telemetry topic, but is independently typed and
 * never advances the sensor sequence. At most one bounded packet per poll. */
EXPORT int s1_network_aux(unsigned char *out,unsigned capacity,uint32_t session,
                           const uint32_t counters[3])
{
    uint32_t boot,now=source_now(),seq,t,values[6],counts[4];
    UB packed[S1_SCAN_PACKED_BYTES],count;unsigned i,width_mm;int n;
    if(!out||!session||!counters||capacity<S1_SCAN_PACKET_BYTES)return E_PAR;
    if(s1_network_uptime(&boot)!=E_OK)return E_OBJ;
    if(session!=last_session){last_session=session;scan_cursor=0;last_aux=boot-5000U;last_terrain=boot-500U;}
    if((UW)(boot-last_aux)>=5000U) {
        if(tk_wai_sem(publication_sem,1,TMO_POL)!=E_OK){++network_busy;return E_TMOUT;}
        t=s1_read_u32(resource_data+15);
        for(i=0;i<6;++i)values[i]=s1_get_bits(resource_data,19*i,19);
        (void)tk_sig_sem(publication_sem,1);
        if((UW)(now-t)>2000U)for(i=1;i<6;++i)values[i]=UINT32_MAX;
        for(i=0;i<3;++i)counts[i]=counters[i];
        counts[3]=network_busy;
        n=s1_pack_diagnostics(session,boot,values,counts,out,capacity);
        if(n>0)last_aux=boot;
        return n;
    }
    if((UW)(boot-last_terrain)>=500U) {
        UB terrain[S1_TERRAIN_BODY_BYTES];
        if(tk_wai_sem(publication_sem,1,TMO_POL)!=E_OK){++network_busy;return E_TMOUT;}
        t=s1_read_u32(publication.timestamp[S1_AGE_IMU]);
        uint32_t pitch=s1_read_u32(publication.timestamp[S1_AGE_PITCH]);
        copy_bytes(terrain,publication.terrain,sizeof(terrain));
        (void)tk_sig_sem(publication_sem,1);
        last_terrain=boot;
        if((UW)(now-t)<=1000U) {
            if((UW)(now-pitch)>100U)terrain[0]=terrain[1]=terrain[2]=0;
            if(s1_get_bits(terrain,0,11)||s1_get_bits(terrain,24,2)) {
                n=s1_pack_terrain(session,boot-(UW)(now-t),terrain,out,capacity);
                if(n>0)return n;
            }
        }
    }
    if((UW)(boot-last_chunk)<100U)return E_TMOUT;
    if(tk_wai_sem(publication_sem,1,TMO_POL)!=E_OK){++network_busy;return E_TMOUT;}
    count=scan_count;t=s1_read_u32(scan_time);seq=s1_read_u32(scan_sequence);
    width_mm=scan_width_mm[0]|((unsigned)scan_width_mm[1]<<8);
    if(count)copy_bytes(packed,scan_data,sizeof(packed));
    (void)tk_sig_sem(publication_sem,1);
    if(!count||(UW)(now-t)>S1_SCAN_MAX_AGE_MS)return E_TMOUT;
    if(seq!=last_scan){last_scan=seq;scan_cursor=0;}
    if(scan_cursor>=count)return E_TMOUT;
    if(scan_cursor==0)scan_boot=boot-(UW)(now-t);
    n=s1_pack_scan(session,seq,scan_boot,packed,count,scan_cursor,width_mm,out,capacity);
    if(n>0){last_chunk_count=out[15]&S1_SCAN_COUNT_MASK;scan_cursor=(UB)(scan_cursor+last_chunk_count);last_chunk=boot;}
    return n;
}
EXPORT void s1_network_aux_result(unsigned marker,int accepted)
{
    /* Backpressure retries a scan chunk after the existing 100 ms gate.
     * Ordinary telemetry is replaced with a newer sample rather than replayed. */
    if(marker==0xb5U&&!accepted&&scan_cursor>=last_chunk_count)
        scan_cursor=(UB)(scan_cursor-last_chunk_count);
}
static void gather_scan(void)
{
    TELEMETRY_SCAN *s=&scan_workspace;unsigned i,previous=0;
    if(telemetry_get_scan(s)!=E_OK||!s->count||s->count>181||s->width_mm>TELEMETRY_SCAN_WIDTH_MAX_MM)return;
    if((UW)(source_now()-s->timestamp_ms)>S1_SCAN_MAX_AGE_MS)return;
    /* An ID denotes one immutable completed profile. Never consume a scan queue. */
    if(scan_count&&s->sequence==s1_read_u32(scan_sequence))return;
    for(i=0;i<s->count;++i) {
        unsigned a=s->points[i].angle,d=s->points[i].distance_mm;
        if(a>180||(i&&a<=previous)||(d&&(d<20||d>4000)))return;
        previous=a;
    }
    if(tk_wai_sem(publication_sem,1,TMO_POL)!=E_OK)return;
    for(i=0;i<s->count;++i) {
        s1_set_bits(scan_data,20*i,8,s->points[i].angle);
        s1_set_bits(scan_data,20*i+8,12,s->points[i].distance_mm);
    }
    scan_count=s->count;
    scan_width_mm[0]=(UB)s->width_mm;scan_width_mm[1]=(UB)(s->width_mm>>8);
    s1_write_u32(scan_time,s->timestamp_ms);s1_write_u32(scan_sequence,s->sequence);
    (void)tk_sig_sem(publication_sem,1);
}
static uint32_t linked_static_ram(void)
{
#if defined(__arm__)
    extern char __data_start[],__data_end[],__bss_start[],__bss_end[],__vector_start[],__vector_end[];
    return (uint32_t)((uintptr_t)__data_end-(uintptr_t)__data_start+
                      (uintptr_t)__bss_end-(uintptr_t)__bss_start+
                      (uintptr_t)__vector_end-(uintptr_t)__vector_start);
#else
    return UINT32_MAX;
#endif
}
static void gather_resources(void)
{
    TELEMETRY_RESOURCES r;uint32_t v[6],minimum=s1_get_bits(sampled_heap_min,0,19);
    UB packed[19]={0};unsigned i;
    for(i=0;i<6;++i)v[i]=UINT32_MAX;
    v[0]=linked_static_ram();
    if(telemetry_get_resources(&r)==E_OK&&(UW)(source_now()-r.timestamp_ms)<=2000U) {
        v[1]=r.free_heap;v[2]=r.largest_free_block;v[4]=r.allocated_stacks;v[5]=r.stack_high_water;
        if(r.free_heap<=270336U&&r.free_heap<minimum) {
            minimum=r.free_heap;s1_set_bits(sampled_heap_min,0,19,minimum);
        }
        v[3]=minimum;
    }
    for(i=0;i<6;++i)s1_set_bits(packed,19*i,19,v[i]<=270336U?v[i]:S1_RESOURCE_UNKNOWN);
    s1_write_u32(packed+15,source_now());
    if(tk_wai_sem(publication_sem,1,TMO_POL)!=E_OK)return;
    copy_bytes(resource_data,packed,sizeof(packed));(void)tk_sig_sem(publication_sem,1);
}
static void gather_sources(S1_SNAPSHOT *s)
{
    S5_RANGE_DATA range;S3_LINE_DATA line;HUMP_TELEMETRY_T imu;
    S2_MOTION_DATA motion;S1_MOTION_INPUT m;TELEMETRY_TRAVEL travel;
    TELEMETRY_BARCODE barcode;TELEMETRY_FAULTS f;
    if(subsystem5_get_range(&range)==E_OK)
        (void)s1_set_range(s,range.distance_mm,range.valid!=0,range.timestamp_ms);
    if(subsystem3_get_line(&line)==E_OK)
        (void)s1_set_line(s,line.pattern,line.valid!=0,line.timestamp_ms);
    if(subsystem4_get_telemetry(&imu)==E_OK &&
       s1_set_imu(s,imu.pitch_deg_x10,imu.current_hump_mm,imu.max_hump_mm,
                  imu.state,imu.flags,imu.timestamp_ms,imu.pitch_timestamp_ms)==0)
        copy_bytes(s->terrain,imu.terrain,sizeof(s->terrain));
    if(subsystem2_get_motion(&motion)==E_OK) {
        m.timestamp_ms=motion.timestamp_ms;m.speed_timestamp_ms=motion.speed_timestamp_ms;
        /* Exact same 355/113 circumference approximation as S2; unit change only. */
        m.left_speed_x10=(int64_t)motion.actual_left_rpm_x10*S2_WHEEL_DIAMETER_MM*355/(113*60);
        m.right_speed_x10=(int64_t)motion.actual_right_rpm_x10*S2_WHEEL_DIAMETER_MM*355/(113*60);
        m.left_count=motion.count_left;m.right_count=motion.count_right;
        m.net_mm_x10=motion.distance_centre_mm_x10;m.encoder_valid=motion.encoder_valid!=0;
        m.speed_valid=motion.speed_valid!=0;m.faults=motion.faults;
        m.mode=motion.mode;m.stop_reason=motion.stop_reason;
        (void)s1_set_motion(s,&m);
    }
    if(telemetry_get_travel(&travel)==E_OK)(void)s1_set_travel(s,travel.distance_mm,travel.timestamp_ms);
    if(telemetry_get_barcode(&barcode)==E_OK)
        (void)s1_set_barcode(s,barcode.code,barcode.navigation,barcode.event_sequence,barcode.timestamp_ms);
    if(telemetry_get_faults(&f)==E_OK)(void)s1_set_faults(s,f.known,f.active,f.timestamp_ms);
}
static void telemetry_task(INT stacd,void *exinf)
{
    S1_SNAPSHOT s;UW auxiliary=source_now()-1000U;
    (void)stacd;(void)exinf;s1_snapshot_init(&s,0);
    for(;;) {
        gather_sources(&s);
        if(tk_wai_sem(publication_sem,1,TMO_POL)==E_OK) {
            publication=s;s1_write_u32(publication_ms,source_now());
            (void)tk_sig_sem(publication_sem,1);
        }
        if((UW)(source_now()-auxiliary)>=1000U) {
            auxiliary=source_now();gather_scan();gather_resources();
        }
        (void)tk_dly_tsk(S1_GATHER_MS);
    }
}
EXPORT ER subsystem1_init(void)
{
    T_CSEM sem={0};T_CTSK task={0};unsigned i;
    if(publication_sem>0)return E_OBJ;
    s1_snapshot_init(&publication,0);
    for(i=0;i<6;++i)s1_set_bits(resource_data,19*i,19,S1_RESOURCE_UNKNOWN);
    sem.sematr=TA_TFIFO;sem.isemcnt=sem.maxsem=1;
    publication_sem=tk_cre_sem(&sem);if(publication_sem<E_OK)return publication_sem;
    task.tskatr=TA_HLNG|TA_RNG0;task.task=(FP)telemetry_task;
    task.itskpri=S1_TASK_PRIORITY;task.stksz=TASK_STACK_SIZE;
    telemetry_tid=tk_cre_tsk(&task);
    return telemetry_tid<E_OK?telemetry_tid:E_OK;
}
EXPORT ER subsystem1_start(void)
{
    ER er=telemetry_tid>0?tk_sta_tsk(telemetry_tid,0):E_OBJ;
    if(er==E_OK)s1_resource_add_stack(TASK_STACK_SIZE);
    return er;
}
