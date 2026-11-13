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
static unsigned cursor[3],calls,advance,wrong_count,bad_range,locked,blocked;
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
static ID tk_cre_sem(const T_CSEM *s){assert(s->isemcnt==1&&s->maxsem==1);return 1;}
static ER tk_wai_sem(ID id,INT count,INT timeout){assert(id==1&&count==1&&timeout==TMO_POL);if(locked||blocked)return E_TMOUT;locked=1;return E_OK;}
static ER tk_sig_sem(ID id,INT count){assert(id==1&&count==1&&locked);locked=0;return E_OK;}
static ER tk_dly_tsk(INT ms){assert(ms==20||ms==1000);longjmp(task_done,1);return E_OK;}
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
    S3_LINE_DATA sample,out;
    UB code=0xa5;
    assert(subsystem3_get_line(&out)==E_OBJ);
    assert(subsystem3_init()==E_OK&&sizeof(published)==9);
    assert(subsystem3_get_line(NULL)==E_PAR&&subsystem3_get_line(&out)==E_TMOUT);
    assert(s3_line_configure()==E_OK);
    assert(configured==((1U<<26)|(1U<<27)|(1U<<28))&&pads==configured);
    assert(!(configured&(1U<<16))); /* no encoder pin or old GP6 access */
    for(unsigned pattern=0;pattern<4;++pattern){
        levels(pattern&2?950:949,pattern&1?1420:1419,4095);
        clock_ms=100;s3_line_sample(&sample);
        assert(calls==9&&sample.valid&&sample.pattern==pattern);
        assert(subsystem3_get_line(&out)==E_OK&&out.pattern==pattern&&out.timestamp_ms==100);
        levels(pattern&2?950:949,pattern&1?1420:1419,0);
        s3_line_sample(&sample);assert(sample.pattern==pattern); /* barcode never steers */
    }
    levels(949,1419,0);raw_values[1][1]=900;raw_values[1][2]=1000;
    s3_line_sample(&sample);assert(sample.valid&&sample.pattern==2); /* mean exactly 950 */
    for(int ch=0;ch<3;++ch){
        levels(950,1420,0);error_channel=ch;s3_line_sample(&sample);
        assert(!sample.valid&&subsystem3_get_line(&out)==E_TMOUT);
    }
    levels(950,1420,0);wrong_count=1;s3_line_sample(&sample);assert(!sample.valid);
    levels(950,1420,0);bad_range=1;s3_line_sample(&sample);assert(!sample.valid);
    levels(950,1420,0);advance=12;clock_ms=100;s3_line_sample(&sample);
    assert(!sample.valid&&sample.timestamp_ms==100); /* 108 ms old */
    levels(950,1420,0);advance=11;clock_ms=100;s3_line_sample(&sample);
    assert(sample.valid&&sample.timestamp_ms==100); /* 99 ms; timestamp is oldest read */
    levels(950,1420,0);clock_ms=0xfffffff0U;advance=3;s3_line_sample(&sample);
    assert(sample.valid&&sample.timestamp_ms==0xfffffff0U); /* timer wrap */
    assert(subsystem3_get_line(&out)==E_OK);
    UW old_stamp=out.timestamp_ms;
    levels(0,0,0);clock_ms=200;blocked=1;s3_line_sample(&sample);
    memset(&out,0xa5,sizeof(out));assert(subsystem3_get_line(&out)==E_TMOUT);
    blocked=0;assert(subsystem3_get_line(&out)==E_OK&&out.timestamp_ms==old_stamp);
    assert(subsystem3_get_barcode(&code)==E_TMOUT&&code==0xa5);
    assert(subsystem3_get_barcode(NULL)==E_PAR);
    s_line_sem=s_adc=0;clock_ms=300;levels(0,0,0);assert(subsystem3_init()==E_OK);
    if(setjmp(task_done)==0)ir_task(0,NULL);
    assert(subsystem3_get_line(&out)==E_OK&&out.valid&&out.pattern==0);
    assert(subsystem3_start()==E_OK);
    puts("PASS: actual AO task/pins, all patterns, ADC discard/averaging, thresholds, errors, bounds, age/wrap, contention, packed snapshot and no fabricated barcode");
    return 0;
}
