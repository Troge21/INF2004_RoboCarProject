/*
 *	mqtt_utk.h
 *	MQTT client wiring for micro T-Kernel (RP2040, NO_SYS=1)
 */

#ifndef MQTT_UTK_H
#define MQTT_UTK_H
#include "../../../../app_program/subsystem1/s1_protocol.h"

/*
 * Network owner calls set_link before start/poll each iteration. While the
 * link is up, start begins the MQTT connection to
 * MQTT_BROKER_IP:MQTT_BROKER_PORT (config/wifi_secrets.h) and, once
 * connected, a periodic heartbeat publish on MQTT_TOPIC. Safe to call more
 * than once -- connecting/connected/backoff calls are no-ops.
 */
void mqtt_utk_start(void);

/* Called every service iteration, even offline to discard pending telemetry. */
void mqtt_utk_poll(void);
/* Network owner, outside the radio lock; WiFi/IP available = nonzero.
 * Down cancels pending work/timers and closes stale TCP. Idempotent. */
void mqtt_utk_set_link(unsigned int available);

/* S1 queue bridge ABI; deliberately no kernel or SDK headers in this file. */
int s1_network_poll(unsigned char *out, unsigned int capacity,uint32_t session);
int s1_network_aux(unsigned char *out,unsigned capacity,uint32_t session,const uint32_t counters[3]);
void s1_network_aux_result(unsigned marker,int accepted);
int s1_network_status(const unsigned char *request, unsigned int length,
                      unsigned char *out, unsigned int capacity,uint32_t session);
unsigned s1_network_controller_word(void);
int s1_network_uptime(uint32_t *out);
int s1_network_submit_stop(uint32_t id,uint32_t session,uint32_t deadline);
int s1_network_stop_reply(uint8_t out[10]);
/* Radio-owner implementation reads oscillator variation; no GPIO/clock writes.
 * Session freshness, not authentication or a cryptographic security claim. */
unsigned int mqtt_utk_session_entropy(void);

#endif /* MQTT_UTK_H */
