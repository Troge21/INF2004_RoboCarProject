/* Actual controller handoff; deterministic semaphore/clock, no GPIO/SDK. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define APP_H
#ifndef APP_TEST
#define APP_TEST 0
#endif
#define E_OK 0
#define E_PAR (-17)
#define E_OBJ (-41)
#define E_TMOUT (-50)
#define TMO_POL 0
#define TA_TFIFO 0
typedef int ID;
typedef struct {uint32_t hi,lo;} SYSTIM;
typedef struct {unsigned sematr,isemcnt,maxsem;} T_CSEM;
static uint32_t clock_ms;
static int held,blocked,failures,checks;
static int tk_cre_sem(T_CSEM *s){return s->isemcnt==1&&s->maxsem==1?1:E_PAR;}
static int tk_wai_sem(ID id,int n,int timeout){(void)timeout;if(id!=1||n!=1)return E_PAR;if(held||blocked)return E_TMOUT;held=1;return E_OK;}
static int tk_sig_sem(ID id,int n){(void)id;(void)n;held=0;return E_OK;}
static int tk_get_otm(SYSTIM *t){t->lo=clock_ms;t->hi=0;return E_OK;}
#include "../../../app_program/controller.c"
static void check(int ok,const char *name){checks++;if(!ok)failures++;printf("[controller] %s %s\n",ok?"PASS":"FAIL",name);}
static void reset(void){controller_sem=0;held=blocked=0;clock_ms=1000;(void)app_controller_init();app_controller_publish(APP_CTRL_FOLLOWING,E_OK);}
static void latch(void){
    uint8_t b[10];
    (void)app_controller_submit_stop(91,99,clock_ms+1000U);
    (void)app_controller_poll();app_controller_publish(APP_CTRL_STOP_LATCHED,E_OK);
    (void)app_controller_take_reply(b);
}
int main(void){
    uint8_t reply[10],state[5];
    reset();
    check(app_controller_submit_stop(0,1,1100)==E_PAR&&app_controller_submit_stop(1,0,1100)==E_PAR,"zero IDs/session are rejected");
    check(app_controller_submit_stop(12,99,2000)==E_OK&&app_controller_submit_stop(13,99,2000)==E_TMOUT,"one occupied request slot rejects another request");
    check(app_controller_poll()==APP_CTRL_LATCH&&app_controller_take_reply(reply)==E_TMOUT,"latch is held by controller, not acknowledged before motor submission");
    app_controller_publish(APP_CTRL_STOP_LATCHED,E_TMOUT);
    check(app_controller_take_reply(reply)==E_TMOUT,"busy motor handoff does not produce a success reply");
    clock_ms+=20;app_controller_publish(APP_CTRL_STOP_LATCHED,E_OK);
    check(app_controller_take_reply(reply)==E_OK&&read32(reply)==12&&read32(reply+4)==99&&
        ((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_LATCHED&&(reply[8]&APP_CTRL_LATCH),"matching applied result follows accepted zero-duty submission");
    app_controller_publish(APP_CTRL_FOLLOWING,E_OK);(void)app_controller_snapshot(state);
    check((state[4]&APP_CTRL_STATE_MASK)==APP_CTRL_STOP_LATCHED&&(state[4]&APP_CTRL_LATCH),"subsequent steering state cannot override stop latch");
    check(app_controller_init()==E_OBJ&&(app_controller_poll()&APP_CTRL_LATCH),"reinitialization cannot clear an active latch");
    reset();(void)app_controller_submit_stop(1,99,1000);(void)app_controller_poll();app_controller_publish(APP_CTRL_FOLLOWING,E_OK);
    (void)app_controller_take_reply(reply);
    check(((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_EXPIRED&&!(reply[8]&APP_CTRL_LATCH),"deadline expiry is checked again by the controller");
    reset();(void)app_controller_submit_stop(1,99,4000);(void)app_controller_poll();app_controller_publish(APP_CTRL_FOLLOWING,E_OK);
    (void)app_controller_take_reply(reply);check(((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_EXPIRED,"unbounded future deadlines are rejected");
    reset();(void)app_controller_submit_stop(1,99,2000);(void)app_controller_poll();app_controller_publish(APP_CTRL_STOP_LATCHED,E_OBJ);
    (void)app_controller_take_reply(reply);
    check(((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_FAULT&&(reply[8]&APP_CTRL_STATE_MASK)==APP_CTRL_FAULT&&(reply[8]&APP_CTRL_LATCH),"motor errors retain inhibit and report fault, not successful stop");
    reset();(void)app_controller_submit_stop(1,99,2000);(void)app_controller_poll();
    app_controller_publish(APP_CTRL_FAULT,E_OK);(void)app_controller_take_reply(reply);
    check(((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_FAULT&&(reply[8]&APP_CTRL_LATCH),"S2 feedback fault cannot report success even when zero request is accepted");
    reset();(void)app_controller_submit_stop(1,99,1100);(void)app_controller_poll();clock_ms=1100;
    app_controller_publish(APP_CTRL_STOP_LATCHED,E_TMOUT);(void)app_controller_take_reply(reply);
    check(((reply[8]|((unsigned)reply[9]<<8))>>6)==APP_STOP_FAULT&&(reply[8]&APP_CTRL_LATCH),"permanent busy handoff expires without clearing inhibit");
    reset();blocked=1;memset(state,0xa5,sizeof(state));
    check(app_controller_submit_stop(1,99,2000)==E_TMOUT&&app_controller_snapshot(state)==E_TMOUT&&state[0]==0xa5&&
        (app_controller_poll()&APP_CTRL_POLL_BUSY),"contended shared access never waits or invents a state");
    reset();clock_ms=0xfffffff0U;app_controller_publish(APP_CTRL_DISABLED,E_OK);
    (void)app_controller_submit_stop(1,99,16);(void)app_controller_poll();clock_ms=0;
    app_controller_publish(APP_CTRL_DISABLED,E_OK);(void)app_controller_take_reply(reply);
    check(((reply[8]|((unsigned)reply[9]<<8))>>6)==0&&(reply[8]&APP_CTRL_STATE_MASK)==APP_CTRL_DISABLED&&(reply[8]&APP_CTRL_LATCH),"lease survives wrap and disabled build reports only logical inhibit");
    check(((reply[8]&APP_CTRL_TEST)!=0)==(APP_TEST!=0),"build mode is real compile-time mode");
    reset();clock_ms+=APP_CTRL_MAX_AGE_MS+1;
    check(app_controller_submit_stop(1,99,clock_ms+1000)==E_OBJ,"stale owner publication cannot admit commands");
    reset();app_controller_publish(APP_CTRL_UNAVAILABLE,E_OK);
    check(app_controller_submit_stop(1,99,2000)==E_OBJ,"unavailable/calibration controller does not admit stop");

    reset();blocked=1;
    app_controller_report_pickup();
    app_controller_report_pickup();
    check(app_controller_poll()==(APP_CTRL_LATCH|APP_CTRL_POLL_BUSY),
        "pickup is retained even when the command mailbox is contended");
    blocked=0;app_controller_cancel_resume();app_controller_publish(APP_CTRL_FOLLOWING,E_OK);
    check(app_controller_snapshot(state)==E_OK&&(state[4]&APP_CTRL_LATCH)&&
          (state[4]&APP_CTRL_STATE_MASK)==APP_CTRL_STOP_LATCHED&&
          app_controller_take_reply(reply)==E_TMOUT,
        "pickup publishes a local latch without a fabricated command reply");
    check(app_controller_init()==E_OBJ&&(app_controller_poll_ready(1)&APP_CTRL_LATCH),
        "pickup stays latched through reinitialization attempts and ready input");
    reset();
    check(app_controller_poll()==0,"a fresh boot starts with the pickup latch clear");

    {
        uint32_t ticket=777;
        reset();check(app_controller_arm_resume(clock_ms+1000,&ticket)==APP_RESUME_NOT_READY&&ticket==777,
            "unlatched control cannot arm Resume or change output on refusal");
        latch();
        check(app_controller_arm_resume(clock_ms+1000,&ticket)==(APP_TEST?APP_RESUME_NOT_READY:0),
            "only a normal latched controller offers Resume authorization");
#if !APP_TEST
        uint32_t old=ticket;check(app_controller_poll()&APP_CTRL_LATCH,"arming alone cannot release STOP");
        check(app_controller_arm_resume(clock_ms+1000,&ticket)==0&&ticket!=old,
            "server issues a new one-use ticket instead of trusting a replayed client ID");
        check(app_controller_submit_resume(old,99,clock_ms+1000)==APP_RESUME_AUTH_CHANGED,
            "superseded authorization cannot resume");
        check(app_controller_submit_resume(ticket,99,clock_ms+1000)==0&&
              (app_controller_poll()&APP_CTRL_LATCH),"commit waits for current controller source checks");
        check(app_controller_poll_ready(1)==0&&app_controller_take_reply(reply)==E_TMOUT,
            "eligible explicit Resume releases latch but waits for motor handoff before reply");
        app_controller_publish(APP_CTRL_FOLLOWING,E_OK);
        check(app_controller_take_reply(reply)==E_OK&&(reply[9]&2U)&&
              (((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==0&&!(reply[8]&APP_CTRL_LATCH),
            "successful Resume reply identifies its command kind and released controller state");
        check(app_controller_submit_resume(ticket,99,clock_ms+1000)==APP_RESUME_AUTH_CHANGED,
            "consumed ticket is never usable again");
        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);
        check(app_controller_poll_ready(0)&APP_CTRL_LATCH,"invalid or blocked source checks retain STOP");
        app_controller_publish(APP_CTRL_STOP_LATCHED,E_OK);(void)app_controller_take_reply(reply);
        check((((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==APP_RESUME_NOT_READY,
            "not-ready is an explicit failure, not a fabricated successful resume");
        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);
        (void)app_controller_poll_ready(1);app_controller_publish(APP_CTRL_FOLLOWING,E_TMOUT);
        (void)app_controller_take_reply(reply);
        check((app_controller_poll()&APP_CTRL_LATCH)&&
              (((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==APP_RESUME_FAULT,
            "failed motor submission immediately restores inhibit and cannot report Resume success");
        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);
        (void)app_controller_poll_ready(1);blocked=1;
        app_controller_publish(APP_CTRL_FOLLOWING,E_TMOUT);blocked=0;
        check(app_controller_poll()&APP_CTRL_LATCH,
            "failed Resume handoff restores the latch even when publication is contended");
        app_controller_publish(APP_CTRL_STOP_LATCHED,E_OK);(void)app_controller_take_reply(reply);
        check((((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==APP_RESUME_FAULT,
            "contended publication preserves the failed Resume outcome until its reply is delivered");
        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);
        (void)app_controller_submit_stop(92,99,clock_ms+1000);
        (void)app_controller_poll_ready(1);app_controller_publish(APP_CTRL_STOP_LATCHED,E_OK);
        (void)app_controller_take_reply(reply);
        check(read32(reply)==92&&!(reply[9]&2U)&&(app_controller_poll()&APP_CTRL_LATCH),
            "STOP preempts a queued Resume in the one-slot controller mailbox");
        check(app_controller_submit_resume(ticket,99,clock_ms+1000)==APP_RESUME_AUTH_CHANGED,
            "STOP invalidates the previous authorization even after its own reply is drained");
        (void)app_controller_arm_resume(clock_ms+1000,&old);
        check(old!=ticket,"a replayed old arm cannot recreate the old server token after STOP");
        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        clock_ms+=2000;
        check(app_controller_submit_resume(ticket,99,clock_ms+1000)==APP_RESUME_AUTH_CHANGED,
            "unused authorization expires at its hard two-second boundary");
        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);clock_ms+=1000;
        (void)app_controller_poll_ready(1);app_controller_publish(APP_CTRL_STOP_LATCHED,E_OK);
        (void)app_controller_take_reply(reply);
        check((((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==APP_RESUME_EXPIRED&&(reply[8]&APP_CTRL_LATCH),
            "queued Resume expiry is checked again immediately before release");
        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);
        app_controller_cancel_resume();
        check((app_controller_poll_ready(1)&APP_CTRL_LATCH)&&read32(request_slot)==0,
            "connection cancellation discards unprocessed Resume and preserves the latch");
        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        app_controller_publish(APP_CTRL_FAULT,E_OK);
        check(app_controller_submit_resume(ticket,99,clock_ms+1000)==APP_RESUME_AUTH_CHANGED&&
              app_controller_arm_resume(clock_ms+1000,&ticket)==APP_RESUME_FAULT,
            "fault publication invalidates tickets and cannot be cleared by Resume");
        reset();clock_ms=0xfffffff0U;app_controller_publish(APP_CTRL_FOLLOWING,E_OK);latch();
        (void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);clock_ms=8;
        check(app_controller_poll_ready(1)==0,"Resume authorization and lease handle uint32 uptime wrap");
        app_controller_publish(APP_CTRL_FOLLOWING,E_OK);(void)app_controller_take_reply(reply);
        reset();latch();blocked=1;
        check(app_controller_arm_resume(clock_ms+1000,&ticket)==APP_RESUME_BUSY,
            "Resume authorization never waits behind a controller lock");blocked=0;

        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);
        app_controller_report_pickup();
        check(app_controller_poll_ready(1)&APP_CTRL_LATCH,"pickup wins over a queued Resume");
        app_controller_publish(APP_CTRL_FOLLOWING,E_OK);
        check(app_controller_take_reply(reply)==E_OK&&(reply[9]&2U)&&
              (((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==APP_RESUME_NOT_READY&&
              (reply[8]&APP_CTRL_LATCH),"queued Resume reports a refusal with the pickup latch held");
        check(app_controller_arm_resume(clock_ms+1000,&ticket)==APP_RESUME_NOT_READY&&
              app_controller_submit_resume(ticket,99,clock_ms+1000)==APP_RESUME_NOT_READY,
            "pickup refuses both fresh Resume authorizations and existing tickets");

        reset();latch();(void)app_controller_arm_resume(clock_ms+1000,&ticket);
        (void)app_controller_submit_resume(ticket,99,clock_ms+1000);
        check(app_controller_poll_ready(1)==0,"fixture releases Resume before pickup arrives");
        app_controller_report_pickup();
        check(app_controller_poll()&APP_CTRL_LATCH,"pickup before motor submission cancels in-flight Resume");
        app_controller_publish(APP_CTRL_FOLLOWING,E_OK);
        check(app_controller_take_reply(reply)==E_OK&&
              (((reply[8]|((unsigned)reply[9]<<8))>>6)&7U)==APP_RESUME_NOT_READY&&
              (reply[8]&APP_CTRL_LATCH),"a pickup during Resume prevents a success reply");
#endif
    }
    printf("[controller] SUMMARY %d checks, %d failures (TEST=%d)\n",checks,failures,APP_TEST);
    return failures?1:0;
}
