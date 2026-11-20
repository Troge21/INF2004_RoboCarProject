/* Integrated AO owner. GP26 is now right analog IR and GP16 an encoder:
 * the old digital barcode capture cannot run on this wiring. */
#include "subsystem3.h"
#include "line_detection.h"
#include <config.h>
EXPORT ER subsystem3_get_barcode(UB *code)
{
    if(code==NULL) return E_PAR;
    return E_TMOUT; /* AO barcode acquisition awaits its owner. */
}
static void ir_task(INT stacd,void *exinf)
{
    /* A positive relative delay includes one extra kernel tick. */
    const UINT delay_budget_ms=S3_IR_PERIOD_MS>CNF_TIMER_PERIOD ?
        S3_IR_PERIOD_MS-CNF_TIMER_PERIOD:0U;
    ER configured;
    (void)stacd;(void)exinf;
    configured=s3_line_configure();
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
