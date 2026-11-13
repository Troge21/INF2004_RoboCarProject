/* S3 is the integrated ADC owner. AO: GP28/27/26 -> ADC2/1/0.
 * Never configures encoder GP16 or starts the old digital barcode timer. */
#include "line_detection.h"
#include <sys/sysdef.h>
#include <tk/syslib.h>

static ID s_line_sem, s_adc;
/* timestamp:32 + ADCs:12 each + valid:1 + reserved:3 = nine shared bytes.
 * Pattern derives from ADC values; no duplicate state or native padding. */
static UB published[9];
static UW read32(const UB *b)
{
    return (UW)b[0] | ((UW)b[1]<<8) | ((UW)b[2]<<16) | ((UW)b[3]<<24);
}
static void decode(const UB *b, S3_LINE_DATA *sample)
{
    uint64_t word=0;
    UINT i;
    for(i=0;i<5U;++i) word|=(uint64_t)b[4U+i]<<(8U*i);
    sample->timestamp_ms=read32(b);
    sample->valid=(BOOL)((word>>36)&1U);
    sample->pattern=sample->valid ?
        ((((word>>12)&4095U)>=S3_IR_CENTRE_THRESHOLD ? 2U:0U) |
         (((word>>24)&4095U)>=S3_IR_RIGHT_THRESHOLD ? 1U:0U)):0U;
}
EXPORT ER subsystem3_get_line(S3_LINE_DATA *sample)
{
    ER er;
    if(sample==NULL) return E_PAR;
    if(s_line_sem<=0) return E_OBJ;
    er=tk_wai_sem(s_line_sem,1,TMO_POL);
    if(er!=E_OK) return er;
    decode(published,sample);
    (void)tk_sig_sem(s_line_sem,1);
    return sample->valid ? E_OK:E_TMOUT;
}
ER s3_line_init(void)
{
    T_CSEM sem={0};
    UINT i;
    if(s_line_sem>0) return E_OBJ;
    sem.sematr=TA_TFIFO|TA_FIRST;sem.isemcnt=sem.maxsem=1;
    for(i=0;i<sizeof(published);++i) published[i]=0;
    s_line_sem=tk_cre_sem(&sem);
    return s_line_sem<E_OK ? s_line_sem:E_OK;
}
ER s3_line_configure(void)
{
    const UINT pins[]={S3_BARCODE_PIN,S3_IR_CENTRE_PIN,S3_IR_RIGHT_PIN};
    UINT i;
    if(s_adc>0) return E_OBJ;
    for(i=0;i<3U;++i){
        out_w(GPIO_CTRL(pins[i]),GPIO_CTRL_FUNCSEL_NULL);
        clr_w(GPIO(pins[i]),GPIO_IE|GPIO_PUE|GPIO_PDE);
    }
    s_adc=tk_opn_dev((UB *)"adca",TD_READ);
    return s_adc<E_OK ? s_adc:E_OK;
}
static ER read_raw(INT channel,UW *out)
{
    SZ count=0;
    ER er=tk_srea_dev(s_adc,channel,out,1,&count);
    if(er!=E_OK) return er;
    return count==1 && *out<=4095U ? E_OK:E_IO;
}
void s3_line_sample(S3_LINE_DATA *sample)
{
    const INT channels[]={S3_BARCODE_ADC,S3_IR_CENTRE_ADC,S3_IR_RIGHT_ADC};
    UB bytes[9]={0};
    uint64_t word=0;
    SYSTIM started={0},now={0};
    BOOL valid=s_adc>0;
    UINT i,n;
    (void)tk_get_tim(&started);
    for(i=0;i<3U && valid;++i){
        UW raw=0,sum=0;
        /* Discard after channel switch, then average two samples, exactly
         * as in the teammate's bench. UW is the hardware driver's API type. */
        ER er=read_raw(channels[i],&raw);
        for(n=0;n<S3_IR_SAMPLES && er==E_OK;++n){
            er=read_raw(channels[i],&raw);
            if(er==E_OK) sum+=raw;
        }
        if(er!=E_OK) valid=FALSE;
        else word|=(uint64_t)(sum/S3_IR_SAMPLES)<<(12U*i);
    }
    (void)tk_get_tim(&now);
    if((UW)(now.lo-started.lo)>APP_IR_MAX_AGE_MS) valid=FALSE;
    if(valid) word|=(uint64_t)1U<<36;else word=0;
    for(i=0;i<4U;++i) bytes[i]=(UB)(started.lo>>(8U*i));
    for(i=0;i<5U;++i) bytes[4U+i]=(UB)(word>>(8U*i));
    decode(bytes,sample);
    if(tk_wai_sem(s_line_sem,1,TMO_POL)==E_OK){
        for(i=0;i<sizeof(published);++i) published[i]=bytes[i];
        (void)tk_sig_sem(s_line_sem,1);
    }
}
