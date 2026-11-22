/* Actual integrated S3 ADC/snapshot/task, deterministic driver/RTOS only. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#define APP_H
#define EXPORT
#define TRUE 1
#define FALSE 0
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_IO (-57)
#define E_TMOUT (-50)
#define TMO_POL 0
#define TA_TFIFO 0
#define TA_FIRST 0
#define TP_PRC1 1
#define TD_READ 1
#define CNF_TIMER_PERIOD 10U
#define APP_IR_MAX_AGE_MS 100U
#define GPIO_CTRL(pin) (100U+(pin))
#define GPIO(pin) (200U+(pin))
#define GPIO_CTRL_FUNCSEL_NULL 31U
#define GPIO_IE 1U
#define GPIO_PUE 2U
#define GPIO_PDE 4U
#define CHECK(er,name) ((void)(er))
typedef unsigned UINT,BOOL;
typedef int INT,ER,ID,PRI;
typedef uint32_t UW;
typedef unsigned char UB;
typedef long SZ;
typedef void (*FP)();
typedef struct {UW lo,hi;} SYSTIM;
typedef struct {unsigned sematr,isemcnt,maxsem;} T_CSEM;
#include "subsystem3.h"
#if APP_S3_BARCODE_DETECT
#include "../telemetry_sources.h"
#define TA_HLNG 1U
#define TWF_ORW 1U
#define TWF_BITCLR 2U
#define TIMER_ARMED 500U
#define TIMER_INTR 501U
#define TIMER_INTE 502U
#define TIMER_ALARM2 503U
#define TIMER_TIMERAWL 504U
typedef struct {UINT flgatr,iflgptn;} T_CFLG;
typedef struct {UINT intatr;FP inthdr;} T_DINT;
static UW hardware_us,alarm_us,scan_start_us;
static unsigned timer_reserved=1,remaining_samples,scan_mode;
static FP sample_irq;
/* One A between two Code 39 guards, including the two white gaps. */
static UW scan_widths[S3_BC_RUNS];
/* Real reverse-A driving case. One extra 8 ms ink pulse split a white gap. */
static const UW driving_width_ms[]={
    152,200,250,76,238,114,60,260,58,80,214,108,64,232,50,124,
    110,10,8,178,258,88,52,82,196,94,196,80,54,232,48
};
static const UW driving_white_gap_ms[S3_BC_RUNS]={
    68,272,64,100,296,82,230,90,52,98,258,88,60,168,88,
    330,114,116,304,166,64,252,60,94,194,108,224,84,56
};
static const UW driving_guard_ms[][S3_BC_RUNS]={
    {64,480,96,112,198,102,274,196,86,140,216,82,68,130,172,
     340,48,92,202,112,32,272,106,230,262,90,182,96,50},
    {48,298,44,110,442,148,276,82,64,82,234,92,50,106,60,
     384,74,100,208,110,62,216,60,86,192,190,328,86,62},
    /* The median wide run in A is one sample longer than four narrow units. */
    {66,272,68,136,442,80,244,74,74,108,394,128,86,98,40,
     258,56,92,358,174,54,236,72,82,378,130,202,90,56}
};
static UW in_w(UINT reg){
    if(reg==TIMER_ARMED)return timer_reserved?4U:0U;
    if(reg==TIMER_INTE)return 0;
    assert(reg==TIMER_TIMERAWL);return hardware_us;
}
static void set_w(UINT reg,UW value){assert(reg==TIMER_INTE&&value==4);}
static void ClearInt(UINT irq){assert(irq==2);}
static void EnableInt(UINT irq,UINT priority){assert(irq==2&&priority==3);}
static ID tk_cre_flg(const T_CFLG *flag){assert(flag->flgatr==0&&flag->iflgptn==0);return 3;}
static ER tk_del_flg(ID id){assert(id==3);return E_OK;}
static ER tk_def_int(UINT irq,const T_DINT *config){
    assert(irq==2&&config->intatr==TA_HLNG);
    sample_irq=config->inthdr;return E_OK;
}
static ER tk_set_flg(ID id,UINT bits){assert(id==3&&bits==1);return E_OK;}
static UW barcode_raw(void){
    const UW age=hardware_us-scan_start_us;
    if(scan_mode>=6U){
        if(age<1500000U)return 135U;
        const UW *widths=scan_mode==6U?driving_width_ms:
                         scan_mode==7U?driving_white_gap_ms:driving_guard_ms[scan_mode-8U];
        const unsigned count=scan_mode==6U?S3_BC_RUNS+2U:S3_BC_RUNS;
        UW elapsed=age-1500000U;
        for(unsigned i=0;i<count;++i){
            const UW width=widths[i]*1000U;
            if(elapsed<width)return (i&1U)?135U:210U;
            elapsed-=width;
        }
        return 135U;
    }
    const unsigned weak=scan_mode>=4U;
    /* Match the range observed on still white paper and on the black bar.
     * Test a faint print near the cutoff and brief high-contrast changes. */
    const UW white=weak?99U+(age/2000U)%58U:130U;
    const UW ink=weak?174U+(age/2000U)%8U:210U;
    UW elapsed=age;
    if(elapsed<200000U){
        if(weak && elapsed>=160000U && elapsed<168000U)return ink;
        return elapsed%46000U==12000U && elapsed<150000U?ink:white;
    }
    elapsed-=200000U;
    if(scan_mode==1)return elapsed<30000U?ink:white;
    for(unsigned i=0;i<S3_BC_RUNS;++i){
        const UW width=scan_widths[scan_mode%2U==0U?i:S3_BC_RUNS-1U-i];
        if(elapsed<width){
            if(weak && width==30000U && elapsed>=12000U && elapsed<20000U)
                return (i&1U)?ink:white;
            return (i&1U)?white:ink;
        }
        elapsed-=width;
    }
    return white;
}
#endif
static UW clock_ms,configured,pads;
static UW raw_values[3][3];
static unsigned cursor[3],calls,advance,wrong_count,bad_range,locked,blocked,delay_ms;
static int error_channel=-1;
static jmp_buf task_done;
static void out_w(UINT reg,UW value){
#if APP_S3_BARCODE_DETECT
    if(reg==TIMER_ALARM2){alarm_us=value;return;}
    if(reg==TIMER_INTR){assert(value==4);return;}
#endif
    assert(reg>=126U&&reg<=128U&&value==31U);configured|=1U<<(reg-100U);
}
static void clr_w(UINT reg,UW value){assert(reg>=226U&&reg<=228U&&value==7U);pads|=1U<<(reg-200U);}
static ID tk_opn_dev(UB *name,UINT mode){assert(!strcmp((const char *)name,"adca")&&mode==TD_READ);return 7;}
static ER tk_srea_dev(ID id,INT channel,UW *value,INT size,SZ *count){
    assert(id==7&&channel>=0&&channel<=2&&size==1);
    ++calls;clock_ms+=advance;
    if(channel==error_channel)return E_IO;
    *count=wrong_count?0:1;
    *value=bad_range?4096U:raw_values[channel][cursor[channel]++%3U];
#if APP_S3_BARCODE_DETECT
    if(scan_mode&&channel==S3_BARCODE_ADC)*value=barcode_raw();
#endif
    return E_OK;
}
static ER tk_get_tim(SYSTIM *t){t->lo=clock_ms;t->hi=0;return E_OK;}
static ER tk_get_otm(SYSTIM *t){return tk_get_tim(t);}
static unsigned sem_created;
static ID tk_cre_sem(const T_CSEM *s){assert(s->isemcnt==1&&s->maxsem==1);return (ID)++sem_created;}
static ER tk_wai_sem(ID id,INT count,INT timeout){assert(id>=1&&id<=(ID)sem_created&&count==1&&timeout==TMO_POL);if(locked||blocked)return E_TMOUT;locked=1;return E_OK;}
static ER tk_sig_sem(ID id,INT count){assert(id>=1&&id<=(ID)sem_created&&count==1&&locked);locked=0;return E_OK;}
#if APP_S3_BARCODE_DETECT
static ER tk_wai_flg(ID id,UINT bits,UINT mode,UINT *flags,INT timeout){
    assert(id==3&&bits==1&&mode==(TWF_ORW|TWF_BITCLR)&&timeout==S3_IR_PERIOD_MS);
    if(!remaining_samples)longjmp(task_done,1);
    --remaining_samples;
    hardware_us=alarm_us;
    clock_ms=hardware_us/1000U;
    assert(sample_irq);
    ((void (*)(UINT))sample_irq)(2);
    *flags=1;
    return E_OK;
}
#endif
static ER tk_dly_tsk(UINT ms){assert(ms>0&&ms<=1000);delay_ms=ms;longjmp(task_done,1);return E_OK;}
static ID app_start_task(FP entry,PRI priority,UW core,const char *name){(void)entry;assert(priority==17&&core==1&&!strcmp(name,"ir_task"));return 3;}

