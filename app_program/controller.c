#include "app.h"
#include "controller.h"

static ID controller_sem;
/* Presence derives from nonzero IDs; no queue-count/validity booleans. */
static uint8_t request_slot[12]; /* ID:32, session:32, boot deadline:32 */
static uint8_t reply_slot[10];
static uint8_t published[5];
/* Nine owner bits: state:4, stop latch:1, TEST:1, pending result:3.
 * Result 7 means not yet processed. Owner fields are never read by S1. */
static uint16_t owner_word;
static void copy_bytes(uint8_t *out,const uint8_t *in,unsigned size) {
    unsigned i;for(i=0;i<size;++i)out[i]=in[i];
}
static void zero_bytes(uint8_t *out,unsigned size) {
    unsigned i;for(i=0;i<size;++i)out[i]=0;
}
#define RESULT_SHIFT 6U
#define UNPROCESSED (7U << RESULT_SHIFT)
static uint32_t read32(const uint8_t *b) {
    return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
static void write32(uint8_t *b,uint32_t v) {
    unsigned i;for(i=0;i<4;++i)b[i]=(uint8_t)(v>>(8U*i));
}
static uint32_t uptime(void) { SYSTIM t={0};(void)tk_get_otm(&t);return t.lo; }
static int lock(void) { return controller_sem>0?tk_wai_sem(controller_sem,1,TMO_POL):E_OBJ; }
int app_controller_init(void) {
    T_CSEM sem={0};
    if(controller_sem>0)return E_OBJ; /* No runtime reset/clear-latch API. */
    sem.sematr=TA_TFIFO;sem.isemcnt=sem.maxsem=1;
    controller_sem=tk_cre_sem(&sem);
    if(controller_sem<0)return controller_sem;
    zero_bytes(request_slot,sizeof(request_slot));zero_bytes(reply_slot,sizeof(reply_slot));
    zero_bytes(published,sizeof(published));
    owner_word=(uint16_t)(UNPROCESSED|(APP_TEST?APP_CTRL_TEST:0U));
    return E_OK;
}
int app_controller_submit_stop(uint32_t id,uint32_t session,uint32_t deadline_ms) {
    int result;
    if(!id||!session)return E_PAR;
    result=lock();if(result!=E_OK)return result;
    if(!(published[4]&APP_CTRL_STATE_MASK)||(uint32_t)(uptime()-read32(published))>APP_CTRL_MAX_AGE_MS)result=E_OBJ;
    else if(read32(request_slot)||read32(reply_slot))result=E_TMOUT;
    else {write32(request_slot,id);write32(request_slot+4,session);write32(request_slot+8,deadline_ms);}
    (void)tk_sig_sem(controller_sem,1);return result;
}
int app_controller_snapshot(uint8_t out[5]) {
    int result;if(!out)return E_PAR;
    result=lock();if(result!=E_OK)return result;
    copy_bytes(out,published,5);(void)tk_sig_sem(controller_sem,1);return E_OK;
}
int app_controller_take_reply(uint8_t out[10]) {
    int result;if(!out)return E_PAR;
    result=lock();if(result!=E_OK)return result;
    if(!read32(reply_slot))result=E_TMOUT;
    else {copy_bytes(out,reply_slot,10);write32(reply_slot,0);}
    (void)tk_sig_sem(controller_sem,1);return result;
}
unsigned app_controller_poll(void) {
    if(lock()==E_OK) {
        if(read32(request_slot)&&(owner_word>>RESULT_SHIFT)==7U) {
            uint32_t remaining=read32(request_slot+8)-uptime();
            unsigned result=APP_STOP_EXPIRED;
            if(remaining>0U&&remaining<=APP_CTRL_STOP_LEASE_MS) {
                owner_word|=APP_CTRL_LATCH;
                result=APP_STOP_LATCHED;
            }
            owner_word=(uint16_t)((owner_word&APP_CTRL_WORD_MASK)|(result<<RESULT_SHIFT));
        }
        (void)tk_sig_sem(controller_sem,1);
    } else return (owner_word&APP_CTRL_LATCH)|APP_CTRL_POLL_BUSY;
    return owner_word&APP_CTRL_LATCH;
}
void app_controller_publish(unsigned state,int motor_result) {
    unsigned result=owner_word>>RESULT_SHIFT;
    if(state>APP_CTRL_STOPPED)return;
    /* No claim of physical standstill: E_OK means S2 accepted a zero-duty
     * request. S2 alone owns PWM; encoder standstill feedback is not wired. */
    if(motor_result!=E_OK&&state!=APP_CTRL_DISABLED&&state!=APP_CTRL_UNAVAILABLE)
        state=APP_CTRL_FAULT;
    if((owner_word&APP_CTRL_LATCH)&&state!=APP_CTRL_DISABLED&&state!=APP_CTRL_FAULT&&state!=APP_CTRL_UNAVAILABLE)
        state=APP_CTRL_STOP_LATCHED;
    owner_word=(uint16_t)((owner_word&~APP_CTRL_STATE_MASK)|state);
    if(lock()!=E_OK)return; /* Owner latch persists even under contention. */
    write32(published,uptime());published[4]=owner_word&APP_CTRL_WORD_MASK;
    if(state==APP_CTRL_UNAVAILABLE)published[4]&=(uint8_t)~APP_CTRL_LATCH;
    if(read32(request_slot)&&result!=7U&&!read32(reply_slot)) {
        uint32_t remaining=read32(request_slot+8)-uptime();
        if(result==APP_STOP_LATCHED&&motor_result==E_TMOUT&&
           remaining>0U&&remaining<=APP_CTRL_STOP_LEASE_MS) {
            (void)tk_sig_sem(controller_sem,1);return;
        }
        if(result==APP_STOP_LATCHED&&(motor_result!=E_OK||state==APP_CTRL_FAULT))result=APP_STOP_FAULT;
        copy_bytes(reply_slot,request_slot,8);
        reply_slot[8]=(uint8_t)((result<<RESULT_SHIFT)|published[4]);
        reply_slot[9]=(uint8_t)(result>>2);
        write32(request_slot,0);
        owner_word=(uint16_t)((owner_word&APP_CTRL_WORD_MASK)|UNPROCESSED);
    }
    (void)tk_sig_sem(controller_sem,1);
}
