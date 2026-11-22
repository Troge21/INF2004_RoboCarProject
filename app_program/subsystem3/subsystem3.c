/* Integrated AO owner. GP26 is now right analog IR and GP16 an encoder:
 * the old digital barcode capture cannot run on this wiring. */
#include "subsystem3.h"
#include "line_detection.h"
#include <config.h>
#if APP_S3_BARCODE_DETECT
#include <sys/sysdef.h>
#include <tk/syslib.h>

#define BARCODE_TIMER_IRQ 2U
#define BARCODE_ALARM_BIT (1U << BARCODE_TIMER_IRQ)
#define BARCODE_MAX_GAP_US 10000U
#define BARCODE_FILTER_SAMPLES 5U
/* At the installed ride height, blank A4 read 99..156 and the printed
 * black bar read 174..251. Keep both switching levels in that gap. */
#define BARCODE_BLACK_ADC 170U
#define BARCODE_WHITE_ADC 162U
#define BARCODE_EDGE_CONFIRM_SAMPLES 5U
#define BARCODE_SIGNAL_PERIOD_US 200000U
#define BARCODE_HISTORY_RUNS (S3_BC_RUNS+3U)
#define BARCODE_HISTORY_IDLE_US 1500000U

static ID barcode_sample_ready;
static UW barcode_last_sample_us;
static UW barcode_edge_candidate_us;
static uint16_t barcode_adc[BARCODE_FILTER_SAMPLES];
static unsigned barcode_adc_slot;
static unsigned barcode_edge_samples;
static BOOL barcode_sample_valid, barcode_black;
static S3_BARCODE_SIGNAL barcode_signal;
static UW barcode_history[BARCODE_HISTORY_RUNS];
static UW barcode_history_edge_us, barcode_analog_quiet_us;
static unsigned barcode_history_count;
static UB barcode_pending_code;

static void barcode_sample_isr(UINT intno)
{
    (void)intno;
    out_w(TIMER_INTR,BARCODE_ALARM_BIT);
    out_w(TIMER_ALARM2,in_w(TIMER_TIMERAWL)+S3_BC_ANALOG_SAMPLE_US);
    (void)tk_set_flg(barcode_sample_ready,1U);
}

static ER barcode_timer_start(void)
{
    if((in_w(TIMER_ARMED)|in_w(TIMER_INTE))&BARCODE_ALARM_BIT)return E_OBJ;
    T_CFLG flag={0};
    T_DINT irq={.intatr=TA_HLNG,.inthdr=(FP)barcode_sample_isr};
    barcode_sample_ready=tk_cre_flg(&flag);
    if(barcode_sample_ready<E_OK)return barcode_sample_ready;
    ER er=tk_def_int(BARCODE_TIMER_IRQ,&irq);
    if(er!=E_OK){
        (void)tk_del_flg(barcode_sample_ready);
        return er;
    }
    out_w(TIMER_INTR,BARCODE_ALARM_BIT);
    ClearInt(BARCODE_TIMER_IRQ);
    set_w(TIMER_INTE,BARCODE_ALARM_BIT);
    out_w(TIMER_ALARM2,in_w(TIMER_TIMERAWL)+S3_BC_ANALOG_SAMPLE_US);
    EnableInt(BARCODE_TIMER_IRQ,3);
    return E_OK;
}

static void barcode_record_run(BOOL black,UW edge_us)
{
    const UW duration=edge_us-barcode_history_edge_us;
    barcode_history_edge_us=edge_us;
    barcode_analog_quiet_us=0;
    barcode_pending_code=0;
    if(barcode_history_count==BARCODE_HISTORY_RUNS){
        for(unsigned i=1U;i<BARCODE_HISTORY_RUNS;++i)barcode_history[i-1U]=barcode_history[i];
        --barcode_history_count;
    }
    barcode_history[barcode_history_count++]=duration;
    if(black || barcode_history_count<=S3_BC_RUNS)return;

    /* Try both window lengths after an ending black run. Either requires
     * the same stable letter/direction and actual trailing white before use. */
    for(unsigned runs=S3_BC_RUNS;runs<barcode_history_count;runs+=2U){
        const unsigned start=barcode_history_count-runs-1U;
        UW quiet;
        UB code;
        if(s3_barcode_decode_analog(barcode_history+start+1U,runs,barcode_history[start],
                                    &quiet,&code)!=S3_BC_OK)continue;
        if(barcode_pending_code && barcode_pending_code!=code){
            barcode_pending_code=0;
            barcode_analog_quiet_us=0;
            return;
        }
        barcode_pending_code=code;
        if(quiet>barcode_analog_quiet_us)barcode_analog_quiet_us=quiet;
    }
}