/* SUBSYSTEM_SOURCE */

#if APP_S3_BARCODE_DETECT
static void barcode_trial(unsigned input){
    scan_mode=input;
    scan_start_us=hardware_us;
    remaining_samples=input>=6U?3500U:500U; /* at 2 ms, including both white margins */
    if(setjmp(task_done)==0)sample_line_and_barcode();
    scan_mode=0;
}
static void test_barcode_acquisition(void){
    const unsigned patterns[]={0x094,0x109,0x094};
    for(unsigned i=0;i<3;++i){
        for(unsigned j=0;j<9;++j)scan_widths[i*10+j]=(patterns[i]&(1U<<(8U-j)))?30000U:10000U;
        if(i<2)scan_widths[i*10+9]=10000U;
    }
    TELEMETRY_BARCODE detection;
    assert(telemetry_get_barcode(&detection)==E_TMOUT);
    advance=0;
    hardware_us=clock_ms*1000U;
    timer_reserved=0;
    assert(barcode_timer_start()==E_OK);
    barcode_trial(1); /* plain black stripe */
    assert(telemetry_get_barcode(&detection)==E_TMOUT);
    S3_BARCODE_SIGNAL signal;
    assert(s3_get_barcode_signal(&signal)==E_OK);
    assert(signal.sample_word==(S3_BARCODE_SIGNAL_VALID|130U));
    assert(signal.edges==2 && signal.rejected==1 && signal.gaps==0);
    assert(signal.run_count==2 && signal.runs[1]==15);
    for(unsigned direction=2;direction<=5;++direction){
        barcode_trial(direction);
        assert(telemetry_get_barcode(&detection)==E_OK&&detection.event_sequence==direction-1);
        assert(s3_barcode_character(detection.code)=='A' && !detection.navigation);
        assert(!!(detection.code&S3_BC_REVERSED)==(direction%2U!=0U));
        assert(clock_ms-detection.timestamp_ms<300U);
        assert(s3_get_barcode_signal(&signal)==E_OK);
        assert(signal.run_count==S3_BARCODE_SIGNAL_RUNS && signal.edges==2U+30U*(direction-1U));
        for(unsigned i=0;i<S3_BC_RUNS;++i){
            const unsigned index=(signal.next_run+1U+i)%S3_BARCODE_SIGNAL_RUNS;
            const UW width=scan_widths[direction%2U==0U?i:S3_BC_RUNS-1U-i];
            assert(signal.runs[index]*S3_BARCODE_SIGNAL_RUN_US==width);
        }
        S3_LINE_DATA line;
        assert(subsystem3_get_line(&line)==E_OK&&line.pattern==3&&clock_ms-line.timestamp_ms<S3_IR_PERIOD_MS);
    }
    UW actual[S3_BC_RUNS+2U],quiet;
    UB code;
    for(unsigned i=0;i<S3_BC_RUNS+2U;++i)actual[i]=driving_width_ms[i]*1000U;
    assert(s3_barcode_decode(actual,1500000U,1500000U,&code)==S3_BC_INVALID);
    assert(s3_barcode_decode_analog(actual,S3_BC_RUNS+2U,1500000U,&quiet,&code)==S3_BC_OK);
    assert(s3_barcode_character(code)=='A' && (code&S3_BC_REVERSED) && quiet==670000U);
    assert(s3_barcode_decode_analog(actual,S3_BC_RUNS+2U,100000U,&quiet,&code)==S3_BC_INVALID);
    /* Complete subsequent passes stretched several runs and a white gap. */
    const UW stretched[][S3_BC_RUNS]={
        {180,194,210,88,228,122,70,238,62,82,202,100,38,430,100,
         84,64,100,176,80,56,92,198,172,306,102,62,232,62},
        {108,122,344,100,224,154,96,312,72,92,184,80,66,400,128,
         130,66,82,190,80,52,94,340,128,204,84,62,214,46}
    };
    for(unsigned j=0;j<sizeof(stretched)/sizeof(stretched[0]);++j){
        for(unsigned i=0;i<S3_BC_RUNS;++i)actual[i]=stretched[j][i]*1000U;
        assert(s3_barcode_decode_analog(actual,S3_BC_RUNS,1500000U,&quiet,&code)==S3_BC_OK);
        assert(s3_barcode_character(code)=='A' && (code&S3_BC_REVERSED));
    }
    for(unsigned j=0;j<sizeof(driving_guard_ms)/sizeof(driving_guard_ms[0]);++j){
        for(unsigned i=0;i<S3_BC_RUNS;++i)actual[i]=driving_guard_ms[j][i]*1000U;
        assert(s3_barcode_decode_analog(actual,S3_BC_RUNS,1500000U,&quiet,&code)==S3_BC_OK);
        assert(s3_barcode_character(code)=='A' && !(code&S3_BC_REVERSED));
        for(unsigned i=20U;i<S3_BC_RUNS;++i)actual[i]=90000U;
        assert(s3_barcode_decode_analog(actual,S3_BC_RUNS,1500000U,&quiet,&code)==S3_BC_INVALID);
    }
    for(unsigned input=6;input<8+sizeof(driving_guard_ms)/sizeof(driving_guard_ms[0]);++input){
        barcode_trial(input);
        assert(telemetry_get_barcode(&detection)==E_OK && detection.event_sequence==input-1U);
        assert(s3_barcode_character(detection.code)=='A' && !detection.navigation);
    }
    puts("PASS: AO task detects normal and faint labels in both directions and rejects brief spikes and a plain stripe");
}
#endif

