#include "app.h"
#include "controller.h"

static ID controller_sem;
/* Nonzero ID means occupied. Final byte: action:1/result:3/spare:4. */
static uint8_t request_slot[13];
/* ID/session + control:6/result:3/resume-kind:1/spare:6. */
static uint8_t reply_slot[10],published[5];
static uint8_t owner_word; /* state:4/latch:1/TEST:1/Resume in-flight:1/failed:1 */
#define OWNER_RESUMING 64U /* release awaiting the controller's motor handoff */
#define OWNER_RESUME_FAILED 128U
/* Protected authorization, not motor state: ticket:32/expiry:32. */
static uint8_t resume_ticket[8];
static uint32_t ticket_counter;
#define RESULT_SHIFT 6U
#define REQUEST_RESUME 1U
#define REQUEST_UNPROCESSED (7U << 1)
static void copy_bytes(uint8_t *out,const uint8_t *in,unsigned size) {
    unsigned i;for(i=0;i<size;++i)out[i]=in[i];
}
static void zero_bytes(uint8_t *out,unsigned size) {
    unsigned i;for(i=0;i<size;++i)out[i]=0;
}
static uint32_t read32(const uint8_t *b) {
    return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);
}
static void write32(uint8_t *b,uint32_t v) {
    unsigned i;for(i=0;i<4;++i)b[i]=(uint8_t)(v>>(8U*i));
}
static uint32_t uptime(void) { SYSTIM t={0};(void)tk_get_otm(&t);return t.lo; }
static int lock(void) { return controller_sem>0?tk_wai_sem(controller_sem,1,TMO_POL):E_OBJ; }
static int lease_valid(uint32_t deadline) {
    uint32_t remaining=deadline-uptime();
    return remaining>0U&&remaining<=APP_CTRL_STOP_LEASE_MS;
}
static unsigned request_result(void) { return (request_slot[12]>>1)&7U; }
static int publication_fresh(void) {
    return (published[4]&APP_CTRL_STATE_MASK)&&
           (uint32_t)(uptime()-read32(published))<=APP_CTRL_MAX_AGE_MS;
}
int app_controller_init(void) {
    T_CSEM sem={0};
    if(controller_sem>0)return E_OBJ;
    sem.sematr=TA_TFIFO;sem.isemcnt=sem.maxsem=1;
    controller_sem=tk_cre_sem(&sem);
    if(controller_sem<0)return controller_sem;
    zero_bytes(request_slot,sizeof(request_slot));zero_bytes(reply_slot,sizeof(reply_slot));
    zero_bytes(published,sizeof(published));zero_bytes(resume_ticket,sizeof(resume_ticket));
    ticket_counter=0;owner_word=APP_TEST?APP_CTRL_TEST:0U;
    return E_OK;
}
int app_controller_submit_stop(uint32_t id,uint32_t session,uint32_t deadline_ms) {
    int result;
    if(!id||!session)return E_PAR;
    result=lock();if(result!=E_OK)return result;
    /* Every admitted STOP cancels an unused authorization or queued Resume.
     * An already queued STOP/result remains bounded and is never overwritten. */
    write32(resume_ticket,0);
    if(!publication_fresh())result=E_OBJ;
    else if((read32(request_slot)&&!(request_slot[12]&REQUEST_RESUME))||read32(reply_slot))result=E_TMOUT;
    else {
        write32(request_slot,id);write32(request_slot+4,session);write32(request_slot+8,deadline_ms);
        request_slot[12]=REQUEST_UNPROCESSED;
    }
    (void)tk_sig_sem(controller_sem,1);return result;
}
int app_controller_arm_resume(uint32_t deadline_ms,uint32_t *ticket) {
    unsigned result=APP_RESUME_NOT_READY;
    if(!ticket)return E_PAR;
    if(lock()!=E_OK)return APP_RESUME_BUSY;
    if(!lease_valid(deadline_ms))result=APP_RESUME_EXPIRED;
    else if((published[4]&APP_CTRL_STATE_MASK)==APP_CTRL_FAULT)result=APP_RESUME_FAULT;
    else if(read32(request_slot)||read32(reply_slot))result=APP_RESUME_BUSY;
    else if(!APP_TEST&&publication_fresh()&&
            (published[4]&APP_CTRL_LATCH)&&
            (published[4]&APP_CTRL_STATE_MASK)==APP_CTRL_STOP_LATCHED) {
        if(++ticket_counter==0U)++ticket_counter;
        write32(resume_ticket,ticket_counter);
        write32(resume_ticket+4,uptime()+APP_CTRL_STOP_LEASE_MS);
        *ticket=ticket_counter;result=APP_RESUME_RELEASED;
    }
    (void)tk_sig_sem(controller_sem,1);return (int)result;
}
int app_controller_submit_resume(uint32_t ticket,uint32_t session,uint32_t deadline_ms) {
    unsigned result=APP_RESUME_AUTH_CHANGED;
    if(!ticket||!session)return E_PAR;
    if(lock()!=E_OK)return APP_RESUME_BUSY;
    if(!lease_valid(deadline_ms))result=APP_RESUME_EXPIRED;
    else if(read32(request_slot)||read32(reply_slot))result=APP_RESUME_BUSY;
    else if(ticket==read32(resume_ticket)&&lease_valid(read32(resume_ticket+4))) {
        write32(request_slot,ticket);write32(request_slot+4,session);write32(request_slot+8,deadline_ms);
        request_slot[12]=REQUEST_RESUME|REQUEST_UNPROCESSED;
        write32(resume_ticket,0);result=APP_RESUME_RELEASED;
    }
    (void)tk_sig_sem(controller_sem,1);return (int)result;
}
void app_controller_cancel_resume(void) {
    if(lock()!=E_OK)return;
    write32(resume_ticket,0);
    if(read32(request_slot)&&(request_slot[12]&REQUEST_RESUME)&&request_result()==7U)
        write32(request_slot,0);
    (void)tk_sig_sem(controller_sem,1);
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
static unsigned poll_command(int resume_ready) {
    if(lock()==E_OK) {
        if(read32(request_slot)&&request_result()==7U) {
            unsigned resume=request_slot[12]&REQUEST_RESUME,result=APP_STOP_EXPIRED;
            if(lease_valid(read32(request_slot+8))) {
                if(!resume) {owner_word=(uint8_t)((owner_word|APP_CTRL_LATCH)&~(OWNER_RESUMING|OWNER_RESUME_FAILED));write32(resume_ticket,0);result=APP_STOP_LATCHED;}
                else if(!APP_TEST&&resume_ready<0) { /* Wait for this iteration's actual source checks. */
                    (void)tk_sig_sem(controller_sem,1);return owner_word&APP_CTRL_LATCH;
                } else if((owner_word&APP_CTRL_STATE_MASK)==APP_CTRL_FAULT)result=APP_RESUME_FAULT;
                else if(!(owner_word&APP_CTRL_LATCH))result=APP_RESUME_AUTH_CHANGED;
                else if(APP_TEST||!resume_ready)result=APP_RESUME_NOT_READY;
                else {owner_word=(uint8_t)((owner_word&~APP_CTRL_LATCH)|OWNER_RESUMING);result=APP_RESUME_RELEASED;}
            }
            request_slot[12]=(uint8_t)(resume|(result<<1));
        }
        (void)tk_sig_sem(controller_sem,1);
    } else return (owner_word&APP_CTRL_LATCH)|APP_CTRL_POLL_BUSY;
    return owner_word&APP_CTRL_LATCH;
}
unsigned app_controller_poll(void) { return poll_command(-1); }
unsigned app_controller_poll_ready(int ready) { return poll_command(ready!=0); }
void app_controller_publish(unsigned state,int motor_result) {
    unsigned result,resume;
    if(state>APP_CTRL_STOPPED)return;
    if(motor_result!=E_OK&&state!=APP_CTRL_DISABLED&&state!=APP_CTRL_UNAVAILABLE)
        state=APP_CTRL_FAULT;
    /* Restore before attempting the publication lock: contention must not
     * leave Resume's inhibit cleared after a failed motor handoff. */
    if((owner_word&OWNER_RESUMING)&&
       (motor_result!=E_OK||state==APP_CTRL_FAULT||state==APP_CTRL_DISABLED||state==APP_CTRL_UNAVAILABLE)) {
        owner_word=(uint8_t)((owner_word|APP_CTRL_LATCH|OWNER_RESUME_FAILED)&~OWNER_RESUMING);
        state=APP_CTRL_FAULT;
    }
    if(owner_word&OWNER_RESUME_FAILED)state=APP_CTRL_FAULT;
    if((owner_word&APP_CTRL_LATCH)&&state!=APP_CTRL_DISABLED&&state!=APP_CTRL_FAULT&&state!=APP_CTRL_UNAVAILABLE)
        state=APP_CTRL_STOP_LATCHED;
    owner_word=(uint8_t)((owner_word&~APP_CTRL_STATE_MASK)|state);
    if(lock()!=E_OK)return;
    if(state==APP_CTRL_FAULT)write32(resume_ticket,0);
    result=request_result();resume=request_slot[12]&REQUEST_RESUME;
    if(read32(request_slot)&&result!=7U&&resume&&result==0U&&
       (motor_result!=E_OK||state==APP_CTRL_FAULT||state==APP_CTRL_UNAVAILABLE||state==APP_CTRL_DISABLED)) {
        /* A failed handoff cannot leave the inhibit cleared. Do not clear S2 faults. */
        owner_word|=APP_CTRL_LATCH;
        owner_word=(uint8_t)((owner_word&~APP_CTRL_STATE_MASK)|APP_CTRL_FAULT);
        state=APP_CTRL_FAULT;result=APP_RESUME_FAULT;
    }
    write32(published,uptime());published[4]=owner_word&APP_CTRL_WORD_MASK;
    if(state==APP_CTRL_UNAVAILABLE)published[4]&=(uint8_t)~APP_CTRL_LATCH;
    if(read32(request_slot)&&result!=7U&&!read32(reply_slot)) {
        if(!resume&&result==APP_STOP_LATCHED&&motor_result==E_TMOUT&&lease_valid(read32(request_slot+8))) {
            (void)tk_sig_sem(controller_sem,1);return;
        }
        if(!resume&&result==APP_STOP_LATCHED&&(motor_result!=E_OK||state==APP_CTRL_FAULT))result=APP_STOP_FAULT;
        if(resume&&result==0U&&(owner_word&APP_CTRL_LATCH))result=APP_RESUME_AUTH_CHANGED;
        copy_bytes(reply_slot,request_slot,8);
        result=(result<<RESULT_SHIFT)|published[4]|(resume?APP_REPLY_RESUME:0U);
        reply_slot[8]=(uint8_t)result;reply_slot[9]=(uint8_t)(result>>8);
        write32(request_slot,0);request_slot[12]=0;
        owner_word&=(uint8_t)~(OWNER_RESUMING|OWNER_RESUME_FAILED);
    }
    (void)tk_sig_sem(controller_sem,1);
}
