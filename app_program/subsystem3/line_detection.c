/* S3 is the integrated ADC owner. AO: GP28/27/26 -> ADC2/1/0.
 * Never configures encoder GP16 or starts the old digital barcode timer. */
#include "line_detection.h"
#if APP_S3_BARCODE_DETECT
#include "../telemetry_sources.h"
#endif
#include <sys/sysdef.h>
#include <tk/syslib.h>

static ID s_line_sem, s_adc;
/* timestamp:32 + two ADC values:12 each + valid:1 + reserved:7 = eight bytes.
 * Pattern derives from ADC values; no duplicate state or native padding. */
static UB published[8];
#if APP_S3_BARCODE_DETECT
/* Separate lock so barcode readers never contend with the steering snapshot. */
static ID s_barcode_sem;
static UB barcode_event[7]; /* publication timestamp:32, event counter:16, code:8 */
static S3_BARCODE_SIGNAL published_barcode_signal;
#endif
static UW read32(const UB *b)
{
    return (UW)b[0] | ((UW)b[1]<<8) | ((UW)b[2]<<16) | ((UW)b[3]<<24);
}
static void decode(const UB *b, S3_LINE_DATA *sample)
{
    sample->timestamp_ms=read32(b);
    sample->valid=(BOOL)(b[7]&1U);
    sample->centre_adc=(uint16_t)(b[4]|((UINT)(b[5]&15U)<<8));
    sample->right_adc=(uint16_t)((b[5]>>4)|((UINT)b[6]<<4));
    sample->pattern=sample->valid ?
        ((sample->centre_adc>=S3_IR_CENTRE_THRESHOLD ? 2U:0U) |
         (sample->right_adc>=S3_IR_RIGHT_THRESHOLD ? 1U:0U)):0U;
}
EXPORT ER subsystem3_get_line(S3_LINE_DATA *sample)
{
    UB bytes[sizeof(published)];
    ER er;
    if(sample==NULL) return E_PAR;
    if(s_line_sem<=0) return E_OBJ;
    er=tk_wai_sem(s_line_sem,1,TMO_POL);
    if(er!=E_OK) return er;
    for(UINT i=0;i<sizeof(published);++i) bytes[i]=published[i];
    (void)tk_sig_sem(s_line_sem,1);
    decode(bytes,sample);
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
#if APP_S3_BARCODE_DETECT
    if(s_line_sem>E_OK)s_barcode_sem=tk_cre_sem(&sem);
#endif
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
#if APP_S3_BARCODE_DETECT
ER s3_barcode_read_ao(UW *value, BOOL channel_changed)
{
    if(channel_changed){
        ER er=read_raw(S3_BARCODE_ADC,value);
        if(er!=E_OK)return er;
    }
    return read_raw(S3_BARCODE_ADC,value);
}

ER s3_publish_barcode_detection(UW timestamp_ms, UB code)
{
    if(code && !s3_barcode_character(code))return E_PAR;
    if(s_barcode_sem<=0)return E_OBJ;
    ER er=tk_wai_sem(s_barcode_sem,1,TMO_POL);
    if(er!=E_OK)return er;
    uint16_t sequence=(uint16_t)(barcode_event[4]|((UINT)barcode_event[5]<<8));
    if(++sequence==0U)++sequence;
    for(UINT i=0;i<4U;++i)barcode_event[i]=(UB)(timestamp_ms>>(8U*i));
    barcode_event[4]=(UB)sequence;
    barcode_event[5]=(UB)(sequence>>8);
    barcode_event[6]=code;
    (void)tk_sig_sem(s_barcode_sem,1);
    return E_OK;
}

int telemetry_get_barcode(TELEMETRY_BARCODE *out)
{
    if(!out)return E_PAR;
    if(s_barcode_sem<=0)return E_OBJ;
    ER er=tk_wai_sem(s_barcode_sem,1,TMO_POL);
    if(er!=E_OK)return er;
    const UW timestamp=read32(barcode_event);
    const uint16_t sequence=(uint16_t)(barcode_event[4]|((UINT)barcode_event[5]<<8));
    const UB code=barcode_event[6];
    (void)tk_sig_sem(s_barcode_sem,1);
    if(!sequence)return E_TMOUT;
    *out=(TELEMETRY_BARCODE){.timestamp_ms=timestamp,.event_sequence=sequence,.code=code};
    return E_OK;
}

void s3_publish_barcode_signal(const S3_BARCODE_SIGNAL *value)
{
    if(s_barcode_sem<=0 || tk_wai_sem(s_barcode_sem,1,TMO_POL)!=E_OK)return;
    published_barcode_signal=*value;
    (void)tk_sig_sem(s_barcode_sem,1);
}

ER s3_get_barcode_signal(S3_BARCODE_SIGNAL *out)
{
    if(!out)return E_PAR;
    if(s_barcode_sem<=0)return E_OBJ;
    ER er=tk_wai_sem(s_barcode_sem,1,TMO_POL);
    if(er!=E_OK)return er;
    *out=published_barcode_signal;
    (void)tk_sig_sem(s_barcode_sem,1);
    return E_OK;
}
#endif
void s3_line_sample(void)
{
    const INT channels[]={S3_IR_CENTRE_ADC,S3_IR_RIGHT_ADC};
    UB bytes[sizeof(published)]={0};
    UW word=0;
    SYSTIM started={0},now={0};
    BOOL valid=s_adc>0;
    UINT i,n;
    (void)tk_get_tim(&started);
    for(i=0;i<sizeof(channels)/sizeof(channels[0]) && valid;++i){
        UW raw=0,sum=0;
        /* Discard after channel switch, then average two samples, exactly
         * as in the teammate's bench. UW is the hardware driver's API type. */
        ER er=read_raw(channels[i],&raw);
        for(n=0;n<S3_IR_SAMPLES && er==E_OK;++n){
            er=read_raw(channels[i],&raw);
            if(er==E_OK) sum+=raw;
        }
        if(er!=E_OK) valid=FALSE;
        else word|=(sum/S3_IR_SAMPLES)<<(12U*i);
    }
    (void)tk_get_tim(&now);
    if((UW)(now.lo-started.lo)>APP_IR_MAX_AGE_MS) valid=FALSE;
    for(i=0;i<4U;++i) bytes[i]=(UB)(started.lo>>(8U*i));
    if(valid){
        for(i=0;i<3U;++i) bytes[4U+i]=(UB)(word>>(8U*i));
        bytes[7]=1U;
    }
    if(tk_wai_sem(s_line_sem,1,TMO_POL)==E_OK){
        for(i=0;i<sizeof(published);++i) published[i]=bytes[i];
        (void)tk_sig_sem(s_line_sem,1);
    }
}