/* Runs only in the existing ADC owner. A missed sampling window discards
 * the partial scan, and the decoder must see real white before a new scan. */
static UB barcode_sample(UW raw,UW now_us)
{
    const BOOL contiguous=barcode_sample_valid && now_us-barcode_last_sample_us<=BARCODE_MAX_GAP_US;
    if(barcode_sample_valid && !contiguous)++barcode_signal.gaps;
    barcode_last_sample_us=now_us;
    barcode_sample_valid=TRUE;
    if(!contiguous){
        barcode_edge_samples=0;
        barcode_signal.run_count=0;
        barcode_history_count=0;
        barcode_analog_quiet_us=0;
        barcode_pending_code=0;
        barcode_history_edge_us=now_us;
        for(unsigned i=0;i<BARCODE_FILTER_SAMPLES;++i)barcode_adc[i]=(uint16_t)raw;
        barcode_adc_slot=0;
        barcode_black=raw>=BARCODE_BLACK_ADC;
        return 0;
    }

    /* Median-equivalent vote filters outliers without averaging bright and
     * dark levels together at each edge. That would shrink faint bars. */
    barcode_adc[barcode_adc_slot]=(uint16_t)raw;
    if(++barcode_adc_slot==BARCODE_FILTER_SAMPLES)barcode_adc_slot=0;
    const UW threshold=barcode_black?BARCODE_WHITE_ADC+1U:BARCODE_BLACK_ADC;
    unsigned dark_count=0;
    for(unsigned i=0;i<BARCODE_FILTER_SAMPLES;++i)dark_count+=barcode_adc[i]>=threshold;
    const BOOL black=dark_count>BARCODE_FILTER_SAMPLES/2U;

    if(black==barcode_black){
        barcode_edge_samples=0;
        if(black)return 0;
        const UW elapsed=now_us-barcode_history_edge_us;
        if(barcode_pending_code && elapsed>=barcode_analog_quiet_us){
            const UB code=barcode_pending_code;
            barcode_pending_code=0;
            barcode_history_count=0;
            return code;
        }
        if(!barcode_pending_code && barcode_history_count>=2U && elapsed>S3_BC_MAX_RUN_US){
            ++barcode_signal.rejected;
            barcode_history_count=0;
        }
        if(elapsed>BARCODE_HISTORY_IDLE_US)
            barcode_history_edge_us=now_us-BARCODE_HISTORY_IDLE_US;
        return 0;
    }

    if(!barcode_edge_samples)barcode_edge_candidate_us=now_us;
    if(++barcode_edge_samples<BARCODE_EDGE_CONFIRM_SAMPLES)return 0;
    barcode_edge_samples=0;
    barcode_black=black;
    ++barcode_signal.edges;
    const UW duration=barcode_edge_candidate_us-barcode_history_edge_us;
    const UW ticks=duration/S3_BARCODE_SIGNAL_RUN_US;
    barcode_signal.runs[barcode_signal.next_run]=(UB)(ticks>255U?255U:ticks);
    if(++barcode_signal.next_run==S3_BARCODE_SIGNAL_RUNS)barcode_signal.next_run=0;
    if(barcode_signal.run_count<S3_BARCODE_SIGNAL_RUNS)++barcode_signal.run_count;
    /* A confirmed new bar ends the preceding white interval. */
    UB code=0;
    if(black && barcode_pending_code){
        if(duration>=barcode_analog_quiet_us){
            code=barcode_pending_code;
            barcode_history_count=0;
        }else if(duration>S3_BC_MAX_RUN_US){
            ++barcode_signal.rejected;
            barcode_history_count=0;
        }
    }
    barcode_record_run(black,barcode_edge_candidate_us);
    return code;
}

