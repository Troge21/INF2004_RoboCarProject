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
typedef void (*FP)(INT,void *);
typedef struct {UW lo,hi;} SYSTIM;
typedef struct {unsigned sematr,isemcnt,maxsem;} T_CSEM;
#include "subsystem3.h"
static UW clock_ms,configured,pads;
static UW raw_values[3][3];
static unsigned cursor[3],calls,advance,wrong_count,bad_range,locked,blocked,delay_ms;
static int error_channel=-1;
static jmp_buf task_done;
static void out_w(UINT reg,UW value){assert(reg>=126U&&reg<=128U&&value==31U);configured|=1U<<(reg-100U);}
static void clr_w(UINT reg,UW value){assert(reg>=226U&&reg<=228U&&value==7U);pads|=1U<<(reg-200U);}
static ID tk_opn_dev(UB *name,UINT mode){assert(!strcmp((const char *)name,"adca")&&mode==TD_READ);return 7;}
static ER tk_srea_dev(ID id,INT channel,UW *value,INT size,SZ *count){
    assert(id==7&&channel>=0&&channel<=2&&size==1);
    ++calls;clock_ms+=advance;
    if(channel==error_channel)return E_IO;
    *count=wrong_count?0:1;
    *value=bad_range?4096U:raw_values[channel][cursor[channel]++%3U];
    return E_OK;
}
static ER tk_get_tim(SYSTIM *t){t->lo=clock_ms;t->hi=0;return E_OK;}
static ER tk_get_otm(SYSTIM *t){return tk_get_tim(t);}
static ID tk_cre_sem(const T_CSEM *s){assert(s->isemcnt==1&&s->maxsem==1);return 1;}
static ER tk_wai_sem(ID id,INT count,INT timeout){assert(id==1&&count==1&&timeout==TMO_POL);if(locked||blocked)return E_TMOUT;locked=1;return E_OK;}
static ER tk_sig_sem(ID id,INT count){assert(id==1&&count==1&&locked);locked=0;return E_OK;}
static ER tk_dly_tsk(UINT ms){assert(ms>0&&ms<=1000);delay_ms=ms;longjmp(task_done,1);return E_OK;}
static ID app_start_task(FP entry,PRI priority,UW core,const char *name){(void)entry;assert(priority==17&&core==1&&!strcmp(name,"ir_task"));return 3;}

/* SUBSYSTEM_SOURCE */

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
    s_line_sem=s_adc=0;clock_ms=300;levels(0,0,0);assert(subsystem3_init()==E_OK);
    if(setjmp(task_done)==0)ir_task(0,NULL);
    assert(subsystem3_get_line(&out)==E_OK&&out.valid&&out.pattern==0);
    assert(delay_ms+CNF_TIMER_PERIOD==S3_IR_PERIOD_MS);
    s_adc=0;clock_ms=500;levels(950,1420,0);advance=3;
    if(setjmp(task_done)==0)ir_task(0,NULL);
    assert(subsystem3_get_line(&out)==E_OK&&out.valid&&out.pattern==3&&delay_ms==1);
    assert(subsystem3_start()==E_OK);
    puts("PASS: AO task/pins, steering independent of ADC2, ADC validation, age/wrap, contention, eight-byte snapshot, tick-aware cadence and no fabricated barcode");
    return 0;
}
