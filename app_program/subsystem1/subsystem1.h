/* S1 owns transport/serialization; producers retain all driver/actuator ownership. */
#ifndef SUBSYSTEM1_H
#define SUBSYSTEM1_H
#include "../app.h"
#include "../telemetry_sources.h"
#include "s1_protocol.h"
#define S1_TASK_PRIORITY 12
#define S1_GATHER_MS 40U
#define S1_PUBLISH_MS 400U
#define S1_PAYLOAD_CAPACITY S1_TX_CAPACITY
/* Startup-only registration (may wait) of successfully started application task reservations.
 * This does not claim current stack consumption or include kernel/radio tasks. */
void s1_resource_add_stack(uint32_t bytes);
uint32_t s1_resource_stack_capacity(void);
/* Radio-owner calls only. No GPIO/I2C/ADC reads, blocking waits or actuator calls. */
int s1_network_poll(unsigned char *out,unsigned capacity,uint32_t session);
int s1_network_aux(unsigned char *out,unsigned capacity,uint32_t session,
                     const uint32_t counters[3]);
void s1_network_aux_result(unsigned marker,int accepted);
int s1_network_status(const unsigned char *request,unsigned length,
                       unsigned char *out,unsigned capacity,uint32_t session);
unsigned s1_network_controller_word(void);
int s1_network_uptime(uint32_t *out);
int s1_network_submit_stop(uint32_t id,uint32_t session,uint32_t deadline);
int s1_network_stop_reply(uint8_t out[10]);
#endif