static void sample_line_and_barcode(void)
{
    const UW line_period_us=S3_IR_PERIOD_MS*1000U;
    UW last_line_us=in_w(TIMER_TIMERAWL)-line_period_us;
    UW last_signal_us=last_line_us;
    UW detection_ms=0;
    UB detection_pending=0;
    BOOL channel_changed=TRUE;
    for(;;){
        UINT flags;
        const ER wake=tk_wai_flg(barcode_sample_ready,1U,TWF_ORW|TWF_BITCLR,
                                &flags,S3_IR_PERIOD_MS);
        const UW now_us=in_w(TIMER_TIMERAWL);
        const UW elapsed=now_us-last_line_us;
        if(elapsed>=line_period_us){
            last_line_us=now_us-elapsed%line_period_us;
            s3_line_sample();
            channel_changed=TRUE;
        }
        UW raw;
        if(wake!=E_OK || s3_barcode_read_ao(&raw,channel_changed)!=E_OK){
            if(barcode_sample_valid)++barcode_signal.gaps;
            barcode_sample_valid=FALSE;
            barcode_signal.run_count=0;
            channel_changed=TRUE;
            barcode_signal.sample_word=0;
        }else{
            channel_changed=FALSE;
            const UB code=barcode_sample(raw,in_w(TIMER_TIMERAWL));
            if(code){
                SYSTIM now={0};
                (void)tk_get_tim(&now);
                detection_ms=now.lo;
                detection_pending=code;
            }
            barcode_signal.sample_word=(uint16_t)(raw|S3_BARCODE_SIGNAL_VALID|
                (barcode_black?S3_BARCODE_SIGNAL_BLACK:0));
        }
        if(now_us-last_signal_us>=BARCODE_SIGNAL_PERIOD_US){
            SYSTIM now={0};
            (void)tk_get_tim(&now);
            barcode_signal.timestamp_ms=now.lo;
            s3_publish_barcode_signal(&barcode_signal);
            last_signal_us=now_us;
        }
        if(detection_pending && s3_publish_barcode_detection(detection_ms,detection_pending)!=E_TMOUT)
            detection_pending=0;
    }
}
#endif
EXPORT ER subsystem3_get_barcode(UB *code)
{
    if(code==NULL) return E_PAR;
    return E_TMOUT; /* Detection milestone does not expose navigation input. */
}
static void ir_task(INT stacd,void *exinf)
{
    /* A positive relative delay includes one extra kernel tick. */
    const UINT delay_budget_ms=S3_IR_PERIOD_MS>CNF_TIMER_PERIOD ?
        S3_IR_PERIOD_MS-CNF_TIMER_PERIOD:0U;
    ER configured;
    (void)stacd;(void)exinf;
    configured=s3_line_configure();
#if APP_S3_BARCODE_DETECT
    while(configured!=E_OK){
        CHECK(configured,"S3 ADC setup");
        (void)tk_dly_tsk(1000);
        configured=s3_line_configure();
    }
    ER timer=barcode_timer_start();
    if(timer==E_OK)sample_line_and_barcode();
    CHECK(timer,"S3 barcode timer");
#endif
    for(;;){
        if(configured!=E_OK){
            CHECK(configured,"S3 ADC setup");
            (void)tk_dly_tsk(1000);
            configured=s3_line_configure();
            continue;
        }
        SYSTIM started={0},now={0};
        (void)tk_get_otm(&started);
        s3_line_sample();
        (void)tk_get_otm(&now);
        const UW elapsed=(UW)(now.lo-started.lo);
        (void)tk_dly_tsk(elapsed<delay_budget_ms?delay_budget_ms-elapsed:1U);
    }
}
EXPORT ER subsystem3_init(void){return s3_line_init();}
EXPORT ER subsystem3_start(void)
{
    ID tid=app_start_task((FP)ir_task,S3_PRI_IR,TP_PRC1,"ir_task");
    return tid<E_OK ? tid:E_OK;
}
