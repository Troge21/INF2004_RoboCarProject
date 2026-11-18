/* Minimal queue/connection view for testing the SDK-specific port adapter.
 * The ARM build uses the real SDK header, never this substitute. */
#ifndef HOST_MQTT_PRIV_H
#define HOST_MQTT_PRIV_H
#include "mqtt.h"
#include "lwip/altcp.h"
struct mqtt_request_t { struct mqtt_request_t *next; };
#endif
