/* Subsystem 1 public interface. */
#ifndef SUBSYSTEM1_H
#define SUBSYSTEM1_H

#include "../app.h"

#define S1_TASK_PRIORITY 12

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
