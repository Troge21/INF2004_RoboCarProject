/* Shared controller/S1 contract. Plain types keep SDK/kernel namespaces apart.
 * S1 submits requests and reads snapshots; only the controller applies them. */
#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H
#include <stdint.h>
#define APP_CTRL_UNAVAILABLE 0U
#define APP_CTRL_DISABLED 1U
#define APP_CTRL_STARTING 2U
#define APP_CTRL_FOLLOWING 3U
#define APP_CTRL_RECOVERING 4U
#define APP_CTRL_WAITING_LINE 5U
#define APP_CTRL_STOP_LATCHED 6U
#define APP_CTRL_FAULT 7U
#define APP_CTRL_READY 8U
#define APP_CTRL_TURNING_LEFT 9U
#define APP_CTRL_TURNING_RIGHT 10U
#define APP_CTRL_SCANNING 11U
#define APP_CTRL_AVOIDING 12U
#define APP_CTRL_STOPPED 13U
#define APP_CTRL_STATE_MASK 15U
#define APP_CTRL_LATCH 16U
#define APP_CTRL_TEST 32U
#define APP_CTRL_WORD_MASK 63U
#define APP_CTRL_POLL_BUSY 64U
#define APP_CTRL_MAX_AGE_MS 500U
#define APP_CTRL_STOP_LEASE_MS 2000U
#define APP_STOP_LATCHED 0U
#define APP_STOP_EXPIRED 1U
#define APP_STOP_SESSION 2U
#define APP_STOP_REPLAY 3U
#define APP_STOP_BUSY 4U
#define APP_STOP_FAULT 5U
#define APP_RESUME_RELEASED 0U
#define APP_RESUME_EXPIRED 1U
#define APP_RESUME_SESSION 2U
#define APP_RESUME_REPLAY 3U
#define APP_RESUME_BUSY 4U
#define APP_RESUME_FAULT 5U
#define APP_RESUME_NOT_READY 6U
#define APP_RESUME_AUTH_CHANGED 7U
/* Private owner reply bit; converted to a distinct MQTT marker, not sent here. */
#define APP_REPLY_RESUME 0x200U
/* Snapshot: operating-time:32 LE + state:4/latch:1/TEST:1/reserved:2.
 * Reply: ID:32 + session:32 + control:6/result:3/reserved:7. */
int app_controller_init(void);
int app_controller_submit_stop(uint32_t id,uint32_t session,uint32_t deadline_ms);
/* S4 calls this after controller init when pickup is confirmed. Nonblocking
 * and idempotent. The controller applies zero wheel targets until reboot. */
void app_controller_report_pickup(void);
/* A fresh arm creates a one-use server ticket, invalidated by STOP. */
int app_controller_arm_resume(uint32_t deadline_ms,uint32_t *ticket);
int app_controller_submit_resume(uint32_t ticket,uint32_t session,uint32_t deadline_ms);
void app_controller_cancel_resume(void);
int app_controller_snapshot(uint8_t out[5]);
int app_controller_take_reply(uint8_t out[10]);
/* Controller task only, once before and once after its motor submission. */
unsigned app_controller_poll(void);
unsigned app_controller_poll_ready(int resume_ready);
void app_controller_publish(unsigned state,int motor_result);
#endif
