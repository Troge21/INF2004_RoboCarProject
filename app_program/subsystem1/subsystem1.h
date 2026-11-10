/* Subsystem 1 public interface. */
#ifndef SUBSYSTEM1_H
#define SUBSYSTEM1_H

#include "../app.h"
#include "s1_protocol.h"

#define S1_TASK_PRIORITY 12
#define S1_WIFI_LOG_ENABLED 0 /* set to 1 to restore WiFi status output */

/* Temporary S5 -> S1 bench interface; later supplied by the controller.
 * Nonblocking sends: E_TMOUT means full (new sample dropped).
 * Use s1_make_telemetry() to validate raw values before packing.
 * Distance zero encodes unavailable, never a clear-space distance. */
#define S1_PAYLOAD_CAPACITY S1_FRAME_BYTES
IMPORT ER s1_submit_telemetry(const S1_TELEMETRY *sample);
IMPORT INT s1_encode_telemetry(const S1_TELEMETRY *sample,
                              const S1_LINE_TELEMETRY *line, UW now_ms,
                              UB *out, UINT capacity);
/* Network task only: removes latest fresh payload, no waiting.
 * Returns exact binary length or negative error; no NUL terminator.
 * E_TMOUT means no fresh payload. Buffer must hold S1_PAYLOAD_CAPACITY bytes. */
IMPORT INT s1_network_poll(unsigned char *out, unsigned int capacity);

/* Network task only: read-only STATUS request -> current snapshot reply.
 * Does not call a sensor driver or control actuators; no waiting/queue send. */
IMPORT INT s1_network_status(const unsigned char *request, unsigned int length,
                            unsigned char *out, unsigned int capacity);

#endif /* SUBSYSTEM1_H */