static void levels(unsigned left,unsigned right,unsigned barcode){
    unsigned values[]={right,left,barcode};
    for(unsigned ch=0;ch<3;++ch){
        cursor[ch]=0;raw_values[ch][0]=4095U; /* must be discarded */
        raw_values[ch][1]=raw_values[ch][2]=values[ch];
    }
    calls=0;advance=wrong_count=bad_range=0;error_channel=-1;
}
int main(void){
    S3_LINE_DATA out;
    UB code=0xa5;
    const unsigned centre_cutoff=S3_IR_CENTRE_THRESHOLD;
    const unsigned right_cutoff=S3_IR_RIGHT_THRESHOLD;
    assert(subsystem3_get_line(&out)==E_OBJ);
    assert(subsystem3_init()==E_OK&&sizeof(published)==8);
    assert(subsystem3_get_line(NULL)==E_PAR&&subsystem3_get_line(&out)==E_TMOUT);
    assert(s3_line_configure()==E_OK);
    assert(configured==((1U<<26)|(1U<<27)|(1U<<28))&&pads==configured);
    assert(!(configured&(1U<<16))); /* no encoder pin or old GP6 access */
    for(unsigned pattern=0;pattern<4;++pattern){
        unsigned centre=pattern&2?centre_cutoff:centre_cutoff-1;
        unsigned right=pattern&1?right_cutoff:right_cutoff-1;
        levels(centre,right,4095);
        clock_ms=100;s3_line_sample();
        assert(calls==6&&cursor[S3_BARCODE_ADC]==0);
        assert(subsystem3_get_line(&out)==E_OK&&out.valid&&out.pattern==pattern&&out.timestamp_ms==100);
        assert(out.centre_adc==centre&&out.right_adc==right);
        levels(centre,right,0);
        s3_line_sample();assert(subsystem3_get_line(&out)==E_OK&&out.pattern==pattern);
    }
    levels(centre_cutoff-1,right_cutoff-1,0);
    raw_values[S3_IR_CENTRE_ADC][1]=centre_cutoff-50;
    raw_values[S3_IR_CENTRE_ADC][2]=centre_cutoff+50;
    s3_line_sample();assert(subsystem3_get_line(&out)==E_OK&&out.pattern==2); /* mean exactly at cutoff */
    /* Observed centre white and weak black at the track. */
    levels(158,159,0);s3_line_sample();
    assert(subsystem3_get_line(&out)==E_OK&&out.pattern==0);
    levels(752,153,0);s3_line_sample();
    assert(subsystem3_get_line(&out)==E_OK&&out.pattern==2);
    levels(centre_cutoff,right_cutoff,0);error_channel=S3_BARCODE_ADC;s3_line_sample();
    assert(calls==6&&subsystem3_get_line(&out)==E_OK&&out.valid&&out.pattern==3);
    for(int ch=0;ch<2;++ch){
        levels(950,1420,0);error_channel=ch;s3_line_sample();
        assert(subsystem3_get_line(&out)==E_TMOUT&&!out.valid);
    }
    levels(950,1420,0);wrong_count=1;s3_line_sample();
    assert(subsystem3_get_line(&out)==E_TMOUT&&!out.valid);
    levels(950,1420,0);bad_range=1;s3_line_sample();
    assert(subsystem3_get_line(&out)==E_TMOUT&&!out.valid);
    levels(950,1420,0);advance=17;clock_ms=100;s3_line_sample();
    assert(subsystem3_get_line(&out)==E_TMOUT&&!out.valid&&out.timestamp_ms==100); /* 102 ms */
    levels(950,1420,0);advance=16;clock_ms=100;s3_line_sample();
    assert(subsystem3_get_line(&out)==E_OK&&out.valid&&out.timestamp_ms==100); /* 96 ms */
    levels(950,1420,0);clock_ms=0xfffffff0U;advance=3;s3_line_sample();
    assert(subsystem3_get_line(&out)==E_OK&&out.valid&&out.timestamp_ms==0xfffffff0U);
    UW old_stamp=out.timestamp_ms;
    levels(0,0,0);clock_ms=200;blocked=1;s3_line_sample();
    memset(&out,0xa5,sizeof(out));assert(subsystem3_get_line(&out)==E_TMOUT);
    blocked=0;assert(subsystem3_get_line(&out)==E_OK&&out.timestamp_ms==old_stamp);
    assert(subsystem3_get_barcode(&code)==E_TMOUT&&code==0xa5);
    assert(subsystem3_get_barcode(NULL)==E_PAR);
    s_line_sem=s_adc=0;sem_created=0;clock_ms=300;levels(0,0,0);assert(subsystem3_init()==E_OK);
    if(setjmp(task_done)==0)ir_task(0,NULL);
    assert(subsystem3_get_line(&out)==E_OK&&out.valid&&out.pattern==0);
    assert(delay_ms+CNF_TIMER_PERIOD==S3_IR_PERIOD_MS);
    s_adc=0;clock_ms=500;levels(950,1420,0);advance=3;
    if(setjmp(task_done)==0)ir_task(0,NULL);
    assert(subsystem3_get_line(&out)==E_OK&&out.valid&&out.pattern==3&&delay_ms==1);
    assert(subsystem3_start()==E_OK);
#if APP_S3_BARCODE_DETECT
    test_barcode_acquisition();
#endif
    puts("PASS: AO task/pins, steering independent of ADC2, ADC validation, age/wrap, contention, eight-byte snapshot, tick-aware cadence and no fabricated barcode");
    return 0;
}
