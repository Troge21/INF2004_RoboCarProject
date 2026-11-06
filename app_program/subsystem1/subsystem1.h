/* Subsystem 1 public interface. */
#ifndef SUBSYSTEM1_H
#define SUBSYSTEM1_H

#include "../app.h"

#define S1_TASK_PRIORITY 12
#define S1_WIFI_LOG_ENABLED 0 /* set to 1 to restore WiFi status output */

/* Temporary S5 -> S1 bench interface; later supplied by the controller.
 * Nonblocking sends: E_TMOUT means full (new sample dropped).
 * Invalid readings encode null, never a clear-space distance. */
#define S1_PAYLOAD_CAPACITY 128
typedef struct {
    UW sequence;
    UW obstacle_mm;
    UB obstacle_valid; /* exactly 0 or 1; valid distance 20..4000 mm */
} S1_TELEMETRY;
/* Packed line snapshot: bit 2 valid, bit 1 centre black, bit 0 right black.
 * Zero means unavailable; 4 means a valid all-background reading.
 * timestamp_ms is local metadata for freshness, not a wire field. */
#define S1_LINE_VALID 4U
typedef struct {
    UW timestamp_ms;
    UB bits;
} S1_LINE_TELEMETRY;
IMPORT ER s1_submit_telemetry(const S1_TELEMETRY *sample);
IMPORT INT s1_format_telemetry(const S1_TELEMETRY *sample,
                              const S1_LINE_TELEMETRY *line, UW now_ms,
                              char *out, INT capacity);
/* Network task only: removes latest fresh payload, no waiting.
 * Returns length or negative error; output is NUL terminated.
 * E_TMOUT means no fresh payload. Buffer must hold S1_PAYLOAD_CAPACITY bytes. */
IMPORT INT s1_network_poll(char *out, unsigned int capacity);

/* Proposed vocabulary for local tests, not an agreed wire protocol.
 * Parsing does not execute commands or establish freshness/authorization. */
typedef enum {
    S1_COMMAND_INVALID = 0,
    S1_COMMAND_STOP,
    S1_COMMAND_STATUS
} S1_COMMAND;

/* Exact case-sensitive byte span; no NUL terminator required.
 * Rejects extra bytes, whitespace, embedded NULs and incomplete payloads. */
S1_COMMAND s1_parse_command(const unsigned char *payload, unsigned int length);

#endif /* SUBSYSTEM1_H */
