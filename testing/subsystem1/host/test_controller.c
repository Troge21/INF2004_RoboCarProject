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
    printf("[controller] SUMMARY %d checks, %d failures (TEST=%d)\n",checks,failures,APP_TEST);
    return failures?1:0;
}
